# 348: align the StickS3 simulator model with the verified board facts

State: implemented on `fix/sticks3-sim-align`. Simulator and model only. No
production behaviour changes: the shared sources are compiled unchanged and the
one production header that moves carries simulator-only declarations.

## Why

An independent board-level pass over the StickS3 found five places where
`sim/` describes hardware that is not there, or describes real hardware in a way
that cannot express the fault the board can actually have. A model that is
wrong in the confident direction is worse than no model: a scenario passes, and
the pass is cited as evidence.

The five findings:

1. The model claimed an RTC on the StickS3.
2. IR was modelled as always available, although both ends sit behind the
   switched 5 V output.
3. `Platform::setRail()` returned the fake receiver's flag instead of a PMIC
   read-back, and nothing modelled the output as shared.
4. The panel-off path was a local bool, so a lost read-modify-write could not be
   expressed even though the firmware edits one register byte.
5. The M5PM1 shim carried only the two symbols the old simulator path used, so
   the production PMIC register model (GPIO4, the IRQ latches, the timer action,
   the wake source) was not represented at all.

Each finding below records the verified fact and its source, the model change,
and the scenario that fails if the change is reverted.

## 1. The board has no RTC

Verified fact: the StickS3 has no calendar RTC chip. Sources:
`docs/console-commands.md` records that M5Unified finds no calendar RTC chip on
the StickS3 or the Core, and `src/FurbleBoard.cpp` gives the StickS3 profile
`rtcBatteryBacked = false` with `TimedWake::PMIC_TIMER`, so the timed wake it
does have is the PM1 timer and not an RTC alarm.

Model change, `sim/hardware_model.cpp`:

- `STICKS3.rtc` is `false`.
- A new `RTC_FACTS` table records one verified row per profile with the source
  it came from: StickC, StickC Plus, StickC Plus2, Core2 and Tough have a
  BM8563; StickS3 and Core have no RTC chip; the headless S3 and the Waveshare
  StampS3 add none.
- The check found three more wrong profiles the board pass had not reached: the
  model also claimed an RTC for the Core, the headless S3 and the Waveshare
  build. All three now say no, which is what `docs/console-commands.md` and the
  `docs/power/board-power.md` headless row record. `hw-profile-core-uart.txt`,
  `hw-profile-headless.txt`, `hw-profile-waveshare.txt`,
  `hw-pmic-ip5306-core.txt`, `hw-audio-ir-sd-faults.txt` and the Core and
  WaveShare boundary scripts are the regression surface. The four profile
  scenarios asserted the old value, so each now reads `hw.rtc no` with
  `hw.rtc_capability match` beside it, and `hw-pmic-ip5306-core.txt` joins the
  list because the Core profile it seeds has the same correct answer.
  `hw-audio-ir-sd-faults.txt` and the boundary scripts needed no change.
- `rtcCapabilityMatchesLocked()` compares the selected profile with its row when
  a profile is selected and again at `hwStart()`. A mismatch logs `SIM FAIL`,
  names the row, and sets the model's fail-closed flag. It does not exit, so a
  scenario can assert the pair itself.
- `hw.rtc_capability` reports `match` or `mismatch`, and `hw.fail_closed`
  exposes the flag.

Scenario evidence: `sim/scenarios/e2e/hw-rtc-capability.txt` asserts
`hw.rtc no`, `hw.rtc_capability match` and `hw.fail_closed no` for the StickS3
profile, then flips the modelled capability with `action hw-rtc-mismatch` and
asserts the check reports the mismatch and logs the row it disagrees with.
Reverting `.rtc` to `true` fails the first assert; removing the check fails the
second block. `hw-psram-octal-sticks3.txt` also changed from `hw.rtc yes` to
`hw.rtc no` with `hw.rtc_capability match` beside it.

## 2. IR sits behind the switched 5 V output

Verified fact: on this board the IR LED and the IR receiver are supplied from
the PM1 EXT 5 V output, the same output the Port A rail uses
(`M5PM1 v1.0.6 M5PM1.h` register 0x06 `BOOST_EN`, `src/FurbleBoard.cpp` IR pin
46 on the S3 image). The model reported `ir.state idle` with no path to
unpowered, so an IR transfer could not fail for the reason it fails on hardware.

Model change, `sim/hardware_model.cpp`:

- `ir.state` is derived: `absent` when the profile has no IR, `transmitting`
  while a transfer window is open, `unpowered` while the switched output is off,
  `timeout` after a modelled RMT timeout, `idle` otherwise. The rail dependency
  applies to the M5PM1 boards, where the rail and the IR parts share the one
  output; on the AXP and IP5306 boards the modelled rail is the Port A supply and
  the IR emitter is its own GPIO, so the two are independent there.
- `hwIrStartTransfer()` refuses when the rail is off and counts the failure;
  `ir.failures` and `ir.transmits` expose the counters.
- The transfer window is a lowest-level hardware event injection, like the
  existing `ir-timeout`, because the host has no RMT peripheral: `action
  ir-transmit` opens it and `action ir-transmit-end` closes it.

Scenario evidence: `sim/scenarios/e2e/hw-ir-rail-cut-in-flight.txt` asserts
`ir.state idle`, `transmitting` and then `unpowered` with `ir.failures 1` after
the rail is cut, and `ir.transmits` stays at the one transfer that ran.

## 3. EXT 5 V is one output with more than one holder

Verified fact: the board has a single switched 5 V output. `Platform::setRail()`
asks the PMIC for it, and `M5.Power.getExtOutput()` is the read-back
(`src/FurblePlatform.cpp`). The simulator returned the fake receiver's own flag
(`furble_sim_uart_rail_on()`), so the answer the GPS driver saw was decided by
the receiver model, not by the PMIC.

Model change:

- `sim/hardware_model.cpp` owns the output: `rail.on`, the two holder requests
  (`gps`, `ir`), the transfer window, a refusal counter and the fixed/refused
  capability seeds. The output bit is `PWR_CFG BOOST_EN`, so the rail and the
  register cannot disagree.
- `hwSetRail(holder, wanted)` returns the read-back. A cut is refused while
  another holder still needs the output, and a request the board cannot honour
  is counted in `rail.refusals`.
- `sim/FurblePlatformSim.cpp` asks the model as the GPS holder, mirrors the
  read-back into the fake receiver, and returns the read-back, which is the
  firmware contract. `canCutGpsRail()` reads the model's `rail.fixed`.
- `sim/fake_uart.cpp` no longer clamps the rail. `furble_sim_uart_set_rail()` is
  the receiver's view of the model's answer, so one owner decides.
- `seed gps_rail_fixed` and `seed gps_rail_refused` now seed the model
  (`hwSetRailFixed`, `hwSetRailRefused`) instead of the fake.

Scenario evidence: `sim/scenarios/e2e/hw-ir-rail-cut-in-flight.txt` opens an IR
transfer, asserts `rail.holders gps+ir`, turns GPS off, and asserts the rail is
still `on` with `rail.refusals` at least 1. After the transfer ends and no
holder wants it, the cut takes and `rail.state off`. Cutting the rail for the
GPS holder without checking the IR holder fails the first post-toggle assert.
The existing `gps-ubx-rail-fixed.txt` and `gps-ubx-rail-refused.txt` keep
passing through the model-owned refusal path.

## 4. One PWR_CFG byte for the rail, the LED and the charge enable

Verified fact: `M5PM1 v1.0.6 M5PM1.h` register 0x06 is one byte holding
`CHG_EN [0]`, `DCDC_EN [1]`, `LDO_EN [2]`, `BOOST_EN [3]` and `LED_CTRL [4]`.
The firmware panel-off path reads that byte, saves the LED level, clears it, and
restores it on wake (`src/FurblePlatform.cpp` `Platform::setDisplayOff`). The
simulator kept the panel-off state in a file-local bool, so a write built from a
read that a rail change had invalidated was not representable at all.

Model change:

- `pmic.pwrCfg` is the one byte. The model keeps two values, because the board
  does: `PWR_CFG_RESET` is the M5PM1-UM register default 0x17 with the 5 V boost
  off (`docs/components/m5pm1.md`, M5PM1-UM section 2 register (7), page 13 of
  33), and `PWR_CFG_BOOT` adds `BOOST_EN` (0x1F) because M5Unified's
  `output_power` config defaults to true and `M5Unified::_begin()` calls
  `Power.setExtOutput(cfg.output_power)`. `hwStart()` and the watchdog power
  cycle apply the reset byte and then the modelled output-power step.
- `pwrCfgWriteMaskedLocked()` applies a masked write and, when the write carries
  a stale copy of the bits outside its mask, records `pmic.pwr_cfg_lost`. The
  rail follows the byte, so a lost update moves the output rather than being
  invisible.
- `Platform::setDisplayOff()` in the simulator drives `hwSetDisplayOff()`, which
  does the same save, clear and restore on the byte, and `platform.display_off`
  reads the LED state back.
- `seed pmic_pwr_cfg_stale` keeps the post-init snapshot for the next LED write,
  which is what an interleaved rail change leaves behind.

Scenario evidence: `sim/scenarios/e2e/hw-pmic-pwr-cfg-lost-update.txt` asserts
31 (0x1F) after the modelled M5Unified output-power step, 23 (0x17) after the
GPS cut, then 15 (0x0F) with `pmic.pwr_cfg_lost 1` and `rail.state on` after the
inactivity sleep writes the byte it read after boot. Modelling panel-off as its
own flag leaves the counter at 0 and the rail off, so the asserts fail.

## 5. The M5PM1 shim now models the registers the production path uses

Verified fact: the production StickS3 path uses GPIO4 as an input with a
pull-up and push-pull drive, maps it to the IRQ function, sets the GPIO4 mask,
enables a falling-edge wake, reads and clears the GPIO status latch, reads the
wake source, and arms the timer with `timerSet(seconds, action)`
(`src/FurblePlatform.cpp`). `M5PM1 v1.0.6` documents every one of those
registers: `REG_IRQ_STATUS1 0x40` and `REG_IRQ_STATUS2 0x41` are write-0-to-clear
latches, `REG_IRQ_MASK1 0x43` is 0 for enabled and 1 for masked,
`REG_GPIO_FUNC1 0x17` selects IRQ on GPIO4, `REG_WAKE_SRC 0x05` bit 0 is the
timer wake, `REG_TIM_CFG 0x3C [2:0]` action 011 is power on, and
`REG_WDT_CNT 0x0A` documents "system resets when countdown reaches 0".

Model change, `sim/shim/M5PM1.h`:

- Every symbol the production path names is present: `M5PM1_GPIO_NUM_4`,
  `M5PM1_GPIO_MODE_INPUT`, `M5PM1_GPIO_PULL_UP`, `M5PM1_GPIO_DRIVE_PUSHPULL`,
  `M5PM1_GPIO_FUNC_IRQ`, `M5PM1_IRQ_GPIO4`, `M5PM1_IRQ_MASK_ENABLE` and
  `M5PM1_IRQ_MASK_DISABLE`, `M5PM1_GPIO_WAKE_FALLING`, `M5PM1_CLEAN_ONCE`,
  `M5PM1_WAKE_SRC_TIM`, `M5PM1_TIM_ACTION_POWERON`, plus the Status 2 system
  bits, and `timerSet(seconds, action)` beside the one-argument form. Each
  constant carries the vendor register it comes from.
- IRQ Status 1 and Status 2 are latches. A read returns them unchanged; only a
  write clears a bit, and the clear is the vendor's write-0-to-clear
  (`status &= value`). `irqGetGpioStatus` and `irqGetSysStatus` implement
  `CLEAN_ONCE` and `CLEAN_ALL` the way `M5PM1.cpp` does.
- `irqLineAsserted()` is the aggregated line the PMIC drives into the SoC:
  Status 1 or Status 2 with the corresponding mask removed.
- Watchdog expiry is a system power cycle, not a flag. `serviceWatchdog()`
  counts the cycle, clears the latches, restarts the countdown the way the
  SoC's boot does, and calls the board hook. The simulator installs
  `Sim::hwNoteWatchdogCycle()`, which records the cycle, the reset reason
  `pmic_watchdog` and the outputs returning at their reset values. No process
  restart is claimed: the host has no reset line.

Scenario evidence:

- `sim/scenarios/e2e/hw-pmic-5vin-latch-blocks-wake.txt` latches a 5VIN event in
  Status 2 (`action pmic-irq-5vin`), asserts the aggregated line is `asserted`
  and `platform.motion_wake blocked`, re-arms the motion source and shows the
  latch survives it (the motion path clears Status 1 only), then clears Status 2
  with a write and asserts the line is `idle` and the wake path reads `armed`
  again. Dropping the Status 2 term from the aggregated line fails the blocked
  asserts; clearing on read instead of on write fails the status asserts.
- `watchdog-stall.txt` and `clock-wrap.txt` now assert `platform.watchdog
  cycled` with `pmic.watchdog_cycles 1`, and `watchdog-stall.txt` also asserts
  the `pmic_watchdog` reset reason. Their intent is unchanged: the retained PM1
  deadline crossed. What changed is that the crossing is now modelled as what
  the chip does.
- `tests/host/pmic_recovery_test.cpp` and `tests/host/sim_clock_test.cpp` cover
  the same model directly: the latch survives a read, a masked bit releases the
  line, `CLEAN_ONCE` clears one GPIO bit, and one expiry is one power cycle.

## Simulator runtime support added for the above

- `sim/platform_state.h`, `sim/FurblePlatformSim.cpp`: `motionWakeState`,
  `pmicIrqState`, `pmicIrqStatus`, `pmicInjectSystemIrq`, `pmicClearSystemIrq`,
  and the power-cycle hook installation. `Platform::update()` polls the PM1
  deadlines so a scripted stall is acted on.
- `include/FurblePlatform.h`: those six functions are declared and befriended
  under `FURBLE_SIM`, beside the existing `Sim::watchdogState` accessor.
  Firmware has no callers.
- `sim/driver.cpp`: the new query keys, the three new seeds on the model, and
  the two PMIC event actions.
- `sim/scenario_action.cpp`: `ir-transmit`, `ir-transmit-end`,
  `hw-rtc-mismatch`, `pmic-irq-5vin`, `pmic-irq-clear` in both the validation
  and the canonical lists.
- `sim/scenarios/manifest.json`: four new certified `e2e` entries, all on the
  `m5stick-s3` board matrix.
- `sim/coverage/hardware_quirks.json`: six new modelled entries, each pointing
  at one of the new scenarios and naming assertions that scenario contains.
- `docs/sim.md`, `sim/CLAUDE.md`, `include/CLAUDE.md`, `tests/host/CLAUDE.md`:
  the vocabulary, the register citations and the seam list.

## Gates

Run in `/tmp/furble-s3sim` on `fix/sticks3-sim-align` rebased onto `a5cb2e14`,
all passing:

- `python3 tools/check_sim_scenarios.py`: manifest complete, 420 scenarios, and
  all 245 certified e2e entries for the m5stick-s3 board still present.
- `python3 tools/check_sim_coverage.py`: 204/204 hardware, 20/20 crash, 11/11
  fuzz and 6912/6912 permutations modelled.
- `sh sim/scripts/check-doc-tokens.sh`: tokens present.
- `python3 tools/pre_review.py --base origin/master`: 10 gates pass.
- Host suite: `cmake -S tests/host -B /tmp/furble-s3sim-host
  -DCMAKE_BUILD_TYPE=Debug`, `cmake --build /tmp/furble-s3sim-host --parallel 6`
  and `ctest --test-dir /tmp/furble-s3sim-host -j 4`: 196/196 tests pass,
  including `pmic_recovery_test` and `sim_clock_test`, which cover the latch and
  power-cycle model directly.
- The four new scenarios exit 0 under the dummy SDL drivers with the
  `sim/scripts/run-e2e.sh` environment (`FURBLE_SIM_IR`, `FURBLE_SIM_FEEDBACK`
  and `FURBLE_SIM_SD` set, which the capability and GPX paths need):
  `hw-rtc-capability`, `hw-pmic-5vin-latch-blocks-wake`,
  `hw-ir-rail-cut-in-flight`, `hw-pmic-pwr-cfg-lost-update`.
- The full certified e2e set for the m5stick-s3 board, 245 scenarios, passes
  245/245.
- The adjacent set passes 43/43: `hw-psram-octal-sticks3`, `watchdog-stall`,
  `clock-wrap`, `watchdog-before`, `watchdog-feed`, `clock-wrap-before`,
  `pmic-flash-recovery`, `watchdog-disabled`, `gps-ubx-rail-fixed`,
  `gps-ubx-rail-refused`, `gps-fixed-baud-absent`, `gps-duty-standby-cycle`,
  `console-mode-led-off`, `screen-off-flush-gate`, `display-dim-then-off`,
  `imu-gesture-wake-tap`, `imu-gesture-wake-shake`, `hw-audio-ir-sd-faults`,
  `hw-profile-{stickc,stickc-plus,core-uart,headless,tough,waveshare}`,
  `hw-pmic-{sticks3,axp192-core2,ip5306-core,brownout-crash}`,
  `hw-motion-{light-sleep,no-wake-when-off}`, `hw-power-button-state-machine`,
  `hw-crash-reset-matrix`, `hw-psram-{absent-core,init-fail}`,
  `hw-memory-webui-first-boot`, `hw-boot-settings-migration`,
  `blind-remote-shutter`, `gps-autobaud-ladder`, `gps-ubx-standby`,
  `gps-no-receiver`.
- `sh sim/scripts/run-watchdog.sh`: the retained-PMIC boundary set passes.
- `sh sim/scripts/run-invalid.sh`: the malformed-script set passes.

The scenario gates ran against `sim/build/furble-sim` built from this tree by
`sh sim/build.sh`. This re-run is what caught the four profile scenarios that
still asserted `hw.rtc yes`; they are corrected above.

Still needs hardware. None of this is a device measurement:

- There is no differential trace of the EXT 5 V output against a scope, so the
  refusal behaviour is the model's reading of the PM1 register contract, not a
  measured recovery time or drop-out.
- The Status 2 latch behaviour is taken from the vendor register description.
  Whether a 5VIN event asserts PYG1_IRQ while the system is running is not
  documented and not measured; the model asserts it does, which is the
  pessimistic case for light sleep.
- The power-cycle consequence of a watchdog expiry is register-documented, but
  the reset reason a PM1 reset produces on the SoC, and the retained state that
  survives it, need a board capture.
- The RTC absence is a documented board fact. It is not a measurement of the
  ESP32-S3 internal RC clock drift that replaces it.

## Follow-up: the PWR_CFG reset default and the M5Unified rail enable

The retrospective review of PR 502 found that section 4 and
`hw-pmic-pwr-cfg-lost-update.txt` called 0x1F the boot value. That contradicts
`docs/components/m5pm1.md` row 1, which records the M5PM1-UM section 2 register
(7) default as 0x17 with the 5 V boost off. 0x1F is reachable only after
M5Unified's `output_power` config, which defaults to true, makes
`M5Unified::_begin()` call `Power.setExtOutput(cfg.output_power)`.

The model now carries both values: `PWR_CFG_RESET` (0x17, the documented reset
byte) and `PWR_CFG_BOOT` (0x1F, the post-init byte). `hwStart()` and
`hwNoteWatchdogCycle()` apply the reset byte and then
`applyBootOutputPowerLocked()`, which models the M5Unified step. The observed
post-init byte is unchanged, so the scenario asserts and the stale snapshot
stay valid. The point of the split is that the model no longer silently
pre-applies a firmware step: a future change that stops M5Unified enabling the
boost has a place to show up, and a reader can match the reset byte to the
vendor manual.

The review record is `plans/348-review-feedback.md`.
