# 95 engineering lessons

Durable engineering and workflow notes for furble. The goal is that future
work, human or agent, does not relearn these the hard way. The CLAUDE.md files
carry one-line pointers. This document is the detailed home.

Each section is a flat list of findings. Read the section that matches the area
you are about to touch before you start.

## Hardware traps

The root CLAUDE.md lists the short form of these. The detail lives here.

- DFS clock family. Enabling BT modem sleep is what makes esp_pm DFS actually
  engage. Once DFS engages, any peripheral clocked from APB breaks subtly when
  the CPU drops frequency. Known cases: the GPS UART must select the crystal
  clock (`UART_SCLK_XTAL`, guarded by `#if SOC_UART_SUPPORT_XTAL_CLK`), and the
  LEDC backlight PWM flickers unless the display holds `ESP_PM_APB_FREQ_MAX`
  while the backlight is on. RMT is safe: it holds its own APB power lock, so
  the IR path does not need extra handling. When you touch power management,
  audit every peripheral clock source, not just the one that broke.
- Display sleep. The display releases `ESP_PM_APB_FREQ_MAX` while the panel
  sleeps and must reacquire it before `M5.Display.wakeup()`. ST7789 and ILI934x
  need a 120 ms dwell between SLPIN and SLPOUT. M5GFX does not enforce that
  dwell, so furble must.
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
- PSRAM on the S3. `SPIRAM_MALLOC_ALWAYSINTERNAL=4096` routes large allocations
  to PSRAM. DMA display buffers must stay `MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL`
  or the display corrupts.
- LVGL redraw cost. Guard every periodic `lv_image_set_src` and
  `lv_label_set_text` behind a changed check. The unconditional setters
  invalidate and redraw. Icons are compressed and cost a 12.3 KB decompress per
  64x64 draw unless the image cache covers them. Diagnosis tools are
  `CONFIG_LV_USE_REFR_DEBUG` and the console-gated invalidation logger.

## USB and flashing

- S3 native USB Serial/JTAG resets the chip on host port open. Every time the
  host opens the serial port, the chip resets to a clean idle state. Use that
  port-open as the safe state transition. Do not script a disconnect followed by
  a reconnect to force a known state: the open alone already gave you one.
- esptool prints a post-write reset that reads `FAILED` on the S3. It is benign.
  The write already succeeded. Do not treat that line as a flash failure.
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
    `4c0020fe`, on characteristics `f9150137` and `ad06c7b7`.
  - The shutter characteristic is `7fcf49c6`. The press-and-release sequence is
    `0100`, `0200`, `0100`, `0000`.
