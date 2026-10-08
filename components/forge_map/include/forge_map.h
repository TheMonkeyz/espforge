#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "forge_geo.h"

// Street map pictures from map tiles (OpenStreetMap's by default): a w x h RGB565 picture of the map around a place at
// a zoom, desaturated and dimmed for a dark AMOLED screen, built from 256 px PNG tiles decoded a row at a time
// (forge_core png_rows: ~50 KB whatever the tile, every bit depth OSM sends, LESSONS L190). From esp32-s3-rtcquebec's
// map.c (v0.3.1) and weather_amoled's radar.c, which each had their own copy. No LVGL: the app shows px as an image.
//
// Two ways to use it:
//   - fmap_create() + fmap_set_center(): the component's own task downloads in the background into PSRAM pictures
//     ("slots") kept by (zoom, origin), so going back to a place or a zoom downloads nothing. set_center never waits
//     for the network; the picture fills in as tiles arrive (opts.updated is called after each one).
//   - fmap_render(): one picture, now, in the caller's task, into the caller's buffer (weather_amoled's alert map and,
//     later, its radar's base map), with its own connection. It uses no state of the task: both can run at once.
//
// Tile servers' rules (OSM's tile usage policy): an identifying User-Agent (svc_user_agent() by default), one
// connection kept alive, few requests (kept pictures; 404s are not retried), the attribution shown with the map.
//
// TODO(phase 2): a flash cache of whole pictures, weather_amoled's "MAP7" layout (one slot per zoom in a "mapcache"
// data partition, a header {magic, zoom, ox, oy} written last so it is valid only when complete, sectors erased and
// written one at a time with vTaskDelay(1) between them and paused while the screen is busy, read back in 4 KB pieces
// (a bigger read into PSRAM borrows internal RAM)). Not in phase 1: rtcquebec doesn't cache in flash, and weather
// keeps its own cache until then.

typedef enum { FMAP_LOADING, FMAP_READY, FMAP_FAILED } fmap_state_t;

typedef struct {
    int w, h;                          // the pictures' size in pixels (the task's slots; fmap_render takes its own)
    // The tile URL: "{z}", "{x}" and "{y}" are replaced by the tile's numbers (x already wrapped, forge_geo.h), every
    // other character is copied as it is, '%' included (an encoded query or key: never used as a printf format, L195).
    // At most FMAP_URL_MAX - 1 characters once filled in; a longer one fails that tile.
    const char *tile_url;
    const char *user_agent;            // NULL: forge_net's svc_user_agent()
    const char *attribution;           // the app shows it with the map (fmap_attribution()); the component shows nothing
    // The service the tile downloads are reported to (forge_net svc.h): fmap_create() adds it unless an app already
    // did (svc_find by name); fmap_render() only reports to one that exists. NULL name: not reported.
    const char *svc_name, *svc_api;
    int zoom_min, zoom_max;            // fmap_set_center() keeps the zoom within these (0..GEO_ZOOM_MAX)
    int retries;                       // tries after the first for a tile that failed (transport error, 5xx, 429),
                                       //   1 s, 2 s... after it; a 404 or a body too big is not tried again
    int dim_pct;                       // brightness kept, % (100: as the tiles are; rtcquebec and weather: 55)
    int desat_pct;                     // % of the way to grey (0: colours kept; rtcquebec and weather: 50)
    uint16_t empty565;                 // the colour where no tile is (yet), RGB565
    int psram_slots;                   // pictures kept (w x h x 2 bytes each, allocated on first use, never freed),
                                       //   1..FMAP_SLOTS_MAX; 3 or more keep the two returned last while a third loads
    void (*updated)(void *user);       // a tile was drawn or a picture is done: from the map task (take the display
    void *user;                        //   lock there); NULL: poll fmap_status()
} fmap_opts_t;

#define FMAP_URL_MAX 256               // with the User-Agent, under esp_http_client's 512-byte request head (L193)
#define FMAP_SLOTS_MAX 8
#define FMAP_OSM_URL "https://tile.openstreetmap.org/{z}/{x}/{y}.png"

// OpenStreetMap's standard tiles with esp32-s3-rtcquebec's choices: zoom 0..19, 1 retry, half-way to grey then 55 %
// (weather_amoled's dim_map), empty rgb565(18, 20, 24), 4 slots, attribution "© OpenStreetMap contributors",
// service "OpenStreetMap" / "Map tiles". Change what differs after the call.
void fmap_opts_default(fmap_opts_t *o, int w, int h);

typedef struct {
    const uint16_t *px;                // w x h RGB565, empty565 until tiles arrive; NULL if PSRAM was short. Stays
                                       //   valid until the slot is reused (the buffers are never freed): not while it
                                       //   is one of the two pictures set_center returned last (an app shows the old
                                       //   zoom until the new one is done), unless every other slot is the one the
                                       //   task is still drawing into
    int w, h;
    int zoom;
    double ox, oy;                     // its top-left corner in world pixels at that zoom (forge_geo.h)
    fmap_state_t state;
    int tiles, done;                   // tiles it needs (geo_cover), tiles drawn
} fmap_view_t;

// Copies opts (the strings are kept as pointers: literals or static), starts the task. Once; false if the options
// are unusable (size, zoom range, no URL) or the task couldn't start. The app needs
// CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY (the component's statics are in PSRAM, L185).
bool fmap_create(const fmap_opts_t *opts);

// The picture centred on (lat, lon) at zoom (kept within zoom_min..zoom_max): a kept one, or a new one the task
// starts downloading, the one shown first if several wait. A failed one is tried again. Never waits for the network
// (a new picture is cleared to empty565 here, ~1 ms per 100 K pixels). From any task.
void fmap_set_center(double lat, double lon, int zoom, fmap_view_t *out);
// What the last fmap_set_center()'s picture is now (state, tiles done); FMAP_FAILED with px NULL before the first
void fmap_status(fmap_view_t *out);
// opts.attribution ("" if none): the app shows it with the map, in its own font and place
const char *fmap_attribution(void);

// One picture now, in the caller's task: dst (w x h RGB565) cleared to opts->empty565, then every tile of the window
// at (ox, oy) at zoom z fetched (opts' URL, User-Agent, retries) and drawn with opts' dimming. opts->w/h and the task's
// fields are not used. cancel (may be NULL) is asked before each request: true stops (the picture keeps what it has).
// *ok_tiles (may be NULL): tiles drawn. True when every tile arrived and nothing was cancelled. Several tasks may
// call it at once (each with its own connection and buffers).
bool fmap_render(const fmap_opts_t *opts, int z, double ox, double oy, uint16_t *dst, int w, int h,
                 bool (*cancel)(void *user), void *user, int *ok_tiles);

// ---- pure C, host-tested (tests/host/test_map.c) ----

// A tile URL template (fmap_opts_t.tile_url) filled in for tile (z, x, y) into out (n bytes). False if it didn't fit
// (out is then cut, still terminated) or there is no template.
bool fmap_tile_url(char *out, size_t n, const char *tmpl, int z, int x, int y);

// One pixel of a tile, as the picture shows it: desat % of the way to its grey (luma 77/150/29), then dim % of that
uint16_t fmap_dim565(uint8_t r, uint8_t g, uint8_t b, int dim_pct, int desat_pct);

