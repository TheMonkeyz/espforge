// What main.c and the screens ask of the rest of the firmware that isn't the network, answered for the browser: no
// diagnostics or test console (the log is the browser's console). The board's microphones and motion sensor
// (board_audio.h, imu.h) answer "not there": forge_presence's real presence.c runs with its state machine idle, so the
// screen stays on (and the settings page, with forge_net, shows "No microphone"). Wi-Fi, updates and the web server:
// emu_net.c, built unless the app's Makefile says USE_NET := 0 (emu.mk).
#include <stdio.h>
#include <string.h>
#include <emscripten.h>
#include "esp_timer.h"
#include "esp_err.h"
#include "esp_system.h"
#include "esp_app_desc.h"
#include "diag.h"
#include "nvs_util.h"
#include "testcon.h"
#include "board_audio.h"
#include "imu.h"

const char *esp_err_to_name(esp_err_t e)
{
    switch (e) {
    case ESP_OK: return "ESP_OK";
    case ESP_ERR_NO_MEM: return "ESP_ERR_NO_MEM";
    case ESP_ERR_TIMEOUT: return "ESP_ERR_TIMEOUT";
    case ESP_ERR_HTTP_CONNECT: return "ESP_ERR_HTTP_CONNECT";
    default: return "ESP_FAIL";
    }
}

int64_t esp_timer_get_time(void) { return (int64_t)(emscripten_get_now() * 1000.0); }

// The version and the app's name (forge_settings' Version row, /api/info): the emulator's build
const esp_app_desc_t *esp_app_get_description(void)
{
    static esp_app_desc_t d;
    if (!d.version[0]) {
        snprintf(d.version, sizeof(d.version), "%s", EMU_VERSION);
        snprintf(d.project_name, sizeof(d.project_name), "%s", EMU_APP);
    }
    return &d;
}

/* ---------- diagnostics and the test console: the board's serial port, not here ---------- */
void diag_start(int period_s) { (void)period_s; }
void diag_mark(const char *stage) { (void)stage; }
uint32_t diag_failed_allocs(void) { return 0; }
void diag_add_hook(diag_hook_t hook) { (void)hook; }
void testcon_add_where(testcon_where_fn_t fn) { (void)fn; }
void testcon_start(void) {}

/* ---------- the board's microphones and motion sensor: not there (presence.c runs without them) ---------- */
bool board_audio_init(bool speaker) { (void)speaker; return false; }
bool board_mic_open(float gain_db) { (void)gain_db; return false; }
bool board_mic_read(int16_t *samples, size_t n) { (void)samples; (void)n; return false; }
const void *board_audio_data_if(void) { return NULL; }
bool imu_init(i2c_master_bus_handle_t bus) { (void)bus; return false; }
bool imu_read(float g[3]) { (void)g; return false; }

// forge_core's NVS check (forge_core's i18n_nvs.c, the app's saves): emu_nvs.c never fails
bool nvs_check(esp_err_t err, const char *what) { (void)what; return err == ESP_OK; }
// forge_core's nvs_init (an app with no Wi-Fi calls it; forge_net's net_init did it): emu_nvs.c needs none
void nvs_init(void) {}
