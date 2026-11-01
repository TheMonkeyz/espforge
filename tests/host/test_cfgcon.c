// cfgcon.c: settings over the USB console (docs/PROTOCOL.md §2 "cfg"): the field table, the JSON of "cfg" and
// "cfg schema" (escapes, types, a buffer too small), and "cfg set" (bool, int range and step, select options, colour,
// text length and escapes, read-only and unknown keys), also through the console command itself (testcon_dispatch).
#include <stdio.h>
#include <string.h>
#include "check.h"
#include "cfgcon.h"
#include "testcon.h"

static bool dim = true;
static int bright = 80, set_calls;
static uint32_t color = 0xff8000;
static char lang[8] = "en", name[CFGCON_TEXT_MAX + 1] = "Desk \"A\"\\1";

static void get_dim(cfgcon_value_t *v) { v->b = dim; }
static const char *set_dim(const cfgcon_value_t *v) { dim = v->b; set_calls++; return NULL; }
static void get_bright(cfgcon_value_t *v) { v->i = bright; }
static const char *set_bright(const cfgcon_value_t *v) { bright = v->i; set_calls++; return NULL; }
static void get_color(cfgcon_value_t *v) { v->rgb = color; }
static const char *set_color(const cfgcon_value_t *v) { color = v->rgb; set_calls++; return NULL; }
static void get_lang(cfgcon_value_t *v) { snprintf(v->s, sizeof(v->s), "%s", lang); }
static const char *set_lang(const cfgcon_value_t *v)
{
    snprintf(lang, sizeof(lang), "%.7s", v->s);
    set_calls++;
    return NULL;
}
static void get_name(cfgcon_value_t *v) { snprintf(v->s, sizeof(v->s), "%s", name); }
static const char *set_name(const cfgcon_value_t *v)
{
    snprintf(name, sizeof(name), "%s", v->s);
    set_calls++;
    return NULL;
}
static void get_ip(cfgcon_value_t *v) { snprintf(v->s, sizeof(v->s), "no Wi-Fi"); }
static const char *set_refused(const cfgcon_value_t *v) { set_calls++; return "not saved"; }

static const cfgcon_option_t langs[] = { { "en", "English" }, { "fr", "Français" } };
static const cfgcon_field_t f_lang = { .key = "lang", .label = "Language", .type = CFGCON_SELECT, .options = langs,
                                       .noptions = 2, .get = get_lang, .set = set_lang };
static const cfgcon_field_t f_dim = { .key = "dim", .label = "Screen dimming", .help = "in a quiet room",
                                      .type = CFGCON_BOOL, .get = get_dim, .set = set_dim };
static const cfgcon_field_t f_bright = { .key = "brightness", .label = "Brightness", .type = CFGCON_INT, .min = 5,
                                         .max = 100, .step = 5, .unit = "%", .get = get_bright, .set = set_bright };
static const cfgcon_field_t f_color = { .key = "color", .label = "Colour", .type = CFGCON_COLOR, .get = get_color,
                                        .set = set_color };
static const cfgcon_field_t f_name = { .key = "name", .label = "Name", .type = CFGCON_TEXT, .max_len = 16,
                                       .get = get_name, .set = set_name };
static const cfgcon_field_t f_ip = { .key = "ip", .label = "Address", .type = CFGCON_INFO, .get = get_ip };
static const cfgcon_field_t f_refused = { .key = "save", .label = "Saved", .type = CFGCON_BOOL, .get = get_dim,
                                          .set = set_refused };
static const cfgcon_field_t f_nosetter = { .key = "ro", .label = "Read only", .type = CFGCON_BOOL, .get = get_dim };

static int run(const char *s)
{
    char l[128];
    snprintf(l, sizeof(l), "%s", s);
    return testcon_dispatch(l);
}

// One cfgcon_set: NULL (applied) for OK, the reason for WHY
static bool why_is(const char *key, const char *value, const char *why)
{
    const char *r = cfgcon_set(key, value);
    if (r && why && !strcmp(r, why)) return true;
    if (!r && !why) return true;
    fprintf(stderr, "  cfgcon_set(%s, %s): %s, expected %s\n",
            key ? key : "NULL", value, r ? r : "NULL", why ? why : "NULL");
    return false;
}
#define OK(k, v) why_is(k, v, NULL)
#define WHY(k, v, why) why_is(k, v, why)

int main(void)
{
    char js[CFGCON_LINE_MAX];

    // No field yet: no "cfg" command; the JSON has the app and version only
    CHECK(run("cfg") == -1, "cfg before any field");
    CHECK(cfgcon_values_json(js, sizeof(js)) > 0 && !strcmp(js, "{\"app\":\"host\",\"version\":\"v0.0.0-host\"}"),
          "%s", js);

    // The table: bad keys refused, reserved ones too
    CHECK(!cfgcon_add(&(cfgcon_field_t){ .key = "Bad", .type = CFGCON_BOOL, .get = get_dim }), "upper case key");
    CHECK(!cfgcon_add(&(cfgcon_field_t){ .key = "a b", .type = CFGCON_BOOL, .get = get_dim }), "space in key");
    CHECK(!cfgcon_add(&(cfgcon_field_t){ .key = "version", .type = CFGCON_INFO, .get = get_ip }), "reserved key");
    CHECK(!cfgcon_add(&(cfgcon_field_t){ .key = "app", .type = CFGCON_INFO, .get = get_ip }), "reserved key app");
    CHECK(!cfgcon_add(&(cfgcon_field_t){ .key = "seventeen_chars_x", .type = CFGCON_INFO, .get = get_ip }), "long key");
    CHECK(!cfgcon_add(&(cfgcon_field_t){ .key = "noget", .type = CFGCON_INFO }), "no getter");
    CHECK(!cfgcon_add(NULL), "NULL");
    CHECK(cfgcon_add(&f_lang) && cfgcon_add(&f_dim) && cfgcon_add(&f_bright) && cfgcon_add(&f_color) &&
          cfgcon_add(&f_name) && cfgcon_add(&f_ip) && cfgcon_add(&f_refused) && cfgcon_add(&f_nosetter), "add");
    CHECK(cfgcon_add(&f_dim), "the same key again replaces it");

    // cfg: values by type, strings escaped
    int n = cfgcon_values_json(js, sizeof(js));
    const char *want = "{\"app\":\"host\",\"version\":\"v0.0.0-host\",\"lang\":\"en\",\"dim\":true,\"brightness\":80,"
                       "\"color\":\"#ff8000\",\"name\":\"Desk \\\"A\\\"\\\\1\",\"ip\":\"no Wi-Fi\","
                       "\"save\":true,\"ro\":true}";
    CHECK(n == (int)strlen(want) && !strcmp(js, want), "values:\n %s\n %s", js, want);
    snprintf(name, sizeof(name), "tab\there\x01");
    cfgcon_values_json(js, sizeof(js));
    CHECK(strstr(js, "\"name\":\"tab\\u0009here\\u0001\""), "control characters escaped: %s", js);

    // cfg schema: every type's own members; a field without a setter is shown as info
    n = cfgcon_schema_json(js, sizeof(js));
    const char *top = "{\"app\":\"host\",\"version\":\"v0.0.0-host\",\"fields\":[";
    CHECK(n > 0 && !strncmp(js, top, strlen(top)), "%s", js);
    CHECK(strstr(js, "{\"key\":\"lang\",\"label\":\"Language\",\"type\":\"select\",\"options\":[{\"value\":\"en\","
                     "\"label\":\"English\"},{\"value\":\"fr\",\"label\":\"Français\"}]}"), "select: %s", js);
    CHECK(strstr(js, "{\"key\":\"dim\",\"label\":\"Screen dimming\",\"type\":\"bool\","
                     "\"help\":\"in a quiet room\"}"), "bool");
    CHECK(strstr(js, "{\"key\":\"brightness\",\"label\":\"Brightness\",\"type\":\"int\",\"min\":5,\"max\":100,"
                     "\"step\":5,\"unit\":\"%\"}"), "int: %s", js);
    CHECK(strstr(js, "{\"key\":\"color\",\"label\":\"Colour\",\"type\":\"color\"}"), "color");
    CHECK(strstr(js, "{\"key\":\"name\",\"label\":\"Name\",\"type\":\"text\",\"max\":16}"), "text");
    CHECK(strstr(js, "{\"key\":\"ip\",\"label\":\"Address\",\"type\":\"info\"}"), "info");
    CHECK(strstr(js, "{\"key\":\"ro\",\"label\":\"Read only\",\"type\":\"info\"}"), "no setter = info");
    CHECK(!strcmp(js + n - 3, "}]}"), "closed: %s", js + n - 3);

    // A buffer too small: -1, cut but terminated
    char small[40];
    memset(small, 'x', sizeof(small));
    CHECK(cfgcon_schema_json(small, sizeof(small)) == -1 && strlen(small) < sizeof(small), "too small: %s", small);
    CHECK(cfgcon_values_json(small, 0) == -1, "n = 0");

    // cfg set: bool
    set_calls = 0;
    CHECK(OK("dim", "off") && !dim && OK("dim", "1") && dim && OK("dim", "false") && !dim && OK("dim", "on") && dim,
          "bool forms");
    CHECK(WHY("dim", "maybe", "expected on or off") && WHY("dim", "", "expected on or off") && dim, "bool refused");
    // int: range and step (from min)
    CHECK(OK("brightness", "5") && bright == 5 && OK("brightness", "100") && bright == 100 && OK("brightness", "55"),
          "int in range: %d", bright);
    CHECK(WHY("brightness", "0", "out of range") && WHY("brightness", "105", "out of range") &&
          WHY("brightness", "-5", "out of range"), "int bounds");
    CHECK(WHY("brightness", "52", "not a step of the range") && bright == 55, "int step");
    CHECK(WHY("brightness", "5x", "expected a whole number") && WHY("brightness", "", "expected a whole number") &&
          WHY("brightness", "99999999999", "expected a whole number"), "int parse");
    // select
    CHECK(OK("lang", "fr") && !strcmp(lang, "fr"), "select");
    CHECK(WHY("lang", "de", "not one of the options") && WHY("lang", "FR", "not one of the options") &&
          !strcmp(lang, "fr"), "select refused");
    // color
    CHECK(OK("color", "#00FF80") && color == 0x00ff80 && OK("color", "102030") && color == 0x102030, "color");
    CHECK(WHY("color", "#fff", "expected RRGGBB") && WHY("color", "12345g", "expected RRGGBB") && color == 0x102030,
          "color refused");
    // text: escapes, length (bytes), control characters
    CHECK(OK("name", "Salle%20de%20bain") && !strcmp(name, "Salle de bain"), "text escapes: [%s]", name);
    CHECK(OK("name", "100%25") && !strcmp(name, "100%"), "percent: [%s]", name);
    CHECK(OK("name", "50%") && !strcmp(name, "50%"), "lone percent kept: [%s]", name);
    CHECK(OK("name", "") && !strcmp(name, ""), "empty text");
    CHECK(OK("name", "Cuisine é") && !strcmp(name, "Cuisine é"), "UTF-8");
    CHECK(OK("name", "sixteen_chars_ab") && WHY("name", "seventeen_chars_a", "too long"), "length");
    CHECK(WHY("name", "a%09b", "control character") && WHY("name", "a%00b", "control character") &&
          !strcmp(name, "sixteen_chars_ab"), "control");
    // read-only, unknown, a setter's own refusal
    CHECK(WHY("ip", "1.2.3.4", "read-only") && WHY("ro", "on", "read-only"), "read-only");
    CHECK(WHY("nope", "1", "unknown key") && WHY(NULL, "1", "unknown key"), "unknown key");
    int before = set_calls;
    CHECK(WHY("save", "on", "not saved") && set_calls == before + 1, "setter refused");
    before = set_calls;
    CHECK(WHY("brightness", "3", "out of range") && set_calls == before, "a bad value never reaches the setter");

    // Through the console: words joined again, the other forms answered
    CHECK(run("cfg set name Salle de  bain") == 0 && !strcmp(name, "Salle de bain"), "words: [%s]", name);
    CHECK(run("cfg set name") == 0 && !strcmp(name, ""), "set to empty: [%s]", name);
    CHECK(run("cfg set brightness 70") == 0 && bright == 70, "set int");
    CHECK(run("cfg set brightness 71") == 0 && bright == 70, "set refused keeps the value");
    CHECK(run("cfg") == 0 && run("cfg schema") == 0 && run("cfg bogus") == 0 && run("cfg set") == 0, "other forms");

    return check_done("test_cfgcon");
}
