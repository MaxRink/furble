# M5Stack M5PM1

**Part and package.** M5Stack M5PM1, a power management chip with a hardware
watchdog, power button gestures, GPIO wake, a status LED, a 5 V boost and a
3.3 V rail. Package and pinout were not read for this page and are UNVERIFIED.

**Role in furble.** The power-management chip on the StickS3. furble drives it
through the pinned M5PM1 library, not through `lib/hal`, so the facts below are
about the library calls and the register semantics the firmware relies on.

**Status.** shipped. `m5stick-s3` is the only board that carries the part, and
`src/FurblePlatform.cpp` drives it at every boot and every power transition.

**Documents.** [M5PM1-UM]

## Documents used

| Key | Title | Revision or date | URL | Retrieved | Opened in this change |
| :--- | :--- | :--- | :--- | :--- | :--- |
| M5PM1-UM | M5PM1 Chip User Manual, Shenzhen Mingzhan Information Technology Co., Ltd. | V 1.9, firmware HW:5 / SW:S dated 2026-01-06 | <https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1207/M5PM1_Datasheet_EN.pdf> | 2026-11-25, reopened 2026-11-26 | The register list on pages 6 to 11 of 33, and the numbered register sections on pages 13, 15, 17, 19 to 21 and 25 of 33 |

The manual carries both a register list, with the address in an adjacent column,
and numbered sections per register. This page cites the section number and the
printed page, so a reader can find the register in either form.

## Facts furble depends on

| # | Fact | Document and section | Code | State |
| :--- | :--- | :--- | :--- | :--- |
| 1 | `PWR_CFG` is register 0x06 and its default 0x17 means LED enable, 5 V boost off, 3.3 V LDO on, 3.3 V DCDC on and charge on | M5PM1-UM section 2 register (7), `PWR_CFG` 0x06, page 13 of 33 | No direct write; `Platform::updatePowerConfig()` at `src/FurblePlatform.cpp:1320` owns every `PWR_CFG` write, because the LED and the EXT_5V boost share the byte | VERIFIED |
| 2 | `I2C_CFG` is register 0x09, its `SPD` field is bit 4 and its `SLP_TO` field is bits 3:0, where 0 disables idle sleep | M5PM1-UM section 2 register (10), `I2C_CFG` 0x09, page 15 of 33 | `include/FurblePlatform.h:523` and the `m5pm1Access` retry path | VERIFIED |
| 3 | The first I2C transaction after idle sleep fails and only wakes the chip, and an incomplete address within 300 ms of that first transaction puts it back to sleep, so every access is retried once | M5PM1-UM section 2 register (10) note 2, page 15 of 33 | `src/FurblePlatform.cpp:254`, `:260`, `:296`, `:316`, `:336` | VERIFIED |
| 4 | `WDT_CNT` is 0x0A in seconds with 0 disabling the watchdog, and `WDT_KEY` at 0x0B is fed 0xA5 to clear and reload | M5PM1-UM section 2 registers (11) and (12), page 15 of 33 | `src/FurblePlatform.cpp:336`, `:368` | VERIFIED |
| 5 | `SYS_CMD` is 0x0C with the key 0xA in bits 7:4, and 0xA1 in the register means power off | M5PM1-UM section 2 register (13), `SYS_CMD` 0x0C, page 15 of 33 | `src/FurblePlatform.cpp` power-off path | VERIFIED |
| 6 | `HOLD_CFG` at 0x07, default 0x00, holds the power and GPIO state | M5PM1-UM section 2 register (8), `HOLD_CFG` 0x07, page 13 of 33 | Library call, no direct use in furble | VERIFIED |
| 7 | `WAKE_SRC` at 0x05 is read-clear only, so writing it cannot select a source | M5PM1-UM section 2 register (6), `WAKE_SRC` 0x05, page 13 of 33 | `src/FurblePlatform.cpp:833` | VERIFIED |
| 8 | The RTC buffer is 32 bytes at 0xA0 to 0xBF and survives an ESP32 power-off | M5PM1-UM section 11 RTC Buffer, page 25 of 33 | `src/FurblePlatform.cpp:932` (marker, 4 bytes at offset 0), `:963` (predicted wake clock, 28 bytes at offset 4), `:1116` (the whole buffer cleared on consume, on every boot) | VERIFIED |
| 9 | The system timer counts in seconds in `TIM_CNT` 0x38 to 0x3B and arms through `TIM_CFG` 0x3C, and the 32-bit limit is 214,748,364 s | M5PM1-UM section 5 System Timer, Table 4, page 20 of 33 | `src/FurblePlatform.cpp:749-776` | VERIFIED |
| 10 | The status LED stays lit at display off unless it is cleared, which saves `PWR_CFG` bit 4 | M5PM1-UM section 2 register (7) bit 4, page 13 of 33, and `CLAUDE.md` hardware traps | `src/FurbleFeedback.cpp:306` to `:312` | VERIFIED |
| 11 | The BMI270 interrupt reaches the chip on GPIO4 on the StickS3, and a GPIO interrupt has a polarity bit where 1 is active low | M5Stack StickS3 low-power page, and M5PM1-UM GPIO interrupt registers, pages 19 and 20 of 33 | `src/FurbleBoard.cpp:17`, `src/FurblePlatform.cpp:537-548` | CITED |
| 12 | `SYS_CMD` command 11 in bits 1:0 is Download mode, and I2C idle sleep is invalid while the part is in it | M5PM1-UM section 2 register (13) page 15 of 33, and section 6 note page 17 of 33 | `src/FurblePlatform.cpp:182-198` | VERIFIED |

## Operations the firmware uses

| Operation | Purpose | Citation |
| :--- | :--- | :--- |
| `begin` | bind to the internal bus | `src/FurblePlatform.cpp:136` |
| `setSingleResetDisable(true)` | disable the BtnPWR single-click reset | `src/FurblePlatform.cpp:137` |
| `setDoubleOffDisable(true)` | disable the BtnPWR double-click power off | `src/FurblePlatform.cpp:138` |
| `setDownloadLock` and `getDownloadLock` | long-press download recovery, set and verified | `src/FurblePlatform.cpp:182-198` |
| `wdtSet`, `wdtGetCount`, `wdtFeed` | watchdog arm, verify and feed | `src/FurblePlatform.cpp:252-284` |
| `readVbat` | battery voltage | `src/FurblePlatform.cpp:377,1426` |
| `updatePowerConfig` | keep the status LED dark at display off and restore it, without losing the EXT_5V boost bit in the same byte | `src/FurblePlatform.cpp:387-410`, `:1320` |
| `shutdown` | power off | `src/FurblePlatform.cpp:590,980` |
| `gpioSet`, `gpioSetFunc`, `gpioSetWakeEnable`, `gpioSetWakeEdge`, `irqSetGpioMask`, `irqClearGpioAll` | arm GPIO 4 as the IMU wake source and unmask its IRQ | `src/FurblePlatform.cpp:537-548` |
| `irqGetGpioStatus` | read the latched IRQ state | `src/FurblePlatform.cpp:664` |
| `timerSet(seconds, M5PM1_TIM_ACTION_POWERON)`, `timerClear` | timed wake for a scheduled power off | `src/FurblePlatform.cpp:749-776` |
| `writeRtcRAM` and `readRtcRAM` | a 4-byte marker across a timed power off, `TIMED_WAKE_MARKER` 0x49564c31, followed by the 28-byte `TimeSample` record of the predicted wake clock | `src/FurblePlatform.cpp:38`, `:932`, `:963` |
| `getWakeSource` | why the device woke | `src/FurblePlatform.cpp:833` |
| `btnGetState` | power key sampling | `src/FurblePlatform.cpp:1038` |

## Timing constants

| Constant | Value | Meaning | Citation |
| :--- | ---: | :--- | :--- |
| `PM1_TIMEOUT_MS` | 45000 | watchdog window | `include/FurbleWatchdog.h:12` |
| `PM1_TIMEOUT_S` | 45 | the same value in the register's unit | `include/FurbleWatchdog.h:13` |
| `PM1_FEED_PERIOD_MS` | 1000 | feed cadence | `include/FurbleWatchdog.h:14` |
| `TIMED_WAKE_MARKER` | 0x49564c31 | RTC RAM marker that proves a timed wake was programmed | `src/FurblePlatform.cpp:34` |
| `TIMED_WAKE_RECORD_OFFSET` | 4 | where the predicted wake clock starts, after the marker | `src/FurblePlatform.cpp:73` |
| `TIMED_WAKE_PPM` | 20000 | estimated M5PM1 timer error used to bound an adopted clock, 2 percent | `include/FurbleTimeKeeper.h:126` |
| `TIMED_WAKE_BASE_MS` | 2000 | fixed part of that bound: one-second timer resolution plus the boot-to-read gap | `include/FurbleTimeKeeper.h:127` |
| `TIMED_WAKE_MAX_EARLY_S` | 60 | how early a timed wake may fire and still resume | `include/FurblePlatform.h:207` |

The 45 s window is well inside the register's range, which is one second per
count up to the 32-bit timer limit. The feed period of one second leaves 44
missed feeds before a reset.

The timer's clock source and accuracy are not published, so `TIMED_WAKE_PPM` is
an estimate rather than a measurement. It bounds the wall clock a verified timed
wake adopts, together with the fixed base term, and plan 320 records the bench
run that would replace it. Until then a clock recovered after a timed power-off
is stated as `2 percent of the armed interval plus 2 s` on top of the uncertainty
the clock carried at shutdown.

## The retry-once rule

The manual states the rule the firmware was written around: after idle sleep the
first I2C transaction only wakes the chip and fails, and a second incomplete
address inside 300 ms puts it back to sleep (`M5PM1-UM` page 15 of 33, note 2).
`m5pm1Access()` runs the access, retries it once on failure, and counts both
outcomes for the console (`include/FurblePlatform.h:370-386`). Every PMIC call in
`src/FurblePlatform.cpp` goes through it, which is why a wake from light sleep
does not lose the access that follows it. Do not call a raw `M5PM1` method.

## The LED rule

The status LED stays lit at display off unless it is cleared. `PWR_CFG` bit 4 is
the LED enable, so the firmware clears it through `updatePowerConfig()` and
restores the previous level afterwards (`src/FurblePlatform.cpp:387-410`).

## Flash and download recovery

- `prepareFlash()` disables the watchdog, verifies the count is zero, unlocks
  long-press download recovery and refuses to proceed when the PMIC rejects
  either step (`src/FurblePlatform.cpp:202-226`).
- `cancelFlashPreparation()` restores the watchdog and reports failure when the
  restore cannot be confirmed (`src/FurblePlatform.cpp:232-243`).
- The recovery path stays available after a cancel on purpose. Manual rescue of a
  wedged device depends on it.
- Download mode is `SYS_CMD` command 11, and the manual states that I2C idle
  sleep is invalid while the part is in it (`M5PM1-UM` pages 15 and 17 of 33),
  which is why the helper does not rely on idle sleep after the unlock.

## Where the code implements it

- `src/FurblePlatform.cpp:208` to `:210`: `begin()`, then single-click reset and
  double-click power off are disabled so a button gesture cannot reset the
  board under the UI.
- `src/FurblePlatform.cpp:253` to `:370`: download recovery, watchdog readback
  and the watchdog set path, all through the retrying access helper.
- `src/FurbleFeedback.cpp:306` to `:312`: wakes the chip before the amplifier
  access that follows an idle sleep.
- `include/FurblePlatform.h:104` to `:115`: the watchdog feed, the wake helper
  and the retry contract.
- `src/FurbleConsole.cpp:2486`: reports the retry count so a bench session can
  see the idle-sleep behaviour.

## Error handling

Every PMIC failure logs at WARN or ERROR with the operation name and returns
false. `watchdogEnable()` re-arms on a verify failure so a half-configured
watchdog cannot leave the device unwatched (`src/FurblePlatform.cpp:252-268`).

## Citation gaps

| Constant or fact | Driver location | Why no section | Page row |
| :--- | :--- | :--- | :--- |
| none | - | The M5PM1 is not a `lib/hal` driver, so the gate does not scan it. Its gaps are the rows below. | - |

## Facts still unverified

- Package and pinout.
- The `SLP_TO` timeout in seconds for each field value. The manual gives the
  field position and the unit, not a table of the sixteen values.
- The GPIO wake polarity contract against the BMI270 INT1 active-low setting.
  Both ends are configured, and neither document states the pairing.
- The manual's revision history dates the software at 2026-01-06, and the pinned
  M5PM1 library was not compared against that build.

## Open rows

| ID | Missing | Document and section needed | State |
| :--- | :--- | :--- | :--- |
| C-17a | The `SLP_TO` value that the pinned library writes, and whether it matches the 300 ms note | M5PM1-UM section 2 register (10), page 15 of 33, against M5Unified's `M5PM1.h` | open, needs the library source rather than a document |
| C-17b | The GPIO wake edge and the BMI270 INT1 polarity as a documented pair | BMI270 datasheet section 5.2.36 plus M5PM1-UM GPIO registers, pages 17 to 19 of 33 | open, no document states the pairing |

## Redistribution

The manual carries no licence statement in its front matter. A silent licence is
a refusal under the rule in [README.md](README.md#committed-documents), so the
PDF is cited and not stored.

[M5PM1-UM]: https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1207/M5PM1_Datasheet_EN.pdf
