#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Settings over the USB console: the app lists its settings as fields, and the test console (testcon.h) answers
//   cfg                      -> test: cfg {"app":"<name>","version":"<ver>","<key>":<value>,...}
//   cfg schema               -> test: cfg-schema {"app":...,"version":...,"fields":[{"key","label","type",...},...]}
//   cfg set <key> <value>    -> test: ok cfg <key>   or   test: error cfg <key> <why>
// (docs/PROTOCOL.md §2). The flasher site's "Configure over USB" card (web/flash/config.js) builds its form from the
// schema, so a project without Wi-Fi or a settings page gets one with no page code: a board on a USB cable is enough.
// Every answer is one log line; values: bool true/false, int a number, color "#rrggbb", the others a string.
//
// Fields are the app's (texts in English: the labels are for the PC page, not the display). The getter and setter
// run in the console task (4 KB stack): a setter that needs a lock gives up after ~2 s (testcon.h). cfgcon checks
// the value against the field (range and step, one of the options, 6 hex digits, length) before the setter sees it,
// so a setter only applies and saves: it returns NULL when done, or a short English reason ("not saved").
//
// On the command line a text value may hold several words (joined with one space each) and %XX escapes ("%20" a
// space, "%25" a percent sign): the page escapes spaces so a text arrives exactly. The console reads lines of 119
// characters at most: "cfg set <key> <value>" must fit (config.js checks before sending).

typedef enum {
    CFGCON_TEXT,     // free text, up to max_len bytes (UTF-8), no control characters
    CFGCON_BOOL,     // on / off ("on", "off", "true", "false", "1", "0" on the command line)
    CFGCON_INT,      // min..max in steps of step (from min), shown with unit ("%", "s"; optional)
    CFGCON_SELECT,   // one of options[].value; the page shows options[].label
    CFGCON_COLOR,    // RRGGBB (a leading # is allowed); the getter fills rgb as 0xRRGGBB
    CFGCON_INFO,     // read-only text (an address, a state): no setter
} cfgcon_type_t;

typedef struct { const char *value, *label; } cfgcon_option_t;

#define CFGCON_TEXT_MAX 64          // the longest text value (bytes), whatever a field's max_len
typedef struct {
    bool b;                         // CFGCON_BOOL
    int i;                          // CFGCON_INT
    uint32_t rgb;                   // CFGCON_COLOR, 0xRRGGBB
    char s[CFGCON_TEXT_MAX + 1];    // CFGCON_TEXT, CFGCON_SELECT, CFGCON_INFO
} cfgcon_value_t;

typedef struct {
    const char *key;                // [a-z0-9_], up to 16 characters; not "app" or "version" (cfg's own)
    const char *label;              // "Screen dimming"
    const char *help;               // a hint under the field on the page (optional)
    cfgcon_type_t type;
    int min, max, step;             // CFGCON_INT (step 0 = 1)
    const char *unit;               // CFGCON_INT (optional)
    const cfgcon_option_t *options; // CFGCON_SELECT: may be filled at start-up (languages), before cfgcon_add
    int noptions;
    int max_len;                    // CFGCON_TEXT (0 or more than CFGCON_TEXT_MAX = CFGCON_TEXT_MAX)
    void (*get)(cfgcon_value_t *v);              // fills the member of its type; s starts empty
    const char *(*set)(const cfgcon_value_t *v); // NULL = applied; else why not. NULL setter = read-only
} cfgcon_field_t;

// Registers the field (kept by pointer: static storage) and, the first time, the "cfg" console command. A key
// already there is replaced; a bad key, a reserved one or a full table (CFGCON_MAX) logs an error and returns false.
#define CFGCON_MAX 24
bool cfgcon_add(const cfgcon_field_t *f);

// The JSON of the answers (without the "test: cfg " prefix): the length, or -1 when it didn't fit in n (out is then
// cut but terminated). cfgcon_set checks and applies one value; NULL = done, else the reason (a static string).
int cfgcon_values_json(char *out, size_t n);
int cfgcon_schema_json(char *out, size_t n);
const char *cfgcon_set(const char *key, const char *value);

#define CFGCON_LINE_MAX 2048        // the longest answer line (a static buffer, in PSRAM when there is some)
