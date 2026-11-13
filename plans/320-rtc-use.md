# 320 - Making better use of the ESP32 RTC domain

Status: research and ranked ideas. Nothing here is implemented. Code facts
were checked against master `e66e8807` on 2026-11-13. External sources were
read the same day.

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
| BLE sleep clock | ESP32: `CONFIG_BTDM_CTRL_LPCLK_SEL_MAIN_XTAL`. S3: `CONFIG_BT_CTRL_LPCLK_SEL_MAIN_XTAL` plus `MAIN_XTAL_PU_DURING_LIGHT_SLEEP`. | sdkconfigs; [00-hardware-experiments.md](00-hardware-experiments.md) experiment A |
| System time | `CONFIG_LIBC_TIME_SYSCALL_USE_RTC_HRT=y` (RTC timer plus high-resolution timer). | `sdkconfig.m5stick-s3:2379` |
| RTC memory | Only `src/FurbleCrashLog.cpp:31` uses `RTC_NOINIT_ATTR`. No `RTC_DATA_ATTR`. On the S3, RTC fast RAM is heap (`CONFIG_ESP_SYSTEM_ALLOW_RTC_FAST_MEM_AS_HEAP=y`). | grep of `src/`, `include/`, `lib/` |
| ULP | `CONFIG_ULP_COPROC_ENABLED` unset. | `sdkconfig.m5stick-s3:2560` |
| ESP deep sleep | Never entered. Timed wake is a full power-off: M5PM1 timer plus shutdown on the StickS3, BM8563 countdown plus GPIO4 hold release on the StickC Plus2. `Platform::canTimedWake()` is false on every other board. | `src/FurblePlatform.cpp:689-830` |
| Wall clock | `TimeKeeper::init()` reads the M5 RTC if present, else restores the NVS checkpoint with a one hour uncertainty penalty (`NVS_RESTORE_UNCERTAINTY_MS`), then sets the system time from it. `knownBatteryBackedRtc()` is true for StickC, StickC Plus and Core2. | `src/FurbleTimeKeeper.cpp:43-52,130-145,198-235` |
| Intervalometer resume | NVS key `ivl_resume`. On the StickS3 the post-wake clock is NVS-sourced and therefore untrusted (`trustedWallClock()`), so the M5PM1 timer is the only authority. | `src/FurbleUIIntervalometer.cpp:25,193` |
| GPS aid | Last fix cached in NVS key `gps_fix`, write-throttled. | `src/FurbleGPS.cpp` `loadAidCache()`, `updateAidCache()` |
| M5PM1 | 32-bit second timer and a 32-byte RTC buffer. furble uses 4 bytes of it for the timed wake marker. | `src/FurblePlatform.cpp` |

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

- Finding: `UNCERTAINTY_GROWTH_PPM = 100` (`include/FurbleTimeKeeper.h:36`)
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

Research only. No idea here is implemented. C1 is the recommended first
change.
