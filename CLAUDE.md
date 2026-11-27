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
  to keep dual-OTA slots while retaining debug symbols, the assert() check and
  abort, the full `ESP_LOG_VERBOSE` log maximum and the `sdkconfig.debug`
  diagnostics; stepping is less exact. The assertions are
  `CONFIG_COMPILER_OPTIMIZATION_ASSERTIONS_SILENT`, so the check and the abort
  stay and only the message strings go. `-DLOG_LOCAL_LEVEL=ESP_LOG_VERBOSE`
  raises the compiled log maximum of every translation unit in the image, not
  just furble's, so it overrides the image-wide `CONFIG_LOG_MAXIMUM_LEVEL=3`
  cap from plan 106 group A and every component's DEBUG and VERBOSE format
  strings are linked. `m5stack-core-debug` pays for them with the BMI270 init
  blob and driver it no longer links, 8,633 to 9,120 bytes depending on the
  build (plans/347), and it is the
  one debug env that omits the stack protector.
- `m5stack-core-usb-debug` is a developer-only USB-UART fallback with one
  factory app partition. It is not part of the six-board release or web
  installer matrices and has no wireless update or rollback path. Normal CI
  builds two mandatory, strict Core debug profiles: this one and the dual-OTA
  `m5stack-core-debug`, so an image that outgrows its OTA slot fails CI.
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
  flags, so it was reverted. Release optimization, security features and the
  OTA layout were unchanged. The flash slimming below (#435) brought the image
  back under the slot.
- Flash slimming (plan 314) keeps every feature and IPv6. All boards use ESP-IDF
  Log V2 with the DHCP server and non-AES GCM off. StickC images compile out
  the SD stack and the unused Montserrat 22. Images that map to one M5GFX board
  (`m5stick-c`, `m5stack-core`, `m5stick-s3`) set `M5GFX_BOARD`, and
  `post:patches/m5gfx_board.py` pins it in `M5GFX::autodetect`, so the other
  boards' panel and touch drivers drop out. StickC Plus/Plus2 and Core2/Tough
  share an image and keep autodetect. Waveshare pins `board_M5StampS3`, which
  has no M5GFX arm, so the patch returns it from the ESP32-S3 section before
  the first probe and nothing drives the W5500 pins; the assignment alone was
  not enough, see plans/352-m5gfx-board-pin.md. `tools/check_m5gfx_pinning.py`
  fails a branch where a pin stops taking effect. Core debug fits its OTA slot
  again and is a mandatory CI lane; with the u-blox driver (plan 321) and the
  esp_err codes in error logs (plan 106) it is 1,740,400 bytes, 400 under the
  slot. Log V2 writes
  one record as three vprintf calls, so `FurbleCrashLog` joins fragments up to
  the newline.
- Per-env `sdkconfig.<env>` files are committed at the repo root. Debug envs
  share the release sdkconfig via `board_build.esp-idf.sdkconfig_path`.
  Regenerating builds may append derived symbols to sdkconfig files. Commit
  those changes consistently across all six release files, never for just one
  env. Regeneration also strips comments and rewrites deprecated aliases to
  match their real options, so keep the reason for a setting in its plan doc,
  never in an sdkconfig comment.
- Debug hardening (plan 314 PR 13): `sdkconfig.debug` turns on the
  end-of-stack watchpoint and standalone heap tracing (`heap trace` console
  command). Every debug env except `m5stack-core-debug` also merges
  `sdkconfig.stack-protector` (`COMPILER_STACK_CHECK_MODE_NORM`); its 24 KB
  does not fit that OTA slot. Debug builds
  define `FURBLE_TASK_WDT`, which subscribes the MQTT, WebUI and companion
  loops to the 5 s task watchdog through `FurbleTaskWatchdog.h`; a timeout
  logs a backtrace but `CONFIG_ESP_TASK_WDT_PANIC` stays off until a hardware
  soak. The 8 and 16 MB tables end with a 256 KiB `coredump` partition after
  `ota_1`, and their debug envs merge `sdkconfig.coredump-flash`.
  `m5stack-core-usb-debug` layers both under its partition fragment.
- S3 and Core2 release sdkconfigs disable `CONFIG_SPIRAM_MEMTEST` to remove the
  full boot-time PSRAM test. Their debug envs merge `sdkconfig.spiram-memtest` back
  in. `patches/debug_sdkconfig.py` accepts comma-separated fragments so a
  diagnostic env can layer one on `sdkconfig.debug`.
  `waveshare-s3-eth-debug` also merges `sdkconfig.full-diagnostics`, which
  restores full assertions, the VERBOSE log maximum and esp_err names over
  the plan 106 group A release values.
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
  Every pull request workflow groups runs on
  `${{ github.workflow }}-${{ github.ref }}` and sets `cancel-in-progress` to
  `${{ github.event_name == 'pull_request' }}`, so a newer head stops the
  superseded run while push to master, release tags and dispatch finish. The ref
  of a stacked PR is its own merge ref, so a stack and its base never share a
  group. See [plans/392-ci-concurrency.md](plans/392-ci-concurrency.md).
- The simulator power gate compares each report against its baseline with the
  two-sided `tools/power-model/compare.py --threshold` policy. The default 10%
  value is compatibility policy, not calibrated hardware noise. It also fails
  timer-fire and invalidated or flushed pixel drift beyond
  `--activity-threshold` (default 25% plus an absolute slack); per-scenario
  opt-outs with a reason live in `tools/power-model/activity-optout.json`.
  Focused comparator coverage is `python3 -m unittest tests/test_power_compare.py`.
  `tools/power-model/README.md` distinguishes the legacy-default report from
  the opt-in synthetic microsecond accounting mode; neither is a hardware
  current measurement.
- The Apple workflow packages the unsigned macOS Debug app only after its tests
  pass. The uploaded artifact includes a checksum and source/Xcode provenance;
  it is for companion testing, not signing or release distribution.
- `companion/apple` keeps one UI snapshot in the existing `swift test` target:
  the shipped Status section body (`CompanionStatusView`) rendered through
  `ImageRenderer` and compared to
  `companion/apple/Tests/Snapshots/CompanionStatusView.1x.png`. It is macOS
  only, because the iOS bundle compiles it out. Regenerate after an intended
  visual change with `FURBLE_APPLE_SNAPSHOT_UPDATE=1 swift test --filter
  CompanionStatusViewSnapshotTests`, review the PNG, and commit it. The flag
  refuses to run when `CI` is set. A passing run prints the baseline path and
  the measured pixel deltas, so the CI log states the comparison it made.
  `CompanionStatusListRowTests` covers the `List` flattening the pixels cannot:
  it counts the rows SwiftUI builds for the same section. See
  `plans/343-apple-ui-snapshot.md`.
- Release tags beginning with `companion-test-` are reserved for companion
  testing and skip the firmware release workflow; other release tags are
  unchanged. Release and Pages matrices omit the Core single-factory debug
  profile because it has no OTA data partition. The release artifact job has
  scoped `contents: write` permission;
  keep that permission on the publishing job, not on firmware build jobs.
  All simulator scenarios are listed in
  `sim/scenarios/manifest.json`, including their owner, board matrix,
  capabilities, expected exit status and, when the assertions need the optional
  native MQTT transport, the transport they run on.
  The Android companion metadata table mirrors shipped firmware settings by
  wire ID. LEGEND wire 65 has values 0 (Buttons), 1 (Bottom), and 2 (Off);
  keep its protocol test aligned when that production enum changes.
  Manual PlatformIO dispatches always run the complete firmware matrix because
  a dispatch has no meaningful comparison base.
  Android's optional `run_emulator` input keeps the default dispatch fast.
- Before pushing config, enum, or settings changes, clean-build every
  documented env, including `esp32-s3-headless`, which only the reproducible
  build workflow compiles, not the PR firmware matrix. A new settings
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
  runs the diff hygiene, settings contract, and C++ safety scanners,
  clang-format 21 on the changed C and C++ files, the CI workflow and
  simulator manifest checks, the coupling ratchet, the datasheet citation gate
  (`tools/check_datasheet_citations.py`), the documentation citation gate
  (`tools/check_doc_citations.py`), and the scanner unit tests. The ratchet
  fails when a per-file count of board
  guards, `M5.` calls, `M5.getBoard()` or ESP/NimBLE public header types
  grows; after a reduction run `python3 tools/check_coupling_ratchet.py
  --update` and commit `tools/coupling_ratchet.json`. pre_review passes its
  `--base` through, so growth over the base manifest fails even after
  `--update`. Board guards count each board macro on a conditional line and
  each `#define` naming one. The capability scan also covers `sim/` and
  `tests/host/`, and rejects a `FURBLE_HAS_*` or `FURBLE_PANEL_*` name that
  `include/FurbleBoardCaps.h` does not define.
  `git config core.hooksPath .githooks` runs it from `.githooks/pre-push` on
  every push, against each pushed commit. The tracked `.claude/settings.json`
  sets that hook path for Claude Code sessions through `GIT_CONFIG_COUNT`, so
  a failing gate blocks an agent's push. `git push --no-verify` skips one push.
- `python3 tools/check_doc_citations.py` checks every `file:line` citation in
  every non-ignored Markdown file: the path exists, the line is inside the file,
  and a symbol the citation annotates sits on that line or inside its definition.
  It reads a symbol only from the code span the citation annotates, so a wrong
  line inside the same file passes when the sentence names no symbol; see
  `CONTRIBUTING.md` for the full list of what it cannot see. Citations into an
  unvendored tree go in `tools/doc_citations.json` with a reason, and an
  intentionally approximate citation goes in its `allow` list, which fails when
  an entry suppresses nothing. pre_review runs the full sweep; `--base REV`
  narrows it to the citations a range could break.
- Run the fuzz gate before any hardware image build:
  `python3 tools/check_fuzz.py --full --host-build <host-build> --mqtt-binary
  sim/build-mqtt/furble-sim`. `--smoke` is the pull-request budget. It runs
  seeded UI input on all three panels in both layouts, the WebUI, companion,
  MQTT and OTA fuzzers, the fixed memory-pressure scenarios, and randomized
  heap-pressure boots. The committed artifact in
  `sim/coverage/fuzz_results.json` passes 11 targets, 145 seed runs and 124,148
  iterations with zero failures. The earlier MQTT owner stack
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
  survival under pressure must come from admission gates. A new runtime path
  that creates a task or retained structure needs one; the WebUI identity task
  and the companion first enable are the latest (plan 314 PR 7). The abort
  fixtures run through `sh sim/scripts/run-panic-fixtures.sh` with the default and
  `FURBLE_SIM_MQTT=1` binaries. The simulator build force-includes the board's
  memory sdkconfig subset generated by `tools/gen_sim_sdkconfig.py`.
- `tools/derive_sim_heap.py` derives each board's boot heap ceiling from its
  `firmware.elf`, sdkconfig and the ESP-IDF region tables into
  `sim/coverage/board_heap.json`; the memory profile seeds each board with it.
  Regenerate it after static RAM or boot allocations change, see
  `docs/sim-memory-profiler.md`.
- `python3 tools/check_size_budget.py --env <env>` fails a build whose
  `firmware.bin` outgrows its app slot or whose static DRAM
  (`.dram0.data + .noinit + .dram0.bss`) or IRAM grows past
  `tools/size_budget.json`. Firmware CI runs it after the stack budget, and
  the reproducible workflow runs it on `esp32-s3-headless`. A RAM budget is
  the measured size plus at least 512 bytes of headroom, rounded up to a KiB;
  a change that crosses one raises it with `--write` and states the reason in
  its PR.
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
  The walk collapses strongly connected components and takes the longest
  path over the DAG, exact outside known limits. Panic paths count: the
  ESP-IDF panic handler runs on the faulting task's stack, so `panic_abort`
  is charged the handler's depth. `infeasible_calls` drops an edge only with
  a file:line citation of the guard that makes it unreachable. A `-debug` env
  is checked against its own stacks: `sdkconfig.debug` sizes `sys_evt` and
  `esp_timer`, and each furble task owner adds a named `..._DEBUG_EXTRA`
  under `FURBLE_DEBUG_BUILD`, which only the `-debug` envs define. The
  manifest's `debug_stack_bytes` records the result.
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
  hardware. The committed manifests hold 198 modeled hardware entries, 20 crash
  entries, 11 fuzz targets, 6,912 permutations and 13 memory profile scenarios.
  `sh sim/scripts/run-boundary-conformance.sh` runs the 29 linked boundary ctest
  cases registered in `tests/host/CMakeLists.txt`. The
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
  It runs every host test under ASan and UBSan except the two ThreadSanitizer
  probes, which the normal host build runs through `run_tsan_race.sh`.
  The fuzz targets refuse to configure without ASan/UBSan unless
  `-DFURBLE_FUZZ_ALLOW_UNSANITIZED=ON`, and `tools/check_fuzz.py` rejects a
  host build made that way.
- `.github/workflows/pre-review.yml` runs the scanners on pull requests;
  `.github/workflows/host-sanitized.yml` runs the sanitized host tests on C++
  changes. Both are path-filtered, read-only, and dispatchable for stacked PRs.
  The `sim-sanitizers` job in `sim-e2e.yml` runs the certified e2e suite under
  ASan, UBSan and LeakSanitizer on Linux. Only the third-party sites in
  `sim/ubsan.supp` and `sim/lsan.supp` are suppressed, each with its reason;
  never add a furble frame there. On macOS a sanitized sim needs the SDL3
  rpath `sim/build.sh` adds; check `otool -l` before running one, because a
  missing rpath raises a desktop dialog per process. The sim-e2e S3 job runs
  `sim/scripts/run-env-order.sh`, which fails any setenv that adds a variable
  while SDL is live (issue #283).
- Machine-checked companion claims live in `docs/companion-test-coverage.md`:
  the per-app layer matrix, the CI job for each layer, and the flows no test
  reaches. State a new companion test there and in the matching README in the
  same PR. The Android module runs 40 tests inside one `./gradlew test` step;
  `FurbleGoldenCorpusTest` reads `tests/protocol/golden/`, so regenerate it with
  `make -C tests/protocol generate` and commit the fixtures with a firmware
  packet change. The generated test results are printed by the Gradle test task
  (`testLogging`), so a missing `matches ... with N differing pixels` line in an
  Android run means the step did not run.
- The USB console (debug builds, `FURBLE_CONSOLE`) is the automation surface:
  settings get/set, gps subcommands, shutter, status.
- Hardware verification on the attached M5StickS3 happens before PRs.
- Only Fujifilm cameras are available. Other vendors get code review plus the
  FauxNY test camera and are declared untested in the PR.
- `tools/coverage.py` measures host and simulator line coverage and enforces
  `tests/coverage_floor.json` in CI. See `docs/coverage.md`. It needs the
  compiler-rt profile runtime; on Ubuntu install `libclang-rt-18-dev`
  explicitly, because `clang-18` only recommends it.
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
| A chip, its register use, a bus address, a pin or a timing constant | the matching page under `docs/components/`, and `docs/components/board-map.md` when a pin, address or bus changes |
| A protocol frame, message, key, topic or characteristic | the matching page under `docs/protocols/`, then `docs/protocols/README.md` when a protocol is added or removed |
| A new `docs/` page, or a page moved or renamed | `docs/README.md` and the `README.md` of its directory, so the index stays complete |
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
fork CI jobs. It also fails when the PR head has no check runs, or any check
is still running or did not pass, so merging without CI is never silent. Actions
was off from 2026-10-21 to 2026-11-13 and PRs merged with no CI; see
`plans/95-engineering-lessons.md`. A PR whose paths no workflow covers (web-installer only, say) passes the
no-checks rule only with `--allow-no-checks "<reason>"`, which is printed.
That rule decides from the workflow files at the pull request's base commit,
read at `baseRefOid` through `gh api repos/{owner}/{name}/contents/.github/workflows?ref=<oid>`,
and never from the checkout. A clone can be stale against the base, so a local
tree cannot show what CI should have run. A pull request that edits
`.github/workflows/` never qualifies for the override: GitHub evaluates the
pull request's own copy of a workflow for its pull_request run, so the edit
changes what CI runs and the base filters cannot see a broadened filter. The
gate fails closed, with exit 2, when the base oid is
missing, when the API errors, and when a base workflow cannot be fetched or
parsed. The checkout-reading local mode is a test-only fallback: `--pr` is
mandatory, so the gate never reaches it. See `plans/implemented/331-merge-gate-base.md`.
Record the audit in `plans/NN-review-feedback.md`. Run the final
gate set before pushing or merging: `python3 tools/pre_review.py --base
origin/master`, `sh sim/scripts/check-doc-tokens.sh`, and the review-thread
check.

## Hardware traps (hard-won, do not relearn)

- DFS clock family: with BT modem sleep enabled, esp_pm DFS actually engages,
  and any peripheral clocked from APB breaks subtly. The GPS UART takes
  `UART_SCLK_XTAL` on the S3, which has no REF_TICK source, and
  `UART_SCLK_REF_TICK` on the classic ESP32, which has REF_TICK at a fixed
  1 MHz and no XTAL UART source
  (`components/soc/esp32s3/include/soc/clk_tree_defs.h:264`,
  `components/soc/esp32/include/soc/clk_tree_defs.h:231`). The S3 selects one
  of three UART Core sources, APB_CLK, RC_FAST_CLK and XTAL_CLK, through
  `UART_SCLK_SEL` (ESP32-S3 TRM v1.8, 26.3 and 26.4.1 p. 926, field values
  p. 961). The ESP32 has only APB_CLK and REF_TICK, and REF_TICK is derived
  from APB_CLK with a per-source divider (ESP32 TRM v5.9, 19.3.2 p. 312 and
  7.2.4.2 with Table 7.2-5 p. 169), so `UART_SCLK_REF_TICK` is not an escape
  from the clock family there; ESP-IDF reprograms that divider on every CPU
  clock source change
  (`components/esp_hw_support/port/esp32/rtc_clk.c`, `clk_ll_ref_tick_set_divider`
  for the XTAL, RC_FAST and PLL sources). The display holds
  `ESP_PM_APB_FREQ_MAX` while the backlight is on because the LEDC divisor is
  fixed at config time; visible flicker without the lock is unmeasured. RMT is
  safe on two furble facts, not on a driver APB lock: the driver takes
  `ESP_PM_CPU_FREQ_MAX` without DMA and `ESP_PM_NO_LIGHT_SLEEP` with it, never
  `ESP_PM_APB_FREQ_MAX` (`components/esp_driver_rmt/src/rmt_common.c:230`).
  `src/FurbleIR.cpp` never sets `with_dma`, and furble raises the PM CPU
  maximum itself, 160 MHz by default
  (`include/FurblePlatform.h:25-28`, `src/FurblePlatform.cpp:84`), where APB is
  80 MHz. The sdkconfig boot default is not what pins it. Setting `with_dma` on
  the S3 would swap that lock for `ESP_PM_NO_LIGHT_SLEEP` and let the 38 kHz
  carrier drift. When touching power management, audit every peripheral clock
  source.
- Display sleep: the display releases `ESP_PM_APB_FREQ_MAX` while the panel
  sleeps and must reacquire it before `M5.Display.wakeup()`
  (`src/FurbleUI.cpp:1879`, `src/FurbleUI.cpp:1900`). The dwell rule is per
  panel. ILI9342C needs 120 ms in both directions, Sleep Out to the next Sleep
  In and Sleep In to the next Sleep Out (V101 sections 8.2.11 and 8.2.12,
  pages 100 and 101), and the ILI9341 copy carries the same pair. ST7789V
  sections 9.1.11 and 9.1.12 and ST7796S V1.0 sections 9.2.12 and 9.2.13 need
  120 ms from Sleep Out to the next Sleep In only, plus 5 ms after SLPIN and
  after SLPOUT before any other command. ILI9342C is the panel on
  `m5stack-core`, `m5stack-core2` and `m5stack-core-usb-debug`, so the reverse
  wait is required on three of the six display boards and the code keeps it.
  M5GFX 0.2.27 sends the bare command and no delay
  (`src/lgfx/v1/panel/Panel_LCD.cpp:101-107`,
  `src/lgfx/v1/LGFXBase.hpp:1436-1437`), so both 120 ms waits are furble's:
  `UI::sleepDisplay()` refuses a sleep within 120 ms of the last wake
  (`src/FurbleUI.cpp:1847-1891`) and `UI::wakeDisplay()` waits the same interval
  before SLPOUT (`src/FurbleUI.cpp:1894-1897`). Together they satisfy all four
  panels. The 5 ms needs no separate delay: after SLPIN the next panel command
  is the SLPOUT that the wake gate holds back 120 ms, and after SLPOUT the wake
  path writes only the brightness, which is the backlight, not a panel command
  (`src/FurbleUI.cpp:1901`, `:1905`).
- Never hold the Control mutex across a delay. The reconnect cancel deadlock
  bricked the device (buttons and USB dead) because furble disables all M5PM1
  power button gestures at boot. A retained `DL_LOCK` is cleared only by true
  PMIC power loss, not USB unplugging or reset. Restore battery power, hold the
  side button until the green LED flashes, then reflash.
- M5PM1 (StickS3 PMIC): the first I2C transaction after its idle sleep fails
  and only wakes it. Always retry once, which is what `m5pm1Access()` does
  (`include/FurblePlatform.h`). The manual states the rule in `I2C_CFG` note 2,
  page 15, and the M5StickS3 low-power page repeats it. Idle sleep is off in the
  current library path (`SLP_TO = 0`) and is invalid while PWM is enabled or
  while entering Download mode (manual, pages 27 and 28). The status LED stays
  lit at display off unless you clear it: `setLedEnLevel(false)` saves PWR_CFG
  bit 4.
- M5PM1 IRQ status is two latches, not one. The chip manual says a GPIO
  configured as IRQ is held low until the IRQ Status is cleared, and IRQ
  Status 2 (0x41) carries 5VIN insert and remove, which furble does not
  consume. Clearing only Status 1 (0x40) left PYG1_IRQ asserted on GPIO13 for
  the rest of the session after a USB insert or removal, so `armMotionWake()`
  armed a level wake on a line that never releases. Both registers go through
  `Platform::clearPmicIrqLatches()`; do not swap that for a mask write, because
  the clear is the release the manual documents and mask semantics are not
  (plans/363).
- The StickS3 speaker amplifier needs about 1 s after `M5.Speaker.begin()`
  before the first tone, and its volume must stay at or below 75 percent
  (`FeedbackPolicy::MAX_BATTERY_VOLUME` = 191) while the device runs on the
  battery. The settle belongs to the amplifier enable, so the feedback session
  outlives its last tone by an idle window instead of being power-cycled per
  pattern; wait the settle on the feedback timer, never delay the caller's task
  and never rewrite the stored volume. Only a positive VBUS reading counts as
  mains power (plans/363).
- M5Unified instantiates an RTC only when its PCF8563 probe at 0x51 is
  acknowledged, so `M5.Rtc.isEnabled()` is false on every board except the
  Plus2 and the AXP192's internal RTC is unreachable. Guard RTC writes with
  `Board::profile().rtcOnBus`, not with the timed-wake kind.
- S3 native USB Serial/JTAG resets the chip on each host port open: the host
  sets RTS 1 with DTR 0 (ESP32-S3 TRM v1.8, Table 33.3-2 p. 1248), and SRST
  resets the digital system but not the USB Serial/JTAG controller, so the port
  survives (33.3.5 p. 1249). The module needs the PLL and APB_CLK at
  40 MHz or more, is gated in light sleep and is powered down in deep sleep
  (33.4.2 p. 1253). Use that port-open as the safe state transition, never a
  disconnect then reconnect. esptool 4.11.0 prints `FAILED` only from the flash
  verify check (`esptool/cmds.py:1382`, `:1389`), so a `FAILED` line means the
  image did not verify. The reset phase prints `Hard resetting via RTS pin...`
  (`esptool/loader.py:1711`). On the USB Serial/JTAG part the port is gone by
  then, so the RTS change raises a port error. A macOS bench run saw errno 6,
  ENXIO; other hosts report their own code. `esptool/reset.py:51` ignores only
  ENOTTY and EINVAL, so the error is retried and then re-raised (`:43-62`). The
  hash has verified by then, so the traceback is benign even though the upload
  command exits non-zero.
- GPS unit v1.1 (AT6668): no backup supply, so a rail cut costs a ~108 s cold
  start. `$PCAS12` timed standby works. `$PCAS02` only accepts 100-1000 ms.
  Sub-second rates need `$PCAS03` sentence pruning first.
- LVGL: guard every periodic `lv_image_set_src` / `lv_label_set_text` behind a
  changed check. Unconditional setters invalidate and redraw. Icons are
  compressed and cost a 12.3 KB decompress per 64x64 draw unless the image
  cache covers them. `CONFIG_LV_USE_REFR_DEBUG` plus the console-gated
  invalidation logger are the diagnosis tools.
- PSRAM (S3 and Core2): `SPIRAM_MALLOC_ALWAYSINTERNAL=4096` routes large allocations to
  PSRAM. DMA display buffers must stay `MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL`
  (`src/FurbleUI.cpp:581-583`). That is a source requirement. The panel
  corruption a wrong caps set would cause is unmeasured, and on the S3
  `SOC_PSRAM_DMA_CAPABLE=1`
  (`components/soc/esp32s3/include/soc/soc_caps.h:57`), so PSRAM is not
  categorically unusable for DMA there.
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
  preallocated `m_Incoming` copy, and the owner receives into `m_Received`;
  both live with the queue and are never freed.
- MQTT retries forever. A retryable failure (transport error, broker
  unavailable, a refused admission gate) doubles the gap from 10 s to a 60 s
  cap (`RETRY_INITIAL_MS`, `RETRY_MAX_MS`) and schedules the next start; it
  never blocks. Only a classified credentials, protocol or client-id rejection
  sets `m_Blocked`, and `mqtt connect` clears it. A retry destroys and
  recreates the esp-mqtt client only, so every attempt passes the heap gates
  again; the owner task, the event queue and the two event copies survive, and
  a live connection resets the policy. Do not reintroduce a failure count that
  stops the service: a broker outage longer than about a minute must not
  silence the hub until a reboot.
- Main task stack: keep the StickS3 at 12 KiB. The 8 KiB experiment overflowed
  in task main after MQTT was enabled. With MQTT connected `perf tasks` left
  3,020 of 12,288 bytes free on main, so it used about 9,268 bytes.
- `perf tasks` reports free bytes: `stack_high_watermark` is
  `uxTaskGetStackHighWaterMark`, the minimum free stack, never bytes used.
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
- `lib/hal/`: pure, host-tested chip drivers (AXP2101, BQ27220, XL9555,
  SX1262 sleep) behind a register-bus seam. See plans/322. The SX1262 timing is
  document-only: NRESET low typically 100 us (DS.SX1261-2.W.APP Rev 2.2, 8.1
  p. 53), BUSY 600 ns maximum from the NSS rising edge and held high through
  20 kOhm in sleep (8.3.1 p. 55), and the switching table 8-2 p. 56 gives the
  STBY_RC warm start at 340 us and the cold start at 3500 us.
- `components/icons/`: generated LVGL image arrays.
- `sim/`: host SDL simulator for the UI (shims, fakes, script driver). It
  never modifies firmware sources.
- `web-installer/`: ESP Web Tools manifest generation.
- `.github/workflows/pages.yml`: tag-triggered GitHub Pages installer builds.
- `plans/`: numbered improvement plans, one per PR (on the plans branch until
  integration lands).

## Dependency updates

Dependabot checks GitHub Actions, Python tooling, Android Gradle dependencies, and the Apple Swift package weekly. Routine minor and patch changes are grouped for review. Firmware pins in PlatformIO and ESP-IDF component manifests remain manual; Dependabot does not support those formats. Updates are proposed through pull requests and do not deploy or flash hardware. Pin Git-sourced PlatformIO dependencies to the full 40-character commit SHA so the VSCode extension can fetch them (upstream issue 309).

Static analysis: `python3 tools/check_static_analysis.py --base origin/master` with the host, simulator and firmware compile databases (`tools/firmware_compile_db.py --env m5stick-s3` and `m5stick-c` after `pio run -t compiledb`); see `CONTRIBUTING.md`. A source no database compiles fails the gate. The firmware databases add `-nostdlibinc` so clang never reaches the host's `/usr/include`, and the gate anchors clang-tidy's header filter to the checkout so vendored `.pio/libdeps` and `sim/.pio` headers stay out (#463). Raw C++ `new` and `delete` fail `cppcoreguidelines-owning-memory`: use `std::unique_ptr`, and hand a queue its pointer with `release()` and `reset()` on a failed send.
