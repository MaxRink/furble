# furble agent guide

furble is a BLE camera shutter remote for M5Stack ESP32 devices. This repo is a
friendly fork of gkoh/furble. Fork master moves only through merged PRs. Every
PR updates its plans/NN doc (implementation state and deviations) and any
CLAUDE.md whose directory it touches.

## Build

- ESP-IDF 5.x via PlatformIO (`framework = espidf`). This is NOT Arduino.
- Six board envs: m5stick-c, m5stick-c-plus, m5stick-s3, m5stack-core,
  m5stack-core2, and waveshare-s3-eth. Each has a `-debug` variant for
  development. CI and releases build only the six release envs.
- Per-env `sdkconfig.<env>` files are committed at the repo root. Debug envs
  share the release sdkconfig via `board_build.esp-idf.sdkconfig_path`.
  Regenerating builds may append derived symbols to sdkconfig files. Commit
  those changes consistently across all six release files, never for just one
  env.
- `FURBLE_VERSION` and `FURBLE_TEST` env vars are required for every build:
  `FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e m5stick-s3-debug`
- The PlatformIO version adapter expands the exact `dev` value to
  `dev+g<short-revision>`, adding `.dirty` for tracked, staged, or non-ignored
  untracked changes. Explicit release and experiment versions are unchanged.
- Quirk: the global git fsmonitor breaks the first TinyGPSPlus install in each
  fresh libdeps dir. Re-run the same pio command once. Worktree-isolated agents
  cannot export `GIT_CONFIG_*` to work around it.
- CI validation workflows use path filters rather than pull request base-branch
  filters, so stacked PRs run without retargeting. Safe validation workflows
  also expose `workflow_dispatch`; use the Actions tab to select a branch.
  Manual PlatformIO dispatches always run the complete firmware matrix because
  a dispatch has no meaningful comparison base.
  Android's optional `run_emulator` input keeps the default dispatch fast.
- Before pushing config, enum, or settings changes, clean-build every
  documented env, including the non-CI `esp32-s3-headless` env. A new settings
  enum value must be added in five places or the headless build fails
  `-Werror=switch`. See `plans/95-engineering-lessons.md`.
- The direct SDL simulator build writes compiler depfiles beside each object.
  Its incremental cache follows project and dependency headers with `make -q`;
  run `sh sim/scripts/test-build-deps.sh` when changing this cache logic.
- Board-specific ESP-IDF component dependencies must be gated by a CMake
  profile argument. A compiler define alone is too late for component
  dependency resolution. The Waveshare profile passes `FURBLE_ETHERNET` both
  ways for this reason.

## Style

- Short declarative sentences. Plain English. No em-dashes anywhere: code
  comments, commits, PRs, docs.
- 2-space indent. Match existing naming. clang-format 21 is CI-enforced
  (see `.clang-format`).

## Testing

- The USB console (debug builds, `FURBLE_CONSOLE`) is the automation surface:
  settings get/set, gps subcommands, shutter, status.
- Hardware verification on the attached M5StickS3 happens before PRs.
- Only Fujifilm cameras are available. Other vendors get code review plus the
  FauxNY test camera and are declared untested in the PR.

## Documentation (keep docs in sync, every PR)

Any PR that changes user-facing behavior, settings, console commands, supported
hardware, supported cameras or vendors, supported GPS units, boards, or the UI
MUST update the matching docs in the same PR. A behavior change without its doc
change is incomplete and should not merge. Verify each claim against the code,
not against a plan doc.

Doc surface to change-type mapping:

| Change type | Docs to update in the same PR |
|---|---|
| User-facing feature or behavior | `README.md`, the matching `docs/` reference, and `docs/wiki/` |
| Setting added, removed, renamed, or default changed | `docs/settings-and-controls.md`, `docs/wiki/Settings-Reference.md`, and the README settings overview |
| Console command or subcommand | `docs/console-commands.md`, the README serial console block, and `docs/wiki/` |
| Supported board added or dropped | `README.md` Supported Controllers, `docs/supported-hardware.md`, and `docs/wiki/Getting-Started.md` |
| Supported camera or vendor | `README.md` Supported Cameras and the feature table, and `docs/supported-hardware.md` |
| Supported GPS unit | `README.md` GPS section, `docs/supported-hardware.md`, and `docs/wiki/` |
| Any UI change (a page, a menu entry, a label) | regenerate `docs/ui-walkthrough.md` screenshots with `sim/scripts/docs-capture.sh`, then apply the rows above that also match |
| Contributor workflow, build, or CI | `CONTRIBUTING.md`, `CLAUDE.md`, and `AGENTS.md` |

The wiki is in-repo under `docs/wiki/`. The GitHub wiki repo
(`MaxRink/furble.wiki.git`) is published by hand from those files, so update the
in-repo copies and note in the PR that the wiki needs a push.

Reviewers confirm the mapped docs were updated for any user-facing, behavior, or
UI change before approving.

## Hardware traps (hard-won, do not relearn)

- DFS clock family: with BT modem sleep enabled, esp_pm DFS actually engages,
  and any peripheral clocked from APB breaks subtly. GPS UART needs
  `UART_SCLK_XTAL`. The LEDC backlight PWM flickers unless the display holds
  `ESP_PM_APB_FREQ_MAX` while the backlight is on. RMT is safe: it holds its own
  APB power lock. When touching power management, audit every peripheral clock
  source.
- Display sleep: the display releases `ESP_PM_APB_FREQ_MAX` while the panel
  sleeps and must reacquire it before `M5.Display.wakeup()`. ST7789/ILI934x
  need a 120 ms dwell between SLPIN and SLPOUT; M5GFX does not enforce it.
- Never hold the Control mutex across a delay. The reconnect cancel deadlock
  bricked the device (buttons and USB dead) because furble disables all M5PM1
  power button gestures at boot. A retained `DL_LOCK` is cleared only by true
  PMIC power loss, not USB unplugging or reset. Restore battery power, hold the
  side button until the green LED flashes, then reflash.
- M5PM1 (StickS3 PMIC): the first I2C transaction after its idle sleep fails
  and only wakes it. Always retry once. The status LED stays lit at display off
  unless you clear it: `setLedEnLevel(false)` saves PWR_CFG bit 4.
- S3 native USB Serial/JTAG resets the chip on each host port open. Use that
  port-open as the safe state transition, never a disconnect then reconnect.
  esptool prints a post-write reset that reads `FAILED` on the S3: it is benign.
- GPS unit v1.1 (AT6668): no backup supply, so a rail cut costs a ~108 s cold
  start. `$PCAS12` timed standby works. `$PCAS02` only accepts 100-1000 ms.
  Sub-second rates need `$PCAS03` sentence pruning first.
- LVGL: guard every periodic `lv_image_set_src` / `lv_label_set_text` behind a
  changed check. Unconditional setters invalidate and redraw. Icons are
  compressed and cost a 12.3 KB decompress per 64x64 draw unless the image
  cache covers them. `CONFIG_LV_USE_REFR_DEBUG` plus the console-gated
  invalidation logger are the diagnosis tools.
- PSRAM (S3): `SPIRAM_MALLOC_ALWAYSINTERNAL=4096` routes large allocations to
  PSRAM. DMA display buffers must stay `MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL`.
- ESP-IDF 5.5 per-task heap tracking can deadlock concurrent frees during WiFi
  association. Keep `CONFIG_HEAP_TASK_TRACKING` disabled on every board until
  the upstream fix is present in the pinned SDK. Capability heap diagnostics
  do not require it.
- Full detail and more findings live in `plans/95-engineering-lessons.md`. Read
  the matching section before you touch that area.
- Settings are called from the UI and background tasks. Each NVS transaction
  must own its own `Preferences` wrapper; never share a mutable begin/end handle
  across tasks.

## Layout

- `src/` + `include/`: app layer (tasks, UI, settings, GPS, platform).
- `lib/furble/`: BLE camera protocol library, one class per vendor mode.
- `lib/blowfish/`, `lib/preferences/`: support libs used by lib/furble.
- `components/icons/`: generated LVGL image arrays.
- `sim/`: host SDL simulator for the UI (shims, fakes, script driver). It
  never modifies firmware sources.
- `web-installer/`: ESP Web Tools manifest generation.
- `.github/workflows/pages.yml`: tag-triggered GitHub Pages installer builds.
- `plans/`: numbered improvement plans, one per PR (on the plans branch until
  integration lands).
