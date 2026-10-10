# Backlog: changes from the apps that could go into espforge

Every project built on espforge (or that espforge came from) adds a line here when it changes something that could
help the others: framework code, board support, tools, tests, docs, a lesson learned. An alignment session works
through the list: port it (a PR here, then the apps take the new tag), or decide not to, and says so in the entry.
The rule is in each project's CLAUDE.md (esp32-s3-rtcquebec, weather_amoled, esp32-s3-meteobus, espforge).

How to write an entry: `- [ ] YYYY-MM-DD, <project> <version or commit>: what, where (files), why it matters.` Add
"PR #n" once a pull request exists here; tick it `[x]` with the espforge version that has it, or strike it with the
reason it stays in the app.

## Framework code

- [ ] 2026-10-09, esp32-s3-meteobus v0.2.0-rc (dbd61f2): slide.c's `get()` with `force` (a slide that needs a picture
  now) allocated a new 434 KB buffer even when `room_for(1)` said no: PSRAM's low point fell from ~450 to 268 KB the
  first time a screen not kept as a neighbour (the radar, opened on top since MeteoBus) slid in. Now it takes the
  buffer of the picture worth least (not a spare). forge_lvgl's slide.c has the same `!room_for(1) && !force` test:
  port the fix (`main/slide.c`, `get()`).
- [ ] 2026-10-09, esp32-s3-meteobus v0.2.0-rc: slide.c's idle work never took a slot holding a needed picture, even
  one worth less than the picture it had to render, nor reused a less useful picture's buffer when PSRAM had no room
  for another: with five neighbours to keep (MeteoBus's weather screen: itself, the next place, extras, the stop page,
  the hourly view) the next place's picture, evicted during a visit to the extras page, never came back, and place
  drags waited ~0.12 s (harness drag_start_ms.drag_place 14 -> 121 ms). `slot_for(..., want_p)` and the idle path's
  buffer reuse in `main/slide.c`; forge_lvgl's slide.c has the same rule.
- [ ] 2026-10-09, esp32-s3-meteobus v0.2.0-rc: `slide_cache_release_unneeded()`: frees the pictures not in the keep
  list, for a screen that needs PSRAM for a while (MeteoBus's bus map: its 434 KB picture and its tiles exist only
  while it is open). A small addition to forge_lvgl's slide.c for any app with a memory-hungry screen on top.
- [ ] 2026-10-09, esp32-s3-meteobus v0.2.0-rc: `netq.c`: who is downloading now (flags set around the weather loop's,
  the radar's and the bus map's downloads), so a lower-priority poller (departures.c) waits before each request: each
  TLS download holds 10-15 KB of internal RAM, and two at once took a place switch's low point from ~40 to 29 KB.
  A forge_net-level "download slot" would let every app (and svc probes, OTA checks) take turns. MeteoBus went
  further (rc.13): no task for the poller at all, its requests run in the radar task's idle time
  (`radar_set_side_work`), after a PSRAM-stack TLS task stopped for good once and an internal one cost 6 KB. A
  framework "background download worker" with a queue would serve every app's pollers the same way.

- [ ] 2026-10-09, esp32-s3-meteobus v0.1.0-rc.0 (3bbe34a): forge_net `svc_user_agent()` builds the User-Agent in a
  128-byte buffer, and `-Werror=format-truncation` fails the build once `CONFIG_FORGE_PRODUCT`, `CONFIG_FORGE_REPO` and
  `CONFIG_FORGE_UA_COMMENT` are a little longer ("esp32-s3-meteobus", "open-source weather and bus display").
  MeteoBus shortened its comment to "open-source weather/bus". Make the buffer larger (256) or check the lengths
  with a `_Static_assert` with a clear message, in `components/forge_net/svc.c`.

- [x] v0.3.0-rc.1, 2026-10-06, esp32-s3-rtcquebec v0.1.0: forge_lvgl `pager_set_count()`: build every page once, show as many as
  the app has data for (pages past n hidden, out of the scroll range and of slide.c's drags). PR #8; review found
  slide.c's idle loop still rendered the hidden page after the last one (`pager_shown()` now), host test
  `tests/host/test_pager.c`.
  **→** Merged (PR #8).
- [x] v0.3.0-rc.1, 2026-10-07, esp32-s3-rtcquebec v0.2.0 (f71f9fd): forge_lvgl `pager_set_order()`, on top of
  `pager_set_count()`: the app gives the order of the pages it shows (a summary page that stays last after a
  varying list), the others follow hidden. `components/forge_lvgl/pager.c`. Added to PR #8, with a fix: a page
  listed twice overran the order array (ASan in `test_pager.c`); esp32-s3-rtcquebec's call never repeats one.
  **→** Merged (PR #8).
- [x] v0.3.0-rc.1, 2026-10-07, esp32-s3-rtcquebec v0.2.0: a tap on a page of a slide.c pager right after a drag can be the next
  quick swipe's press (it reached LVGL as a short click: the app opened its map). The app ignores a tap within
  600 ms of a page settling (`stop_tapped` in its ui.c); slide.c / pager could offer that ("last settle" tick, or
  swallow the click) so every app gets it.
  **→** `slide_tap_ok()` in forge_lvgl, PR #12 (no automatic swallowing: a tap can be wanted); esp32-s3-rtcquebec's
  `stop_tapped` takes it when it moves onto espforge's components, its harness `quick_swipes` the proof.
- [x] v0.4.0-rc.1 (phase 1), 2026-10-07, esp32-s3-rtcquebec v0.2.1: a street-map module: OpenStreetMap tiles around a place at zoom z,
  dimmed for the AMOLED, decoded a row at a time (`png_rows`), kept in PSRAM by zoom (`main/map.c`, `main/geo.c`
  with a host test). weather_amoled's radar.c does the same with a flash cache: a shared `forge_map` component
  would serve both (and the next app with a map).
  **→** Later (user, 2026-10-07): a plan first, after forge_presence. PR #20 (phase 1): `components/forge_map`
  (`forge_geo.h` + `forge_map.h`: the map task with PSRAM slots, `fmap_render` for another task, URL templates,
  host test `tests/host/test_map.c`); no flash cache yet (phase 2: weather_amoled's MAP7 layout). Then
  esp32-s3-rtcquebec moves its map.c/geo.c onto it, and weather_amoled's radar.c its tile code. In v0.4.0-rc.1, with
  the emulator's `USE_MAP := 1` (PR #22); esp32-s3-rtcquebec's map on it (its v0.4.0-rc.1). Still to do: phase 2 (the
  flash cache) when weather_amoled's radar.c moves.
- [x] v0.3.0-rc.1, 2026-10-07, esp32-s3-rtcquebec v0.3.0 (5114f19): screen dimming by sound, ported from weather_amoled's
  presence.c: typed NVS keys instead of its `cfg` blob (a blob that changes size drops the settings on an update), the
  state machine as pure C with a host test (`main/presence_sm.c`, `tests/host/test_presence.c`), touch counted as
  activity through `touch_idle_ms()`. A shared presence component would serve both apps; weather_amoled should move
  to typed keys too.
  **→** PR #14: `components/forge_presence` (user's OK for A+B, 2026-10-07), rtcquebec's code as the base, hooks for
  the hardware; harness `presence.dim_off_wake` passed on COM5 (v0.3.0-rc.0 test build).
- [x] v0.3.0-rc.1, 2026-10-07, esp32-s3-rtcquebec v0.3.0: board API `touch_set_press_filter()` (ws_amoled175 `touch.c` /
  `board.h`, and the emulator's touch): a press the app swallows whole before LVGL and slide.c see it (the touch
  that wakes a dark screen must not also open a map or move a page). forge_lvgl owns the read hook, so a hook of
  its own was needed.
  **→** PR #12 (board and emulator).
- [x] v0.3.0-rc.1, 2026-10-07, esp32-s3-rtcquebec v0.3.0: the board's microphones (ES7210 over I2S, pins MCLK 42, BCLK 9, WS 45,
  DIN 10; esp_codec_dev 1.5.11) set up in the app (`main/presence.c`): move into the board component
  (`board_mic_open()`), keeping I2S TX for the ES8311 speaker.
  **→** PR #14: `board_audio.h` (I2S0 both ways, `board_mic_open/read`, `board_audio_data_if` for a speaker).
- [x] weather_amoled v1.15.0-rc.1, 2026-10-07, espforge PR #14 (the other way: espforge → weather_amoled): weather_amoled's presence.c onto
  forge_presence: its `cfg` blob read once into the typed keys (then erased), its speaker (`sound.c`) on
  `board_audio_data_if()`'s twin, its preview brightness and console commands kept in the app, its page for the
  200 + `"ok":false` answers and the `cal` message (en, fr, iu). A plan first (its own board and audio code).
  **→** weather_amoled PR #28 (branch `feat/forge-presence`, for v1.15.0-rc.1): on forge_presence v0.3.0 with hooks
  in its main.c; the blob is imported by the component and kept (not erased: a rollback reads it); preview brightness
  and the console commands are the component's; host test `test_presence_blob.c` pins the old struct's layout.
  Merged; on COM5 the import read the real blob (every value as v1.14.4 showed it) and v1.15.0-rc.0 passed 22/22.
- [ ] 2026-10-08, weather_amoled PR #28: `board_audio.c` copied into the app as `main/audio.c` only to use the app's own
  I2C bus (`touch_i2c_bus()` instead of `board_i2c_bus()`) and to open I2S both ways at start. A board-free audio
  helper (the bus as a parameter, e.g. `forge_audio_init(bus, speaker)`) would let an app with its own board code
  share it.
- [x] v0.5.0-rc.1, 2026-10-09, esp32-s3-rtcquebec v0.4.0 (the user's new navigation, as weather_amoled's places): a
  pager on a page of another, across it (the stops in a column, in the middle of a row alerts | stops | map), each
  axis dragging its own: `pager_on_view()` (pager.c, host test), slide.c picking the pager by the drag's axis, four
  neighbour pictures instead of two, `slide_stale()` for neighbours whose contents the app changed.
  **→** PR #26.
- [x] v0.5.0-rc.1, 2026-10-09, esp32-s3-rtcquebec v0.4.0 (the user: "maybe the whole settings screen should be
  standardized in espforge for current and future projects"): weather_amoled's on-device Settings (`cfg_*` in its
  ui.c) as `components/forge_settings`: the screen (Done, rows in sections, brightness arc), ready-made rows (dimming,
  pick-up, timing, language, phone QR, Wi-Fi, updates, restart, About), the app's own rows, texts by code.
  **→** PR #26; the starter opens it with a long press (harness `long_press_opens_settings`, `settings_row_acts`).
- [ ] 2026-10-09, espforge v0.5.0-rc.1: weather_amoled onto forge_settings (its `cfg_*`, main/ui.c:2347-2672). It has
  its own slide.c, not forge_lvgl's, and forge_settings slides with forge_lvgl's `slide_to()`: give forge_settings a
  hook for the slide (or move weather_amoled to forge_lvgl first). Its rows of its own: units, sound.
- [ ] 2026-10-09, espforge v0.5.0-rc.1: list scrolls drawn as pictures (weather_amoled's `slide_scroll`) in forge_lvgl,
  for forge_settings' list and apps' lists (esp32-s3-rtcquebec's alerts): LVGL scrolls them at ~25 fps.
- [ ] 2026-10-09, esp32-s3-rtcquebec v0.4.0-rc.1: a long press only from a finger that stayed put (its ui.c
  `long_pressed`: within 24 px of where it came down). LVGL fires LONG_PRESSED for a press held 400 ms however far it
  moved: a swipe read late on a slow page opened Settings (L200). A forge_lvgl helper (`touch_held_still()`), used by
  the starter and forge_settings' openers; weather_amoled's `open_cfg` has the same exposure.
- [ ] 2026-10-09, esp32-s3-rtcquebec v0.4.0-rc.1: its map composes picture + route path in a canvas (`map_compose`,
  434 KB PSRAM) because `lv_line`s over the image cost 170-200 ms a frame (L200). forge_map could draw polylines into
  its own picture (an overlay list per view): no second 434 KB buffer, and weather_amoled's radar could use it too.

## Tools and tests

- [ ] 2026-10-09, esp32-s3-meteobus v0.1.0-rc.0: this was the third new repo (after esp32-s3-weather and
  esp32-s3-rtcquebec) set up by hand: the "protect main" and "release tags" rulesets, Pages built by a workflow, the
  `github-pages` environment's branch and tag policy (`main`, `v*`) and Actions' default read permissions. A
  `tools/setup_github.py` (or a step in the new-project skill) that copies these from a reference repo with `gh api`,
  then checks they match, would make this repeatable.

- [x] v0.3.0-rc.1, 2026-10-06, esp32-s3-rtcquebec v0.1.0: the flash helper started by Claude in the background, windowless;
  `tools\esptool.exe` to copy into a new project (git-ignored). PR #7.
  **→** Merged (PR #7).
- [x] v0.3.0-rc.1, 2026-10-06, esp32-s3-rtcquebec v0.1.0: a new project's CI fails `test_snapshot_metrics_follow_forge_screens`
  until the first harness run writes a baseline (NEW-PROJECT resets it to `{}`): say so in docs/NEW-PROJECT.md, or
  let the test skip an empty baseline.
  **→** PR #13: the test skips an empty baseline (a unit test for it), NEW-PROJECT says so.
- [x] v0.3.0-rc.1, 2026-10-07, esp32-s3-rtcquebec v0.1.0: on a fresh clone the harness's Playwright step fails every test in ~1 ms
  ("Executable doesn't exist") while `npm test` by hand passes: the Microsoft Store Python hides AppData\Local from
  its children (L146); copy `%LOCALAPPDATA%\ms-playwright` to `tools/webtest/.browsers`. Add to NEW-PROJECT.md.
  **→** PR #13: docs/NEW-PROJECT.md §4.
- [x] v0.3.0-rc.1, 2026-10-07, esp32-s3-rtcquebec v0.2.0: the harness's navigation test hard-coded the starter's two pages; the
  app's version reads the pages from the device (`/api/favs`) and swipes through all of them (`pages()` in
  `tools/harness/app_suites.py`): a pattern for apps whose page count varies.
  **→** PR #13: the starter's `pages()` in `tools/harness/app_suites.py`, the navigation test walks it; TESTING §5.
- [x] v0.3.0-rc.1, 2026-10-07, esp32-s3-rtcquebec v0.3.0: the webtest mock's `/api/presence` and `/__presence` hook and
  `tools/webtest/tests/screen.spec.js`: a template for a settings section with live state.
  **→** PR #14 (mock, `screen.spec.js`, with a case for each calibration result).
- [x] v0.3.0-rc.1, 2026-10-07, esp32-s3-rtcquebec v0.2.1 (ca8d132): `git add -A` committed an agent's worktree
  (`.claude/worktrees/agent-...`) as an embedded repository: the template's `.gitignore` should list
  `.claude/worktrees/`.
  **→** PR #13: `.gitignore`.
- [x] v0.3.0-rc.1, 2026-10-07, esp32-s3-rtcquebec v0.3.0: the settings page's `.row { align-items: flex-end }`, so a French label
  that wraps to two lines doesn't push its field out of line.
  **→** PR #14 (the Screen section brings `.row`).
- [ ] 2026-10-07, espforge PR #6 (the other way: espforge → weather_amoled): the PowerShell helpers restart through
  the test console with the port open (`monitor.ps1 -Reboot`, exit 3 = esptool instead; the COM port found by VID
  303A in `flash_helper.ps1`), so a restart's boot log is whole from `ESP-ROM:` (L191). weather_amoled's
  `monitor.ps1` / `flash_helper.ps1` (repository root) still use esptool's reset and lose the first ~2.5 s (L154);
  its Python `tools/flash_helper.py` already does it (22f76f1). Twins: port, then prove on COM5 (`flash QIO`).
  **→** For weather_amoled (twins): ported in its espforge-bump release.
- [ ] 2026-10-07, esp32-s3-rtcquebec (alignment): an app made from the template can take espforge at a tag
  instead of copies (as weather_amoled and now esp32-s3-rtcquebec): `tools/fetch_forge.py`, the Makefiles'
  `FORGE ?=` (managed_components, else `.espforge`), emu.mk's `FORGE_EMU`/`COMP`/`BOARD` pointed at the clone. Say
  so in docs/NEW-PROJECT.md (a section "Later: the framework at a tag") and ship `tools/fetch_forge.py` here.
  **→** esp32-s3-rtcquebec moved (its PR #1, v0.3.1-rc.1: 20/20 on COM5); espforge's side: `gen_config.py` reads
  espforge's own Kconfig (v0.3.0-rc.1), `smoke.js` finds Playwright from the current directory (PR #16). Still to
  do here: NEW-PROJECT's section and `tools/fetch_forge.py` in the template.
- [ ] 2026-10-07, espforge v0.3.0-rc.1 (harness on COM5): `perf.page_swipes` runs right after
  `navigation.setup_pages_slide`, whose Easy Connect page leaves the home network: the first swipe can land while
  Wi-Fi rejoins (46 fps and a 146 ms gap once, a console reply lost once; clean on re-runs). Wait for `net:
  Connected` (or `wifi status`) before the perf suite measures.
- [x] v0.3.0-rc.2, 2026-10-07, espforge v0.3.0-rc.1: internal RAM since forge_presence: free 125 -> 111 KB, largest block 47-55 ->
  36-41 KB (floor 40), most likely I2S0's DMA buffers both ways (4 x 320 frames each, internal) and the presence
  task's stack. To measure (a build without presence_start), then: open TX only when an app plays sound, or fewer
  DMA frames for the microphones. Before the stable v0.3.0.
  **→** Measured (no presence 128/58 KB, without microphones 123/52, rc.1 112/41) and fixed in v0.3.0-rc.2: the
  speaker's direction only on request (117/46). The rest (~6 KB) is the microphones' own DMA and the codec.
- [ ] 2026-10-08, espforge v0.4.0 (harness on COM5): `memory.heap_numbers` in an `--ota` run measures right after the
  update installed (the image confirmed, the update's TLS connection maybe still held): internal_largest_kb 35 there,
  46 three times on the same firmware at rest and after a restart (36-46 across tonight's runs). Measure memory after
  the OTA suite's settle, or let `--ota` runs wait for the update's connection to close before the memory suite.
- [x] v0.5.0-rc.1, 2026-10-09, esp32-s3-rtcquebec v0.4.0: the flash helpers (`flash_helper.ps1`, `.py`) checked the
  staged parts against forge.json's build_dir and the harness looked for the ELF there: a build staged from another
  folder (`stage.py --build build/forge`) was refused, and its panic not decoded. Both follow
  `stage/manifest.json`'s `build_dir` now; test `test_another_build_folder_staged_is_flashed` (LESSONS L198).
  **→** PR #26.
- [ ] 2026-10-09, esp32-s3-rtcquebec v0.4.0: its `tools/forge_local.py` (weather_amoled's, with every espforge
  component of the manifest and the board, `COMPONENTS` a name -> folder map): ship it in the template with the
  "framework at a tag" section of NEW-PROJECT.md (the open entry above).
- [ ] 2026-10-09, esp32-s3-rtcquebec v0.4.0-rc.1: building from the Claude desktop app's shell needs
  `IDF_COMPONENT_CACHE_PATH` outside AppData for the component manager's git fetches (L199): say so in WORKFLOW.md
  and the build skill (or set it in the devloop / CI helper scripts that build).

## Emulator (web/emu)

- [x] v0.3.0-rc.1, 2026-10-07, esp32-s3-rtcquebec v0.1.1: the browser emulator. PR #9 (generic part). The app's port adds, worth
  having in the generic part: `app_main` run as a task with Wi-Fi stubbed "up" (main.c unchanged, no routes moved);
  real binary semaphores in `emu_tasks.c` (a task waiting for another's answer never got it); `emu_time.c` applying
  the firmware's TZ (Emscripten's `localtime_r` uses the browser's zone); `make_flasher_site.py --emu` copying
  `settings.html` and `emu-settings.js`; miniz's tinfl for `png_rows`.
  **→** Merged (PR #9, which already has app_main as a task, binary semaphores, TZ, `--emu` copying the settings
  files). miniz for `png_rows`: PR #12, with a fix to `emu.mk` (an app's `FORGE_CORE +=` replaced the list).
- [x] v0.3.0-rc.1, 2026-10-07, esp32-s3-rtcquebec v0.2.0: Emscripten's `strftime("%z")` ignores TZ too (it wrote local time as
  +00:00): the app computes the offset from the date (`rtc_iso_local`). A lesson for web/emu/README.md.
  **→** PR #12 (web/emu/README.md), PR #13 (LESSONS L196).
- [ ] 2026-10-08, weather_amoled PR #28: its emulator builds forge_presence's real `presence.c` (not a stand-in like
  `web/emu/forge/emu_presence.c`) with the app's hooks: it needed only a `testcon_register` / `testcon_add_where` stub
  and NVS `i16` in the emulator's NVS. espforge's emulator could do the same and drop `emu_presence.c`.
- [ ] 2026-10-09, esp32-s3-rtcquebec v0.4.0-rc.1: the CI emulator job builds the latest stable release (the site's
  "Try it"), so a pull request that changes the emulator or the UI is never built for the browser in CI (rtcquebec's
  PR #8 passed "emulator" on v0.3.3 while its Makefile lacked USE_SETTINGS). Build the branch too on pull requests
  (and keep publishing the stable one).

## Lessons (docs/LESSONS.md)

- [ ] 2026-10-10, esp32-s3-meteobus (PR #13): a settings-page list whose rows act on the list they were drawn from
  (My stops: `rm.onclick = () => saveFavs(favs.filter((_, k) => k !== i))`) removes the wrong item when a tap lands
  between a save the display accepted and the page's redraw (Remove right after Move up: the old first stop went).
  Found as a flaky Playwright test on the Mac runner; reproduced every time by holding the display's answer back 1.5 s
  (`page.route` + `route.fetch`, then a delay, then `route.fulfill`). Fix: disable the rows during a save and show the
  saved list at once. Lesson: a flaky UI test may be a real race; hold the reply back to make it deterministic.
  **esp32-s3-rtcquebec's `main/web/index.html` has the same handlers** (its `saveFavs`): port the fix there.
- [ ] 2026-10-09, esp32-s3-meteobus v0.2.0-rc: LVGL 9.2's `LV_LABEL_LONG_DOT` needs a fixed width **and** a fixed
  height: with the width set by its content (a pill sized to its text) it showed only "…"; with a fixed width but a
  content height it wrapped and ran into the line below ("Terminus Chute-Montmorency"). Size a one-line label from
  `lv_text_get_size()` (width capped, height one line). A lesson for docs/LESSONS.md (LVGL).
- [ ] 2026-10-09, esp32-s3-meteobus v0.2.0-rc: a weather app and a bus app merged into one firmware (docs/MERGE-PLAN.md
  there): the memory plan that held (one picture cache, the two maps taking turns, a poller's stack in PSRAM, one TLS
  download at a time) is worth a page in docs/ for future merges of espforge apps.

- [x] v0.3.0-rc.1, 2026-10-07, esp32-s3-rtcquebec v0.2.1: during `lv_screen_load_anim()` (200 ms) `lv_screen_active()` is still
  the previous screen, and LVGL 9.2 has no public getter for the one loading: a timer that checks "is my screen
  shown?" took the map for closed as it opened. Note when the screen was opened and ignore the first 500 ms.
  **→** PR #13: LESSONS L192.
- [x] v0.3.0-rc.1, 2026-10-07, esp32-s3-rtcquebec v0.2.0: `esp_http_client`'s send buffer is 512 bytes by default: a ~1.3 KB query
  string can't be sent; set `buffer_size_tx`.
  **→** PR #13: LESSONS L193.
- [x] v0.3.0-rc.1, 2026-10-07, esp32-s3-rtcquebec v0.2.1: an LVGL 9 label sized to its content (`LV_SIZE_CONTENT`) in
  `LV_LABEL_LONG_DOT` mode measures nothing and shows only "...": give it a width from `lv_text_get_size()`.
  **→** PR #13: LESSONS L194.
- [x] v0.3.0-rc.1, 2026-10-07, esp32-s3-rtcquebec v0.2.0: building URLs with `snprintf` from pieces that hold `%5B` (encoded
  query strings) reads them as conversions: pass such pieces as `%s` arguments.
  **→** PR #13: LESSONS L195.
- [x] v0.3.0-rc.1, 2026-10-07, esp32-s3-rtcquebec v0.3.0: the sound baseline is the 90th percentile of a 5 s calibration: speech
  during it set it 30 dB too high (-35 instead of -67 dBFS) and the screen would never have dimmed. The settings
  page should say "quiet" plainly, or the calibration use the median, or reject a spread that wide.
  **→** PR #14: the median, a spread over 12 dB refused with the old baseline kept (`"cal":"noisy"`, shown
  on the page); host test.
- [x] v0.5.0-rc.1, 2026-10-09, esp32-s3-rtcquebec v0.4.0: L197 (code a console command reaches runs on the
  console's 4 KB stack: a `screen stop2` overflowed it), L198 (tools follow the staged build).
  **→** PR #26.
- [x] 2026-10-09, esp32-s3-rtcquebec v0.4.0-rc.1: L199 (the desktop app's shell and the component manager's cache), L200
  (a page with slow frames loses drags; long presses from moved fingers).
