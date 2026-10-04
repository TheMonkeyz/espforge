#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Text from outside (a network name, a place, anything a phone typed) may hold characters the display's font can't
// draw: emoji showed as boxes ("tofu"). TinyTTF can't tell (it always returns a glyph: the box), so the font's own
// character map is read once (the TTF's cmap, format 4: the Basic Multilingual Plane; emoji above it are never there).
// No ESP-IDF or LVGL calls: tests/host checks it with main/montserrat.ttf.

void textfit_init(const uint8_t *ttf, size_t len);   // the embedded font; false coverage (keep all) if it can't be read
bool textfit_has(uint32_t cp);                       // the font draws this character
// in -> out without the characters the font lacks (emoji, their joiners and variation selectors), spaces left
// doubled by a removal collapsed, trimmed. Returns out's length. Without textfit_init: a plain copy.
size_t textfit(const char *in, char *out, size_t n);
