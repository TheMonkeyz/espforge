// Snapping full-screen pager (see pager.h)
#include "pager.h"
#include "lvgl.h"
#define DISP_W lv_display_get_horizontal_resolution(NULL)
#define DISP_H lv_display_get_vertical_resolution(NULL)

typedef struct {
    bool vertical, quiet;              // quiet: moved by pager_peek / pager_switch, no callbacks from the scroll events
    int pages, cur;
    pager_cb_t on_change, on_settle;
    void *user;
    lv_obj_t *page[];
} pager_t;

static int page_at(lv_obj_t *o, const pager_t *p)
{
    int pos = p->vertical ? lv_obj_get_scroll_y(o) : lv_obj_get_scroll_x(o);
    int size = p->vertical ? DISP_H : DISP_W;
    int i = (pos + size / 2) / size;
    return i < 0 ? 0 : i >= p->pages ? p->pages - 1 : i;
}

static void scrolled(lv_event_t *e)
{
    lv_obj_t *o = lv_event_get_current_target(e);
    if (lv_event_get_target(e) != o) return;   // a page's own list scrolling (bubbled up), not the pager
    pager_t *p = lv_obj_get_user_data(o);
    if (p->quiet) return;
    int i = page_at(o, p);
    if (i != p->cur) {
        p->cur = i;
        if (p->on_change) p->on_change(i, p->user);
    }
    if (lv_event_get_code(e) == LV_EVENT_SCROLL_END && p->on_settle) p->on_settle(i, p->user);
}

static void deleted(lv_event_t *e)
{
    if (lv_event_get_target(e) == lv_event_get_current_target(e)) lv_free(lv_obj_get_user_data(lv_event_get_target(e)));
}

lv_obj_t *pager_create(lv_obj_t *parent, bool vertical, int pages, pager_cb_t on_change, pager_cb_t on_settle,
                       void *user)
{
    pager_t *p = lv_malloc_zeroed(sizeof(pager_t) + pages * sizeof(lv_obj_t *));
    *p = (pager_t){ .vertical = vertical, .pages = pages, .on_change = on_change, .on_settle = on_settle,
                    .user = user };
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, DISP_W, DISP_H);
    lv_obj_set_user_data(o, p);
    lv_obj_set_scroll_dir(o, vertical ? LV_DIR_VER : LV_DIR_HOR);
    if (vertical) lv_obj_set_scroll_snap_y(o, LV_SCROLL_SNAP_CENTER);
    else lv_obj_set_scroll_snap_x(o, LV_SCROLL_SNAP_CENTER);
    lv_obj_add_flag(o, LV_OBJ_FLAG_SCROLL_ONE | LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_scrollbar_mode(o, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(o, scrolled, LV_EVENT_SCROLL, NULL);
    lv_obj_add_event_cb(o, scrolled, LV_EVENT_SCROLL_END, NULL);
    lv_obj_add_event_cb(o, deleted, LV_EVENT_DELETE, NULL);
    for (int i = 0; i < pages; i++) {
        lv_obj_t *pg = p->page[i] = lv_obj_create(o);
        lv_obj_remove_style_all(pg);
        lv_obj_set_size(pg, DISP_W, DISP_H);
        lv_obj_set_pos(pg, vertical ? 0 : i * DISP_W, vertical ? i * DISP_H : 0);
        lv_obj_remove_flag(pg, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(pg, LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    }
    return o;
}

lv_obj_t *pager_page(lv_obj_t *o, int i)
{
    pager_t *p = lv_obj_get_user_data(o);
    return i >= 0 && i < p->pages ? p->page[i] : NULL;
}

void pager_go(lv_obj_t *o, int i, bool anim)
{
    pager_t *p = lv_obj_get_user_data(o);
    lv_obj_update_layout(o);
    if (p->vertical) lv_obj_scroll_to_y(o, i * DISP_H, anim ? LV_ANIM_ON : LV_ANIM_OFF);
    else lv_obj_scroll_to_x(o, i * DISP_W, anim ? LV_ANIM_ON : LV_ANIM_OFF);
    if (!anim) p->cur = i;
}

int pager_current(lv_obj_t *o)
{
    return page_at(o, lv_obj_get_user_data(o));
}

int pager_count(lv_obj_t *o) { return ((pager_t *)lv_obj_get_user_data(o))->pages; }

int pager_index(lv_obj_t *o, const lv_obj_t *page)
{
    pager_t *p = lv_obj_get_user_data(o);
    for (int i = 0; i < p->pages; i++) if (p->page[i] == page) return i;
    return -1;
}

static void move(lv_obj_t *o, pager_t *p, int i)
{
    p->quiet = true;
    lv_obj_update_layout(o);
    if (p->vertical) lv_obj_scroll_to_y(o, i * DISP_H, LV_ANIM_OFF);
    else lv_obj_scroll_to_x(o, i * DISP_W, LV_ANIM_OFF);
    p->quiet = false;
}

void pager_peek(lv_obj_t *o, int i) { move(o, lv_obj_get_user_data(o), i); }

void pager_switch(lv_obj_t *o, int i)
{
    pager_t *p = lv_obj_get_user_data(o);
    move(o, p, i);
    p->cur = i;
    if (p->on_change) p->on_change(i, p->user);
    if (p->on_settle) p->on_settle(i, p->user);
}

void pager_freeze(lv_obj_t *o) { lv_obj_set_scroll_dir(o, LV_DIR_NONE); }
bool pager_vertical(lv_obj_t *o) { return ((pager_t *)lv_obj_get_user_data(o))->vertical; }
