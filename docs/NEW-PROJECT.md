# Starting a new project from espforge

The repository is a GitHub template and a buildable starter app. A new project is a copy with its names changed,
its history reset, and its own docs started. Claude can do all of it with the `new-project` skill; the GitHub
settings at the end need the owner.

## 1. Create the repository

On GitHub: **Use this template → Create a new repository** (public if devices should update from GitHub Pages:
Pages on a private repo needs a paid plan). Clone it on the PC, next to the others (e.g.
`C:\Users\<you>\ESPDEV\<name>`).

Pick the names once; they are hard to change later:

| Name | Example | Used for |
|---|---|---|
| app | `kitchen_timer` | CMake project name, image names, the updater's project-name check |
| repo | `TheMonkeyz/kitchen-timer` | CI, release links, the User-Agent of outgoing requests |
| ota_site | `https://themonkeyz.github.io/kitchen-timer/` | where devices look for updates **forever** (L130) |
| setup_ssid | `Timer-Setup` | the setup access point's name |

## 2. Rename checklist

- [ ] **`forge.json`**: `app`, `repo`, `ota_site` (ends with `/`), `setup_ssid`, `screens` (the app's screen names),
      `screen` (size, shape) if the board differs. Leave `idf`, `chip`, `build_dir` unless you mean it.
- [ ] **Root `CMakeLists.txt`**: `project(<app>)`. It must equal `forge.json` `app`: the updater refuses an image with
      another project name (L129).
- [ ] **`sdkconfig.defaults`**: the framework's Kconfig values (menuconfig "espforge"):
      ```
      CONFIG_FORGE_SETUP_SSID="Timer-Setup"
      CONFIG_FORGE_OTA_SITE="https://themonkeyz.github.io/kitchen-timer/"
      CONFIG_FORGE_TLS_NAME="Kitchen Timer"
      CONFIG_FORGE_REPO="TheMonkeyz/kitchen-timer"
      ```
      Then delete `build\v55\sdkconfig` and reconfigure (L12).
- [ ] **`README.md`**: what the device does, for its users first; the web flasher link ("Try it on the board") pointing at
  your `ota_site`, and your board; keep the developer sections.
- [ ] **`CHANGELOG.md`**: keep the `# Changelog` header, replace the sections with `## v0.1.0-rc.1 - <date>`.
- [ ] **`tools/harness/baseline.json`**: reset to `{}`, then run the harness on the first good build with
      `--update-baseline`, review `baseline.proposed.json` and copy it over (docs/TESTING.md §5).
- [ ] **`tools/harness/app_suites.py`**: replace the starter's suites with the app's.
- [ ] **`main/i18n_strings.h`** and **`I18N` in `main/web/index.html`**: the app's texts, every language.
- [ ] **`CLAUDE.md`**: fill "Working setup" for this PC and board; leave "Bugs hit", "User preferences learned" and
      "Useful facts" to grow.
- [ ] **`docs/`**: copy `docs/templates/HISTORY.md` and `ARCHITECTURE.md` to `docs/` and start them;
      `DIAGNOSTICS.md` after the first diagnostics run; `IDEAS.md` when the backlog starts.
- [ ] **`LICENSE`**: your name and year. Keep `THIRD_PARTY_NOTICES.md` whatever licence you choose.
- [ ] Search for leftovers: `git grep -n -i "espforge\|Forge-Setup"` (the framework's own component names
      `forge_*` stay).

## 3. GitHub settings (the owner)

- [ ] **Settings → Pages → Build and deployment → Source: GitHub Actions.**
- [ ] **Settings → Environments → github-pages → Deployment branches and tags → Tag rule `v*`.**
- [ ] **Settings → Rules → Rulesets → New tag ruleset** on `v*`, only the owner may create, update or delete.
- [ ] `gh auth status` on the PC shows an account with `repo` and `workflow` scopes.

## 4. First build, first release

1. `idf.py -B build\v55 -D SDKCONFIG=build\v55\sdkconfig build`; flash with the helper; check `ota: Running` and that
   the setup network has the new name.
2. `python tools/harness/harness.py` (after resetting the baseline, with `--update-baseline`).
3. Commit, push `main`: CI builds (no release).
4. Tag `v0.1.0-rc.1`: a pre-release and a Beta-only site (docs/RELEASING.md "First release"). With the board on a
   test build labelled `v0.1.0-rc.0` and the Beta channel: `harness.py --ota v0.1.0-rc.1`.
5. With the owner's OK, tag `v0.1.0`: Stable is offered too.

## A new board

Board support is an ESP-IDF component named `board` under `boards/<name>/board/`, selected by one line in the root
`CMakeLists.txt`:

```cmake
set(EXTRA_COMPONENT_DIRS boards/ws_amoled175)
```

A new board is a new `boards/<name>/board/` directory exposing the same `board.h` API as `boards/ws_amoled175/board/`
(see [COMPONENTS.md](COMPONENTS.md)): `BOARD_NAME`, `DISP_W`, `DISP_H`, `BOARD_ROUND`, `board_init()`,
`display_lock()` / `display_unlock()`, `display_brightness()`, the touch read and the shared I2C bus. Then switch the
`EXTRA_COMPONENT_DIRS` line and update `forge.json` `screen` and `chip`.

What to carry over from the first board (docs/LESSONS.md): panel quirks stay inside the board component (L88);
esp_lcd's interrupt on the LVGL task's core and no esp_lcd call while a band is in flight (L81, L82); a touch read
that treats repeated NACKs as "up", reads at most every 10 ms and accepts injected touches (L103-L105); draw buffers
in internal DMA RAM, sized deliberately (L86). Measure a full-screen render on the new panel before promising any
frame rate (L87).
