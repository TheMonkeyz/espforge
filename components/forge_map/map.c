// The map task and its kept pictures (forge_map.h): fmap_set_center() picks a slot (map_slots.c) and wakes the task,
// the task draws the current slot's tiles (map_draw.c) and reports each one. One mutex guards the slots' bookkeeping;
// the pixels themselves are written by the task without it (an app reading px meanwhile sees a tile half drawn, then
// the next update).
#include "map_priv.h"
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "svc.h"
#include "net.h"

static const char *TAG = "fmap";
// The task's stack, internal RAM: the TLS handshake of esp_http_client runs in it (rtcquebec's map task: 6 KB) plus
// the tile URL (256 bytes); the body (PSRAM) and png_rows' ~50 KB (PSRAM) are not on it
#define TASK_STACK (7 * 1024)

typedef struct {
    uint16_t *px;                      // allocated on the slot's first use, kept (reused for the next view)
    fmap_state_t state;
    int tiles, done;
} slot_t;

// All of it in PSRAM (L185: plain statics are internal RAM, the scarce kind, in every app that links the component).
// .ext_ram.bss is zeroed at start but takes no initialisers: slot numbers are kept + 1 (0 = none), as in fmap_slots_t.
EXT_RAM_BSS_ATTR static struct {
    fmap_opts_t o;
    fmap_slots_t t;                    // keys, LRU stamps, the current / previous / busy slot
    slot_t slot[FMAP_SLOTS_MAX];
    int svc;
    SemaphoreHandle_t mu;
    TaskHandle_t task;
} m;

static void lock(void) { xSemaphoreTake(m.mu, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(m.mu); }
static void tell(void) { if (m.o.updated) m.o.updated(m.o.user); }

// fmap_draw's hooks, for slot s (in user): stop once set_center has moved on to another slot; count the tiles drawn
static bool moved_on(void *user)
{
    lock();
    bool moved = m.t.cur1 != (int)(intptr_t)user + 1;
    unlock();
    return moved;
}

static void progress(int ok, int total, void *user)
{
    lock();
    m.slot[(int)(intptr_t)user].done = ok;
    unlock();
    tell();
}

static void map_task(void *arg)
{
    fmap_body_t b = { 0 };
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        for (;;) {                                        // the current slot while it waits for tiles
            lock();
            int s = m.t.cur1 - 1;
            bool go = s >= 0 && m.t.key[s].valid && m.slot[s].state == FMAP_LOADING;
            fmap_slot_key_t k = go ? m.t.key[s] : (fmap_slot_key_t){ 0 };
            uint16_t *px = go ? m.slot[s].px : NULL;
            m.t.busy1 = go ? s + 1 : 0;
            unlock();
            if (!go) break;
            if (!net_is_connected()) {                    // not a failure: the picture waits for Wi-Fi
                lock();
                m.t.busy1 = 0;
                unlock();
                vTaskDelay(pdMS_TO_TICKS(2000));
                continue;
            }
            int ok = 0, total = 0;
            bool all = fmap_draw(&m.o, m.svc, k.zoom, k.ox, k.oy, px, m.o.w, m.o.h, &b, moved_on, (void *)(intptr_t)s,
                                 progress, (void *)(intptr_t)s, &ok, &total);
            lock();
            // Cancelled or short of tiles: FAILED, kept with what it has; set_center tries it again when it comes back
            m.slot[s].state = all ? FMAP_READY : FMAP_FAILED;
            m.slot[s].done = ok;
            m.t.busy1 = 0;
            unlock();
            tell();
        }
        free(b.buf);                                      // up to 256 KB of PSRAM back between maps
        b = (fmap_body_t){ 0 };
    }
}

bool fmap_create(const fmap_opts_t *opts)
{
    if (m.mu) { ESP_LOGE(TAG, "fmap_create: already created"); return false; }
    if (!opts || opts->w <= 0 || opts->h <= 0 || !opts->tile_url || opts->zoom_min < 0 ||
        opts->zoom_max > GEO_ZOOM_MAX || opts->zoom_min > opts->zoom_max) {
        ESP_LOGE(TAG, "fmap_create: unusable options");
        return false;
    }
    m.o = *opts;
    m.t.n = opts->psram_slots < 1 ? 1 : opts->psram_slots > FMAP_SLOTS_MAX ? FMAP_SLOTS_MAX : opts->psram_slots;
    m.svc = -1;
    if (opts->svc_name && (m.svc = svc_find(opts->svc_name)) < 0) m.svc = svc_add(opts->svc_name, opts->svc_api, NULL);
    if (!(m.mu = xSemaphoreCreateMutex())) return false;
    // Core 0, below LVGL's task: the decode yields every 64 rows (map_draw.c)
    if (xTaskCreatePinnedToCore(map_task, "fmap", TASK_STACK, NULL, 3, &m.task, 0) != pdPASS) {
        ESP_LOGE(TAG, "fmap_create: no memory for the task");
        m.task = NULL;
        return false;
    }
    return true;
}

static void fill(fmap_view_t *out, int s)
{
    const slot_t *sl = &m.slot[s];
    *out = (fmap_view_t){ .px = sl->px, .w = m.o.w, .h = m.o.h, .zoom = m.t.key[s].zoom, .ox = m.t.key[s].ox,
                          .oy = m.t.key[s].oy, .state = sl->state, .tiles = sl->tiles, .done = sl->done };
}

void fmap_set_center(double lat, double lon, int zoom, fmap_view_t *out)
{
    if (!m.mu) { *out = (fmap_view_t){ .state = FMAP_FAILED }; return; }
    zoom = zoom < m.o.zoom_min ? m.o.zoom_min : zoom > m.o.zoom_max ? m.o.zoom_max : zoom;
    double ox, oy;
    geo_origin(lat, lon, zoom, m.o.w, m.o.h, &ox, &oy);
    lock();
    bool hit;
    int s = fmap_slots_take(&m.t, zoom, ox, oy, &hit);
    if (s < 0) {                                          // only with 1 slot while the task is still drawing into it
        unlock();
        ESP_LOGW(TAG, "zoom %d at %.0f,%.0f: every picture in use", zoom, ox, oy);
        *out = (fmap_view_t){ .w = m.o.w, .h = m.o.h, .zoom = zoom, .ox = ox, .oy = oy, .state = FMAP_FAILED };
        return;
    }
    slot_t *sl = &m.slot[s];
    if (hit && sl->state == FMAP_FAILED) sl->state = FMAP_LOADING;   // a failed or cancelled one: tried again
    else if (hit && sl->state == FMAP_READY) ESP_LOGI(TAG, "zoom %d at %.0f,%.0f: kept", zoom, ox, oy);
    if (!hit) {                                           // a new picture in that slot (its key is set)
        if (!sl->px) sl->px = heap_caps_malloc((size_t)m.o.w * m.o.h * 2, MALLOC_CAP_SPIRAM);
        geo_cover_t c;
        geo_cover(ox, oy, m.o.w, m.o.h, zoom, &c);
        sl->tiles = c.count;
        sl->done = 0;
        sl->state = sl->px ? FMAP_LOADING : FMAP_FAILED;
        m.t.key[s].valid = sl->px != NULL;                // (no picture: asked again next time)
        if (sl->px) for (size_t i = 0; i < (size_t)m.o.w * m.o.h; i++) sl->px[i] = m.o.empty565;
        else ESP_LOGE(TAG, "no memory for a map picture (%d KB)", m.o.w * m.o.h * 2 / 1024);
    }
    fill(out, s);
    bool start = sl->state == FMAP_LOADING;
    unlock();
    if (start && m.task) xTaskNotifyGive(m.task);
}

void fmap_status(fmap_view_t *out)
{
    if (!m.mu) { *out = (fmap_view_t){ .state = FMAP_FAILED }; return; }
    lock();
    if (m.t.cur1) fill(out, m.t.cur1 - 1);
    else *out = (fmap_view_t){ .state = FMAP_FAILED };
    unlock();
}

const char *fmap_attribution(void) { return m.o.attribution ? m.o.attribution : ""; }
