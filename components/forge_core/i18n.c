// Display language core (see forge_i18n.h). No ESP-IDF APIs: tests/host compiles it.
#include "forge_i18n.h"
#include <stdio.h>
#include <string.h>

static const char *const *texts;
static int ntexts;

static const char *const LANG_CODE[LANG_COUNT] = { "en", "fr" };
static const char *const LANG_NAME[LANG_COUNT] = { "English", "Français" };

static const char *const WD_FULL[LANG_COUNT][7] = {
    { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" },
    { "Dimanche", "Lundi", "Mardi", "Mercredi", "Jeudi", "Vendredi", "Samedi" },
};
static const char *const WD_SHORT[LANG_COUNT][7] = {
    { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" },
    { "Dim.", "Lun.", "Mar.", "Mer.", "Jeu.", "Ven.", "Sam." },
};
static const char *const MONTH[LANG_COUNT][12] = {
    { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October",
      "November", "December" },
    { "janvier", "février", "mars", "avril", "mai", "juin", "juillet", "août", "septembre", "octobre",
      "novembre", "décembre" },                      // lowercase: they're used inside a date
};

static volatile lang_t lang = LANG_EN;

void i18n_init(const char *const *t, int count) { texts = t; ntexts = count; }
void i18n_set(lang_t l) { lang = l < LANG_COUNT ? l : LANG_EN; }
lang_t i18n_lang(void) { return lang; }
const char *i18n_code(lang_t l) { return LANG_CODE[l < LANG_COUNT ? l : 0]; }
const char *i18n_name(lang_t l) { return LANG_NAME[l < LANG_COUNT ? l : 0]; }

lang_t i18n_from_code(const char *code)
{
    for (int i = 0; i < LANG_COUNT; i++) if (code && !strcmp(code, LANG_CODE[i])) return i;
    return LANG_EN;
}

const char *i18n_text(int id)
{
    if (!texts || id < 0 || id >= ntexts) return "";
    const char *s = texts[id * LANG_COUNT + lang];
    return s && s[0] ? s : texts[id * LANG_COUNT + LANG_EN];   // an empty text showed as nothing (weather_amoled)
}

const char *tr_weekday(int wday, bool full)
{
    wday = ((wday % 7) + 7) % 7;
    return full ? WD_FULL[lang][wday] : WD_SHORT[lang][wday];
}

void tr_date_long(const struct tm *tm, char *out, int n)
{
    int m = tm->tm_mon % 12, d = tm->tm_mday;
    if (lang == LANG_FR)
        snprintf(out, n, "%s %d%s %s", WD_FULL[LANG_FR][tm->tm_wday % 7], d, d == 1 ? "er" : "", MONTH[LANG_FR][m]);
    else
        snprintf(out, n, "%s, %s %d", WD_FULL[LANG_EN][tm->tm_wday % 7], MONTH[LANG_EN][m], d);
}

void tr_date_ymd(int y, int m, int d, char *out, int n)
{
    m = (m - 1) % 12;
    if (m < 0) m = 0;
    if (lang == LANG_FR) snprintf(out, n, "%d%s %s %d", d, d == 1 ? "er" : "", MONTH[LANG_FR][m], y);
    else snprintf(out, n, "%s %d, %d", MONTH[LANG_EN][m], d, y);
}
