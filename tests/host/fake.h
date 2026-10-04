#pragma once
#include <stdbool.h>
#include "esp_err.h"
// The reply every request gets (fake.c): body, HTTP status, or a transport error; no_client = init returns NULL
typedef struct { const char *body; int status; esp_err_t err; bool no_client; int requests; } fake_http_t;
extern fake_http_t fake_http;
