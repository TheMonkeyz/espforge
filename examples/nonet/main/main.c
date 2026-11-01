// espforge's no-Wi-Fi starter: the start-up order of an app without forge_net, forge_ota or dns_server (a Bluetooth
// LE app, as esp32-s3-colormesh, or one with no radio at all). Next to the Wi-Fi starter (main/main.c at the top of
// this repository), what changes:
//   - NVS: the app calls nvs_init() (forge_core) first; net_init() did it in the Wi-Fi starter.
//   - no web_set_page / web_add_routes / web_start, no presence_web_routes, no settings page on a phone;
//   - no ota_start: no "ota: Running" line (diag's "firmware" and "running from ota_N" lines say which image booted;
//     harness.py --no-net), no updates over Wi-Fi (USB, or the app's own way), nothing confirms a new image (so no
//     CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE, or the app confirms it itself);
//   - Settings without the phone, Wi-Fi, updates, network and IP rows (forge_settings builds them only with
//     forge_net / forge_ota in the build); Restart restarts at once.
// check_nonet.py proves none of forge_net, forge_ota, dns_server, esp_wifi, esp_https_server is in the build.
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "board.h"
#include "board_audio.h"
#include "imu.h"
#include "diag.h"
#include "nvs_util.h"
#include "testcon.h"
#include "presence.h"
#include "app_text.h"
#include "ui.h"

static const char *TAG = "app";

// Screen dimming (forge_presence) on this board's microphones, motion sensor and touch, as the Wi-Fi starter
static bool mic_open(void) { return board_mic_open(30); }        // 30 dB: a room's background noise
static bool accel_open(void) { return imu_init(board_i2c_bus()); }
static void set_brightness(int pct)
{
    display_lock(-1);
    display_brightness((uint8_t)(pct * 255 / 100));
    display_unlock();
}
static void screen_off(bool off)                                  // a real off: the panel asleep (board.h)
{
    display_lock(-1);
    display_sleep(off);
    display_unlock();
}
static const presence_hooks_t presence_hooks = {
    .mic_open = mic_open, .mic_read = board_mic_read, .accel_open = accel_open, .accel_read = imu_read,
    .touch_idle_ms = touch_idle_ms, .set_brightness = set_brightness, .screen_off = screen_off,
};

void app_main(void)
{
    ESP_LOGI(TAG, "starting");
    diag_mark("start");
    nvs_init();                 // NVS first: the language, presence's settings (the app's own, a radio's calibration)
    app_text_init();
    diag_start(60);             // "diag:" lines every 60 s (heap, tasks, display)
    board_init();               // display, LVGL, touch; registers fps/tap/swipe/screen with the test console
    diag_mark("board");
    presence_start(&presence_hooks);    // dims, then turns off, the screen in a quiet room (console: presence, wake)
    touch_set_press_filter(presence_touch);   // a touch on a dark screen only wakes it
    ui_init();
    testcon_start();
    diag_mark("app ready");     // the ready line (forge.json ready_line): the harness starts its tests here

    // The app's work runs in its own tasks (a Bluetooth host, its scans). Waits must wake on requests
    // (ulTaskNotifyTake), never a fixed vTaskDelay that makes the device ignore the user for its length.
    while (1) ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
}
