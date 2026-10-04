#pragma once
#include <stdbool.h>
#include <time.h>

// Display language core. The texts are the app's: main/i18n_strings.h has one line per text, one column per language
// (X(T_ID, "English", "Français")), and main/app_text.h turns it into an enum and tr(). Framework components never
// show text themselves: they return codes the app translates.
// Adding a language: a LANG_ value here, its code and name in i18n.c, its weekday and month names, and a column in
// every line of i18n_strings.h (tests/host/test_i18n.c checks every text in every language).

typedef enum { LANG_EN, LANG_FR, LANG_COUNT } lang_t;

void i18n_init(const char *const *texts, int count);   // texts[id * LANG_COUNT + lang]
const char *i18n_text(int id);            // in the current language; English when missing *or empty* ("" isn't text)
lang_t i18n_lang(void);
void i18n_set(lang_t l);
const char *i18n_code(lang_t l);          // "en", "fr"
const char *i18n_name(lang_t l);          // "English", "Français" (in its own language)
lang_t i18n_from_code(const char *code);  // unknown -> LANG_EN

// Saved language (NVS "i18n"/"lang"): i18n_load() at start-up after NVS is up, i18n_save() after i18n_set()
void i18n_load(void);
bool i18n_save(void);

const char *tr_weekday(int wday, bool full);   // 0 = Sunday; "Wednesday" / "Wed", "Mercredi" / "Mer."
// "Wednesday, September 30" / "Mercredi 1er octobre" (Québec: "1er" for the first of the month)
void tr_date_long(const struct tm *tm, char *out, int n);
void tr_date_ymd(int y, int m, int d, char *out, int n);   // m 1..12: "September 30, 2026" / "30 septembre 2026"
