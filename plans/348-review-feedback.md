# 348 - Review feedback for the StickS3 simulator alignment

## Scope

This is the retrospective, adversarial review of PR 502 (`9a617355`,
`fix/sticks3-sim-align`), which merged on green CI plus author verification
without an independent review. It was re-read at `c644f39e`, after #507 added
the modelled RTC chip, so the record covers the interaction as well as the
merged commit.

Verdict: one real defect was found. The model called 0x1F the `PWR_CFG` boot
value although the verified M5PM1-UM register default is 0x17 with the 5 V boost
off; 0x1F is only the post-`M5.begin` state. It is fixed in this PR. The rest of
the review found nits and limitations, recorded below with the attacks that
produced them.

## Finding 1: the PWR_CFG boot value contradicted the verified manual

Status: fixed in this PR.

`sim/hardware_model.cpp` set the byte to `PWR_CFG_BOOT` = 0x1F and
`plans/348-sticks3-sim-align.md` section 4 plus the scenario comment called that
the boot value. `docs/components/m5pm1.md` row 1 records the M5PM1-UM section 2
register (7), page 13 of 33, default as **0x17**, "LED enable, 5 V boost off,
3.3 V LDO on, 3.3 V DCDC on and charge on". The boost is off at reset.

0x1F is the post-init byte because M5Unified's `config_t::output_power` defaults
to true (`M5Unified.hpp:133`) and `M5Unified::_begin()` calls
`Power.setExtOutput(cfg.output_power)` (`M5Unified.cpp:2015`), which sets
`BOOST_EN` on the StickS3 M5PM1 path. The model hardcoded that firmware step as
part of "boot" and never represented the reset byte, so the plan and scenario
asserted a value the chip does not have. That is also the one place where the
model could mask a firmware change: setting `output_power` false, or a failed
`setExtOutput`, would still leave the modelled rail on.

The fix keeps both steps: `PWR_CFG_RESET` is 0x17 with the manual citation,
`PWR_CFG_BOOT` adds `BOOST_EN`, and `hwStart()` and `hwNoteWatchdogCycle()` apply
the reset byte and then `applyBootOutputPowerLocked()` for the M5Unified step.
The post-init observable is unchanged, so the scenario asserts and the stale
snapshot still hold. The section 4 text, the scenario comment and the follow-up
in `plans/348-sticks3-sim-align.md` now say "post-init", not "boot".

## Finding 2: the RTC rows match the profile table and the vendor facts

Status: no defect.

Every `RTC_FACTS` row was compared with `src/FurbleBoard.cpp` and a vendor
source:

| Profile | `RTC_FACTS` | `src/FurbleBoard.cpp` | Vendor check |
| :--- | :--- | :--- | :--- |
| `stickc` | yes | `rtcBatteryBacked = true` | M5Stack StickC page: "RTC BM8563" |
| `stickc-plus` | yes | `true` | M5Stack StickC Plus page: "RTC BM8563" |
| `stickc-plus2` | yes | `rtcBatteryBacked = false`, `TimedWake::RTC_ALARM` | M5Stack StickC Plus2 page: "RTC BM8563" |
| `sticks3` | no | `false`, `PMIC_TIMER` | `docs/console-commands.md`: no calendar RTC chip |
| `core` | no | `false`, `TimedWake::NONE` | `docs/console-commands.md`: no calendar RTC chip |
| `core2` | yes | `true` | `docs/power/board-power.md`: BM8563 |
| `tough` | yes | `rtcBatteryBacked = false` | `docs/power/board-power.md`: Core2 row BM8563 |
| `headless-s3` | no | no board row | `docs/power/board-power.md`: no RTC |
| `waveshare-s3-eth` | no | UNKNOWN row | Waveshare ESP32-S3-ETH resource list: no RTC or IMU |

The "chip present" and "battery backed" questions stay separate, which is what
lets the Plus2 and the Tough carry a chip that is not battery-backed. The four
corrected profile scenarios (`hw-profile-core-uart`, `hw-profile-headless`,
`hw-profile-waveshare`, `hw-pmic-ip5306-core`) assert the corrected values with
`hw.rtc_capability match`. All four pass on the built binaries.

The prompt's "octal PSRAM pin loss" is not in PR 502. The only PSRAM-adjacent
change is the RTC assert in `hw-psram-octal-sticks3.txt`. The octal PSRAM
GPIO33-GPIO37 fact is recorded in `docs/power/board-power.md` and is not
modelled by this PR; that is a false premise in the review request, not a
finding against the change.

## Finding 3: the host latch test and the RTC scenario go red on a revert

Status: proven, no defect.

- Reverting `writeStatus1`/`writeStatus2` from write-0-to-clear to a plain
  write built `pmic_recovery_test` and failed at
  `FAIL: CLEAN_ONCE clears the GPIO4 latch`, exit 1. The same binary passes on
  master (`pmic recovery persistence tests passed`).
- Reverting `STICKS3.rtc` to `true` and rebuilding the simulator made
  `hw-rtc-capability.txt` fail with
  `ASSERT FAILED: hw.rtc expected 'no' got 'yes'`, exit 1. Restoring the row
  returns the scenario to exit 0.
- On the built master binary, the four new scenarios and the seven adjacent
  scenarios named in the plan all exit 0:
  `hw-rtc-capability`, `hw-ir-rail-cut-in-flight`,
  `hw-pmic-5vin-latch-blocks-wake`, `hw-pmic-pwr-cfg-lost-update`,
  `hw-profile-core-uart`, `hw-profile-headless`, `hw-profile-waveshare`,
  `hw-pmic-ip5306-core`, `hw-psram-octal-sticks3`, `watchdog-stall`,
  `clock-wrap`.
- `python3 tools/check_sim_scenarios.py`,
  `python3 tools/check_sim_coverage.py` and
  `sh sim/scripts/check-doc-tokens.sh` all pass.

## Finding 4: the rail contract is consistent

Status: no defect.

`setRailLocked()` returns the byte read-back, counts a request it cannot honour
in `rail.refusals`, and refuses a cut while `gpsWants`, `irWants` or
`irInFlight` holds the output. `sim/FurblePlatformSim.cpp::Platform::setRail()`
turns the read-back into `false` when the GPS asked for a state the model kept,
which is what `GPS::railOffOrRestart()` needs: on a refused cut with the GNSS
stopped it restarts the receiver (`m10.resets 2` in `gps-ubx-rail-refused`), and
on a fixed rail it never sends `CFG-RST` or `UPD-SOS` (`gps-ubx-rail-fixed`).
The `gps_rail_fixed` seed makes `canCutGpsRail()` false, which matches the
board-level meaning of "the rail cannot be switched"; `gps_rail_refused` leaves
it true, which matches the PMIC-refusal meaning. The old `uart.h` comment said
exactly that. No mismatch found.

## Finding 5: the M5PM1 shim matches the pinned vendor driver

Status: no defect found in the checked methods.

The shim's `irqGetGpioStatus`/`irqGetSysStatus` `CLEAN_ONCE` path writes the
inverted status, and `writeStatus1`/`writeStatus2` apply `status &= value`, the
same write-0-to-clear the vendor `M5PM1.cpp` uses. `irqSetGpioMask` reads,
ORs/ANDs one pin and writes back, matching `M5PM1::irqSetGpioMask`. The idle
first-transaction failure is preserved, so the production retry wrapper is still
exercised. `irqLineAsserted()` masks each status with its own mask register and
ORs the two, which is the one PYG1_IRQ line. The host test covers the latch
survives a read, a masked bit releases the line, `CLEAN_ONCE` clears one GPIO
bit, and one expiry is one power cycle.

## Finding 6: what the model can still mask

Status: limitations, recorded.

- The simulator compiles the production GPS path, so the refused-cut restart is
  a real firmware test. The production `Platform::setRail()` and
  `Platform::setDisplayOff()` bodies are replaced by `sim/FurblePlatformSim.cpp`
  and `Sim::hwSetDisplayOff()`. A regression inside those firmware bodies is not
  executed by the simulator. That is a pre-existing seam, not something PR 502
  introduced; the model now mirrors the body's two-step LED read-modify-write and
  the rail read-back.
- `hwNoteWatchdogCycle()` resets `PWR_CFG` and the rail but leaves
  `pmic.displayOff`, `statusLedSaved` and `statusLedLevel` as they were. A real
  power cycle starts the panel-off software state fresh. No scenario asserts
  display state after a watchdog cycle today, so this is a gap, not a failing
  scenario. A future display-after-watchdog scenario should reset those fields.
- `RTC_FACTS` checks only chip presence. #507 added `.rtcBatteryBacked` and
  `.timedWake` to each board row and they match `src/FurbleBoard.cpp` today, but
  nothing checks them. If a future board PR changes one of those columns, the
  presence check stays green. Extend `rtcCapabilityMatchesLocked()` when that
  surface next moves.
- The M5PM1 shim arms and clears the timer, but an expiry only clears
  `timerArmed`; it never sets `WAKE_SRC` bit 0. The simulator's
  `Platform::consumeTimedWake()` is shimmed to the `sim_timed_wake` preference,
  so the production `getWakeSource()` path is not exercised. A future scenario
  that wants to prove the PM1 timer wake source must add the bit on expiry and
  drop the preference seam. Today the deep-sleep scenarios use the preference.

## Finding 7: the plan was missing from the index

Status: fixed in this PR.

`plans/348-sticks3-sim-align.md` was not listed in `plans/README.md`, unlike
352 and 355. The index row is added with this review record.
