# Host simulator

The simulator is a host-only SDL build of the furble UI. It uses the real UI
and control code with simulator shims and fakes. It does not build firmware or
talk to a radio. The IMU is represented by a deterministic injection seam, so
production level and diagnostics code can be exercised without a physical
sensor. Scripted time comes from the
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
button layout when `FURBLE_SIM_NO_TOUCH` is set or a scenario seeds
`no_touch true`.

The SDL panel always attaches a mouse-driven touch device, so an unseeded run
renders the touch layout on every modeled board. None of the three modeled
boards has a touch panel. The Sticks and the M5Stack Core Basic all ship the
non-touch layout, which reserves a 26 px navigation bar band at the bottom of
the window content; on the Sticks the band stays empty and the three indicators
float over the screen, and on the Core they live inside the band. Only the
Core2, which `sim/build.sh` does not model, ships the touch layout.

`bughunt/stick-notouch-layout-135.txt`, `bughunt/stick-notouch-layout-80.txt`
and `bughunt/core-notouch-layout.txt` seed `no_touch` and are each certified for
exactly one board, so they measure the layout that board really has. Every other
overflow scenario measures the touch layout. See
[`plans/165-sim-no-touch-layout.md`](../plans/165-sim-no-touch-layout.md).

## Production-path parity

The simulator's shared-code boundary is audited in [`sim/CLAUDE.md`](../sim/CLAUDE.md).
In brief, the whole connection stack is production: `Control`, `Camera`,
`CameraList`, `Scan`, `Device` and every vendor class compile into the
simulator over the shared MockNimBLE boundary in `lib/testing/nimble` and the
virtual camera peers in `lib/testing/peer`. Scan startup, generation fencing,
queue draining, advertisement matching, list persistence, display mode/flush
and LVGL timers are all production code. The simulator supplies only the radio
below NimBLE: `sim/BleSim.cpp` runs a virtual radio task that advertises the
seeded peers and models the controller-owned discovery timer, and it injects
faults at the transport only.

M5GFX SDL, FreeRTOS/ESP-IDF calls, the NimBLE transport, UART/GPS
bytes, PMIC/power, NVS, and optional IR/feedback/SD hardware are the remaining
lowest-level host seams. Script actions dispatch real LVGL/control handlers;
`simulatorHome`/`simulatorBack` and timing-sensitive gesture setup are the
explicit deterministic input seams. There is no simulator-only display
rotation or display-policy implementation. Adding a shortcut requires a
contract scenario and an inventory entry.

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

The retained M5PM1 watchdog contract has a dedicated gate:
`sim/scripts/run-watchdog.sh` runs the feed, near-boundary, expiry, and
uint32-wrap scenarios against the default, freshly built M5StickS3 binary.
Keep this explicit gate when changing the watchdog timeout or simulator timing
path.

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
| `--fuzz-verbose` | Enable fuzzing and print each UI fuzzer event. |
| `--help` | Print the option synopsis and exit. |

The UI fuzzer also accepts `FURBLE_FUZZ_SEED` and `FURBLE_FUZZ_STEPS` as
fallbacks. Explicit `--seed` and `--fuzz-steps` values take precedence over
their matching environment variables, even when a wrapper inherits both. The
release fuzzer wrapper is `sim/scripts/run-fuzz.sh`; it uses
`FURBLE_FUZZ_SEEDS`, `FURBLE_FUZZ_XFAIL_SEEDS`, `FURBLE_FUZZ_STEPS`,
`FURBLE_FUZZ_SEED_TIMEOUT`, `FURBLE_FUZZ_REPEAT_SEED`, and `FURBLE_SIM_BIN`.

After the guarded seeds, `run-fuzz.sh` replays `FURBLE_FUZZ_REPEAT_SEED`
(default: the first guarded seed, empty to skip) and requires the two runs to
produce identical `FUZZ EVENTS`, `FUZZ COVERAGE` and `FUZZ SUMMARY` lines, with
`observed_delta` and `no_observed_delta` masked. The same seed must drive the
same event stream and reach the same pages.

The comparison stops there on purpose. Firmware behaviour under the fuzzer is
not yet reproducible line for line: two runs of the same seed can differ by one
connect attempt. `Camera::m_Mutex`, the one host mutex a connect holds for its
whole attempt, is scheduler visible since plans/173; the host mutexes that are
left are held for microseconds each and have not been measured to move a run,
but they are still invisible, so asserting the whole log stays out of the
gate.

## Wall-clock bounds and the stall watchdog

Every bound inside the simulator except one is denominated in virtual time, so
none of them can see a stall that stops virtual time advancing at all.

`sim/watchdog.cpp` is that one exception. It samples the virtual clock, the
scheduler progress counter and the recorded phase, and if none of them moves
for `FURBLE_SIM_WATCHDOG_SECONDS` host seconds (default 120, `0` disables it
for an interactive debugging session) it prints the phase, the virtual clock,
the scheduler task table and a native backtrace of every registered thread,
then exits non-zero.

A fatal fault is the other thing no virtual-time bound can see. `SIGSEGV`,
`SIGBUS`, `SIGILL` and `SIGFPE` are caught, print `SIM CRASH:` with the
scenario line being executed, the boot phase, the faulting thread and a native
backtrace (`-rdynamic` is on), and are then re-raised so the process still ends
with the real fatal status. Set `FURBLE_SIM_CRASH_STEP=<index>` to fault
deliberately at a script step; that is the self test for the reporter and has
no other use.

`FURBLE_SIM_SDL_STEP_DETECT=1` restores the M5GFX debugger-detector thread,
which is off by default. The detector infers that a debugger has stopped the
process when a 1 ms `SDL_Delay` overshoots 64 ms, which a loaded host does
routinely, and the step-exec mode it then latches deadlocks simulator boot
against the SDL render pump. Turn it on only under an actual debugger.

`sim/scripts/run-e2e.sh` and `sim/scripts/run-watchdog.sh` bound every scenario
with `timeout -k 10` at `FURBLE_SIM_SCENARIO_TIMEOUT` seconds, default 300. The
per-seed bound in `run-fuzz.sh` is separate and larger, `FURBLE_FUZZ_SEED_TIMEOUT`
seconds, default 600, because one seed is 600 events rather than one scenario. A
wedged run fails its leg instead of hanging the job. All three scripts require
GNU `timeout`, or the coreutils `gtimeout` Homebrew installs on macOS, and fail
with that instruction if neither is present.

For an instrumented UI-fuzzer build, set `FURBLE_SIM_SANITIZE` to a clang
sanitizer list and use a separate `FURBLE_SIM_BUILD_DIR`, for example
`FURBLE_SIM_SANITIZE=address,undefined`.

`FURBLE_SIM_COVERAGE=1` builds with clang source-based coverage instead. Only
firmware translation units are instrumented, so LVGL and M5GFX stay at full
speed. Each run writes a raw profile to the path in `LLVM_PROFILE_FILE`. Use a
separate `FURBLE_SIM_BUILD_DIR` here too. `tools/coverage.py` drives all of
this; see [coverage.md](coverage.md).

Both flags shape the objects, so `build.sh` stamps them in
`$FURBLE_SIM_BUILD_DIR/build-flags` and drops the object cache when they change.
Reusing one build directory with different flags therefore rebuilds rather than
mixing objects.

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
| `restart` | Reboots the simulated device: the simulator shuts down in order, re-executes itself, and resumes the script at the next step. RAM state is wiped like an esp_restart(); the per-run NVS preferences file is inherited through `FURBLE_SIM_PREFS` and persists like flash. Seeds are reapplied on the resumed boot. Takes no arguments and must not be the final step. |
| `action` | `action COMMAND` invokes one of the simulator actions below. The complete action line is parsed once, with whitespace-tolerant tokenization, strict arity, finite numeric validation, and no silently ignored trailing values. Invalid actions fail during script loading with status 2. |
| `print` | `print KEY` prints the resolved scenario query. |
| `assert` | `assert KEY VALUE` aborts with exit status 1 when the resolved value differs. |
| `assert-eventually` | `assert-eventually TIMEOUT_MS KEY VALUE` polls the resolved value using a monotonic wall-clock timeout while yielding to background simulator tasks. TIMEOUT_MS must be 1 through 60000; a timeout reports the last value and exits 1. |
| `assert-eventually-virtual` | `assert-eventually-virtual TIMEOUT_MS KEY VALUE` polls once per normal UI tick while virtual time, platform updates, and background tasks continue. TIMEOUT_MS must be 1 through 60000 virtual milliseconds; a timeout reports the last value and exits 1. |
| `xassert` | `xassert KEY VALUE` records `XFAIL (WILL_FAIL)` on a mismatch and continues. A match prints `XPASS` and FAILS the run, so a closed gap is promoted back to `assert` deliberately. `xassert board-varies KEY VALUE` is the exception for a gap already closed on some panels: a match there prints and continues. |
| `exit` | Ends the simulator with status 0. |

`assert`, `assert-eventually`, `assert-eventually-virtual`, `xassert`, and
`print` use the same query namespaces:
`ui.*`, `control.*`, `camera.*`, `gps.*`, `uart.*`, and `setting.*`.

The `ble_peers` seed selects the virtual radio topology. Its peers advertise to
the production `Scan` and answer the production `Camera` connect paths, so the
`scan-distinct-rows-heartbeat.txt` scenario now proves that two real
advertisements are matched by `CameraList::match` and drained on the UI task
while the watchdog remains armed.

The `clock.ms` query reports the current virtual millisecond clock.

### Effective `seed` names

These byte settings are applied before the UI is constructed:
`brightness`, `inactivity`, `display_off`, `gps_rate`, `gps_constel`,
`gps_power`, `gps_duty`, `gps_hold`, `cpu_freq`, `tx_power`, `scan_mode`,
`text_size`, `auto_off`, `low_batt`, `fb_output`, and `imu_wake` (0 off, 1 tap,
2 shake, 3 both).

`clock_ms` seeds the simulator's uint32 millisecond clock before platform
initialization. It is intended for deterministic wrap-boundary scenarios.

Battery seeds select the initial deterministic platform sample:
`battery_level` (0 to 100), `battery_voltage` (millivolts),
`battery_current` (signed milliamps), and `battery_charging` (`true` or
`false`).

These boolean settings are applied before the UI is constructed:
`gps`, `gps_nmea`, `fauxny`, `autoconnect`, `reconnect`, `recon_backoff`,
`sleep_conn`, and
`boot_splash`, `gps_extrap`, `sd_gpx`, `imu`, and `imu_trigger`. `auto_off_charging` opts into auto-off while charging, and
`imu_sensor` controls modeled IMU presence. The M5StickS3 model also accepts
`watchdog`; other board models reject that seed because they cannot apply it.
`scan_timeout` seeds the discovery scan timeout in seconds; the default 0 scans
until the page is left, so a scenario that asserts a scan-end callback must
seed a bounded value.

`ble_peers` selects the virtual BLE radio topology from a strict allowlist:

| Topology | Peers |
| --- | --- |
| `none` | no peers (the default) |
| `fuji` | one healthy Fujifilm Basic camera |
| `fuji-secure` | one healthy Fujifilm Secure camera, which advertises a serial |
| `fuji-pair` | two healthy Fujifilm Basic cameras |
| `fuji-ricoh-flappy` | one healthy Fujifilm plus a Ricoh GR IV in BLE standby that fails one security handshake the way a supervision timeout does (rc=520) before letting a connect through |
| `fuji-secure-stale` | one Fujifilm Secure body that the central is still bonded to and that no longer holds the pairing, so every security handshake times out and takes the link with it (the 2026-09-02 X100VI bench signature) |

`ble_saved true` persists the topology's cameras through the production
`CameraList::match` and `CameraList::save`, so the scenario boots with saved
cameras exactly as a device does after the user scanned and connected once.

`ble_max_clients` caps the mock NimBLE client pool the way the board's
`CONFIG_BT_NIMBLE_MAX_CONNECTIONS` caps it, which is 9 on every furble board.
The pool is unlimited by default, so a client leaked once per connect cycle is
invisible; on the device it ends the session for good, because
`NimBLEDevice::createClient()` starts returning nullptr and every later connect
fails until a reboot. `ble_client_selfdelete` models NimBLE freeing a
self-deleting client when its disconnect fires, which is what
`Camera::connect()` arms on a live session. Both are off by default, deliberately: turning them on globally
would change client lifetimes under every existing scenario at once, and two
shapes report a leak that is the model's and not the firmware's (a leg that
severs the link, and any FauxNY leg, which has no radio to deliver the GAP
disconnect its client's self-delete waits on). A scenario that walks many
connect cycles against a peer-backed camera turns both on and bounds
`ble.live_clients`. One limit: the mock frees a self-deleting
client of a link-loss disconnect only when `reapDeferredClients()` is pumped
from a quiescent point, and the simulator has no such point, so the pool guard
is sound on Camera-driven teardowns and not on `action drop`.

`secure_stall_ms` holds every Fujifilm peer inside its security handshake for
N virtual milliseconds (`seed secure_stall_ms N`). `NimBLEClient::secureConnection()` is the one wait in a
Fujifilm Secure connect that takes no cancel token, and `Camera::connect()`
holds `Camera::m_Mutex` across it, so an attempt parked there is uncancellable
and a target task's `Camera::disconnect()` blocks behind it. The virtual peers
answered that call in under a millisecond, so before this seed no scenario could
cancel into a live connect at all. The stall ends early once the link goes down,
which is how a supervision timeout or a central-side terminate releases the real
call. 3500 is the bench signature of a healthy X100VI Secure connect; 32000 is
the link supervision timeout bound that releases a camera which never finishes
the encryption procedure. The default 0 keeps the instant handshake every other
scenario is timed against. The knob is the peer's
`FujifilmVirtualCamera::setSecureConnectionStallMs()`, shared with the host
suite, which spends real milliseconds where the simulator spends virtual ones.

The scenario-only settings are `saved_camera`, `connect_fail`, `no_touch`,
`scan_start_probe`, `ble_saved`, `liveness_check`, and
`liveness_grace_ms`. `saved_camera` adds an
inactive saved camera, `connect_fail` registers one virtual Fujifilm peer and
makes `NimBLEClient::connect()` fail at the transport (FauxNY has no radio to
fail at), and
`no_touch` selects the physical-button layout. `liveness_check
false` opts a scenario out of the continuous liveness invariant enforcement
(detection still counts violations), and `liveness_grace_ms` overrides the
3000 ms divergence grace period. The interval settings are `interval_count`, `interval_delay`,
`interval_shutter`, and `interval_wait`; `bulb_duration` seeds the bulb timer.
`gps_stationary` selects the stationary canned NMEA track instead of the
moving one. Both report the same position, but the stationary track reports
0.412 knots rather than 22.678, which is below the speed floor fix hold
extrapolation needs. It is the seed a scenario uses to prove that a parked user
is never dead reckoned.

`gps_uart_mode` selects `ack`, `nack`, `timeout`, `malformed`, `partial`,
`write-error`, or `pause` before the GPS task starts.

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
action ble-kill
action ble-standby
action ble-connect-fail
action ble-connect-ok
action ble-withhold-registration
action ble-allow-registration
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
action imu.enable
action imu.disable
action imu.accel.fail
action imu.accel.recover
action imu.gyro.fail
action imu.gyro.recover
action invalidate.reset
action select
action bulb-stop
```

The parameterized action forms are:

```text
action toggle NAME
action nav PAGE
action scroll top|bottom|next|PIXELS
action page PAGE
action imu.accel X Y Z
action imu.gyro X Y Z
action imu.roll DEGREES
action imu.pitch DEGREES
```

`toggle NAME` accepts `gps`, `gps_nmea`, `autoconnect`, `reconnect`,
`multiconnect`, `companion`, `watchdog`, `ir`, `show_title`, `tx_adaptive`,
`conn_saver`, `preset_picker`, and `recon_backoff`.

`nav PAGE` accepts `connect`, `scan`, `delete`, `power_off`, `bulb_duration`,
`bulb`, `settings`, `display`, `features`, `sensors`, `gestures`, `infrared`,
`gps_rate`, `gps_sentences`, `gps_constellation`, `gps_power`, `gps_assist`,
`gps_hold`, `gps`, `gps_data`, `nmea`, `timer`, `theme`, `text_size`,
`bluetooth`, `tx_power`,
`about`, `power`, `feedback`, `feedback_events`, `feedback_volume`,
`diagnostics`, `device_info`, `power_state`, `ble`, `interval_count`,
`interval_delay`, `interval_shutter`, `interval_wait`, `battery`, `storage`,
`imu`, `level`, and `level_main`.

`page PAGE` accepts `main`, `menu`, `connect`, `scan`, `delete`, `power_off`,
`connected`, `ir`, `shutter`, `bulb`, `bulb_duration`, `bulb_run`, `cameras`,
`remote_timer`, `remote_gps`, `remote_disconnect`, `timer`, `timer_run`,
`settings`, `display`, `features`, `sensors`, `infrared`, `gps_rate`,
`gps_sentences`, `gps_constellation`, `gps_power`, `gps_assist`, `gps_hold`,
`gps`, `gps_data`, `nmea`, `theme`, `text_size`, `bluetooth`, `tx_power`,
`about`,
`power`, `feedback`, `feedback_events`, `feedback_volume`, `storage`,
`diagnostics`, `device_info`, `battery`, `power_state`, `ble`, `interval_count`,
`interval_delay`, `interval_shutter`, and `interval_wait`.

The battery action changes the platform sample at runtime:

```text
action battery LEVEL VOLTAGE_MV CURRENT_MA CHARGING
```

For example, `action battery 80 4000 0 false` simulates a recovered,
discharging pack. The next battery timer sample consumes the new value.

`action drop` severs the live link of every control target. `action drop N`
drops target `N`, using zero-based target numbering. `action connect-two`
selects two cameras for multi-connect coverage: the seeded virtual peers when a
topology is seeded, otherwise the FauxNY test cameras. `action companion-pair-request` injects a
pending companion PIN without a rig TCP peer; `action companion-accept` and
`action companion-reject` click the real pairing dialog buttons.

`watchdog` is present in the M5StickS3 build. The lists above are the canonical
toggle, navigation, and page vocabularies.

Actions are parsed into a canonical typed representation before SDL, UI, or
worker startup. Runtime dispatch uses that representation and reports one of
four outcomes to its caller: `APPLIED` after successful handling,
`VALID_NO_EFFECT` for an intentional no-op, `UNAVAILABLE` when a valid route or
capability is absent on the modeled board, and `INVALID` for malformed or
unhandled input. A plain `action COMMAND` requires `APPLIED`; it fails the
scenario if the action has no effect or is unavailable. To assert an intentional
alternative, use `action expect no-effect COMMAND` or `action expect unavailable
COMMAND`. `action expect applied COMMAND` is accepted for clarity. A queued UI
action owns its input and result state, including when the UI request times out.
The root aliases `page main` and `page menu` are both valid and select the root
menu.

The `scroll` action accepts `top`, `bottom`, `next`, or a signed pixel count in
the inclusive range -2147483647 through 2147483647. `-2147483648` is rejected:
the runtime negates pixel counts to form LVGL's signed int32 scroll delta, so
that value cannot be represented safely.
The connected page map includes the run and disconnect transition pages listed
in the canonical `page PAGE` vocabulary above.

### Query keys

The complete `ui.*` query set is:

| Query | Returned value |
| --- | --- |
| `ui.connect_box` | `hidden` or `visible`. |
| `ui.connect_error` | `none`, or the dismissable connect error box's title as one lowercased token: `already_saved`, `pairing_lost`, or `connect_failed`. |
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
| `ui.row_text` | Text of the focused menu row, or `none`. Every whitespace character is reported as an underscore, so a literal underscore and a space both read as `_`. |
| `ui.row_scrolling` | `yes`, `no`, or `none`. Whether the focused menu row's label is running LVGL's scroll animation. |
| `ui.overflow` | `unknown`, `yes`, or `no`. |
| `ui.nav_layout` | `touch` or `buttons`. |
| `ui.indicator_clearance` | `clear`, `overlap`, or `n/a`. |
| `ui.indicator_overlaps` | Numeric count of widgets under an indicator. |
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
| `ui.gps_fix_state` | Rendered fix hold row: `hidden` while fix hold is off, otherwise `live`, `held`, or `searching`. |
| `ui.gps_hold_remaining` | Rendered seconds left on a held fix, `none` in any other state. |
| `ui.gps_extrap_enabled` | `yes` or `no` for the Extrapolate switch, which is greyed out until fix hold is set. `none` off the GPS settings page. |
| `ui.gps_source` | Rendered fix source, `uart`, `comp`, `none`, or `none` when the row is absent. |
| `ui.gps_link_age` | Rendered sentence age exactly as drawn, such as `3s`, `17m`, `99m+`, or `n/a`. |
| `ui.gps_cycle` | Rendered power cycle state, such as `waiting` or `degraded`. |
| `ui.gps_retries` | Rendered degraded retry count, `0` when the row shows none, `none` when the row is absent. |
| `ui.focus_outline_count` | Numeric outline count, or `none`. |
| `ui.lock_outline` | Numeric outline width, or `none`. |
| `ui.link_alert` | `yes` or `no`. |
| `ui.page_banner` | `none`, `yes`, or `no`. |
| `ui.bt_icon` | `hidden`, `red`, or `plain`. |
| `ui.battery_x` | Numeric header x position, or `none`. |
| `ui.battery_drift` | Numeric x delta from the first read, or `none`. |
| `ui.low_battery` | `none`, `warn`, or `power_off_pending`. |
| `ui.liveness_violations` | Numeric count of continuous liveness invariant firings. Restarts at zero on a boot resumed by `restart`, with the rest of RAM. |

`ui.gps_source` and `gps.source` deliberately report different vocabularies.
`gps.source` is `GPS::sourceName()`, the receiver's own state: `uart`,
`companion` or `none`. `ui.gps_source` is what the GPS Data page drew, which is
`GPS::sourceShortName()`: `uart`, `comp` or `none`. The page row is budgeted to
fourteen characters and "companion" does not fit. Assert `gps.source` for what
the receiver is doing and `ui.gps_source` for what the user sees.

`ui.gps_link_age` returns the age token verbatim, unit and all, so a unit or
clamp regression fails the assertion. It still parses as a leading integer, so
`assert_min` and `assert_max` work within one unit. The minute and saturation
clamps are not reachable from a scenario, because the degraded retry re-sends
its configuration and any received byte refreshes the tick; they are unit tested
in `tests/host/gps_format_test.cpp`.

The other namespaces are:

- `platform.battery.level`, `platform.battery.voltage`,
  `platform.battery.current`, and `platform.battery.charging` report the
  current deterministic platform sample.
- `platform.power_off` reports `yes` after production `UI::doPowerOff()` reaches
  the simulator power-off seam. The simulator records the request instead of
  terminating the process, so the scenario can assert shutdown ordering.
- `platform.download_lock` reports the StickS3 PMIC long-press download lock as
  `unlocked` or `locked`. Firmware boot is required to leave this `unlocked`.

- `control.state`: `idle`, `connect`, `connecting`, `connect_failed`,
  `active`, `disconnecting`, or `unknown`.
- `control.connected` and `control.targets`: numeric target counts.
- `control.zombies`: number of quarantined targets still draining. A teardown
  that never drains leaves this above zero.
- `control.connect_in_progress` and `control.connect_abort`: `yes` or `no`.
  The 2026-08-28 multi-target wedge was diagnosed from exactly this pair
  (`control.state disconnecting` with `control.connect_in_progress yes`).
- `control.reconnect_attempt`: number of reconnect retries already performed,
  which walks the `ReconnectBackoff::delayMs()` curve.
- `control.reconnect_backoff` and `control.infinite_reconnect`: `yes` or `no`.
- `ble.secure_stall_aborted`: `yes` once a Fujifilm peer's modelled security
  handshake has ended on a link terminate rather than on its own deadline. This
  is provenance a cancel bound cannot give: a bound says the cancel was quick,
  not that it was the cancel that ended the handshake.
- `ble.live_clients`: number of NimBLE clients the mock currently holds. With
  `ble_max_clients` and `ble_client_selfdelete` seeded this is the client-leak
  guard: a session holds one, and anything left over after a settled teardown is
  a client the firmware did not reclaim.
- `control.connecting_camera`: name of the camera currently being connected.
- `camera.count`: numeric camera-list row count, useful for scan de-duplication
  assertions.
- `gpx.points`: track points the firmware has queued for the SD writer. A held
  GPS fix reaches the camera but must never reach the track log, so a scenario
  proves that split by holding this still while `camera.geo_count` climbs.
  Needs `sd_gpx` seeded and the `sd` capability.
- `camera.geo_count`, `camera.geo_lat_e5`, `camera.geo_lon_e5`, and
  `camera.geo_utc_s`: the geotag that last reached the simulated camera through
  the production GPS to camera path. Coordinates are in units of 1e-5 degrees
  and the timestamp is seconds since midnight UTC, both integers, so
  `assert_min` and `assert_max` can bound them. Fix hold exists so these keep
  arriving after the receiver loses its fix, so this is where a hold scenario
  asserts the far end rather than inferring it from the GPS Data page.
- `scan.end_callbacks`: numeric count of scan-end callbacks delivered by the
  current scan. A bounded discovery scan should deliver exactly one callback.
- `scan.advertisements`: number of advertisements the virtual radio has
  delivered.
- `ble.peers`: number of virtual peers registered by the seeded topology.
- `camera.shutter_presses`, `camera.shutter_releases`, `camera.focus_presses`,
  and `camera.focus_releases`: numeric counts of the camera commands that
  reached a per-target camera task.
- `setting.text_size`: the persisted numeric text-size setting.
`ui.nav_layout` reports which navigation layout the running build rendered:
`touch` for the touch grid, `buttons` for the physical-button layout. A scenario
that means to measure a board's shipped layout asserts this first, so a lost
`no_touch` seed fails loudly instead of passing against the wrong layout.

`ui.indicator_clearance` reports whether any visible content widget on the
current page sits under a navigation indicator: `clear`, `overlap`, or `n/a` on
a build with no indicators. `ui.indicator_overlaps` reports the count for
diagnosis. Labels, images, rollers, switches, sliders, checkboxes and bars are
measured; containers are not, because a flex row spans the page by construction.
A label box is stretched by its row while the glyphs occupy only part of it, so
only the drawn text extent counts, with the text alignment honoured. Areas are
clamped to the page viewport, so rows scrolled out of sight are not counted. The
viewport ends above the reserved navbar band, so the query cannot reach the
bottom-edge indicators: it measures the indicators that float over content, and
reports nothing about the bottom two rather than proving them clear.

- `setting.fauxny`, `setting.autoconnect`, `setting.reconnect`,
  `setting.multiconnect`, `setting.companion`, `setting.watchdog`,
  `setting.gps`, `setting.gps_nmea`, `setting.ir`, `setting.conn_saver`,
  `setting.preset_picker`, `setting.show_title`, `setting.tx_adaptive`, and
  `setting.recon_backoff`: `1` or `0`. `setting.watchdog` is in the
  M5StickS3 build.

## Fault injection and fuzzing

### SDL simulator faults

All camera-link faults are now transport faults on the real MockNimBLE link
behind a production `Camera`. Nothing overrides the control state machine, so
whatever the production stack does after a fault is what the scenario observes.

- `seed connect_fail true` registers one virtual Fujifilm peer, saves it, and
  makes `NimBLEClient::connect()` fail. Combine it with `action connect` to
  exercise the connect-failed UI path.
- `action drop` severs the live link of every control target with the GAP
  disconnect delivered, which is how a supervision timeout is reported.
  `action drop N` drops one zero-based target. With `seed reconnect true` the
  control state re-enters `connecting`; without it, the state returns to
  `idle` when no other link remains. `action connect-two` keeps a surviving
  link active while one target is dropped. A FauxNY camera has no radio, so
  the equivalent observable (`Camera::resetConnectionState()`) is used for it.
- `action ble-kill` severs every live link and leaves the GAP disconnect event
  queued, so the camera keeps reporting connected over a link that is
  physically gone. This is the one fault the liveness invariant cannot see,
  because `Camera::isConnected()` is still true; see the residual gaps in
  `plans/161-sim-real-control.md`.
- `action ble-standby` runs the standby drop of every flappy virtual peer: the
  peer re-arms its handshake failure budget, announces its power state (a
  Ricoh sends CameraPower 0x00, Fujifilm is silent) and severs the link. The
  simulator schedules it from the scenario rather than from the peer's
  wall-clock timer so it lands at a known virtual time.
- `action ble-connect-fail` and `action ble-connect-ok` toggle transport-level
  connect failure at runtime, which is how a scenario holds a session in the
  reconnect backoff long enough to assert the curve.
- `action ble-withhold-registration` and `action ble-allow-registration` make
  every Fujifilm peer answer the link and every GATT operation but never
  confirm registration, so the production connect blocks in its registration
  wait. That is a camera sitting in its own settings screen.
- These actions require `seed ble_peers <topology>`; the scenario parser
  rejects them otherwise rather than letting them be silent no-ops.
- Every scripted scenario runs a continuous liveness invariant on each driver
  tick: if the UI presents the Connected screen (the `ui.connected` three-way
  check) while fewer camera links are actually up than the session has
  targets, and the divergence outlives the grace period (default 3000 ms,
  override with `seed liveness_grace_ms N`), the run fails with `LIVENESS
  INVARIANT FAILED`. `seed liveness_check false` opts a scenario out of the
  failure; detection still increments `ui.liveness_violations` so an opted-out
  scenario can assert the invariant would have fired.
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
  simulator; a scripted run overrides it with a per-run path
  `.pio/furble-sim-preferences-<scenario>-<pid>.bin` and removes it again on an
  orderly exit. One flash image per simulated device: keying it on the scenario
  alone let two simulators running the same script from one working directory
  erase each other's flash at boot (issue #284).
- `FURBLE_SIM_RESTART_STEP` is set by the `restart` step for the process it
  re-executes, and nothing else should set it. The resumed boot consumes it,
  unsets it, and keeps the `FURBLE_SIM_PREFS` store it inherited rather than
  wiping a fresh one, so the reboot reads the flash the previous boot wrote. It
  lives exactly one boot; a value outside the script's step range fails the run
  with status 2.
- Battery policy tests should seed `low_batt` and the four battery fields, then
  use `action battery ...` to change the sample. Six consecutive low samples
  qualify the production 30-second hysteresis; charging suppresses both the
  warning and power-off path.

### IMU injection

The simulator exposes the same accelerometer and gyro read surface used by the
firmware. It does not emulate BMI270 timing or I2C faults, but it does run the
production level filter, orientation thresholds, diagnostics polling, and UI
layout. Use these deterministic controls in scenarios:

- `seed imu true|false` selects the startup setting. The default is `false`.
- `action imu.enable` and `action imu.disable` toggle the simulated sensor.
- `action imu.accel X Y Z` injects an accelerometer vector in G.
- `action imu.roll DEGREES` and `action imu.pitch DEGREES` inject orientation.
- `action imu.gyro X Y Z` injects gyro data in degrees per second.

The `imu.*` actions feed the shared simulated `M5.Imu` surface. Queries include
`imu_accel_x`, `imu_accel_y`, `imu_accel_z`, `level_bubble_x`,
`level_bubble_y`, `level_rotation`, `level_side_visible`, `level_root_width`,
`level_root_height`, `level_side_on_screen`, and `ui.overflow`. The root-size
and on-screen queries verify that a landscape panel resize reaches the
pixel-sized top-level window and that the moving tube bubble is not clipped.
Checked-in scenarios cover the setting gate, live diagnostics, portrait and
rotated level layouts, redraw stability, and overflow on all three panel sizes:
`e2e/imu-gating.txt`, `e2e/imu-diagnostics.txt`, `e2e/level-spirit.txt`,
`e2e/level-overflow.txt`, and `e2e/redraw-steady.txt`.

### IMU gestures

The gesture detector is the same `Furble::GestureDetector` the firmware runs;
the simulator feeds it through the shared `M5.Imu` seam with `imu.accel`, so a
scenario measures the production state machine rather than a stand-in. Seed the
two settings with `imu_wake` (0 off, 1 tap, 2 shake, 3 both) and `imu_trigger`
(boolean). Both default off, which is master behaviour: no detector is
constructed and no accelerometer is read.

Query keys, all `FURBLE_SIM` only: `ui.gesture_timer` (`yes`/`no`, whether the
50 Hz poll timer exists), `ui.gesture_period_ms`, `ui.gesture_events` (gestures
accepted by the UI, which is not the same as shutter frames, so a swallowed or
wake-only gesture is still observable), `ui.gesture_last` (`none`, `tap`,
`double_tap`, `shake`) and `ui.display_state` (`on`/`dim`/`off`, the value
production already feeds the power profiler).

At 50 Hz a tap is one sample: hold the high value for 20 ms, then release to
baseline. Two consecutive high samples is a shove and three is a shake, so an
impulse longer than 40 ms is classified as a shake by design.

Checked-in scenarios: `e2e/imu-gesture-detect.txt` (tap, refractory, shake,
walking, table bump), `e2e/imu-gesture-doubletap.txt` (the 80 to 400 ms
window's boundaries), `e2e/imu-gesture-wake-tap.txt`, `-wake-shake.txt` and
`-wake-off.txt` (the wake masks, driven through the real display-off path with
`inactivity` and `display_off` seeds), `e2e/imu-gesture-shutter.txt` and
`-shutter-blocked.txt` (one gesture is one frame; disconnected, wrong page and
a running intervalometer each block it), `e2e/imu-gesture-defaults.txt`
(defaults change nothing, including the invalidation count),
`e2e/imu-gesture-disabled.txt` and `e2e/imu-gesture-gating.txt` (the IMU gate
and live sensor loss, run on all three panels).

`sim/scenarios/gesture-idle-30s.txt` is the power baseline with the detector
on. The 50 Hz timer costs real light-sleep residency in the model, so this
scenario is what stops a future rate change from going unnoticed.

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
   after virtual time has advanced. Use `assert-eventually-virtual` when the
   observed device timeout itself must continue advancing. Keep exact
   count/value assertions after the eventual state assertion.
   CI repeats the asynchronous GPS scenarios through
   `sim/scripts/run-async-stress.sh`; keep cross-task regressions in that set.
3. Prefer `assert` for a fixed contract. Use `xassert` only for a known gap
   that is expected to fail today. It records XFAIL and continues; an XPASS
   fails the run, which is the signal to promote the line to `assert` after the
   product fix. For a gap already closed on some panels and open on others, use
   `xassert board-varies KEY VALUE`, which prints on a match instead of failing.
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
xassert board-varies ui.page main
print ui.page
exit
```

The first line prints XFAIL and continues, the second prints XPASS
(board-varies), and the process exits 0. A plain `xassert ui.page main` would
print XPASS and exit 1 instead, because a closed gap must be promoted.
