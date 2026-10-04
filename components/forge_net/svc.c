// External service health (see svc.h)
#include "svc.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_http_client.h"
#include "http_once.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_app_desc.h"
#include "net.h"

static const char *TAG = "svc";
#define PROBE_AFTER_US (5 * 60 * 1000000LL)

static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static svc_info_t svc[SVC_MAX];
static svc_probe_url_t probe_fn[SVC_MAX];
static int nsvc;
static volatile bool probe_running;

int svc_add(const char *name, const char *api, svc_probe_url_t probe)
{
    int id = -1;
    taskENTER_CRITICAL(&mux);
    if (nsvc < SVC_MAX) {
        id = nsvc++;
        svc[id] = (svc_info_t){ .name = name, .api = api };
        probe_fn[id] = probe;
    }
    taskEXIT_CRITICAL(&mux);
    if (id < 0) ESP_LOGE(TAG, "no room for service %s (SVC_MAX)", name);
    return id;
}

int svc_count(void) { return nsvc; }

static void record(int id, bool ok, const char *why, int64_t t0)
{
    if (id < 0 || id >= nsvc) return;
    int64_t now = esp_timer_get_time();
    bool changed;
    int fails;
    taskENTER_CRITICAL(&mux);
    svc_info_t *s = &svc[id];
    changed = s->last_try && s->ok != ok;
    s->last_try = now;
    s->ms = t0 ? (int)((now - t0) / 1000) : 0;
    s->ok = ok;
    s->probing = false;
    if (ok) { s->last_ok = now; s->fails = 0; }
    else { s->fails++; strlcpy(s->why, why, sizeof(s->why)); }
    fails = s->fails;
    taskEXIT_CRITICAL(&mux);
    if (changed || fails == 1) {               // log transitions only, not every retry
        if (ok) ESP_LOGI(TAG, "%s: OK again", svc[id].name);
        else ESP_LOGW(TAG, "%s: %s", svc[id].name, why);
    }
}

void svc_http(int id, esp_err_t err, int status, int64_t t0)
{
    if (err == ESP_OK && status == 200) { record(id, true, "", t0); return; }
    char why[40];
    if (err == ESP_OK) snprintf(why, sizeof(why), "HTTP %d", status);
    else if (err == ESP_ERR_HTTP_CONNECT) snprintf(why, sizeof(why), "can't connect");
    else if (err == ESP_ERR_HTTP_EAGAIN || err == ESP_ERR_TIMEOUT) snprintf(why, sizeof(why), "timeout");
    else if (err == ESP_ERR_HTTP_FETCH_HEADER) snprintf(why, sizeof(why), "no reply");
    else snprintf(why, sizeof(why), "%s", esp_err_to_name(err));
    record(id, false, why, t0);
}

void svc_ok(int id, int64_t t0) { record(id, true, "", t0); }
void svc_fail(int id, const char *why, int64_t t0) { record(id, false, why, t0); }

void svc_get(int id, svc_info_t *out)
{
    memset(out, 0, sizeof(*out));
    if (id < 0 || id >= nsvc) return;
    taskENTER_CRITICAL(&mux);
    *out = svc[id];
    taskEXIT_CRITICAL(&mux);
}

const char *svc_user_agent(void)
{
    static char ua[128];                       // filled once; two tasks racing write the same bytes
    if (!ua[0]) {
        const char *v = esp_app_get_description()->version;
        snprintf(ua, sizeof(ua), "%s/%s (+https://github.com/%s)", esp_app_get_description()->project_name,
                 v[0] == 'v' ? v + 1 : v, CONFIG_FORGE_REPO);
        ESP_LOGI(TAG, "User-Agent: %s", ua);
    }
    return ua;
}

static void probe_task(void *arg)
{
    char url[200];
    for (int i = 0; i < nsvc; i++) {
        bool due;
        taskENTER_CRITICAL(&mux);
        due = svc[i].probing;
        taskEXIT_CRITICAL(&mux);
        if (!due) continue;
        probe_fn[i](url, sizeof(url));
        esp_http_client_config_t cfg = {
            .url = url, .crt_bundle_attach = esp_crt_bundle_attach, .timeout_ms = 10000,
            .user_agent = svc_user_agent(),
        };
        int64_t t0 = esp_timer_get_time();
        int status;
        esp_err_t err = http_once(&cfg, &status);         // body read and dropped
        svc_http(i, err, status, t0);
        ESP_LOGI(TAG, "probe %s: %s, HTTP %d, %d ms", svc[i].name, esp_err_to_name(err), status,
                 (int)((esp_timer_get_time() - t0) / 1000));
    }
    probe_running = false;
    vTaskDelete(NULL);
}

void svc_probe_stale(void)
{
    if (probe_running || !net_is_connected()) return;
    int64_t now = esp_timer_get_time();
    int n = 0;
    taskENTER_CRITICAL(&mux);
    for (int i = 0; i < nsvc; i++) {
        svc[i].probing = probe_fn[i] && (!svc[i].last_try || now - svc[i].last_try > PROBE_AFTER_US);
        n += svc[i].probing;
    }
    taskEXIT_CRITICAL(&mux);
    if (!n) return;
    probe_running = true;
    if (xTaskCreate(probe_task, "svc_probe", 8192, NULL, 2, NULL) != pdPASS) {
        ESP_LOGW(TAG, "probe task not started");
        taskENTER_CRITICAL(&mux);
        for (int i = 0; i < nsvc; i++) svc[i].probing = false;
        taskEXIT_CRITICAL(&mux);
        probe_running = false;
    }
}
