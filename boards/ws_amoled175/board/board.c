// Board bring-up and its test console / diagnostics hooks (see board.h)
#include "board.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "testcon.h"
#include "diag.h"
#include "forge_lvgl.h"

void display_init(void);
void touch_init(void);
void touch_register_lvgl(void);
void display_get_test_stats(display_stats_t *out, bool reset);
int display_lvgl_inflight(void);
extern volatile int disp_phase;

// "fps reset", then "fps": frames since the reset (the harness measures animations this way)
static void cmd_fps(int argc, char **argv)
{
    display_stats_t d;
    display_get_test_stats(&d, true);
    if (argc == 2 && !strcmp(argv[1], "reset")) { ESP_LOGI("test", "ok fps reset"); return; }
    ESP_LOGI("test", "fps frames=%lu render_avg_ms=%.1f render_max_ms=%.1f anim_frames=%lu anim_fps=%.1f "
                     "gap_max_ms=%.1f mpx=%.2f", (unsigned long)d.frames,
             d.frames ? d.render_us / 1000.0f / d.frames : 0, d.render_max_us / 1000.0f,
             (unsigned long)d.anim_frames, d.anim_us ? d.anim_frames * 1e6f / d.anim_us : 0,
             d.anim_gap_max_us / 1000.0f, d.pixels / 1e6f);
}

// Breadcrumbs for "where" (no lock): what the display code is doing now
static void where_display(char *out, size_t n)
{
    snprintf(out, n, " disp_phase=%d lvgl_inflight=%d", disp_phase, display_lvgl_inflight());
}

static void diag_display(void)
{
    display_stats_t d;
    display_get_stats(&d, true);
    ESP_LOGI("diag", "display: %lu frames, render avg %.1f ms max %.1f ms, %.1f Mpx sent | "
             "animation %.1f fps (%lu frames, worst gap %.0f ms) | LVGL lock wait max %.1f ms, "
             "longest hold %.1f ms by %s",
             (unsigned long)d.frames, d.frames ? d.render_us / 1000.0 / d.frames : 0, d.render_max_us / 1000.0,
             d.pixels / 1e6, d.anim_us ? d.anim_frames * 1e6 / d.anim_us : 0, (unsigned long)d.anim_frames,
             d.anim_gap_max_us / 1000.0, d.lvgl_wait_max_us / 1000.0, d.hold_max_us / 1000.0,
             d.hold_task[0] ? d.hold_task : "-");
}

void board_init(void)
{
    ESP_LOGI("board", "%s", BOARD_NAME);
    touch_init();
    display_init();
    display_lock(-1);
    touch_register_lvgl();
    display_unlock();
    forge_lvgl_init(display_lock, display_unlock);
    testcon_register("fps", "fps [reset]", cmd_fps);
    testcon_add_where(where_display);
    diag_add_hook(diag_display);
}
