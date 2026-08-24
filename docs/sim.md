# Host simulator

The simulator is a host-only SDL build of the furble UI. It uses the real UI
and control code with simulator shims and fakes. It does not build firmware,
talk to a radio, or model the device IMU. Scripted time comes from the
simulator virtual clock, so scenario runs do not wait on wall-clock time.

## Panels

`sim/build.sh` builds one panel class per invocation. Set both board variables
together:

| Panel | Furble board | M5GFX board |
| --- | --- | --- |
| M5StickC, 80x160 | `FURBLE_M5STICKC` | `board_M5StickC` |
| M5StickS3, 135x240 | `FURBLE_M5STICKS3` | `board_M5StickS3` |
| M5Stack Core, 320x240 | `FURBLE_M5COREX` | `board_M5Stack` |

The default is the M5StickS3 135x240 panel. The Core panel models the 320x240
Core class used by the simulator; the Stick panels use the non-touch physical
button layout when `FURBLE_SIM_NO_TOUCH` is set.

## Build and run

The direct build uses clang, SDL2, M5GFX, M5Unified, TinyGPSPlus, and LVGL.
By default it looks for the PlatformIO cache at
`.pio/libdeps/m5stick-s3` and the managed LVGL component at
`managed_components/lvgl__lvgl`. Override those locations with
`FURBLE_DEP_ROOT` and `FURBLE_LVGL_DIR`.

If an sdkconfig changed, regenerate the LVGL configuration first:

```sh
python3 tools/gen_lv_conf.py sdkconfig.m5stick-s3 sim/lv_conf.h
sh sim/build.sh
```

The output is `sim/build/furble-sim`. A separate panel build uses a separate
`FURBLE_SIM_BUILD_DIR`:

```sh
FURBLE_SIM_BUILD_DIR=sim/build-stickc \
FURBLE_SIM_FURBLE_BOARD=FURBLE_M5STICKC \
FURBLE_SIM_M5GFX_BOARD=board_M5StickC \
sh sim/build.sh
```

The direct build stores a compiler depfile beside every object in
`<build-dir>/obj`. Incremental runs ask `make -q` to evaluate those depfiles,
so changing a project or dependency header rebuilds every dependent object even
when the including source file itself is unchanged. A missing depfile is
treated as a cache miss. Check this contract with:

```sh
sh sim/scripts/test-build-deps.sh
```

The self-test performs one complete build and one incremental build. It leaves
the project header timestamp unchanged when it exits.

The CMake entry point has the same source and board defaults:

```sh
cmake -S sim -B sim/build-cmake
cmake --build sim/build-cmake
```

Run one scenario headlessly:

```sh
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  sim/build/furble-sim --script sim/scenarios/e2e/connect-flow.txt
```

`sim/scripts/run-e2e.sh` runs every `*.txt` file in its argument directory,
defaulting to `sim/scenarios/e2e`. It exports `FURBLE_SIM_IR`,
`FURBLE_SIM_FEEDBACK`, and `FURBLE_SIM_SD` as optional capabilities for that
run. The capture helpers use `--out` to choose the capture directory.
`sim/scripts/docs-capture.sh` rebuilds the three panel classes and regenerates
the documentation screenshot gallery. It uses `FURBLE_SIM_BOARDS` and
`FURBLE_SIM_BUILD_ROOT` to limit the matrix or relocate its build trees.

The simulator command-line options are:

| Option | Effect |
| --- | --- |
| `--script FILE` | Run a scenario file. |
| `--out DIR` / `--capture-dir DIR` | Set the PNG capture directory. |
| `--report-dir DIR` | Set the profiler report directory. |
| `--rig` | Start the companion TCP rig on `127.0.0.1`. |
| `--rig-port PORT` | Select the rig listening port. |
| `--ignore-uuid-mismatch` | Accept a rig HELLO with a different service UUID. |
| `--drop-notify` | Drop companion status notifications from the rig. |
| `--delay-ms MS` | Delay each rig outbound frame by the requested milliseconds. |
| `--fuzz` | Run the seeded UI fuzzer instead of a scenario. |
| `--seed N` | Select the UI fuzzer seed and enable it. |
| `--fuzz-steps N` | Set the UI fuzzer event budget and enable it. |
| `--fuzz-verbose` | Print each UI fuzzer event. |
| `--help` | Print the option synopsis and exit. |

The UI fuzzer also accepts `FURBLE_FUZZ_SEED` and `FURBLE_FUZZ_STEPS`. The
release fuzzer wrapper is `sim/scripts/run-fuzz.sh`; it uses
`FURBLE_FUZZ_SEEDS`, `FURBLE_FUZZ_XFAIL_SEEDS`, `FURBLE_FUZZ_STEPS`, and
`FURBLE_SIM_BIN`.

For an instrumented UI-fuzzer build, set `FURBLE_SIM_SANITIZE` to a clang
sanitizer list and use a separate `FURBLE_SIM_BUILD_DIR`, for example
`FURBLE_SIM_SANITIZE=address,undefined`.

## Scenario files

Scenario files are line oriented. A `#` starts a comment. Blank lines and the
text after a comment are ignored. Each line starts with one verb.

### Core verbs

| Verb | Form and behavior |
| --- | --- |
| `seed` | `seed NAME VALUE` stores a scenario setting before UI construction. Effective names are listed below. |
| `wait` / `advance` | `wait MS` or `advance MS` advances virtual time. |
| `stall` | `stall MS` advances virtual time without running the platform update loop. On the StickS3 model this can expire the PM1 watchdog; `MS` must be non-zero. |
| `key` / `press` | `key KEY` or `press KEY`, where `KEY` is `up`, `down`, `left`, `right`, `return`, or `enter`. |
| `btn` / `button` | `btn NAME [hold\|long]` or `button NAME [hold\|long]`. Stick boards expose `a`, `b`, and `pwr`; Core exposes `a`, `b`, and `c`. |
| `capture` | `capture NAME` writes a PNG under the capture directory. |
| `uart-dump` | Prints captured fake-UART writes as `uart-tx` lines, then clears them. |
| `home` | Goes to the root menu and focuses Scan. |
| `back` | Clicks the LVGL header back button. It fails at the root page. |
| `report` | `report NAME` writes a profiler JSON report. |
| `action` | `action COMMAND` invokes one of the simulator actions below. |
| `print` | `print KEY` prints the resolved scenario query. |
| `assert` | `assert KEY VALUE` aborts with exit status 1 when the resolved value differs. |
| `assert-eventually` | `assert-eventually TIMEOUT_MS KEY VALUE` polls the resolved value using a monotonic wall-clock timeout while yielding to background simulator tasks. TIMEOUT_MS must be 1 through 60000; a timeout reports the last value and exits 1. |
| `xassert` | `xassert KEY VALUE` records `XFAIL (WILL_FAIL)` on mismatch, continues the scenario, and records `XPASS` on a match. It never aborts. |
| `exit` | Ends the simulator with status 0. |

`assert`, `assert-eventually`, `xassert`, and `print` use the same query namespaces:
`ui.*`, `control.*`, `camera.*`, `gps.*`, `uart.*`, and `setting.*`.

### Effective `seed` names

These byte settings are applied before the UI is constructed:
`brightness`, `inactivity`, `display_off`, `gps_rate`, `gps_constel`,
`gps_power`, `gps_duty`, `cpu_freq`, `tx_power`, `scan_mode`, `text_size`,
`auto_off`, and `low_batt`.

Battery seeds select the initial deterministic platform sample:
`battery_level` (0 to 100), `battery_voltage` (millivolts),
`battery_current` (signed milliamps), and `battery_charging` (`true` or
`false`).

These boolean settings are applied before the UI is constructed:
`gps`, `gps_nmea`, `fauxny`, `autoconnect`, `reconnect`, `sleep_conn`, and
`boot_splash`.

The scenario-only settings are `saved_camera`, `connect_fail`, and
`no_touch`. `saved_camera` adds an inactive saved camera, `connect_fail`
makes the fake camera reject connect, and `no_touch` selects the physical-button
layout. The interval settings are `interval_count`, `interval_delay`,
`interval_shutter`, and `interval_wait`.

### `action` commands

The fixed action commands are:

```text
action blind
action blind-shutter
action indicator-click-focus
action focus-lock
action connect
action connect-two
action disconnect
action drop
action drop N
action cancel
action shutter
action button-mode one-button
action button-mode two-button
action main-press-hold
action main-double-click
action main-click-hold
action intervalometer
action stop
action bulb-start
action preset-step-up
action preset-step-down
action companion-pair-request
action companion-accept
action companion-reject
```

The battery action changes the platform sample at runtime:

```text
action battery LEVEL VOLTAGE_MV CURRENT_MA CHARGING
```

For example, `action battery 80 4000 0 false` simulates a recovered,
discharging pack. The next battery timer sample consumes the new value.

`action drop` drops every active fake camera. `action drop N` drops target
`N`, using zero-based target numbering. `action connect-two` selects two fake
cameras for multi-connect coverage. `action companion-pair-request` injects a
pending companion PIN without a rig TCP peer; `action companion-accept` and
`action companion-reject` click the real pairing dialog buttons.

The `toggle` action accepts these setting names:
`gps`, `gps_nmea`, `autoconnect`, `reconnect`, `multiconnect`, `companion`,
`watchdog`, `ir`, `show_title`, `tx_adaptive`, `conn_saver`, `preset_picker`,
and `recon_backoff`. `watchdog` is present in the M5StickS3 build.

The `nav` action clicks a real menu button. Its page names are:
`connect`, `scan`, `delete`, `bulb`, `settings`, `display`, `features`,
`infrared`, `gps`, `gps_data`, `nmea`, `timer`, `theme`, `text_size`,
`bluetooth`, `about`, `power`, `feedback`, `diagnostics`, `device_info`,
`power_state`, `ble`, `battery`, and `storage`.

The `scroll` action accepts `top`, `bottom`, `next`, or a signed pixel count.
The `page` action accepts `main`, `shutter`, `bulb`, `cameras`,
`remote_timer`, `remote_gps`, `connected`, `settings`, `display`, `features`,
`gps`, `timer`, `theme`, `text_size`, `bluetooth`, `about`, `power`, and
`diagnostics`. The connected page map can also report `bulb_run`, `timer_run`,
and `remote_disconnect` while a run or disconnect transition is active.

### Query keys

The complete `ui.*` query set is:

| Query | Returned value |
| --- | --- |
| `ui.connect_box` | `hidden` or `visible`. |
| `ui.indicators_focused` | `yes` or `no`. |
| `ui.bulb_ms` | Persisted bulb duration in milliseconds. |
| `ui.disconnect_calls` | Numeric disconnect count. |
| `ui.reconnecting` | `yes` or `no`. |
| `ui.bt_color` | `hidden`, `red`, or `other`. |
| `ui.status_text` | The current status title, or `none`. |
| `ui.reconnect_count` | `down/total`, such as `1/2`. |
| `ui.remote_status` | `hidden`, `shown`, or the rendered reconnect text. |
| `ui.remote_reconnecting` | `yes` or `no`. |
| `ui.remote_named` | `yes` or `no`. |
| `ui.battery_pinned` | `yes`, `no`, or `unknown`. |
| `ui.bulb_status` | `hidden`, `shown`, or the rendered reconnect text. |
| `ui.bulb_reconnecting` | `yes` or `no`. |
| `ui.bulb_named` | `yes` or `no`. |
| `ui.connect_timer` | `none`, `paused`, or `running`. |
| `ui.connected` | `yes` or `no`. |
| `ui.page` | A page name, or `other`. |
| `ui.back` | `none`, `hidden`, `disabled`, or `visible`. |
| `ui.modal` | `open` or `closed`. |
| `ui.boot_splash` | `off`, `shown`, or `partial`. |
| `ui.modal_focus` | `closed`, `yes`, or `no`. |
| `ui.modal_count` | Numeric live modal count. |
| `ui.focus` | `none`, `stale`, or `ok`. |
| `ui.focus_on_page` | `yes` or `no`. |
| `ui.overflow` | `unknown`, `yes`, or `no`. |
| `ui.scroll_bottom` | Numeric pixels, or `unknown`. |
| `ui.scroll_top` | Numeric pixels, or `unknown`. |
| `ui.text_size` | Numeric roller selection, or `unknown`. |
| `ui.text_size_options` | Numeric roller option count, or `unknown`. |
| `ui.interval_state` | `idle`, `wait`, `shutter`, `delay`, `finished`, or `unknown`. |
| `ui.bulb_remaining` | The rendered Bulb countdown, or `unknown`. |
| `ui.bulb_state` | `idle`, `running`, `done`, or `unknown`. |
| `ui.shutter_held` | `yes` while the Bulb shutter lock is held, otherwise `no`. |
| `ui.interval_count_fired` | Numeric count taken from the rendered intervalometer counter. |
| `ui.gps_speed` | Rendered speed value. |
| `ui.gps_lat` | Rendered latitude value. |
| `ui.gps_lon` | Rendered longitude value. |
| `ui.gps_satellites` | Rendered satellite count. |
| `ui.gps_fix` | `yes` when the GPS source is active, otherwise `no`. |
| `ui.focus_outline_count` | Numeric outline count, or `none`. |
| `ui.lock_outline` | Numeric outline width, or `none`. |
| `ui.link_alert` | `yes` or `no`. |
| `ui.page_banner` | `none`, `yes`, or `no`. |
| `ui.bt_icon` | `hidden`, `red`, or `plain`. |
| `ui.battery_x` | Numeric header x position, or `none`. |
| `ui.battery_drift` | Numeric x delta from the first read, or `none`. |
| `ui.low_battery` | `none`, `warn`, or `power_off_pending`. |

The other namespaces are:

- `platform.battery.level`, `platform.battery.voltage`,
  `platform.battery.current`, and `platform.battery.charging` report the
  current deterministic platform sample.
- `platform.power_off` reports `yes` after production `UI::doPowerOff()` reaches
  the simulator power-off seam. The simulator records the request instead of
  terminating the process, so the scenario can assert shutdown ordering.

- `control.state`: `idle`, `connect`, `connecting`, `connect_failed`,
  `active`, `disconnecting`, or `unknown`.
- `control.connected` and `control.targets`: numeric target counts.
- `camera.count`: numeric camera-list row count, useful for scan de-duplication
  assertions.
- `scan.end_callbacks`: numeric count of simulated scan-end callbacks delivered
  by the current scan. A discovery scan should deliver exactly one callback.
- `camera.shutter_presses`, `camera.shutter_releases`, `camera.focus_presses`,
  and `camera.focus_releases`: numeric fake-camera command counts.
- `setting.text_size`: the persisted numeric text-size setting.
- `setting.fauxny`, `setting.autoconnect`, `setting.reconnect`,
  `setting.multiconnect`, `setting.companion`, `setting.watchdog`,
  `setting.gps`, `setting.gps_nmea`, `setting.ir`, `setting.conn_saver`,
  `setting.preset_picker`, `setting.show_title`, `setting.tx_adaptive`, and
  `setting.recon_backoff`: `1` or `0`. `setting.watchdog` is in the
  M5StickS3 build.

## Fault injection and fuzzing

### SDL simulator faults

- `seed connect_fail true` makes `Camera::connect` return false. Combine it
  with `action connect` to exercise the connect-failed UI path.
- `action drop` models a peer link drop for every active fake camera. `action
  drop N` drops one zero-based target. With `seed reconnect true`, the
  control state re-enters `connecting`; without it, the state returns to
  `idle` when no other link remains. `action connect-two` keeps a surviving
  link active while one target is dropped.
- The companion rig is a localhost TCP peer, not BLE. `--rig` enables it,
  `--rig-port` selects the port, `--ignore-uuid-mismatch` accepts a bad service
  UUID, `--drop-notify` drops status notifications, and `--delay-ms` delays
  outbound frames. `FURBLE_SIM_RIG=0` removes the rig transport at build time.
- `action companion-pair-request` injects a pending pairing without a rig
  peer. This is the headless pairing fault seam.
- `FURBLE_SIM_IR`, `FURBLE_SIM_FEEDBACK`, and `FURBLE_SIM_SD` inject optional
  capability presence. `FURBLE_SIM_THEME` and `FURBLE_SIM_TEXTSIZE` select
  launch-time rendering variants. `FURBLE_SIM_CAPTURE_SPLASH` captures the
  boot splash. `FURBLE_SIM_PREFS` selects the preferences file used by the
  simulator.
- Battery policy tests should seed `low_batt` and the four battery fields, then
  use `action battery ...` to change the sample. Six consecutive low samples
  qualify the production 30-second hysteresis; charging suppresses both the
  warning and power-off path.

There is no IMU injection knob in this host SDL simulator. The platform shim
sets `config.internal_imu = false`, and the scenario parser has no IMU action,
seed, or query. Do not add an IMU token to a scenario until the harness source
registers one.

### Real-code host BLE faults

The host harness under `tests/host` drives production camera and control code
against `MockNimBLE` and `FujifilmVirtualCamera`. Build it with:

```sh
cmake -S tests/host -B /tmp/furble-host-build
cmake --build /tmp/furble-host-build
ctest --test-dir /tmp/furble-host-build --output-on-failure
```

The seeded connect-handshake fuzzer is `control_fuzz`:

```sh
/tmp/furble-host-build/control_fuzz 42 20
/tmp/furble-host-build/control_fuzz --repro handshake-phase-drop 42
```

Its operation names are `connect-clean`, `transient-connect-fail`,
`pool-exhaustion`, `stale-session-reconnect`, `write-fail-abort`,
`mid-handshake-drop`, `handshake-phase-drop`, `drop-auto-reconnect`,
`dead-camera-disconnect`, and `mid-connect-selfdelete` when that hook is
compiled. The repro names are `missing-shutter-service`,
`missing-shutter-char`, and `handshake-phase-drop`. `FUZZ_SEED` and
`FUZZ_ITERS` provide the seed and iteration count when command-line values
are omitted.

The peer and mock fault APIs used by that fuzzer are
`setConnectShouldFail`, `setConnectFailCount`, `mockDropLink`,
`mockDropLinkSelfDelete`, `setStaleSubscribeSession`, `suppressService`,
`suppressCharacteristic`, `failWrite`, `dropLinkOnWrite`,
`dropLinkDuringConnect`, and `clearFaults`. These faults cover connect failure,
client-pool exhaustion, stale subscriptions, missing GATT elements, rejected
handshake writes, link drops during a handshake, and deferred client deletion.

## Adding a scenario

1. Add a `.txt` file under `sim/scenarios/e2e` for a normal end-to-end guard,
   or under `sim/scenarios/bughunt` for a focused regression. Use only the
   verbs, action values, and query keys above.
2. Use `seed` for state needed before UI construction. Use `action` to drive a
   real UI path, and `wait` or `advance` before checking timer-driven state.
   Use `assert-eventually` only when a background simulator task must catch up
   after virtual time has advanced. Keep exact count/value assertions after the
   eventual state assertion.
   CI repeats the asynchronous GPS scenarios through
   `sim/scripts/run-async-stress.sh`; keep cross-task regressions in that set.
3. Prefer `assert` for a fixed contract. Use `xassert` only for a known gap
   that is expected to fail today. It records XFAIL and continues; an XPASS
   is the signal to promote the line to `assert` after the product fix.
4. Run the smallest panel first, then run the same file with the other board
   builds if the behavior is panel-independent. A physical button name must
   exist on every board used by the scenario.
5. Run the directory wrapper so the new file is included:

```sh
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  FURBLE_SIM_BIN=sim/build/furble-sim sim/scripts/run-e2e.sh sim/scenarios/e2e
```

For a capture scenario, use `capture NAME` and invoke the binary with `--out`.
For a GPS scenario, `uart-dump` exposes the fake UART writes for inspection.

### Minimal xassert example

This demonstrates both paths and proves that the following `print` still runs
after an XFAIL:

```text
xassert ui.page connected
xassert ui.page main
print ui.page
exit
```

The first line prints XFAIL, the second prints XPASS, and the process exits 0.
