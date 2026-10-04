#pragma once
#include <stdbool.h>
#include "lvgl.h"

// Full-screen pager: pages side by side (horizontal) or stacked (vertical) in a scroller that snaps one page at a
// time, so a page follows the finger, snaps into place, and bounces at the first and last page (LVGL elastic
// scrolling). (In weather_amoled the hourly view and the weather screen used it; their drags became pictures, slide.c,
// for 60 fps: see docs/LESSONS.md, LVGL.)
//
// Pages are plain containers: not clickable, presses and gestures bubble up through the pager (which stays
// clickable so it can scroll) to the screen. A gesture across the pager's direction still reaches the screen.

typedef void (*pager_cb_t)(int page, void *user);

// on_change: while dragging, each time another page reaches the middle (page dots).
// on_settle: when scrolling stops, with the page it settled on.
lv_obj_t *pager_create(lv_obj_t *parent, bool vertical, int pages, pager_cb_t on_change, pager_cb_t on_settle,
                       void *user);
lv_obj_t *pager_page(lv_obj_t *pager, int i);
void pager_go(lv_obj_t *pager, int i, bool anim);   // show page i
int pager_current(lv_obj_t *pager);                // the page in the middle now
int pager_count(lv_obj_t *pager);
bool pager_vertical(lv_obj_t *pager);
int pager_index(lv_obj_t *pager, const lv_obj_t *page);   // -1: not one of its pages

// For drags drawn as pictures (outside LVGL): the finger no longer scrolls the pager (pager_freeze); a page's picture is
// taken with the pager moved there and back within one LVGL cycle (pager_peek: no callbacks); the drag ends with
// pager_switch (on_change and on_settle, like a scroll that settled there).
void pager_freeze(lv_obj_t *pager);
void pager_peek(lv_obj_t *pager, int i);
void pager_switch(lv_obj_t *pager, int i);
