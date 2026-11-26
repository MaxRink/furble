# 365 - Align the simulator with the verified Core family facts

Status: implemented. The simulator model is the subject, and the only firmware
source it touches is `src/FurbleBoard.cpp`: three capability literals change
(the Core charge-state cap, and the Tough SD slot and sound path) and the rest is
the `FURBLE_SIM` seam. Those three are user visible on those boards, and
`docs/supported-hardware.md` and `docs/power/board-power.md` record them. The
plan changes no partition table, sdkconfig or build environment, and it makes no
new hardware measurement. Every vendor claim below is either already recorded in
this tree or read from the pinned vendor code in `.pio/libdeps`; nothing here is
a bench result.

## Motivation

A second, independent pass over the Core family found that the simulator could
not answer a Core question at all. `sim/platformio.ini` defines
`FURBLE_M5STICKS3` and `sim/CMakeLists.txt` pins `M5GFX_BOARD=board_M5StickS3`,
so `M5.getBoard()` reports a StickS3 in every run and `Board::profile()` always
returns the `STICKS3` row. Every scenario that claimed to check a Core, Core2 or
Tough capability was therefore checking `sim/hardware_model.cpp` against itself.
That is how the Tough firmware row could say "no SD slot, no sound" while the
model and the panel scenarios said the opposite, and the suite stayed green.

The same pass listed seven smaller mismatches. Each one is a claim the
simulator made about hardware that the tree's own evidence contradicts.

## What was verified, and where

| Claim | Evidence |
| :--- | :--- |
| The Core, Core2 and Tough have no IR emitter; the StickC, Plus, Plus2 and StickS3 do | `src/FurbleBoard.cpp` IR pin column (9, 9, 19, 46, -1, -1, -1) and `docs/power/board-power.md` section 2, which lists "IR on G9" for the StickC, "LED and IR G19" for the Plus2 and "IR TX G46 and RX G42" for the StickS3, and no IR part for any Core board |
| The Core has no RTC chip | `docs/power/board-power.md` section 2 lists the BM8563 for the StickC pair, Core2 and Tough only; `plans/implemented/139-persistent-time.md` records which boards expose a calendar RTC |
| The StickS3 has no calendar RTC | `plans/implemented/139-persistent-time.md` ("the official product selector does not list a calendar RTC. M5PM1 keeps the RTC wake peripheral alive") |
| No backup cell is recorded for the Plus2 or Tough RTC | `docs/power/datasheet-register.md` marks the BM8563 as "cited, not opened" and names the cell only for the Core2 |
| The Core image is built for 4 MB, current Basic units ship 16 MB | `sdkconfig.m5stack-core` (`CONFIG_ESPTOOLPY_FLASHSIZE_4MB`), `platformio.ini` `board_build.partitions` |
| The IP5306 has no shutdown command; the manual Off releases the boost keep-on bit, deep sleeps, and the light-load cut removes the rail after the window | `include/FurbleIP5306.h`, `docs/power/board-power.md` "IP5306 (Core)" and plans/354 finding 2; M5Unified 0.2.20 `Power_Class.cpp` `_powerOff()` `case pmic_t::pmic_ip5306: Ip5306.setPowerBoostKeepOn(withTimer)` |
| The firmware holds `SYS_CTL0` bit 1 for the whole session, so the cut is not armed while awake | `src/FurblePlatform.cpp` `keepIp5306BoostOn()`, plans/354; the model takes the same step at boot and only the manual Off releases it |
| The IP5306 reports charge state but no voltage or current | M5Unified 0.2.20 `IP5306_Class.cpp` `isCharging()` reads `REG_READ0` bit 3; `getBatteryLevel()` is the four-level table |
| IP5306 light-load cutoff: below 45 mA for the window `REG_SYS_CTL2` bits 3:2 select, unless "BOOST output normally open" is set | IP5306 register document V1.21 for the field: bits 3:2 are 00 = 8 s, 01 = 32 s, 10 = 16 s, 11 = 64 s. M5Unified 0.2.20 `Power_Class.cpp:614` and `:617` write `0x02 = 0x6C`, bits 3:2 = 11, so the shipped part cuts at 64 s. IP5306 datasheet V1.01 gives the 45 mA threshold and the 32 s power-on default of field 01, which the firmware overwrites. M5Unified `IP5306_Class.hpp` `setPowerBoostKeepOn()` and the `0x00 = 0b00110001` init block leave bit 1 clear |
| AXP192 init writes `0x82 = 0xFF`, `0x83 = 0x80`, `0x84 = 0x32`, `0x90 = 0x07`, plus per-rail `0x12` bits | M5Unified 0.2.20 `Power_Class.cpp` AXP192 block and the board switch under it |
| AXP2101 (Core2 v1.1) writes `0x12 = 0x00` (BATFET), `0x30 = 0x0F` (ADC) and never `0x82` | M5Unified 0.2.20 `Power_Class.cpp`, the `else if (_pmic == pmic_t::pmic_axp2101)` branch commented "for Core2 v1.1" |
| `0x82` is the AXP2101 DCDC1 voltage register, which is the CPU rail on that board | X-Powers AXP2101 register map as carried by the reference driver. This tree has no AXP2101 datasheet copy (`docs/power/datasheet-register.md` records that gap), so the guard is justified by the vendor code never writing the byte, and the rail claim is datasheet-only |
| The Tough has an SD slot and a speaker | `docs/power/board-power.md` section 2 lists SD, NS4168 and BM8563 for the shared `m5stack-core2` row, and the AXP192 board switch calls `setGPIO2(false)` "SPEAKER STOP" for the Tough, which only a board with a speaker needs |
| The Core2 and Tough Grove 5 V output is the external 5 V net | `docs/power/board-power.md` section 3.2: Core2 `LDOIO0` "Grove 5 V", switched by `M5.Power.setExtOutput` |
| The Core has no touch panel | `docs/power/board-power.md` section 2 lists FT6336U touch for Core2 and Tough only |

## Design

### Board identity under FURBLE_SIM

`Board::profile()` asks the weak `furble_sim_board_profile()` hook that
`src/FurbleBoard.cpp` already carried for the host builds. The simulator answers
it through `Sim::setBoardProfile()`, and `sim/driver.cpp` seeds that from
`hw_profile` when the scenario is applied, so one seed selects both the model row
and the firmware row; an explicit `seed board` overrides it. The mapping from
model profile to `m5::board_t` therefore lives in the driver, next to the other
scenario seeds, and no firmware header sees a simulator type. `waveshare-s3-eth`
maps to `board_M5StampS3`, the identity M5Unified detects on that board and the
row #500 gave it; `headless-s3` maps to `board_unknown`, which is what that
devkit build gets on device.

### Model columns mean what the firmware row means

`sim/hardware_model.cpp` `board_caps_t` `ir`, `sd`, `audio`, `rtc`, `touch` and
`rtcBatteryBacked` are now the capabilities the firmware reads, not silicon
inventory:

- `rtc` is a calendar RTC chip at all, which is `RTC_FACTS`; `rtcBatteryBacked`
  is whether the profile expects that chip to keep time, which is exactly
  `Profile::rtcBatteryBacked`, and `hw.rtc_backed` exposes it. Master split the
  two columns while this branch was open, so the model carries both and each has
  its own two-sided assertion.
- `audio` is a speaker or amplifier path the firmware drives for feedback sound,
  which is exactly `Profile::feedbackSound`. The StickC's buzzer is not one.
- `ir` is an emitter the firmware can drive, which is exactly `profile.irPin >= 0`.
- `timedWake` mirrors `Profile::timedWake`, and `board.timed_wake` was added so
  the column is asserted on both sides.

Without that, the comparison could not be one-to-one and the StickC would have
had to disagree on sound.

### Register model

`pmic_state_t` carries a 256-byte register file, a per-address write count and a
violation counter. `pmicWriteLocked()` refuses an address the modelled part does
not have and counts the refusal. The boot block is the vendor block per part.
`pmic.reg.<0xNN>.meaning` is part of the model because the same address is a
different register on each part, and on the AXP2101 `0x82` moves DCDC1.

### Power-off, rails and light load

`Platform::powerOff()` keeps recording the request, so the teardown-ordering
scenarios still read `platform.power_off`, and returns `Sim::hwPowerOff()`. The
Core reports false and stays up, which is what the vendor IP5306 path actually
does; master's `Profile::powerOffPath` column and `PlatformPolicy` refusal run
first, so a row with no path at all (the Waveshare) never reaches the model. The IP5306 light-load timer runs from
`Platform::update()` on the virtual clock, with `Platform::setDisplayOff()` as
its panel input and `ip5306_load_ma` as its load. Its window is not a constant:
`ip5306LightLoadMs()` decodes `REG_SYS_CTL2` bits 3:2 through the table from the
register document V1.21, so the model runs the 64 s the vendor block selects and
another seeded byte selects another window. `Platform::setRail(GPS)` feeds
the same model, and on Core2 and Tough the external 5 V net follows the Port A
rail.

### Touch

The SDL panel always attaches a mouse-driven touch device, so touch used to be
present on every board. The attach decision now defaults to `Sim::hwTouch()` and
`seed touch true` opts into the touch layout. That is the same layout
`M5.Touch.isEnabled()` selects on the device, and it is the layout
`sim/scripts/run-notouch.sh` already exercised for the whole suite. The 18
scenarios that assert `ui.nav_layout touch` now seed it explicitly.

## Implementation state

Implemented:

- `sim/hardware_model.{h,cpp}`: the board rows above, `powerOff`, `ext5vShared`,
  the register file with its per-part guard, the rails, the IP5306 light-load
  timer, and the `pmic.*`/`power.*` queries.
- `sim/driver.cpp`: the profile-name to `m5::board_t` table that seeds
  `Sim::setBoardProfile()`, so the model row and the firmware row agree.
- `src/FurbleBoard.cpp`: the seam call, the Core charge-state cap, and the Tough
  SD-slot and sound row.
- `sim/FurblePlatformSim.cpp`: `getBatteryCapacity()` from the profile,
  `powerOff()` from the model, the panel state and rail hooks, the tick, and the
  touch default.
- `sim/memory_profile.cpp` and the rest of the profile-name surface: the memory
  budget rows are keyed `core-4mb` and `core-16mb`, because the budgets are
  looked up by the seeded profile name and the old `core` key would have fallen
  back to the StickS3 row (8 MB PSRAM, 512 KiB internal). The derived pool key in
  `sim/coverage/board_heap.json`, its generator `tools/derive_sim_heap.py`, the
  memory profile runner `sim/scripts/run-memory-profile.py` and the `profiles`
  list and 19 entries in `sim/coverage/hardware_quirks.json` name the same
  profile. `hw-pmic-ip5306-core` and `hw-profile-core-uart` assert the Core
  figures this exposes.
- `sim/driver.cpp`, `sim/scenario_action.cpp`: `touch`, `ip5306_load_ma`,
  `ip5306_light_load_ms` and `ip5306_sys_ctl2` seeds, `core-4mb` and `core-16mb`
  profile names, the `pmic.light_load_field` and `pmic.light_load_source`
  queries that make the window decode assertable,
  `pmic-boost-keep-on`, `pmic-boost-keep-off`, `pmic-axp192-adc`, `gps-rail-on`
  and `gps-rail-off` actions, the `board.*` and `pmic.reg.*` queries, and
  `platform.battery.capacity` / `platform.battery.caps.*`.
- Scenarios: 7 `board-row-*` model-versus-row checks, 4 `hw-poweroff-*`, 3
  `hw-ext5v-*`, 3 `hw-touch-*`, 4 `hw-ip5306-*` (the shipped decode, a second
  code point, the armed cut and the keep-on defeat), 3 `hw-pmic-*` register
  scenarios, 1 `hw-profile-core-16mb`, plus the assertion updates in the existing
  `hw-*` profile scenarios and `seed touch true` in the 18 touch-layout ones.
- Docs: `docs/sim.md` (seeds, actions, queries, board identity), `sim/CLAUDE.md`
  (seam inventory), `docs/supported-hardware.md` and
  `docs/power/board-power.md` (Core charge state, 4 MB build versus 16 MB part,
  Tough SD and sound, no Core IR or RTC).

Deviations:

- **The Core flash size.** The verified fact is that current Basic units ship
  16 MB; the committed image is still built for 4 MB. Rather than claim the
  image is 16 MB, the model keeps the 4 MB configuration and names it
  `core-4mb`, and a second row `core-16mb` carries the retail part. The bare
  name `core` is refused. Changing the firmware's flash size is a separate PR
  with its own partition, size-budget and CI consequences. Because the bare name
  is refused, every consumer of the profile name moved with it: the memory
  budget rows, the derived heap pool key and its generator, the memory profile
  runner and the hardware quirks manifest. A budget row still keyed `core` fails
  the Core memory assertions with the StickS3 figures, which is how the gap was
  found on the `m5stack-core` scenario list. Four more consumers were still on
  the old name and are now moved: the memory budget keys, the sdkconfig/profile
  map in `tests/test_sim_profile_sdkconfig.py`, the panic fixture, and the
  permutation manifest with its unit test. The committed
  `sim/coverage/permutation_results.json` still carries the pre-rename board
  label in its result ids because the run itself was not re-executed; the
  coverage gate counts entries and passes, and the next full permutation run
  regenerates it.
- **The IP5306 light-load window is the written 64 s, not the datasheet's 32 s
  row.** An earlier revision of this branch modelled the 32 s the IP5306
  datasheet headline row gives and exposed the `0x02 = 0x6C` byte beside it. That
  was inconsistent: the simulator ran a window no shipped Core runs at. PR 482
  settled the field from the IP5306 register document V1.21, where `SYS_CTL2`
  `0x02` bits 3:2 select 00 = 8 s, 01 = 32 s, 10 = 16 s and 11 = 64 s, and from
  M5Unified 0.2.20 `Power_Class.cpp:614` and `:617`, which write `0x6C`, bits
  3:2 = 11. The shipped Core cuts at 64 s. This supersedes the 32 s figure, and
  the model now decodes the byte the firmware writes instead of choosing a
  figure: `hw-ip5306-default-window.txt` asserts the shipped byte, its bits 3:2
  field and the 64 s it selects, and `hw-ip5306-window-decode.txt` asserts a
  second code point (00 = 8 s) so a decoder that returned one constant fails.
  PR 482 changes the same number in `docs/power/board-power.md`,
  `docs/power/datasheet-register.md` and `include/FurbleSettingsDefaults.h`, so
  the two must agree and the 32 s claims that PR 482 rewrites are left to it.
  PR 482 also rewrites `sim/scenarios/e2e/hw-pmic-ip5306-core.txt`, which this
  branch already seeds as `core-4mb`; that file must keep the named profile and
  the 64 s, so a resolution that restores the bare `core` name fails the
  scaffold refusal.
- **The Core2 v1.1 `0x82` claim.** This tree has no AXP2101 datasheet
  (`docs/power/datasheet-register.md` records the gap), so the scenario asserts
  what the vendor code does (never writes the byte) and what the model refuses,
  not a transcribed byte layout.
- **Firmware `Platform::powerOff()` still returns true unconditionally** on the
  non-M5PM1 path, including the Core. This PR is simulator-only, so the model
  reports the truth and the firmware deviation is recorded here as an open
  follow-up rather than changed.
- **Two more model rows moved to match the firmware row** while the comparison
  was being written, because the old values were unverifiable rather than
  merely different: the model claimed an RTC for the StickS3 (which has a PM1
  wake timer, not a calendar RTC), and it claimed one for the Plus2 and Tough
  (whose backup cells are not recorded anywhere in this tree). It also claimed
  the StickC drives feedback sound, which its row does not and `board-power.md`
  does not describe.
- **The power-gate baselines are not re-recorded.** The committed reports in
  `tools/power-model/baseline` were taken in the touch layout, so the ten
  scenarios keep that layout with an explicit seed. Re-recording them in the
  shipped button layout is a plan 63 follow-up, and the seed makes the current
  gate a comparison of the same scenario rather than a new measurement.

## Rebase onto master

The branch was opened 51 s after #482 landed, and #482 rewrote the same
`docs/power/board-power.md` line and the same `hw-pmic-ip5306-core.txt` lines it
rewrites, so it was conflicting from creation and never got a
`refs/pull/N/merge` commit. That is why it had no check runs at all; the review
record in [365](365-review-feedback.md) works it through.

The rebase onto `6a82c71b` was a re-implementation rather than a text merge,
because master had moved four of the structures this branch touches:

- The Port A rail is now owned by `rail_state_t` behind
  `hwSetRail(rail_holder_t, bool)`, with holders, refusals and the PM1
  `PWR_CFG` output bit. The branch's `Sim::hwSetGpsRail()` was dropped, and the
  external 5 V net it introduced is driven from `setRailLocked()` instead, so
  there is one owner.
- `rtc` was split into the chip column plus `rtcBatteryBacked` and an
  `RTC_FACTS` table. The model rows follow the split and `hw.rtc_backed` exposes
  the profile column.
- `src/FurbleBoard.cpp` gained `powerOffPath`, `ledPinActiveLow`, `irMarkLow`
  and the Waveshare row. The branch's three literal changes are re-expressed on
  master's rows, and its `FURBLE_SIM` seam is unchanged.
- `Platform::setDisplayOff()` now drives the PM1 `PWR_CFG` byte and
  `platform.display_off` reads that model state, so the branch's local
  `displayOffRequested` flag is gone and the same call also feeds the IP5306
  panel input.

Two facts master's model had wrong were corrected while the two-sided scenarios
were being wired up, because the new assertions failed on them: the Plus2
`rtcBatteryBacked` and the Plus `timedWake`, both of which plan
[350](350-stickc-family-fixes.md) already fixed on the firmware side. The plan
number moved twice for the same reason: master took 355 for
[355-sim-cmake-entry-point.md](355-sim-cmake-entry-point.md) and then 363 for
[363-pmic-irq-speaker.md](363-pmic-irq-speaker.md) while this review was open, so
the plan and this record are 365.

The final rebase is onto `79df13a7`. That base only conflicted in three files:
#493 replaced `PowerBackend::Rail::GPS` with `Rail::EXT_5V` behind a
reference-counted `RailAccount`, so the `gps-rail-on/off` action moved to the
new rail and `Platform::tick()` keeps both the IR fake's poll and the IP5306
timer. Every merge before it is covered below.

The rebase then landed on `535ad7d6`, which merged
[354](354-core-family-fixes.md) (#497) while this review was running. That
commit took over the firmware half of four rows this branch had changed:

- The Tough SD slot, the NS4168 speaker path, the BT1-backed BM8563 and its
  alarm wake are #497's, so this branch no longer changes that row at all.
- The capacities moved to Core 110 mAh and Core2 500 mAh.
- `SYS_CTL0` bit 1 is now held for the session and `Platform::canTurnDisplayOff()`
  answers from it, which replaces this branch's premise that the cut is armed on
  a stock Core. The model follows the merged firmware: it holds the bit at boot,
  only the manual Off releases it, that Off is accepted and deferred rather than
  refused, and the Deferred path leaves the state machine ON until the
  light-load cut fires.
- The IP5306 register map and the bits 3:2 decode moved to
  `include/FurbleIP5306.h`, so the model calls the firmware decoder instead of
  keeping a second copy of the table.

What is left of the firmware side is the Core charge-state bit, which #497 did
not take. Everything else here is the simulator: the board-identity seam, the
model rows, the register file and its guard, the rails, the touch default and
the named 4 MB and 16 MB Core profiles.

## Verification

Run on the branch work tree rebased onto `6a82c71b`, macOS, SDL dummy video and
audio drivers.

- `python3 tools/check_sim_scenarios.py`: "sim scenario manifest is complete",
  433 entries (25 new).
- `sh sim/build.sh`: clean build of `sim/build/furble-sim` with
  `FURBLE_DEP_ROOT` and `FURBLE_LVGL_DIR` pointing at the shared dependency
  tree.
- `sh sim/scripts/run-e2e.sh` (the default `m5stick-s3` binary): "All 258
  end-to-end scenarios passed", including every new scenario.
- `sh sim/scripts/run-notouch.sh` (default binary): "All 302 m5stick-s3
  scenarios passed in the physical-button layout, 13 skipped". The skips are the
  scenarios that assert `ui.nav_layout touch`, which now seed `touch true` and
  are the touch layout's subject.
- The other board lists were run too. The `m5stick-c` list passed 66 of 66 on
  the S3 binary. The `m5stack-core` list first ran on the S3 binary and failed
  three scenarios, which is how the memory-budget key gap above was found:
  `hw-pmic-ip5306-core` stopped at `mem.internal_total` (532480 against 524288),
  `hw-profile-core-uart` at `mem.psram_total` (0 against 8388608) and
  `modal-input-ownership` reported "Unknown or unavailable simulator button for
  this board: c" because `btn c` comes from the `FURBLE_M5COREX` build. After the
  budget rows moved, the list was run against the core-panel binary
  (`FURBLE_SIM_FURBLE_BOARD=FURBLE_M5COREX`,
  `FURBLE_SIM_M5GFX_BOARD=board_M5Stack`, built in
  `/tmp/coresim-build-core` with `FURBLE_LVGL_DIR` pointing at the shared
  dependency tree because this work tree's `managed_components/lvgl__lvgl` is
  empty): "All 70 end-to-end scenarios passed."
- Power gate: all ten certified scenarios run and compared with
  `tools/power-model/compare.py`. Nine match their baseline exactly (+0.00
  percent). `gps-duty-standby-cycle` is the one scenario whose figure moves
  between runs: the recorded runs land between +0.13 and -1.59 percent against a
  10 percent threshold, and three consecutive runs of the last build all read
  84.798941 mA (-1.56 percent). The gate is clean. The touch seeds are load
  bearing: dropping the new `seed touch true` from `menu-idle-30s.txt` makes it
  fail on `activity invalidated_area_pixels` (124,603 against 56,843, +119.21
  percent), which is why those seeds are there.
- `sh sim/scripts/run-invalid.sh`: "CLI and environment validation passed."
- `sh sim/scripts/run-deep-sleep.sh`: exit 0.
- `sh sim/scripts/check-doc-tokens.sh`: "sim documentation tokens are present".
- `python3 tools/check_sim_coverage.py`: "simulator coverage manifest is
  complete", 198 hardware entries, 20 crash entries, 11 fuzz targets and 6,912
  permutations.
- `python3 tools/pre_review.py --base origin/master`: 10 gates pass, including
  clang-format 21 on the changed C and C++ files and the coupling ratchet.

## What still needs hardware

- The Core: confirm the charge-state bit a real IP5306 reports through M5Unified,
  and confirm the 45 mA threshold and the 64 s window the written `0x02` field
  selects. The model decodes the byte the firmware writes, and the decode is a
  document plus the pinned library, not a measurement.
- The Core: confirm that a refused software power off leaves the UI usable, which
  is now the modelled outcome, and whether the firmware's unconditional `true`
  should become the model's `false`.
- The Tough: confirm the SD slot and the speaker path on a unit. The row change
  rests on the shared schematic and the vendor speaker-stop call, not on a test.
- The Tough and Plus2: the RTC backup cell is unverified, so neither side claims
  a battery-backed RTC. A cell measurement would let both say yes.
- The Core2 v1.1: confirm the AXP2101 register `0x82` DCDC1 mapping against the
  X-Powers datasheet, and confirm that no path in the boot sequence writes it.
- The external 5 V collateral on Core2 and Tough: confirm on a unit that a
  `setExtOutput(false)` drops BUS_5V with the Grove port.
