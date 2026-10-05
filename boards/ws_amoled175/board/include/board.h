#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"
#include "driver/i2c_master.h"

// Board support: Waveshare ESP32-S3-Touch-AMOLED-1.75 (CO5300 466x466 round AMOLED over QSPI, CST9217 touch and
// QMI8658 motion sensor on I2C). Every board directory (boards/<name>/board) exposes this same API; the root
// CMakeLists.txt picks one with EXTRA_COMPONENT_DIRS (docs/NEW-PROJECT.md, "A new board").

#define BOARD_NAME  "Waveshare ESP32-S3-Touch-AMOLED-1.75"
#define DISP_W      466
#define DISP_H      466
#define BOARD_ROUND 1           // round panel: keep content inside the circle (snapshots tint the corners red)

// Display, LVGL (its task on core 1), touch and the forge_lvgl hooks, the test console's "fps" and "where"
// breadcrumbs, and the diag "display:" line. Call after nvs/net init is not required; call before building screens.
void board_init(void);

// The LVGL lock (recursive). Every LVGL call from outside the LVGL task holds it. Never hold it for long: a task
// that blocks LVGL for more than ~50 ms loses quick flicks.
bool display_lock(int timeout_ms);   // -1 = forever
void display_unlock(void);
void display_brightness(uint8_t level);   // 0..255, with the lock held, from any task

// Frame statistics since the last reset (diag line every period; "fps" in the test console has its own counters)
typedef struct {
    uint32_t frames, render_us, render_max_us;     // rendered frames and their render time
    uint32_t anim_frames, anim_us, anim_gap_max_us; // back-to-back frames (animations): interval sum / worst gap
    uint32_t anim_gap_max_at_ms;                   // ...when it ended (ms since the reset; test console only)
    char anim_gap_max_kind[12];                    // ...between which frames: "lvgl>move", "move>move", ...
    uint64_t pixels;                               // pixels sent to the panel
    uint32_t lvgl_wait_max_us;                     // longest wait of the LVGL task for the lock
    uint32_t hold_max_us;                          // longest lock hold by another task...
    char hold_task[16];                            // ...and which task
} display_stats_t;
void display_get_stats(display_stats_t *out, bool reset);

// A frame drawn without LVGL (forge_lvgl's slide.c): fill() writes rows y0..y0+n-1, full width, RGB565
// *byte-swapped* (panel order), into dst; each band is sent while the next one is filled. Display lock held;
// afterwards LVGL must redraw before it flushes again (lv_obj_invalidate).
typedef void (*display_fill_cb_t)(int y0, int n, void *dst, void *user);
void display_raw_frame(display_fill_cb_t fill, void *user);

// Called with every area LVGL sends to the panel, before the byte swap (RGB565 as LVGL draws it), in the LVGL task.
// For a picture of the screen kept in step with the panel (drags drawn outside LVGL).
typedef void (*display_flush_hook_t)(const lv_area_t *a, const uint8_t *px);
void display_set_flush_hook(display_flush_hook_t hook);

i2c_master_bus_handle_t board_i2c_bus(void);   // the touch bus, shared with the motion sensor (imu.h) and audio
uint32_t touch_idle_ms(void);                  // ms since a finger was last down (any task)
// The finger now, read directly, for moves drawn outside LVGL (display lock held, LVGL not reading meanwhile):
// 1 pressed at x,y, 0 up, -1 bus error. The chip is read at most every 10 ms: polled faster it answers "not in
// contact" for long stretches with the finger on it. touch_fresh(): the last touch_get() read the chip.
int touch_get(int *x, int *y);
bool touch_fresh(void);
void touch_forget(void);                       // the touch LVGL last saw was handled by a drag: start afresh
// Called with each touch read LVGL makes, before LVGL handles it (gesture recognisers, waking a dark screen)
typedef void (*touch_read_hook_t)(lv_indev_t *indev, lv_indev_data_t *data);
void touch_set_read_hook(touch_read_hook_t hook);
