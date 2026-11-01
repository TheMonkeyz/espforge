#pragma once
#include <stdint.h>

// An RGB565 picture in memory, handed from the one who draws it to the one who sends or saves it: forge_lvgl's
// screens_snapshot() fills one, forge_net's GET /api/snapshot sends it (web_set_snapshot). Here in forge_core so that
// forge_lvgl needs no forge_net: an app with no Wi-Fi (Bluetooth only) builds without forge_net, forge_ota and
// dns_server. stride: bytes per row; priv: the owner's handle, for its free function.
typedef struct { const uint8_t *data; int w, h, stride; void *priv; } forge_image_t;

// The name it had in forge_net's web.h (apps' snapshot functions use it): the same type
typedef forge_image_t web_image_t;
