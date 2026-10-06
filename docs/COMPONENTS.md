# Components

The framework's firmware side. Each component lives in `components/` (the board in `boards/<name>/board/`), has its
public headers in `include/`, and never shows text itself: it returns codes the app translates. Read the header
before using a function; the comments there say why things are done the way they are.

```
            main (the app: screens, texts, settings route, work loop)
              │
   ┌──────────┼───────────────┬───────────────┐
 board     forge_lvgl      forge_ota        (lvgl)
   │          │               │
   └──► forge_lvgl ──► forge_net ◄─────────────┘
              │           │
              └──► forge_core ◄──┘
```

## Start-up order (main/main.c)

1. `net_init()`: NVS (`nvs_init`), Wi-Fi driver, the "NTP" service, the test console's `wifi` command.
2. `app_text_init()`: the app's text table and the saved language.
3. `diag_start(60)`: periodic `diag:` lines.
4. `board_init()`: display, LVGL task, touch, `forge_lvgl_init()` (touch and screen commands), `fps`, `where`.
5. `web_set_page()`, `web_add_routes()` for the app's routes; `ota_start(listener)` (adds `/api/update`).
6. `ui_init()`: screens, `screens_register()`, `web_set_snapshot()`.
7. `testcon_start()`: the console task and its built-in commands.
8. Wi-Fi: saved network → `net_begin()` + `web_start()` + `net_wait()`; none → first setup; unreachable → offline
   setup. Then `diag_mark("app ready")` (the harness's start line).

Routes must be added before `web_start()`; console commands can be registered at any time.

## forge_core (no LVGL, no Wi-Fi)

| Header | What |
|---|---|
| `testcon.h` | Test console on USB Serial/JTAG. `testcon_register(name, usage, fn)`, `testcon_add_where(fn)`, `testcon_start()`. Built in: `ping help heap where memspeed reboot`. Replies `test: …` ([PROTOCOL.md](PROTOCOL.md) §2). The table is in `testcon_registry.c` (host-tested). |
| `diag.h` | `diag_start(period_s)`, `diag_mark(stage)`, `diag_failed_allocs()`, `diag_add_hook(fn)` for an extra line per period. Boot info, last crash from the core dump (then erased), heap with worst largest block, per-task CPU and stack. |
| `forge_i18n.h` | Language core: `i18n_init(texts, count, langs, nlangs)` with the app's language list (`i18n_en`, `i18n_fr`, or its own `i18n_lang_t`: code, name, weekdays, months, date style EN or FR) and its `LANG_` enum in that order; `i18n_text(id)` (the first language's when missing or empty), `i18n_count`, `i18n_set/lang/code/name/from_code`, optional `i18n_load/save` (NVS `i18n/lang`), `tr_weekday`, `tr_date_long` ("Mercredi 1er octobre"), `tr_date_ymd`. |
| `nvs_util.h` | `nvs_check(err, what)` (logs, returns success), `nvs_init()`. Don't grow a struct saved as a blob: add keys. |
| `version.h` | `parse_ver`, `cmp_ver`: `vX.Y.Z-anything < -rc.N < vX.Y.Z < vX.Y.Z-N-gHASH`. Host-tested; CI mirrors it. |
| `http_once.h` | `http_once(cfg, &status)`: init/perform/cleanup; NULL client = `ESP_ERR_NO_MEM`, not a crash. |
| `utf8.h` | `utf8_cut`, `utf8_copy`: never split a character. |
| `png_rows.h` | PNG decode one row at a time with the ROM's inflate (~50 KB whatever the size): 8-bit grey, RGB, palette (+tRNS), grey+alpha, RGBA, and 1/2/4-bit grey and palette (OpenStreetMap's few-colour tiles); not 16-bit, not interlaced. |
| `textfit.h` | Text from outside (network names, places...) without the characters the fonts lack (emoji): `textfit_init(ttf, len)` reads a TTF's cmap, `textfit_add()` a fallback font (a character counts if any font has it); `textfit(in, out, n)`. Pure C, host-tested. |

## forge_net

| Header | What |
|---|---|
| `net.h` | Station with retries forever (1 s → 3 s → 30 s, paused while setup is open), saved credentials (NVS `wifi`), setup access point `CONFIG_FORGE_SETUP_SSID` with a per-device password (NVS `setup/pass`) and captive portal (DNS answers everything), first-time portal, Wi-Fi Easy Connect (DPP: listens on the router's channel, the setup AP held on it so the phone's confirmation is received, LESSONS L174), `net_set_restart()` / `net_restart()`, test hooks (`net_test_offline…`). |
| `web.h` | HTTPS (per-device cert) + HTTP portal. `web_add_routes()`, `web_set_page()`, `web_set_snapshot()`, `web_set_info()`, `web_start()`, `web_key()`, `web_url()` (for the settings QR code), `web_send_json/read_json`, `web_from_setup_ap`. Built in: `/`, `/api/info`, `/api/scan`, `POST /api/wifi`, `/api/snapshot`. Guard: Host 421, JSON 415, constant-time `X-Key` 401, HTTPS only on the home network. Console: `key`, `portal windows-quiet`. |
| `tlscert.h` | `tlscert_get()`: EC P-256 self-signed certificate made at first use, kept in NVS `tls`. CN = `CONFIG_FORGE_TLS_NAME` + MAC. |
| `svc.h` | Health of external services: `svc_add(name, api, probe_url_fn)` (in the order to list them), `svc_find(name)` (`SVC_NAME_NTP`, `SVC_NAME_UPDATES`), `svc_http/ok/get`, `svc_fail_why(id, code)` / `svc_fail(id, text)`, reasons as `svc_why_t` codes with an optional `svc_set_why_text(fn)` for the display language, `svc_user_agent()`, `svc_probe_stale()`. Logs changes only. |

Kconfig ("espforge"): `FORGE_SETUP_SSID`, `FORGE_TLS_NAME`, `FORGE_REPO`, `FORGE_PRODUCT` (User-Agent and certificate
O=; empty: the project name), `FORGE_UA_COMMENT`, `FORGE_PORTAL_NAME` (the sign-in page's "<name> setup" link).

## forge_ota

`ota.h`: `ota_start(listener)`, `ota_check_now()`, `ota_install()`, `ota_set_channel("stable"|"beta")`,
`ota_get_status()` (state, versions, progress, `err` code + `error` text: English, or the app's with
`ota_set_err_text(fn)`; `rolled_back`), `ota_get_notes()`,
`ota_pending_verify()`, `ota_restart_when_safe()`, `ota_state_name()`.
Checks a minute after boot, when Wi-Fi comes back, and every 6 h; installs only what the user asks for, only a
higher version with the same project name. A new image is confirmed after 60 s **with Wi-Fi** (10 min without): a
restart before that rolls it back, and the old image then reports `rolled_back` once.
Kconfig: `FORGE_OTA_SITE` (the Pages URL, ending with `/`).

## forge_lvgl

| Header | What |
|---|---|
| `forge_lvgl.h` | `forge_lvgl_init(lock, unlock)` (called by the board), `ui_lock/ui_unlock`, the simulated finger (`finger_inject`, `finger_injected` for the board's touch driver) and the console's `tap press swipe drag`. `lvgl_mem.c` puts LVGL's heap in PSRAM (small blocks fall back to internal RAM, counted; out of memory = restart). |
| `screens.h` | Named screens: `screens_register(defs, n)` with `get / show / prepare / shown`; console `screen [name]`; `screens_snapshot` for `/api/snapshot`. Names = `forge.json` `screens`. |
| `pager.h` | Full-screen pager: pages side by side (or stacked), `pager_switch` / `pager_go`; its drags are `slide.h`'s. |
| `slide.h` | Moves drawn as pictures copied to the panel (~66 fps, LVGL's own scrolling ~24): `slide_pager(pager)` takes over its drags (follow the finger, bounce at the ends, flick), `slide_change()` / `slide_to()` slide an in-place change or a screen load. A shadow of the panel (every flush copied) plus the current page's neighbours kept ready (rendered when idle, refreshed every 2 s): 4 pictures, 1.7 MB of PSRAM at 466x466. Log: `slide: drag: first frame after N ms, ... gap max, held reads, finger still max, renders`. Needs the board's panel hooks (`forge_lvgl_set_panel`). |

## board (boards/ws_amoled175/board)

`board.h`: `BOARD_NAME`, `DISP_W`, `DISP_H`, `BOARD_ROUND`, `board_init()`, `display_lock/unlock` (recursive; records
the longest hold and who held it), `display_brightness()`, `display_get_stats()`, `display_set_flush_hook()`,
`board_i2c_bus()`, `touch_idle_ms()`, `touch_set_read_hook()`, and for slide.c `display_raw_frame()` (a frame without
LVGL, bands filled while the previous one is sent), `touch_get()` / `touch_fresh()` (the chip read at most every
10 ms) and `touch_forget()`, handed over with `forge_lvgl_set_panel()`. Also `imu.h` (QMI8658 accelerometer).

Rules kept in the code (see [LESSONS.md](LESSONS.md), Display and Touch): the SPI interrupt runs on the LVGL core;
no esp_lcd call from outside LVGL while its last band is in flight (`display_brightness` waits); every touch report
is acknowledged; 5 read errors in a row count as a release.

A new board: copy the directory, keep the `board.h` API, change pins, panel init and touch driver, and point
`EXTRA_COMPONENT_DIRS` in the root `CMakeLists.txt` at it ([NEW-PROJECT.md](NEW-PROJECT.md)).

## The starter app (main/)

| File | What |
|---|---|
| `main.c` | Start-up order, BOOT button (forget Wi-Fi), first-time and offline setup (15 min window), `POST /api/settings`, the work loop placeholder. |
| `ui.c` | Screens `hello`, `system` (pager), `setup` (setup network / Easy Connect), `message`; registry for the console and snapshots. |
| `app_text.h/.c`, `i18n_strings.h` | The texts, English and Québec French. |
| `web/index.html` | Settings page: Wi-Fi, language, updates; `I18N` + `t()`. Tested by `tools/webtest`. |
