# PR18 - Motion adaptive GPS

## Goal

Cut GPS power when the device is not moving. Use the IMU to detect a stationary
state, then drop the receiver to standby or a low fix rate. Resume full rate as
soon as motion returns. Off by default.

## Scope

In scope:

- New `GPS_MOTION` setting, default false.
- Stationary detection from accelerometer variance.
- Hooks into the GPS power policy added in PR15.
- Menu entry under `Settings->GPS`, greyed out unless `IMU` is on.
- Fast resume so that geodata stays inside the existing freshness budget.

Out of scope:

- The `$PCAS` command builder. That is PR14.
- The receiver power policy itself, standby vs rail cycling. That is PR15.
- Any change to camera geodata protocol code.

## Files to change

Verified anchors against the current tree.

| File | Lines | What |
|---|---|---|
| `include/FurbleSettings.h` | 16-29 | `type_t` enum. Add `GPS_MOTION`. |
| `include/FurbleSettings.h` | 101-148 | `storage_type<>` binding, `bool`. |
| `src/FurbleSettings.cpp` | 11-24 | Setting table. One new row. |
| `src/FurbleSettings.cpp` | 169-230 | Defaults. Add to the false group at 209-215. |
| `include/FurbleGPS.h` | 32-56 | Private section. Add motion state, timestamps and the policy hook. |
| `include/FurbleGPS.h` | 38 | `MAX_AGE_MS = 30 * 1000`. The freshness budget this PR must respect. |
| `src/FurbleGPS.cpp` | 111-133 | `enable()` and `disable()`. The existing power on and off points. |
| `src/FurbleGPS.cpp` | 136-143 | `reloadSetting()`. Reload `GPS_MOTION` here as well. |
| `src/FurbleGPS.cpp` | 150-158 | `startService()`, the 1 Hz `SERVICE_MS` timer. Motion evaluation hangs off this. |
| `src/FurbleGPS.cpp` | 161-192 | `update()`. Contains the `MAX_AGE_MS` checks at 166-172 and the `Control::updateGPS` call at 186. |
| `src/FurbleUI.cpp` | 1514-1604 | `addGPSMenu()`. Add the motion entry, following the show and hide pattern at 1550-1553. |
| `src/FurbleUI.cpp` | 705-748 | `addSettingItem()`, including the GPS specific show and hide callback at 733-748. |

Camera side, read only, no changes:

| File | Lines | What |
|---|---|---|
| `lib/furble/Fujifilm.cpp` | 132-137 | `updateGeoData()` only sends when `m_GeoRequested` is set. |
| `src/FurbleControl.cpp` | 36-39, 63-66 | `Target::updateGPS()` stores the last fix, `CMD_GPS_UPDATE` forwards it to the camera. |

## New settings

| Enum | NVS key | Namespace | Type | Default | Notes |
|---|---|---|---|---|---|
| `GPS_MOTION` | `gps_motion` (10) | `FURBLE_STR` | `bool` | `false` | False keeps the PR15 policy running unchanged. |

Name string: `"Motion Adaptive"`.

## Menu placement

```
Settings
└─ GPS
   ├─ GPS              (existing)
   ├─ GPS Baud         (existing)
   ├─ Update Rate      (PR14)
   ├─ Sentences        (PR14)
   ├─ Constellation    (PR14)
   ├─ Power Saving     (PR15)
   ├─ Motion Adaptive  (this PR)
   └─ GPS Data         (existing)
```

The entry is greyed out when `IMU` is false or when `GPS` is false. Follow the
existing hide logic: the GPS switch callback at `src/FurbleUI.cpp:733-748` already
shows and hides `m_Status.gpsBaud` and `m_Status.gpsData`. Add the motion entry to
the same list. Add a second gate on `Settings::IMU`.

## Implementation notes

Stationary detection:

- Reuse the accelerometer poll from PR17 if that PR is merged. Otherwise create a
  10 Hz `lv_timer`. 10 Hz is enough for a stationary or moving decision and is
  much cheaper than the 50 Hz gesture poll.
- Keep a rolling variance of the acceleration magnitude over a 5 s window.
- Stationary when the variance stays under a small threshold, roughly 0.02 g
  squared, for a continuous 60 s. The long hold avoids flapping when a tripod
  mounted camera is nudged.
- Moving as soon as a single sample exceeds the threshold. Entry is slow, exit is
  immediate. That asymmetry is what protects fix freshness.

Policy interaction with PR15:

- This PR does not add a new receiver control path. It selects between the
  policies PR15 already implements.
- Stationary and the PR15 policy is standby: send the standby command.
- Stationary and the PR15 policy is rail cycling: cut the 5 V rail with
  `M5.Power.setExtOutput(false, m5::ext_PA)`, mirroring `GPS::disable()` at
  `src/FurbleGPS.cpp:126-133`.
- Stationary and the PR15 policy is always on: drop the fix rate instead, using
  the PR14 `$PCAS02` path. Do not cut power.
- Moving: restore the full rate policy immediately.

Fix freshness is the hard constraint. `GPS::update()` only forwards a fix to
`Control::updateGPS()` when location, date and time are all newer than
`MAX_AGE_MS`, which is 30 s (`include/FurbleGPS.h:38`,
`src/FurbleGPS.cpp:166-172`). A stationary device does not move, so the last fix
stays geographically correct, but the age check will fail after 30 s of silence
and `m_HasFix` will go false. Two consequences:

- The GPS icon will show no fix while stationary. That is confusing.
- The camera stops receiving updates.

Handling:

- Keep a cached last good fix in `GPS`, with its own timestamp, separate from the
  TinyGPS++ age. While stationary and the cache is younger than a configurable
  ceiling, keep sending it and keep the icon showing a fix.
- Any camera request must trigger an immediate wake of the receiver and a fresh
  fix attempt in the background, so the next request is real data.
- Do not touch `MAX_AGE_MS` itself. Other code paths depend on it.

Fujifilm on-request path. `Fujifilm::updateGeoData()`
(`lib/furble/Fujifilm.cpp:132-137`) only writes when the camera has asked, by
setting `m_GeoRequested`. The request can arrive at any time, including while the
receiver is in standby. The wake path must be short enough that the camera gets
data soon after asking. Measure it. If a cold restart of the receiver takes longer
than a few seconds, prefer the low-rate policy over full standby when a camera is
connected, and say so in the PR body.

Wake latency depends on whether the GPS unit has backup power. That is hardware
experiment B in the index plan. Its result decides the default stationary policy
for the unit in use. Do not guess. State the measured re-fix time in the PR body.

Do not run stationary detection while the intervalometer is running with deep
sleep enabled (PR19). Those two features both control power to the same rail.
Give PR19 priority and skip motion policy in that case.

## Dependencies

- PR15 (GPS power policies). Hard dependency. This PR chooses between policies
  and does not implement any of them.
- PR16 (IMU enable). Hard dependency.
- PR14 (`$PCAS` support) is needed for the low-rate variant.
- PR17 is optional. If present, share its accelerometer poll timer.
- Hardware experiment B feeds the default stationary policy.

## Risks

- Stale geodata written to a photo. If the device moves and the detector is slow
  to notice, a frame can get the previous position. The immediate exit rule
  limits this to one poll period, but the receiver still needs time to re-fix.
  Bound the cached fix lifetime and stop sending when it expires.
- Camera requests arriving during standby. Fujifilm asks on its own schedule.
  If wake is slow, the camera sees nothing. Mitigation is the low-rate policy
  while connected.
- Vibration on a tripod in wind can keep the detector in the moving state, so no
  power is saved. Acceptable. Log the state so it can be diagnosed.
- Variance thresholds are per board. Tune on BMI270 first, then check MPU6886.
- The 10 Hz poll costs power. If the poll costs more than the GPS saves, the
  feature is pointless. Measure both.
- Interaction with the PR06 light sleep locks. Waking the receiver takes a
  NO_LIGHT_SLEEP lock. Frequent stationary and moving transitions will thrash
  the lock. The 60 s entry hold limits the rate.

## Verification

Build matrix:

```
pio run -e m5stick-c -e m5stick-c-plus -e m5stack-core -e m5stack-core2 -e m5stick-s3
```

Defaults regression: fresh NVS boot. `GPS_MOTION` false. GPS behaviour identical
to master and to PR15. No motion timer created.

On device, M5StickS3 with GPS/BDS Unit v1.1 over USB:

1. `pio run -e m5stick-s3 -t upload`, then `pio device monitor`.
2. With `IMU` off, confirm the Motion Adaptive entry is greyed out.
3. Turn `IMU` on, restart, turn `GPS_MOTION` on.
4. Acquire a fix outdoors. Confirm normal operation while walking.
5. Put the device down. Confirm the log reports stationary after about 60 s and
   the selected policy is applied.
6. Pick the device up and move it. Confirm the log reports moving within one poll
   period and the receiver returns to full rate.
7. Measure time from motion to first valid fix after each stationary duration:
   1 minute, 10 minutes, 60 minutes. Record all three.
8. Confirm the GPS Data page (`src/FurbleUI.cpp:1548-1603`) shows a sensible age
   value in both states and does not show garbage while stationary.

Camera checks, Fujifilm only:

1. Connect to a Fujifilm body with GPS and motion adaptive on. Leave the rig
   stationary on a tripod for 10 minutes. Take a frame. Check the EXIF position
   on the card is present and correct.
2. Move 200 m, take a frame within 30 s of stopping. Check EXIF position updated.
3. Long run: 60 minutes stationary, then one frame. Report whether the position
   was written and how long the receiver took to serve the request.
4. Confirm the geodata request path still works after several stationary and
   moving cycles. Watch for the `updateGeoData` log line at
   `src/FurbleControl.cpp:64`.

Battery impact, on-board instrumentation only, no external meter:

1. Unplug USB, log battery voltage and percent every 30 s.
2. Run A: 60 minutes connected with GPS on and motion adaptive off, device
   stationary.
3. Run B: same 60 minutes with motion adaptive on.
4. Report both drain slopes and the difference. If run B is not clearly better,
   the feature does not justify its complexity. Say so.

## References

All links checked.

- StickS3 product page: https://docs.m5stack.com/en/core/StickS3
- StickS3 low power guide, M5PM1 power levels and IMU wake:
  https://docs.m5stack.com/en/arduino/m5sticks3/m5pm1
- M5Unified IMU class API: https://docs.m5stack.com/en/arduino/m5unified/imu_class
- M5Unified `IMU_Class` header: https://github.com/m5stack/M5Unified/blob/master/src/utility/IMU_Class.hpp
- Bosch BMI270 sensor API, any-motion and no-motion features:
  https://github.com/boschsensortec/BMI270_SensorAPI
- ESP-IDF sleep modes, background for the light sleep interaction:
  https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32/api-reference/system/sleep_modes.html
