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

Not drawn: forge_presence and forge_map, which use forge_core and forge_net only (no LVGL, no board). forge_lvgl
needs only lvgl and forge_core (since v0.7.0); forge_presence and forge_settings take forge_net and forge_ota when the
app has them ("No Wi-Fi" below).

## Start-up order (main/main.c)

1. `net_init()`: NVS (`nvs_init`), Wi-Fi driver, the "NTP" service, the test console's `wifi` command.
2. `app_text_init()`: the app's text table and the saved language.
3. `diag_start(60)`: periodic `diag:` lines.
4. `board_init()`: display, LVGL task, touch, `forge_lvgl_init()` (touch and screen commands), `fps`, `where`.
5. `web_set_page()`, `web_add_routes()` for the app's routes; `presence_start(hooks)`, `presence_web_routes()` and
   `touch_set_press_filter(presence_touch)` (screen dimming); `ota_start(listener)` (adds `/api/update`).
6. `ui_init()`: screens, `screens_register()`, `web_set_snapshot()`.
7. `testcon_start()`: the console task and its built-in commands.
8. Wi-Fi: saved network → `net_begin()` + `web_start()` + `net_wait()`; none → first setup; unreachable → offline
   setup. Then `diag_mark("app ready")` (the harness's start line).

Routes must be added before `web_start()`; console commands can be registered at any time.

## forge_core (no LVGL, no Wi-Fi)

| Header | What | Console `shot [SCREEN [DIV]]` (screens.c): a screen rendered off-display and sent over USB as base64 RGB565 rows, for a session without HTTPS to the board (`tools/devloop/shot.py`).
|---|---|
| `testcon.h` | Test console on USB Serial/JTAG. `testcon_register(name, usage, fn)`, `testcon_add_where(fn)`, `testcon_start()`. Built in: `ping help heap where memspeed reboot`. Replies `test: …` ([PROTOCOL.md](PROTOCOL.md) §2). The table is in `testcon_registry.c` (host-tested). |
| `diag.h` | `diag_start(period_s)`, `diag_mark(stage)`, `diag_failed_allocs()`, `diag_add_hook(fn)` for an extra line per period. Boot info, last crash from the core dump (then erased; the Xtensa backtrace, or on RISC-V the PC, RA, mcause and mtval), heap with worst largest block, per-task CPU and stack. Kconfig ("espforge core"): `FORGE_DIAG_TASK_STATS` (the per-task lines) and `FORGE_DIAG_PSRAM_LARGEST` (PSRAM's largest block in the heap line) each hold interrupts off for milliseconds on an ESP32-P4 with 32 MB, which its per-frame display interrupt cannot take (L201); `diag: stats cost` at start times both here. |
| `forge_i18n.h` | Language core: `i18n_init(texts, count, langs, nlangs)` with the app's language list (`i18n_en`, `i18n_fr`, or its own `i18n_lang_t`: code, name, weekdays, months, date style EN or FR) and its `LANG_` enum in that order; `i18n_text(id)` (the first language's when missing or empty), `i18n_count`, `i18n_set/lang/code/name/from_code`, optional `i18n_load/save` (NVS `i18n/lang`), `tr_weekday`, `tr_date_long` ("Mercredi 1er octobre"), `tr_date_ymd`. |
| `nvs_util.h` | `nvs_check(err, what)` (logs, returns success), `nvs_init()`. Don't grow a struct saved as a blob: add keys. |
| `version.h` | `parse_ver`, `cmp_ver`: `vX.Y.Z-anything < -rc.N < vX.Y.Z < vX.Y.Z-N-gHASH`; a git-describe build after an rc (`-rc.N-M-gHASH`) between rc.N and rc.N+1 (L216). Host-tested; CI mirrors it (tags only). |
| `forge_image.h` | `forge_image_t`: an RGB565 picture (data, w, h, stride, priv) from `screens_snapshot()` for `/api/snapshot`; forge_net's `web_image_t` is the same type. |
| `cfgcon.h` | Settings over the USB console: the app registers fields (`cfgcon_add`: key, English label, type text/bool/int/select/color/info, a getter, a setter that only applies and saves). Console `cfg` (one JSON line of values, with `app` and `version`), `cfg schema`, `cfg set KEY VALUE` (cfgcon checks range, step, options, length first; `%XX` escapes for texts). The flasher site's "Configure over USB" card builds its form from the schema, so a project without Wi-Fi or a settings page gets one with no page code. The starter registers language, update channel, dimming, brightness and its address. Pure parts host-tested (`test_cfgcon.c`). |
| `http_once.h` | `http_once(cfg, &status)`: init/perform/cleanup; NULL client = `ESP_ERR_NO_MEM`, not a crash. forge_core does not require `esp_http_client` (a no-Wi-Fi build has none): a component that includes this requires it itself. |
| `utf8.h` | `utf8_cut`, `utf8_copy`: never split a character. |
| `png_rows.h` | PNG decode one row at a time with the ROM's inflate (~50 KB whatever the size): 8-bit grey, RGB, palette (+tRNS), grey+alpha, RGBA, and 1/2/4-bit grey and palette (OpenStreetMap's few-colour tiles); not 16-bit, not interlaced. |
| `textfit.h` | Text from outside (network names, places...) without the characters the fonts lack (emoji): `textfit_init(ttf, len)` reads a TTF's cmap, `textfit_add()` a fallback font (a character counts if any font has it); `textfit(in, out, n)`. Pure C, host-tested. |

## forge_net

| Header | What |
|---|---|
| `net.h` | Station with retries forever (1 s → 3 s → 30 s, paused while setup is open), saved credentials (NVS `wifi`), setup access point `CONFIG_FORGE_SETUP_SSID` with a per-device password (NVS `setup/pass`) and captive portal (DNS answers everything), first-time portal, Wi-Fi Easy Connect (DPP: listens on the router's channel, the setup AP held on it so the phone's confirmation is received, LESSONS L174; `net_dpp_supported()` is false with esp_wifi_remote, Wi-Fi through a co-processor, and the starter then shows the setup-network page alone), `net_set_restart()` / `net_restart()`, test hooks (`net_test_offline…`). `net_low_latency(on)`: Wi-Fi power save off while a screen downloads much (counted; ESP-IDF's default waits for the router's beacon, ~100 ms a request, L204). `net_dl_begin(who, timeout_ms)` / `net_dl_end(who)`: one TLS download at a time (each holds 10-15 KB of internal RAM; svc's probes, forge_ota and forge_map take it; `net_dl_busy()`, `net_dl_holder()`; the bookkeeping `dl_gate.c` is host-tested). `net_bg_post(fn, arg, who)`: a background worker, one short-lived task (internal 8 KB, gone after 30 s idle) running small network jobs one at a time inside the slot; svc's probes run there. `wifi status` reports `ps lowlat dl bg`; console `netstat` prints lwIP's counters (L208). |
| `web.h` | HTTPS (per-device cert) + HTTP portal. `web_add_routes()`, `web_set_page()`, `web_set_snapshot()`, `web_set_info()`, `web_start()`, `web_key()`, `web_url()` (for the settings QR code), `web_port()` (`""`, or `":<port>"` when `CONFIG_FORGE_HTTPS_PORT` isn't 443: every address the component writes carries it), `web_send_json/read_json`, `web_from_setup_ap`. Built in: `/`, `/api/info`, `/api/scan`, `POST /api/wifi`, `/api/snapshot`. Guard: Host 421, JSON 415, constant-time `X-Key` 401, HTTPS only on the home network. Console: `key`, `portal windows-quiet`. |
| `tlscert.h` | `tlscert_get()`: EC P-256 self-signed certificate made at first use, kept in NVS `tls`. CN = `CONFIG_FORGE_TLS_NAME` + MAC. |
| `svc.h` | Health of external services: `svc_add(name, api, probe_url_fn)` (in the order to list them), `svc_find(name)` (`SVC_NAME_NTP`, `SVC_NAME_UPDATES`), `svc_http/ok/get`, `svc_fail_why(id, code)` / `svc_fail(id, text)`, reasons as `svc_why_t` codes with an optional `svc_set_why_text(fn)` for the display language, `svc_user_agent()` (256 bytes; the three Kconfig strings are length-checked at compile time), `svc_probe_stale()`. Logs changes only. |

Kconfig ("espforge"): `FORGE_HTTPS_PORT` (443; a board whose Wi-Fi co-processor doesn't hand it 443 sets another, and
the PC tools read `forge.json` `https_port`), `FORGE_SETUP_SSID`, `FORGE_TLS_NAME`, `FORGE_REPO`, `FORGE_PRODUCT`
(User-Agent and certificate O=; empty: the project name), `FORGE_UA_COMMENT`, `FORGE_PORTAL_NAME` (the sign-in page's
"<name> setup" link).

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
| `pager.h` | Full-screen pager: pages side by side (or stacked), `pager_switch` / `pager_go`; its drags are `slide.h`'s. `pager_set_count(pager, n)` shows only the first n of the pages created (the others are hidden, out of reach of swipes; `pager_page()` still gives them): build every page once, show as many as there is data for. `pager_set_order(pager, pages, n)` shows those pages in that order, the others hidden after them (a summary page that stays last after a varying list; keep page objects, not numbers: `pager_index()`). `pager_shown(pager, i)`: page i only if it is shown (slide.c's neighbours). A pager can sit on a page of another, across it (esp32-s3-rtcquebec: the stops in a column, in the middle of a row alerts | stops | map): `pager_on_view(pager)` says whether every pager page above it is the one shown. Host test: `tests/host/test_pager.c`. |
| `slide.h` | Moves drawn as pictures copied to the panel (~66 fps, LVGL's own scrolling ~24): `slide_pager(pager)` takes over its drags (follow the finger, bounce at the ends, flick), `slide_change()` / `slide_to()` slide an in-place change or a screen load. `slide_tap_ok()`: false during a move and 600 ms after it (a quick swipe's next press can reach LVGL as a tap); a click handler on a pager page asks it first. With a pager on a page of another, a drag moves the pager on view along its axis (none that way: LVGL's, a list or a gesture). A shadow of the panel (every flush copied) plus the neighbours of the pagers on view kept ready (rendered when idle, refreshed every 2 s; `slide_stale()` when the app changed what they show): two fixed pictures (shadow, other) and up to four neighbours whose 434 KB buffers are allocated when a picture needs one and PSRAM has room (`SLIDE_PSRAM_RESERVE_KB` stays for the app): the starter's two pages take 1.7 MB in all. Which slot a picture takes follows MeteoBus's rules (`slide_slots.c`, host-tested: empty first, then an unwanted page, a wanted one only when worth less, a drag forces); `slide_release_unneeded()` frees the pictures of pages not wanted now, for a memory-hungry screen on top. `slide_held_still(px)`: the finger held is where it came down (a long-press handler asks it: LVGL fires LONG_PRESSED however far the finger moved when no scroll took over, L200; `SLIDE_HOLD_PX` 24). A vertical drag on a scrollable list with no pager that way (Settings) is drawn as pictures too (weather_amoled's `slide_scroll`): the rows moved within the shadow, the rows coming in rendered as a strip, only the list's rectangle sent through the panel's `raw_area` hook (LVGL's own scrolling: ~25 fps); `slide_screen_busy()` for long flash writes to wait on. Log: `slide: drag: first frame after N ms, ... gap max, held reads, finger still max, renders`. Needs the board's panel hooks (`forge_lvgl_set_panel`). `slide_set_drag_hook(hook, user)`: called once per pager drag, from the touch read at the moment it is decided (LVGL sends the pressed object no PRESS_LOST then: an app that started something on PRESSED ends it there; flags and logs only). `slide_drop_touch()`: a long-press handler takes the touch now down: no pager drag, no list scroll, and LVGL waits for the release too (no scroll of its own, no CLICKED on the lift; PRESS_LOST then). The drag threshold is Kconfig `FORGE_SLIDE_DRAG_PX` (menu "espforge LVGL", 6..60, default 10; esp32-s3-colormesh 28); lists drawn as pictures still start after 10 px, LVGL's own scroll limit is 10 px past whichever applies. Console `slide` (thresholds; drags, scrolls, hook calls and dropped touches since boot) and `slide hold X Y MS X2 Y2 [MOVE_MS [END_MS]]` (a held finger that then moves). |

## forge_presence (screen dimming)

`presence.h`: the screen dims, then turns off, when the room stays quiet; sustained noise, a movement or a touch
wakes it (weather_amoled's presence.c, through esp32-s3-rtcquebec v0.3.0). No board code: `presence_start(hooks)`
takes the microphones (`mic_open`, `mic_read`: 100 ms windows of 16 kHz samples), the motion sensor, the touch's
`touch_idle_ms`, `set_brightness` (with the display lock) and an optional `screen_off(bool)` (a real off once the fade
down reached 0, the panel awake again before the fade up: the starter gives it the board's `display_sleep`) as
`presence_hooks_t`; a NULL hook = that part missing (no microphones: always on; no `screen_off`: brightness 0 is "off"). `presence_web_routes()` (built only when forge_net is in the build) adds `GET/POST /api/presence` and `POST /api/calibrate`
(PROTOCOL.md §4); console `presence [calibrate N]`, `wake`. Give `presence_touch` to the board as its press filter:
the touch that wakes a dark screen does nothing else.

- Settings in NVS namespace `presence`, one typed key each (`enabled margin wake dim off bright dim_pct baseline
  motion motion_mg`, `enabled` written last): weather_amoled's `cfg` blob was dropped as unreadable whenever the struct
  changed size. That blob is imported once when no typed keys exist (`presence_cfg_from_blob_v1`) and left in place
  for a rollback.
- `presence_preview_brightness(pct)`: a slider being dragged (shown at once, not saved); hook `settings_changed`: the
  page changed the settings (an app's settings screen redraws).
- Calibration (a few seconds of quiet): the baseline is the **median** of the levels; a spread over 12 dB (90th - 10th
  percentile: someone spoke) is refused and the old baseline kept (`"cal":"noisy"`). weather_amoled took the 90th
  percentile, and speech set it 30 dB too high: the screen would never have dimmed.
- Pure half (`presence_sm.c`: the state machine, the limits, the calibration statistic; `presence_json.c`): host
  test `tests/host/test_presence.c`. The settings page's "Screen" section: `tools/webtest/tests/screen.spec.js`.
  On the board: harness suite `presence` (`dim_off_wake`: dim, off with the panel asleep and the "screen" lock released
  (console `power`), a long-press on the dark screen only wakes it).
  In the browser: the real `presence.c` runs, the board's microphones and motion sensor answering "not there"
  (`web/emu/forge/emu_stubs.c`): the screen stays on, the settings page shows "No microphone"; the emulator's smoke
  test checks that `/api/presence` answers.

## forge_settings (the Settings screen)

`forge_settings.h`: the screen a long press opens (weather_amoled's, shared since espforge v0.5.0). Done at the top, `settings_set_slider(fn)`: how the screen and its phone page slide in and out (forge_lvgl's `slide_to` by default; an app with its own slide.c gives its own).
rows in sections that scroll, a brightness arc along the bottom (the finger's x on the band under it: previewed live,
saved on release). It slides up from below; Done or a swipe right slides back to the screen it came from. Refreshed
every second while shown, so a change made on the phone's page shows up.

- forge_net and forge_ota are optional: linked only when the app has them (`idf_component_optional_requires`; the
  sources see `FORGE_HAVE_NET` / `FORGE_HAVE_OTA`). Without forge_net: no phone page (no `LV_USE_QRCODE` needed), no
  `settings_std_phone` / `_network` / `_ip` (a link error if listed); without forge_ota: no `settings_std_updates`, and
  Restart restarts at once.
- Rows (`settings_row_t`: kind, label, callbacks): a section title, a switch (the whole row toggles it), a value (a tap
  cycles it), an action (a tap does it), an info line. Ready-made ones, `settings_std_*`: dimming, wake on pick-up
  (only with a motion sensor), timing presets (forge_presence); language (`i18n_save`, then the app's `changed`); the
  phone's settings page (a QR code of `web_url()`); Wi-Fi (the app's setup screen, which calls `settings_resume()` when
  it closes); updates (check, then "Install vX" on a second tap); restart (a second tap); About (version, network,
  IP, memory, uptime). The app adds rows of its own.
- Texts are the app's: each row's label is an app text id; the ready-made rows' words come from
  `settings_opts_t.texts`, the app's id for each `settings_text_t` code (a table in `settings_text_t`'s order, checked
  by a `_Static_assert` on its length: see the starter's `ui.c`).
- Snapshots: register `settings` (shown), and `settings1` / `settings2` (the list scrolled down, `settings_scroll(n)`;
  in `screens_not_shown`). Harness: the starter's `long_press_opens_settings` and `settings_row_acts`. In the browser:
  `USE_SETTINGS := 1` in `web/emu/Makefile`.

## forge_map (street map pictures)

`forge_map.h`: a w x h RGB565 picture of the map around a place at a zoom, built from 256 px PNG tiles `fmap_set_path(pts, n, c565, width, opa)`: a path (a route) drawn after the tiles into the picture itself, the pixels under it kept so it can be changed or removed (no second canvas; `map_path.c`, host-tested). `fmap_set_home(lat, lon)` and `fmap_opts_t.flash_cache`: the home place's pictures kept in a `mapcache` partition (one 512 KB slot per zoom, header last, sectors one at a time with `opts.busy` pausing them; `map_cache.c` over an I/O table, host-tested with a RAM-backed one; the starter's `partitions.csv` has the partition at 0x620000, 8.75 MB, as weather_amoled's and MeteoBus's).
(OpenStreetMap's by default), desaturated and dimmed for the AMOLED (esp32-s3-rtcquebec's map.c and weather_amoled's
radar.c, which each had a copy). No LVGL: the app shows `px` as an image. Options in `fmap_opts_t`
(`fmap_opts_default(&o, w, h)`: OSM, zoom 0..19, 1 retry, half-way to grey then 55 %, 4 slots): the tile URL template
(`{z}` `{x}` `{y}` replaced, everything else copied as it is, `%` included, L195), the User-Agent (NULL:
`svc_user_agent()`), the attribution the app shows (`fmap_attribution()`), the service it reports to (`svc.h`), the
zoom range, retries, dimming, the empty colour, the PSRAM pictures kept, an `updated(user)` callback.

- `fmap_create(&o)` starts the map task (7 KB internal stack, its statics in PSRAM: L185). `fmap_set_center(lat, lon,
  zoom, &view)` never waits for the network: it returns a kept picture (same zoom and origin: nothing downloaded) or a
  new one the task fills tile by tile (`updated` after each, then `view.state` READY or FAILED; a failed one is tried
  again when asked for again). The pictures are kept least recently used first, never the two returned last (an app
  shows the previous zoom until the new one is complete) nor the one the task is still drawing into; a `px` stays
  valid until its slot is reused. `fmap_status()` is the last picture now. Log: `fmap: zoom Z at X,Y: ok/total tiles`
  (`kept`, `cancelled at ...`).
- `fmap_render(&o, z, ox, oy, dst, w, h, cancel, user, &ok)`: one picture now, in the caller's task and buffer, with its
  own connection (weather_amoled's alert map, later its radar's base map); `cancel` is asked before each request.
- Downloads: one keep-alive `esp_http_client` per picture, dropped after a transport error; a 404 isn't retried, a
  5xx, 429 or transport error is (1 s, then 2 s... later); the body grows in PSRAM from 64 KB to a 256 KB cap (past it: refused) and is freed
  after each picture; tiles decode a row at a time (`png_rows`, every bit depth OSM sends, L190), pausing every 64 rows.
- `forge_geo.h`: Web Mercator maths (`geo_world_px`, `geo_origin`, `geo_to_view`, `geo_cover`, `geo_wrap_x`,
  `geo_distance_m`, `geo_clamp_circle`). x wraps (tile x mod 2^z: a picture across the 180th meridian shows the other
  side); north and south of the world (85.05 degrees) there are no tiles: not requested, the empty colour stays.
- Phase 2 (not yet): a flash cache of whole pictures with weather_amoled's "MAP7" layout (see the header's TODO).
- Host test `tests/host/test_map.c` (geo.c, map_slots.c, map_draw.c against the scripted HTTP client: a real 4-bit OSM
  tile at a negative offset, 404, retries, a body too big, cancel, the world's edge at zoom 4). Not in the browser
  emulator yet (nothing there uses it).

## forge_audio

`forge_audio.h`: audio for a board with one I2S port shared by its codecs, board-free: the board (or an app with its
own board code, weather_amoled) gives `forge_audio_cfg_t` (the codecs' I2C bus, the I2S port and pins, 16 kHz, the
microphones' address and channels) to `forge_audio_init(cfg, speaker)`, then `forge_audio_mic_open(gain_db)` /
`forge_audio_mic_read` (forge_presence's hooks) and `forge_audio_data_if()` for an app's own speaker device. Two
layouts: `FORGE_AUDIO_ES7210_ES8311` (the Waveshare AMOLED: ES7210 microphones in, an ES8311 speaker the app opens and
closes around each sound; the speaker's direction only when asked, ~5 KB of internal DMA RAM each way) and
`FORGE_AUDIO_ES8311_BOTH` (MeteoBus's Guition ESP32-P4 board: one ES8311 is microphone and speaker, one IN_OUT device
kept open under a lock since `es8311_open()` rewrites the chip, L212; the app writes to `forge_audio_speaker_dev()`
and switches the amplifier itself). esp_codec_dev 1.5.11. Proven here on the Waveshare layout (harness `presence`);
the single-ES8311 layout is MeteoBus's working code, to be proven on the P4 board when it joins `boards/`.

## board (boards/ws_amoled175/board)

`board.h`: `BOARD_NAME`, `DISP_W`, `DISP_H`, `BOARD_ROUND`, `board_init()` (also `esp_pm_configure` 80..240 MHz, no
light sleep, with `CONFIG_PM_ENABLE`), `display_lock/unlock` (recursive; records the longest hold and who held it),
`display_brightness()`, `display_sleep(off)` / `display_asleep()` / `display_cpu_full_speed()` (the panel asleep while
the screen is off: DISPOFF + SLPIN from the caller; the wake split, SLPOUT from the caller then the LVGL task's whole
frame and DISPON 120 ms later, L81; a "screen" `ESP_PM_CPU_FREQ_MAX` lock held while the panel is awake, so the CPU
idles at 240 MHz with the screen on and may drop to 80 MHz with it off; console `power cpu_mhz= panel= screen_lock=`,
where only `screen_lock` says anything, L202), `display_get_stats()`, `display_set_flush_hook()`, `display_raw_area()` (a part of the screen without LVGL, bottom up when asked: slide.c's list scrolls),
`board_i2c_bus()`, `touch_idle_ms()`, `touch_set_read_hook()`, `touch_set_press_filter()` (asked when a finger comes
down: true = LVGL and the read hook see no finger until it lifts; "a touch on a dark screen only wakes it"), and for slide.c `display_raw_frame()` (a frame without
LVGL, bands filled while the previous one is sent), `touch_get()` / `touch_fresh()` (the chip read at most every
10 ms) and `touch_forget()`, handed over with `forge_lvgl_set_panel()`. Also `imu.h` (QMI8658 accelerometer) and
`board_audio.h`: thin wrappers over forge_audio with this board's pins and bus (`board_audio_init(speaker)`,
`board_mic_open(gain_db)`, `board_mic_read`, `board_audio_data_if`; the speaker's direction only when asked,
`board_audio_init(true)` before the microphones).

Display (since v0.7.0, from esp32-s3-colormesh): LVGL draws in DIRECT mode into one whole 466x466 RGB565 frame in
PSRAM (424 KB); each finished frame's dirty rows go to the panel in 32-line bands through the two internal DMA buffers,
started on the panel's Tearing Effect pulse (GPIO13, ~59 Hz), or behind / ahead of the scan when the write cannot
catch it ("chase"): each refresh shows one whole frame, no tearing. `display_raw_frame()` (moves) and top-down
`display_raw_area()` start on TE too (areas always chase); an area sent bottom up (a list scrolled down) goes at once
(L219), and areas are copied back into the frame, which always holds what the panel shows (L218). While the panel
sleeps pushes don't wait for TE; after 3 pushes without a pulse they stop waiting until it comes back; the wake sends
the frame without a re-render. The flush hook gets whole-width rows of the frame. Boot line `display: TE <n> Hz on
GPIO13 (period .. ms, high .. ms)`; console `te [lvgl|raw|chase on|off] [log N]`; `fps` adds `pushes= push_avg_ms=
push_max_ms= te_wait_avg_ms= te_wait_max_ms= te_lag_max_ms= te_timeouts= te_chased=`; a second `diag: display:` line
and a 60 s `display: frames ...` summary. Moves are bound by the panel: 55 fps measured (59 at most).

Rules kept in the code (see [LESSONS.md](LESSONS.md), Display and Touch): the SPI interrupt runs on the LVGL core;
no esp_lcd call from outside LVGL while its last band is in flight (`display_brightness` waits); every touch report
is acknowledged; 5 read errors in a row count as a release.

A new board: copy the directory, keep the `board.h` API, change pins, panel init and touch driver, and point
`EXTRA_COMPONENT_DIRS` in the root `CMakeLists.txt` at it ([NEW-PROJECT.md](NEW-PROJECT.md)).

## No Wi-Fi (examples/nonet)

An app with no Wi-Fi (Bluetooth LE only, like esp32-s3-colormesh) requires `board forge_core forge_lvgl forge_presence
forge_settings` and not forge_net, forge_ota or dns_server (its `main/idf_component.yml` lists none of them). Start-up:
`nvs_init()` (net_init does it in the Wi-Fi starter), `app_text_init()`, `diag_start(60)`, `board_init()`,
`presence_start(&hooks)` and `touch_set_press_filter(presence_touch)`, `ui_init()` (Settings without the phone, Wi-Fi,
updates, network and IP rows), `testcon_start()`, `diag_mark("app ready")`. No `web_*`, `presence_web_routes` or
`ota_start`: there is no `ota: Running` line, so the harness reads diag's lines (`--no-net`, forge.json `no_net`).
Nothing confirms a new image: leave out `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`, or confirm it in the app.
`examples/nonet` is the template (921 KB, 68 components); `examples/nonet/check_nonet.py` fails when forge_net,
forge_ota, dns_server, esp_wifi or esp_https_server is in its build (CI job `nonet`). Settings over USB: `cfgcon.h`.

## The starter app (main/)

| File | What |
|---|---|
| `main.c` | Start-up order, BOOT button (forget Wi-Fi), first-time and offline setup (15 min window), `POST /api/settings`, screen dimming's hooks (board microphones, motion sensor, touch, brightness), the work loop placeholder. |
| `ui.c` | Screens `hello`, `system` (pager), `settings` (forge_settings, a long press), `setup` (setup network / Easy Connect: offline, a long press; online, Settings' Wi-Fi row), `message`; registry for the console and snapshots. |
| `app_text.h/.c`, `i18n_strings.h` | The texts, English and Québec French. |
| `web/index.html` | Settings page: Wi-Fi, language, updates; `I18N` + `t()`. Tested by `tools/webtest`. |

## The display in the browser (web/emu)

Not a firmware component: the app's `main/` sources and the components above (forge_lvgl, forge_core's i18n and
textfit, forge_net's svc.c, forge_ota's ota_web.c) built to WebAssembly with Emscripten, the board and ESP-IDF
replaced by the framework's stand-ins in `web/emu/forge/` (display, touch, HTTP client, NVS in localStorage, tasks
as fibers, local time, the web server for the settings page, stubs for Wi-Fi, updates, diagnostics and the test
console). `main.c`'s `app_main()` runs unchanged, its Wi-Fi already joined. The app supplies `web/emu/Makefile`
(its sources, its embedded font), `emu_main.c` (`main()`: demo data, then `emu_start_app_main(); emu_loop();`) and
`index.html` (the page's words); `ui.c` has one `#ifdef EMU_BUILD` for its font. Audio, motion sensor or flash
partitions an app uses are its own stand-ins in `web/emu`. Design, build and what an app adds:
[web/emu/README.md](../web/emu/README.md). Published by CI on the flasher site as `try/`.

`USE_NET := 0` in an app's `web/emu/Makefile` builds it without forge_net and forge_ota (no `svc.c`, `ota_web.c`,
`presence_web.c`, `emu_net.c`, `emu_web.c`, no settings page beside the screen; smoke.js skips the page's checks);
default 1. `examples/nonet/web/emu` is built that way.

Keep components buildable there: a call to the radio, the flash or a driver belongs behind the component's own
functions (stubbed in `forge/emu_stubs.c`), not in code the screens run.
