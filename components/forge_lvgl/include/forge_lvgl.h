#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

// LVGL helpers shared by every espforge app. The board owns LVGL (display driver, task, lock); it tells forge_lvgl
// how to take the lock with forge_lvgl_init(). Everything here that touches LVGL takes that lock, and gives up after
// 2 s when called from the test console, so "where" can still answer.

typedef bool (*forge_lock_fn)(int timeout_ms);   // -1 = forever
typedef void (*forge_unlock_fn)(void);
// Called by the board once LVGL and its input device exist. Registers the test console's touch and screen commands.
void forge_lvgl_init(forge_lock_fn lock, forge_unlock_fn unlock);
bool ui_lock(int timeout_ms);
void ui_unlock(void);

/* ---- simulated finger (test console) ----
 * tap/press/swipe/drag replace the touch controller's report while active, so long-press, gestures and scrolling go
 * through exactly the same code as a real finger. The board's touch read callback asks finger_injected() first. */
void finger_inject(bool down, int x, int y);   // screen pixels
void finger_inject_end(void);                  // back to the real controller
bool finger_injected(int *x, int *y, bool *down);

uint32_t lvgl_mem_fallbacks(void);             // LVGL blocks put in internal RAM because PSRAM was full
