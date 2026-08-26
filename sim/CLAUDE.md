# sim/ (host SDL simulator)

Host build of the furble UI over M5GFX/M5Unified SDL. Developer tool only.
It never changes firmware behavior: no shipping source under src/, include/,
or lib/ may be modified for the simulator. All adaptation happens through the
shim headers and the fake implementations here. One narrow exception exists:
`UI::simulatorHome` and `UI::simulatorBack` in `src/FurbleUI.cpp` give scripts
deterministic menu navigation. They are guarded by `#if defined(FURBLE_SIM)`,
which only the sim build defines, so firmware builds compile identical code.

The complete build, panel, scenario DSL, query, fault-injection, and
scenario-authoring reference is [docs/sim.md](../docs/sim.md). Keep this file
as the directory-local contract and keep the reference synchronized with the
tokens in `sim/driver.cpp`, `src/FurbleUI.cpp`, and the host fault harness.

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
- The fake scan delivers its result and the scan end callback in the same
  `update()` tick. The fake UART captures all writes and models receiver
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
  `assert-eventually`, `xassert`, `stall`, and `exit`. `home` resets to the root menu and focuses
  Scan. `back` clicks the LVGL header back button and fails on the root page.
  See `docs/sim.md` for every action value and query key.
- `stall <ms>` advances virtual time without running `Platform::update`. On the
  StickS3 simulator this lets scenarios expire the virtual M5PM1 watchdog while
  ordinary `wait` keeps feeding it. The `watchdog` seed is therefore accepted
  only by the StickS3 model. Specialized pre-start seeds also include
  `bulb_duration` and `gps_uart_mode`; keep their values synchronized with
  `docs/sim.md` and `validateSeed`.
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
- `assert-eventually <timeout-ms> <key> <value>` polls a query using a bounded
  monotonic wall-clock timeout while yielding to background simulator tasks.
  It is for cross-task state convergence after virtual time has advanced, not
  for extending a scenario's virtual-time budget. Timeout values must be from
  1 through 60000 ms. A timeout prints the last observed value and exits 1.
- UI-task delays advance virtual time and then perform a short host scheduler
  handoff. Preserve that handoff: it lets detached background task threads
  observe each part of a scripted wait instead of starting work only after the
  entire virtual-time budget has elapsed.
- `action drop` and `action drop <n>` model a dropped fake peer link. `seed
  connect_fail true` makes the fake camera reject connection. The rig options
  are `--rig`, `--rig-port`, `--ignore-uuid-mismatch`, `--drop-notify`, and
  `--delay-ms`.
- The SDL harness has no IMU injection action, seed, or query. Its platform shim
  sets `config.internal_imu = false`; do not document or add an IMU DSL token
  until a source seam exists.
- GPS query keys include `gps.source`, `gps.satellites`, `gps.state`, and
  `gps.config.<index>.state|attempts`. UART write count and the last command are
  available as `uart.count` and `uart.last`. `camera.count` reports the current
  simulated camera-list row count, allowing scan-result de-duplication scenarios
  to assert that a repeated fake advertisement does not add a second row.
  `scan.end_callbacks` reports scan completion callback delivery, allowing
  scenarios to catch duplicate simulated completion events.
- Battery policy scenarios seed `battery_level`, `battery_voltage`,
  `battery_current`, and `battery_charging`, plus the real `auto_off` and
  `low_batt` settings. The `action battery LEVEL VOLTAGE_MV CURRENT_MA
  CHARGING` command changes the reading at runtime. `ui.low_battery` reports
  the rendered warning state and `platform.power_off` records a production
  power-off request without terminating the simulator.
  `sim/scripts/run-invalid.sh` requires malformed typed seed fixtures to exit
  with DSL validation status 2. Typed seeds are validated while the scenario
  file is parsed, before SDL or firmware worker threads start, so invalid input
  cannot race simulator teardown or be silently narrowed. Seed names are
  allowlisted and each seed requires exactly two arguments, preventing typos or
  trailing values from being silently ignored.
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
  and navigation-trap invariants checked after every event. Same seed and board
  reproduce a finding exactly. See plans/105-ui-fuzzing.md.
- `sim/scripts/run-fuzz.sh` runs the pinned seed set and fails on any finding;
  `FURBLE_FUZZ_XFAIL_SEEDS` pins tracked-but-unfixed bugs as expected-fail.
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
