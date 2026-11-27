# 336 - GPS UART pin seam

Status: implemented on `feat/gps-profile-pins`, rebased onto master at
`b711a44a`, not merged. No new board and no support claim in this work. Two shipped boards stop claiming a pin they
never could use (the Core and the Waveshare ESP32-S3-ETH, below); every other
image keeps the pins it already used. This is the first step of the
[322](322-hardware-abstraction.md) port sequence.

Related: [322](322-hardware-abstraction.md) (board profile, port stack),
[318](../318-next-board-battery-gps.md) (next board for GPS), [287](../287-mcu-portability-and-bundle-hardware.md)
(board survey).

## The defect

`src/FurbleGPS.cpp` installed the GPS UART from the M5Unified pin table
directly:

    const int8_t tx = M5.getPin(m5::port_a_pin2);
    const int8_t rx = M5.getPin(m5::port_a_pin1);

That keeps the GPS path tied to M5Unified, which is exactly the coupling plan
322 removes. A board M5Unified does not know has no `board_t` row, so the table
cannot describe its Port A: the lookup reports the `board_unknown` row instead,
which for an ESP32 image is 33/32, the pins of the unsupported Module GPS v2.1.
The T-Beam Supreme, the first board on the 322 list, runs the GNSS UART on RX 9
and TX 8 on a different SoC die and never on Port A, so
[322](322-hardware-abstraction.md) step 3 ("Add GPS UART pins (RX 9, TX 8) to
the row") had nowhere to put those numbers. A third board family has no M5
Port A at all and must be able to say "no GPS UART".

The same lookup is also a live defect on two shipped boards, because Port A is
not always free. Both are board-level findings from an independent pass; the
M5Unified line numbers below are the evidence.

### The Core: Port A is the internal I2C bus

M5Unified's `board_M5Stack` row gives `GPIO_NUM_22,GPIO_NUM_21` for both the
internal and the external column pair (`M5Unified.cpp:139`), so `Ex_I2C`
is the same bus as `In_I2C`; `_setup_pinmap`'s caller even keeps one port when
the two pairs are equal (`M5Unified.cpp:1868-1871`). The IP5306 (0x75) and
the MPU6886 (0x68) sit on that bus. `GPS::init()` runs on this board, so
`uart_set_pin(UART_NUM_2, 21, 22, ...)` re-routed both pads and every
`M5.Power.getBatteryLevel()` and IMU read failed for the rest of the boot.
`UI::addGPSMenu()` exposes the GPS setting with no board gate
(`src/FurbleUI.cpp:9868-9981`), so nothing prevented it.

There is no UART pair on Port A on the Core: the port is the bus. The row
therefore names `GpsPinSource::NONE`, and the GPS module installs no driver.

### The Waveshare ESP32-S3-ETH: Port A is the W5500 bus

The Waveshare image reports `board_M5StampS3`, the only image that does, so
M5Unified resolves `GPIO_NUM_15,GPIO_NUM_13` (`M5Unified.cpp:93`).
`ETH_SPI_SCLK_GPIO = 13`, `MOSI = 11`, `MISO = 12`, `CS = 14`,
`INT = 10`, `RST = 9` (`src/FurbleEthernet.cpp:131-136`, `sclk` applied at
`:153`). `GPS::init()` runs on this board too (`src/main.cpp:528`, the
`FURBLE_NO_DISPLAY` path), so `uart_set_pin(UART_NUM_2, 13, 15, ...)` rewrote
the GPIO13 pad output selection after `spi_bus_initialize` and silently killed
the W5500 clock.

A new row keyed on `m5::board_t::board_M5StampS3` names
`GpsPinSource::NONE`; every non-GPS column copies the unknown row, because the
node has no battery, PMIC, IMU, IR or SD slot. `board_M5StampS3` must not get
`BOARD_SUPPORT` just because M5Unified reports the id: the id is what M5GFX was
pinned to for its panel profile, not a claim that Port A is spare. A future
bare StampS3 image fails closed here until it names its own source.

### Does the Plus2 share the Core's problem? No

The Plus2 has no row in the ESP32 `_pin_table_i2c_ex_in` block either, so it
resolves the `board_unknown` row (`M5Unified.cpp:147`): internal 22/21,
Port A 33/32. The pairs differ, so the Port A pads are not the internal bus,
and the row keeps `BOARD_SUPPORT`. The StickC and StickC Plus land on the same
row for the same reason. The Core2 and the Tough resolve 33/32 too, which is
their real Grove A port, so they stay on `BOARD_SUPPORT`.

## The seam

`include/FurbleBoard.h` gains a pin source and two pins per row:

    enum class GpsPinSource : uint8_t {
      BOARD_SUPPORT,  // M5Unified's pin table, M5.getPin(port_a_pin1/2)
      PROFILE,        // the row carries the pins
      NONE,           // the board has no GPS UART
    };

    GpsPinSource gpsPinSource;
    int8_t gpsRxPin;  // PROFILE source, -1 when none
    int8_t gpsTxPin;  // PROFILE source, -1 when none

and one resolver, defined once and used by the GPS path:

    bool gpsPins(const Profile &row, int8_t &rx, int8_t &tx);
    bool gpsPins(int8_t &rx, int8_t &tx);  // row of the running board

`BOARD_SUPPORT` performs the M5Unified lookup, `PROFILE` reads the row, and
`NONE` returns false. Both outputs are set to -1 before the switch, so a false
return and a missing pin are the same value. The M5Unified call now lives in
`src/FurbleBoard.cpp` only, next to `profile()`, which the coupling ratchet
already treats as an adapter.

The M5Unified 0.2.20 pin table reports a missing pin as 255 in its `uint8_t`
row, and `_setup_pinmap()` copies those bytes into an `int8_t` table, so
`getPin()` returns -1 for a board with no entry. The -1 convention of the
profile is therefore the same value, not a translation.

`src/FurbleGPS.cpp` calls `Board::gpsPins(rx, tx)` before it installs
anything. On `BOARD_SUPPORT` and `PROFILE` it installs the driver exactly as
before: same `uart_driver_install`, `uart_param_config`, `uart_set_pin`,
pattern interrupt, queue reset and flush order. An unknown board still reaches
`uart_set_pin` with -1 as it did, because a missing pin is -1, not a refusal.

On `NONE` it installs nothing. `getInstance()` returns before the UART driver,
the pattern interrupt and the task exist, logs "No GPS UART on this board."
once, and stores `receiver_state_t::ABSENT`, which the console (`gps status`),
the UI diagnostics and the simulator already report as "absent". No pin, rail
or baud is touched, so a board that fails closed cannot disturb another
peripheral. `enable()` and `disable()` return early while the task is missing,
so the stored GPS setting cannot make a receiver appear or wind down a driver
that was never installed; the state stays `ABSENT`. `wakeGPSService()` already
tolerated a null queue, and `serviceSerial()`, the only UART reader, is gated
on `m_Enabled` and not `ABSENT`, so even a stray task pass returns early.

Removing the direct lookup also removed the `<M5Unified.h>` include from that
file.

## M5Unified 0.2.20 evidence

Pinned version from `.pio/libdeps/m5stick-s3/M5Unified/library.json` (line 19,
`"version": "0.2.20"`). `port_a_pin1` and `port_a_pin2` are aliases of
`ex_i2c_scl` and `ex_i2c_sda` (`M5Unified.hpp:30-31`), the third and fourth
columns of `_pin_table_i2c_ex_in` (`M5Unified.cpp:86`). `_setup_pinmap()`
(`M5Unified.cpp:450`) walks that table to the row for the detected board,
or to the `board_unknown` row at its end, and copies the four pin bytes in
order.

The table is per SoC target, which is why no number can be hardcoded:

| Env | SoC target | Detected board | Row | Line | port_a_pin1 (RX) | port_a_pin2 (TX) |
| :--- | :--- | :--- | :--- | --: | --: | --: |
| `m5stick-s3` | ESP32-S3 | `board_M5StickS3` | `M5StickS3` | 92 | 10 | 9 |
| `m5stack-core` | ESP32 | `board_M5Stack` | `M5Stack` | 139 | 22 | 21 |
| `waveshare-s3-eth` | ESP32-S3 | `board_M5StampS3` | `M5StampS3` | 93 | 15 | 13 |
| `m5stick-c` | ESP32 | `board_M5StickC` | `board_unknown` | 147 | 33 | 32 |
| `m5stick-c-plus` | ESP32 | `board_M5StickCPlus` / `Plus2` | `board_unknown` | 147 | 33 | 32 |
| `m5stack-core2` | ESP32 | `board_M5StackCore2` / `M5Tough` | `board_unknown` | 147 | 33 | 32 |

The S3 table's own `board_unknown` row is line 114 and reads 1/2; it is not
reached by any shipped image. The row text:

    92:  { board_t::board_M5StickS3    , GPIO_NUM_48,GPIO_NUM_47 , GPIO_NUM_10,GPIO_NUM_9  },
    93:  { board_t::board_M5StampS3    , 255        ,255         , GPIO_NUM_15,GPIO_NUM_13 },
    139: { board_t::board_M5Stack      , GPIO_NUM_22,GPIO_NUM_21 , GPIO_NUM_22,GPIO_NUM_21 },
    147: { board_t::board_unknown      , GPIO_NUM_22,GPIO_NUM_21 , GPIO_NUM_33,GPIO_NUM_32 }, // Core2,Tough,StickC,CoreInk,Station,StampPico

The board comes from `M5GFX::autodetect()`, which a single-board image pins
with `M5GFX_BOARD` (`patches/m5gfx_board.patch`), and `_check_boardtype()`
(`M5Unified.cpp:1433`) only replaces it when it is `board_unknown`. So the
four pinned images report the board in the table above, and the two autodetect
images (StickC Plus/Plus2, Core2/Tough) report the same `board_unknown` row
whichever of their boards is fitted. `m5stack-core2` and `m5stick-c-plus` are
unaffected by which model answers: no Core2, Tough, StickC Plus or Plus2 row
exists in the ESP32 `_pin_table_i2c_ex_in` block. What each row then does with
those pins is the profile's decision, below.

## Which board got which source

| Row | Source | Why |
| :--- | :--- | :--- |
| StickC, StickC Plus | `BOARD_SUPPORT` | M5Unified's `board_unknown` row gives Port A 33/32, internal I2C 22/21. Different pads, unchanged behaviour. |
| StickC Plus2 | `BOARD_SUPPORT` | Same row, same 33/32 against its own 22/21. Nothing was broken here, so nothing changes. |
| StickS3 | `BOARD_SUPPORT` | Its own row gives Port A 10/9, the port the GPS unit is wired to. |
| Core2, Tough | `BOARD_SUPPORT` | 33/32 is their real Grove A port, separate from the internal 22/21 bus. |
| Core | `NONE` | Port A is the internal I2C bus itself (22/21 on both column pairs). The IP5306 and MPU6886 live there, and the port has no second pair a UART could use. |
| Waveshare ESP32-S3-ETH (`board_M5StampS3`) | `NONE` | Port A is 15/13 and GPIO13 is the W5500 SPI clock. |

Every `BOARD_SUPPORT` row keeps its two row pins at -1, so the resolved values
are the ones the GPS task read before this change, including the 33/32
fallback on an unknown board. No row uses `PROFILE` yet; the T-Beam Supreme row
in [322](322-hardware-abstraction.md) is the first that will, and
`include/CLAUDE.md` now records that a new board uses `PROFILE`.

`FURBLE_SIM` links the real M5Unified host build. Whatever it reports for
Port A (its host `_setup_pinmap()` fills the table with 255 when
`M5UNIFIED_PC_BUILD` is set), the simulator's fake UART ignores pins, so the
simulated M5 boards behave as before. The simulator's own board selection is
described under test coverage.

## Behaviour change on the two closed boards

The Core and the Waveshare lose a GPS UART they could not use:

- The Core no longer re-routes 21/22, so `M5.Power.getBatteryLevel()` and the
  IMU keep working for the whole boot. The GPS menu still offers the setting,
  and the diagnostics report the receiver `absent`, which is the truth: there
  is no GPS port on this board.
- The Waveshare no longer touches GPIO13, so the W5500 clock survives boot.
  `gps status` and the WebUI/companion status report `absent`.

Neither board had a working receiver on those pins before, so no capability is
removed. A future board that wants GPS names `PROFILE` and its own pair.

## Overlap with the Waveshare board row

A separate change, `fix/waveshare-board-row` (plan 349), also gives
`board_M5StampS3` a row and a case, and it adds a `powerOffPath` column. That
row and column merged as #500, and #497 (`fix/core-family`, plan 354) then
rewrote every row of the table and merged as 535ad7d6. All three land on the
same table, so they have to end up as one row with every column set, never as
two rows or two `case` labels. The rebase onto 535ad7d6 resolved to that shape:

- The three GPS columns are appended after `gpsRail` at the end of the struct,
  and the Waveshare row sits after `TOUGH`, so `powerOffPath` (which follows
  `canPowerOff`) and the GPS columns do not overlap in the struct.
- The resolution keeps #497's rows verbatim, including the capacities it
  corrected (Core 110, Core2 500, Tough 390), the Tough speaker, SD slot and
  RTC alarm, and its power-off comments, and re-appends only the three GPS
  columns. There is one `WAVESHARE` row carrying `powerOffPath` false and
  `GpsPinSource::NONE`, and one `case m5::board_t::board_M5StampS3`.
- The Core row keeps `canPowerOff` false, which is the UI policy gate for its
  coarse IP5306 level and absent charging state, and `powerOffPath` true, which
  is the deferred cut #497 reconciled with #500. The GPS columns take neither
  side of that argument: the row simply claims no pin.
- `tests/host/board_profile_test.cpp` carries the same resolution in its
  `EXPECTED` table, so `board-profile` and `platform-power-off` pin both column
  sets at once.

## Test coverage

`tests/host/board_profile_test.cpp` (CTest `board-profile`) pins the three new
columns for all ten rows, including the Core and the Waveshare at `NONE`.

The harness sets the board through the boundary M5Unified shim
(`tests/host/boundaries/shims/M5Unified.h`), so the resolver is tested through
the production path:

- `gpsPins(rx, tx)` for every row with the shim's Port A set to 10/9, -1/-1 and
  22/21. A `BOARD_SUPPORT` row follows the shim, which proves it reads M5Unified
  rather than the row, and keeps a missing pin at -1. A `NONE` row returns
  false and leaves -1/-1 in both outputs for every one of those values, which is
  the Core and the Waveshare case: nothing the pin table says can make those two
  boards claim a pin.
- `PROFILE` is exercised on a copy of a shipped row with only the three GPS pin
  columns overridden, because no board ships that source yet. It returns the
  row's 9/8 while the shim reports 15/13, so the row wins.

The shim gained `port_a_pin1`, `port_a_pin2`, their values (default -1, appended
after the SD names so existing tests keep their enum values) and
`board_M5StampS3`. The host stubs that also compile `src/FurbleBoard.cpp`
(`time_keeper_stubs`, `settings_stubs`) gained the enum names and a `getPin`,
with `pin_name_t` unscoped like M5Unified's. Breaking any branch of the resolver
fails the test: swapping the two pins in each branch, and making `NONE` resolve,
were both checked.

## Simulator coverage

`sim/scenarios/waveshare-no-gps-uart.txt` (manifest entry: suite `power-gate`,
capability `gps`) seeds the Waveshare row and turns the GPS setting on:

    seed board stamps3
    seed gps true
    wait 5000
    assert gps.enabled 0
    assert gps.receiver absent

`seed board` is new: `FurblePlatformSim` supplies the strong definition of the
weak `furble_sim_board_profile()` hook that `Board::profile()` calls under
`FURBLE_SIM`, so a scenario can exercise a row the simulated build does not
detect. Without the seed the detected board is used, so every existing scenario
is unchanged. The two assertions are the contract this plan adds: the stored
enable state cannot make a receiver appear, and the receiver reports absent.

The scenario certifies the profile and GPS-state path. It does not certify that
a pin stayed untouched: the simulator's fake UART has no pads, so "no
`uart_set_pin` ran" is proved by code inspection and by the `NONE` row test,
not by a pad observation. A hardware check on the Core and the Waveshare (IMU
and battery after boot, W5500 link after boot) remains a device gate.

## The UI offers GPS from the setting, not the receiver

`GPS::isEnabled()` reports whether the receiver is running. On a `NONE` row it
is false for the whole boot: `getInstance()` installs nothing and `enable()`
returns early, so the stored setting cannot make `m_Enabled` true. Three UI
gates used that flag to decide whether to *offer* GPS surfaces, and on those
boards they hid all of them even with the setting on:

- `showGPSWidgets()` at menu construction (`addGPSMenu`) and in the
  `Settings::GPS` toggle callback, which owns every GPS settings row: baud,
  rate, sentences, constellation, power, assist, hold, extrapolate, platform,
  motion, and the GPS Data, NMEA and satellite sub-pages.
- The Connected page's GPS Data entry.

The first cut shipped the receiver flag there and the certified suite caught it.
`core-connected-grid-large.txt` failed with

    ASSERT FAILED: ui.visible_objects expected '25' got '22'

one entry (container, icon, label) short of "eight entries, eight cells",
because the eighth entry is GPS Data. The same cause failed 15 of the 51
certified Core bughunt scenarios: `core-notouch-layout` and the four
`gps-motion-row*` files reported `ui.gps_motion_row expected 'enabled' got
'hidden'` or `'disabled' got 'hidden'`, the six `gps-hold-rows-*` files hit
`Simulator action 'nav gps_hold' returned UNAVAILABLE, expected APPLIED`, and
`page-matrix` and `overflow-sweep` could not reach the GPS pages they must
sweep.
`page-matrix` is pinned to all three boards by `EXACT_BOARDS` in
`tools/check_sim_scenarios.py`, so re-scoping was not an option: the pages have
to stay reachable on a board with no UART. `connstate-page-sweep` passed
throughout because it reaches the Connected GPS Data page with
`action page remote_gps`, which does not go through the hidden entry.

This also contradicted the paragraph above ("The GPS menu still offers the
setting, and the diagnostics report the receiver `absent`"): the page carrying
that `absent` reading was the one being hidden. A Core owner with phone GPS over
the companion link had no route to the page at all, while
`UI::updateGpsIcon()` shows the status icon for a companion source precisely
because a session can have no wired receiver.

`src/FurbleUI.cpp` now has `gpsEnabledForUI()`, which reads
`Settings::load<bool>(Settings::GPS)`, and the three gates call it. It follows
`imuEnabledForUI()`: the persisted opt-in is read only while a menu is built or
redisplayed, never from a live timer. `GPS::isEnabled()` keeps its meaning for
the duty-cycle and rail policy, the status icon, the console and the companion
status, so `gps.enabled` stays 0 and `gps.receiver` stays `absent` on a `NONE`
row exactly as asserted above. The pages report the state instead of vanishing:
the GPS Data page's cycle row already renders `receiverStateName(ABSENT)`.

Result on the Core binary: 51 of 51 certified bughunt scenarios pass, and
`core-connected-grid-large.txt` reads `assert ok: ui.visible_objects = 25`
again. On the S3 and StickC the flag and the setting agree whenever a receiver
can run, so the change is a no-op there: the power gate's eleven existing
scenarios compare at +0.00% locally, `gps-duty-standby-cycle` apart, which sits
at -1.56% locally and +0.14% on Linux CI for the same binary, both inside the
10 percent band.

## Receiver scenarios leave the Core board matrix

Fifteen certified `e2e` scenarios drive the fake UART and assert receiver
behaviour: the autobaud ladder, the acquisition deadline
(`gps-fixed-baud-absent`), the ephemeris tiers, NMEA parsing, the satellite
pages, MON-HW, the motion detector and prearm, and the GPS time-zone source.
None of that can happen on a row that installs no driver, so the Core was never
going to see them pass again: `gps.receiver` stays `absent` where they expect
`present` or `detecting`, `gps.sats_in_view` stays 0, `gps.monhw` stays `none`,
and `gps.motion_state` stays `off` because the detector is gated on `m_Enabled`.
This is the scenario side being wrong, not the firmware: a receiver assertion on
a board with no receiver certifies nothing.

`m5stack-core` therefore left the `boards` list of those fifteen entries in
`sim/scenarios/manifest.json`, and each file carries a two line header saying
why. They stay certified on `m5stick-s3` and `m5stick-c`, both built and swept
in CI, so the receiver stack keeps its coverage on two panels. The Core keeps
what it can still certify: reachability and layout of every GPS page
(`page-matrix`, `overflow-sweep`, `connstate-page-sweep`, the `core-*` layout
files and the six `gps-hold-rows-*` files). All of them pass on the Core, and
`connstate-page-sweep` still opens the Connected GPS Data page there.

## Power baseline for waveshare-no-gps-uart

`waveshare-no-gps-uart` is a `power-gate` scenario, so the gate demands a
baseline and the check failed on that, not on a number:

    waveshare-no-gps-uart: no baseline at tools/power-model/baseline/waveshare-no-gps-uart.json. Add one with the plan 63 flow.

The other eleven scenarios compared at +0.00% and `gps-duty-standby-cycle` at
+0.14%, so nothing regressed: the missing baseline was the whole failure.

The committed baseline is the report the code on this branch produces, in the
same format as its neighbours: 81.255970 mA over 5015 ms, with
`gps_service_timer` 5 fires, `battery_timer` 2, `icon_timer` 10,
`inactivity_timer` 5, `invalidated_area_pixels` 4,036,887 and
`flushed_pixels` 32,904. 81.255970 mA is also `menu-idle-30s`' figure, and that
is the justification rather than a re-baseline: the model charges the GPS UART
driver, the rail and the duty wake, so a row with no GPS UART must read the same
as an idle menu. A future change that starts the rail, installs the driver or
runs a duty cycle on a `NONE` board moves this number and fails the gate instead
of passing silently. There is no before figure to record because the scenario is
new and its subject, a `NONE` row, did not exist on master. The seeds are the
only non-default input (`board stamps3`, `gps true`), and the report is
byte-identical across repeated runs.

## Verification

Local runs on `feat/gps-profile-pins`, base `origin/master` at `b711a44a`.
That base adds only simulator scenarios and plan docs on top of
`afb8513b`, where the firmware and host runs below were made, so they
carry over unchanged:

- `ctest --test-dir /tmp/furble-gps-host-build`: 196 of 196 pass, and
  `-R "^board-profile$"` passes on its own.
- `sh sim/build.sh` then `./sim/build/furble-sim --script
  sim/scenarios/waveshare-no-gps-uart.txt`: exit 0, log line
  `furble: No GPS UART on this board.`, `assert ok: gps.enabled = 0` and
  `assert ok: gps.receiver = absent`. The same scenario without the `board`
  seed fails with `ASSERT FAILED: gps.enabled expected '0' got '1'` and exit
  1, which shows the seeded row is what suppresses the UART and not a side
  effect of the run.
- `pio run -e m5stick-s3` (`FURBLE_TEST=0`, `FURBLE_VERSION=1.0.0-ci`):
  SUCCESS, flash 1,861,597 of 3,145,728 bytes (59.2%), RAM 47,464 of 327,680
  (14.5%).
- `pio run -e waveshare-s3-eth` (same flags): SUCCESS, flash 1,529,373 of
  6,291,456 bytes (24.3%), RAM 46,904 of 327,680 (14.3%), so the row change
  does not move the image out of budget.
- `python3 tools/check_coupling_ratchet.py --base origin/master`: `m5_calls`
  drops from 135 in 12 files to 133 in 11 files and `src/FurbleGPS.cpp` leaves
  the map. That is the intended direction and the only move: the lookup moved
  into `src/FurbleBoard.cpp`, an adapter file the ratchet does not count. No
  category grew.
- `python3 tools/pre_review.py --base origin/master`: 10 gates pass.
- `sh sim/scripts/check-doc-tokens.sh`: tokens present. The new `board` seed is
  documented in `docs/sim.md`, which that gate requires.

Hardware gates that remain, recorded so they are not lost: on the Core, boot
with GPS enabled and confirm the IMU and the battery level still read after
`GPS::init()`; on the Waveshare, confirm the W5500 link and MQTT stay up with
the GPS setting on. Both are device gates, not simulator results.

### Review

The independent review of this branch is recorded in
[336-review-feedback.md](336-review-feedback.md): the Waveshare doc row that
still advertised a GPS port the profile now closes, the two citations that
still pointed at the pin lookup this change deleted, eight doc citations the
rebase rotated, and the regression, stickiness, `isEnabled()` and GPIO13
questions with their evidence.

### Runs behind the two failing checks

Worktree `/tmp/furble-gps-pins` on `feat/gps-profile-pins`, base
`origin/master` at `b711a44a`. Three simulators were built with `sh sim/build.sh`
and the dependency trees of the primary checkout: `sim/build-core` (320x240,
`FURBLE_M5COREX` / `board_M5Stack`), `sim/build-s3` (135x240) and
`sim/build-stickc` (80x160, `FURBLE_M5STICKC` / `board_M5StickC`).

- `gh run view --log-failed` on run 45746216297 (`Simulator power regression`):
  eleven `PASS` lines, then `waveshare-no-gps-uart: no baseline at
  tools/power-model/baseline/waveshare-no-gps-uart.json. Add one with the plan
  63 flow.` and `Power model gate failed`. No scenario regressed.
- `gh run view --log-failed` on run 45746216291 (`sim-e2e`):
  `ASSERT FAILED: ui.visible_objects expected '25' got '22'` in
  `sim/scenarios/bughunt/core-connected-grid-large.txt`.
- Core bughunt sweep, the same `--list-certified --suite bughunt --board
  m5stack-core` selection CI uses, 51 scenarios: 36 pass and 15 fail before the
  UI gate fix, 51 pass after. The pre-fix failures, captured from that binary:
  `ASSERT FAILED: ui.gps_motion_row expected 'enabled' got 'hidden'`
  (`core-notouch-layout`, `gps-motion-row`, `gps-motion-row-large`,
  `gps-motion-row-small`), `ASSERT FAILED: ui.gps_motion_row expected
  'disabled' got 'hidden'` (`gps-motion-row-gates`),
  `Simulator action 'nav gps_hold' returned UNAVAILABLE, expected APPLIED`
  (`gps-hold-rows-*`), `Simulator action 'nav gps_rate' returned UNAVAILABLE,
  expected APPLIED` (`page-matrix`, `overflow-sweep`), and the object count
  (`core-connected-grid`, `core-connected-grid-large`).
- Core end-to-end sweep (`run-e2e.sh`, `FURBLE_SIM_BOARD_ID=m5stack-core`): 15
  failures before the board matrix change, all receiver scenarios; 0 after.
- Core physical-button pass (`run-notouch.sh`, same board id): 94 pass, 12
  skipped as touch-layout scenarios, 0 fail.
- S3 bughunt sweep, 57 scenarios: 57 pass before and after, which is the check
  that the UI gate change is a no-op wherever a receiver can run.
- Power gate, run exactly as the workflow does, per scenario `compare.py`
  against `tools/power-model/baseline`: 11 of 11 `PASS`. `waveshare-no-gps-uart`
  reads `81.255970 mA vs 81.255970 mA (+0.00%)`. `gps-duty-standby-cycle` reads
  -1.56% here and +0.14% on the Linux CI run for the same binary, so that
  scenario carries a platform offset well inside the 10 percent band; it is not
  a result of this change.
- `ctest --test-dir /tmp/furble-gps-host-build`: 196 of 196 pass.
- `python3 tools/pre_review.py --base origin/master`: 10 gates pass.
- `python3 tools/check_sim_scenarios.py`, `sh sim/scripts/check-doc-tokens.sh`
  and `python3 tools/check_sim_coverage.py`: pass. `clang-format 21.1.2` leaves
  no diff in `src/FurbleUI.cpp`.

The Waveshare-side device gate above is unchanged by this round; the Core gate
gains one item, that the GPS settings page and the Connected GPS Data entry stay
reachable with the receiver absent.
