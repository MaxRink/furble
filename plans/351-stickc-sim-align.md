# Plan 351: align the simulator with verified StickC-family facts

Status: implemented on `fix/stickc-sim-align`, rebased onto master after the plan
renumbering, the LilyGO and T-Beam ports, the simulator RTC calendar (#507) and
the memory model work. Simulator and shared-constant work only. The one firmware
edit replaces inline arithmetic with the shared guard and keeps the same rule, so
no board behaves differently. Hardware questions are listed at the end and stay
open.

## Motivation

The simulator is the only place a board's power, wake, feedback and peripheral
behavior is exercised without hardware, and its scenarios are cited as release
evidence. Two review passes over the StickC family found thirteen places where
the model answered a question the hardware or the firmware already answers
differently. Each one is a class of defect, not a single wrong line:

- A capability invented for a board that has no such part (sound on the
  original StickC).
- A build-wide answer standing in for a board row (motion wake, timed wake,
  battery capacity). The build compiles one panel but the profile row is what
  production reads, so a build-wide `true` claims a pin or a rail the part does
  not have.
- A constant duplicated instead of shared (the MPU6886 4 mg per count scale).
- Two different mechanisms conflated under one name (a PMIC power latch and the
  Plus2 GPIO HOLD latch).
- A static flag used where the row has a per-board value (`hw.rtc`).
- Hardware facts with no model at all: IR/LED polarity and the shared G19,
  button pins, the Plus2 part versus its image, and the Port A rail level at
  boot.

The consequence is a simulator that passes scenarios describing a board that
does not exist, and scenarios that cannot fail when a firmware constant drifts.

## Verified facts and where they come from

| Fact | Source opened in this tree |
| --- | --- |
| M5StickC has no buzzer and no speaker, red LED on G10 | plans/implemented/23-feedback-outputs.md, https://docs.m5stack.com/en/core/m5stickc |
| StickC Plus and Plus2 have a passive buzzer on G2 | plans/implemented/23-feedback-outputs.md, the two product pages there |
| IR emitter G9 (StickC, Plus), G19 (Plus2, shared with the LED), G46 (StickS3), none (Core, Core2) | plans/implemented/22-ir-remote-trigger.md |
| Plus2 IR emitter and red LED are the same pin | plans/implemented/22-ir-remote-trigger.md, plans/implemented/23-feedback-outputs.md |
| BM8563 on the StickC and Plus sits on the PMU rail, so the calendar is backed while the cell is fitted | plans/implemented/139-persistent-time.md |
| BM8563 countdown selection: 1 s below 270 s, minute rounding at or above it, 8-bit clamp, control 0x82/0x83 | M5Unified `src/utility/rtc/PCF8563_Class.cpp` `setTimerIRQ()`, reached through `RTC_Class::setAlarmIRQ(int)` |
| Plus2 GPIO4 HOLD latch keeps power on and must be driven immediately after `M5.begin()` | plans/implemented/19-interval-deep-sleep.md |
| BtnA/BtnB/power per board | M5Unified `src/M5Unified.cpp` `_update()` board switch, pinned 0.2.x copy |
| Port A 5 V (AXP192 EXTEN) is already on after `M5.begin()` on StickC and Plus | M5GFX `src/M5GFX.cpp` `Light_M5StickC::init()`, register 0x12 = 0x4D |
| StickS3 Port A rail starts off | M5Unified `_setup_pinmap()` sets M5PM1 GPIO3 output low |
| Plus2 part: 8 MB QIO flash, 2 MB quad PSRAM; shared image 4 MB DIO, PSRAM off | M5StickC Plus2 product page; `sdkconfig.m5stick-c` (`FLASHSIZE="4MB"`, `CONFIG_ESPTOOLPY_FLASHMODE="dio"`, `CONFIG_SPIRAM` not set). Every committed sdkconfig leaves the image at `dio`; five of them (`m5stick-s3`, `esp32-s3-headless`, `waveshare-s3-eth`, `lilygo-tdeck`, `lilygo-twatch-ultra`) also select `CONFIG_ESPTOOLPY_FLASHMODE_QIO=y` under `CONFIG_ESPTOOLPY_FLASH_MODE_AUTO_DETECT=y`, so the bootloader may bring the part up quad while the image's own mode stays `dio`. The build rows record the image, which is what `hw.build_flash_mode` reports |

Unverified and marked as such in code: the MPU6886 wake-on-motion threshold LSB
(4 mg), the BM8563 register semantics beyond the M5Unified driver, the emitter
polarity of every StickC-family IR LED, and the LED polarity plan 23 already
flags as "not documented per board". The Plus2 row claims RTC backing
(`rtcBatteryBacked = true`, set with the BM8563 INT wake path by #505, plan 350)
with no datasheet confirmation in the tree, so the model follows the row and the
bench question is recorded below. The Tough row sets `feedbackSound`, which
M5Unified's own `board_M5Tough` speaker setup and
`tests/host/board_profile_test.cpp` both agree with, so that one is not open.

## Design

One rule: a fact the firmware owns is read from the firmware, and a fact the
hardware owns is read from a cited table. Nothing is copied into a second table
where it can drift.

- `sim/hardware_model.cpp` maps a seeded profile name to a `m5::board_t` and
  reads `Board::profileFor()` for sound, battery capacity, RTC backing, motion
  wake and timed wake. The `audio` column is deleted from the simulator's own
  capability table, so the two cannot disagree, and the same reasoning removes
  the `rtcBatteryBacked` and `timedWake` columns master had started to carry
  there.
- `include/FurbleRtcCountdown.h` (new, dependency free) owns the BM8563
  selection and the acceptance rule. Production `verifyBm8563Timer()` and the
  `powerOffUntil()` early-window guard call it, and the simulator models the same
  selection from it, so the mode switch, the 255 clamp and the early-window edge
  are one implementation.
- `sim/hardware_model.{h,cpp}` gains modeled state for the BM8563 countdown, the
  GPIO HOLD latch, and the IR/LED pins, plus a per-board `board_extra_t` table
  for the cited hardware facts the profile does not carry.
- `sim/FurblePlatformSim.cpp` answers motion wake, timed wake and battery
  capacity from the row, runs the BM8563 branch of `powerOffUntil()` in the same
  order as production, asserts the HOLD latch inside `Platform::getInstance()`,
  decides a timed wake from the latched RTC flag on an `RTC_ALARM` board, and
  seeds the rail level from the board fact.
- New query keys: `hw.audio` (now row-driven), `hw.battery_mah`,
  `hw.motion_wake`, `hw.motion_wake_pin`, `hw.timed_wake`, `hw.rtc_backed`,
  `hw.rtc_irq_armed`, `hw.ir_pin`, `hw.ir_mark_low` and `hw.ir_mark_level` (the
  same column in pin terms: the level the emitter pin holds during a mark),
  `hw.led_pin`,
  `hw.led_active_low`, `hw.led_shared_with_ir`, `hw.button_a`, `hw.button_b`,
  `hw.button_pwr`, `hw.button_pwr_source`, `hw.rail_boot`,
  `hw.build_flash_kb`, `hw.build_flash_mode`, `hw.build_psram`,
  `pmic.hold_latch`, `pmic.hold_order`, `rtc.*` (including the
  `rtc.plan.<seconds>` journal), `ir.frames`, `ir.busy`, `ir.led_pulses`,
  `ir.led_conflicts` and `platform.rail`.
- New actions: `rtc-countdown SECONDS` (probe the countdown through the same
  helpers `powerOffUntil()` uses), and the pin-level injectors `ir-frame` and
  `feedback-light`.

## What this change did not take from the branch

The work was cut before the motion-scale audit and the simulator RTC calendar
landed, and the recovery dropped the branch's own copy of both rather than
reintroducing a competing implementation:

- `include/FurbleIMUMotionScale.h` and the four motion-scale edits
  (`sim/FurbleIMUSim.cpp`, `src/FurbleIMUMotionBMI270.cpp`,
  `src/FurbleIMUMotionMPU6886.cpp`, `tests/host/imu_motion_encoding_test.cpp`).
  Plan 332 (merged as #504) landed a fuller version of the same header, with the
  datasheet derivation, the shared stationary window and the `static_assert`
  pairs, and the engines and the host test already read it. The branch's file was
  a subset, so nothing is lost by dropping it.
- The branch's own simulator RTC calendar and backing model. Plan 380 (merged as
  #507) landed the seeded calendar chip, the `TimeKeeper` seam, the `.rtc`
  sidecar and the `rtc.*` cache, and `sim/hardware_model.h` keeps exactly that
  function set. What this change adds on top is the countdown, the alarm flag and
  the firmware acceptance journal, which #507 does not model.

## Item state

| Item | State | Evidence |
| --- | --- | --- |
| 1. StickC audio capability | Done. `audio` removed from the simulator table; `hw.audio` reads `Board::profile().feedbackSound`. `hw-profile-stickc.txt` now asserts `no`. | `hw-stickc-capabilities.txt`, `hw-profile-stickc.txt` |
| 2. `armMotionWake` build-wide true | Done. Returns the row's `MotionWake`, with production's M5PM1 guard. Plus2 reports `no`. | `hw-stickc-capabilities.txt`, `hw-stickc-plus2-capabilities.txt` |
| 3. `canTimedWake` hard false off-S3 | Done. Switches on the row: `PMIC_TIMER` needs the M5PM1 image, `RTC_ALARM` needs a detected RTC. The BM8563 countdown, mode switch, clamp and early-window are modeled and run in `powerOffUntil()`. | `hw-plus2-rtc-wake.txt`, `hw-stickc-plus2-capabilities.txt` |
| 4. `getBatteryCapacity` fixed 250 | Done. Returns the row's `batteryCapacityMah`: 95, 120, 200, 250, 390. | `hw-stickc-capabilities.txt` |
| 5. Plus2 modeled as no power latch | Done. `pmic.power_latch` is the PMIC latch only; `pmic.hold_latch` and `pmic.hold_order` model the G4 HOLD latch and when it was asserted. | `hw-pmic-plus2-no-pmic.txt`, `hw-stickc-plus2-capabilities.txt` |
| 6. `hw.rtc` static flag | Done. `hw.rtc_backed` and `hw.rtc_irq_armed` are row-driven, and `hw.rtc` keeps its existing "calendar device present" meaning on top of the #507 model. | `hw-stickc-capabilities.txt`, `hw-profile-stickc-plus.txt` |
| 7. 4 mg per LSB constant | Not this plan. Plan 332 (#504) owns it; see the section above. | `tests/host/imu_motion_encoding_test.cpp`, `imu-motion-encoding` |
| Second pass 1. Timed wake for every `RTC_ALARM` row | Done, same work as item 3. | `hw-plus2-rtc-wake.txt` |
| Second pass 2. IR and LED polarity, shared G19 | Modeled. Per-board pins, active-low LED, the emitter-pin level during a mark from the row's `irMarkLow`, and a conflict counter for a light pulse inside a frame. The missing production guard is tracked by `xassert`, not fixed here. | `hw-plus2-shared-led-ir.txt`, `hw-stickc-capabilities.txt` |
| Second pass 3. StickC buttons | Done. Physical pins are `hw.button_*`; `docs/sim.md` no longer presents the host pin convention as board wiring. | `hw-stickc-capabilities.txt`, docs/sim.md |
| Second pass 4. Plus2 part versus image | Done. `hw.flash_kb` 8192 and `hw.part_psram quad` for the part; `hw.build_flash_kb 4096`, `hw.build_flash_mode dio`, `hw.build_psram absent` and `hw.psram absent` for the image, and the heap model seeds from the image. | `hw-stickc-plus2-capabilities.txt`, `hw-pmic-plus2-no-pmic.txt` |
| Second pass 5. Rail level at boot | Done. `hw.rail_boot` is per board, the row seeds the modelled rail at boot, and `platform.rail` and `rail.state` report that rail, the same answer `Platform::getRail()` gives. The StickC pair starts on and the StickS3 starts off; the firmware's own acquire and release move it from there. The fake receiver keeps answering after a cut so the u-blox fixture stays hot, which is a fixture limit and not the reported level. | `hw-stickc-gps-rail-boot.txt`, `hw-psram-octal-sticks3.txt` |

## Scenarios added

| Scenario | Fails when |
| --- | --- |
| `hw-stickc-capabilities.txt` | Audio is `yes`, capacity is not 95 mAh, motion wake is not direct/GPIO35, a timed wake is claimed, the RTC is not backed, the IR/LED pins collapse to one, a button pin is wrong, or the separate-pin light pulse starts counting conflicts. |
| `hw-stickc-plus-capabilities.txt` | Audio is `no` (the Plus has a buzzer), capacity is not 120 mAh, or any wake, RTC, pin or flash fact drifts. |
| `hw-stickc-plus2-capabilities.txt` | Motion wake is claimed as direct, capacity is not 200 mAh, timed wake is missing, the RTC is claimed as backed, G19 is not modeled as shared, the power key is not GPIO35, the part is reported as 4 MB without PSRAM, or the HOLD latch is not asserted before the platform is ready. |
| `hw-plus2-rtc-wake.txt` | The mode switch moves off 270 s, the 255 clamp disappears, a late round-up is accepted, the 60 s early-window edge moves, or a rejected request leaves a countdown armed. |
| `hw-plus2-shared-led-ir.txt` | The shared pin stops being reported, a light pulse inside a frame stops counting a conflict, the pulse outside the frame starts counting one, or the row's `irMarkLow` flips. |
| `hw-stickc-gps-rail-boot.txt` | The rail does not start on for the StickC, or the GPS rail-cycle path stops cutting and restoring it. |

Each scenario is registered in `sim/scenarios/manifest.json` (suite `e2e`,
board `m5stick-c`) and its facts in `sim/coverage/hardware_quirks.json`.

## Deviations

- The branch asserted `rtc_backed no` for the Plus2, on the premise that its row
  is `rtcBatteryBacked = false`. That premise is stale: #505 (plan 350) set the
  Plus2 row to `true` when the BM8563 INT was wired to the AXP192 power key, and
  both Plus2 scenarios now assert `yes`
  (`hw-stickc-plus2-capabilities.txt`, `hw-pmic-plus2-no-pmic.txt`). The model
  follows the row, as it does everywhere else. The bench question is whether the
  claim holds across a real cut, and it is under "Still needs hardware".
- The Tough's sound feedback is the same rule with the opposite history. The
  branch flipped `hw-profile-tough.txt` to `hw.audio no` on the premise that the
  board has no speaker. The premise is wrong: M5Unified's `board_M5Tough`
  speaker setup configures the Core2 codec path with a higher magnification for
  the waterproof case, and `tests/host/board_profile_test.cpp` asserts the row's
  `feedbackSound` against "the NS4168 1 W speaker". The recovery keeps
  `hw.audio yes` and the scenario says why.
- The shared-pin guard for the Plus2 LED is **not** added to production.
  `Feedback` has no way to ask the IR driver whether a frame is in flight, and
  adding one is a product change. The scenario records the hazard with a plain
  assert and the wanted state with `xassert ir.led_conflicts 0`, so the fix
  lands knowingly and the xassert is promoted in the same change.

## What the twin asserts are worth

For every column the model now reads from `Board::profileFor()`, a `board-row-*`
scenario's `hw.` assert and its `board.` twin read the same row, so they agree by
construction: `audio`, `rtc_backed`, `timed_wake`, `motion_wake`,
`led_active_low`, `ir_mark_low` and the IR, LED, GPS and button pins are all in
that set. Those twins are drift detectors for the model, not independent
evidence, and the plan does not claim otherwise. Where the two sides really are
different sources the twin is worth more, and those stay: `board.rtc_on_bus`
against the modelled `hw.rtc` capability, the caps rows against the profile, and
the Plus2 flash part against its image.

## Still needs hardware

- BM8563 countdown semantics against the datasheet: register values, the exact
  minute-rounding, and whether the alarm flag survives the power cut with the
  cell fitted. The model follows the M5Unified driver and the tree marks the
  datasheet as not opened.
- Emitter polarity on the StickC family, and the LED active-low assumption plan
  23 already flags. The model reports the row: `hw.ir_mark_level` is the level
  the emitter pin holds during a mark, so the StickC and Plus read `low` and the
  Plus2 `high`. A bench check settles whether each row's `irMarkLow` is right,
  which is the same question as whether G19 needs inverting.
- MPU6886 wake-on-motion threshold LSB. The 4 mg per count figure has no
  datasheet section in the tree; a bench measurement of the first acceleration
  that asserts INT closes it. Plan 332 carries the question.
- Plus2 RTC backing across a real power cut. The row claims it (`true`, #505)
  and the model reports the row; nothing in the tree measures the cell or the
  rail through an actual cut.
- Plus2 held power with the HOLD latch released, and the wake from an RTC alarm:
  the simulator models the ordering and the countdown, not the electrical hold.
- Port A rail level at boot on the Core2 and Tough, which the model reports as
  `unknown` rather than guessing.
- The re-power half of a Port A rail cycle. The fake CASIC receiver keeps
  answering after `Platform::setRail(GPS, false)`, so the GPS state machine sees
  a live receiver with the rail cut and never re-enables it in the modelled
  window. `hw-stickc-gps-rail-boot.txt` therefore stops at "the firmware drove
  the cut itself". Teaching the receiver to stop on a cut, or asserting the
  re-power on hardware, is follow-up work.

## Verification

Run on 2026-11-27 in the `fix/stickc-sim-align` work tree, on top of master at
`d31b8f95`.

Gates:

- `python3 tools/check_sim_scenarios.py`: "sim scenario manifest is complete".
- `python3 tools/check_sim_coverage.py`: 228 hardware entries all modeled, 23
  crash entries all modeled, 11 fuzz targets, 6912 permission checks.
- `sh sim/scripts/check-doc-tokens.sh`: "sim documentation tokens are present".
- `python3 tools/check_doc_citations.py --root .`: 0 errors over 3785 citations
  in 206 files. The guard edit moves lines in `src/FurblePlatform.cpp`, so the
  citations in plan 13 and plan 26 were re-pointed to the new lines.
- `python3 tools/pre_review.py --base origin/master`: PASS, 14 gates.

Simulator builds, both exit 0: `sh sim/build.sh` for the default M5StickS3
panel, and `FURBLE_SIM_FURBLE_BOARD=FURBLE_M5STICKC
FURBLE_SIM_M5GFX_BOARD=board_M5StickC` into a separate build directory for the
board the new scenarios are registered against.

Scenarios:

- `sh sim/scripts/run-e2e.sh`, the runner's default `m5stick-s3` board: 277 of
  277 pass. Master's own list for that board is the same 277, because the six new
  scenarios are registered to `m5stick-c` only.
- `FURBLE_SIM_BIN=sim/build-c/furble-sim FURBLE_SIM_BOARD_ID=m5stick-c sh
  sim/scripts/run-e2e.sh`: 72 of 72 pass, against 66 on master. The six extra are
  `hw-stickc-capabilities`, `hw-stickc-plus-capabilities`,
  `hw-stickc-plus2-capabilities`, `hw-plus2-rtc-wake`,
  `hw-plus2-shared-led-ir` and `hw-stickc-gps-rail-boot`.

The rehearsal that produced this change reported 240 of 247 for the default
board before a full disk stopped it. That 247 was that tree's list; master has
added 30 certified end-to-end scenarios to the same list since, which is the 277
above. Of the seven the rehearsal failed, two cannot have passed and are fixed
here: `hw-plus2-shared-led-ir` and `hw-stickc-capabilities` use `ir-frame` and
`feedback-light`, which the branch declared in the action parser and never
wired to the model, so their assertions on `ir.frames`, `ir.busy` and
`ir.led_conflicts` had nothing to read. The other five are not recoverable from
the patch, and this tree fails none of them.

### Review round 1

The PR 555 review found six defects and they are fixed in this revision: the
duplicate `hw.led_active_low` handler and its dead caps copy are gone with the
`ledActiveLow` and `irMarkLow` columns; `hw.ir_mark_level` reports the row
instead of a constant; the board row now seeds the modelled rail and
`platform.rail` reads it; and this plan, the Tough comment and the PR body say
what the tree actually holds.

Each of the four row reads was probed by flipping one row column, rebuilding and
running the scenario that pins it, so the asserts are known to be able to fail:

```text
flip StickC ledActiveLow -> ASSERT FAILED: hw.led_active_low expected 'yes' got 'no'
flip StickC irMarkLow    -> ASSERT FAILED: hw.ir_mark_low expected 'yes' got 'no'
flip Plus2 irMarkLow     -> ASSERT FAILED: hw.ir_mark_level expected 'high' got 'low'
flip StickC railBoot     -> ASSERT FAILED: hw.rail_boot expected 'on' got 'off'
```

Evidence lines from the run:

```text
assert ok: rtc.plan.100 = accepted:ok:seconds:100
assert ok: rtc.plan.269 = accepted:ok:seconds:255
assert ok: rtc.plan.270 = rejected:late:minutes:300
assert ok: rtc.plan.815 = rejected:late:minutes:840
assert ok: rtc.plan.15360 = accepted:ok:minutes:15300
assert ok: rtc.plan.15361 = rejected:early:minutes:15300
assert ok: pmic.hold_latch = asserted
assert ok: pmic.hold_order = before-ready
assert ok: hw.part_psram = quad
assert ok: mem.psram_total = 0
XFAIL (WILL_FAIL): ir.led_conflicts expected '0' got '1'
All 277 end-to-end scenarios passed.
All 72 end-to-end scenarios passed.
```
