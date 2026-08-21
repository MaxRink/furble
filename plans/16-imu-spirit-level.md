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

## Implementation status

Implemented:
- Added the `IMU` setting with key `imu`, default `false`, and `cfg.internal_imu` loading before `M5.begin()`.
- Reordered startup so `Settings::init()` runs before `Platform::init()`. This is safe because Settings initialization only brings up NVS and the Preferences wrapper.
- Added runtime M5Unified IMU reads for the Connected level and Diagnostics live pages, with EWMA filtering and runtime visibility.
- Added Settings -> Sensors with the IMU switch, restart notice, and Restart action. Added Connected -> Level and Settings -> Diagnostics -> IMU live.
- The main menu page dispatch resumes the level timer only while the Level page is open and pauses it otherwise, the same pattern as the diagnostics timer. The dispatch also gates diagnostics IMU polling on the IMU live page being active.

Review fixes after the first PR pass:
- Bubble positioning keeps the LVGL centre alignment and applies only a delta, computed from the surface content width. The earlier code doubled the offset because style x/y adds to the centre alignment.
- Removed the board axis-order switch. It only re-applied the M5Unified default, so it was a no-op. The code now documents the assumption at the IMU init site: default axis order, roll from atan2(ay, az) drives screen X, pitch drives screen Y.
- Replaced the CLICKED-based level timer start/stop with the page dispatch. The old stop callback never unregistered itself (wrong event target) and programmatic navigation stranded the timer.
- The layout is settled with `lv_obj_update_layout` on page entry so the first frame reads real geometry.
- Dropped the duplicate `lv_menu_set_load_page_event` for the Level button and the no-op IMU setting re-save before restart.

Deviations:
- No scope deviations. Axis orientation uses the M5Unified default on every board. Sign confirmation is deferred to hardware verification.

Known cost, accepted for now:
- The 20 Hz level timer keeps running through the inactivity dim while the Level page stays open. Not fixed in this PR.

Hardware verification is still pending on all boards: IMU detection, level axis orientation per board, and bubble centring have not been checked on device. The StickS3 walk is owed before merge.

Rebase notes:
- `IMU` is assigned wire_id 27, continuing after `GPS_DUTY` (26) from PR 27.
- Console settingType, printValue and setValue treat `IMU` as a bool setting.
  `appliesImmediately` stays false because the setting is read once before
  `M5.begin()` and the UI offers an explicit restart.
- `src/FurbleCompanion.cpp` settingType and settingValue cover `IMU` as
  SETTING_BOOL.
- Second rebase onto master with PR 26 and PR 27 merged: the Settings menu
  keeps both master's GPS 'Power saving' page and this branch's 'Sensors'
  page. The empty CI retrigger commit was dropped.

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

## UX rework after hardware testing, 2026-08-21

Hardware testing on the M5StickS3 found the level page usable but not merge
ready. Four defects were fixed on this branch as new commits. On-device retest of
the level page is required before merge, the simulator cannot see the physical
screen or the real IMU.

### 1. Overlapping text on the level page

The old layout put the roll and pitch labels side by side in a row. On the narrow
135x240 and 80x160 panels the two "Roll/Pitch: -12.3 deg" strings did not fit and
collided. `addLevelMenu` now stacks the value labels in a column, drops the circle
diameter reserve to `m_Height - 130` to make room, and turns off scrolling on the
containers. Nothing overlaps.

### 2. Spirit level not responsive enough

The bubble mapping was linear over +/-45 deg, so a large tilt was needed to move
it. The circle is for finding exact level, so small tilts near flat must be
clearly visible. `applyLevelSample` (factored out of `levelUpdate`) now maps
+/-15 deg to full deflection and applies a sub unity gain curve
(`pow(|tilt/15|, 0.6)`) that adds extra gain close to centre. A 6 degree tilt now
moves the circle bubble 16 px instead of the old 4 px, four times more sensitive.
The EWMA filter (alpha 0.2) is kept so a still device does not jitter. Every
`lv_obj_set_pos` and `lv_label_set_text` stays behind its changed check.

### 3. Side view bubble level

Added a classic linear bubble tube under the circle. It carries roll only, like a
spirit level held against a wall, with the same sensitivity curve as the circle.
`level_t` gains `sideTube`, `sideBubble` and `sideBubbleX`.

### 4. Diagnostics IMU live back navigation trap

The IMU live page (and the other read-only diagnostics pages) hold only info-row
labels. `addInfoRow` makes each label clickable and adds it to the encoder group
so button boards can scroll, and `lv_menu` lands the focus on the first row when
the page opens. A short select then hits a dead label, so only the universal
long-press back left the page. The page dispatch now queues an `lv_async_call`
that moves the focus onto the existing header back button after the page load, so
a normal select returns to the parent. It reuses the header arrow, it adds no new
focusable widget (no focus-outline regression). The trap is broader than IMU
live, it is the same class for About, Device info and BLE, which are all fixed
here. Power state also uses info rows but already carries an actionable Dump
button, so it was left alone.

### Simulator verification

The device IMU is disabled in the simulator, so a FURBLE_SIM seam was added:
`action nav level` / `nav imu`, a `level_accel x y z` injection that feeds a
synthetic accel vector through `applyLevelSample`, an `action select` that
activates the focused object (models a short OK press), and `simQueryState` keys
`level_bubble_x`, `level_bubble_y`, `level_side_x`, `level_has_side` and
`back_focused`.

- `sim/scenarios/e2e/level-spirit.txt`: reaches the level page, asserts it does
  not overflow the panel, that a 6 deg tilt drives the circle bubble 16 px and
  the side tube 25 px, that pitch moves only the vertical axis and leaves the
  side tube centred, and that a 30 deg tilt pins both bubbles to the rim. Mutation
  test: reverting the mapping to +/-45 deg linear drops the 6 deg value to 4 px
  and the scenario fails.
- `sim/scenarios/e2e/imu-back-nav.txt`: walks to the IMU live page, asserts the
  header back button holds the focus, and that a normal select returns to
  Diagnostics. Mutation test: disabling the focus fix leaves `back_focused` at no
  and the scenario fails.
- `sim/scenarios/bughunt/overflow-sweep.txt`: adds the level page to the
  per-panel fit sweep. It fits on 80x160, 135x240 and 320x240.

All 13 end-to-end scenarios pass. The firmware `m5stick-s3-debug` build is clean
and clang-format 21 is clean. Wire id 27 (IMU) is unchanged.

On-device retest owed before merge: level page responsiveness, no overlapping
text, the side bubble tracking roll, and the Diagnostics IMU live short-press
back returning to the submenu.
