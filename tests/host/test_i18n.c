// i18n: every app text (main/i18n_strings.h) exists in every language and keeps the English printf conversions;
// forge_core/i18n.c falls back to English for an empty text, and its dates follow each language's rules.
#include <string.h>
#include <time.h>
#include "check.h"
#include "forge_i18n.h"

enum {
#define X(id, ...) id,
#include "i18n_strings.h"
#undef X
    T_COUNT
};
static const char *const texts[T_COUNT][LANG_COUNT] = {
#define X(id, ...) [id] = { __VA_ARGS__ },
#include "i18n_strings.h"
#undef X
};
static const char *const names[T_COUNT] = {
#define X(id, ...) [id] = #id,
#include "i18n_strings.h"
#undef X
};

// The printf conversions of a format, in order ("%s%d" for "Wi-Fi %s · %d dBm")
static void convs(const char *s, char *out, size_t n)
{
    size_t k = 0;
    for (; *s && k + 3 < n; s++) {
        if (*s != '%') continue;
        s++;
        if (*s == '%') continue;
        while (*s && strchr("0123456789.-+ #lhz", *s)) s++;        // flags, width, length
        if (!*s) break;
        out[k++] = '%';
        out[k++] = *s;
    }
    out[k] = 0;
}

int main(void)
{
    for (int id = 0; id < T_COUNT; id++) {
        char en[32], other[32];
        convs(texts[id][LANG_EN] ? texts[id][LANG_EN] : "", en, sizeof(en));
        for (int l = 0; l < LANG_COUNT; l++) {
            const char *s = texts[id][l];
            // An empty text showed as nothing at all (the source project: two update messages in a draft language)
            CHECK(s && s[0], "%s is empty or missing in language %d", names[id], l);
            if (!s) continue;
            convs(s, other, sizeof(other));
            CHECK(!strcmp(en, other), "%s, language %d: conversions %s, English has %s", names[id], l, other, en);
        }
    }

    // i18n.c: the language's text, English when it is empty, "" out of range
    static const char *const two[2][LANG_COUNT] = { { "Yes", "Oui" }, { "Only English", "" } };
    i18n_init(&two[0][0], 2);
    i18n_set(LANG_FR);
    CHECK(!strcmp(i18n_text(0), "Oui"), "%s", i18n_text(0));
    CHECK(!strcmp(i18n_text(1), "Only English"), "empty French falls back: %s", i18n_text(1));
    CHECK(!strcmp(i18n_text(2), ""), "out of range: %s", i18n_text(2));
    CHECK(i18n_from_code("fr") == LANG_FR && i18n_from_code("xx") == LANG_EN, "language codes");

    // Dates: Québec French "1er" for the first of the month, lowercase month names
    char d[64];
    struct tm tm = { .tm_year = 126, .tm_mon = 9, .tm_mday = 1, .tm_wday = 4 };   // Thursday, October 1, 2026
    tr_date_long(&tm, d, sizeof(d));
    CHECK(!strcmp(d, "Jeudi 1er octobre"), "%s", d);
    tr_date_ymd(2026, 9, 30, d, sizeof(d));
    CHECK(!strcmp(d, "30 septembre 2026"), "%s", d);
    i18n_set(LANG_EN);
    tr_date_long(&tm, d, sizeof(d));
    CHECK(!strcmp(d, "Thursday, October 1"), "%s", d);

    // The app's real table, as app_text.c hands it over
    i18n_init(&texts[0][0], T_COUNT);
    for (int l = 0; l < LANG_COUNT; l++) {
        i18n_set(l);
        for (int id = 0; id < T_COUNT; id++) CHECK(i18n_text(id)[0], "%s shows nothing in language %d", names[id], l);
    }
    return check_done("i18n");
}
