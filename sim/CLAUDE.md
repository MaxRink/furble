# sim/ (host SDL simulator)

Host build of the furble UI over M5GFX/M5Unified SDL. Developer tool only.
Simulator-only production policy is forbidden. Narrow `FURBLE_SIM` guards in
shipping sources are allowed only for observability, deterministic navigation,
or orderly host exit, and firmware builds must compile the unchanged production
path. The current build still contains simulator substitutes for Control,
Camera, CameraList, and Scan, so it is not yet a hardware-identical build of the
production connection stack. The target architecture is to remove those policy
substitutes and run the production sources against a MockNimBLE boundary and
calibrated virtual peers. Current shared-source seams include
`UI::simulatorHome`, `UI::simulatorBack`, and the orderly exit check in
`UI::task`; the seam inventory below is authoritative.

The complete build, panel, scenario DSL, query, fault-injection, and
scenario-authoring reference is [docs/sim.md](../docs/sim.md). Keep this file
as the directory-local contract and keep the reference synchronized with the
tokens in `sim/driver.cpp`, `src/FurbleUI.cpp`, and the host fault harness.

## Parity inventory and seam rules

The simulator shares substantial production UI, GPS, settings, and power
policy, but the connection path is currently a fake and the host scheduler is
not yet equivalent to FreeRTOS. The following is the current seam inventory
and target boundary (a new seam needs a contract test and an entry here):

| Area | Shared production path | Narrow simulator seam and reason |
| --- | --- | --- |
| BLE discovery | `UI::startScan` and UI-side result materialization are shared; production `Scan` and `CameraList` are not in the SDL build | `sim/shim/Scan.h`, `sim/ScanSim.cpp`, and `sim/CameraListSim.cpp` replace scan lifecycle, radio events, matching, and list persistence. The worker publishes immutable fake events and never touches LVGL. `tests/host` separately compiles production `lib/furble/Scan.cpp` against fake NimBLE. |
| Display | `UI::setDisplayMode`, `wakeDisplay`, `sleepDisplay`, `displayFlush`, LVGL timers and task loop | M5GFX SDL is the panel/pixel sink. Display mode and flush accounting remain production methods; there is no simulator-only rotation or display-state implementation. |
| Input/navigation | LVGL event callbacks and menu handlers | `simulatorHome`, `simulatorBack`, and `simScenarioAction` are script entry points. `driverTick` runs in the UI task's locked phase, so actions and physical-input shims share LVGL ownership; direct page/focus selection is limited to deterministic setup or input timing SDL cannot reproduce. |
| Camera links | UI connection handlers are shared; production `Control`, `Camera`, and `CameraList` are not yet in the SDL build | `sim/FurbleControlSim.cpp`, the fake camera/list, and `sim/ScanSim.cpp` provide the current deterministic peer and link-drop hooks. They are temporary policy substitutes. The target is production `Control`/`Camera`/`CameraList`/`Scan` over MockNimBLE and virtual peers; `action link-lies-kill` remains an interim fault injection until that boundary exists. |
| GPS/UART | Production parser, configuration, retry and power-lock logic | Fake UART/receiver is the lowest host-device boundary; replies and faults are injected as bytes/events on a worker thread. |
| Power/display hardware | Production policy and lock ownership | M5PM1, ESP-IDF power, timer, random, NVS, sleep, flash and system calls are host implementations. Observable state is exposed through `platform_state` rather than replacing policy code. |
| Optional hardware | Production capability checks and menu paths | IR, feedback and SD shims report an env-selected capability because no host GPIO/SD/audio device exists. They do not bypass UI or persistence handlers. |
| Build-time observations | Production behavior is unchanged | `FURBLE_SIM` adds profiler counters, query-only state, the UI-task switch registry, click-streak input injection, the scan-start probe, and the post-`lv_task_handler` `fuzzCycleComplete` seam. The dependency-free `fuzz_machine` owns fuzzer phase/cadence state and counters; these are observability/input seams, not alternate policy. Plan 158 now covers retained task lifecycle records and delete-other quiescence. The cooperative simulator unwind is safer than abrupt cleanup but is not FreeRTOS cleanup parity; production Companion remains blocked pending worker-owned shutdown. Priority, preemption, same-tick dispatch, queue ownership, and CPU-time gaps remain. |

### Hardware identity status

The acceptance target is 100 percent identical observable behavior for every
firmware state that can be represented on the host. That requires the same
production state machines, event ordering, timeout semantics, cancellation and
ownership rules, boot order, board configuration, and power policy. A passing
scenario against a fake Control implementation is not evidence of that target.

Plan 158 first makes host deadlines and teardown orderly: one virtual clock for
FreeRTOS delays, queue deadlines, and esp_timer callbacks, joinable tasks,
explicit stop and wake, and no process termination from worker threads. Timer
callbacks use one serialized dispatch boundary and enforce active-state API
errors. Equal timer deadlines preserve arm order, and a callback can cancel a
second due timer or delete itself. On exit, companion-rig socket workers join
first, then tasks and the timer dispatcher join, and only then may the rig
service owning timer callback arguments be destroyed. Same-tick task ordering and FreeRTOS
priority/preemption are not yet deterministic and must not be described as
parity-complete.
The next vertical slice replaces the connection fakes with production sources
and MockNimBLE peers. Peripheral models and current tables then require board
calibration and differential traces against hardware. Physical radio timing,
analog current, sensor noise, and unavailable peripherals are irreducible
boundaries; each must be measured, bounded, and an explicit release gate, not
silently treated as identical.

Plan 159 defines camera peer certification. Virtual peers may import pinned
behavior from common GitHub implementations and official documentation, but
only exact capture-backed model and firmware fields can produce a certified
result. Inferred, synthetic, missing, or conflicting behavior must return
`UNCERTIFIED` rather than a plausible compatibility pass.

`FURBLE_SIM` conditionals in shared sources are audited at each release:
`FurbleBootScreen.cpp` (wall-clock boot padding), `FurbleControl.h` and
`FurbleControlSim.cpp` (link-drop injection), `FurbleGPS.cpp` (state/timer
profiling), `FurblePlatform.h` (watchdog query friend), `FurblePower.cpp`
(power-lock owner assertions), and `FurbleUI.cpp`/`FurbleUI.h` (profiling,
queries, deterministic input, scan-start probe, UI-task scan materialization,
disconnect count, and scripted actions). `FurbleUIAudit` is enabled for the
simulator and console alike. `FURBLE_SIM_*` environment variables select only
host capabilities, captures, preferences, themes, text size, sanitizers, and
build output; they are not firmware settings.

The DSL verbs are implemented in `sim/driver.cpp`: `wait`/`stall`, key/button
input, capture, UART faults, GPS restart, home/back, actions, queries/asserts,
and exit. Every action must either dispatch a real LVGL/control handler or be
listed above as a lowest-level hardware/event injection. In particular, scan
delivery is asynchronous and drained by the UI task, while display mode uses
the real `UI::setDisplayMode` path. The distinct-row and watchdog scenarios
are parity contract tests; a same-tick fake or worker-side `CameraList` edit is
a regression.

## Build entry points

- `sim/build.sh`: the verified direct-clang path on macOS. Run
  `python3 tools/gen_lv_conf.py sdkconfig.m5stick-s3 sim/lv_conf.h` first if
  the sdkconfig changed. Each object has a compiler-generated `.d` depfile, so
  project-header edits rebuild only their dependents; `make -q` evaluates the
  depfile and the old source-only timestamp shortcut is not used.
- `sim/scripts/test-build-deps.sh`: builds a complete simulator, touches
  `include/FurbleGPS.h`, and proves GPS dependents rebuild while an unrelated
  source stays cached. It requires the same dependency overrides as
  `sim/build.sh`.
- `sim/CMakeLists.txt`: the CMake path for machines with CMake installed.
- `sim/platformio.ini`: planned `platform = native` environment for networked
  developer machines.
- The three modeled panels are the 80x160 `FURBLE_M5STICKC` /
  `board_M5StickC`, the 135x240 `FURBLE_M5STICKS3` /
  `board_M5StickS3`, and the 320x240 `FURBLE_M5COREX` / `board_M5Stack`.
- Keep the firmware source list in `sim/build.sh` and `sim/CMakeLists.txt` in
  sync. Both carry a note.
- Console-only firmware modules that have no simulator behavior still get a
  no-capability shadow in `sim/shim`, such as `FurbleBtDebug.h`.

## Dependency resolution

- M5GFX, M5Unified, and TinyGPSPlus come from the repo-local PlatformIO cache
  `.pio/libdeps/m5stick-s3` (populate it with a firmware build, override with
  `FURBLE_DEP_ROOT`).
- LVGL comes from `managed_components/lvgl__lvgl`, which tracks the version
  pinned by `src/idf_component.yml` (override with `FURBLE_LVGL_DIR`).
- SDL2 comes from Homebrew or /usr/local.

## Determinism caveats

- The virtual clock makes scripted runs reproducible: two smoke runs produce
  byte-identical PNGs.
- Exception: `gps.txt` renders the TinyGPSPlus fix age from the real host
  clock, so `gps.png` is not byte-reproducible and must not be a golden
  baseline as-is.
- The fake scan publishes two advertisement events and a scan end from a
  background host worker. `processPendingCallbacks()` drains them on the UI
  task, so the fake never mutates `CameraList` or touches LVGL from its worker.
  The fake UART captures all writes and models receiver
  replies with `uart-mode ack|nack|timeout|malformed|partial|write-error`.
  Inject UART driver events with `uart-event data|fifo|buffer|break|parity|frame|pattern`.
  Dump writes with the `uart-dump` script verb. These controls exercise the
  production GPS binary parser, retry state machine and PCAS fallback without
  changing firmware behavior.
- The fake UART and GPS task run on a background host thread. GPS page scenarios
  therefore exercise the real cross-task ownership boundary; use the
  thread-safe GPS status snapshot rather than adding direct parser reads to a
  simulator seam.
- The full script verb set is `seed`, `wait`/`advance`, `key`/`press`,
  `btn`/`button`, `capture`, `uart-dump`, `uart-mode`, `uart-event`,
  `gps-restart`, `home`, `back`, `report`, `action`, `print`, `assert`,
  `assert-eventually`, `assert-eventually-virtual`, `xassert`, `stall`, and
  `exit`. `home` resets to the root menu and focuses Scan. `back` clicks the
  LVGL header back button and fails on the root page.
  See `docs/sim.md` for every action value and query key.
- Scenario parsing is a pre-runtime gate: every verb has strict arity and
  numeric validation, unknown verbs/options and trailing values are rejected
  with status 2, and duplicate `seed` names are invalid. `action` lines are
  parsed once into `sim/scenario_action_t`; runtime dispatch consumes that
  typed value rather than reparsing whitespace-delimited text. UI action calls
  classify outcomes as `APPLIED`, `VALID_NO_EFFECT`, `UNAVAILABLE`, or
  `INVALID`, and malformed direct calls fail closed. The valid root aliases
  `page main` and `page menu` both dispatch to the root page. Signed pixel
  scrolling accepts -2147483647 through 2147483647 (plus `top`, `bottom`, and
  `next`); `INT32_MIN` is rejected because runtime negates the delta for LVGL.
- `stall <ms>` advances virtual time without running `Platform::update`. On the
  StickS3 simulator this lets scenarios expire the virtual M5PM1 watchdog while
  ordinary `wait` keeps feeding it. The `watchdog` seed is therefore accepted
  only by the StickS3 model. Specialized pre-start seeds also include
  `bulb_duration`, `gps_uart_mode`, `link_lies`, `liveness_check`, and
  `liveness_grace_ms`; keep their values synchronized with
  `docs/sim.md` and `validateSeed`.
  The simulator suppresses only the first post-stall bookkeeping feed, so the
  following assertion observes retained PMIC state before another normal UI
  cycle can feed it.
- `assert <key> <value>` fails the run on a mismatch. `xassert <key> <value>` is
  an expected-fail assert: it documents a value the app SHOULD produce once a
  pending product fix lands, prints `XFAIL (WILL_FAIL)` on a mismatch and keeps
  running, so CI stays green while the gap is on record. When the fix lands the
  value matches and it prints `XPASS`; promote that line back to `assert` in the
  fix PR so the guard starts enforcing.
- The StickS3 PMIC model retains the watchdog and download-lock registers across
  `M5PM1::begin()`, just as the PMIC is independent of an ESP32 reset.
  `platform.download_lock` exposes the long-press recovery state so a scenario
  can prove that firmware never leaves manual download recovery locked.
- The simulator consumes `Watchdog::PM1_TIMEOUT_S` and its watchdog boundary
  scenarios cover normal feeding, just-before expiry, exact-boundary expiry,
  and uint32 clock wrap. Keep those scenarios aligned with the host watchdog
  timing tests when changing the timeout.
- `assert-eventually <timeout-ms> <key> <value>` polls a query using a bounded
  monotonic wall-clock timeout while yielding to background simulator tasks.
  It is for cross-task state convergence after virtual time has advanced, not
  for extending a scenario's virtual-time budget. Timeout values must be from
  1 through 60000 ms. A timeout prints the last observed value and exits 1.
- `assert-eventually-virtual <timeout-ms> <key> <value>` polls once per normal
  UI tick until the query matches or the virtual deadline expires. Use it for a
  bounded device timeout that must continue to run while it is observed. It is
  nonblocking so `Platform::update`, LVGL, and scheduler handoffs keep running.
- UI-task delays advance virtual time and then perform a short host scheduler
  handoff. Preserve that handoff: it lets joinable background task threads
  observe each part of a scripted wait instead of starting work only after the
  entire virtual-time budget has elapsed. The handoff does not provide
  deterministic priority or preemption; plan 158 tracks that remaining gap.
- `action drop` and `action drop <n>` model a dropped fake peer link. `seed
  connect_fail true` makes the fake camera reject connection. The rig options
  are `--rig`, `--rig-port`, `--ignore-uuid-mismatch`, `--drop-notify`, and
  `--delay-ms`.
- The SDL harness models the IMU through `sim/ImuSim.cpp`, which is the host
  implementation of the same `M5.Imu` read boundary used by production code.
  Keep IMU actions and queries general enough for diagnostics, spirit-level
  orientation, and future gesture features; do not add widget-only shortcuts.
- GPS query keys include `gps.source`, `gps.satellites`, `gps.state`, and
  `gps.config.<index>.state|attempts`. UART write count and the last command are
  available as `uart.count` and `uart.last`. `camera.count` reports the current
  simulated camera-list row count, allowing scan-result de-duplication scenarios
  to assert that a repeated fake advertisement does not add a second row.
  `scan.end_callbacks` reports scan completion callback delivery, allowing
  scenarios to catch duplicate simulated completion events.
- The `scan_start_probe` boolean seed enables a concurrent callback-shaped
  probe during scan startup. `scan.start_probe_blocked` reports whether that
  callback waited for the UI mutex, guarding the watchdog-sensitive scan-start
  boundary.
- The `scan_distinct` scenario-only boolean makes the asynchronous scan worker
  publish two distinct FauxNY advertisements. The
  `scan-distinct-rows-heartbeat.txt` scenario asserts both rows and the live
  watchdog after the UI task drains them.
- Battery policy scenarios seed `battery_level`, `battery_voltage`,
  `battery_current`, and `battery_charging`, plus the real `auto_off` and
  `low_batt` settings. The `action battery LEVEL VOLTAGE_MV CURRENT_MA
  CHARGING` command changes the reading at runtime. `ui.low_battery` reports
  the rendered warning state and `platform.power_off` records a production
  power-off request without terminating the simulator.
  `auto_off_charging` is the explicit opt-in for auto-off while charging.
  Charging-safe and opt-in scenarios assert both production power-off and
  light-sleep surfaces; no simulator-only policy or persistence path is used.
  `sim/scripts/run-invalid.sh` requires malformed typed seed fixtures to exit
  with DSL validation status 2. Typed seeds are validated while the scenario
  file is parsed, before SDL or firmware worker threads start, so invalid input
  cannot race simulator teardown or be silently narrowed. Seed names are
  allowlisted and each seed requires exactly two arguments, preventing typos or
  trailing values from being silently ignored.
- `sim/scripts/run-watchdog.sh` is the explicit M5StickS3 watchdog gate. It
  runs all retained-PMIC feed and boundary scenarios against the default freshly built
  binary; do not substitute a stale binary or a different panel profile.
- Connection-state coverage: `connstate-page-sweep.txt` visits every page
  reachable during a connected session (connected menu, Remote shutter, Bulb,
  Cameras, Intervalometer, GPS Data), drops the link on each, and asserts the
  drop is surfaced there (`ui.link_alert yes`) and clears on recovery.
  `statusbar-stability.txt` asserts the status row stays stable across the
  disconnected / connected / reconnecting / GPS matrix. The CI job runs both on
  all three panel widths. Query seams (all `FURBLE_SIM` only): `ui.link_alert`,
  `ui.page_banner` (`none` off the full-screen remote pages, `yes`/`no` on them),
  `ui.bt_icon` (`hidden`/`plain`/`red` status-row reconnect icon), `ui.battery_x`
  and `ui.battery_drift` (battery icon x, and its shift from the first-read
  anchor). `action page <shutter|bulb|cameras|remote_timer|remote_gps>` reaches
  the connected sub-pages. WILL_FAIL (`xassert`) lines pending product fixes are
  the bulb-page reconnect banner (`ui.page_banner yes` on Bulb) and the exact
  battery anchor (`ui.battery_drift 0` as status icons change). The current
  audit passes that exact check on 135x240 and 320x240 but still finds drift on
  80x160, so it remains xassert until all three panels pass.
- `sim/scripts/ui-screenshots.txt` captures every modeled page for the
  screenshot CI workflow. Menu routes are position-sensitive: adding or
  removing a settings entry changes the `key down` counts in the scripts.

## Seeded UI fuzzer

- `--fuzz --seed N --fuzz-steps N [--fuzz-verbose]` (or `FURBLE_FUZZ_SEED` /
  `FURBLE_FUZZ_STEPS`) runs `sim/fuzz.cpp` instead of a script: a deterministic
  randomized stream of button, navigation, settings and modal events into the
  real UI, with crash, stale-focus, stacked-modal, must-fit-overflow, timer-leak
  and navigation-trap invariants checked after each event's completed settle
  cycles. `sim/fuzz_machine.{h,cpp}` owns the Apply/Settle/Check/Escape/Finish
  phases and raw-output rejection sampling, while the UI task reports each
  completed `lv_task_handler` cycle through `fuzzCycleComplete`. Same seed and
  board reproduce a finding exactly. See plans/105-ui-fuzzing.md.
- `sim/scripts/run-fuzz.sh` runs the pinned seed set and fails on any finding;
  `FURBLE_FUZZ_XFAIL_SEEDS` pins tracked-but-unfixed bugs as expected-fail.
  The wrapper passes explicit `--seed` and `--fuzz-steps` values on every run;
  those CLI values take precedence over matching `FURBLE_FUZZ_SEED` and
  `FURBLE_FUZZ_STEPS` fallbacks. `--fuzz-verbose` also enables fuzzing when it
  is supplied by itself.
- `FURBLE_SIM_SANITIZE=address,undefined sh sim/build.sh` builds an instrumented
  binary for the deeper memory hunt. Off by default so the plain build and CI
  stay fast.

## Docs screenshots and sim capabilities

- The docs walkthrough gallery is regenerated by `sim/scripts/docs-capture.sh`,
  which builds the sim once per panel class and drives the capture scenarios for
  each of the three themes (Default, Dark, Mono Furble) with every optional
  feature enabled. Output lands in `docs/img/<board>/<theme>/` plus the flat
  default and dark sets from `docs-screenshots.txt` / `docs-screenshots-dark.txt`.
  It also captures a Text size gallery on the StickS3 Default theme, one set per
  size, under `docs/img/textsize/<size>/` via `docs-textsize.txt`.
- `FURBLE_SIM_TEXTSIZE` picks the UI text size at launch the same way
  `FURBLE_SIM_THEME` picks the theme: the font is chosen once at UI construction
  from the TEXT_SIZE setting, so main.cpp seeds it before the UI exists. It
  accepts a name (`small` / `normal` / `large`, case insensitive) or the numeric
  setting value (`0` / `1` / `2`). Reusable for text-size capture and collision
  sweeps without navigating the roller and restarting.
- Optional-hardware presence is sim only and env gated, so the default build and
  the key-count menu routes are unchanged. A capture run sets these to make the
  gated submenus render: `FURBLE_SIM_IR` (Infrared), `FURBLE_SIM_FEEDBACK`
  (Feedback), `FURBLE_SIM_SD` (Storage). They never change on-device detection;
  the switches live in `sim/shim/FurbleSimCaps.h`.
- `FURBLE_SIM_NO_TOUCH=1` selects the non-touch physical-button layout without a
  scenario seed, so a harness can pick it per board. `FURBLE_SIM_RIG=0` builds
  without the rig so the shipped one-line header title renders.
  `FURBLE_SIM_CAPTURE_SPLASH=<png>` snapshots the boot splash before the LVGL UI
  starts.
- Scenario seams for the list pages: the `saved_camera` seed adds a saved but
  inactive camera so the Connect and Delete lists render and their buttons
  enable; the `nav` action reaches `scan`, `connect`, `delete`, `infrared`,
  `feedback` and `storage` in addition to the settings pages.
- `sim/scenarios/bughunt/page-matrix.txt` is the release gate for modeled-page
  reachability and layout. It walks the root, Connect/Scan/Delete lists, every
  settings and diagnostics route, optional Infrared/Feedback/Storage pages,
  connected-session pages, and the Bulb and intervalometer run pages. It
  asserts `ui.page` identity for every route, asserts no overflow on compact
  pages, and drives intentional-scroll pages to `scroll bottom` and back to
  `scroll top`, asserting both extents are zero. Run it with
  `FURBLE_SIM_IR=1 FURBLE_SIM_FEEDBACK=1 FURBLE_SIM_SD=1` against all three
  panel builds. The CI matrix uses the same script for 80x160 M5StickC,
  135x240 M5StickS3, and 320x240 M5Stack Core, so a page that only fails on a
  particular geometry cannot hide behind a single reference panel.
- `sim/scenarios/bughunt/overflow-sweep.txt` is the complementary layout audit.
  It visits every reachable root, settings, diagnostics, capability, and
  connected-session page, asserts fit for compact pages, and prints the
  overflow state for intentional-scroll pages. CI runs it on all three panel
  classes with optional capabilities enabled. Keep route identity and scroll
  endpoint assertions in `page-matrix.txt` rather than duplicating them here.
- All simulator scenarios are catalogued in `sim/scenarios/manifest.json`.
  Every entry declares its suite owner, board matrix, capabilities, and
  expected exit status. Obsolete scenarios must be non-certified with a reason.
- `seed fb_output 0..4` models the persisted feedback output selection. Page
  and overflow matrices seed `fb_output 1` so the sound volume route is truly
  reachable when feedback capability is enabled. `feedback-hidden-route.txt`
  verifies that the volume route remains unreachable when the output is Off;
  hidden controls must never be activated by simulator navigation.

## IMU injection and redraw probe

- The IMU (BMI270/MPU6886) is a physical sensor with no host counterpart, so a
  scenario injects orientation through `sim/ImuSim.cpp`, which mirrors the same
  `M5.Imu` surface (enabled, update, getAccel, getGyro) the firmware reads under
  `#if defined(FURBLE_SIM)`. Actions: `imu.accel <x> <y> <z>` (G), `imu.roll
  <deg>`, `imu.pitch <deg>`, `imu.gyro <x> <y> <z>`, `imu.enable`, `imu.disable`,
  `imu.accel.fail/recover` and `imu.gyro.fail/recover`.
  Seed `imu true` turns the IMU setting on, while `seed imu false` models the
  disabled setting and hides both optional pages. The spirit level filter,
  sensitivity curve and auto-rotate all run on the injected sample.
  `imu_accel_x/y/z` read the rendered Diagnostics > IMU live label back.
  `imu_accel_updates` and `imu_gyro_updates` count actual label redraws; validity
  is tracked independently so one failed sensor does not redraw the other.
  Script actions are queued onto the UI task before touching LVGL; the
  `sim_action_on_ui` query is a thread-ownership regression guard.
  `level_root_width/height` expose the pixel-sized top-level window after panel
  rotation, and `level_side_on_screen` verifies the complete moving bubble lies
  inside the active display. Keep these guards with the numeric bubble-offset
  checks: an offset can be correct while a stale portrait-width root clips the
  widget from the landscape framebuffer.
- Redraw-storm probe: the `invalidate.reset` action zeroes a counter fed by the
  LVGL `LV_EVENT_INVALIDATE_AREA` hook (`profilerInvalidationProbeCount` in
  `sim/power_profiler.cpp`), and `ui.invalidate_count` reports events since the
  reset. Hold a page at a fixed injected state over a wait and bound the count to
  catch a per-tick setter that redraws every frame (CLAUDE.md "LVGL redraw
  trap"). `redraw-steady.txt` guards the level and connected pages this way.
- Numeric bounds: alongside the exact-match `assert`, the driver has `assert_max
  <key> <n>` and `assert_min <key> <n>` (integer parse, inclusive). They express
  a redraw ceiling and a width-agnostic direction or render floor (for example
  `assert_min ui.visible_objects 1`) without pinning a per-panel pixel value.
