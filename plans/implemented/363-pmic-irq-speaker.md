# 363 - PMIC IRQ latches, the RTC IRQ write and StickS3 speaker timing

Status: implemented on `fix/pmic-irq-and-speaker`, rebased onto `0f3e8905`
after an independent review round. The plan was numbered 347 until master's
#524 took that number for the Core debug flash headroom, so it is 363 here and
every reference in the branch moved with it. The firmware lane builds and
passes the size, stack and pinning gates, and the host suite passes.
**Nothing here was measured on a StickS3**, so the settle time and the volume
ceiling are vendor-documented values, not measurements (see "Unverified" at the
end). The review findings and their dispositions are in
[363-review-feedback](363-review-feedback.md).

Related: [95](../95-engineering-lessons.md) (the hardware traps these findings
joined), [20](20-imu-hw-motion.md) (the PMIC motion wake path),
[23](23-feedback-outputs.md) (the speaker and IR rails),
[320](../320-rtc-use.md) (which boards have a usable RTC).

## Why

Three defects were reproduced from the vendor documentation and the source, and
one vendor warning had no code behind it at all.

1. **A 5VIN event holds the wake line for the rest of the session.**
   `M5PM1` IRQ Status 2 (0x41) covers 5VIN insert and remove. The manual's status
   note says a GPIO configured as IRQ is held low until the IRQ Status is
   cleared. The masks power up unmasked, nothing in furble writes IRQ_MASK2
   (0x44), and `clearMotionWake()` cleared only Status 1 (0x40). So inserting or
   removing USB left a Status 2 bit latched, PYG1_IRQ asserted on ESP32-S3
   GPIO13, and `armMotionWake()` armed a level-triggered light-sleep source on a
   line that never releases. `motionWakeSample()` then reported an idle IMU
   forever while the CPU could not stay in light sleep.
2. **A boot-window RTC write on a board with no RTC.**
   `Platform::getInstance()` called `M5.Rtc.disableIRQ()` whenever the board's
   timed-wake kind was not `RTC_ALARM`, which is every board without an RTC.
   The condition tested the timed-wake feature, not the presence of the chip.
3. **The first beep lost its attack.** `Feedback::beginSpeaker()` enabled the
   amplifier and `startTone()` toned immediately. The M5PM1 low-power guide,
   section 1 "SPK AMP", asks for about 1 s between enabling the amplifier and
   playback. The first fix parked the tone for that window but closed the
   amplifier after every pattern, so the settle belonged to each pattern instead
   of each amplifier enable and the beep landed a second after the LED and the
   vibration. Review round B1; the window is now per enable, see "Keep the
   session open across a burst".
4. **No volume ceiling on battery.** The StickS3 documentation carries a Speaker
   Volume Notice: keep the volume at or below 75 percent on battery, or the
   amplifier's current can brown the supply out and reboot the device.
   `startTone()` applied the stored 0 to 255 setting with no battery or rail
   check.
5. **A refused watchdog change was only in the platform log.** The first draft
   claimed the refusal was silent. It was not: `watchdogEnable()` already logs
   the failed set and the failed verify at ERROR
   (`src/FurblePlatform.cpp:262`, `:269`), and `m5pm1Access()` logs a WARN. The
   real defect was narrower. `src/main.cpp` discarded the bool, so a user
   reading the boot summary saw nothing, and the log line the first draft added
   said "arm refused" even on the disarm path, which is the normal call when
   the setting is off. Review round B4.

## Sources

- M5PM1 Chip User Manual V1.9, register map: IRQ Status 1 at 0x40 (GPIO0 to
  GPIO4), IRQ Status 2 at 0x41 (bit 0 5VIN insert, bit 1 5VIN remove, bit 2
  5VINOUT insert, bit 3 5VINOUT remove, bit 4 battery insert, bit 5 battery
  remove), masks at 0x43 and 0x44, and the status note that a GPIO configured
  as IRQ is held low until the IRQ Status is cleared. The manual states the hold
  rule and does not state the clear mechanism; the zero write below is the
  driver's, not the manual's.
- M5PM1 driver 1.0.6 (`M5PM1.cpp`, `.pio/libdeps/m5stick-s3`): `irqClearGpioAll()`
  and `irqClearSysAll()` both write 0x00, and `irqGetGpioStatus()` with
  `M5PM1_CLEAN_ONCE` writes the inverted read so only the asserted bits clear.
- M5PM1 low-power guide, section 1 "SPK AMP", about 1 s after enabling the
  amplifier before playback:
  https://docs.m5stack.com/en/arduino/m5sticks3/m5pm1
- StickS3 product documentation, Speaker Volume Notice (at or below 75 percent
  on battery, the amplifier's draw on 3V3_L3B_AU can cause a reboot):
  https://docs.m5stack.com/en/core/StickS3
- M5PM1 function reference for `irqGetSysStatus`, `irqClearSysAll`,
  `irqSetGpioMask`: https://github.com/m5stack/M5PM1/blob/main/README_FUNCTION_EN.md
- M5Unified 0.2.20 `utility/RTC_Class.cpp` and
  `utility/rtc/PCF8563_Class.cpp`: `RTC_Class::begin()` instantiates a
  `PCF8563_Class` at 0x51 for any board it has no dedicated class for, and keeps
  it only when `PCF8563_Class::begin()`'s register writes are acknowledged;
  `disableIRQ()` returns immediately when no instance survived. M5GFX
  `lgfx::i2c::writeRegister8` returns an error on a NACK, which is what makes an
  absent chip leave `M5.Rtc.isEnabled()` false.
- ESP-IDF v5.4 GPIO wakeup is level only (`gpio_wakeup_enable` accepts
  `GPIO_INTR_LOW_LEVEL` or `GPIO_INTR_HIGH_LEVEL`), which is why a line that
  never releases stops light sleep:
  https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/peripherals/gpio.html

## Design

### Clear Status 2, do not mask the 5VIN bits

Both were available and only one was implemented.

- The clear is the release the manual documents: "a GPIO configured as IRQ is
  held low until the IRQ Status is cleared". Clearing Status 2 therefore removes
  the hold on the documented mechanism.
- What a mask does to the aggregated output is not documented. A mask write
  would have been a guess about the chip's interrupt OR tree.
- A masked bit is still latched. Whatever reads Status 2 next, including a
  future charger or battery feature, would see a stale event, and the first
  unmask of that bit would raise the line immediately.
- Nothing writes IRQ_MASK2 today, so a boot-time mask would be the only thing
  between a stale 5VIN event and the line. A single clear at boot would not be
  enough either, because a USB event can arrive while the device is armed.

`PmicIrq::clearLatched()` in `include/FurblePmicIrq.h` writes Status 1 and then
Status 2 through the caller's retried access wrapper. The order matters and the
first draft had it backwards: Status 1 is what the motion poll reads and
clears, so an edge that lands between the two writes is latched after the
sequence has passed that register and survives to the next poll. Clearing
Status 2 first would clear Status 1 second and swallow exactly that edge, for
no gain, because nothing consumes the Status 2 bits. Review round N7.

The clear is defensive. No hardware run has shown the 5VIN latch actually
holding PYG1_IRQ low; the physical event path in
[plan 20](20-imu-hw-motion.md) is still uncertified ("the hardware
gate must pass before calling it certified"). The sequence is correct against
the manual's hold rule and the model, and that is all this plan claims. Review
round N8.

`clearLatched()` returns true only when both writes were accepted, so
`armMotionWake()` now fails when either clear fails and does not arm a level
wake on an unproven release. Review round N9. It is called from:

- `Platform::armMotionWake()`, replacing the Status 1-only clear, so a latch
  from earlier in the session cannot arm the wake on an asserted line.
- `Platform::disarmMotionWake()`, so a 5VIN event that arrived while armed does
  not leave the line asserted after the wake source is gone.
- `Platform::clearMotionWake()`, replacing the Status 1-only clear.
- `Platform::motionWakeSample()`, which keeps its existing Status 1 read with
  `M5PM1_CLEAN_ONCE` and adds a Status 2 clear. Status 1 is deliberately left to
  the read so an edge that arrives between the two accesses is not discarded.

The wake contract in `include/FurblePlatform.h` states all of this on
`armMotionWake()`, `motionWakeSample()` and `clearMotionWake()`.

### Gate the RTC write on the profile, not on the timed-wake kind

`Board::Profile` gains `rtcOnBus`: an RTC chip answers on the internal I2C bus.
Only the StickC Plus2 sets it, its BM8563 at 0x51. `Platform::getInstance()` now
writes:

```
if (board.rtcOnBus && (board.timedWake != Board::TimedWake::RTC_ALARM)) {
  M5.Rtc.disableIRQ();
}
```

The Plus2 stays excluded because `consumeTimedWake()` must still see its alarm
flag. Every other row skips a call that can only be a no-op or an unneeded write
on a board with no RTC chip. `Board::TimedWake` was the wrong field to test: it
answers "does this board have the timed-wake feature", and three boards answer
that question without having the chip.

The compound guard is unreachable for every shipped row, which is worth stating
rather than leaving to discovery. Only the Plus2 sets `rtcOnBus`, and its timed
wake is `RTC_ALARM`, so `rtcOnBus && (timedWake != RTC_ALARM)` is false
everywhere. The guard is a contract, not a live branch: it reaches the call the
moment a board with a reachable RTC and another timed-wake kind is added.
`board_profile_test.cpp` asserts the implication (`rtcOnBus` implies an
`RTC_ALARM` timed wake), so such a row fails the test and the decision is made
in review rather than by accident. Review round N1.

### Wait the amplifier settle time on the feedback timer

`beginSpeaker()` records `m_AmpSettleDeadline` (now + 1000 ms) and sets
`m_AmpSettlePending`. `startTone()` checks it: inside the window it parks the
first tone (`m_ToneSettlePending`, deadline in `m_ToneDeadline`) and returns.
The existing 10 ms LVGL feedback timer keeps running because `m_ToneCount` is
non-zero, and `update()` re-enters `startTone()` once the deadline passes. No
task sleeps, no mutex is taken, and the UI task is not blocked.

`AMP_SETTLE_MS` is the documented round number, not a measurement. If a hardware
sweep finds a shorter real settle it should replace it, in one place.

### Keep the session open across a burst (B1)

The first version of this fix had the settle in the right place and the session
lifetime in the wrong one. The speaker session was per pattern: `update()`
called `stopSpeaker()` after the final tone, `M5.Speaker.end()` powered the
codec down, and the next pattern's `beginSpeaker()` took a fresh deadline. So
every pattern's first tone was parked for a second, not just the first after
boot. A probe measured tones at 1010, 2080, 2191, 3281, 4351, 4511, 5601, 6671,
7741 and 8811 ms: a second late each time the amplifier had been closed. The
UI fires `SHUTTER_FIRED` at `src/FurbleUI.cpp:7753` and `COUNTDOWN` at `:10897`,
so the beep landed about a second after the LED and the vibration. The
`boundary-feedback` fixture hid it by calling `settleAmplifier()` after every
signal.

Three fixes were available and the idle window was chosen:

- Enable the speaker at boot and leave it enabled. This removes the delay
  everywhere, but it holds the codec and the amplifier rail up for the whole
  session, including display-off sleep, against the reason
  [plan 23](23-feedback-outputs.md) keeps the rail off between
  beeps. A power regression for one timing win.
- Park one tone after boot and never again. This contradicts the vendor guide:
  the settle is a property of the amplifier enable, and the amplifier really is
  off after `M5.Speaker.end()`, so a later cold session would play too early
  and lose its attack.
- Keep the amplifier session open after its last tone for
  `AMP_IDLE_TIMEOUT_MS` (10 s) and close it then. The settle is paid once per
  enable, a burst of feedback (countdown, shutter, connect, disconnect) never
  pays it again, and an idle device still returns the rail off. The longest
  burst the UI produces is the intervalometer: it announces its countdown on a
  one second cadence over the last three seconds (`src/FurbleUI.cpp:10894-10900`)
  and then fires the shutter (`:7753`), so ten seconds spans it with room for a
  connection event on the same action.

`update()` therefore records `m_SpeakerIdleDeadline` after the final tone
instead of calling `stopSpeaker()`, keeps the feedback timer running while the
amplifier is open, and closes the session when the deadline passes. The
`idle-window` mode of `boundary-feedback` is the regression: a pattern started
after a completed pattern inside the window sounds its first tone on the next
timer pass.

Two follow-ons belong to the same field:

- The power source is sampled once per pattern in `signal()`, not once per
  session. A session that now spans several patterns must not keep a stale
  "on battery" or, worse, a stale "on mains" verdict for the life of the
  window.
- Console mode skips `lv_task_handler()` (`src/FurbleUI.cpp`), so no LVGL timer
  runs and a parked tone would never be retried. `UI::setDisplayMode()` reports
  that through `Feedback::setTimerPathActive()`, and `startTone()` plays
  immediately instead of parking. The attack is lost, the beep is not. Review
  round N6.

The settle, the idle window and the power-source sample are all plain fields
carried from `signal()` to the timer callback with no lock, and
`include/FurbleFeedback.h` says so: a stale read can drop one retry, never
corrupt a pattern. Review round N10.

Both the settle and the power-source read are inside `#if FURBLE_HAS_M5PM1`.
That macro is the StickS3 amplifier, and a 1 s delay before the first tone on
the other five boards would delay every shutter beep for a rail that is not
documented to need it.

### Cap the amplifier, never the setting

`include/FurbleFeedbackPolicy.h` holds the policy:

- `MAX_BATTERY_VOLUME = 191`, 75 percent of the 0 to 255 range, rounded down.
- `onBatteryPower(telemetry, vbus_present)` is true when the board can tell its
  power source apart and has no positive VBUS reading. `vbus_present` must come
  from a measurement, never from the charge flag. A full battery still on the
  cable keeps VBUS asserted, so it still counts as mains.
- `effectiveVolume(configured, on_battery)` caps above the ceiling only.

`Feedback::onBatteryPower()` runs only in the M5PM1 image, for the reason the
settle does, and reads the platform battery status once per pattern, in
`signal()`, and repeats the read once whenever the VBUS reading is zero,
whatever the charge flag says. The repeat matters twice over: the M5PM1 sleeps
between transactions and its first access after the idle wake only wakes it, and
a failed read must not be believed.

### Fail safe on a failed read (B2)

The first version could conclude "mains power" from a failed PMIC read, which is
the unsafe direction for the amplifier. The chain is exact:

- M5Unified `I2C_Class::readRegister8()` returns `value_or(0)`, so a failed
  reading is 0 mV (`I2C_Class.cpp:83-88`).
- The StickS3 charge flag is `M5PM1_Class::getGPIOInput(gpio0)`, which returns
  false on a failed or uninitialised read (`M5PM1_Class.cpp:161-165`), and
  `Power_Class::isCharging()` maps false to `is_charging`
  (`Power_Class.cpp:2553-2558`).
- `Platform::getBatteryStatus()` therefore reports `vbus = 0` and
  `charging = true` for one failure, which is indistinguishable from a device on
  a charger.
- The old guard only re-read when `!status.charging`, so the failure suppressed
  its own retry, `FeedbackPolicy::onBatteryPower()` returned false, and the
  amplifier got the full stored volume, up to 255, on battery, with no log.

Two changes fix it. Mains power now requires a positive VBUS reading, so the
charge flag cannot make that decision at all. And the retry is keyed on the VBUS
reading alone, so it always runs when the reading is zero. The
`boundary-feedback` `sound` mode covers the signature directly:
`setBatteryPower(0, true)` must still cap 255 to 191, and
`feedback-volume-policy` pins the two-argument decision. Review round B2.

The reviewer also suggested reading the chip's own PWR_SRC (0x04 bits 2:0,
0 = 5VIN, 1 = 5VINOUT, 2 = BAT) through `M5.Power.M5pm1.getPowerSource()` so
the sample is one register read. That is not the release condition here, and
using it as one would rebuild the same bug: the vendor read is `value_or(0)`, so
a failed PWR_SRC read reports `vin`, which is mains. The positive VBUS
measurement is the only signal that cannot fail towards mains, and it is already
a single block read of VIN_L/VIN_H through the platform's VBUS gate. The
`FurbleFeedbackPolicy` ceiling also must not depend on a board-specific chip
access, because it is the one place the arithmetic is testable.

The result lives in `m_SessionOnBattery` for every tone in that pattern, so a
slider drag cannot turn into an I2C storm. `applyVolume()` is the only place
that calls `M5.Speaker.setVolume()` while a session is open, and it logs the cap
once per boot at WARN. The stored setting and the NVS record are never touched:
the Settings menu still shows and stores the user's 0 to 255 value.

### Report a refused watchdog change, keep booting (B4)

`watchdogEnable()` already logs both failure points at ERROR, and `m5pm1Access()`
logs a WARN, so the premise that the refusal was silent was false. What
`app_main()` adds is one neutral WARN naming the requested state,
`M5PM1 watchdog arm refused; continuing` or `M5PM1 watchdog disable refused;
continuing`, and then it keeps booting. The arm-only wording was wrong for the
disarm path, which is the normal call when the setting is off. It does not abort
or restart: a refused change is a degraded state, while a boot loop on that path
would remove every other function. Nothing is logged twice: this is the one line
that says which change was refused, not a replacement for the platform's ERROR.
`boundary-headless-service-watchdog-refused` runs the unchanged `src/main.cpp`
with the platform double returning false and asserts the boot still reaches
`boot.finish`, that the refusal was observed once, and that the arm line reached
the boundary log store.

## Changes

- `include/FurblePmicIrq.h` (new): the latch map, the clear rationale and
  `clearLatched()`.
- `include/FurbleFeedbackPolicy.h` (new): the volume ceiling and the power-source
  decision.
- `include/FurbleBoard.h`, `src/FurbleBoard.cpp`: the `rtcOnBus` column.
- `include/FurblePlatform.h`, `src/FurblePlatform.cpp`: the RTC guard, the
  `clearPmicIrqLatches()` helper, and the four call sites.
- `include/FurbleFeedback.h`, `src/FurbleFeedback.cpp`: the settle window, the
  volume cap and the one power-source read per session.
- `sim/shim/M5PM1.h`: IRQ Status 1 and 2 as latches, zero-write clears, mask
  registers recorded, and an aggregated-line model.
- `src/main.cpp`: one neutral WARN naming the refused watchdog state (`arm` or
  `disable`) and the boot continues.
- `tests/host/headless/headless_state.h`, `headless_doubles.cpp`,
  `headless_service_test.cpp`: a configurable `watchdogEnable()` result, a
  refusal counter, and the `watchdog-refused` boot case.
- `tests/host/pmic_irq_test.cpp` (new), which covers the header and the model,
  and `tests/host/feedback_volume_policy_test.cpp` (new), registered in
  `tests/host/CMakeLists.txt`.
- `tests/host/platform/` (new): the `platform-wake` target, which compiles
  `src/FurblePlatform.cpp` against `sim/shim/M5PM1.h`, with the M5Unified, ESP
  GPIO/sleep/timer and boot-mark stubs and a forwarding `M5PM1.h`.
- `sim/shim/M5PM1.h`: the GPIO configuration, power-source, LED, timed-wake and
  RTC RAM surface the production translation unit calls, plus the retained
  state a test reads back.
- `src/FurbleUI.cpp`: `setDisplayMode()` reports the console-mode timer path to
  the feedback driver.
- `tests/host/board_profile_test.cpp`: the new column.
- `CLAUDE.md`, `plans/95-engineering-lessons.md`, `docs/power/gps-imu.md`,
  `plans/322-hardware-abstraction.md`: the traps, the poll cost and the profile
  row count.

## Tests

### `pmic-irq` (header and model)

This target was misdescribed in the first draft as production coverage. It links
`include/FurblePmicIrq.h` and `sim/shim/M5PM1.h` and nothing else, so it cannot
fail on the pre-fix tree: the pre-fix tree has no header, and the sequence it
drives is the header's own. The production call sites now have their own target
below. Review round B3.

`pmic-irq` drives the clear sequence against the latching model:

| Scenario | Expected |
| --- | --- |
| Status 1 latches, a plain read does not clear it | line stays asserted after `M5PM1_CLEAN_NONE` |
| Status 1 with `M5PM1_CLEAN_ONCE` | only the returned bits clear, line releases |
| **Fix-absent scenario**: 5VIN insert with a GPIO4 event, then the old Status 1-only read and clear | Status 2 still holds the 5VIN bit and the line stays asserted with no motion pending |
| The same state through `PmicIrq::clearLatched()` | both status registers zero, line released |
| Clear as the first access after idle | retried once and still clears both registers |
| Mask write with a latched bit | mask recorded, line still asserted (documented limitation of the model) |
| Clear after `shutdown()` | reports failure instead of pretending success |

`feedback-volume-policy` covers both power states and the boundaries: 255 and
192 cap to 191 on battery, 191 and below pass through, 255 stays 255 on VBUS, a
zero VBUS reading is battery power whatever the charge flag says, and no
telemetry never claims battery power.

### `platform-wake` (production call sites)

`tests/host/platform` compiles the unchanged `src/FurblePlatform.cpp` against
`sim/shim/M5PM1.h` plus stubs for M5Unified, the ESP GPIO and light-sleep calls,
the microsecond clock and the boot marks, and drives the four production call
sites. It reads the PMIC through its own `M5PM1` handle because the shim's
retained state is static, which is what lets a test observe what production did.

| Step | Expected |
| --- | --- |
| 5VIN insert latched, then `armMotionWake()` | Status 1 and 2 both zero, line released, GPIO4 configured as an IRQ source with its wake enabled and its mask clear |
| GPIO4 and 5VIN latched, then `motionWakeSample()` | sample returns true, Status 1 and 2 both zero, line released |
| 5VIN remove alone, then `motionWakeSample()` | sample returns false, Status 2 zero, line released |
| GPIO4 and 5VIN latched, then `clearMotionWake()` | Status 1 and 2 both zero, line released |
| 5VIN latched, then `disarmMotionWake()` | wake disabled, GPIO4 masked, Status 2 zero, line released, later samples return false |
| 5VIN latched, then `armMotionWake()` again | wake re-enabled, line released |

**Proven by deletion.** Removing the `irqClearSysAll()` call from
`motionWakeSample()` makes the target fail with
`FAIL: motionWakeSample left IRQ Status 2 latched`, exit status 1. That run is
the reason the target exists.

The unrestructured parts are stated rather than implied: unreferenced platform
entry points reference `Control`, `SD` and `TimeKeeper`, which are the whole
application, so the target links with the platform linker's section discard
(`-Wl,-dead_strip` on Apple, `-Wl,--gc-sections` elsewhere). Without it the
target stops linking, which is loud, not silent.

### `boundary-feedback` (session lifetime and the volume cap)

The `idle-window` mode is the B1 regression and replaces the fixture's habit of
calling `settleAmplifier()` after every signal:

| Step | Expected |
| --- | --- |
| First pattern after boot | parked on the settle window, then sounds |
| Pattern after a completed pattern, inside the idle window | first tone on the next timer pass, no settle wait |
| Past `AMP_IDLE_TIMEOUT_MS`, then a new pattern | parked again, because the amplifier really was closed |
| Console mode (`setTimerPathActive(false)`) with a cold session | sounds immediately instead of parking |

The `sound` mode also covers `setBatteryPower(0, true)`, the failed-read
signature, and asserts the cap still applies.

`boundary-headless-service-watchdog-refused` runs the same production boot as
`boundary-headless-service` with `Platform::watchdogEnable()` returning false.
It asserts the full boot order still completes, the refusal happened once, the
error line is in the log, and the ordinary run logs no refusal.

## Implementation state

Branch `fix/pmic-irq-and-speaker`, rebased four times onto `origin/master` as it
moved: `92618bff` to `b57f0118` (no conflict), `b57f0118` to `a5cb2e14` (only
`plans/README.md`, where master's plan 328 row and this plan's row landed on the
same line; both rows are kept), `a5cb2e14` to `b711a44a` (only
`plans/322-hardware-abstraction.md`, where master added the `rtcBatteryBacked`
column and this branch adds `rtcOnBus`; master's T-Beam paragraph is kept and
only the example row grew the one new column), and `b711a44a` to `7caf0468`
(`plans/README.md` again, this time master's plan 342 rows against this plan's
row, both kept, and `tests/host/boundaries/boundary_support.cpp`, where master's
new `gpioEdges()` and this branch's `setBatteryPower()` landed on the same line,
both kept). The review round below is folded into this tree. The exact pushed
head is recorded in the PR.

The last two rebases are not source-neutral. Master landed the panel dwell
correction (#473) in `src/FurbleIR.cpp`, `src/FurbleUI.cpp` and
`include/FurbleUI.h` between `a5cb2e14` and `b711a44a`, and the lost-lease,
HAL-pacing and control-lock fixes (#479, #482, #485, #487) in
`src/FurbleControl.cpp`, `src/FurbleEthernet.cpp`, `src/FurbleWebUI.cpp`,
`src/FurbleWiFi.cpp` and `lib/hal/` between `b711a44a` and `7caf0468`. The only
overlap with this branch is `UI::setDisplayMode()`, which carries both master's
panel-off sequence and this branch's `setTimerPathActive(false)` call. Those
files are compiled by the host and simulator builds below and by the six-env
firmware matrix in CI.

A fifth rebase moved the branch from `7caf0468` to `6a82c71b`, and #497 (`535ad7d6`, the Core family hardware and power fixes) landed while this branch waited in CI, so the tree was rebased onto it too. Master had taken
plan number 347 for the Core debug headroom (#524) in the meantime, so this plan
renumbered to 363 together with its review record and every reference in the
branch; master's own `plans/347` citations were left alone. The conflicts were
`CLAUDE.md`, where master's expanded M5PM1 and USB Serial/JTAG traps meet this
branch's three new traps and the linked boundary case count moves from 27 to 29,
`include/CLAUDE.md`, which keeps master's pin-share, `canPowerOff` and
`powerOffPath` paragraphs and adds this branch's `rtcOnBus`, `FurblePmicIrq.h`
and `FurbleFeedbackPolicy.h` entries, `include/FurbleBoard.h`,
`src/FurbleBoard.cpp` and `tests/host/board_profile_test.cpp`, which keep
master's Waveshare row, its `powerOffPath`, `ledActiveLow` and `irMarkLow`
columns and its implication test and add the `rtcOnBus` column, true only on the
Plus2, `sim/shim/M5PM1.h`, which keeps master's register-shaped latch model and
re-adds this branch's power-config and RTC RAM surface plus the test helpers the
new targets call, `src/FurbleFeedback.cpp` (both includes),
`tests/host/CMakeLists.txt` (master's `platform-power-off` and this branch's
`platform-wake`), `tests/host/boundaries/boundary_support.cpp` (master's
`gpioPad()` and this branch's `setBatteryPower()`), `plans/README.md`, and
`plans/implemented/322-hardware-abstraction.md`, where master's 19 columns plus
plan 350's two plus this plan's one make 22. `platformio.ini`,
`docs/supported-hardware.md` and `docs/crash-diagnostics.md` needed no edit:
#524 had already put the Core debug lane back at `ESP_LOG_VERBOSE` and paid for
it with the BMI270 blob, which is what this plan needs. Eleven `file:line`
citations moved with the new lines in `include/FurbleBoard.h`, `src/main.cpp`,
`src/FurblePlatform.cpp` and `src/FurbleUI.cpp` and were repointed; the
documentation citation gate is clean.

A seventh rebase moved the branch from `535ad7d6` to `0f3e8905`, which is #510,
the GPS UART pin seam. Seven files conflicted, all with the same shape: master
adds the GPS columns and this branch adds `rtcOnBus`. `src/FurbleBoard.cpp` and
`tests/host/board_profile_test.cpp` keep master's rows, its GPS pin columns and
its `GpsPinSource::NONE` rows for the Core and the Waveshare, and take
`rtcOnBus` between `rtcBatteryBacked` and the IR pin, which is the order the
struct now uses. `include/CLAUDE.md` keeps this branch's `rtcOnBus` sentence on
the board-row bullet with master's `gpsPins()` bullet after it.
`tools/coupling_ratchet.json` keeps master's removal of the `src/FurbleGPS.cpp`
entry, which #510 caused by deleting that file's two `M5.getPin` calls, and this
branch's `src/FurbleFeedback.cpp` 15 to 14. Three citations rotated with the
rebase and were repointed: `UI::task` is at `src/FurbleUI.cpp:13464-13524`, the
5 ms `vTaskDelay` at `:13522`, and the three feedback fields at
`include/FurbleBoard.h:77-79`.

The rebase also exposed one integration defect a text merge cannot see.
`platform_wake_test` compiles the real `src/FurbleBoard.cpp`, and #510 added
`Board::gpsPins()` to it, which asks M5Unified for `m5::port_a_pin1` and
`port_a_pin2`. `tests/host/platform/shims/M5Unified.h` declared neither the
`pin_name_t` names nor `getPin`, so the target stopped building. The shim now
declares the enum with the three spellings the real header uses and answers
`getPin` with the missing-pin value. Nothing on the wake path reads a GPS pin,
so the stub still decides no test outcome.

| Gate | Result |
| --- | --- |
| `FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e m5stick-s3` | SUCCESS on the `92618bff` sources before the review round: RAM 47,480 of 327,680 (14.5%), flash 1,861,829 of 3,145,728 (59.2%). The seven warnings were pre-existing ones in `FurbleGPS.cpp`, `FurbleUI.cpp` and `FurbleWiFi.cpp`. Not re-run on this head; see below. |
| `FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e m5stack-core-debug` | SUCCESS on the rebased tree: RAM 56,244 of 327,680 (17.2%), flash 1,735,392 of 1,740,800 (99.7%). `tools/check_size_budget.py --env m5stack-core-debug` reports `flash 1,735,392/1,740,800, dram 56,244/57,344, iram 119,036/119,808`, so the OTA slot keeps 5,408 bytes. `tools/stack_budget.py --env m5stack-core-debug` passes every task and `tools/check_m5gfx_pinning.py --env m5stack-core-debug --require-source` passes. |
| Host `ctest` (`/tmp/host488`, `ctest -j 8`) | 219 of 219 passed in 64.8 s on the rebased tree, including `pmic-irq`, `platform-wake`, `feedback-volume-policy`, `boundary-headless-service-watchdog-refused`, `boundary-feedback-idle-window` and the updated `boundary-feedback` and `board-profile`. It was 203 of 203 on the fourth rebase, and master's later merges added the rest. |
| ASan/UBSan subset (`tools/run_sanitized_host_tests.sh`, `-R` over `boundary-feedback*`, `boundary-headless-service*`, `platform-wake`, `pmic-irq`, `feedback-volume-policy`, `board-profile`) | 12 of 12 passed, exit 0, on the fourth rebase. |
| `python3 tools/pre_review.py --base origin/master` | 10 of 10 gates: whitespace, diff hygiene, settings contracts, clang-format (31 changed files), C++ safety, CI workflow contract, simulator scenario manifest, simulator hardware coverage, coupling ratchet, scanner unit tests (140 tests). 0 failures, 1 warning: diff hygiene cannot map the `src/FurbleUI.cpp` change to a doc page token, because the change restores the master behaviour for console mode rather than adding a user-facing feature. |
| `sh sim/scripts/check-doc-tokens.sh` | `sim documentation tokens are present`. |
| Coupling ratchet | The shrink it demanded is recorded: `src/FurbleFeedback.cpp` `m5_calls` 15 to 14, because the volume application moved into `applyVolume()`. |
| `python3 tools/check_static_analysis.py --base origin/master --build-dir <host> --build-dir <sim>` | Clean: 6 sources and 6 headers, no findings, on the host and simulator databases. See below for the four-database CI equivalent. |

Re-run on this head, `0f3e8905`: host `ctest` 100% of 219 in 59.5 s,
`python3 tools/pre_review.py --base origin/master` PASS 13 gates,
`python3 tools/check_doc_citations.py` 0 errors of 3098 citations in 174 files,
`sh sim/scripts/check-doc-tokens.sh` present. `platform_wake_test` prints
`platform-wake: PASS` with all six steps `released`,
`boundary_feedback_test idle-window` prints `first_pattern=settled`,
`second_pattern=immediate`, `idle_close=1 recold=settled` and
`console_mode=played_and_closed`, and `boundary_feedback_test sound` prints
`power=battery stored=255 applied=191`, `power=vbus stored=255 applied=255` and
`power=zero-vbus-charging stored=255 applied=191`.

Behavioural checks on this head, all from `/tmp/furble-pmicfix-host-build2`:

- `boundary_feedback_test idle-window` printed `first_pattern=settled`,
  `second_pattern=immediate`, `idle_close=1 recold=settled` and
  `console_mode=played_and_closed`, so a pattern that follows a completed one
  sounds on the next timer pass and the console path plays instead of parking.
- `boundary_feedback_test sound` printed
  `power=zero-vbus-charging stored=255 applied=191` alongside
  `power=battery stored=255 applied=191` and `power=vbus stored=255 applied=255`.
- Deletion proofs for `platform-wake`, both reverted afterwards: deleting the
  Status 2 clear from `motionWakeSample()` printed
  `FAIL: motionWakeSample left IRQ Status 2 latched` and exited 1; reducing
  `PmicIrq::clearLatched()` to Status 1 printed
  `FAIL: armMotionWake left IRQ Status 2 latched` and exited 1.

The build was run from a normal repo path. Two earlier `pio run` attempts were
killed by memory pressure from unrelated concurrent builds on the same machine,
not by a compile error, and were rerun.

`m5stack-core-debug` was built on the rebased tree and is the firmware lane
quoted here; the other five environments and the two firmware compile databases
are CI's. The image is 1,735,392 bytes of the 1,740,800-byte OTA slot, so 5,408
bytes remain: the blocker that failed this lane before #524 is gone, and nothing
was trimmed to fit. Nothing here was measured on a StickS3.

## Deviations and corrections

- **The RTC call was already a no-op on the StickS3 in M5Unified 0.2.20.** The
  first read of the defect treated `M5.Rtc.disableIRQ()` as a live NACKing
  transaction. It is not: `PCF8563_Class::begin()` probes 0x51 and only keeps the
  instance when those writes are acknowledged, so on a board with no RTC the
  instance is gone and `disableIRQ()` returns before touching the bus. The
  NACKing probe in the boot window is M5Unified's own fallback probe inside
  `M5.begin()`, which furble does not control. The guard is still the right fix,
  because it removes a call whose guard tested the wrong fact and which becomes a
  real write the moment a future M5Unified stops gating on its probe, but the
  commit does not claim to remove a transaction that was already absent. A
  follow-up could set `cfg.internal_rtc = false` on boards without an RTC to skip
  M5Unified's probe entirely. It is not in this change: it moves `M5.begin()`'s
  behaviour for the Plus2 timed-wake contract and for `M5.Rtc.isEnabled()`
  consumers such as `canTimedWake()` and `TimeKeeper`, which is a separate change
  with its own gate.
- The volume cap and the settle window are compile-time limited to the M5PM1
  image, which is the StickS3. The first draft applied both to every board with
  audio; that would have delayed every Core2 and StickC beep by a second and
  capped their volume for a rail the notice does not cover. The policy functions
  themselves stay board-agnostic and are tested directly.
- The cap covers only what reaches the speaker amplifier. The first draft said
  the amplifier and an IR burst share 3V3_L3B_AU and cited the low-power guide's
  rail map for it. Nothing in the tree supports that. `plans/91-mic-trigger.md`
  reads the same guide as L3B carrying the ES8311 codec, the LCD backlight and
  the speaker, and `docs/power/board-power.md` puts the IR rail on the 5 V boost.
  The shared-rail clause is withdrawn (review round N2) and the unsupported
  citation is gone. The IR LED's own current is simply not part of this change.
- **The telemetry capability is asked for only when it can change the answer.**
  The first revision read `getBatteryCaps()` into a local and combined it with
  `FURBLE_HAS_VBUS_SENSE`. That macro is 1 on every board but the Waveshare, so
  the local was an unread store in the firmware build and CI's clang-tidy
  reported `clang-analyzer-deadcode.DeadStores`. Folding the read into the
  expression silenced it; the review round went further and asks for the charge
  capability only when VBUS is not readable, which keeps the warning away, drops
  a needless platform call and pairs with the fail-safe power-source rule.

## Unverified

- `AMP_SETTLE_MS = 1000` is the vendor's "about 1 s", not a measured settle. No
  tone was recorded on hardware to confirm the attack is restored.
- The 75 percent ceiling is the vendor's figure. The actual StickS3 brownout
  threshold at 255 on a depleted cell was not measured, and the notice's "75
  percent" is treated as 191 of 255.
- VBUS presence is read through `Platform::getBatteryStatus()`, which on the
  StickS3 is `M5.Power.getVBUSVoltage()` (the PMIC's VIN register). The charge
  flag is not an input, and a zero reading is retried once. A device whose VIN
  reads 0 while on USB would still cap the volume: that is the safe direction,
  and it is not observed on hardware. The PWR_SRC alternative was considered and
  rejected because its failed read reports `vin`, which is mains power.
- `motionWakeSample()` now performs one extra PMIC write per second while the
  PMIC path is armed. The added I2C cost is not measured against light-sleep
  residency.
- The shim models the aggregated line as asserted while either latch holds a
  bit. That is the manual's hold-until-cleared rule; the real chip's behaviour
  for a masked bit is not modelled and is not relied on.
