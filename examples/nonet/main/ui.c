// The no-Wi-Fi starter's screens (see ui.h): "hello" and "system" (pages of one pager: swipe between them) and the
// Settings screen a long press opens, with only the rows an app without forge_net and forge_ota can have. The part a
// new project replaces.
#include "ui.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_app_desc.h"
#include "board.h"
#include "pager.h"
#include "screens.h"
#include "slide.h"
#include "textfit.h"
#include "app_text.h"
#include "forge_settings.h"

static const char *TAG = "ui";

#define C_BG     lv_color_hex(0x000000)       // AMOLED: black pixels are off
#define C_TEXT   lv_color_hex(0xF2F4F7)
#define C_DIM    lv_color_hex(0x8B95A1)
#define C_ACCENT lv_color_hex(0x4DA3FF)

#ifdef EMU_BUILD                             // the browser emulator (web/emu): the font is an array, its end a pointer
extern const uint8_t ttf_start[];
extern const uint8_t *const ttf_end;
#else
extern const uint8_t ttf_start[] asm("_binary_montserrat_ttf_start");
extern const uint8_t ttf_end[]   asm("_binary_montserrat_ttf_end");
#endif

static lv_font_t *f_mid, *f_small, *f_tiny;
static lv_obj_t *scr_main, *pager;
static lv_obj_t *h_title, *h_sub, *h_hint, *s_title, *s_lines;

static lv_font_t *mkfont(int px)
{
    // No kerning: it cost 71 % of render time in weather_amoled. 96 cached glyphs per size.
    return lv_tiny_ttf_create_data_ex(ttf_start, ttf_end - ttf_start, px, LV_FONT_KERNING_NONE, 96);
}

// Labels are never clickable: a decorative object that is clickable swallows presses (long-press never fired)
static lv_obj_t *label(lv_obj_t *parent, lv_font_t *f, lv_color_t c, int y, int w)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(l, w);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(l, LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_label_set_text(l, "");
    return l;
}

// Set only when it changes: a redraw that changes nothing still costs a frame
static void set_text(lv_obj_t *l, const char *s)
{
    if (strcmp(lv_label_get_text(l), s)) lv_label_set_text(l, s);
}

static void long_pressed(lv_event_t *e)
{
    if (!slide_held_still(SLIDE_HOLD_PX)) { ESP_LOGI(TAG, "long-press from a moving finger: a swipe, ignored (L200)"); return; }
    ESP_LOGI(TAG, "long-press: settings");
    settings_open();
}

/* ---------- hello and system pages ---------- */

static void hello_refresh(void)
{
    set_text(h_title, tr(T_HELLO));
    set_text(h_sub, tr(T_HELLO_SUB));
    set_text(h_hint, tr(T_SWIPE_HINT));
}

static void system_refresh(void)
{
    char lines[240], up[24];
    int64_t s = esp_timer_get_time() / 1000000;
    if (s < 3600) snprintf(up, sizeof(up), tr(T_SET_UPTIME_MIN), (int)(s / 60));
    else if (s < 86400) snprintf(up, sizeof(up), tr(T_SET_UPTIME_H), (int)(s / 3600), (int)(s / 60 % 60));
    else snprintf(up, sizeof(up), tr(T_SET_UPTIME_D), (int)(s / 86400), (int)(s / 3600 % 24));
    int n = snprintf(lines, sizeof(lines), tr(T_SYS_VERSION), esp_app_get_description()->version);
    n += snprintf(lines + n, sizeof(lines) - n, "\n");
    n += snprintf(lines + n, sizeof(lines) - n, tr(T_SYS_MEMORY),
                  (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
                  (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
    n += snprintf(lines + n, sizeof(lines) - n, "\n");
    snprintf(lines + n, sizeof(lines) - n, tr(T_SYS_UPTIME), up);
    set_text(s_title, tr(T_SYSTEM));
    set_text(s_lines, lines);
}

// Every second, both pages: a swipe shows a picture of the other one rendered in the background (slide.c)
static void tick(lv_timer_t *t)
{
    if (lv_screen_active() != scr_main) return;
    hello_refresh();
    system_refresh();
}

static void page_settled(int page, void *user)
{
    ESP_LOGI(TAG, "page %s", page ? "system" : "hello");
    if (page == 1) system_refresh();
    else hello_refresh();
}

static void main_create(void)
{
    scr_main = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_main, C_BG, 0);
    lv_obj_remove_flag(scr_main, LV_OBJ_FLAG_SCROLLABLE);
    pager = pager_create(scr_main, false, 2, NULL, page_settled, NULL);
    lv_obj_add_event_cb(pager, long_pressed, LV_EVENT_LONG_PRESSED, NULL);
    slide_pager(pager);                                   // drags drawn as pictures (~60 fps), not LVGL scrolling
    lv_obj_t *p0 = pager_page(pager, 0), *p1 = pager_page(pager, 1);
    h_title = label(p0, f_mid, C_ACCENT, 130, 300);
    h_sub = label(p0, f_small, C_TEXT, 190, 340);
    h_hint = label(p0, f_small, C_DIM, 290, 300);
    s_title = label(p1, f_mid, C_ACCENT, 110, 300);
    s_lines = label(p1, f_small, C_TEXT, 170, 380);
    hello_refresh();
    system_refresh();
    lv_timer_create(tick, 1000, NULL);
}

/* ---------- Settings (forge_settings) ---------- */

// The app's words for forge_settings' codes, in settings_text_t's order (T_SET_NONE: a row this app doesn't list)
static const int set_texts[] = {
    T_SET_DONE, T_SET_SEC_SCREEN, T_SET_DIM_QUIET, T_SET_WAKE_PICKUP, T_SET_TIMING, T_SET_SHORT, T_SET_NORMAL,
    T_SET_LONG, T_SET_CUSTOM, T_SET_BRIGHTNESS, T_SET_LANGUAGE, T_SET_SEC_MORE,
    T_SET_NONE, T_SET_NONE, T_SET_NONE,              // phone page, its scan note, "connect first": no forge_net
    T_SET_NONE,                                      // Wi-Fi network
    T_SET_NONE, T_SET_NONE, T_SET_NONE, T_SET_NONE, T_SET_NONE, T_SET_NONE,   // updates: no forge_ota
    T_SET_TAP_AGAIN, T_SET_RESTART, T_SET_RESTARTING, T_SET_SEC_ABOUT, T_SET_VERSION,
    T_SET_NONE, T_SET_NONE, T_SET_NONE,              // network, offline, IP address: no forge_net
    T_SET_MEMORY, T_SET_MEMORY_KB, T_SET_UPTIME, T_SET_UPTIME_MIN, T_SET_UPTIME_H, T_SET_UPTIME_D,
};
_Static_assert(sizeof(set_texts) / sizeof(set_texts[0]) == SET_T_COUNT, "one text per settings_text_t code");

static void lang_changed(void)                       // display lock held (a Settings row)
{
    hello_refresh();
    system_refresh();
}

static void settings_make(void)
{
    const settings_row_t rows[] = {
        settings_std_section(SET_T_SEC_SCREEN),
        settings_std_dim(), settings_std_motion(), settings_std_timing(),
        settings_std_language(lang_changed),
        settings_std_section(SET_T_SEC_MORE),
        settings_std_restart(),
        settings_std_section(SET_T_SEC_ABOUT),
        settings_std_version(), settings_std_memory(), settings_std_uptime(),
    };
    settings_opts_t o = { .texts = set_texts, .font = f_small, .font_small = f_tiny, .brightness = true };
    settings_create(&o, rows, sizeof(rows) / sizeof(rows[0]));
}

/* ---------- screen registry (test console: "screen", "shot") ---------- */

static lv_obj_t *get_hello(void) { return pager_page(pager, 0); }
static lv_obj_t *get_system(void) { return pager_page(pager, 1); }
static void show_hello(void) { pager_go(pager, 0, false); lv_screen_load(scr_main); }
static void show_system(void) { system_refresh(); pager_go(pager, 1, false); lv_screen_load(scr_main); }
static bool shown_hello(void) { return lv_screen_active() == scr_main && pager_current(pager) == 0; }
static bool shown_system(void) { return lv_screen_active() == scr_main && pager_current(pager) == 1; }
static lv_obj_t *get_settings(void) { return settings_screen(); }
static void show_settings(void) { settings_show_page(0); }
static void prep_settings(void) { settings_scroll(0); }
static void show_settings1(void) { settings_show_page(1); }
static void prep_settings1(void) { settings_scroll(1); }
static bool shown_settings(void) { return lv_screen_active() == settings_screen(); }
static bool shown_never(void) { return false; }      // settings1: the same screen, never named as the one shown

static const screen_def_t screens[] = {
    { "hello",     get_hello,    show_hello,     hello_refresh,  shown_hello },
    { "system",    get_system,   show_system,    system_refresh, shown_system },
    { "settings",  get_settings, show_settings,  prep_settings,  shown_settings },
    { "settings1", get_settings, show_settings1, prep_settings1, shown_never },   // the list scrolled down a box
};

void ui_init(void)
{
    textfit_init(ttf_start, ttf_end - ttf_start);
    display_lock(-1);
    f_mid = mkfont(30);
    f_small = mkfont(20);
    f_tiny = mkfont(14);
    main_create();
    settings_make();
    screens_register(screens, sizeof(screens) / sizeof(screens[0]));
    lv_screen_load(scr_main);
    display_unlock();
}
