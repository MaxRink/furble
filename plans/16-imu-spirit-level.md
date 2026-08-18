# PR16 - Enable the IMU and add a spirit level

## Goal

Turn on the on-board IMU and use it. Add a spirit level page on the Connected
menu and a live IMU page under Diagnostics. The IMU stays off by default.

## Scope

In scope:

- New `IMU` setting, default false.
- Reorder `Settings::init()` before `Platform::init()` in `main.cpp`.
- Pass `cfg.internal_imu` from the setting instead of the hardcoded `false`.
- New `Settings->Sensors` submenu.
- Spirit level page on the Connected menu.
- Live IMU page under `Settings->Diagnostics`.
- Restart prompt when the toggle changes.

Out of scope:

- Gestures, wake on motion, tap shutter (PR17).
- Motion driven GPS policy (PR18).
- Any IMU interrupt or PMIC wake wiring (documented in PR17).

## Files to change

Verified anchors against the current tree.

| File | Lines | What |
|---|---|---|
| `src/FurblePlatform.cpp` | 16-22 | `M5.config()` block. Line 18 is `cfg.internal_imu = false;`. Replace with the loaded setting. |
| `src/main.cpp` | 27-28 | Currently `Furble::Platform::init();` then `Furble::Settings::init();`. Swap the two lines. |
| `include/FurbleSettings.h` | 16-29 | `type_t` enum. Add `IMU`. |
| `include/FurbleSettings.h` | 101-148 | `storage_type<>` specialisations. Add the `IMU` binding. |
| `src/FurbleSettings.cpp` | 11-24 | Setting table. Add the `IMU` row. |
| `src/FurbleSettings.cpp` | 169-230 | `Settings::init()` defaults. Add `IMU` to the false-default group at 209-215. |
| `include/FurbleUI.h` | 161-191 | Menu name strings. Add `m_SensorsStr`, `m_LevelStr`, `m_IMUDataStr`. |
| `include/FurbleUI.h` | 299-346 | Private menu builders. Add `addSensorsMenu()`, `addLevelMenu()`, `addIMUDataMenu()`. |
| `src/FurbleUI.cpp` | 53-76 | `m_Menu` map with `{col,row}` grid positions. Add the three new entries. |
| `src/FurbleUI.cpp` | 705-731 | `addSettingItem()` bool switch helper. Reused for the `IMU` toggle. |
| `src/FurbleUI.cpp` | 1278-1428 | Connected menu construction. Add the Level entry next to Remote, Interval, Disconnect. |
| `src/FurbleUI.cpp` | 1984-1992 | Theme restart precedent. Same pattern for the IMU toggle. |
| `src/FurbleUI.cpp` | 2062-2082 | `addSettingsMenu()`. Call `addSensorsMenu(menu)`. |

## New settings

| Enum | NVS key | Namespace | Type | Default | Notes |
|---|---|---|---|---|---|
| `IMU` | `imu` (3) | `FURBLE_STR` | `bool` | `false` | False reproduces the current hardcoded `cfg.internal_imu = false`. |

Name string in the table: `"IMU"`.

## Menu placement

```
Settings
└─ Sensors
   └─ IMU        (switch, restart button)
Settings
└─ Diagnostics
   └─ IMU live   (hidden when IMU is off)
Connected
└─ Level         (hidden when IMU is off)
```

`Sensors` is created by this PR. It uses `addMenu(m_SensorsStr, <icon>, true, parent)`
and one `addSettingItem(menu.page, NULL, Settings::IMU)` call.

The IMU live page belongs under the `Diagnostics` submenu created by PR05. If
PR05 is not merged yet, put the live page under `Sensors` and move it in a
follow-up.

`m_Menu` grid coordinates are only meaningful for the Core grid layout
(`src/FurbleUI.cpp:2065-2069`). Take the next free `{col,row}` on the Settings
page. PR01 (Power), PR05 (Diagnostics) and PR08 (Bluetooth) also add cells, so
the final grid must be settled by whichever of those lands last.

## Implementation notes

Init order is the critical part. `Platform::getInstance()` runs `M5.config()` and
`M5.begin()` at `src/FurblePlatform.cpp:16-22`, and `main.cpp:27` calls it before
`Settings::init()` at `main.cpp:28`. Reading the `IMU` setting inside
`Platform::getInstance()` therefore requires the swap.

The swap is safe. `Settings::init()` (`src/FurbleSettings.cpp:169-230`) only calls
`nvs_flash_init()`, `nvs_flash_erase()` and the `Preferences` wrapper.
`lib/preferences/Preferences.cpp:15-21` includes only `esp_log.h`, `nvs.h` and
`nvs_flash.h`. There is no M5Unified dependency, so NVS can be brought up before
`M5.begin()`.

Read the setting once in `Platform::getInstance()` and assign it:

```
cfg.internal_imu = Settings::load<Settings::IMU>();
```

M5Unified abstracts the sensor. `IMU_Class` reports `imu_bmi270` on StickS3 and
`imu_mpu6886` on the older sticks, and exposes `getAccel()`, `getGyro()`,
`update()`, `getType()` and `isEnabled()`. Use `M5.Imu.isEnabled()` at runtime to
hide the Level and IMU live entries. No `#ifdef` is needed for reads. On StickS3
the BMI270 sits on the internal I2C bus at address 0x68.

Spirit level maths: read accelerometer x, y, z, then

```
roll  = atan2(y, z)
pitch = atan2(-x, sqrt(y*y + z*z))
```

Draw a bubble as an `lv_obj` circle offset from the page centre, clamped to a
fixed radius, plus numeric roll and pitch labels. Refresh from an `lv_timer` at
about 20 Hz, and pause the timer when the page is not shown. Use the same
timer start/stop pattern as the GPS Data page (`src/FurbleUI.cpp:1592-1611`).

Apply a light low pass filter (EWMA, alpha around 0.2) to the accelerometer
vector. Raw BMI270 output is noisy enough to make the bubble jitter.

Axis orientation differs per board. Use `M5.Imu.setAxisOrder()` or a per-board
sign table selected by `M5.getBoard()`, following the existing runtime switch
style at `src/FurbleUI.cpp:95-109`. Get the S3 orientation right first, then
correct the AXP192 sticks.

Toggling `IMU` needs a restart because `cfg.internal_imu` is only consumed by
`M5.begin()`. Copy the Theme page pattern at `src/FurbleUI.cpp:1984-1992`: a
Restart button that saves the setting and calls `esp_restart()`.

Cameras stay untouched. No BLE code changes.

## Dependencies

- PR02 (battery display) and PR05 (diagnostics scaffold) per the roadmap
  dependency graph. PR05 provides the `Diagnostics` submenu that hosts the IMU
  live page.
- PR03 is useful but not required. Without it, `Settings->Sensors` is only
  reachable from the main menu.
- Blocks PR17 and PR18.

## Risks

- Init reorder touches boot. If NVS init fails before `M5.begin()`, there is no
  display to report it. `Settings::init()` uses `ESP_ERROR_CHECK`, so the failure
  becomes a panic and a reboot loop. Check the console log first when debugging a
  brick.
- IMU on adds current draw. BMI270 in normal accelerometer mode costs a few
  hundred microamps. The setting defaults to false so nothing regresses.
- Wrong axis signs on non-S3 boards produce a mirrored bubble. Cosmetic only.
- The level page refresh timer keeps LVGL busy and blocks light sleep while
  shown. Pause the timer on page exit.
- `M5.Imu.isEnabled()` returning false on a board with no IMU must hide the
  entries instead of crashing.

## Verification

Build matrix:

```
pio run -e m5stick-c -e m5stick-c-plus -e m5stack-core -e m5stack-core2 -e m5stick-s3
```

All five must build clean with `-Wall -Wextra`.

Defaults regression:

1. Erase NVS, flash master, note boot behaviour and menu layout.
2. Flash this branch on a fresh NVS. Boot must be identical. The `Sensors`
   submenu exists, the IMU toggle is off, Level and IMU live are hidden.

On device, M5StickS3 over USB:

1. `pio run -e m5stick-s3 -t upload`, then `pio device monitor`.
2. Confirm boot log order: NVS init messages appear before M5 init.
3. Settings -> Sensors -> IMU on -> Restart. Device reboots.
4. Confirm `M5.Imu.getType()` logs `imu_bmi270`.
5. Connected menu shows Level. Lay the device flat: bubble centres, roll and
   pitch near zero. Tilt 45 degrees each axis and check the sign and magnitude.
6. Diagnostics -> IMU live shows moving accel and gyro values.
7. Turn IMU off, restart, confirm Level and IMU live are hidden again.
8. Power cycle twice to confirm the setting persists.

On device, one AXP192 board (StickC or StickC Plus) because the board switch
changes:

1. Repeat steps 3 to 7. `getType()` must report `imu_mpu6886`.

Camera check, Fujifilm only (the only hardware available):

1. Connect to a Fujifilm body with IMU on. Open Level, then Remote, fire the
   shutter. Confirm the level page does not disturb the connection.
2. Leave connected 10 minutes with the Level page open. No disconnects.

Battery impact, on-board instrumentation only, no external meter:

1. Unplug USB. Log battery voltage and percent to the console every 30 s.
2. Run 30 minutes connected and idle with IMU off, then 30 minutes with IMU on.
3. Compare drain slopes. Expect a small difference. Record the numbers in the PR
   body.

## References

All links checked.

- StickS3 product page, confirms BMI270 and 250 mAh battery:
  https://docs.m5stack.com/en/core/StickS3
- M5Unified IMU class API (`getAccel`, `getGyro`, `update`, `getType`,
  `isEnabled`): https://docs.m5stack.com/en/arduino/m5unified/imu_class
- M5Unified `IMU_Class` header, confirms the `imu_bmi270` and `imu_mpu6886` enum
  values and that no wake or register API is exposed:
  https://github.com/m5stack/M5Unified/blob/master/src/utility/IMU_Class.hpp
- Bosch BMI270 product page:
  https://www.bosch-sensortec.com/products/motion-sensors/imus/bmi270/
- Bosch BMI270 sensor API, feature list per config variant:
  https://github.com/boschsensortec/BMI270_SensorAPI
- StickS3 low power guide, shows the BMI270 at I2C 0x68 alongside the M5PM1:
  https://docs.m5stack.com/en/arduino/m5sticks3/m5pm1

## Hardware verification, 2026-08-17

Partial verification on the combined image. The `imu` setting saves, reads
back and reports `applies: on reboot`. After reboot the boot log shows no
BMI270 line because M5Unified detects the IMU silently, and this branch has no
`imu` console command. On-device checks remain: the Diagnostics menu should
show the `IMU live` page (gated on `M5.Imu.isEnabled()`) and the spirit level
page needs a visual pass. Both are on the user checklist.

## Hardware verification, pass 3, 2026-08-18

Verdict: PARTIAL. Tested on the combined image (fork/master plus feat/16,
feat/35, feat/13, feat/25, version `hwv3`, app `v3.9.1-159-g138dd80`) flashed to
the M5StickS3 over USB.

Console evidence:

- `settings set imu true` then reboot then `settings get imu` returns
  `value: true`, `applies: on reboot`. The setting saves, persists across a
  power cycle, and is wired at wire id 27.
- Boot with `imu true` completes and the device runs. No crash attributable to
  the IMU. Toggling `imu false` and rebooting also boots clean.

Not console observable, still on the user checklist:

- BMI270 detection does not print a boot line. M5Unified brings up the IMU
  silently and this branch adds no `imu` console command, so the boot log shows
  no `bmi270`. Same conclusion as pass 2.
- Spirit level bubble centering when flat and direction under tilt, the page
  timer stopping on leave, the Diagnostics `IMU live` accel and gyro readout,
  and the Level menu entry icon all need eyes on the screen.

Combined image caveat: this build tripped a one shot boot task watchdog. See the
cross cutting note in plans/25. The watchdog is independent of the `imu` setting,
it reproduced with `imu false`, so it is not attributed to this PR.
