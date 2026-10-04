#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

// Health of the external services the firmware depends on (a status screen, /api/info, the log). Every fetch reports
// its outcome here; the log shows changes only ("X: OK again", "X: timeout"), not every retry. Times are esp_timer
// microseconds (0 = never). forge_net adds "NTP", forge_ota adds "Updates"; the app adds its own at start-up.

typedef struct {
    const char *name;       // "Open-Meteo"
    const char *api;        // "Forecast API v1"
    int64_t last_try, last_ok;
    bool ok;                // outcome of the last try
    bool probing;           // a check is queued or running (svc_probe_stale)
    int fails;              // failures in a row
    int ms;                 // duration of the last try
    char why[40];           // reason of the last failure (English: diagnostics, not user text)
} svc_info_t;

// probe: writes a small URL to check this service with (NULL: never probed, e.g. SNTP). Returns the id, -1 if full.
typedef void (*svc_probe_url_t)(char *url, size_t n);
int svc_add(const char *name, const char *api, svc_probe_url_t probe);
int svc_count(void);

// t0 = esp_timer_get_time() taken before the request. Success = ESP_OK and HTTP 200.
void svc_http(int id, esp_err_t err, int status, int64_t t0);
void svc_ok(int id, int64_t t0);
void svc_fail(int id, const char *why, int64_t t0);
void svc_get(int id, svc_info_t *out);
// The User-Agent of the firmware's own requests (public APIs ask apps to identify themselves):
// "<project>/<version> (+https://github.com/<CONFIG_FORGE_REPO>)"
const char *svc_user_agent(void);
// A status page opened: in a short-lived task, send a small request to each service not contacted for 5 min
void svc_probe_stale(void);
#define SVC_MAX 12
