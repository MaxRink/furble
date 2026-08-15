# PR02: battery percent and battery info page

## Goal

Show battery percent next to the header icon, and add a Battery info page with
voltage, current, charging state and a runtime estimate. Promote the existing
`FURBLE_BATTERY_DEBUG` code into a supported feature so later power PRs have a
measurement harness.

## Scope

In scope:

- New `BATT_STYLE` setting: icon, percent, or both. Default icon, which is
  current behaviour.
- Header status area renders per that setting.
- New Settings -> Power -> Battery page with voltage, current, charging state,
  level and a runtime estimate.
- Board switched battery backend: AXP192 boards report current, the M5PM1 on the
  S3 needs its own path, the Core reports level only.
- Optional console logging of battery samples, used as the drain measurement
  harness by PR01, PR07, PR12 and PR13.

Out of scope:

- Low battery policy and auto power off. That is PR13.
- Removing the `FURBLE_BATTERY_DEBUG` build flag. Keep it working.

## Files to change

- `src/FurbleUI.cpp:146-150`, the `FURBLE_BATTERY_DEBUG` fork that creates either
  a label or an image for `m_Status.batteryIcon`. Replace with an always present
  icon plus an optional label, driven by the setting.
- `src/FurbleUI.cpp:156-201`, the 250 ms icon timer. It currently holds both the
  `FURBLE_BATTERY_DEBUG` EWMA branch (lines 160-167) and the five level icon
  branch (lines 169-183). Split battery sampling out of this timer.
- `include/FurbleUI.h:67-75`, `status_t`. `batteryIcon` is at line 70. Add a
  `batteryLabel` member and the last sampled values.
- `include/FurbleUI.h:176-182`, add `m_BatteryStr = "Battery"`.
- `include/FurbleUI.h:337`, declare `void addBatteryMenu(const menu_t &parent);`.
- `src/FurbleUI.cpp:53-76`, add the `m_BatteryStr` entry to `UI::m_Menu`. It sits
  under Power, which is a scrolling page, so `{0, 0}` is fine.
- `src/FurbleUI.cpp:1851-1950`, add `addBatteryMenu` following the
  `addDisplayMenu` layout, and add the style roller to the Power page created by
  PR01.
- `include/FurblePlatform.h:52`, `M5PM1 m_M5PM1;`. Add battery accessors on
  `Platform` so the UI never talks to the PMIC directly.
- `src/FurblePlatform.cpp:34-39`, the `FURBLE_M5STICKS3` block where `m_M5PM1`
  is begun. The retry helper belongs next to it.
- `include/FurbleSettings.h:16-29` and `:145-148`, enum and `storage_type`.
- `src/FurbleSettings.cpp:11-24` and `:186-227`, table row and default.
- `platformio.ini:5`, `-DFURBLE_BATTERY_DEBUG=0`. Leave the flag in place.

## New settings

| Item | Value |
|---|---|
| Enum | `Settings::BATT_STYLE` |
| Display name | `Battery Style` |
| NVS key | `batt_style` (10 chars) |
| NVS namespace | `FURBLE_STR` |
| Storage type | `uint8_t` |
| Values | 0 icon, 1 percent, 2 both |
| Default | 0, icon only |

Default 0 renders exactly the five state icon that master shows today.

## Menu placement

```
Settings
+- Power
   +- CPU speed        (PR01)
   +- Battery style    (roller, this PR)
   +- Battery          (page, this PR)
```

The Power page is created by PR01 and is a scrolling list on all boards, so no
grid slot is consumed on Core and Core2 beyond the one PR01 already takes.

## Implementation notes

### Sampling rate

`src/FurbleUI.cpp:156` creates the icon timer with a 250 ms period. Battery reads
go over I2C to the PMIC. Four I2C reads per second for a value that changes over
minutes is waste, and on the S3 each read may cost two transactions, see below.
Add a separate battery timer at 5 s, keep the 250 ms timer for the GPS icon and
the screen lock message box, and have the 250 ms timer render the cached values.

### Board backends

Use the runtime `M5.getBoard()` switch pattern already used at
`src/FurbleUI.cpp:95-109` and `src/FurblePlatform.cpp:24-32`. Do not compile out
board paths.

- AXP192 boards, `board_M5StickC`, `board_M5StickCPlus`, `board_M5StackCore2`:
  `M5.Power.getBatteryVoltage()`, `getBatteryCurrent()` and `isCharging()` are
  all meaningful. Current is signed, positive when charging.
- M5StickS3, `board_M5StickS3`: the PMIC is the M5PM1. The M5PM1 driver exposes
  `readVbat` and `readVin` but documents no battery current register. Probe
  `M5.Power.getBatteryCurrent()` on the device first. If it returns an error or a
  constant, fall back to voltage only and hide the current row rather than
  printing a fake number.
- M5Stack Core: the PMIC is an IP5306, which reports a coarse level only. Show
  level and hide voltage, current and runtime.

Feature detection must be by probe result, not by assumption. Read once at
startup, record which fields are usable, and hide the rest.

### M5PM1 I2C wake

The M5PM1 sleeps after an I2C idle period, configured with `setI2cSleepTime`.
The vendor documentation states that the first communication after that sleep
fails and only wakes the chip. So every read path on the S3 must retry once:
issue the transaction, and on failure repeat it. Put this in one helper in
`Platform` and use it for every M5PM1 access. `src/FurblePlatform.cpp:86` already
calls `m_M5PM1.btnGetState()` every update and checks for `M5PM1_OK`, so the
failure mode is already reachable in the current code.

### Runtime estimate

Runtime in hours is remaining capacity in mAh divided by average discharge
current in mA. Needs three things:

1. A per board capacity table. The StickS3 is 250 mAh per the vendor product
   page. Fill the other boards from their product pages before merge. Do not
   guess.
2. Remaining capacity from `getBatteryLevel()`, which returns 0 to 100.
3. Smoothed current. Reuse the EWMA already written at
   `src/FurbleUI.cpp:164-165`, `mean = mean + (current - mean) / 3`. At a 5 s
   sample period that is a time constant of roughly 15 s, which is too twitchy
   for a runtime figure. Use a slower alpha, for example `/ 12`, and say in the
   UI that the estimate is rough.

Show the estimate only where current is available. Show "unknown" elsewhere.
While charging, show charging instead of a runtime.

### Percent in the header

The header height is set from the font line height at `src/FurbleUI.cpp:204`.
Adding a label next to the icon costs horizontal space, which is tight on the
StickC at 80 px wide. Check the StickC first. If the percent does not fit there,
still offer the setting and let the text clip or hide it on that board, but state
the behaviour in the PR body.

### Measurement harness

Add an optional periodic `ESP_LOGI` of level, voltage, current and uptime, gated
behind a compile time flag that defaults off, or behind the existing
`FURBLE_BATTERY_DEBUG`. Later power PRs need a log they can diff across builds.
Console output while USB powered reflects charging, so drain runs must be
unplugged and the log read afterwards, or captured from the S3 USB console during
a brief reconnect.

## Dependencies

- PR01 creates the Settings -> Power page this PR extends. Land PR01 first.
- Blocks PR05, which adds a fuller battery detail view to Diagnostics, and
  PR10 and PR16, which reuse the debug page pattern.
- Every later power PR uses this PR's harness for before and after numbers.

## Risks

- `getBatteryCurrent()` behaviour on the S3 is unverified. Mitigation: probe and
  hide rather than display a wrong number.
- The M5PM1 wake retry could mask a real I2C fault. Log the retry at debug level
  and count failures.
- Runtime estimates that are visibly wrong are worse than no estimate. Label it
  as an estimate and only show it when the inputs are real.
- Extra I2C traffic keeps the CPU out of light sleep more often. The 5 s period
  and the cached render mitigate this. Measure before and after.

## Verification

Build matrix:

```
export FURBLE_VERSION=dev FURBLE_TEST=0
pio run -e m5stick-c -e m5stick-c-plus -e m5stack-core -e m5stack-core2 -e m5stick-s3
```

Also build once with `-DFURBLE_BATTERY_DEBUG=1` to confirm that path still
compiles.

On device, M5StickS3 over USB:

1. Fresh NVS boot. Confirm the header shows the icon only and looks identical to
   master.
2. Set style to percent, then both. Confirm the header updates without a restart.
3. Open Settings -> Power -> Battery. Confirm voltage is plausible against a
   multimeter reading at the battery, or against the level shown.
4. Unplug USB, confirm charging state flips and current goes negative or is
   hidden.
5. Plug USB, confirm charging state flips back.
6. Watch the log for M5PM1 retry messages. A steady trickle of first transaction
   failures is expected. A rising failure count after the retry is a bug.

On one AXP192 device, M5StickC Plus: repeat steps 1 to 5 and confirm current is
reported. This PR is board switched, so a second board is required.

If a Core is available, confirm the page hides voltage, current and runtime and
shows level only.

Battery drain, no external power meter:

- Unplugged, connected to the Fujifilm camera and idle, log every 30 s for
  60 minutes on master and on this branch. Compare the voltage slope. The extra
  I2C traffic must not make drain measurably worse.
- Repeat for 30 minutes in the menu, disconnected.

Camera coverage: Fujifilm only, used as a realistic load. No BLE code changes.
State this in the PR body.

## References

- M5Unified Power_Class, `getBatteryLevel`, `getBatteryVoltage`,
  `getBatteryCurrent`, `isCharging`, `setExtOutput`, `powerOff`:
  https://docs.m5stack.com/en/arduino/m5unified/power_class
- M5Unified Power_Class header:
  https://github.com/m5stack/M5Unified/blob/master/src/utility/Power_Class.hpp
- M5Stack StickS3 low power configuration, M5PM1 power levels and I2C idle sleep,
  including the note that the first communication after sleep fails:
  https://docs.m5stack.com/en/arduino/m5sticks3/m5pm1
- M5PM1 driver library, I2C address 0x6E, `readVbat`, `readVin`, `shutdown`,
  `timerSet`, `setI2cSleepTime`, `btnGetState`, `readRtcRAM`:
  https://github.com/m5stack/M5PM1/blob/main/README_FUNCTION_EN.md
- M5Stack StickS3 product page, 250 mAh battery:
  https://docs.m5stack.com/en/core/StickS3
- LVGL 9.4, Roller widget:
  https://lvgl.io/docs/open/9.4/details/widgets/roller
- ESP-IDF, Logging library:
  https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/system/log.html
