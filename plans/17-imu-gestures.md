# PR17 - IMU gestures for wake and shutter

## Goal

Use the IMU to wake the display from the off state added in PR12. Optionally fire
the shutter on a double tap. Both are off by default.

## Scope

In scope:

- New `IMU_WAKE` setting: off, tap, shake, both. Default off.
- New `IMU_TRIG` setting: double tap fires the shutter. Default false.
- Motion detection on the accelerometer stream, running only when a gesture
  feature is enabled.
- Menu entries under `Settings->Sensors`.
- A written description of the StickS3 deep wake path (IMU INT1 to M5PM1 GPIO4)
  for a later PR. Not implemented here.

Out of scope:

- Deep sleep or power off wake. This PR only wakes the display inside a running
  system.
- Any change to the display off logic itself. That is PR12.
- GPS behaviour. That is PR18.

## Files to change

Verified anchors against the current tree.

| File | Lines | What |
|---|---|---|
| `include/FurbleSettings.h` | 16-29 | `type_t` enum. Add `IMU_WAKE`, `IMU_TRIG`. |
| `include/FurbleSettings.h` | 101-148 | `storage_type<>` specialisations. `uint8_t` and `bool`. |
| `src/FurbleSettings.cpp` | 11-24 | Setting table. Two new rows. |
| `src/FurbleSettings.cpp` | 169-230 | Defaults. `IMU_WAKE` joins the `uint8_t` group near 196-198, `IMU_TRIG` joins the false group at 209-215. |
| `include/FurbleUI.h` | 161-191 | Menu strings. Add `m_WakeGestureStr`. |
| `include/FurbleUI.h` | 299-346 | Add `addWakeGestureMenu()` and a gesture poll handler. |
| `src/FurbleUI.cpp` | 53-76 | `m_Menu` map. Add the wake gesture page entry. |
| `src/FurbleUI.cpp` | 112-117 | Inactivity `lv_timer` created at 1 Hz. Model the gesture poll timer on it, but at a higher rate. |
| `src/FurbleUI.cpp` | 705-731 | `addSettingItem()` bool helper. Reused for `IMU_TRIG`. |
| `src/FurbleUI.cpp` | 1851-1932 | `addDisplayMenu()`, including the inactivity roller at 1931. Reference for the roller pattern used by `IMU_WAKE`. |
| `src/FurbleUI.cpp` | 2062-2082 | `addSettingsMenu()`. `Sensors` gains the new entries. |
| `src/FurbleUI.cpp` | 2090-2112 | `setInactivityTimeout()` and `processInactivity()`. A gesture must reset the inactivity state the same way user input does. |
| `src/FurbleUIGesture.cpp` | new | Gesture detector. Keeps `FurbleUI.cpp` from growing further. |

The shutter path is `Control::sendCommand(Control::CMD_SHUTTER_PRESS)` and
`CMD_SHUTTER_RELEASE`, as used by the intervalometer at
`src/FurbleUI.cpp:1200` and `src/FurbleUI.cpp:1207`.

## New settings

| Enum | NVS key | Namespace | Type | Default | Notes |
|---|---|---|---|---|---|
| `IMU_WAKE` | `imu_wake` (63) | `FURBLE_STR` | `uint8_t` | `0` | 0 off, 1 tap, 2 shake, 3 both. 0 is current behaviour. |
| `IMU_TRIG` | `imu_trigger` (64) | `FURBLE_STR` | `bool` | `false` | False is current behaviour. |

Name strings: `"Wake Gesture"` and `"Double-Tap Shutter"`.

Both settings are ignored when `IMU` (PR16) is false. Grey out both menu entries
in that case, matching how the GPS baud and GPS Data entries are hidden when GPS
is off (`src/FurbleUI.cpp:1550-1553`, `src/FurbleUI.cpp:733-748`).

## Menu placement

```
Settings
└─ Sensors
   ├─ IMU                 (PR16)
   ├─ Wake Gesture        (roller: Off / Tap / Shake / Both)
   └─ Double-Tap Shutter  (switch)
```

No new top level submenu. `Sensors` was created by PR16.

## Implementation notes

M5Unified does not expose the BMI270 feature engines. `IMU_Class` offers
`getAccel`, `getGyro`, `update`, `getType`, `isEnabled` and
`setINTPinActiveLogic`, and no wake on motion or register level API. Bosch splits
the BMI270 feature sets: the default config provides any-motion, no-motion,
significant motion and wrist gestures, while single, double and triple tap
detection lives in the BMI270 legacy feature config. Loading a different feature
config would mean shipping a config blob and talking to the sensor directly.

Decision: detect gestures in software on the accelerometer stream. This works
identically on BMI270 and MPU6886, keeps the code in one place, and needs no
vendor library. The hardware engines stay available for a later PR if software
detection proves too costly.

Detector design:

- Poll `M5.Imu.update()` and `M5.Imu.getAccel()` from an `lv_timer` at 50 Hz.
  Create the timer only when `IMU` is true and at least one gesture feature is
  enabled. Delete it when both are disabled. This keeps the idle path unchanged
  for default users.
- Shake: keep an EWMA of the acceleration magnitude minus 1 g. Shake fires when
  the deviation stays above about 0.6 g for at least 3 samples inside a 500 ms
  window. Tune on device.
- Tap: a tap is a short high jerk spike. Track the sample to sample delta of the
  magnitude. A tap candidate is a delta above about 1.5 g that decays back inside
  120 ms. Double tap is two candidates 80 ms to 400 ms apart.
- After any gesture, hold a 750 ms refractory period. This stops one physical
  event producing a burst.

Wake behaviour:

- On a gesture matching `IMU_WAKE`, call the same display wake path PR12 uses,
  and reset the LVGL inactivity counter so `processInactivity()`
  (`src/FurbleUI.cpp:2094-2112`) does not immediately dim again.
- A wake gesture must never also fire the shutter. Gate `IMU_TRIG` on the display
  being awake, and swallow the gesture that caused the wake.

Shutter behaviour:

- `IMU_TRIG` only acts when a camera is connected and the UI is on the Connected
  or Remote page. It sends `CMD_SHUTTER_PRESS` then `CMD_SHUTTER_RELEASE` with
  the same spacing the remote page uses.
- Show a warning line on the settings page: a bag or a strap knock can trigger a
  frame. Keep the default off.

Poll cost. A 50 Hz timer prevents long light sleep windows. Measure the effect
and state it plainly in the PR body. If the cost is large, drop the poll to 25 Hz
when the display is off, which is enough for shake and marginal for tap. In that
case restrict `IMU_WAKE` tap mode to display-on use, or accept a lower tap
detection rate and say so.

Deep wake path for StickS3, documented only, not implemented:

The M5Stack low power guide describes waking the whole system from the M5PM1
using an IMU interrupt. The BMI270 any-motion interrupt is mapped to INT1 as
active low, push-pull and non-latched. The M5PM1 is configured with
`gpioSetWakeEnable(M5PM1_GPIO_NUM_4, true)` and
`gpioSetWakeEdge(M5PM1_GPIO_NUM_4, M5PM1_GPIO_WAKE_FALLING)`, then
`setLdoEnable()` and `ldoSetPowerHold(true)` keep the L1 rail alive so the IMU
stays powered while the M5PM1 sleeps, and `shutdown()` drops the system. On wake
the M5PM1 re-runs the power on sequence and the ESP32-S3 boots from scratch. That
means the app restarts, so this path needs the state save and restore work in
PR19 before it is useful. Sleep current figures for the L0 and L1 levels are not
published in the M5Stack documentation, so any number in this project must come
from our own on-board measurement.

## Dependencies

- PR12 (display off). Without it there is no off state to wake from, and the
  wake gesture only restores brightness.
- PR16 (IMU enable). Hard dependency. `M5.Imu` returns nothing useful when
  `cfg.internal_imu` is false.
- Independent of PR18, but both read the same accelerometer stream. If both are
  merged, share one poll timer.

## Risks

- False triggers. This is the main risk for `IMU_TRIG`. A camera in a bag, a
  tripod bump or a strap swing can look like a double tap. Default off, warning
  text in the UI, refractory period, and a conservative threshold.
- Threshold tuning is per board. The BMI270 and MPU6886 differ in noise floor and
  full scale defaults. Tune per `M5.Imu.getType()` if one set of numbers does not
  work for both.
- The 50 Hz poll costs power and blocks light sleep. Measure before and after.
- Waking on shake can produce a wake loop if the device is on a vibrating
  surface, for example a car dashboard. The inactivity timer will re-dim, so the
  loop is bounded, but it drains the battery. Note it in the PR body.
- A gesture that fires the shutter while the intervalometer is running would
  corrupt a sequence. Block `IMU_TRIG` while the intervalometer timer is not
  paused (`src/FurbleUI.cpp:1169-1227`).

## Verification

Build matrix:

```
pio run -e m5stick-c -e m5stick-c-plus -e m5stack-core -e m5stack-core2 -e m5stick-s3
```

Defaults regression: fresh NVS boot must behave exactly like master. `IMU_WAKE`
off, `IMU_TRIG` off, no gesture timer created, no measurable extra idle current.

On device, M5StickS3 over USB:

1. `pio run -e m5stick-s3 -t upload`, then `pio device monitor`.
2. With `IMU` off, confirm both gesture entries are greyed out.
3. Turn `IMU` on and restart. Entries become active.
4. Set Wake Gesture to Shake. Let the display go off. Shake once, display comes
   back. Confirm the log shows a single gesture event, not a burst.
5. Set Wake Gesture to Tap. Tap the case once. Display comes back.
6. Set Wake Gesture to Both. Verify both work.
7. Set Wake Gesture to Off. Verify shaking does nothing.
8. False trigger run: put the device in a pocket and walk 5 minutes with Wake
   Gesture set to Shake. Count wakes from the log. Report the number.
9. Drop test for the refractory period: tap 10 times fast, confirm the event
   count is bounded and no crash.

Camera checks, Fujifilm only:

1. Connect to a Fujifilm body. Enable `IMU_TRIG`. Double tap. Exactly one frame
   is taken. Repeat 20 times, count frames on the camera against the log.
2. Single taps must not fire. 20 single taps, zero frames.
3. Start the intervalometer, then double tap. No extra frame, shot count matches
   the configured count.
4. False trigger run with `IMU_TRIG` on: put the device in a camera bag, carry it
   5 minutes, count frames. Report the number in the PR body, including zero.

On device, one AXP192 board (MPU6886) for detector portability:

1. Repeat steps 4 to 7. Record whether the thresholds needed a per-chip table.

Battery impact, on-board instrumentation only:

1. Unplug USB, log battery voltage and percent every 30 s.
2. 30 minutes connected and idle with all gestures off.
3. 30 minutes connected and idle with Wake Gesture set to Both.
4. Report both drain slopes in the PR body.

## Implementation state

Implemented:

- Added `IMU_WAKE` with the `imu_wake` NVS key and `IMU_TRIG` with the
  `imu_trigger` NVS key. Both default to the current disabled behavior.
- Added a software accelerometer detector for tap, double tap, and shake.
  The detector runs at 50 Hz only when a gesture feature is enabled and the
  effective IMU setting is on.
- Added Settings -> Sensors entries for Wake Gesture and Double-Tap Shutter.
  Both entries are disabled when the effective IMU setting is off. The page
  includes the false-trigger warning.
- Added the Connected and Remote page checks, intervalometer guard, display
  wake hook, inactivity reset, refractory period, and short shutter command
  pair.
- Added console and companion setting type and value cases. Console and
  companion changes notify the UI task without touching LVGL from another task.
- The requested S3 build was attempted twice. The first attempt was blocked by
  the PlatformIO core lock. The second reached CMake but could not install the
  ESP-IDF Python dependency because PyPI DNS is blocked in the sandbox.

Deviations:

- This branch is based on PR16 and does not contain the PR12 display-off state.
  The current wake hook calls `M5.Display.wakeup()`, restores the configured
  brightness, and triggers LVGL activity. Rebase it onto the PR12 wake helper
  when the branches are combined.
- Poll power cost and hardware behavior are not measured yet. Hardware
  verification is pending.

Rebase notes:

- Wire IDs are `IMU_WAKE` 63 and `IMU_TRIG` 64. IDs 45 and 46 are reserved by
  the shipped IMU setting and companion password; 47/48 are reserved by the
  open hardware-motion roadmap, and 51-62 by the Wi-Fi/MQTT settings ledger.

## References

All links checked.

- StickS3 low power guide, M5PM1 sleep levels, IMU interrupt wake, and the
  `gpioSetWakeEnable`, `gpioSetWakeEdge`, `ldoSetPowerHold`, `shutdown` calls:
  https://docs.m5stack.com/en/arduino/m5sticks3/m5pm1
- StickS3 wakeup guide: https://docs.m5stack.com/en/arduino/m5sticks3/wakeup
- M5PM1 Arduino library: https://github.com/m5stack/M5PM1
- M5PM1 function reference:
  https://github.com/m5stack/M5PM1/blob/main/README_FUNCTION_EN.md
- M5Unified IMU class API: https://docs.m5stack.com/en/arduino/m5unified/imu_class
- M5Unified `IMU_Class` header, confirms there is no wake on motion API:
  https://github.com/m5stack/M5Unified/blob/master/src/utility/IMU_Class.hpp
- Bosch BMI270 sensor API, feature list. Tap detection is in the legacy feature
  set, any-motion and no-motion are in the default set:
  https://github.com/boschsensortec/BMI270_SensorAPI
- Bosch BMI270 product page:
  https://www.bosch-sensortec.com/products/motion-sensors/imus/bmi270/
- StickS3 product page, confirms the BMI270:
  https://docs.m5stack.com/en/core/StickS3
