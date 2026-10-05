// Wi-Fi station with credentials in NVS, plus a SoftAP setup page to enter them
#include "net.h"
#include "svc.h"
#include "nvs_util.h"
#include "testcon.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "dns_server.h"
#include "esp_dpp.h"
#include "esp_idf_version.h"
#include "esp_attr.h"
#include "esp_random.h"
#include <ctype.h>

static const char *TAG = "net";
static EventGroupHandle_t ev;
#define BIT_GOT_IP  BIT0
#define BIT_FAIL    BIT1
static int retries;
static bool portal_mode;
static esp_netif_t *sta_netif;
static esp_timer_handle_t retry_timer;
static volatile bool ap_active;
static volatile bool dpp_active;

int net_ap_clients(void)
{
    wifi_sta_list_t l;
    return ap_active && esp_wifi_ap_get_sta_list(&l) == ESP_OK ? l.num : 0;
}

// Reconnect attempts slow down (1 s for the first 8, then 3 s, then every 30 s) but don't give up: the router may
// come back after a power cut. They pause while a setup mode is on (setup network, Easy Connect, first-time
// portal): an attempt makes the radio hop channels, so phones couldn't join the setup network or reach Easy
// Connect. Closing setup tries the saved network again (resume_saved).
static bool setup_on(void) { return portal_mode || dpp_active || ap_active; }

static void retry_cb(void *arg)
{
    if (setup_on() || net_is_connected()) return;
    esp_wifi_connect();
}

static void pause_saved(void)                                // a setup mode starts: no more attempts
{
    esp_timer_stop(retry_timer);
    if (!net_is_connected()) {
        esp_wifi_disconnect();                               // also cancels an attempt in progress
        ESP_LOGI(TAG, "Setup open: not trying the saved network meanwhile");
    }
}

static void resume_saved(void)                               // a setup mode ends
{
    if (setup_on() || net_is_connected()) return;
    retries = 0;
    ESP_LOGI(TAG, "Setup closed: trying the saved network again");
    esp_timer_stop(retry_timer);                             // after 1 s: switching setup pages stops one mode
    esp_timer_start_once(retry_timer, 1000 * 1000ULL);       // and starts the other just after
}

static int svc_ntp = -1;
static void ntp_synced(struct timeval *tv) { svc_ok(svc_ntp, 0); }

// Fixed-size driver fields: a 32-byte name fills ssid[32] with no NUL (strlcpy kept 31 bytes: a legal 32-character
// network was saved but never joined); copy with the length, and read back with the length too
static void put_field(uint8_t *dst, size_t cap, const char *src)
{
    size_t n = strlen(src);
    if (n > cap) n = cap;
    memset(dst, 0, cap);
    memcpy(dst, src, n);
}

static void get_field(char *out, size_t n, const uint8_t *src, size_t cap)
{
    size_t k = strnlen((const char *)src, cap);
    if (k >= n) k = n - 1;
    memcpy(out, src, k);
    out[k] = 0;
}

bool net_creds_valid(const char *ssid, const char *pass)
{
    size_t s = strlen(ssid), p = strlen(pass);
    if (s < 1 || s > NET_SSID_MAX) return false;
    if (p == 0 || (p >= 8 && p <= 63)) return true;
    if (p != 64) return false;
    for (size_t i = 0; i < p; i++) if (!isxdigit((unsigned char)pass[i])) return false;
    return true;                                             // 64 hex digits: the key itself
}

/* The setup network's password, per display: 8 letters and digits without look-alikes (0/o, 1/l/i), easy to type
 * from the screen. A fixed password printed in the README let anyone nearby join it during a router outage. */
static char ap_pass[9];

const char *net_setup_ap_pass(void)
{
    if (ap_pass[0]) return ap_pass;
    nvs_handle_t h;
    size_t n = sizeof(ap_pass);
    bool open = nvs_open("setup", NVS_READWRITE, &h) == ESP_OK;
    if (open && nvs_get_str(h, "pass", ap_pass, &n) == ESP_OK && strlen(ap_pass) == 8) { nvs_close(h); return ap_pass; }
    static const char abc[] = "abcdefghjkmnpqrstuvwxyz23456789";
    for (int i = 0; i < 8; i++) ap_pass[i] = abc[esp_random() % (sizeof(abc) - 1)];
    ap_pass[8] = 0;
    bool saved = open && nvs_set_str(h, "pass", ap_pass) == ESP_OK && nvs_commit(h) == ESP_OK;
    if (open) nvs_close(h);
    ESP_LOGI(TAG, "New setup network password%s", saved ? "" : " (not saved)");
    return ap_pass;
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (!setup_on()) esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(ev, BIT_GOT_IP);
        if (setup_on()) return;
        retries++;
        if (retries == 8) xEventGroupSetBits(ev, BIT_FAIL);      // net_wait() gives up; retries go on
        int ms = retries < 8 ? 1000 : retries < 20 ? 3000 : 30000;
        if (retries == 20) ESP_LOGW(TAG, "Still no Wi-Fi, retrying every 30 s");
        esp_timer_stop(retry_timer);
        esp_timer_start_once(retry_timer, ms * 1000ULL);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = data;
        ESP_LOGI(TAG, "Connected, IP " IPSTR, IP2STR(&e->ip_info.ip));
        retries = 0;
        xEventGroupClearBits(ev, BIT_FAIL);
        xEventGroupSetBits(ev, BIT_GOT_IP);
        static bool sntp;
        if (!sntp) {                                             // clock via SNTP, once
            sntp = true;
            esp_sntp_config_t sc = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
            sc.sync_cb = ntp_synced;
            esp_netif_sntp_init(&sc);
        }
    }
}

static void cmd_wifi(int argc, char **argv);

void net_init(void)
{
    nvs_init();
#if CONFIG_ESP_WIFI_DEBUG_PRINT
    // Debug builds only (docs/TESTING.md, "Easy Connect steps"): the supplicant's DPP steps are DEBUG lines; the
    // build also needs CONFIG_LOG_MAXIMUM_LEVEL_DEBUG. (ESP-IDF 5.5 renamed CONFIG_WPA_DEBUG_PRINT to this.)
    esp_log_level_set("wpa", ESP_LOG_DEBUG);
#endif
    testcon_register("wifi", "wifi status|offline|online|offline-boot|offline-boot-short", cmd_wifi);
    svc_ntp = svc_add(SVC_NAME_NTP, "SNTP", NULL);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ev = xEventGroupCreate();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL);
    const esp_timer_create_args_t ta = { .callback = retry_cb, .name = "wifi_retry" };
    esp_timer_create(&ta, &retry_timer);
}

bool net_load_creds(char *ssid, size_t sl, char *pass, size_t pl)
{
    nvs_handle_t h;
    if (nvs_open("wifi", NVS_READONLY, &h) != ESP_OK) return false;
    bool ok = nvs_get_str(h, "ssid", ssid, &sl) == ESP_OK && nvs_get_str(h, "pass", pass, &pl) == ESP_OK;
    nvs_close(h);
    return ok && ssid[0];
}

void net_clear_creds(void)
{
    nvs_handle_t h;
    if (nvs_open("wifi", NVS_READWRITE, &h) == ESP_OK) { nvs_erase_all(h); nvs_commit(h); nvs_close(h); }
    ESP_LOGW(TAG, "Wi-Fi credentials cleared");
}

/* ---------------- test console hooks (testcon.c) ----------------
 * "Saved network unreachable" without touching the saved credentials: the station is given a network name that
 * doesn't exist. offline-boot keeps a flag in RTC memory (survives esp_restart, not a power cut) so the next boot
 * takes the real start-up path (Connecting... -> 30 s -> offline setup), which is where the October 1 bugs were. */
#define TEST_SSID SETUP_AP_SSID "-Test-Unreachable"       // a name nobody has ("Forge-Setup-Test-Unreachable")
#define TEST_MAGIC 0x0FF11E55u
static RTC_NOINIT_ATTR uint32_t test_offline_boot;
#define TEST_SHORT 0x5407u                      // with TEST_MAGIC in the high half: also a short automatic setup
static bool short_setup;
bool net_test_short_setup(void) { return short_setup; }

static void sta_config(const char *ssid, const char *pass)
{
    wifi_config_t wc = {0};
    put_field(wc.sta.ssid, sizeof(wc.sta.ssid), ssid);
    put_field(wc.sta.password, sizeof(wc.sta.password), pass);
    wc.sta.threshold.authmode = pass[0] ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
    esp_err_t e = esp_wifi_set_config(WIFI_IF_STA, &wc);
    if (e != ESP_OK) ESP_LOGE(TAG, "station config not set: %s", esp_err_to_name(e));
}

void net_test_offline_next_boot(bool short_setup) { test_offline_boot = short_setup ? TEST_MAGIC ^ TEST_SHORT : TEST_MAGIC; }

void net_test_offline(void)
{
    ESP_LOGW(TAG, "TEST: saved network replaced by \"" TEST_SSID "\" until 'wifi online' or a restart");
    esp_timer_stop(retry_timer);
    esp_wifi_disconnect();                        // the disconnect event schedules retries (to the fake network)
    sta_config(TEST_SSID, "unreachable");
}

void net_test_online(void)
{
    char ssid[33] = "", pass[65] = "";
    if (!net_load_creds(ssid, sizeof(ssid), pass, sizeof(pass))) return;
    ESP_LOGW(TAG, "TEST: saved network \"%s\" restored", ssid);
    // Disconnect first: it also cancels an attempt in progress. esp_wifi_set_config() refuses while the station is
    // connecting ("sta is connecting, cannot set config"), and the device then kept trying the fake network forever
    // (espforge harness, wifi_runtime, October 4)
    esp_timer_stop(retry_timer);
    esp_wifi_disconnect();
    sta_config(ssid, pass);
    if (setup_on() || net_is_connected()) return;  // setup open: tried once it closes (resume_saved)
    retries = 0;
    esp_wifi_connect();
}

void net_test_info(char *out, size_t n)
{
    wifi_config_t wc = {0};
    esp_wifi_get_config(WIFI_IF_STA, &wc);
    uint8_t ch = 0; wifi_second_chan_t sc;
    esp_wifi_get_channel(&ch, &sc);
    char ssid[NET_SSID_MAX + 1];
    get_field(ssid, sizeof(ssid), wc.sta.ssid, sizeof(wc.sta.ssid));
    snprintf(out, n, "connected=%d sta_ssid=%s portal=%d ap=%d ap_clients=%d dpp=%d retries=%d channel=%u ap_pass=%s",
             net_is_connected(), ssid, portal_mode, ap_active, net_ap_clients(), dpp_active, retries, ch,
             net_setup_ap_pass());
}

void net_begin(const char *ssid, const char *pass)
{
    if (!sta_netif) sta_netif = esp_netif_create_default_wifi_sta();
    if (test_offline_boot == TEST_MAGIC || test_offline_boot == (TEST_MAGIC ^ TEST_SHORT)) {   // one boot only
        short_setup = test_offline_boot != TEST_MAGIC;
        test_offline_boot = 0;
        ESP_LOGW(TAG, "TEST: this boot uses \"" TEST_SSID "\" instead of \"%s\"", ssid);
        ssid = TEST_SSID;
        pass = "unreachable";
    }
    wifi_config_t wc = {0};
    put_field(wc.sta.ssid, sizeof(wc.sta.ssid), ssid);
    put_field(wc.sta.password, sizeof(wc.sta.password), pass);
    wc.sta.threshold.authmode = pass[0] ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wc);
    ESP_LOGI(TAG, "Connecting to \"%s\"...", ssid);
    esp_wifi_start();
}

bool net_wait(int timeout_ms)
{
    EventBits_t b = xEventGroupWaitBits(ev, BIT_GOT_IP | BIT_FAIL, pdFALSE, pdFALSE,
                                        timeout_ms < 0 ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms));
    if (b & BIT_GOT_IP) return true;
    ESP_LOGW(TAG, "Could not connect (still retrying in the background)");
    return false;
}

bool net_wait_connected(int timeout_ms)
{
    return xEventGroupWaitBits(ev, BIT_GOT_IP, pdFALSE, pdFALSE,
                               timeout_ms < 0 ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms)) & BIT_GOT_IP;
}

bool net_is_connected(void) { return ev && (xEventGroupGetBits(ev) & BIT_GOT_IP); }

/* ---------------- Setup portal (AP) + helpers for the web UI ---------------- */

bool net_save_creds(const char *ssid, const char *pass)
{
    nvs_handle_t h;
    if (!net_creds_valid(ssid, pass) || nvs_open("wifi", NVS_READWRITE, &h) != ESP_OK) return false;
    bool ok = nvs_set_str(h, "ssid", ssid) == ESP_OK && nvs_set_str(h, "pass", pass) == ESP_OK && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    if (ok) ESP_LOGI(TAG, "Saved credentials for \"%s\"", ssid);
    else ESP_LOGE(TAG, "Credentials for \"%s\" NOT saved (NVS)", ssid);
    return ok;
}

bool net_get_ssid(char *out, size_t n)
{
    wifi_config_t wc;
    if (esp_wifi_get_config(WIFI_IF_STA, &wc) != ESP_OK) return false;
    get_field(out, n, wc.sta.ssid, sizeof(wc.sta.ssid));
    return out[0] != 0;
}

bool net_get_ip(char *out, size_t n)
{
    esp_netif_t *nif = portal_mode ? esp_netif_get_handle_from_ifkey("WIFI_AP_DEF") : sta_netif;
    esp_netif_ip_info_t ip;
    if (!nif || esp_netif_get_ip_info(nif, &ip) != ESP_OK || ip.ip.addr == 0) return false;
    snprintf(out, n, IPSTR, IP2STR(&ip.ip));
    return true;
}

bool net_in_portal(void) { return portal_mode; }

static esp_netif_t *ap_netif;
static dns_server_handle_t dns;

// Bring up the setup access point with a captive portal (DNS answers everything with us,
// DHCP option 114 advertises the setup page). Keeps the station connection if there is one.
static void ap_up(void)
{
    if (!ap_netif) ap_netif = esp_netif_create_default_wifi_ap();
    // WPA2 + WPA3 (SAE): a phone that can uses WPA3, whose handshake can't be cracked offline from a capture
    wifi_config_t ap = {
        .ap = { .ssid = SETUP_AP_SSID, .ssid_len = sizeof(SETUP_AP_SSID) - 1, .max_connection = 3,
                .authmode = WIFI_AUTH_WPA2_WPA3_PSK, .channel = 6, .pmf_cfg = { .capable = true } },
    };
    put_field(ap.ap.password, sizeof(ap.ap.password), net_setup_ap_pass());
    esp_wifi_set_mode(WIFI_MODE_APSTA);   // AP follows the station's channel when connected
    esp_wifi_set_config(WIFI_IF_AP, &ap);

    static char uri[] = "http://192.168.4.1/";
    esp_netif_dhcps_stop(ap_netif);
    esp_netif_dhcps_option(ap_netif, ESP_NETIF_OP_SET, ESP_NETIF_CAPTIVEPORTAL_URI, uri, strlen(uri));
    esp_netif_dhcps_start(ap_netif);

    if (!dns) {
        dns_server_config_t cfg = DNS_SERVER_CONFIG_SINGLE("*", "WIFI_AP_DEF");
        dns = start_dns_server(&cfg);
    }
    ap_active = true;
    pause_saved();
}

void net_start_portal(void)
{
    portal_mode = true;
    esp_wifi_stop();
    if (!sta_netif) sta_netif = esp_netif_create_default_wifi_sta();
    ap_up();
    esp_wifi_start();
    ESP_LOGI(TAG, "Setup portal up: join \"%s\" (password on the screen)", SETUP_AP_SSID);
}

void net_setup_ap_start(void)
{
    if (ap_active) return;
    ap_up();
    ESP_LOGI(TAG, "Setup AP started alongside the current connection");
}

static void ap_down(void)
{
    if (!ap_active) return;
    // The DNS server stays up: stop_dns_server() deletes its task without closing the socket, so port 53 stayed
    // taken and the next setup network had no DNS (no captive portal). It is bound to the setup AP's address
    // (192.168.4.1), so it answers phones on the setup network only, not the home network.
    esp_wifi_set_mode(WIFI_MODE_STA);
    ap_active = false;
    ESP_LOGI(TAG, "Setup AP stopped");
    resume_saved();
}

void net_setup_ap_stop(void)
{
    if (!portal_mode) ap_down();
}

void net_setup_ap_stop_any(void) { ap_down(); }

/* ---------------- Wi-Fi Easy Connect (DPP enrollee) ----------------
 * The display shows a DPP QR code; an Android phone (10+) scans it (camera or any QR scanner) and sends
 * the network it's connected to (SSID + password). No connection attempts while it listens (they made the radio hop
 * channels), and the setup AP held on its channel (dpp_hold_channel: without it the phone's confirmation was missed). */

// Listen on ONE channel: the saved network's if it is in range, else the strongest network's. (weather_amoled
// concluded the phone must be on that channel, as Easy Connect "only worked online"; the phone's own log showed the
// real cause, October 4: the phone visits the channel fine, but its confirmation was not ACKed because nothing held
// the display's radio there. See dpp_hold_channel.)
static char dpp_chan[4] = "6";

// One scan; returns the channel of the strongest 2.4 GHz record (of `ssid` if given), 0 if none. *seen = records.
static int scan_channel(const char *ssid, int dwell_ms, int *seen)
{
    wifi_scan_config_t sc = { .ssid = (uint8_t *)ssid, .scan_type = WIFI_SCAN_TYPE_ACTIVE,
                              .scan_time.active = { .min = dwell_ms / 2, .max = dwell_ms } };
    uint16_t n = 16;
    wifi_ap_record_t *r = calloc(n, sizeof(*r));
    int ch = 0;
    if (r && esp_wifi_scan_start(&sc, true) == ESP_OK && esp_wifi_scan_get_ap_records(&n, r) == ESP_OK) {
        for (int i = 0; i < n && !ch; i++)                  // records come sorted by signal
            if (r[i].primary >= 1 && r[i].primary <= 13) ch = r[i].primary;
    } else {
        n = 0;
    }
    free(r);
    *seen = n;
    return ch;
}

// connected: the channel of the network the station is on (no scan: it took ~2 s before the QR code appeared,
// espforge October 4); 0 when offline
static void dpp_pick_channel(int connected)
{
    char saved[33] = "", pass[65];
    net_load_creds(saved, sizeof(saved), pass, sizeof(pass));
    int seen = 0, ch = 0;
    const char *why = "default";
    // The scan is kept even when the channel is known from the connection: skipping it (QR code in 0.15 s instead of
    // ~2 s) made every Easy Connect attempt time out (ESP_ERR_DPP_AUTH_TIMEOUT, 2 tries, October 4), while the
    // attempts after a scan had worked. The app shows a placeholder code meanwhile (main/ui.c).
    // The saved network first, by name: a probe request carrying its name is answered more reliably than a broadcast
    // one, and only its records come back (a broadcast scan keeps the 16 strongest). The broadcast scan at 40-80 ms per
    // channel missed a router on a busy channel (v1.10.0 harness run: it picked the strongest network, channel 11).
    if (saved[0] && (ch = scan_channel(saved, 120, &seen))) why = "saved network";      // ~1.6 s
    else if (connected) { ch = connected; why = "the network it was connected to"; }
    else if ((ch = scan_channel(NULL, 80, &seen))) why = "strongest network";          // ~1 s more
    if (ch < 1 || ch > 13) ch = 6;
    snprintf(dpp_chan, sizeof(dpp_chan), "%d", ch);
    ESP_LOGI(TAG, "Easy Connect: channel %d (%s, %d networks seen)", ch, why, seen);
}

/* The setup AP runs on the Easy Connect channel while it listens, to keep the radio parked there. ESP-IDF stops listening
 * when the phone's request arrives (ROC cancelled), computes the answer (~0.24 s), and sends it with a short wait on
 * the channel; the phone confirms ~7 ms after receiving it. With the station off every network nothing held the
 * radio on the channel, and the phone's log showed its confirmation not ACKed: Auth Confirm timeout, every time with
 * a Pixel 8 Pro (October 4; ESP-IDF issues #12151, #17672 report the same). An AP never leaves its channel. */
static void dpp_hold_channel(int ch)
{
    if (!ap_active) ap_up();
    wifi_config_t c;
    if (esp_wifi_get_config(WIFI_IF_AP, &c) == ESP_OK && c.ap.channel != ch) {
        c.ap.channel = ch;
        esp_wifi_set_config(WIFI_IF_AP, &c);
    }
    ESP_LOGI(TAG, "Easy Connect: setup AP held on channel %d (keeps the radio there for the phone's confirmation)", ch);
}

static net_dpp_uri_cb_t dpp_uri_cb;
// Easy Connect starts in steps that finish in the supplicant's own task: bootstrap_gen -> URI_READY -> start_listen ->
// the listen itself. Deinit before the listen ran made it use a deleted event group: assert in dpp_listen_start, a
// restart (espforge, a setup page switched back within a second, October 4). net_dpp_stop waits for the listen.
static volatile int64_t dpp_started_us, dpp_listen_us;
static net_dpp_done_cb_t dpp_done_cb;
static bool dpp_inited;

static void (*restart_fn)(void) = esp_restart;
void net_set_restart(void (*fn)(void)) { restart_fn = fn ? fn : esp_restart; }
void net_restart(void) { restart_fn(); }
static void restart_cb(void *arg) { restart_fn(); }   // forge_ota: not in an update's first minute

static void dpp_event(esp_supp_dpp_event_t evt, void *data)
{
    switch (evt) {
    case ESP_SUPP_DPP_URI_READY:
        if (data) {
            ESP_LOGI(TAG, "Easy Connect: QR code ready, listening on channel %s", dpp_chan);
            if (dpp_uri_cb) dpp_uri_cb((const char *)data);
            // bootstrap_gen() is asynchronous: listening is only possible once the code exists
            esp_err_t e = dpp_active ? esp_supp_dpp_start_listen() : ESP_OK;
            if (e != ESP_OK) ESP_LOGE(TAG, "Easy Connect: can't listen: %s", esp_err_to_name(e));
            dpp_listen_us = esp_timer_get_time();
        }
        break;
    case ESP_SUPP_DPP_CFG_RECVD: {
        // The driver's fields aren't NUL-terminated when full: a 32-byte name ran into the password (in the log and
        // in NVS)
        wifi_config_t *wc = data;
        char ssid[NET_SSID_MAX + 1], pass[NET_PASS_MAX + 1];
        get_field(ssid, sizeof(ssid), wc->sta.ssid, sizeof(wc->sta.ssid));
        get_field(pass, sizeof(pass), wc->sta.password, sizeof(wc->sta.password));
        ESP_LOGI(TAG, "Easy Connect: received \"%s\" from the phone", ssid);
        bool ok = net_save_creds(ssid, pass);
        if (dpp_done_cb) dpp_done_cb(ok, ssid);
        // This runs on the system event task, which restarts 2.5 s later: the only chance to see its stack use
        ESP_LOGI(TAG, "Easy Connect: event task stack %u B spare", (unsigned)uxTaskGetStackHighWaterMark(NULL));
        if (ok) {                                            // same as the setup page: restart and join
            static esp_timer_handle_t t;
            const esp_timer_create_args_t ta = { .callback = restart_cb, .name = "dpp_restart" };
            if (!t && esp_timer_create(&ta, &t) == ESP_OK) esp_timer_start_once(t, 2500 * 1000);
        }
        break;
    }
    case ESP_SUPP_DPP_FAIL: {
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)
        int why = data ? ((wifi_event_dpp_failed_t *)data)->failure_reason : ESP_FAIL;   // an event struct since 5.5
#else
        int why = (int)(intptr_t)data;
#endif
        ESP_LOGW(TAG, "Easy Connect failed (%s, 0x%x), listening again", esp_err_to_name(why), why);
        if (dpp_done_cb) dpp_done_cb(false, "");
        if (dpp_active) esp_supp_dpp_start_listen();
        break;
    }
    default:
        break;
    }
}

bool net_dpp_start(net_dpp_uri_cb_t on_uri, net_dpp_done_cb_t on_done)
{
    if (dpp_active) return true;
    dpp_uri_cb = on_uri;
    dpp_done_cb = on_done;
    dpp_active = true;
    dpp_started_us = esp_timer_get_time();
    dpp_listen_us = 0;
    wifi_ap_record_t ap;                                   // the router's channel, while still connected to it
    int connected = net_is_connected() && esp_wifi_sta_get_ap_info(&ap) == ESP_OK ? ap.primary : 0;
    esp_timer_stop(retry_timer);
    esp_wifi_disconnect();                                 // listening needs the radio (also cancels an attempt)
    ESP_LOGI(TAG, "Easy Connect: not trying the saved network meanwhile");
    // Listen only once the station has really left (it took ~2 ms here, but the listen must not race it) and with
    // power save off: the radio must be awake on the channel for the phone's request (an AUTH_TIMEOUT followed the
    // first attempt made straight from a connection, October 4; the attempts after a 2 s scan had worked)
    for (int i = 0; i < 50 && net_is_connected(); i++) vTaskDelay(pdMS_TO_TICKS(20));
    esp_wifi_set_ps(WIFI_PS_NONE);
    dpp_pick_channel(connected);
    dpp_hold_channel(atoi(dpp_chan));
    esp_err_t err = ESP_OK;
    if (!dpp_inited) {
        err = esp_supp_dpp_init(dpp_event);
        dpp_inited = err == ESP_OK;
    }
    if (err == ESP_OK) err = esp_supp_dpp_bootstrap_gen(dpp_chan, DPP_BOOTSTRAP_QR_CODE, NULL, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Easy Connect unavailable: %s", esp_err_to_name(err));
        dpp_started_us = 0;                                // nothing pending: no wait in net_dpp_stop
        net_dpp_stop();
        return false;
    }
    ESP_LOGI(TAG, "Easy Connect started");
    return true;
}

void net_dpp_stop(void)
{
    if (!dpp_active) return;
    // Not from the task that delivers DPP events (the wait would block the event it waits for): callers are the app's
    // tasks. At most 3 s for the QR code, then 300 ms for the listen it starts.
    while (dpp_started_us && !dpp_listen_us && esp_timer_get_time() - dpp_started_us < 3000000) vTaskDelay(pdMS_TO_TICKS(50));
    while (dpp_listen_us && esp_timer_get_time() - dpp_listen_us < 300000) vTaskDelay(pdMS_TO_TICKS(50));
    esp_supp_dpp_stop_listen();
    if (dpp_inited) { esp_supp_dpp_deinit(); dpp_inited = false; }
    dpp_active = false;
    dpp_uri_cb = NULL;
    dpp_done_cb = NULL;
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);                    // ESP-IDF's default for a station
    ESP_LOGI(TAG, "Easy Connect stopped");
    resume_saved();
}

bool net_dpp_active(void) { return dpp_active; }

bool net_setup_ap_active(void) { return ap_active; }

// Test console: "wifi status|offline|online|offline-boot[-short]" (docs/PROTOCOL.md §2, harness wifi suites)
static void cmd_wifi(int argc, char **argv)
{
    EXT_RAM_BSS_ATTR static char info[200];              // (PSRAM: L185)
    const char *a = argc == 2 ? argv[1] : "";
    if (!strcmp(a, "offline")) net_test_offline();
    else if (!strcmp(a, "online")) net_test_online();
    else if (!strcmp(a, "offline-boot") || !strcmp(a, "offline-boot-short")) {
        net_test_offline_next_boot(a[12] != 0);
        ESP_LOGI("test", "ok restarting; the next boot can't reach the saved network");
        vTaskDelay(pdMS_TO_TICKS(200));
        esp_restart();
    } else if (strcmp(a, "status")) {
        ESP_LOGW("test", "error wifi: status, offline, offline-boot[-short], online");
        return;
    }
    net_test_info(info, sizeof(info));
    ESP_LOGI("test", "wifi %s", info);
}
