# 320 - Making better use of the ESP32 RTC domain

Status: research and ranked ideas, revisited. C1 and C4 were already
implemented on master when the ranked list was re-checked, and C2 landed in this
PR. C3, C5, C8, C9 and C10 remain open with the gates recorded below. Code facts
were checked against master `e66e8807` on 2026-11-13; the re-check was against
master `253d9cbe` on 2026-11-26, and the implementation state section records
what moved and why.

Tags on external claims: [vendor] Espressif, NXP or M5Stack documentation;
[measured: who] a third-party measurement; [inferred] my conclusion.

## Motivation

furble keeps wall time, timed wake and GPS aid in NVS and in the M5PM1, and
uses almost nothing of the SoC RTC domain. Some of that costs correct time
after a soft reset, and some of it costs NVS wear. This plan records what
the RTC domain can and cannot do on the supported boards, and ranks the
changes worth making.

## Current state

| Area | Current state | Evidence |
| :--- | :--- | :--- |
| RTC slow clock | Internal RC on every board (`CONFIG_RTC_CLK_SRC_INT_RC=y`), recalibrated on every automatic light sleep (`CONFIG_PM_LIGHTSLEEP_RTC_OSC_CAL_INTERVAL=1`). 8MD256, external crystal and external oscillator are unset. | all six release sdkconfigs |
| BLE sleep clock | ESP32: `CONFIG_BTDM_CTRL_LPCLK_SEL_MAIN_XTAL`. S3: `CONFIG_BT_CTRL_LPCLK_SEL_MAIN_XTAL`; StickS3 and headless also set `CONFIG_BT_CTRL_MAIN_XTAL_PU_DURING_LIGHT_SLEEP`, the Waveshare profile does not. | sdkconfigs; [00-hardware-experiments.md](00-hardware-experiments.md) experiment A |
| System time | `CONFIG_LIBC_TIME_SYSCALL_USE_RTC_HRT=y` (RTC timer plus high-resolution timer). | `sdkconfig.m5stick-s3:2379` |
| RTC memory | `src/FurbleCrashLog.cpp:31` and `TimeKeeper`'s retained clock record (`src/FurbleTimeKeeper.cpp:96`) use `RTC_NOINIT_ATTR`. No `RTC_DATA_ATTR`. On the S3, RTC fast RAM is heap (`CONFIG_ESP_SYSTEM_ALLOW_RTC_FAST_MEM_AS_HEAP=y`). | grep of `src/`, `include/`, `lib/` |
| ULP | `CONFIG_ULP_COPROC_ENABLED` unset. | `sdkconfig.m5stick-s3:2560` |
| ESP deep sleep | Never entered. Timed wake is a full power-off: M5PM1 timer plus shutdown on the StickS3, BM8563 countdown plus a hold-pin release on the StickC Plus2 and an AXP192 cut on the StickC Plus and the Tough. `Platform::canTimedWake()` is false on the Core, the Core2 and the Waveshare row. C4 delivered the BM8563 rows through plan 350. | `src/FurblePlatform.cpp`, `src/FurbleBoard.cpp` rows |
| Wall clock | `TimeKeeper::init()` keeps the running clock across a soft reset from an RTC-memory record, else reads the M5 RTC if present, else restores the NVS checkpoint with a one hour uncertainty penalty (`NVS_RESTORE_UNCERTAINTY_MS`), then sets the system time from it. A verified timed wake on the M5PM1 board now adopts a predicted clock (C2). `rtcBatteryBacked` is true for StickC, StickC Plus, Plus2, Core2 and Tough. | `src/FurbleTimeKeeper.cpp`, `src/FurbleBoard.cpp` rows |
| Intervalometer resume | NVS key `ivl_resume`. On the StickS3 the post-wake clock is NVS-sourced and therefore untrusted (`trustedWallClock()`), so the M5PM1 timer is the only authority. | `src/FurbleUIIntervalometer.cpp:25,193` |
| GPS aid | Last fix cached in NVS key `gps_fix`, write-throttled. | `src/FurbleGPS.cpp` `loadAidCache()`, `updateAidCache()` |
| M5PM1 | 32-bit second timer and a 32-byte RTC buffer. furble uses 4 bytes for the timed wake marker and, since C2, the 28 after it for the predicted wake clock. | `src/FurblePlatform.cpp` |

Pin facts that decide what the RTC domain can reach (from M5Unified
`M5Unified.cpp` and `Power_Class.cpp`):

- StickS3: buttons G11 and G12 and the PM1 IRQ G13 are RTC GPIOs (S3 RTC GPIO
  is 0 to 21). Internal I2C G48/G47, IR TX G46 and IR RX G42 are not. The
  battery is measured by the M5PM1, not the SoC ADC.
- StickC and StickC Plus: the BM8563 alarm and the MPU6886 INT share G35, an
  RTC GPIO. M5Unified's own `timerSleep()` uses `ext0` on G35 plus
  `esp_deep_sleep_start()` for these two boards. IR is G9 and internal I2C
  G21/G22, neither RTC capable.
- StickC Plus2: G35 is the power button wake pin, battery on ADC1 G38.
- Core2: M5Unified sets no RTC interrupt pin, only touch INT G39 as a wake
  pin. No known RTC alarm wake path.

## External facts

- The RTC timer keeps time across every reset except power-on; the
  high-resolution timer is better than 10 ppm but does not survive a reset
  or sleep; RC stability depends on temperature; 8MD256 is more stable for
  about 5 uA more deep sleep current; an external crystal costs about 1 uA
  more [vendor: S1].
- The internal 136 kHz RC cannot meet BLE's 500 ppm accuracy and does not
  support connections; the system falls back to the main XTAL when no 32 kHz
  crystal is found [vendor: S2].
- ext0 keeps RTC peripherals powered in deep sleep; ext1 works with them off
  but uses one level for all pins; BLE connections survive only modem sleep
  plus automatic light sleep, not explicit sleep [vendor: S3].
- The S3 ULP RISC-V has no hardware UART (bit-banged TX only), RTC I2C SDA
  only on GPIO1/3 and SCL only on GPIO0/2, 8 KiB of program memory, and a
  timer clocked from the RC slow clock [vendor: S4].
- Internal RC drift: about +23 s per hour on an ESP32 without a 32 kHz
  source, near zero once a DS3231 32 kHz output drove 32K_XP [measured:
  pantaluna, S5]; up to +-20 s per hour on an ESP32-C3 in deep sleep, worse
  after WiFi heats the die [measured: forum user, S6].
- PCF8563 / BM8563: alarm registers are minute, hour, day and weekday only
  (no seconds); no offset or aging trim register; CLKOUT can emit 32.768 kHz
  [vendor: S7].
- M5PM1: the timer counts seconds (0 to 214,748,364); "System power-off and
  re-power-on will clear the TIM_CFG register"; the 32-byte RTC buffer
  survives ESP32 power-off but not M5PM1 power-off. No timer clock source or
  accuracy is published [vendor: S8].

## Ideas, ranked

Ranking weighs user-visible benefit against effort and risk. HW means a
hardware check is needed before claiming the behavior.

### C1. Keep the wall clock across soft resets (all boards). Rank 1.

- Finding: with `RTC_HRT`, `gettimeofday()` already survives a panic, a
  watchdog, `esp_restart()` and deep sleep. `TimeKeeper::init()` ignores it.
  On boards without an RTC chip (StickS3, Core, Waveshare, headless) it
  restores the NVS checkpoint and sets the system time from it, overwriting a
  still-correct clock with a value that can be hours old and carries the one
  hour penalty.
- Change: keep a small `RTC_NOINIT_ATTR` record {magic, version, CRC, last
  accepted `TimeSample`, `gettimeofday()` at that moment} in
  `src/FurbleTimeKeeper.cpp`. In `init()`, if the reset reason is not
  power-on, brownout or unknown and the record checks out, accept the
  current system time with uncertainty = stored uncertainty plus elapsed
  times the growth bound, as a new `TimeSource` ranked below GPS and NTP and
  above NVS. Keep the RTC and NVS paths for power-on. Pure policy goes in
  `TimeKeeperPolicy` so host tests and the simulator cover it.
- Benefit: valid geotag time, camera time sync and GPS aiding time right
  after an OTA, panic, watchdog or reboot, on every board. Fewer NVS
  checkpoints.
- Effort S. Risk low if it fails closed on CRC, magic and reset reason. HW: a
  short StickS3 check that the record and the system time survive
  `esp_restart()`, a forced panic and the USB Serial/JTAG port-open reset
  (`ESP_RST_USB`; RTC retention through it is expected but unverified).

### C2. Restore the clock after a StickS3 timed wake. Rank 2.

- Finding: on a verified M5PM1 timed wake the off time is known (the
  programmed seconds), but the post-wake clock is NVS-sourced and
  `trustedWallClock()` rejects it, so the rest of the timelapse runs without
  trusted time.
- Change: before shutdown in `Platform::powerOffUntil()`, store the predicted
  epoch and uncertainty (in the NVS resume record or 8 more bytes of the
  M5PM1 RTC buffer). In `consumeTimedWake()`, feed `TimeKeeper` with epoch =
  saved plus programmed seconds plus measured boot latency, and uncertainty =
  saved plus seconds times a PM1 bound.
- Effort S to M. Risk low to medium: the PM1 timer accuracy is unpublished,
  so measure it (1 h and 8 h timers against GPS time) and start at a
  conservative 2 percent bound. HW: yes.

### C3. Measure the real clock error, then calibrate it. Rank 3.

- Finding: `UNCERTAINTY_GROWTH_PPM = 100` (`include/FurbleTimeKeeper.h:46`)
  assumes the timer error stays under 100 ppm. With tickless idle, most of a
  screen-off hour is light sleep timed by the RC slow clock
  (`docs/power/soc.md` SOC-06). Third-party deep sleep measurements show RC
  errors well above that [measured: S5, S6]. Nobody has measured furble's
  drift with heavy light sleep.
- Change: a hardware test that logs `TimeKeeper` predicted time against GPS
  RMC time for 4 h screen-off connected and 4 h screen-on. If drift exceeds
  100 ppm, raise the bound per board or change the RTC clock (C8). For
  BM8563 boards, learn a software trim from each GPS or NTP correction and
  store it in NVS, because the chip has no offset register [vendor: S7].
- Effort S for the measurement, M for the trim. Risk low. HW: multi-hour and
  multi-day.

### C4. StickC and StickC Plus timed wake through the BM8563 alarm. Rank 4.

- Finding: these boards lack AXP192 timed power-on, but M5Unified already
  uses the BM8563 alarm on G35 with `ext0` and ESP deep sleep. In deep sleep
  the RTC timer keeps time and `RTC_DATA_ATTR` keeps the resume state.
- Change: add them to `Platform::canTimedWake()` with a `powerOffUntil()`
  branch: disable the MPU6886 interrupt (it shares G35), program the alarm,
  sleep the panel, cut Grove 5 V and the backlight, `ext0` on G35 low, deep
  sleep. Use `esp_sleep_get_wakeup_cause()` instead of the PM1 marker. Reuse
  the plan 19 resume path.
- Effort M. Risk medium: G35 is shared and has no internal pull, AXP192 rails
  left on cost more than the ESP, and no StickC is on hand. HW: yes.

### C5. Scheduled start at a wall-clock time on the RTC alarm. Rank 5.

- Plus2: program the BM8563 calendar alarm (minute resolution) instead of the
  countdown. StickC and StickC Plus: through C4. StickS3: a PM1 duration from
  a trusted clock (needs C1 and C2). Core and Core2: no path; hide the
  feature, as plan 19 does. This is plan 90's mechanism.
- Effort M, mostly the time-entry widget. Risk medium (an alarm in the past,
  DST and time zone, cold-night drift). HW: yes. Depends on C1 to C3.

### C6. GPS restart hint in RTC memory. Rank 6.

- Change: mirror the latest fix into the C1 record on every fix. After a soft
  reset, `sendAidIni()` uses it with the C1 clock.
- Benefit: hot-start aiding after an OTA or panic without NVS wear. Helps
  only on soft resets and only while the receiver keeps its ephemeris; the
  Unit GPS v1.1 has no backup rail.
- Effort S. Risk low. HW: TTFF comparison after `esp_restart()`.

### C7. Crash-resume of an active intervalometer. Rank 7.

- Change: keep {camera index, shots done, target, next deadline, boot
  attempt count} in `RTC_NOINIT`. After a panic or watchdog reset during a
  run, resume once through the plan 19 path. Give up after two crash resets.
- Effort M. Risk medium (crash loops, resuming a run the user wanted
  stopped). HW: a forced panic during a run with FauxNY and the Fujifilm
  body.

### C8. 8MD256 RTC slow clock instead of the RC. Rank 8, only if C3 shows a problem.

- Better RTC timer stability for about 5 uA more in deep sleep [vendor: S1],
  invisible against the 3.3 mA BLE light sleep floor. No effect on BLE, which
  keeps the main XTAL. Must change in all six release sdkconfigs together.
  Effort S, risk low to medium. HW: current and drift re-measure.

### C9. Board selection rule: a 32.768 kHz source on 32K_XP. Rank 9, no firmware change.

- The only way to the 230 uA connected BLE light sleep floor is a 32 kHz
  crystal or an external 32 kHz oscillator on XTAL_32K_P [vendor: S2]. An RTC
  chip with CLKOUT routed to 32K_XP also qualifies
  (`CONFIG_RTC_CLK_SRC_EXT_OSC`), as the DS3231 project shows [measured: S5].
  Check every new board schematic for it. See plan 318.

### C10. ESP deep sleep instead of PM1 shutdown on the StickS3 for short gaps. Rank 10, measure first.

- Deep sleep keeps the RTC timer and RTC memory and allows `ext1` wake on
  G11/G12, but the M5PM1 keeps the 3.3 V rail and peripherals up. The vendor
  PM1 shutdown figure is 14.02 uA [vendor: S9]; ESP deep sleep is about 7 uA
  plus whatever the PM1, panel, IMU and codec draw (`docs/power/board-power.md`
  PM-02). Not a win without a measurement. Effort M, HW required.

## Rejected after checking the pins

- ULP button handling: `ext1` on G11/G12 (StickS3) or `ext0` on G37/G39
  already covers wake with no ULP program, and furble does not deep sleep.
- ULP battery sampling: the StickS3 battery is behind the M5PM1 on I2C
  G47/G48, StickC and Core2 behind the AXP192 on G21/G22. RTC I2C reaches
  only GPIO0 to 3 on the S3 [vendor: S4] and fixed RTC pins on the ESP32, so
  the ULP cannot read them. Only the Plus2 (ADC1 G38) could, and only in a
  deep sleep mode that does not exist.
- ULP GPS handling: the Grove GPS units expose no PPS, the ULP has no UART
  RX [vendor: S4], and a bit-banged 115200 baud RX is not realistic.
- ULP IR trigger during sleep: IR TX is G46 (S3) and G9 (StickC), not RTC
  GPIOs.
- RC slow clock for BLE sleep: forbidden for connections [vendor: S2].
- M5PM1 timer as a power-off elapsed counter: power-off and re-power-on clear
  `TIM_CFG`, and the timer clock is unpublished [vendor: S8]. Worth a
  one-hour bench experiment at most, not a design.
- Moving the intervalometer resume record from NVS to RTC memory: timed wake
  is a full power cut on the StickS3 and Plus2, so RTC memory does not
  survive it. NVS is correct there.

## Open bench items

- C1: RTC_NOINIT and system time retention across `esp_restart()`, a forced
  panic and the USB Serial/JTAG port-open reset on the StickS3.
- C2: M5PM1 timer accuracy over 1 h and 8 h against GPS time.
- C3: `TimeKeeper` drift over 4 h screen-off connected and 4 h screen-on,
  before and after SOC-06.
- C6: TTFF after `esp_restart()` with and without the RTC hint.
- C10: StickS3 current in ESP deep sleep versus PM1 shutdown.

## Sources (accessed 2026-11-13)

- S1 ESP-IDF System Time (ESP32-S3): https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/system_time.html
- S2 ESP-IDF BLE low power mode (ESP32-S3): https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/low-power-mode/low-power-mode-ble.html
- S3 ESP-IDF Sleep Modes (ESP32-S3): https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/sleep_modes.html
- S4 ESP-IDF ULP RISC-V (ESP32-S3): https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/ulp-risc-v.html
- S5 DS3231 32 kHz oscillator on 32K_XP: https://github.com/pantaluna/esp32_ds3231_32khz_oscillator_using_lib/blob/master/README.md
- S6 ESP32-C3 deep sleep drift: https://esp32.com/viewtopic.php?t=35490
- S7 NXP PCF8563 data sheet Rev. 11.1: https://www.nxp.com/docs/en/data-sheet/PCF8563.pdf
- S8 M5PM1 manual V1.9: https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1207/M5PM1_Datasheet_EN.pdf and https://docs.m5stack.com/en/arduino/m5sticks3/m5pm1
- S9 M5StickS3 product page: https://docs.m5stack.com/en/core/StickS3

## Implementation state

Re-checked against master `253d9cbe` on 2026-11-26. What the ranked list assumed
has moved:

- C1 is implemented. `TimeKeeperPolicy::restoreRetained()`, the
  `RTC_NOINIT_ATTR` record and the reset-reason gate landed with plan 139, and
  `tests/host/time_keeper_persistence_test.cpp` runs the whole soft-reset
  matrix: every reset reason, a valid and a lost record, and no chip, an
  unbacked chip or a backed one, plus the fast-kept-clock lockout regression.
  The plan's "now" row for `knownBatteryBackedRtc()` is also stale: the profile
  column is `rtcBatteryBacked` and five rows set it.
- C4 is delivered for the StickC Plus and the Tough through plan 350, as the
  follow-up section below records. The plain StickC row keeps
  `TimedWake::NONE`.
- C2 is implemented in this PR. See the follow-up section.
- C3, C5 and C10 need hardware, and their entries already say so: C3 needs the
  four hour drift measurement before any trim, C5 depends on C3 and needs a
  time-entry UI plus DST and past-alarm semantics, and C10 needs a PM1-shutdown
  current comparison.
- C6 is withdrawn. Its RTC mirror is redundant because every reset the RTC
  memory survives, NVS survives too, including an OTA and a panic, so the
  mirrored fix would never be read where the stored one is not. Its wear claim
  does not survive arithmetic either: the 56-byte `gps_fix` record is rewritten
  every ten minutes, which is 144 writes a day, about three and a half pages of
  a 24 KB NVS partition, and roughly 700 partition cycles over ten years against
  a 100,000-cycle endurance. Trading a decade of cold-start aiding accuracy for
  that is not worth a user-visible change, so the write interval stays.
- C7 is not started: it needs a resume origin that bypasses the timed-wake gate,
  a two-attempt crash-loop bound, and a retained record written at every shot
  boundary, which is a UI state-machine change rather than an RTC-domain one.
- C8 waits for C3, and C9 is a board-selection rule with no firmware change.

## Follow-up: StickC Plus timed wake landed as C4's Part

State: implemented on `fix/stickc-family` as part of [350](implemented/350-stickc-family-fixes.md).
The StickC Plus row is `TimedWake::RTC_ALARM`, so it reuses the Plus2 path rather
than the deep-sleep design C4 sketches: `setAlarmIRQ()`, the countdown register
readback, the early-wake rejection, `consumeTimedWake()`'s `getIRQstatus()` check
and the intervalometer resume window are unchanged, and the sleep is a full
AXP192 `powerOff()` because the board has no hold pin. The wake path is the
BM8563 INT coupled into the AXP192 power key through R401 and C401, which is
vendor-documented and not scoped here, so C4's hardware gate (G35 shared with
the MPU6886 and the AXP192 IRQ, panel state, resume) still applies. C5's
calendar alarm, C3's trim measurement, C6 and C7 remain open.

## Follow-up: C2, the timed-wake clock, landed

State: implemented in this PR. The StickS3 is the only `TimedWake::PMIC_TIMER`
row, so this is its board path; the BM8563 rows already carry their clock in the
chip and are unchanged.

What the plan sketched matches what landed, with two deviations:

- Storage is the M5PM1 RTC buffer, not NVS. `Platform::powerOffUntil()` writes
  the 28-byte record at offset 4 of the 32-byte buffer, after the four byte wake
  marker, and a `static_assert` fails the build if the record grows past it. NVS
  would have cost one flash write per timelapse cycle, which is the wear the
  plan's C6 wants to remove, and the plan already names the buffer as the
  alternative. `consumeTimedWake()` reads the record, clears the whole buffer in
  one write so no later boot can adopt it, and adopts it only when the marker
  verifies.
- The record is the existing 28-byte `TimeSample` wire record with a new
  `TimeSource::TIMED_WAKE` value, so there is no second codec and no record
  version bump. The source appends at 6 and takes its authority from
  `sourcePriority()`, which maps it to 2: above a stored checkpoint and below a
  calendar chip. `TimeKeeperPolicy::valid()` and `decode()` accept it, and a
  record written by an older firmware still decodes to the same values.

**Accuracy claim.** A clock adopted after a verified timed wake is `predicted
epoch at the wake instant + the boot time measured since the rail came back`,
with uncertainty `uncertainty at shutdown + 2 percent of the armed interval +
2 s`. The 2 percent is an estimate, not a measurement: the M5PM1 timer counts
whole seconds and neither its clock source nor its accuracy is published (S8), so
the base term covers the one-second timer resolution and the interval between the
rail restoring and the first read. For a one hour interval that is about 74 s, for
a 30 s interval about 3 s. The bench item C2 in the plan above (PM1 accuracy over
1 h and 8 h against GPS time) would replace the 2 percent with a measured figure;
until then the claim is the estimate above, and `time status` and the simulator's
`time.uncertainty_ms` report it, so the operator sees the window rather than a
bare timestamp.

**What the clock is worth, and where the error comes from.** The formula is
recursive: the next prediction starts from the sample this one adopted, so a
timelapse adds `2 s + 2 percent` of its interval to both the epoch error and the
reported window on every sleep. A single resumed cycle is inside a few seconds;
a long run accumulates. Two consequences, both worth stating because a reader
will otherwise assume the clock is corrected:

- The intervalometer's own timing does not drift with it. `startResume()`
  compares the deadline saved before the shutdown with the clock adopted after
  it, and both are values from the same prediction chain, so the accumulated
  error cancels in the subtraction. What the shot timing loses is the clock's
  error at the moment of saving, and only if another source re-anchors the clock
  between the boot and `startResume()`.
- An absolute value does carry it: a GPX point written before a GPS fix, a
  camera time sync, and the `time status` epoch all report the predicted clock
  within its stated window. A GPS, NTP or companion sample outranks the
  prediction and replaces it, and `trustedWallClock()` has no uncertainty
  threshold of its own, so the window is reported rather than enforced. That is
  the shape of the claim: the clock is bounded and the bound is visible, not
  guaranteed exact.

**How a stale value is rejected.** Five gates:

1. The record does not decode: magic, version, length, the reserved bytes and
   the CRC all have to hold, so a truncated, erased or partially written buffer
   writes nothing.
2. The record's source is not `TIMED_WAKE`. A record left by another path, or a
   buffer that was never written, is refused rather than reinterpreted.
3. The board did not confirm a timed wake. Only `consumeTimedWake()`'s own
   marker check calls the adoption, and a manual power-on or a brownout clears
   the buffer without adopting it, so an interval that never elapsed cannot be
   added to the clock.
4. The stored checkpoint reaches past the prediction. A checkpoint that is newer
   than the shutdown instant the record describes cannot be older than it, so the
   record is stale or from another device and is refused.
5. The arithmetic leaves the accepted range: an interval of zero or beyond a day
   writes no record at all, and an adopted value outside the epoch window or over
   the uncertainty ceiling is refused.

Gates 1, 2, 4 and 5 are host-tested directly, in `time_keeper_policy_test` and
`time_keeper_persistence_test`. Gate 3 is host-tested too, in
`boundary-platform-timed-wake`, which compiles the production
`src/FurblePlatform.cpp` against the M5PM1 double and drives the real
`powerOffUntil()` with the real `consumeTimedWake()` across a simulated power
cycle: the shutdown's marker and record are in the double's RTC buffer, the boot
either adopts the record or clears it, and dropping the marker check from the
production comparison fails the target. The gate needs the part, not a board.

What that target cannot show is what only the device can: that a real M5PM1 keeps
its 32-byte RTC buffer across the shutdown it performs (a battery pull loses it,
which is the fail-closed direction), that a real timer wake sets WAKE_SRC bit 0,
that the buffer survives the rail cut with no partial write, and how accurate the
timer is, which is the C2 bench run. The double models the part's registers and
the buffer's lifetime under a process that outlives the simulated power-off, so a
device confirmation is still owed for those four.

Two earlier versions of this paragraph were wrong in opposite directions: one said
all five gates were host-tested, and the next said gate 3 was not host-testable at
all. The first was false. The second was too pessimistic, and the method it
doubted is the one now used: the production Platform is reachable with a double at
the M5PM1 and nothing else.

The buffer is cleared on every boot, not only on a confirmed timed wake, so the
record cannot outlive the boot that should have consumed it. Three paths hold that
rule, and `boundary-platform-timed-wake` asserts each: `consumeTimedWake()` clears
the buffer even when the record read fails, so an uninterpretable record cannot
survive to be adopted against a later marker; `powerOffUntil()` clears the record
region when it cannot build a new prediction; and it clears the region again when
the record write itself fails, so a marker is never paired with the interval of an
earlier shutdown. The third is best effort by construction: a bus that refuses the
record write usually refuses the clear too, and a marker with no record is then the
fail-closed outcome, because the boot refuses it.

A refused or missing record keeps the stored checkpoint clock with its one hour
penalty, which is the behaviour every board had before this change, so the clock
never becomes silently worse.

Coverage: `time_keeper_policy_test` covers the bound arithmetic, the failure cases
above and the record round trip; `time_keeper_persistence_test` runs a GPS-seeded
run, a simulated power cut, the adoption, the `timed_wake` source surviving a
further soft reset, and the confirmation that adoption costs no NVS write;
`intervalometer-deep-sleep-clock-start.txt` with `-clock-resume.txt` cover the
two-leg power cycle: the resumed leg reports `time.source timed_wake` with a
three second uncertainty and runs its saved deadline instead of counting from
boot; `boundary-platform-timed-wake` drives the production
`Platform::consumeTimedWake()` through gate 3 and the three buffer paths above,
and each assertion was proved red first by reverting the line it guards (the
marker comparison, the clear on a failed read, the clear with no prediction, and
the clear on a failed record write). The stale-record gates are host-tested
rather than scenario-tested, because a scenario clock strong enough to compare a
fixture deadline against outranks the prediction and would not exercise it. The
hardware gate stays open: C1's bench item (RTC retention through `esp_restart()`,
a panic and a USB port-open reset) and C2's PM1 accuracy run both need the StickS3
on the bench, and so do the four device confirmations named above.
