// Moves drawn as pictures (see slide.h). Ported and trimmed from weather_amoled's slide.c (v1.12.1): the frame
// composition, the finger rules and the drag loop are the same; its picture cache is replaced by a shadow of the panel
// plus the current page's neighbours, kept ready while nobody touches (rendered when a drag starts only if not ready).
#include "slide.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "forge_lvgl.h"
#include "pager.h"
#include "testcon.h"
#include "indev/lv_indev_private.h"         // wait_until_release: cleared after a drag (touch_resync)

static const char *TAG = "slide";
static const forge_panel_t *P;
static int W, H;
static volatile int phase;                  // breadcrumb for the test console's "where"

/* ---------- pictures ---------- */

static lv_draw_buf_t *shadow;               // what the panel shows (every LVGL flush is copied into it)
static lv_draw_buf_t *other;                // the picture coming in, rendered when a move starts
/* The neighbours of the page shown, kept ready: rendered while a drag waited for them (35-45 ms), the page started late
 * and jumped to catch the finger up (the user's "hiccups", October 4). Rendered by the idle timer, one per tick, when
 * nobody has touched the screen for IDLE_MS; re-rendered when older than STALE_MS (contents change: a clock, a status
 * line), so a drag shows at most 2 s old contents for the length of the drag (LVGL redraws the real page after). */
#define IDLE_MS 300
#define STALE_MS 2000
static struct { lv_draw_buf_t *buf; lv_obj_t *page; uint32_t at; } nb[2];   // [0] = prev, [1] = next
static uint32_t touched_ms;                 // lv_tick of the last press seen
static bool shadow_ok;                      // a whole frame has been flushed since the shadow was (re)started

static void flushed(const lv_area_t *a, const uint8_t *px)
{
    int w = a->x2 - a->x1 + 1, x1 = a->x1 < 0 ? 0 : a->x1, x2 = a->x2 > W - 1 ? W - 1 : a->x2;
    for (int y = a->y1 < 0 ? 0 : a->y1; y <= a->y2 && y < H; y++)
        memcpy(shadow->data + y * shadow->header.stride + x1 * 2, px + ((y - a->y1) * w + x1 - a->x1) * 2,
               (x2 - x1 + 1) * 2);
    if (a->x1 <= 0 && a->y1 <= 0 && a->x2 >= W - 1 && a->y2 >= H - 1) shadow_ok = true;
}

static void rendered(lv_event_t *e) { if (!shadow_ok && lv_display_get_default()) shadow_ok = true; }

// An object rendered off-display into `other` (its own coordinates: a pager page off screen works too)
static bool render(lv_obj_t *obj)
{
    lv_obj_update_layout(lv_obj_get_screen(obj));
    return lv_snapshot_take_to_draw_buf(obj, LV_COLOR_FORMAT_RGB565, other) == LV_RESULT_OK;
}

/* ---------- frames ---------- */

typedef struct {
    const uint8_t *cur, *prev, *next;   // pictures (RGB565): prev = left / above, next = right / below; NULL = black
    uint32_t stride;                    // bytes per picture row (both pictures alike)
    bool vertical;
    int off;                            // where the current picture is: 0 = in place, < 0 moved left / up (next shows),
                                        // > 0 moved right / down (prev shows); even
} frame_t;

// n pixels, little-endian RGB565 -> panel byte order, two at a time (rows and offsets are even, so 4-byte aligned)
static inline void copy_swap(void *dst, const void *src, int n)
{
    uint32_t *d = dst;
    if (!src) { for (int i = 0; i < n / 2; i++) d[i] = 0; return; }
    const uint32_t *s = src;
    for (int i = 0; i < n / 2; i++) {
        uint32_t v = s[i];
        d[i] = ((v & 0x00FF00FFu) << 8) | ((v >> 8) & 0x00FF00FFu);
    }
}

static inline const uint8_t *row(const uint8_t *pic, int y, uint32_t stride) { return pic ? pic + y * stride : NULL; }
static inline const uint8_t *at(const uint8_t *r, int x) { return r ? r + x * 2 : NULL; }

static void fill(int y0, int n, void *dst, void *user)
{
    const frame_t *f = user;
    uint8_t *d = dst;
    const int o = f->off;
    for (int y = y0; y < y0 + n; y++, d += W * 2) {
        if (!f->vertical) {                              // a row = the end of one picture + the start of the other
            const uint8_t *c = row(f->cur, y, f->stride);
            if (o <= 0) {
                int a = -o;
                copy_swap(d, at(c, a), W - a);
                copy_swap(d + (W - a) * 2, row(f->next, y, f->stride), a);
            } else {
                copy_swap(d, at(row(f->prev, y, f->stride), W - o), o);
                copy_swap(d + o * 2, c, W - o);
            }
        } else if (o <= 0) {                             // a row of one picture or the other
            int sy = y - o;
            copy_swap(d, sy < H ? row(f->cur, sy, f->stride) : row(f->next, sy - H, f->stride), W);
        } else {
            copy_swap(d, y < o ? row(f->prev, H - o + y, f->stride) : row(f->cur, y - o, f->stride), W);
        }
    }
}

static void show(frame_t *f, int off)
{
    f->off = off & ~1;
    P->raw_frame(fill, f);
}

// From the current offset to `to`, ease out (cubic), frames as fast as they go
static int animate(frame_t *f, int to, int ms)
{
    int from = f->off, frames = 0;
    int64_t start = esp_timer_get_time();
    for (;;) {
        int64_t el = esp_timer_get_time() - start;
        float t = el >= ms * 1000LL ? 1.0f : (float)el / (ms * 1000.0f);
        float e = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
        show(f, from + (int)((to - from) * e));
        frames++;
        if (t >= 1.0f) return frames;
    }
}

/* ---------- the finger ----------
 * While a move runs, LVGL is paused and never sees the finger lift. The touch that started it was told to wait for its
 * release (lv_indev_wait_release), so LVGL took the next touch for the same one: quick successive swipes were missed.
 * After the move: LVGL starts afresh, and a finger already down counts as a new press. */
// The press read_hook is tracking (a pager drag in the making)
static struct { bool down, armed; lv_point_t p0; lv_obj_t *pager; } track;

static void touch_resync(bool lifted)
{
    int x, y;
    lv_indev_t *in = lv_indev_get_next(NULL);
    if (!in || (!lifted && P->touch_get(&x, &y) != 0)) return;
    P->touch_forget();                                   // and the board's memory of it (no fake press, then a tap)
    in->wait_until_release = 0;
    lv_indev_reset(in, NULL);
    // And ours: LVGL read nothing during the move, so read_hook never saw this press end. A finger landing during the
    // release animation was then taken for the old press (not armed): quick successive swipes were lost (weather_amoled
    // fixed the same with a counter in touch_forget, v1.12.1; found aligning the two, October 4)
    track.down = false;
}

/* The finger, read directly (LVGL paused, the display lock held). The CST9217 often stops answering (NACK) instead of
 * reporting a release: 5 failed reads in a row count as a release, and further failures stay "up" until a press. A
 * reported "up" counts once it has lasted UP_HOLD_US: under a quick finger the chip reports brief "ups" with the finger
 * still down. *errs carries the state: start at 0 (finger down). 1 = down at x,y; 0 = up; -1 / -2 = keep the last
 * point (a read error, or an "up" not held yet). */
#define FINGER_UP 5
#define UP_HOLD_US 60000
static int64_t up_since;

static int finger(int *x, int *y, int *errs)
{
    int r = P->touch_get(x, y);
    if (!P->touch_fresh()) return r > 0 ? 1 : *errs >= FINGER_UP ? 0 : up_since ? -2 : -1;   // same reading
    int64_t now = esp_timer_get_time();
    if (r > 0) { up_since = 0; *errs = 0; return 1; }
    if (*errs >= FINGER_UP) return 0;
    if (r < 0 && !up_since) {
        if (++*errs < FINGER_UP) return -1;
        *errs = FINGER_UP - 1;
    }
    if (!up_since) up_since = now;
    if (now - up_since < UP_HOLD_US) return -2;
    up_since = 0;
    *errs = FINGER_UP;
    return 0;
}

// A loop following the finger ends after 20 s as if it had lifted (a chip stuck reporting a press must not keep the
// display lock forever)
#define FINGER_FOLLOW_US 20000000LL

/* ---------- pager drags ---------- */

#define MAX_PAGERS 4
#define DRAG_PX 10                      // decided after 10 px, by the larger axis (as LVGL picks a scroll direction)
static lv_obj_t *pagers[MAX_PAGERS];
static int npagers;
static struct { bool queued, vertical; lv_obj_t *pager; int x0, y0, x1, y1; } drag;
static struct { bool queued; int dir; bool vertical; void (*change)(void *); void *user; lv_obj_t *to; } chg;

bool slide_busy(void) { return drag.queued || chg.queued; }

static bool ready(void) { return P && shadow && other && shadow_ok; }

// The picture of page `target` of `pg` as neighbour i (0 prev, 1 next): the cached one, else rendered now
static const uint8_t *neighbour(lv_obj_t *pg, int target, int i, int *renders)
{
    lv_obj_t *page = pager_page(pg, target);
    if (!page || target < 0 || target >= pager_count(pg)) return NULL;
    if (nb[i].buf && nb[i].page == page && nb[i].at) return nb[i].buf->data;
    lv_draw_buf_t *dst = nb[i].buf ? nb[i].buf : other;
    lv_obj_update_layout(lv_obj_get_screen(page));
    if (lv_snapshot_take_to_draw_buf(page, LV_COLOR_FORMAT_RGB565, dst) != LV_RESULT_OK) return NULL;
    (*renders)++;
    if (dst == nb[i].buf) { nb[i].page = page; nb[i].at = lv_tick_get(); }
    return dst->data;
}

static void drag_run(void *unused)
{
    lv_obj_t *pg = drag.pager;
    const int S = drag.vertical ? H : W, cur = pager_current(pg), n = pager_count(pg);
    int64_t t0 = esp_timer_get_time();
    phase = 11;
    frame_t f = { .cur = shadow->data, .stride = shadow->header.stride, .vertical = drag.vertical };
    // The neighbour the finger is heading to, rendered before the first frame (a quick flick may be over by then)
    int side = (drag.vertical ? drag.y1 - drag.y0 : drag.x1 - drag.x0) > 0 ? -1 : 1, target = cur + side;
    int renders = 0;
    const uint8_t *pic = neighbour(pg, target, side > 0, &renders);
    bool have = pic != NULL;
    if (side < 0) f.prev = pic; else f.next = pic;
    (void)n;
    int x = drag.x1, y = drag.y1, raw = 0, frames = 0, samples = 0, errs = 0;
    int64_t t_first = 0, t_prev = esp_timer_get_time(), t_last = t_prev;
    // For the log line: what a real finger does (synthetic drags never hold): reads held by a bus error / a brief
    // "up", the longest run of held reads, the longest gap between two frames, the longest the finger stood still
    int held_err = 0, held_up = 0;
    int64_t hold_since = 0, hold_max = 0, frame_at = 0, gap_max = 0, still_since = 0, still_max = 0;
    int last_raw = 0;
    int pos_prev = 0, pos_last = 0;
    phase = 12;
    for (;;) {
        int nx, ny, r = finger(&nx, &ny, &errs);
        if (r == 0) break;
        if (esp_timer_get_time() - t0 > FINGER_FOLLOW_US) { ESP_LOGW(TAG, "drag: finger down for 20 s, ending it"); break; }
        if (r > 0) { x = nx; y = ny; samples++; }
        raw = drag.vertical ? y - drag.y0 : x - drag.x0;     // > 0: towards prev (it comes in from the left / top)
        // Held (a read error, or an "up" not confirmed yet: up to 60 ms): the page goes on at the finger's last speed
        // instead of standing still (every swipe ended with a ~50 ms freeze, then the snap), at most 80 ms worth
        if (r < 0 && t_last > t_prev) {
            int64_t ahead = esp_timer_get_time() - t_last;
            if (ahead > 80000) ahead = 80000;
            raw = pos_last + (int)((pos_last - pos_prev) * (float)ahead / (t_last - t_prev));
        }
        int s = raw > 0 ? -1 : raw < 0 ? 1 : 0;
        int64_t tnow = esp_timer_get_time();
        if (r < 0) {
            if (r == -1) held_err++; else held_up++;
            if (!hold_since) hold_since = tnow;
            if (tnow - hold_since > hold_max) hold_max = tnow - hold_since;
        } else hold_since = 0;
        if (s && s != side) {                                // the finger turned: the other neighbour
            side = s;
            target = cur + side;
            pic = neighbour(pg, target, side > 0, &renders);
            have = pic != NULL;
            f.prev = side < 0 ? pic : NULL;
            f.next = side > 0 ? pic : NULL;
        }
        int off;
        if (s && have) off = raw < -S ? -S : raw > S ? S : raw;   // follows the finger
        else {                                                     // no neighbour: resists, at most a fifth
            off = raw / 3;
            if (off > S / 5) off = S / 5;
            if (off < -S / 5) off = -S / 5;
        }
        int64_t now = esp_timer_get_time();
        // Speed from fresh readings only: a repeated point measured a flick as 0 px/ms (weather_amoled v1.12.1)
        if (r > 0 && P->touch_fresh() && now - t_last > 8000) { t_prev = t_last; pos_prev = pos_last; t_last = now; pos_last = raw; }
        if (raw != last_raw || !still_since) { still_since = now; last_raw = raw; }
        else if (now - still_since > still_max) still_max = now - still_since;
        show(&f, off);
        int64_t done = esp_timer_get_time();
        if (frame_at && done - frame_at > gap_max) gap_max = done - frame_at;
        frame_at = done;
        if (!frames++) t_first = done;
    }
    // Released: on to the neighbour past a third of the screen or after a flick towards it, else back
    float vel = t_last > t_prev ? (float)(pos_last - pos_prev) / ((t_last - t_prev) / 1000.0f) : 0;   // px/ms
    int s = f.off > 0 ? -1 : f.off < 0 ? 1 : 0;
    bool go = s && have && s == side &&
              (abs(f.off) > S / 3 || (abs(f.off) > 24 && vel * f.off > 0 && fabsf(vel) > 0.35f));
    if (!samples && have) go = true;                         // lifted before the first frame: a flick that way
    int to = go ? (side < 0 ? S : -S) : 0;
    frames += animate(&f, to, 60 + 220 * abs(to - f.off) / S);
    int64_t t1 = esp_timer_get_time();
    phase = 16;
    if (go) {
        pager_switch(pg, target);                           // no LVGL animation: the pictures already moved
        // The page left is now a neighbour: its picture is the shadow (what the panel showed when the drag started)
        int back = side > 0 ? 0 : 1;
        if (nb[back].buf) {
            memcpy(nb[back].buf->data, shadow->data, shadow->header.stride * H);
            nb[back].page = pager_page(pg, cur);
            nb[back].at = lv_tick_get();
        }
        nb[!back].page = NULL;                              // the one beyond: rendered when idle
    }
    lv_obj_invalidate(lv_screen_active());                  // LVGL repaints the real thing (and the shadow)
    touch_resync(true);
    touched_ms = lv_tick_get();                              // LVGL saw no reads during the drag: quiet from now
    int64_t ts = t_first ? t_first : t0;
    ESP_LOGI(TAG, "drag: first frame after %lld ms, %d frames in %lld ms (%.0f fps), %s | gap max %lld ms, held reads "
                  "%d err %d up (longest %lld ms), finger still max %lld ms, samples %d, renders %d, %.2f px/ms",
             (ts - t0) / 1000, frames, (t1 - ts) / 1000, frames * 1e6f / (t1 - ts + 1),
             go ? (side < 0 ? "to prev" : "to next") : "back", gap_max / 1000, held_err, held_up, hold_max / 1000,
             still_max / 1000, samples, renders, vel);
    drag.queued = false;
    phase = 0;
}

static lv_obj_t *pager_under(lv_obj_t *scr)
{
    for (int i = 0; i < npagers; i++)
        if (lv_obj_get_screen(pagers[i]) == scr && !lv_obj_has_flag(pagers[i], LV_OBJ_FLAG_HIDDEN)) return pagers[i];
    return NULL;
}

// Each touch read, before LVGL handles it: a press on a slide pager that moves 10 px becomes a drag
static void read_hook(lv_indev_t *in, lv_indev_data_t *data)
{
    if (data->state != LV_INDEV_STATE_PRESSED) { track.down = false; return; }
    touched_ms = lv_tick_get();
    if (!track.down) {                                     // a new press: may become a drag
        track.down = true;
        track.pager = slide_busy() ? NULL : pager_under(lv_screen_active());
        track.armed = track.pager != NULL;
        track.p0 = data->point;
        return;
    }
    if (!track.armed) return;
    int dx = data->point.x - track.p0.x, dy = data->point.y - track.p0.y;
    if (abs(dx) < DRAG_PX && abs(dy) < DRAG_PX) return;
    track.armed = false;
    bool vertical = pager_vertical(track.pager);
    if ((abs(dy) > abs(dx)) != vertical || !ready() || slide_busy()) return;   // the other axis: LVGL's
    drag = (typeof(drag)){ true, vertical, track.pager, track.p0.x, track.p0.y, data->point.x, data->point.y };
    lv_async_call(drag_run, NULL);                         // at the top of the LVGL task (stack for the render)
    lv_indev_wait_release(in);                             // the drag owns this touch: LVGL ignores it from now on
}

void slide_pager(lv_obj_t *pager)
{
    if (npagers < MAX_PAGERS) pagers[npagers++] = pager;
    if (P) pager_freeze(pager);                            // drags are ours; without the panel LVGL keeps them
}

/* ---------- in-place changes and screen loads ---------- */

static void change_run(void *unused)
{
    phase = 21;
    int64_t t0 = esp_timer_get_time();
    if (chg.change) chg.change(chg.user);                  // the new state (the shadow still holds the old one)
    lv_obj_t *scr = chg.to ? chg.to : lv_screen_active();
    bool ok = render(scr);
    int frames = 0;
    int64_t t1 = esp_timer_get_time();
    if (ok) {
        frame_t f = { .cur = shadow->data, .stride = shadow->header.stride, .vertical = chg.vertical };
        if (chg.dir > 0) f.next = other->data; else f.prev = other->data;
        phase = 22;
        frames = animate(&f, -chg.dir * (chg.vertical ? H : W), 280);
    }
    int64_t t2 = esp_timer_get_time();
    if (chg.to) lv_screen_load(chg.to);
    lv_obj_invalidate(scr);
    touch_resync(false);
    if (ok) ESP_LOGI(TAG, "change: picture %lld ms, %d frames in %lld ms (%.0f fps)", (t1 - t0) / 1000, frames,
                     (t2 - t1) / 1000, frames * 1e6f / (t2 - t1 + 1));
    else ESP_LOGW(TAG, "change: no picture, no animation");
    chg.queued = false;
    phase = 0;
}

static void queue_change(int dir, bool vertical, void (*change)(void *), void *user, lv_obj_t *to)
{
    if (!ready() || slide_busy()) {                        // no slide possible: the change itself, at once
        if (change) change(user);
        if (to) lv_screen_load_anim(to, dir > 0 ? (vertical ? LV_SCR_LOAD_ANIM_MOVE_TOP : LV_SCR_LOAD_ANIM_MOVE_LEFT) :
                                    (vertical ? LV_SCR_LOAD_ANIM_MOVE_BOTTOM : LV_SCR_LOAD_ANIM_MOVE_RIGHT), 250, 0, false);
        return;
    }
    chg = (typeof(chg)){ true, dir, vertical, change, user, to };
    lv_async_call(change_run, NULL);
}

void slide_change(int dir, bool vertical, void (*change)(void *user), void *user)
{
    queue_change(dir, vertical, change, user, NULL);
}

void slide_to(lv_obj_t *scr, int dir, bool vertical)
{
    if (scr != lv_screen_active()) queue_change(dir, vertical, NULL, NULL, scr);
}

// Keeps the neighbours of the page shown ready (see nb): one picture per tick, only while nobody touches the screen
static void idle_tick(lv_timer_t *t)
{
    if (!ready() || slide_busy() || lv_tick_elaps(touched_ms) < IDLE_MS) return;
    lv_obj_t *pg = pager_under(lv_screen_active());
    if (!pg) return;
    int cur = pager_current(pg);
    for (int i = 0; i < 2; i++) {
        lv_obj_t *page = pager_page(pg, cur + (i ? 1 : -1));
        if (!page || !nb[i].buf) continue;
        if (nb[i].page == page && lv_tick_elaps(nb[i].at) < STALE_MS) continue;
        lv_obj_update_layout(lv_obj_get_screen(page));
        bool ok = lv_snapshot_take_to_draw_buf(page, LV_COLOR_FORMAT_RGB565, nb[i].buf) == LV_RESULT_OK;
        nb[i].page = ok ? page : NULL;
        nb[i].at = lv_tick_get();
        return;                                             // one per tick: LVGL stays responsive
    }
}

static void where_slide(char *out, size_t n) { snprintf(out, n, " slide_phase=%d", phase); }

void slide_init(void)
{
    extern const forge_panel_t *forge_lvgl_panel(void);
    P = forge_lvgl_panel();
    W = lv_display_get_horizontal_resolution(NULL);
    H = lv_display_get_vertical_resolution(NULL);
    // Two full-screen pictures in PSRAM (LVGL's heap): 434 KB each at 466x466
    shadow = lv_draw_buf_create(W, H, LV_COLOR_FORMAT_RGB565, 0);
    other = lv_draw_buf_create(W, H, LV_COLOR_FORMAT_RGB565, 0);
    if (!P || !shadow || !other) { ESP_LOGW(TAG, "no pictures: LVGL's own animations"); return; }
    lv_draw_buf_clear(shadow, NULL);
    P->set_flush_hook(flushed);
    P->set_read_hook(read_hook);
    lv_display_add_event_cb(lv_display_get_default(), rendered, LV_EVENT_RENDER_READY, NULL);
    // LVGL's own scroll starts after 10 px too: a read landing between its decision and ours went to LVGL (a list
    // scrolled at ~20 fps, the touch read only between its frames). Ours decides at DRAG_PX, LVGL's at twice that
    // (LESSONS L99).
    lv_indev_t *in = lv_indev_get_next(NULL);
    if (in) lv_indev_set_scroll_limit(in, 2 * DRAG_PX);
    lv_obj_invalidate(lv_screen_active());                 // a whole frame for the shadow
    for (int i = 0; i < npagers; i++) pager_freeze(pagers[i]);
    for (int i = 0; i < 2; i++) nb[i].buf = lv_draw_buf_create(W, H, LV_COLOR_FORMAT_RGB565, 0);   // 434 KB each
    if (!nb[0].buf || !nb[1].buf) ESP_LOGW(TAG, "no room to keep neighbours ready: rendered when a drag starts");
    lv_timer_create(idle_tick, 100, NULL);
    testcon_add_where(where_slide);
}
