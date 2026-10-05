# Device ↔ tools protocol

Everything the PC-side tools (`tools/devloop`, `tools/harness`, `tools/snapshot.py`, `tools/webtest`) rely on from
the firmware. Keep this file and the firmware in step: a tool that parses a line the firmware no longer prints fails
silently or, worse, waits out a timeout. Project values (app name, screen size, setup SSID, ready line…) come from
`forge.json`.

## 1. Files between the agent and the flash helper (`.devloop/`)

The agent's shell may have no USB access (Claude desktop / cloud). The flash helper (`tools/devloop/`) runs in a
window on the PC that owns the port and talks through plain files in `.devloop/`:

| File | Direction | Meaning |
|---|---|---|
| `flash.request` | agent → helper | Body: log seconds (default 60). Flash the parts listed in `<build_dir>/flasher_args.json` from the staged copies in `.devloop/stage/`, then log. |
| `reboot.request` | agent → helper | Body: log seconds. Hard reset (no flash), then log. |
| `stop.request` | agent → helper | End the log window now (`stopped_early=1`). Q / Esc in the helper window does the same. |
| `serial.send` | agent → monitor | Each line is written to the port with `\n` and echoed in the log as `> line`. Write `serial.send.tmp`, then rename. |
| `flash.status` | helper → agent | `idle` / `flashing` / `logging` / `flash_failed` |
| `flash.running` | helper | Present while a request is handled. |
| `flash.done` | helper → agent | One line: `exit=0 port=COM5 flash_s=25 total_s=45 errors=0 warnings=0 resets=0 stopped_early=1 started=… finished=…` (errors = `E (` lines, warnings = `W (`, resets = `rst:0x`). |
| `serial_live.txt` | monitor | Grows line by line while logging (tail this). |
| `serial_log.txt` | monitor | The whole window, written when it ends. |
| `flash_log.txt`, `flash_helper.log` | helper | esptool's output; the helper's own history. |
| `harness.ask` | harness → person | A step only a person can do (scan a QR code, tap the screen). |
| `stage/` | agent | Staged parts, each copied under a **unique name** and checked by md5 before `flash.request`. |
| `ip`, `key` | tools | Cached display IP and settings key. **Reset after every flash or install.** |

## 2. Test console (USB Serial/JTAG, `forge_core/testcon`)

One command per line. Every answer is a log line with the tag `test`: `I (…) test: ok …`, `test: <name> k=v …` or
`W (…) test: error …`. Commands are registered by the components and the app (`testcon_register`). An unknown command answers
`test: error unknown command '<name>' (help lists them)`.

| Command | Answer | From |
|---|---|---|
| (boot) | `test: console ready on USB (send 'help')` | forge_core |
| `ping` | `test: pong <version>` | forge_core |
| `help` | `test: commands: a, b, …` | forge_core |
| `heap` | `test: heap internal=K min=K largest=K psram=K psram_min=K uptime_s=S failed_allocs=N lvgl_fallbacks=N` (KB) | forge_core |
| `where` | `test: where <k=v from each breadcrumb provider> task_lvgl=<state>` — takes **no lock**, answers when the rest hangs | forge_core |
| `memspeed` | `test: memspeed …_ms=…` | forge_core |
| `reboot` | `test: ok restarting` | forge_core |
| `key` | `test: key <16 hex>` | forge_net (web) |
| `wifi status\|offline\|online\|offline-boot\|offline-boot-short` | `test: wifi connected=0/1 sta_ssid=… portal=0/1 ap=0/1 ap_clients=N dpp=0/1 retries=N channel=N ap_pass=<8 chars>` (same keys as weather_amoled) | forge_net |
| `portal windows-quiet` | `test: ok portal windows-quiet` | forge_net |
| `fps [reset]` | `test: fps frames=… render_avg_ms=… render_max_ms=… anim_frames=… anim_fps=… gap_max_ms=… gap_max_at_ms=… gap_max_kind=lvgl>move… mpx=…` (the gap from an LVGL redraw to a move's first frame is not counted; `gap_max_kind` is text) | board display |
| `tap X Y`, `press X Y [ms]`, `swipe left\|right\|up\|down`, `drag X1 Y1 X2 Y2 [ms]` | `test: ok tap` … | forge_lvgl (touch inject) |
| `screen` | `test: screen <name>` (a name from `forge.json` `screens`) | forge_lvgl (screen registry) |
| `screen <name>` | `test: ok screen <name>` | forge_lvgl |
| `profile` | `PROFILE-BEGIN <name>` … `PROFILE-END <name>`, then `test: ok profile done` (profiler builds only) | forge_lvgl |

## 3. Log lines the tools read

| Line | Meaning |
|---|---|
| `ota: Running <ver> from <ota_0\|ota_1>, channel <stable\|beta>` | Which image booted. Check it in a fresh log **before** testing. |
| `ota: Update installed, restarting` | OTA download written. |
| `ota: New firmware ran N s…: marked valid (no rollback)` | The new image is confirmed; a restart before this line rolls it back. |
| `net: Connected, IP a.b.c.d` | Station up (`forge.json` `ip_line`). |
| `web: Settings page: https://a.b.c.d/` | Web server up. |
| `diag: mark <stage>` | Boot stages; `diag: mark app ready` = `forge.json` `ready_line`. |
| `slide: drag: first frame after N ms, F frames in T ms (X fps), to next/to prev/back \| gap max N ms, held reads E err U up (longest N ms), finger still max N ms, samples N, renders N, V px/ms` | Each drag drawn as pictures (forge_lvgl). |
| `slide: change: picture N ms, F frames in T ms (X fps)` | An in-place change or screen load slid as pictures. |
| `diag: mark <stage> internal N KB free (largest N), DMA N KB, PSRAM N KB` | A boot stage. |
| `diag: heap: internal N KB free (min ever N, largest block now N / worst N) \| DMA N KB (largest now N / worst N) \| PSRAM N KB free (min ever N, largest N) \| failed allocs N, LVGL in internal RAM N` | Every period (`tools/diag_summary.py`). |
| `diag: tasks: name(cC pP) X.X% NB \| …`, `diag: cpu: core0 N% busy, core1 N% busy (window N s)` | Per task: core, priority, CPU, stack bytes never used. |
| `diag: display: N frames, render avg N ms max N ms, … \| animation N fps … \| LVGL lock wait max N ms, longest hold N ms by <task>` | From the board (diag hook). |
| `App version: <ver>` | ESP-IDF's own boot line. |
| `diag: boot reset=<reason> …`, `diag: coredump …` | Boot info; the last crash, then erased. |
| `rst:0x…` | A restart (the harness fails a test that didn't expect one). |
| `Guru Meditation`, `Backtrace:` | A crash (the harness decodes it with addr2line when the ELF matches). |

## 4. HTTP API (`forge_net/web`)

HTTPS on :443 with a per-device self-signed certificate; HTTP on :80 serves the captive portal on the setup network
(192.168.4.x) and redirects to HTTPS otherwise. **Every POST and `/api/snapshot` carries `X-Key: <key>`**; POST bodies
are JSON (`415` otherwise); a Host header that isn't the display's address gets `421`.

| Route | Answer |
|---|---|
| `GET /` | The settings page (`main/web/index.html`, sent in 1 KB chunks). |
| `GET /api/info` | `{"app","version","ip","ssid","rssi","uptime_s","setup":bool,"lang","languages":[{code,name}]}` |
| `GET /api/scan` | `[{"ssid","rssi","secure"}]` |
| `POST /api/wifi` | `{"ssid","pass"}` → saved, restart when safe. |
| `GET /api/update` | `{"current","latest","channel","state","progress","error","pending_verify","uptime_s","err","rolled_back"?,"notes"?}` — `rolled_back`: the version that was undone (string, shown once); `err`: `ota_err_t` code; `notes`: only when `available` — `state`: `idle checking up_to_date available downloading done failed` |
| `POST /api/update` | `{"action":"check"}`, `{"action":"install"}` or `{"channel":"stable"\|"beta"}` |
| `GET /api/snapshot?screen=<name>` | 24-bit BMP of that screen, rendered off-display. `forge.json` `screens_not_shown`: screens the harness snapshots without showing them (showing changes the device's state: the starter's Easy Connect page leaves the home network). |
| `POST /api/settings` | App settings, starter: `{"lang":"en"\|"fr"}` |

## 5. OTA site (GitHub Pages, `tools/make_flasher_site.py`)

`<ota_site>/channels.json` (`{"stable":{"version","built","manifest"},"beta":{…}|null}`; no beta = stable), `<ota_site>/<channel>/manifest.json` (esp-web-tools;
the updater takes the part at offset 0x10000), `<ota_site>/notes.json` (from `CHANGELOG.md`). A board only installs
a version **above** what it runs and with the same project name (`esp_app_desc.project_name` = `forge.json` `app`).
Version order (`forge_core/version.c`, mirrored in CI): `vX.Y.Z-anything < vX.Y.Z-rc.N < vX.Y.Z < vX.Y.Z-N-gHASH`.
