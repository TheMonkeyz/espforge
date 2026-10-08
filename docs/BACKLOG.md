# Backlog: changes from the apps that could go into espforge

Every project built on espforge (or that espforge came from) adds a line here when it changes something that could
help the others: framework code, board support, tools, tests, docs, a lesson learned. An alignment session works
through the list: port it (a PR here, then the apps take the new tag), or decide not to, and says so in the entry.
The rule is in each project's CLAUDE.md (esp32-s3-rtcquebec, weather_amoled, espforge).

How to write an entry: `- [ ] YYYY-MM-DD, <project> <version or commit>: what, where (files), why it matters.` Add
"PR #n" once a pull request exists here; tick it `[x]` with the espforge version that has it, or strike it with the
reason it stays in the app.

## Framework code

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
- [ ] 2026-10-07, esp32-s3-rtcquebec v0.2.1: a street-map module: OpenStreetMap tiles around a place at zoom z,
  dimmed for the AMOLED, decoded a row at a time (`png_rows`), kept in PSRAM by zoom (`main/map.c`, `main/geo.c`
  with a host test). weather_amoled's radar.c does the same with a flash cache: a shared `forge_map` component
  would serve both (and the next app with a map).
  **→** Later (user, 2026-10-07): a plan first, after forge_presence.
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
- [ ] 2026-10-07, espforge PR #14 (the other way: espforge → weather_amoled): weather_amoled's presence.c onto
  forge_presence: its `cfg` blob read once into the typed keys (then erased), its speaker (`sound.c`) on
  `board_audio_data_if()`'s twin, its preview brightness and console commands kept in the app, its page for the
  200 + `"ok":false` answers and the `cal` message (en, fr, iu). A plan first (its own board and audio code).

## Tools and tests

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

## Lessons (docs/LESSONS.md)

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
