// Settings over the USB console (cfgcon.h): the field table, the JSON of "cfg" and "cfg schema", and the checks of
// "cfg set". Besides the log and testcon_register (testcon_registry.c, pure too), only the app's name and version
// come from ESP-IDF, so tests/host compiles this file as it is (test_cfgcon.c) and web/emu with its shims.
#include "cfgcon.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "testcon.h"

#if defined(ESP_PLATFORM) || defined(__EMSCRIPTEN__)    // the firmware, and the browser emulator's shims (web/emu)
#include "esp_attr.h"
#include "esp_app_desc.h"
static const char *app_name(void) { return esp_app_get_description()->project_name; }
static const char *app_version(void) { return esp_app_get_description()->version; }
#else
#define EXT_RAM_BSS_ATTR                                  // tests/host
static const char *app_name(void) { return "host"; }
static const char *app_version(void) { return "v0.0.0-host"; }
#endif

static const char *TAG = "test";                          // the answers are test console lines (docs/PROTOCOL.md §2)

static const cfgcon_field_t *fields[CFGCON_MAX];
static int nfields;
static bool registered;

/* ---------- JSON writer: a cursor over a fixed buffer that remembers it ran out ---------- */

typedef struct { char *out; size_t n, len; bool over; } jw_t;

static void jw_raw(jw_t *w, const char *s)
{
    for (; *s; s++) {
        if (w->len + 1 >= w->n) { w->over = true; return; }
        w->out[w->len++] = *s;
        w->out[w->len] = 0;
    }
}

// A JSON string: quotes, backslashes and control characters escaped; UTF-8 passes through (labels in French)
static void jw_str(jw_t *w, const char *s)
{
    jw_raw(w, "\"");
    for (const unsigned char *p = (const unsigned char *)(s ? s : ""); *p; p++) {
        char e[8];
        if (*p == '"') jw_raw(w, "\\\"");
        else if (*p == '\\') jw_raw(w, "\\\\");
        else if (*p == '\n') jw_raw(w, "\\n");
        else if (*p < 0x20 || *p == 0x7f) { snprintf(e, sizeof(e), "\\u%04x", *p); jw_raw(w, e); }
        else { e[0] = (char)*p; e[1] = 0; jw_raw(w, e); }
    }
    jw_raw(w, "\"");
}

static void jw_key(jw_t *w, const char *k) { jw_str(w, k); jw_raw(w, ":"); }
static void jw_int(jw_t *w, int v) { char b[16]; snprintf(b, sizeof(b), "%d", v); jw_raw(w, b); }
static int jw_end(jw_t *w) { return w->over ? -1 : (int)w->len; }

static const char *type_name(cfgcon_type_t t)
{
    static const char *const names[] = { "text", "bool", "int", "select", "color", "info" };
    return (unsigned)t < sizeof(names) / sizeof(names[0]) ? names[t] : "info";
}

static int text_max(const cfgcon_field_t *f)
{
    return f->max_len > 0 && f->max_len < CFGCON_TEXT_MAX ? f->max_len : CFGCON_TEXT_MAX;
}

static void head(jw_t *w)
{
    jw_raw(w, "{");
    jw_key(w, "app"); jw_str(w, app_name());
    jw_raw(w, ","); jw_key(w, "version"); jw_str(w, app_version());
}

/* ---------- the table ---------- */

static bool key_ok(const char *k)
{
    size_t n = k ? strlen(k) : 0;
    if (n < 1 || n > 16 || !strcmp(k, "app") || !strcmp(k, "version") || !strcmp(k, "fields")) return false;
    for (size_t i = 0; i < n; i++)
        if (!((k[i] >= 'a' && k[i] <= 'z') || (k[i] >= '0' && k[i] <= '9') || k[i] == '_')) return false;
    return true;
}

static const cfgcon_field_t *find(const char *key)
{
    for (int i = 0; i < nfields; i++)
        if (!strcmp(fields[i]->key, key)) return fields[i];
    return NULL;
}

static void cmd_cfg(int argc, char **argv);

bool cfgcon_add(const cfgcon_field_t *f)
{
    if (!f || !key_ok(f->key) || !f->get) {
        ESP_LOGE(TAG, "cfgcon: bad field '%s' (key [a-z0-9_] up to 16, not app/version; a getter)",
                 f && f->key ? f->key : "?");
        return false;
    }
    if (!registered) { testcon_register("cfg", "cfg [schema|set KEY VALUE]", cmd_cfg); registered = true; }
    for (int i = 0; i < nfields; i++)
        if (!strcmp(fields[i]->key, f->key)) { fields[i] = f; return true; }
    if (nfields == CFGCON_MAX) { ESP_LOGE(TAG, "cfgcon: no room for '%s' (CFGCON_MAX)", f->key); return false; }
    fields[nfields++] = f;
    return true;
}

/* ---------- cfg, cfg schema ---------- */

int cfgcon_values_json(char *out, size_t n)
{
    jw_t w = { out, n, 0, n == 0 };
    if (n) out[0] = 0;
    head(&w);
    for (int i = 0; i < nfields; i++) {
        const cfgcon_field_t *f = fields[i];
        cfgcon_value_t v;
        memset(&v, 0, sizeof(v));
        f->get(&v);
        v.s[sizeof(v.s) - 1] = 0;
        jw_raw(&w, ",");
        jw_key(&w, f->key);
        char b[16];
        switch (f->type) {
        case CFGCON_BOOL: jw_raw(&w, v.b ? "true" : "false"); break;
        case CFGCON_INT: jw_int(&w, v.i); break;
        case CFGCON_COLOR: snprintf(b, sizeof(b), "#%06x", (unsigned)(v.rgb & 0xffffff)); jw_str(&w, b); break;
        default: jw_str(&w, v.s); break;
        }
    }
    jw_raw(&w, "}");
    return jw_end(&w);
}

int cfgcon_schema_json(char *out, size_t n)
{
    jw_t w = { out, n, 0, n == 0 };
    if (n) out[0] = 0;
    head(&w);
    jw_raw(&w, ",\"fields\":[");
    for (int i = 0; i < nfields; i++) {
        const cfgcon_field_t *f = fields[i];
        cfgcon_type_t t = f->set || f->type == CFGCON_INFO ? f->type : CFGCON_INFO;   // no setter: read-only
        jw_raw(&w, i ? ",{" : "{");
        jw_key(&w, "key"); jw_str(&w, f->key);
        jw_raw(&w, ","); jw_key(&w, "label"); jw_str(&w, f->label ? f->label : f->key);
        jw_raw(&w, ","); jw_key(&w, "type"); jw_str(&w, type_name(t));
        if (f->help) { jw_raw(&w, ","); jw_key(&w, "help"); jw_str(&w, f->help); }
        if (t == CFGCON_INT) {
            jw_raw(&w, ","); jw_key(&w, "min"); jw_int(&w, f->min);
            jw_raw(&w, ","); jw_key(&w, "max"); jw_int(&w, f->max);
            jw_raw(&w, ","); jw_key(&w, "step"); jw_int(&w, f->step > 0 ? f->step : 1);
            if (f->unit) { jw_raw(&w, ","); jw_key(&w, "unit"); jw_str(&w, f->unit); }
        } else if (t == CFGCON_SELECT) {
            jw_raw(&w, ",\"options\":[");
            for (int k = 0; k < f->noptions; k++) {
                jw_raw(&w, k ? ",{" : "{");
                jw_key(&w, "value"); jw_str(&w, f->options[k].value);
                jw_raw(&w, ","); jw_key(&w, "label");
                jw_str(&w, f->options[k].label ? f->options[k].label : f->options[k].value);
                jw_raw(&w, "}");
            }
            jw_raw(&w, "]");
        } else if (t == CFGCON_TEXT) {
            jw_raw(&w, ","); jw_key(&w, "max"); jw_int(&w, text_max(f));
        }
        jw_raw(&w, "}");
    }
    jw_raw(&w, "]}");
    return jw_end(&w);
}

/* ---------- cfg set ---------- */

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// %XX escapes decoded (a "%" not followed by two hex digits stays as it is); false when out is too small
static bool pct_decode(const char *in, char *out, size_t n, size_t *len)
{
    size_t o = 0;
    for (const char *p = in; *p; p++) {
        int c = (unsigned char)*p;
        if (c == '%' && hexval(p[1]) >= 0 && hexval(p[2]) >= 0) { c = hexval(p[1]) << 4 | hexval(p[2]); p += 2; }
        if (o + 1 >= n) return false;
        out[o++] = (char)c;
    }
    out[o] = 0;
    *len = o;
    return true;
}

static bool parse_bool(const char *s, bool *b)
{
    static const char *const on[] = { "on", "true", "1" }, *const off[] = { "off", "false", "0" };
    for (int i = 0; i < 3; i++) {
        if (!strcmp(s, on[i])) { *b = true; return true; }
        if (!strcmp(s, off[i])) { *b = false; return true; }
    }
    return false;
}

static bool parse_int(const char *s, int *v)
{
    char *end;
    errno = 0;
    long l = strtol(s, &end, 10);
    if (!*s || *end || errno || l < -2147483647L || l > 2147483647L) return false;
    *v = (int)l;
    return true;
}

static bool parse_color(const char *s, uint32_t *rgb)
{
    if (*s == '#') s++;
    if (strlen(s) != 6) return false;
    uint32_t v = 0;
    for (int i = 0; i < 6; i++) {
        int h = hexval(s[i]);
        if (h < 0) return false;
        v = v << 4 | (uint32_t)h;
    }
    *rgb = v;
    return true;
}

const char *cfgcon_set(const char *key, const char *value)
{
    const cfgcon_field_t *f = key ? find(key) : NULL;
    if (!f) return "unknown key";
    if (f->type == CFGCON_INFO || !f->set) return "read-only";
    char raw[128];
    size_t len;
    if (!pct_decode(value ? value : "", raw, sizeof(raw), &len)) return "too long";
    if (strlen(raw) != len) return "control character";            // a decoded %00
    cfgcon_value_t v;
    memset(&v, 0, sizeof(v));
    switch (f->type) {
    case CFGCON_BOOL:
        if (!parse_bool(raw, &v.b)) return "expected on or off";
        break;
    case CFGCON_INT: {
        int step = f->step > 0 ? f->step : 1;
        if (!parse_int(raw, &v.i)) return "expected a whole number";
        if (v.i < f->min || v.i > f->max) return "out of range";
        if (((long long)v.i - f->min) % step) return "not a step of the range";
        break;
    }
    case CFGCON_COLOR:
        if (!parse_color(raw, &v.rgb)) return "expected RRGGBB";
        break;
    case CFGCON_SELECT: {
        int k = 0;
        while (k < f->noptions && strcmp(f->options[k].value, raw)) k++;
        if (k == f->noptions || len >= sizeof(v.s)) return "not one of the options";
        memcpy(v.s, raw, len + 1);
        break;
    }
    default:                                                         // CFGCON_TEXT
        if ((int)len > text_max(f)) return "too long";
        for (size_t i = 0; i < len; i++)
            if ((unsigned char)raw[i] < 0x20 || raw[i] == 0x7f) return "control character";
        memcpy(v.s, raw, len + 1);                                   // len <= CFGCON_TEXT_MAX
        break;
    }
    return f->set(&v);
}

/* ---------- the console command ---------- */

// One answer line, in PSRAM when the build allows it (internal RAM is the scarce kind, LESSONS L185); only the
// console task writes it
EXT_RAM_BSS_ATTR static char line[CFGCON_LINE_MAX];

static void cmd_cfg(int argc, char **argv)
{
    if (argc == 1 || (argc == 2 && !strcmp(argv[1], "schema"))) {
        bool schema = argc == 2;
        int n = schema ? cfgcon_schema_json(line, sizeof(line)) : cfgcon_values_json(line, sizeof(line));
        if (n < 0) ESP_LOGW(TAG, "error cfg: answer longer than %d bytes (CFGCON_LINE_MAX)", CFGCON_LINE_MAX);
        else ESP_LOGI(TAG, "%s %s", schema ? "cfg-schema" : "cfg", line);
        return;
    }
    if (argc >= 3 && !strcmp(argv[1], "set")) {
        // The words of a text, joined again with one space (the console splits the line at spaces)
        char value[120] = "";
        size_t len = 0;
        for (int i = 3; i < argc && len < sizeof(value); i++)
            len += snprintf(value + len, sizeof(value) - len, "%s%s", i > 3 ? " " : "", argv[i]);
        const char *why = cfgcon_set(argv[2], value);
        if (why) ESP_LOGW(TAG, "error cfg %s %s", argv[2], why);
        else ESP_LOGI(TAG, "ok cfg %s", argv[2]);
        return;
    }
    ESP_LOGW(TAG, "error cfg: expected cfg, cfg schema or cfg set KEY VALUE");
}
