# furble agent guide

furble is a BLE camera shutter remote for M5Stack ESP32 devices. This repo is a
friendly fork of gkoh/furble. Fork master moves only through merged PRs. Every
PR updates its plans/NN doc (implementation state and deviations) and any
CLAUDE.md whose directory it touches.

## Build

- ESP-IDF 5.x via PlatformIO (`framework = espidf`). This is NOT Arduino.
- Five board envs: m5stick-c, m5stick-c-plus, m5stick-s3, m5stack-core,
  m5stack-core2. Each has a `-debug` variant for development. CI and releases
  build only the five release envs.
- Per-env `sdkconfig.<env>` files are committed at the repo root. Debug envs
  share the release sdkconfig via `board_build.esp-idf.sdkconfig_path`.
  Regenerating builds may append derived symbols to sdkconfig files. Commit
  those changes consistently across all five files, never for just one env.
- `FURBLE_VERSION` and `FURBLE_TEST` env vars are required for every build:
  `FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e m5stick-s3-debug`
- Quirk: the global git fsmonitor breaks the first TinyGPSPlus install in each
  fresh libdeps dir. Re-run the same pio command once. Worktree-isolated agents
  cannot export `GIT_CONFIG_*` to work around it.

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

## Hardware traps (hard-won, do not relearn)

- DFS clock family: with BT modem sleep enabled, esp_pm DFS actually engages,
  and any peripheral clocked from APB breaks subtly. GPS UART needs
  `UART_SCLK_XTAL`. The LEDC backlight PWM flickers unless the display holds
  `ESP_PM_APB_FREQ_MAX` while the backlight is on. When touching power
  management, audit every peripheral clock source.
- Display sleep: the display releases `ESP_PM_APB_FREQ_MAX` while the panel
  sleeps and must reacquire it before `M5.Display.wakeup()`. ST7789/ILI934x
  need a 120 ms dwell between SLPIN and SLPOUT; M5GFX does not enforce it.
- Never hold the Control mutex across a delay. The reconnect cancel deadlock
  bricked the device (buttons and USB dead) because furble disables all M5PM1
  power button gestures at boot. Rescue: hold the side button while replugging
  USB until the green LED flashes, then reflash.
- M5PM1 (StickS3 PMIC): the first I2C transaction after its idle sleep fails
  and only wakes it. Always retry once.
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

## Layout

- `src/` + `include/`: app layer (tasks, UI, settings, GPS, platform).
- `lib/furble/`: BLE camera protocol library, one class per vendor mode.
- `lib/blowfish/`, `lib/preferences/`: support libs used by lib/furble.
- `components/icons/`: generated LVGL image arrays.
- `web-installer/`: ESP Web Tools manifest generation.
- `plans/`: numbered improvement plans, one per PR (on the plans branch until
  integration lands).
