# furble agent guide

furble is a BLE camera shutter remote for M5Stack ESP32 devices. This repo is a
friendly fork of gkoh/furble. Fork master moves only through merged PRs. Every
PR updates its plans/NN doc (implementation state and deviations) and any
CLAUDE.md whose directory it touches.

## Build

- ESP-IDF 5.x via PlatformIO (`framework = espidf`). This is NOT Arduino.
- Six board envs: m5stick-c, m5stick-c-plus, m5stick-s3, m5stack-core,
  m5stack-core2, and waveshare-s3-eth. Each has a `-debug` variant for
  development. CI builds the six release envs and mandatory debug profiles;
  releases build the six release envs and their matching debug profiles.
- The three 4 MB debug envs use size-optimized debug flags (`-Os -g2 -ggdb2`)
  to keep dual-OTA slots while retaining symbols, logs and assertions; stepping is less exact.
- `m5stack-core-usb-debug` is a developer-only USB-UART fallback with one
  factory app partition. Normal CI uses it as the mandatory Core debug profile.
  It is not part of the six-board release or web installer matrices and has no
  wireless update or rollback path. The legacy `m5stack-core-debug` OTA profile
  is an explicit workflow-dispatch opt-in.
- Unsupported Enterprise WiFi and SoftAP are compiled out on all boards.
  WPA2/WPA3 Personal station mode and application TLS remain enabled.
- The HTTPS WebUI is compiled with MQTT only on the 8 MB and 16 MB profiles.
  It stays closed unless its opt-in setting and a checked non-empty companion
  password are both present.
- Before auth integration, Core debug output was 1,732,597 bytes with 54,296 bytes RAM,
  leaving 8,203 bytes in the 1,740,800-byte OTA slot. The `-Os` profile alone
  was 1,781,453 bytes and Enterprise-only removal was 1,780,889 bytes, both
  over the slot; OTA and TLS remain enabled.
- Auth integration with `-Os` reached 1,743,717 bytes, 2,917 over the slot.
  The `-Oz` experiment produced the identical size despite confirmed object-file
  flags, so it was reverted. The combined image still does not fit; release
  optimization, security features and the OTA layout are unchanged.
- Per-env `sdkconfig.<env>` files are committed at the repo root. Debug envs
  share the release sdkconfig via `board_build.esp-idf.sdkconfig_path`.
  Regenerating builds may append derived symbols to sdkconfig files. Commit
  those changes consistently across all six release files, never for just one
  env. Regeneration also strips comments and rewrites deprecated aliases to
  match their real options, so keep the reason for a setting in its plan doc,
  never in an sdkconfig comment.
- S3 release sdkconfigs disable `CONFIG_SPIRAM_MEMTEST` to remove the full
  boot-time PSRAM test. The S3 debug envs merge `sdkconfig.spiram-memtest` back
  in. `patches/debug_sdkconfig.py` accepts comma-separated fragments so a
  diagnostic env can layer one on `sdkconfig.debug`.
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
- The simulator power gate compares each report against its baseline with the
  two-sided `tools/power-model/compare.py --threshold` policy. The default 10%
  value is compatibility policy, not calibrated hardware noise. Focused
  comparator coverage is `python3 -m unittest tests/test_power_compare.py`.
  `tools/power-model/README.md` distinguishes the legacy-default report from
  the opt-in synthetic microsecond accounting mode; neither is a hardware
  current measurement.
- The Apple workflow packages the unsigned macOS Debug app only after its tests
  pass. The uploaded artifact includes a checksum and source/Xcode provenance;
  it is for companion testing, not signing or release distribution.
- Release tags beginning with `companion-test-` are reserved for companion
  testing and skip the firmware release workflow; other release tags are
  unchanged. Release and Pages matrices omit the Core single-factory debug
  profile because it has no OTA data partition. The release artifact job has
  scoped `contents: write` permission;
  keep that permission on the publishing job, not on firmware build jobs.
  All simulator scenarios are listed in
  `sim/scenarios/manifest.json`, including their owner, board matrix,
  capabilities, and expected exit status.
  The Android companion metadata table mirrors shipped firmware settings by
  wire ID. LEGEND wire 65 has values 0 (Buttons), 1 (Bottom), and 2 (Off);
  keep its protocol test aligned when that production enum changes.
  Manual PlatformIO dispatches always run the complete firmware matrix because
  a dispatch has no meaningful comparison base.
  Android's optional `run_emulator` input keeps the default dispatch fast.
- Before pushing config, enum, or settings changes, clean-build every
  documented env, including the non-CI `esp32-s3-headless` env. A new settings
  enum value must be added in five places or the headless build fails
  `-Werror=switch`. See `plans/95-engineering-lessons.md`.
- The direct SDL simulator build writes compiler depfiles beside each object.
  Its incremental cache follows project and dependency headers with `make -q`;
  its stamp also keys the absolute firmware, dependency, and LVGL roots so a
  shared build directory cannot reuse objects from another checkout. Run
  `sh sim/scripts/test-build-deps.sh` when changing this cache logic.
- UI fuzz coverage must run both touch and `FURBLE_SIM_NO_TOUCH=1` layouts on
  all three panel binaries. Preserve the runner's default seeds and strict
  replay check; touch-only runs miss physical input-device lifetime faults.
- UI screenshot CI selects the StickS3 physical-button layout explicitly and
  asserts page identity before every capture. Capture routes are not input tests.
- All OTA application images start at `0x20000`. The shared
  `board_upload.offset_address` setting is intentional: it keeps
  `pio run -t nobuild -t upload` from falling back to PlatformIO's historical
  `0x10000` board default. Use the generated `flash_args` file or the explicit
  `0x20000` app offset when flashing an existing artifact. Never erase NVS or
  bonds just to correct an app offset.
- Board-specific ESP-IDF component dependencies must be gated by a CMake
  profile argument. A compiler define alone is too late for component
  dependency resolution. The Waveshare profile passes `FURBLE_ETHERNET` both
  ways for this reason.
- `tools/flash_prepare.py --preflight-only` performs and reports the PMIC
  console handshake without starting PlatformIO, then cancels the prepare and
  confirms watchdog restoration before returning success. Download recovery
  remains available intentionally after cancel. `--dry-run` remains a
  compatibility alias. If restoration cannot be confirmed, both aliases fail
  with manual recovery guidance. If PlatformIO cannot be started or exits
  unsuccessfully after a successful handshake, the helper also attempts
  `flash cancel` and reports whether watchdog restoration succeeded.

## Style

- Short declarative sentences. Plain English. No em-dashes anywhere: code
  comments, commits, PRs, docs.
- 2-space indent. Match existing naming. clang-format 21 is CI-enforced
  (see `.clang-format`).

## Testing

- Run `python3 tools/pre_review.py --base origin/master` before pushing. It
  runs the diff hygiene, settings contract, and C++ safety scanners, the CI
  workflow and simulator manifest checks, and the scanner unit tests.
- Run the fuzz gate before any hardware image build:
  `python3 tools/check_fuzz.py --full --host-build <host-build> --mqtt-binary
  sim/build-mqtt/furble-sim`. `--smoke` is the pull-request budget. It runs
  seeded UI input on all three panels in both layouts, the WebUI, companion,
  MQTT and OTA fuzzers, the fixed memory-pressure scenarios, and randomized
  heap-pressure boots. The full corpus currently passes 10 targets, 135 seed
  runs and 89,145 iterations with zero failures. The earlier MQTT owner stack
  overflow and Home Assistant discovery OOM are fixed and carry regressions.
  Do not mark a known failure expected-pass; fix it and keep the regression.
- Run the MQTT memory-pressure scenarios with
  `sh sim/scripts/run-memory.sh` after building the simulator with
  `FURBLE_SIM_MQTT=1`. The script uses a closed local port, so no broker is
  required. The default-build WebUI gate and crash-record scenarios run in
  `sim/scripts/run-e2e.sh`.
- Every sdkconfig sets `CONFIG_HEAP_ABORT_WHEN_ALLOCATION_FAILS`, and the
  simulator models it: a failed allocation prints `SIM PANIC`, writes a panic
  record and exits 3. Firmware NULL fallbacks are unreachable on hardware, so
  survival under pressure must come from admission gates. The abort fixtures
  run through `sh sim/scripts/run-panic-fixtures.sh` with the default and
  `FURBLE_SIM_MQTT=1` binaries. The simulator build force-includes the board's
  memory sdkconfig subset generated by `tools/gen_sim_sdkconfig.py`.
- `tools/derive_sim_heap.py` derives each board's boot heap ceiling from its
  `firmware.elf`, sdkconfig and the ESP-IDF region tables into
  `sim/coverage/board_heap.json`; the memory profile seeds each board with it.
  Regenerate it after static RAM or boot allocations change, see
  `docs/sim-memory-profiler.md`.
- Every firmware object is compiled with `-fcallgraph-info=su`, which writes a
  `.ci` call graph next to it and does not change the image.
  `python3 tools/stack_budget.py --env <env>` walks it from each task entry in
  `tools/stack_budget.json` and fails a task whose resolved worst case plus
  the Xtensa port overhead exceeds its stack. Firmware CI runs it after every
  build. Indirect calls are followed only where the manifest resolves them, so
  a task with unresolved sites reports a lower bound; see
  `docs/stack-budget.md`. Update the manifest when a task or its stack changes.
  A recursion or an `alloca`/VLA frame on a task's path fails it until
  `recursion_bounds` or `dynamic_bounds` cites a bound from source.
  `platformio.ini` sets `CJSON_NESTING_LIMIT=8` for every component; the
  cJSON recursion bounds depend on it.
- Hardware flashing is gated on the simulator. The gate manifests are under
  `sim/coverage/`; `python3 tools/check_sim_coverage.py --require-ready`
  validates evidence and `python3 tools/check_flash_readiness.py --ready` is
  the only authority for a flash candidate. It fails on approximate entries,
  pending fuzz results, unfixed seeds, incomplete permutations, memory-budget
  misses, missing memory profile dumps or a dirty tree. Run
  `sim/scripts/check-hardware-quirks.sh`, `sim/scripts/run-memory-profile.py`
  and the full `sim/scripts/run-permutations.py --compact` before asking for
  hardware. On master at `2d2be868` the gate passes: 188 of 188 hardware
  entries, 20 of 20 crash entries, 10 of 10 fuzz targets, 6,912 of 6,912
  permutations, 13 of 13 memory dumps and 18 of 18 linked boundaries. The
  memory dumps are ignored generated evidence, so run
  `python3 sim/scripts/run-memory-profile.py` before the final check. Hardware
  validation is deferred in sim-only mode and #407 has not been
  hardware-retested; see `plans/313-sim-only-status.md`.
- Linked host-boundary evidence is `sh sim/scripts/run-boundary-conformance.sh`.
  It compiles the production headless service loop and the IR, SD, feedback,
  W5500 Ethernet, CompanionGatt and WebUI TLS/socket paths unchanged against
  peripheral doubles. These are the linked paths behind the boundary entries in
  `sim/coverage/hardware_quirks.json`; keep the runner and the manifest tokens
  aligned when changing a driver.
- Run `python3 sim/scripts/run-fuzz-target.py --all` to regenerate
  `sim/coverage/fuzz_results.json`. The readiness gate consumes that file; every
  target must pass and any known failing seed must be fixed with its regression.
  `tools/check_fuzz.py --full` remains the local pre-hardware budget.
- Run `sh tools/run_sanitized_host_tests.sh --clean` when touching C++ state,
  containers, camera drivers, simulator fixtures, or filesystem lifecycle code.
- `.github/workflows/pre-review.yml` runs the scanners on pull requests;
  `.github/workflows/host-sanitized.yml` runs the ASan/UBSan subset on C++
  changes. Both are path-filtered, read-only, and dispatchable for stacked PRs.
- The USB console (debug builds, `FURBLE_CONSOLE`) is the automation surface:
  settings get/set, gps subcommands, shutter, status.
- Hardware verification on the attached M5StickS3 happens before PRs.
- Only Fujifilm cameras are available. Other vendors get code review plus the
  FauxNY test camera and are declared untested in the PR.
- `tools/coverage.py` measures host and simulator line coverage and enforces
  `tests/coverage_floor.json` in CI. See `docs/coverage.md`.
- The built simulator preference ownership gate is
  `sim/scripts/check-doc-tokens.sh` after changing scenario seeds; the existing
`sim-e2e` job runs it with `tools/check_sim_scenarios.py`. The
`sim/scripts/check-preferences-lifecycle.sh` gate; the existing `sim-e2e` job runs
  it against the fresh M5StickS3 binary with a bounded step.
- The host TSAN wrapper fails on any warning or non-zero child status; see
  `CONTRIBUTING.md` for its exact completion marker and compiler-free contract.

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

## Review and merge gate

A PR must have zero unresolved review threads before merge. Record every
Copilot or human finding as fixed, already fixed, or a false positive with
evidence. Resolving a thread without a fix is not acceptable. The same rule
applies to a remediation PR that closes review findings. The online gate is
`python3 tools/check_review_threads.py --repo owner/name --pr N`; it fails
closed on API errors and is intentionally not part of the offline pre-review or
fork CI jobs. Record the audit in `plans/NN-review-feedback.md`. Run the final
gate set before pushing or merging: `python3 tools/pre_review.py --base
origin/master`, `sh sim/scripts/check-doc-tokens.sh`, and the review-thread
check.

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
- LVGL: wide panels need two draw buffers because `displayFlush()` reports the
  flush complete as soon as M5GFX queues the SPI DMA. Narrow panels use one
  buffer and must call `M5.Display.waitDMA()` before `lv_disp_flush_ready()`.
  Tasks that never write NVS may keep their stack in PSRAM; IR is one, the
  WebUI, companion, GPS and NimBLE host are not.
- MQTT: the owner needs 10 KiB. Gate owner creation at 18,432/12,288 internal
  bytes; the client start gate is 8,192/4,096. Plain MQTT uses a 3 KiB worker
  and 1,536-byte buffers; TLS keeps a 6 KiB worker. The 9,392-byte event queue
  lives in PSRAM because it holds only copied topic and payload bytes, never
  flash or NVS pointers. Home Assistant discovery requires 10,240/4,096 after
  the saved camera list loads. `mqtt_ha` is cached on reload; never read it from
  the owner event path. The event handler runs on the 3 KiB esp-mqtt worker:
  never put a 2,348-byte `queued_event_t` on its stack. It fills the owner's
  preallocated `m_Incoming` copy, which lives with the queue and is never freed.
- Main task stack: keep the StickS3 at 12 KiB. The 8 KiB experiment overflowed
  in task main after MQTT was enabled, even though the MQTT-off high-water was
  only 3,476 bytes.
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

## Dependency updates

Dependabot checks GitHub Actions, Python tooling, Android Gradle dependencies, and the Apple Swift package weekly. Routine minor and patch changes are grouped for review. Firmware pins in PlatformIO and ESP-IDF component manifests remain manual; Dependabot does not support those formats. Updates are proposed through pull requests and do not deploy or flash hardware. Pin Git-sourced PlatformIO dependencies to the full 40-character commit SHA so the VSCode extension can fetch them (upstream issue 309).

Static analysis: `python3 tools/check_static_analysis.py --base origin/master` with the host and simulator compile databases; see `CONTRIBUTING.md`.
