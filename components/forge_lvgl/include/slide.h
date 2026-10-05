#pragma once
#include <stdbool.h>
#include "lvgl.h"

// Moves drawn as pictures copied straight to the panel (~60 fps) instead of LVGL redrawing every widget for every
// frame (~25 fps for a simple full screen, 10-15 for a busy one: weather_amoled, docs/LESSONS.md "LVGL").
//
// - The picture of the screen shown follows the panel (every area LVGL flushes is copied into it), so a move never
//   waits for it. The current page's neighbours are rendered off-display while nobody touches (refreshed every 2 s),
//   so a drag's first frame comes ~15 ms after the 10 px that make it a drag; one not ready is rendered then
//   (lv_snapshot, ~30-40 ms). In-place changes and screen loads render the new picture when they start.
// - Pager drags: slide_pager() takes over a pager's finger drags. The drag is recognised in the touch read (before
//   LVGL acts on it), then followed outside LVGL: the neighbour page comes in under the finger, the ends resist and
//   bounce back, release past a third or a flick goes on, else back. Then pager_switch() and LVGL takes over again.
// - In-place changes (slide_change) and screen loads (slide_to): the old picture slides out, the new one in.
// Everything runs in the LVGL task with the display lock held. Without the board's panel hooks or PSRAM for two
// pictures, the same calls fall back to LVGL's own animation (or none). Log lines: "slide: ...".

void slide_init(void);                       // forge_lvgl_set_panel() calls it
void slide_pager(lv_obj_t *pager);           // up to 4 pagers; freezes LVGL's own scrolling of it
// The active screen changes in place: change(user) is called, then the old picture slides out towards -dir (dir 1:
// the new state comes in from the right / below). For pages that are one screen with different contents.
void slide_change(int dir, bool vertical, void (*change)(void *user), void *user);
void slide_to(lv_obj_t *scr, int dir, bool vertical);   // lv_screen_load with a slide; dir 1: from the right / below
bool slide_busy(void);                       // a move is queued or running
