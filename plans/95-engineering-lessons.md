# 95 engineering lessons

Implementation state (2026-11-13): master has six release envs on ESP-IDF 5.5.3. `esp32-s3-headless` builds in the reproducible
build workflow, not in the PR firmware matrix.

Durable engineering and workflow notes for furble. The goal is that future
work, human or agent, does not relearn these the hard way. The CLAUDE.md files
carry one-line pointers. This document is the detailed home.

Each section is a flat list of findings. Read the section that matches the area
you are about to touch before you start.

## Hardware traps

The root CLAUDE.md lists the short form of these. The detail lives here.

- DFS clock family. Enabling BT modem sleep is what makes esp_pm DFS actually
  engage. Once DFS engages, any peripheral clocked from APB breaks subtly when
  the CPU drops frequency. Known cases are the GPS UART and the LEDC backlight.
  The GPS UART takes its clock per target, guarded on the S3 target macro
  (`src/FurbleGPS.cpp:179`). The S3 has no REF_TICK source, so it selects
  `UART_SCLK_XTAL`; the classic ESP32 has REF_TICK at a fixed 1 MHz and no XTAL
  UART source, so it selects `UART_SCLK_REF_TICK`. The `SOC_UART_CLKS`
  initialiser macros sit at
  `components/soc/esp32s3/include/soc/clk_tree_defs.h:256` and
  `components/soc/esp32/include/soc/clk_tree_defs.h:224`; the enum members the
  code names are `UART_SCLK_XTAL` at
  `components/soc/esp32s3/include/soc/clk_tree_defs.h:264` and
  `UART_SCLK_REF_TICK` at
  `components/soc/esp32/include/soc/clk_tree_defs.h:231`. REF_TICK is
  documented as a fixed 1 MHz even while APB changes
  (`components/soc/esp32/include/soc/clk_tree_defs.h:132`). The display holds
  `ESP_PM_APB_FREQ_MAX` while the backlight is on. The LEDC divisor is computed
  once from the source clock at channel configuration (ESP-IDF
  `components/esp_driver_ledc/src/ledc.c:460-485`), so a later APB change
  rescales the PWM period. Whether that is visible as flicker is unmeasured;
  see the bench-status section below.
- RMT and the IR path, a separate mechanism. The RMT driver takes
  `ESP_PM_CPU_FREQ_MAX` for a channel without DMA and `ESP_PM_NO_LIGHT_SLEEP`
  for a channel with DMA, and never `ESP_PM_APB_FREQ_MAX` (ESP-IDF 5.5.3
  `components/esp_driver_rmt/src/rmt_common.c:230`). The IR path is safe because
  of two properties of this build, not because of the driver:
  `IR::createChannel()` never sets `with_dma`, so the zero-initialised struct
  keeps it false (`src/FurbleIR.cpp:230-238`), and furble raises the PM CPU
  maximum itself, 160 MHz by default (`include/FurblePlatform.h:25-28`,
  `src/FurblePlatform.cpp:84`). The sdkconfig boot default of 80 MHz is not what
  pins APB. `ESP_PM_CPU_FREQ_MAX` maps to `PM_MODE_CPU_MAX` (ESP-IDF
  `components/esp_pm/pm_impl.c:230-234`), which holds the CPU at its 160 MHz
  maximum, where APB runs at 80 MHz. That matters because the channel divisor is
  fixed at creation, not at enable: on the S3 the prescale comes from the cached
  APB frequency (`components/esp_driver_rmt/src/rmt_common.c:152-169`, called at
  `:253`), the clock is selected at
  `components/esp_driver_rmt/src/rmt_tx.c:324`, and the lock is only taken
  later in `rmt_tx_enable()` (`:774`). The argument therefore holds only when
  APB was already 80 MHz at channel creation. Enabling `with_dma` on the S3
  would swap `ESP_PM_CPU_FREQ_MAX` for `ESP_PM_NO_LIGHT_SLEEP`, which maps to
  `PM_MODE_APB_MIN` (`components/esp_pm/pm_impl.c:237-238`), and the 38 kHz
  carrier would drift away from the fixed divisor.
- Display sleep. The display releases `ESP_PM_APB_FREQ_MAX` while the panel
  sleeps and must reacquire it before `M5.Display.wakeup()`
  (`src/FurbleUI.cpp:1879`, `:1900`). The panel dwell rule is per panel: both
  ways on the ILI934x, one way on the ST7789 and the ST7796. ILI9342C needs
  120 ms in both directions, SLPOUT to the next SLPIN (V101 section 8.2.11,
  page 100) and SLPIN to the next SLPOUT (section 8.2.12, page 101), and the
  ILI9341 copy carries the same pair. ST7796S V1.0 sections 9.2.12 and 9.2.13,
  and ST7789V sections 9.1.11 and 9.1.12, need 120 ms from SLPOUT to the next
  SLPIN only, plus 5 ms after SLPIN and after SLPOUT before any other command.
  ILI9342C is the panel on `m5stack-core`, `m5stack-core2` and the core usb
  debug profile, three of the six display boards. M5GFX 0.2.27 sends the bare
  SLPIN or SLPOUT and no delay (`src/lgfx/v1/panel/Panel_LCD.cpp:101-107`), and
  `LGFX_Device::sleep()` and `wakeup()` only add a brightness write
  (`src/lgfx/v1/LGFXBase.hpp:1436-1437`), so both 120 ms waits are furble's.
  `UI::sleepDisplay()` refuses to sleep within 120 ms of the last wake
  (`src/FurbleUI.cpp:1843-1845`) and `UI::wakeDisplay()` waits the same interval
  before SLPOUT (`:1894-1897`). Together they satisfy all four panels. The
  5 ms needs no separate delay: after SLPIN the next panel command is the
  SLPOUT that the wake gate holds back 120 ms, and after SLPOUT the wake path
  writes only the brightness, which is the backlight and not a panel command
  (`:1901`, `:1905`).
- Never hold the Control mutex across a delay or a blocking BLE call. The
  reconnect cancel deadlock bricked the device: buttons and USB went dead
  because furble disables all M5PM1 power button gestures at boot, so there was
  no hardware escape. A retained PMIC `DL_LOCK` is not cleared by USB
  unplugging or reset. Remove battery power (disconnect, depletion, or service),
  restore it, then hold the side button until the green LED flashes and reflash.
  Design rule: take the mutex, read or
  write the shared state, release it, then do the slow work.
- M5PM1 (StickS3 PMIC) first-transaction quirk. The first I2C transaction after
  the PMIC idle sleep fails and only wakes it. Always retry once.
- M5PM1 green LED at display off. The status LED stays lit when the display is
  off unless you clear it explicitly. The fix is `setLedEnLevel(false)`, which
  saves PWR_CFG bit 4. Set it as part of the display-off path, not just at boot.
- GPS unit v1.1 (AT6668) has no backup supply. Cutting its power rail costs a
  cold start of roughly 108 s. `$PCAS12` timed standby keeps ephemeris and is
  the way to save power without paying the cold start. `$PCAS02` only accepts
  100 to 1000 ms. Sub-second update rates need `$PCAS03` sentence pruning first.
- StickS3 has no 32.768 kHz crystal. BLE falls back to the main crystal for its
  low-power clock. The connected-idle current floor is about 3.3 mA and that is
  final: no firmware change moves it, because the hardware lacks the slow clock.
- `perf tasks` reports free bytes. Its `stack_high_watermark` is
  `uxTaskGetStackHighWaterMark`, which ESP-IDF returns as the minimum free
  stack in bytes, not bytes used. Plans 305, 308 and 311 read it as bytes used.
  That sized the 8 KiB main stack that overflowed (3,020 free at 12 KiB with
  MQTT connected meant about 9,268 used) and hid a 3,392-byte plain MQTT
  worker behind a 3 KiB cut.
  Used is the configured stack minus the reading.
- PSRAM on the S3. `SPIRAM_MALLOC_ALWAYSINTERNAL=4096` routes large allocations
  to PSRAM. DMA display buffers must stay `MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL`
  or the display corrupts.
- LVGL redraw cost. Guard every periodic `lv_image_set_src` and
  `lv_label_set_text` behind a changed check. The unconditional setters
  invalidate and redraw. Icons are compressed and cost a 12.3 KB decompress per
  64x64 draw unless the image cache covers them. Diagnosis tools are
  `CONFIG_LV_USE_REFR_DEBUG` and the console-gated invalidation logger.
- The ESP system clock survives most soft resets. With `RTC_HRT` time keeping
  the RTC timer keeps `gettimeofday()` running through `esp_restart()`,
  panics, most watchdog resets and USB resets. Power-on, brownout, a clock
  glitch and RTC or super watchdog system resets (also reported as
  `ESP_RST_WDT`) lose it. Never seed the system clock from an older checkpoint
  after a reset that kept it. TimeKeeper keeps its last accepted sample in
  `RTC_NOINIT_ATTR` memory and trusts the running clock when the reset reason
  and the record CRC allow it, failing closed when the clock reads behind the
  record. M5Unified overwrites the system clock from an RTC chip inside
  `M5.begin()`, so on those boards the pre-reset clock is already gone by
  `TimeKeeper::init()` (plan 139).
- A value that crossed a reboot loses its authority. The kept clock is carried
  by the RC slow clock and SNTP can write it directly, so it restores as `RTC`,
  never as GPS or NTP. Keeping GPS authority locked out every later GPS fix
  once the kept clock was more than 5 s fast, because equal-priority backward
  corrections are rejected regardless of uncertainty (plan 139 review).

## USB and flashing

- S3 native USB Serial/JTAG resets the chip on host port open. Every time the
  host opens the serial port, the chip resets to a clean idle state. Use that
  port-open as the safe state transition. Do not script a disconnect followed by
  a reconnect to force a known state: the open alone already gave you one. This
  is a bench observation with no source file behind it, so it is a rule to
  design around rather than a proven property (see the bench-status section
  below).
- The `FAILED` string is never about a reset. In esptool 4.11.0 the only
  `FAILED` strings after a write are the flash verify outcomes
  (`esptool/cmds.py:1382` for a digest mismatch and `esptool/cmds.py:1389` for
  byte differences). A `FAILED` line means the image did not verify, so neither
  dismiss it nor read it as a reset problem.
- The reset phase prints `Hard resetting via RTS pin...`
  (`esptool/loader.py:1711`) and then toggles RTS. On the S3 native USB
  Serial/JTAG part the port is already gone when the toggle runs, so it raises a
  port error. A macOS bench run saw errno 6, ENXIO; the number is a bench
  observation, because other hosts raise their own code, and `reset.py:51`
  ignores only ENOTTY and EINVAL, so this one is retried and then re-raised
  (`esptool/reset.py:43-62`). The traceback is benign: `write_flash` has
  returned and the hash has verified, so the image is good and only the reset
  could not be signalled. The command still exits non-zero.
- `board_upload.after_reset = watchdog_reset` on `m5stick-s3`
  (`platformio.ini:64`) reaches `ESP32S3ROM.watchdog_reset()`, which arms the
  RTC watchdog and prints `Hard resetting with a watchdog...`
  (`esptool/targets/esp32s3.py:355-356`). It does not touch RTS, so it avoids
  the failing toggle path. It is not a fallback to a classic hard reset: the
  generic warning and fallback are in `ESPLoader.watchdog_reset`
  (`esptool/loader.py:1741`), which only chips without an override reach.
  `ESP32S3ROM.hard_reset()` does the reverse, preferring the watchdog when
  `uses_usb_otg()` is true, and that is false on the USB Serial/JTAG part, which
  reports `UARTDEV_BUF_NO_USB_JTAG_SERIAL` instead
  (`esptool/targets/esp32s3.py:84`, `:321`).
- Do not run `pio run` inside a `/tmp` or `/private/tmp` worktree. PlatformIO's
  package installer copies the `.git` directory with `shutil.copy2`, which
  fails on fsmonitor sockets under macOS temp paths and aborts the build with no
  useful message. Build from a normal repo path.
- PlatformIO bundles cmake at `~/.platformio/packages/tool-cmake/bin`. It is not
  on PATH. Point at it directly when a tool needs cmake.
- The global git fsmonitor breaks the first TinyGPSPlus dependency install in
  each fresh libdeps dir. Re-run the same pio command once and it succeeds.
  Worktree-isolated agents cannot export `GIT_CONFIG_*` to work around it.

## Build and CI gotchas

- Debug envs share the release sdkconfig via
  `board_build.esp-idf.sdkconfig_path`. Regenerating a build can append derived
  symbols to sdkconfig files. Commit those changes across all five board files
  consistently, never for a single env.
- Build every documented env before pushing config, enum, or setting changes,
  including the non-CI `esp32-s3-headless` env. That env is not in CI and
  shipped compile-broken twice: once from missing `Request` enum members in the
  `FURBLE_NO_DISPLAY` branch, once from a new `WEB_UI` setting missing from the
  `FurbleConsole` switches. CI green did not catch either. Add
  `esp32-s3-headless` to CI so this cannot recur.
- New settings enum value checklist. A new settings enum value must be added in
  five places for a combined display plus headless build to pass:
  `settingType`, `printValue`, `setValue`, `appliesWhen`, and the reload
  dispatch. A miss is a `-Werror=switch` failure that only appears in the
  headless build. A stale `.o` can hide it on an incremental build, so do a
  clean build to confirm.
- The sim keeps its own source list in `sim/build.sh`, separate from
  `src/CMakeLists.txt`. Add each new `.cpp` to both. A firmware build passing
  does not mean the sim links: omitting a file from `sim/build.sh` gives
  undefined-symbol link errors for everything defined in it. Hardware-only
  modules also need a sim shim in `sim/shim` that matches the real header's
  include guard.
- The sim had a `Preferences::begin` data race: `handleFor` touched a static map
  before taking `values_mutex`, which caused intermittent SIGSEGV in the
  ui-screenshots capture. The fix widened the lock to cover the map access.
- Never grow the process environment while another thread can call getenv.
  glibc walks `environ` without a lock, and a setenv that adds a variable
  reallocates the array and frees the old one under that walk. The sim's SDL
  main loop calls getenv thousands of times a second, so a growing setenv on
  the simulator thread is a use-after-free that shows up as a rare SIGSEGV
  under load (issue #283). A replace is one pointer store and is safe. The
  coverage runtime's own `__LLVM_PROFILE_RT_INIT_ONCE` setenv is enough to
  make the next growth a freeing one. Check every caller, firmware included,
  and check ordering rather than thread alone: production Timezone's boot
  `setenv("TZ")` grows the environment after `SDL_Init`, but before
  `panelReady` releases the loop, so it was ordered and not a live race. The
  sim creates `TZ` first anyway, so the guard can forbid all growth after
  `SDL_Init`.
- On macOS, Homebrew SDL2 is sdl2-compat. It dlopens SDL3 through its own
  rpath, and the ASan runtime's dlopen interceptor hides that rpath. A
  sanitized sim then fails to find SDL3, raises a modal desktop dialog per
  process and aborts. `sim/build.sh` links the SDL3 directory as an rpath into
  sanitized Darwin builds. Check `otool -l` for it, and prove SDL loads with a
  single process before any parallel sanitized run on a shared desktop.
- GitHub Actions cache is branch-scoped. A `pull_request` run cannot restore a
  cache written by a master push, even through a `restore-keys` prefix. For a
  cross-branch baseline, download an artifact from the latest successful master
  run with `gh run download`, not `actions/cache`. This was the size-report
  baseline fix.
- Triage a CI failure by the failing step name and its exit code, not by
  grepping `error:` across the whole log. Exit code 139 is a runtime SIGSEGV,
  not a compile error. A job that builds and links clean can still fail at a
  later runtime or capture step. Broad `error:` greps also match benign M5GFX
  constexpr notes and hide the real failing step.
- Actions was disabled for three weeks; merges without CI accumulated these
  failures. GitHub Actions was off on the fork from 2026-10-21 to 2026-11-13,
  and PRs #411 to #439 merged with no check runs at all. Re-enabling it turned
  master red in eight jobs with eight separate root causes. #447 and #448
  fixed two on master; `ci/master-green` fixes the rest:
  - clang-format (code, #447): twelve sim and host test files were never
    formatted. CI installs clang-format 21.1.8 from pip; the local 21.1.2
    agrees with it on every file, so this was not version drift. #447 also
    added `tools/check_format.py` to `pre_review.py`.
  - build (m5stick-c) (workflow): the partition-table check reads the stock
    ESP-IDF CSV from the framework package but ran before the build installed
    it. It only passed on a warm PlatformIO cache, and every cache expired
    while Actions was off. The check now runs after the build.
  - ASan and UBSan host tests (test): the GPX writer builds its path in a
    64-byte buffer and the test sandbox was an absolute build-tree path, so
    every path was truncated. The shallow plain build tree hid it; the deeper
    sanitized tree truncated into the directory itself. The writer now sizes
    its buffer from the directory and fails on truncation, so the long
    sandbox stays as the regression test.
  - Coverage floor (runner drift, #448): compiler-rt now lives in
    `libclang-rt-18-dev`, which `clang-18` only recommends, and the workflow
    installs without recommends. The sanitizer runtime was missing and the
    fuzz targets failed configure. #448 names the package and then fixed the
    three test and coverage faults behind it; see plan 314.
  - sim-e2e (simulator model): `memory-pressure-mqtt-shed` asserted a figure
    measured on macOS. The host MQTT transport allocated its own adapter
    object, strings and thread on the owner task, and the heap counter charged
    them as device memory. Their sizes follow the host C++ library and pthread
    ABI, so the paused figure was 8,497 on macOS, 8,457 on arm64 Linux and
    8,441 on x86_64 Linux. Every libmosquitto call and the adapter's own
    objects now run inside a `HostAllocScope`, like every other sim hook
    production code calls. In their place `esp_mqtt_client_init()` charges
    what esp-mqtt allocates on the device (3,480 bytes for the StickS3 MQTT
    config), and destroy releases it.
  - sim-sanitizers (third-party read of uninitialized memory, then a
    suppression-path bug): M5GFX's `Panel_sdl` divides by the renderer
    output size, which SDL never writes when the dummy video driver has no
    accelerated renderer. The result is
    stack garbage. clang 18 on ubuntu-24.04 made it an infinite float, which
    `sim/ubsan.supp` already suppressed; clang 21 on ubuntu-26.04 made it a
    signed overflow in every scenario. The simulator now asks SDL for its
    software renderer under a headless driver, so the size is real and both
    reports are gone. The live renderer then leaked at exit, because M5GFX
    never destroys it on close, so the SDL shim now destroys live renderers
    before `SDL_Quit`. Behind it, the LVGL entries in `sim/ubsan.supp` named
    `lvgl__lvgl/src/...`, the managed_components path, while CI clones LVGL
    to `sim/.pio/libdeps/sim/lvgl`. They now start at `lvgl/src/`, which
    matches both. A suppression list must be proven on the CI layout.
  - Apple (runner retirement): GitHub retired the macos-14 images on
    2026-11-02 and cancels jobs on that label within a second. The workflow
    moved to macos-15 with Xcode 16.4.
- A merged PR whose head has no check runs is now a hard failure.
  `tools/check_review_threads.py`, the last gate before every merge, also
  reads the head commit's check rollup. It fails when no check ran, when a
  check is still running, and when any check ended in anything but success,
  neutral or skipped. Run against #439 it reports that no CI ran on its head.
- A gate that judges a pull request must read its evidence at the base commit,
  not from the checkout. The merge gate's path-filter check read the working
  tree, so an out-of-date clone decided whether a workflow should have run and
  accepted heads that the base filters cover (#464, plan 331). Read such inputs
  through the API at `baseRefOid` and fail closed when they cannot be read.
- A green local gate set is not CI. `pre_review.py` does not run the
  sanitizers or the Linux simulator, so a branch can pass it and still break
  both. Never merge on local evidence alone; wait for the checks.

## Multi-agent and git workflow

- Force-push and other destructive git needs the user's own typed
  authorization. A coordinator-relayed instruction is rejected by the subagent
  classifier. Working pattern: the subagent commits, reports the SHA and the
  exact push command, and the main session runs the push. Resolve the current
  remote tip with `git ls-remote fork <branch>` and push with
  `--force-with-lease=<branch>:<current-tip>` as the guard.
- Always `git fetch fork master` before creating worktrees or rebasing. A stale
  remote-tracking ref makes the rebase land on old content and can silently drop
  work with no error. Read the base SHA with `git rev-parse --short fork/master`
  and confirm it before starting.
- gh-stack rebase is for linear chains only. A fan-out family, meaning siblings
  that share one base branch, must use `git rebase --onto <new-parent-tip>
  <old-parent-tip>` per dependent. Running gh-stack rebase on a fan-out tree
  relinearizes the siblings and corrupts their PR bases.
- Wire-id and `Camera::Type` enum ledger. Record every allocation in the plan
  doc that introduces it. When two branches allocate the same value, the
  second-to-merge renumbers its collision and updates every downstream
  reference. Enum values are persisted in NVS, so an existing value is never
  renumbered or reused.
- Sweep for conflict markers across all file types, including `.md` and `.txt`,
  before staging. A marker once slipped into `src/CLAUDE.md` because the grep
  was scoped to source and header directories. Do not rely on the build to catch
  it: a marker in an unreached code path produces no error.
- Never `git stash`, `checkout` or `restore` files while a `pio run` is in
  progress. ESP-IDF rewrites `sdkconfig.<env>` in place during configure, so a
  stash and pop in that window left `sdkconfig.m5stick-c` 489 lines short and
  the build reading it. Stop the build, `git checkout -- sdkconfig.*`, rebuild.
  A concurrent `pio run` also refreshes `managed_components/`, which breaks a
  simulator build running at the same time; run them one after the other.
- The two-gate merge rule. Code-review approval and full per-check CI green are
  both independently necessary. Each has missed a real defect the other caught.
  CI green alone missed a runtime SIGSEGV. Review alone missed it too. Require
  both, and treat the screenshot and capture runtime steps as part of CI green.

## Protocol and project

- Every camera or protocol PR must cite its data source with a real URL. A
  capture log, vendor doc, open-source implementation, or datasheet link. A name
  mention without a link does not satisfy this. PRs have been flagged
  non-compliant for citing a source by name only.
- Only Fujifilm cameras are testable on real hardware. Every other vendor is
  covered by code review and the FauxNY software test camera, and must be
  declared untested in the PR.
- Fujifilm X100VI Secure golden GATT handshake. The reference capture for the
  Secure mode pairing and shutter sequence:
  - STATUS read returns `16552300`. furble responds `16552320`.
  - Identity write is the ASCII string `furble-<id>`.
  - Two registration-accept notifications arrive as `0100` on service
    `4c0020fe`, on characteristics `f9150137` and `ad06c7b7`. The dedicated
    `f9150137` (CHR_NOT1) confirmation fires on a fresh registration only. A
    saved reconnect skips it and goes straight to the periodic `0100` geotag
    request on `ad06c7b7` (GEOTAG_UPDATE) every 10 s, which is implicit
    acceptance: the camera only requests geotag data from a client it has
    accepted (PR #239, hardware trace 2026-08-28).
  - The shutter characteristic is `7fcf49c6`. The press-and-release sequence is
    `0100`, `0200`, `0100`, `0000`.

## Follow-up: hardware-trap corrections (2026-11-25)

An independent source check found claims in this document that the pinned
sources do not support. Each item below names what was wrong and the source that
disproves it. The trap text above is corrected in place, following the
`## Follow-up:` convention in `plans/CLAUDE.md` rather than starting a new plan.

Package versions behind the citations: ESP-IDF 5.5.3 (`framework-espidf`),
esptool 4.11.0 (`tool-esptoolpy`), M5GFX 0.2.27. Paths without a `docs/`,
`plans/`, `src/`, `include/` or `tests/` prefix are inside the ESP-IDF or
esptool package. The M5GFX paths keep the library's own `src/lgfx/` prefix and
live in the pinned `libdeps` copy, not in this tree. Review record:
[473-review-feedback.md](473-review-feedback.md).

1. RMT "holds its own APB power lock" was wrong.
   `components/esp_driver_rmt/src/rmt_common.c:230` selects
   `ESP_PM_CPU_FREQ_MAX` for a channel without DMA and `ESP_PM_NO_LIGHT_SLEEP`
   for a channel with DMA. `ESP_PM_APB_FREQ_MAX` appears nowhere in
   `esp_driver_rmt`. The operational conclusion survives on furble facts, not on
   the driver: `src/FurbleIR.cpp:230-238` leaves `with_dma` false, and furble
   raises the PM CPU maximum to 160 MHz at boot
   (`include/FurblePlatform.h:25-28`, `src/FurblePlatform.cpp:84`), where APB
   runs at 80 MHz. The 80 MHz sdkconfig boot default is not the reason the APB
   clock holds. The divisor is fixed at channel creation
   (`components/esp_driver_rmt/src/rmt_tx.c:324`), before the driver takes the
   lock in `rmt_tx_enable()` (`:774`), so the argument holds only when APB was
   already 80 at creation. Enabling `with_dma` on the S3 would swap the lock for
   `ESP_PM_NO_LIGHT_SLEEP`, which maps to `PM_MODE_APB_MIN`
   (`components/esp_pm/pm_impl.c:237-238`), and the 38 kHz carrier would drift
   away from the fixed divisor.
2. The GPS UART guard was misquoted. `src/FurbleGPS.cpp:179` guards on
   `defined(FURBLE_M5STICKS3) || defined(CONFIG_IDF_TARGET_ESP32S3)`, not on
   `SOC_UART_SUPPORT_XTAL_CLK`. For the record, that macro does exist for the S3
   (`components/soc/esp32s3/include/soc/soc_caps.h:432`), so the original text
   described a guard the code does not use, not a macro that is absent. The
   per-chip clock reasons now carry citations in the trap text.
3. The esptool reset claim was wrong. esptool 4.11.0 has exactly two `FAILED`
   strings, both in the flash verify path (`esptool/cmds.py:1382` and `:1389`),
   and the reset prints `Hard resetting via RTS pin...`
   (`esptool/loader.py:1711`). There is no post-write reset line that reads
   `FAILED`, so a `FAILED` line must never be dismissed. What stays benign is a
   port error from the RTS toggle on a port the USB Serial/JTAG part already
   removed. The error number itself is a bench observation, not a contract: a
   macOS run saw errno 6, ENXIO, and other hosts report their own code.
   `esptool/reset.py:51` ignores only ENOTTY and EINVAL, so this error is
   retried and then re-raised (`:43-62`). The image has verified by then.
   `board_upload.after_reset = watchdog_reset` on `m5stick-s3` arms the RTC
   watchdog (`esptool/targets/esp32s3.py:355-356`) instead of toggling RTS.
4. The panel dwell rule was stated backwards, first as a two-way dwell and
   then as a one-way rule for every panel. Neither is right. The rule is per
   panel. ILI9342C needs 120 ms in both directions, SLPOUT to the next SLPIN
   (V101 section 8.2.11, page 100) and SLPIN to the next SLPOUT (section
   8.2.12, page 101), and the ILI9341 copy carries the same pair. ST7796S V1.0
   sections 9.2.12 and 9.2.13 need 120 ms from SLPOUT to the next SLPIN only,
   and ST7789V sections 9.1.11 and 9.1.12 carry that same one-way limit. Both
   families also want 5 ms after SLPIN and after SLPOUT before any other
   command. ILI9342C is the panel on `m5stack-core`, `m5stack-core2` and the
   core usb debug profile, three of the six display boards, so the wake wait is
   required and it stays. `src/FurbleUI.cpp:1843-1845` enforces the 120 ms from
   SLPOUT to the next SLPIN and `src/FurbleUI.cpp:1894-1897` enforces the same
   interval from SLPIN to the next SLPOUT. M5GFX 0.2.27 writes the bare command
   (`src/lgfx/v1/panel/Panel_LCD.cpp:101-107`), so both waits are furble's, and
   together they satisfy all four panels. The earlier deferral that proposed
   dropping the wake wait is withdrawn. The reversal appeared in
   `include/FurbleUI.h:1135`, `plans/implemented/322-hardware-abstraction.md`, the display
   trap above, `docs/power/display.md`,
   `plans/287-mcu-portability-and-bundle-hardware.md`,
   `plans/implemented/12-display-off.md`, `docs/power/board-power.md` and
   `docs/power/soc.md`; all of them now state the per-panel rule.
5. The W5500 hardware reset pulse is a known defect. WIZnet W5500 datasheet
   version 1.1.0, section 5.5.1 figure 22 and table 2, require RESETn low for at
   least 500 us, and RESETn is pin 37 in the pin description table on page 9.
   The pinned PHY hard-codes 100 us
   (`components/esp_eth/src/spi/w5500/esp_eth_phy_w5500.c:160`). The record, the
   fix direction and the host-test gap are in
   `plans/implemented/42-waveshare-eth-node.md` under its follow-up section.
6. The PSRAM DMA caps claim was over-stated. Display draw buffers must stay
   `MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL` (`src/FurbleUI.cpp:581-583`), and that
   requirement stands. The corruption a wrong caps set would cause is
   unmeasured, and on the S3 PSRAM is not categorically unusable for DMA:
   `SOC_PSRAM_DMA_CAPABLE=1`
   (`components/soc/esp32s3/include/soc/soc_caps.h:57`) and
   `esp_ptr_dma_ext_capable()` returns true for PSRAM pointers when
   `CONFIG_SPIRAM` is set (`components/esp_hw_support/esp_memory_utils.c:20-30`).
   The trap text above carries the same caveat; the bench entry below is the
   only place a corruption symptom may be quoted.

### Bench status of the symptoms

Source proves the mechanism, not the symptom. No run in
`plans/00-hardware-experiments.md` measures any of these three, so each symptom
below is a hypothesis to test on hardware, not a documented fact.

- LEDC backlight flicker. Source: the LEDC divisor is computed once from the
  source clock when the channel is configured
  (`components/esp_driver_ledc/src/ledc.c:460-485`), and `UI::UI()` holds
  `ESP_PM_APB_FREQ_MAX` while the panel is on (`src/FurbleUI.cpp:511`).
  Unproven: that releasing the lock produces visible flicker at any brightness.
  Bench: brightness sweep with the lock released plus a camera flicker check, as
  `docs/power/board-power.md:1059` already requires.
- Chip reset on a USB Serial/JTAG host port open. Source: none in furble. The
  behaviour belongs to the USB Serial/JTAG peripheral and the host driver, and
  no source file asserts it: this document's USB section and the root
  `CLAUDE.md` repeat it as a bench observation. Unproven: that every open resets
  the chip on every host. Bench: count boot banners per port open on macOS,
  Linux and Windows hosts.
- Display corruption from a wrong DMA caps set. Source: the draw buffers are
  allocated with `MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL`
  (`src/FurbleUI.cpp:581-583`). The S3 sets `SOC_PSRAM_DMA_CAPABLE=1`
  (`components/soc/esp32s3/include/soc/soc_caps.h:57`) and
  `esp_ptr_dma_ext_capable()` returns true for PSRAM pointers there
  (`components/esp_hw_support/esp_memory_utils.c:20-30`), so "a PSRAM buffer
  always corrupts" is not a safe claim on the S3. Unproven: the corruption
  itself. Bench: force a PSRAM draw buffer and compare the panel output.
