# Hardware experiments

Two hardware questions block design decisions in later PRs. Both are answered with
throwaway debug builds on the attached M5StickS3. No code from these experiments is
merged. The results are recorded here and cited by PR07 and PR15.

Both experiments need the standard build environment variables, because
`platformio.ini:3-4` reads `${sysenv.FURBLE_VERSION}` and `${sysenv.FURBLE_TEST}`:

```
export FURBLE_VERSION=exp
export FURBLE_TEST=0
```

## Experiment A: does the M5StickS3 have an external 32.768 kHz crystal?

### Question

The BLE controller can take its low power clock from the main XTAL or from an
external 32.768 kHz crystal. The 32 kHz path allows the main XTAL to be powered
down in light sleep, which is the difference between the ~3.3 mA floor and the
~230 uA stretch target in PR07. The M5StickS3 product page does not list a
32.768 kHz crystal. The SoC supports one (`sdkconfig.m5stick-s3:316`,
`CONFIG_SOC_CLK_XTAL32K_SUPPORTED=y`), but SoC support is not board support.

### Current state

`sdkconfig.m5stick-s3` contains no `CONFIG_BT_CTRL_LPCLK_*` entry, and
`sdkconfig.m5stick-s3:856` has `# CONFIG_BT_CTRL_MODEM_SLEEP is not set`. So the
controller currently runs with defaults and no modem sleep.

### Procedure

1. Branch from master. Do not commit.
2. Edit `sdkconfig.m5stick-s3`: add `CONFIG_BT_CTRL_LPCLK_SEL_EXT_32K_XTAL=y` and
   remove or override the conflicting default LPCLK selection line if the build
   regenerates one. Also set `CONFIG_LOG_DEFAULT_LEVEL_DEBUG=y` in place of
   `CONFIG_LOG_DEFAULT_LEVEL_INFO=y` (`sdkconfig.m5stick-s3:1756`) so controller
   init logs are visible.
3. `pio run -e m5stick-s3 -t upload`
4. `pio device monitor -e m5stick-s3` and capture the boot log from reset through
   the first camera connect.
5. Grep the log for the controller low power clock line printed during BLE init.
6. Connect to the Fujifilm camera, leave it connected for 5 minutes, confirm the
   link is stable and the shutter still fires.

### Expected outcomes

ESP-IDF does not fail BLE init when the crystal is absent. It falls back to the
main XTAL and continues. This is documented in the BLE low power guide: "Even if
32 kHz is selected in menuconfig, the system will fall back to the main XTAL if
the external crystal is not detected during Bluetooth LE initialization." So the
observable is the init log line, not a boot failure.

- Crystal present: the init log reports the 32 kHz external crystal as the low
  power clock, and the link stays up. Wake latency from light sleep is longer
  with the 32 kHz clock, so shutter latency must be re-checked in PR07.
- Crystal absent: the init log reports the main XTAL despite the Kconfig
  selection. Record the exact log line as the evidence.

A secondary confirmation is a drain run. If the crystal is real, PR07's stretch
mode later shows a measurable difference in idle drain. That measurement belongs
to PR07, not here.

### How this feeds later work

Gates the stretch 230 uA mode of PR07 only. If the crystal is absent, PR07 keeps
`CONFIG_BT_CTRL_LPCLK_SEL_MAIN_XTAL` plus
`CONFIG_BT_CTRL_MAIN_XTAL_PU_DURING_LIGHT_SLEEP`, and the ~3.3 mA connected idle
floor is final for the S3. PR07 ships either way.

## Experiment B: GPS Unit v1.1 backup rail and $PCAS12 support

### Question

Two independent facts decide PR15's default power policy.

1. Does the unit keep ephemeris across a 5 V rail cut, that is, is there a
   backup supply on V_BCKP? If yes, rail cycling is cheap because re-fix is
   fast. If no, rail cycling costs a cold start every wake.
2. Does the receiver accept `$PCAS12` standby? The CASIC protocol defines
   `$PCAS12,stdbysec` for timed standby, but the command is not supported on
   every module in the family. The M5Stack product page for Unit GPS v1.1 does
   not document any standby command or backup supply.

### Current state

furble drives the unit's 5 V rail directly. `src/FurbleGPS.cpp:118` calls
`M5.Power.setExtOutput(true, m5::ext_PA)` on enable and
`src/FurbleGPS.cpp:128` calls `setExtOutput(false, m5::ext_PA)` on disable.
There is no UART transmit path at all. `uart_set_pin` is called at
`src/FurbleGPS.cpp:45` with the TX pin wired, but nothing in the tree calls
`uart_write_bytes`. Experiment B needs a temporary TX path.

### Procedure, part 1: backup rail

1. Enable GPS in Settings, wait for a fix. Confirm the fix on the GPS Data page
   (`src/FurbleUI.cpp:1548-1603`) and note the satellite count.
2. Add a temporary debug button, or reuse the GPS enable switch at
   `src/FurbleUI.cpp:1517`, to toggle the rail. Toggling the GPS setting off and
   on already calls `disable()` then `enable()`, which cuts and restores the
   rail.
3. Turn GPS off. Wait 60 s with a stopwatch. Turn GPS on.
4. Log the wall time from rail restore to the first valid fix. Take the time from
   the console, not the screen, by adding a one line `ESP_LOGI` in `GPS::update`
   at `src/FurbleGPS.cpp:174` when `m_HasFix` first becomes true.
5. Repeat three times. Repeat once with a 10 minute rail cut.

Expected outcomes:

- Re-fix in 5 s or less after the 60 s cut: a backup supply is holding time and
  ephemeris. Hot start works.
- Re-fix in 30 s or more, with satellite count climbing from zero: no backup
  supply. Every rail cycle is a cold start.
- The 10 minute cut distinguishes a small capacitor from a real backup cell. A
  capacitor holds for tens of seconds only.

### Procedure, part 2: $PCAS12

1. Add a temporary `uart_write_bytes` call to `Furble::GPS`, triggered from the
   GPS Data page. The NMEA checksum is the XOR of all characters between `$` and
   `*`. Send exactly `$PCAS12,10*<CS>\r\n`.
2. Log every received NMEA line for 60 s around the send. `GPS::serviceSerial` at
   `src/FurbleGPS.cpp:195-206` is the hook. Add an `ESP_LOGI` of the raw buffer.
3. Observe the receive stream.

Expected outcomes:

- NMEA output stops for about 10 s and then resumes on its own: `$PCAS12` is
  supported. Standby is available without cutting the rail.
- NMEA output never stops: the command is rejected or unimplemented. Send
  `$PCAS02,1000` as a control to prove the TX path works at all. If the fix rate
  visibly changes, TX is fine and only `$PCAS12` is unsupported.
- Garbage or a checksum complaint in the stream: fix the checksum and retry
  before concluding anything.

### How this feeds later work

Chooses PR15's default receiver policy.

- Backup rail present and `$PCAS12` supported: default to `$PCAS12` standby.
  Cheapest, no cold starts.
- Backup rail present, `$PCAS12` unsupported: default to 5 V rail cycling, since
  re-fix is fast.
- No backup rail: default to always-on. Duty cycling a cold start receiver costs
  more energy than it saves, and it breaks the 30 s `MAX_AGE_MS` freshness budget
  at `include/FurbleGPS.h:38`.

PR14 also depends on the TX path proven here. The throwaway `uart_write_bytes`
call becomes the checksum builder and send path in PR14.

## Recording results

Append results to this file as a short table: date, firmware commit, board serial,
experiment, observed log lines, conclusion. PR07 and PR15 cite this file.

## References

- ESP-IDF, Low Power Mode in Bluetooth Low Energy Scenarios, ESP32-S3:
  https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/low-power-mode/low-power-mode-ble.html
- ESP-IDF, Power Management, ESP32-S3:
  https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/system/power_management.html
- ESP-IDF, Sleep Modes, ESP32-S3:
  https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/system/sleep_modes.html
- ESP-IDF, Logging library:
  https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/system/log.html
- Espressif NimBLE power_save example:
  https://github.com/espressif/esp-idf/tree/master/examples/bluetooth/nimble/power_save
- M5Stack StickS3 product page, 250 mAh battery, M5PM1, BMI270:
  https://docs.m5stack.com/en/core/StickS3
- M5Stack Unit GPS v1.1, ATGM336H with AT6668, 5 V, default 115200 8N1:
  https://docs.m5stack.com/en/unit/Unit-GPS%20v1.1
- CASIC Multimode Satellite Navigation Receiver Protocol specification, English
  translation, PDF. Defines the `$PCAS` command family including standby:
  https://www.espruino.com/files/CASIC_en.pdf
- Espruino Bangle.js 2 technical notes, worked `$PCAS02`, `$PCAS03`, `$PCAS04`
  examples on an AT6558. Note this page does not cover `$PCAS12`:
  https://www.espruino.com/Bangle.js2+Technical
- PlatformIO device monitor:
  https://docs.platformio.org/en/latest/core/userguide/device/cmd_monitor.html
