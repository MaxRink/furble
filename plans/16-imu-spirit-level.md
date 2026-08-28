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
| `IMU` | `imu` (45) | `FURBLE_STR` | `bool` | `false` | False reproduces the current hardcoded `cfg.internal_imu = false`. |

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
- `IMU` is assigned wire_id 45 in the current settings table. Wire id 46 is
  reserved for the companion password setting from PR #166.
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
  power cycle, and is wired at wire id 45.
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
`action nav level` / `nav imu`, shared `imu.accel` / `imu.roll` / `imu.pitch`
injections that feed the production read path through the simulated IMU, an
`action select` that
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
and clang-format 21 is clean. Wire id 45 (IMU) is unchanged.

On-device retest owed before merge: level page responsiveness, no overlapping
text, the side bubble tracking roll, and the Diagnostics IMU live short-press
back returning to the submenu.

## UX rework v3 after hardware testing, 2026-08-21

The v2 level page was retested on the M5StickS3 and four more points came back:
text still clipped, the display slept on the level page, no auto-rotate when the
device was laid on its side, and the side bubble tube was wanted only on the
flipped view. All four are addressed on this branch. On-device retest of the
level page is still owed before merge. The simulator cannot see the physical
screen, the real IMU, or the hardware display rotation path.

### 1. Cut-off text fixed for real

The v2 stacked labels still clipped because each line read "Roll: -12.3 deg" in
the default 16 px font, wider than the 80x160 panel and close to the 135x240
edge. The labels now read "R:"/"P:" with a degree glyph in the 12 px font, set to
100 percent width, centre aligned and wrapping. The worst case in portrait is
"P: -90.0" which fits 80 px. Removing the side tube from the portrait page (see
point 4) also frees vertical room, so the circle diameter reserve dropped from
`m_Height - 130` to a shared `levelDiameter` helper reserving 96 px for the label
and hint stack.

### 2. Keep the screen awake on the level page

furble dims and sleeps the display from `processInactivity`, which is gated on
`lv_disp_get_inactive_time`. The level timer now calls
`lv_display_trigger_activity(NULL)` every tick, so the inactivity clock never
elapses while the page is open. This is self restoring: the timer only runs while
the page is shown, so leaving the page stops the activity resets and the normal
dim and sleep behaviour resumes with no extra bookkeeping. There is no separate
hardware backlight timer to hold off, the backlight follows the same inactivity
path.

### 3. Auto-rotate onto the side

The page rotates the LVGL display from the smoothed roll while it is open.
`applyLevelRotation` calls `lv_display_set_rotation` and reflows the widgets for
the new resolution. Hysteresis stops it flapping at the boundary: it enters a
side past 60 degrees and returns to flat below 45 degrees, holding the current
orientation in the band between. A positive roll rotates to
`LV_DISPLAY_ROTATION_90`, a negative roll to `LV_DISPLAY_ROTATION_270`. The
rotation is scoped to the level page. The page dispatch forces
`LV_DISPLAY_ROTATION_0` on entry and restores it on exit, so the rest of the UI
is never rotated. A non-focusable hint label on the flat page reads "Tilt on side
to rotate".

The sign of the side-to-rotation mapping is a guess until hardware confirms which
way the panel physically reads. Flag for the on-device pass: correct the 90 and
270 assignment if a right roll rotates the wrong way.

### 4. Bubble tube only when flipped

The default flat portrait page shows the circle bubble, the numeric readout and
the hint, with the side tube hidden. When the page flips to a side orientation it
hides the circle and hint and shows the linear tube plus the numeric readout. The
numeric tilt readout stays on both. This matches the request to keep the tube off
the default page and only show it on the flipped side view, and it is what frees
the vertical room that fixes the portrait clipping.

### Rotation hysteresis thresholds

- Enter landscape: smoothed `|roll| >= 60` degrees.
- Return to portrait: smoothed `|roll| < 45` degrees.
- Hold current orientation in the 45 to 60 degree band.

### Hardware verification risk: S3 DMA display rotation

LVGL software rotation via `lv_display_set_rotation` is straightforward in the
SDL simulator and is verified there. On the real StickS3 it interacts with the
DMA display buffers, which must stay `MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL`, and
with the M5GFX flush. The rotated flush path is the key hardware risk for this
change and has not been exercised off the simulator. The on-device pass must
confirm the rotated frames render correctly, without tearing or a wrong stride,
and that returning to portrait leaves the rest of the UI upright.

### Simulator verification v3

New `simQueryState` keys: `level_rotation` reports the active page rotation in
degrees, and `level_side_visible` reports whether the side tube is currently
shown. `level_root_width`, `level_root_height`, and `level_side_on_screen`
prove the rotated display resolution reaches the pixel-sized top-level window
and the complete moving bubble stays inside the framebuffer. This caught a
real defect where the simulated panel became 240 pixels wide while the root
window stayed at its original 135 pixels. `level_has_side` still reports
whether the tube widget exists.

- `sim/scenarios/e2e/level-spirit.txt` was rewritten for v3. It asserts the flat
  page opens in portrait with the tube hidden, a 6 degree roll drives the circle
  bubble 18 px while staying portrait, a 70 degree right roll flips to rotation
  90 and shows the tube pinned near the rim, a 70 degree left roll flips to
  rotation 270, and returning near flat restores portrait and hides the tube.
  Overflow stays no in both orientations. Reverting the sensitivity mapping to
  the old linear curve fails the 18 px assert. The landscape checks also fail
  if the root window retains portrait geometry or either extreme bubble
  position is clipped.
- `sim/scenarios/bughunt/overflow-sweep.txt` still asserts the flat level page
  fits. Verified no overflow on 80x160, 135x240 and 320x240, and a landscape
  probe confirmed the flipped page also fits on all three.

What the simulator cannot exercise, owed on hardware:

- Keep-awake: the sim has no inactivity dim, so the activity reset is not
  observable. Confirm on device that the level page stays lit.
- The DMA rotation flush path above.
- The physical roll-to-rotation sign.

### PENDING HARDWARE RETEST

This v3 rework is verified in the simulator and host build only. A full on-device
pass on the M5StickS3 is required before merge: no clipped text flat or flipped,
the screen staying awake on the level page, the display rotating the correct way
when laid on its side and returning to portrait when held flat, the tube showing
only when flipped, and the rest of the UI staying upright after leaving the page.

## UX rework v3.1 after hardware testing, 2026-08-21

The v3 level page was retested on the M5StickS3. Keep-awake held and the text no
longer clipped, but two defects came back: the bullseye sat at the top jammed
under the header and was cut off, and the side-rotate auto-flip corrupted the
display until the device was held flat again. The Level entry in the Connected
menu was also the only item without an icon. All are addressed on this branch.
On-device retest of the rotation flush is still owed before merge.

### 1. One row of readout text, split left and right

The v3 page stacked the roll and pitch labels in a column above the circle, which
ate vertical room and pushed the circle up. The two labels are now a single row
at the very top, roll in the left half and pitch in the right half, each 50 percent
wide, centre aligned, in montserrat_12 with the degree glyph and wrapping on. One
line height frees the rest of the panel for the bullseye. Worst case "R: -90.0" and
"P: -90.0" fit their halves on 135x240 and wrap without clipping on 80x160.

### 2. Bullseye repositioned and centred with margin

The circle and the side tube now live in a middle container that flex-grows to
fill the space under the readout row. The visible readout is centred in that
container, so the bullseye keeps a symmetric top and bottom margin and is never
jammed under the header. On the StickS3 the circle top sits at y=50, well clear of
the menu header, with the diameter unchanged from v3.

### 3. Centre target ring

A fixed hollow ring (green, 22 px) is drawn at the exact centre of the bullseye as
the "level" mark. It never moves. The moving blue bubble nests inside it when the
device is level, so the target and the bubble are always distinct. Neither is
focusable, so no focus outline appears.

### 4. DMA-safe auto-rotate

v3 rotated with LVGL software rotation (`lv_display_set_rotation`). On the StickS3
that rotates pixels in the draw buffer but does not coordinate with the panel DMA
flush, so a rotated frame tears against the old stride and only recovers when the
device is held flat again. The firmware path now rotates the panel controller
instead: it drains any in-flight flush with `M5.Display.waitDMA()`, calls
`M5.Display.setRotation()` for the new orientation, and swaps the LVGL logical
resolution with `lv_display_set_resolution()`. It then reflows every widget to the
new geometry, and only after that full-invalidates and calls `lv_refr_now()` so the
whole screen is repainted in one pass in the new geometry, and drains the DMA
again. The reflow-before-repaint order is load bearing: see the half-width fix in
v3.2 below. The DMA engine therefore always writes a consistently oriented
framebuffer. The base panel rotation is captured once at build time while
the panel is in its portrait default. The hysteresis thresholds (enter a side at
60 degrees, return to flat below 45) and the relayout are unchanged. The simulator
has no DMA, so under FURBLE_SIM the page still uses `lv_display_set_rotation`,
which lets a scenario verify the orientation state machine and the relayout but
not the flush. PENDING HARDWARE RETEST: the rotated DMA flush can only be
confirmed on device.

### 5. Level menu icon

The Connected menu Level entry was iconless while every sibling had an icon. A new
bullseye icon (`icon_adjust`, an outer ring with a centre dot, matching the level's
centre target) was generated from `components/icons/svg/adjust.svg` through the
standard Material Symbols pipeline (rsvg-convert to 48 and 24 px PNGs, then
LVGLImage.py to RGB565A8 LZ4 C arrays). It is wired in `icons.h`, the icons
`CMakeLists.txt`, the `#define icon_adjust icon_adjust_24` size alias, and
`addLevelMenu`.

### Full SDL sim IMU support

The simulator now injects general IMU state, not just a level-widget poke. A new
`sim/ImuSim.cpp` holds a settable accel, gyro and enabled flag in `Furble::Sim`,
declared in `sim/driver.h`. Under FURBLE_SIM the firmware reads it through the same
`isEnabled`, `update`, `getAccel` and `getGyro` surface it uses for M5.Imu, in both
the level timer and the diagnostics live page, so a scenario drives the real read
path. The production binary is byte-identical: every new read is inside
`#if defined(FURBLE_SIM)` with the original M5.Imu code in the `#else`. This infra
is general enough to later exercise gestures (#45), wake on motion (#48) and gps
motion (#65), not just the level page.

New sim actions (all FURBLE_SIM only):

- `imu.accel <x> <y> <z>`: set the accelerometer vector in G and enable the IMU.
- `imu.roll <deg>` / `imu.pitch <deg>`: set a gravity vector for a pure roll or
  pitch orientation.
- `imu.gyro <x> <y> <z>`: set the gyroscope rate.
- `imu.enable` / `imu.disable`: toggle the injected IMU enabled flag.

Each level injection resets the level filter so the next timer tick settles to the
exact injected orientation, keeping the bubble geometry deterministic. There is
no widget-level injection shortcut.

New `simQueryState` observers: `level_bubble_visible` and `level_target_visible`
(the bullseye and its target ring show only in portrait), `level_diameter` (the
bullseye content diameter) and `level_surface_top` (its absolute top edge, which
proves it clears the header).

### Simulator verification v3.1

- `sim/scenarios/e2e/level-spirit.txt` was rewritten to drive the general
  `imu.roll` / `imu.pitch` / `imu.accel` injection through the firmware read path.
  It asserts the one-row layout (diameter 89, surface top 50), the bullseye and
  target visible only in portrait, a 6 degree tilt driving the circle bubble 18 px,
  the side tube only in the flipped orientation, and the rotation hysteresis both
  ways (50 degrees holds the current orientation, 60 enters a side, below 45
  returns to flat, right roll 90, left roll 270). Mutation test: reverting the
  sensitivity mapping to the old +/-45 degree linear curve drops the 6 degree
  value and the scenario fails.
- `sim/scenarios/bughunt/overflow-sweep.txt` still asserts the flat level page
  fits. Verified `ui.overflow no` on the fixed-height level page at 135x240 and
  80x160.

All 14 end-to-end scenarios pass. The firmware `m5stick-s3-debug` build is clean
and clang-format 21 is clean. Wire id 45 (IMU) is unchanged.

### PENDING HARDWARE RETEST (v3.1)

Sim and host build verified only. The on-device pass on the M5StickS3 must confirm:
the one-row text does not clip flat or flipped, the bullseye and centre target sit
below the header with margin, the DMA-safe rotation flips cleanly with no
corruption and returns to portrait leaving the rest of the UI upright, and the
Level menu icon renders. The roll-to-rotation sign flagged in v3 still needs the
on-device check.

### v3.2 landscape geometry fix

The v3.1 on-device test on the M5StickS3 confirmed the layout is good: the one-row
text does not clip, and the bullseye and centre target sit below the header with
margin. These are HARDWARE-CONFIRMED. The DMA-safe rotation no longer fully
corrupts the screen, but two landscape geometry defects remained.

Half-width landscape render. In landscape the panel is 240 wide but only the left
135 px (the portrait width) was painted, and the far side kept its pre-rotation
pixels. Root cause: `applyLevelRotation` forced its one guaranteed full-screen
repaint (`lv_obj_invalidate` plus `lv_refr_now`) right after the panel rotation and
the `lv_display_set_resolution` swap, but BEFORE the widget reflow. The resolution
swap resizes the active screen, yet the descendant layout is only recomputed on
demand, so at repaint time the level container and the screen children were still
laid out at the portrait width. The single forced pass therefore drew only the
portrait-width content, and the periodic bubble updates that follow invalidate only
small regions, so the right half never recovered. The fix reorders the function:
rotate the panel and swap the resolution, then reflow the whole screen with
`lv_obj_update_layout(lv_screen_active())`, then re-anchor the button overlay, and
only then full-invalidate and `lv_refr_now`. The one forced repaint now covers the
full rotated width.

Button indicator anchoring. On the StickC and StickS3 family the physical-button
indicators float and are aligned to the screen edges rather than living in a flex
navbar. They were pinned to the portrait corners and did not move when the page
rotated, so they landed in the wrong place in landscape. The fix stores their
handles on the level state and re-anchors them against the live rotated resolution
inside `applyLevelRotation` (bottom-left, bottom-mid, right-mid), so they reflow to
the rotated edges and return to portrait on exit.

The sim uses software rotation and repaints the whole SDL frame each cycle, so it
cannot reproduce either defect. The level e2e and overflow sweep pass unchanged
(the rotation STATE logic is untouched, only the hardware flush geometry and the
overlay anchoring changed). The branch was also rebased onto current fork/master to
pick up the reconnect fixes (#120 NimBLE client leak, #121 fast-reconnect stall)
that the v3.1 build predated.

### PENDING HARDWARE RETEST (v3.2)

Sim and host build verified only. The on-device pass on the M5StickS3 must confirm
the landscape frame now fills the full 240 px width with no stale right half, and
that the physical-button indicators land on the correct rotated edges in both
landscape orientations and return to their portrait corners on exit. The
roll-to-rotation sign flagged in v3 still needs the on-device check.

## Sim self-verification of the level page and screen redraw, 2026-08-23

The level page and the screen-redraw behaviour are now proven headless, so the
page is self-verifying in CI and no longer gated on a device for anything the
simulator can express. The principle: it does not make sense to bench-test a page
whose layout, bubble tracking or redraw the simulator can already fail on.

### Level page across all three panel widths

The prior level e2e (`level-spirit.txt`) pins the exact 135x240 geometry and runs
on the default panel only. A width-agnostic companion,
`sim/scenarios/e2e/level-overflow.txt`, now runs on 80x160, 135x240 and 320x240.
It injects a tilt sweep (flat, roll left and right, pitch up and down, both side
flips) through the IMU seam and asserts, on every panel, that the page rendered
widgets (`ui.visible_objects >= 1`), that the bubble tracks the injected tilt in
the correct direction (a right roll drives `ui.level_bubble_x` positive, a left
roll negative, pitch on the y axis the same way), that the page flips to landscape
past 60 degrees and shows the side tube, and that `ui.overflow` stays `no` flat,
tilted and flipped. Direction is asserted rather than exact pixels, so the one
scenario holds on all three panel classes. CI builds the 320x240 M5Stack Core
simulator alongside the existing 80x160 build and runs this scenario, plus the
IMU and redraw scenarios below, on both the narrow and wide panels.

### IMU live diagnostics render

`sim/scenarios/e2e/imu-diagnostics.txt` drives the injected accelerometer sample
and asserts the Diagnostics > IMU live page renders it. New `simQueryState` keys
`imu_accel_x` / `imu_accel_y` / `imu_accel_z` parse the value back out of the live
label text, so a missing poll or a broken label format fails the assert, not just
the read path.

The companion `imu-gating.txt` scenario seeds the IMU off and checks that the
level and live diagnostics entries stay hidden and unreadable. It then enables
the host seam and proves the same pages expose and render a fresh sample.

### Screen-redraw storm guard

The level page updates continuously as tilt changes, which is a prime redraw-storm
risk (see CLAUDE.md "LVGL redraw trap"; a real device stalled a page into the task
watchdog at 358 invalidations per second). `sim/scenarios/e2e/redraw-steady.txt`
holds the level page and the connected page at a fixed injected state and asserts
the invalidation count stays low over a full second. The plumbing: an
`invalidate.reset` sim action zeroes a probe counter fed by the same LVGL
`LV_EVENT_INVALIDATE_AREA` hook the sim already uses, and a `ui.invalidate_count`
query reports the events since the reset. The driver gained numeric `assert_max`
and `assert_min` commands for the bound checks. With the guarded, changed-only
setters the level and connected pages hold at 0 invalidations per second in the
sim. A mutation that reverts the roll/pitch label setters to fire every 50 ms tick
drives the count to 60 and fails `assert_max`, confirming the guard has teeth.

All of the above is behind `#if defined(FURBLE_SIM)`. The release firmware is
byte-identical: a per-panel bin comparison against the same source without the
seam differs only in the ESP-IDF compile-time-date bytes and the derived image
hash, never in a code byte.

### Reduced hardware scope for #28

The level page layout, bubble tracking, auto-rotate state machine, no-overflow at
all three widths, the IMU diagnostics render, and the steady-state redraw discipline
are all now sim-verified. The only residual on-device check for #28 is the
physically irreducible one: that the real BMI270 / MPU6886 sensor reads through
`M5.Imu` and the bubble tracks actual device tilt (and, tied to that read, the
roll-to-rotation sign and the S3 DMA rotation flush filling the full landscape
width, which the SDL software-rotation path cannot reproduce). Everything the
simulator can express is green before that bench step.
