// Screen dimming in the browser: a stand-in for forge_presence's presence.c (its state machine, JSON and routes are
// the real ones). The browser gives no microphone or motion sensor without asking, so the screen stays on (ACTIVE)
// and the settings page shows "No microphone"; the settings are kept for the session. The board's microphones and
// motion sensor (board_audio.h, imu.h) answer "not here" for an app that opens them itself.
#include "presence.h"
#include "presence_json.h"
#include "board_audio.h"
#include "imu.h"

static presence_cfg_t cfg = {
    .enabled = true, .margin_db = 10, .wake_s = 3, .dim_s = 600, .off_s = 3000,
    .bright_pct = 100, .dim_pct = 15, .baseline_db = -60,
};
static bool motion_wake = true;
static float motion_thr = 0.10f;

void presence_start(const presence_hooks_t *hooks) { (void)hooks; }
void presence_get_config(presence_cfg_t *out) { *out = cfg; }
bool presence_set_config(const presence_cfg_t *in)
{
    float baseline = cfg.baseline_db;
    cfg = *in;
    cfg.baseline_db = baseline;
    presence_clamp_cfg(&cfg);
    return true;
}
void presence_get_status(presence_status_t *st)
{
    *st = (presence_status_t){ .level_db = -90, .threshold_db = cfg.baseline_db + cfg.margin_db,
                               .state = PRESENCE_ACTIVE, .mic_ok = false, .brightness = cfg.bright_pct,
                               .imu_ok = false, .motion_thr = motion_thr };
}
bool presence_calibrate(int seconds) { (void)seconds; return false; }
void presence_wake(void) {}
void presence_settings_changed(void) {}
void presence_preview_brightness(int pct) { cfg.bright_pct = pct < 5 ? 5 : pct > 100 ? 100 : pct; }
bool presence_touch(void) { return false; }
bool presence_screen_off(void) { return false; }
bool presence_motion_wake(void) { return motion_wake; }
bool presence_set_motion(bool on, float threshold_g)
{
    motion_wake = on;
    motion_thr = threshold_g < 0.02f ? 0.02f : threshold_g > 0.5f ? 0.5f : threshold_g;
    return true;
}

bool board_audio_init(bool speaker) { (void)speaker; return false; }
bool board_mic_open(float gain_db) { (void)gain_db; return false; }
bool board_mic_read(int16_t *samples, size_t n) { (void)samples; (void)n; return false; }
const void *board_audio_data_if(void) { return NULL; }
bool imu_init(i2c_master_bus_handle_t bus) { (void)bus; return false; }
bool imu_read(float g[3]) { (void)g; return false; }
