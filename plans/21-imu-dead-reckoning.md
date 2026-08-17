# PR21 - Position hold when the GPS fix is lost

## Goal

Keep supplying a position to the camera when the GPS fix goes away. Today furble
silently stops. `GPS::update()` (`src/FurbleGPS.cpp:161-192`) only calls
`Control::updateGPS()` when location, date and time are all valid and all newer
than `MAX_AGE_MS`, which is 30 s (`include/FurbleGPS.h:38`). One tunnel, one
camera bag, one GPS standby window from PR15, and photos stop being tagged with
no indication beyond a small icon change.

This PR adds a bounded hold of the last known fix, an optional short
extrapolation, and an honest UI state for both. It does not attempt inertial
navigation.

## Physics first

Dead reckoning from a consumer MEMS accelerometer does not work. Position is the
double integral of acceleration, so any residual acceleration bias `b` produces a
position error of `0.5 * b * t^2`.

- A 10 mg accelerometer bias is 0.098 m/s^2. Error is 4.9 m at 10 s, 44 m at
  30 s, 176 m at 60 s.
- A 1 degree attitude error tilts gravity into the horizontal plane, which is
  9.81 * sin(1 deg) = 0.171 m/s^2. Error is 8.6 m at 10 s, 77 m at 30 s, 308 m
  at 60 s.

Attitude error is the dominant term and it is not constant. Gyro bias makes the
attitude error itself grow with time, so the real curve is worse than `t^2`.
There is no calibration, no magnetometer on these boards and no zero velocity
update opportunity while the device is in a moving vehicle. Accelerometer double
integration is rejected. It would produce positions that look plausible and are
wrong by hundreds of metres, which is worse than no position at all.

What the IMU is actually good for here is the opposite question: deciding that
the device has not moved. That decision is cheap, robust and is what tier 2 uses.

## Scope

Three tiers, in increasing risk and decreasing value.

- Tier 1, last known fix hold. No IMU. Keep sending the last good fix for a
  configurable time after the fix is lost. This is the whole practical win.
  Geotag accuracy degrades with time in a way the user controls.
- Tier 2, stationary aware hold. With PR18 or PR20 reporting stationary, the
  hold clock does not advance. A device sitting on a tripod keeps a valid
  position indefinitely, up to a hard cap.
- Tier 3, short horizon extrapolation from the last GPS course and speed.
  Experimental, off by default, hard cutoff in tens of seconds.

Out of scope:

- Any change to camera protocol code.
- Any change to `MAX_AGE_MS`. Other logic depends on it.
- Accelerometer double integration. Rejected above.

## Files to change

Verified anchors against the current tree.

| File | Lines | What |
|---|---|---|
| `include/FurbleGPS.h` | 32-56 | Private section. Add the fix cache struct, the hold deadline, the fix quality enum and the extrapolation state. |
| `include/FurbleGPS.h` | 38 | `MAX_AGE_MS = 30 * 1000`. Unchanged. It stays the definition of a live fix. |
| `include/FurbleGPS.h` | 53-54 | `m_Enabled`, `m_HasFix`. `m_HasFix` becomes a three state quality value. |
| `src/FurbleGPS.cpp` | 136-143 | `reloadSetting()`. Load `GPS_HOLD` and `GPS_EXTRAP` here too. |
| `src/FurbleGPS.cpp` | 161-192 | `update()`. The whole change lives here. |
| `src/FurbleGPS.cpp` | 166-172 | Freshness check. On pass, cache the fix. On fail, decide hold, extrapolate or nothing. |
| `src/FurbleGPS.cpp` | 174-187 | `gps_t` and `timesync_t` construction and the `Control::updateGPS()` call. Fed from the cache when held. |
| `src/FurbleGPS.cpp` | 189-191 | Icon update. Two states today. Becomes three. |
| `src/FurbleControl.cpp` | 193-200 | `Control::updateGPS()`. Unchanged. Held fixes go down the same path at the same 1 Hz rate. |
| `lib/furble/Camera.h` | 50-55 | `gps_t`: latitude, longitude, altitude, satellites. Unchanged. |
| `lib/furble/Camera.h` | 60-68 | `timesync_t`. Unchanged, but the values must be advanced during hold. See notes. |
| `lib/furble/Fujifilm.cpp` | 93-130 | `sendGeoData()`. Read only. The geotag payload carries lat, lon, alt and a timestamp. No accuracy or satellite field. |
| `lib/furble/Fujifilm.cpp` | 132-137 | `updateGeoData()` only writes when `m_GeoRequested` is set. Read only. |
| `lib/furble/NikonSmart.cpp` | 223 | `satellites` is packed into the Nikon payload. Read only, but it constrains what we may put in that field. |
| `lib/furble/NikonSmart.h` | 112 | `uint8_t satellites` in the Nikon struct. |
| `include/FurbleSettings.h` | 16-29 | `type_t` enum. Add `GPS_HOLD`, `GPS_EXTRAP`. |
| `include/FurbleSettings.h` | 101-148 | `storage_type<>` bindings. `uint8_t` and `bool`. |
| `src/FurbleSettings.cpp` | 11-24 | Setting table. Two rows. |
| `src/FurbleSettings.cpp` | 169-230 | Defaults. `GPS_HOLD` joins the `uint8_t` group near 190-198, `GPS_EXTRAP` joins the false group at 209-215. |
| `src/FurbleUI.cpp` | 1514-1604 | `addGPSMenu()`. Two new entries, hidden with the existing `gpsBaud` and `gpsData` group. |
| `src/FurbleUI.cpp` | 1548-1603 | GPS Data page timer. Add a fix state line. |
| `src/FurbleUI.cpp` | 1911-1933 | Inactivity roller. Pattern for the hold roller. |
| `src/FurbleUI.cpp` | 705-748 | `addSettingItem()` and the GPS show and hide callback at 733-748. Add the new objects to that list. |
| `include/FurbleUI.h` | 67-75 | `status_t`. Add pointers for the two new menu objects, next to `gpsBaud` and `gpsData`. |

## New settings

| Enum | NVS key | Namespace | Type | Default | Notes |
|---|---|---|---|---|---|
| `GPS_HOLD` | `gps_hold` (8) | `FURBLE_STR` | `uint8_t` | `0` | Roller index. 0 Off, 1 = 30 s, 2 = 2 min, 3 = 10 min, 4 = 60 min. 0 reproduces current behaviour exactly. |
| `GPS_EXTRAP` | `gps_extrap` (10) | `FURBLE_STR` | `bool` | `false` | Tier 3. False reproduces current behaviour. |

Name strings: `"Fix Hold"` and `"Extrapolate"`.

Tier 2 gets no key. It reuses `GPS_MOTION` from PR18. When `GPS_MOTION` is on and
the motion source reports stationary, the hold clock does not advance. That is
one behaviour rule, not a second setting, and it keeps the settings tree small.

`GPS_EXTRAP` is greyed out when `GPS_HOLD` is 0, because extrapolation is bounded
by the hold window.

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
   ├─ Motion Adaptive  (PR18)
   ├─ Fix Hold         (this PR, roller)
   ├─ Extrapolate      (this PR, switch)
   └─ GPS Data         (existing, gains a fix state line)
```

Both entries hide with GPS, using the same callback that already hides
`m_Status.gpsBaud` and `m_Status.gpsData` at `src/FurbleUI.cpp:733-748`.

## Implementation notes

### Fix state

Replace the `bool m_HasFix` with a small enum:

```
enum class Fix { NONE, HELD, LIVE };
```

`LIVE` is the current condition at `src/FurbleGPS.cpp:166-172`. `HELD` means the
cache is being replayed. `NONE` means nothing is sent, which is today's behaviour
in every non live case.

### Fix cache

On every `LIVE` pass, copy the values into a cache: latitude, longitude,
altitude, satellites, the UTC date and time, the last course and speed, and
`Platform::tick()` at the moment of capture. Copy explicitly rather than reading
TinyGPS++ later, because the individual TinyGPS++ fields update independently and
a partial update would mix data from two epochs.

### Hold logic in update()

```
if (live) {
  cache = current;
  hold_start = 0;
  send(cache);
} else if (hold_enabled && cache.valid) {
  if (hold_start == 0) hold_start = tick();
  if (stationary_known()) hold_start = tick();   // tier 2, clock does not advance
  if (tick() - hold_start <= hold_limit_ms) {
    send(extrapolate_or_hold(cache));
  }
}
```

Tier 2 uses the PR18 or PR20 motion source. If that source is unavailable or
`GPS_MOTION` is off, `stationary_known()` returns false and only tier 1 applies.
A hard cap still applies to tier 2, set to the `GPS_HOLD` value multiplied by a
fixed factor or to a compile time ceiling of one hour, whichever is smaller. A
device that has been still for a day should not tag a photo from yesterday's
position without any check at all. Decide the ceiling on device and state it in
the PR body.

### Timestamps during hold

This is easy to get wrong. `timesync_t` is built from the GPS date and time. If
those are replayed unchanged, a Fujifilm body receives a stale timestamp inside
the geotag payload (`lib/furble/Fujifilm.cpp:105-118`). Advance the cached UTC by
the elapsed milliseconds instead.

Convert the cached `timesync_t` into a `struct tm`, take it to epoch seconds with
`timegm()`, add `(tick() - cache.tick) / 1000`, and convert back with
`gmtime_r()`. ESP-IDF defaults to UTC, so `mktime()` also works, but do not rely
on that. Keep the centisecond field at the cached value, its precision is
meaningless after a hold.

### Tier 3 extrapolation

Only when `GPS_EXTRAP` is true and all of these hold:

- The cached course and speed were valid at capture time. TinyGPS++ provides
  `gps.course.deg()` and `gps.speed.mps()`, with `isValid()` and `age()` on both.
  The gkoh fork pinned at `platformio.ini:19` has `TinyGPSCourse::deg()` and
  `TinyGPSSpeed::knots/mph/mps/kmph`.
- Cached speed is at least 2 m/s. Below that the course reading from a consumer
  receiver is noise.
- The motion source does not report stationary. A stationary device must hold,
  not extrapolate.
- Elapsed hold time is under a hard cutoff of 30 s, independent of `GPS_HOLD`.

Great circle step, with `R = 6371000` m, `d = speed * elapsed`:

```
lat' = lat + (d * cos(course) / R) * 180 / pi
lon' = lon + (d * sin(course) / (R * cos(lat))) * 180 / pi
```

Straight line only. No turn rate, no gyro integration. The dominant error is
course error times distance. At 30 m/s for 30 s a 10 degree course error is
already 156 m of cross track error, which is why the cutoff is short and the
default is off. Label it experimental in the menu and in the PR body.

Altitude is not extrapolated. Hold the cached value.

### What the camera is told

No protocol change, and no faked accuracy.

The Fujifilm geotag payload has fields for latitude, longitude, altitude and a
timestamp only (`lib/furble/Fujifilm.cpp:105-118`). There is no accuracy or
satellite count field, so a Fujifilm body cannot be told the fix is held. The
only available control is time. Bound the hold and let the user choose the bound.

`gps_t::satellites` (`lib/furble/Camera.h:54`) is carried into the Nikon payload
(`lib/furble/NikonSmart.cpp:223`, `lib/furble/NikonSmart.h:112`). Do not zero it
to signal degradation. A camera may treat zero satellites as no fix and reject
the record, and that behaviour is untestable here because no Nikon body is
available. Replay the cached satellite count unchanged. Consider clamping it in a
later PR once someone can test on a Nikon.

The result is deliberate: the camera sees a normal fix. The user controls the
worst case error through the hold time. This is the same trade every phone makes
when it geotags from a cached location.

### Fujifilm on request path

`Fujifilm::updateGeoData()` (`lib/furble/Fujifilm.cpp:132-137`) only writes when
the body set `m_GeoRequested`. The request arrives on the camera's schedule, and
`Control::updateGPS()` is the only thing that refreshes the stored values. Under
hold, that path keeps working with no change: `GPS::update()` still runs at 1 Hz
and still queues `CMD_GPS_UPDATE`, it just sends cached data. This is exactly the
gap PR18 identified. Where PR18 needed an ad hoc cache to survive its own standby
windows, this PR provides that cache properly, so PR18 should use it instead of
inventing a second one. If both land, PR18's cache section is deleted in favour
of this one.

### UI indication

`src/FurbleGPS.cpp:189-191` currently picks between `icon_my_location` and
`icon_location_disabled`. Add the third state using `icon_location_searching`,
which is already declared in `components/icons/icons.h:31` and already used as
the GPS menu icon at `src/FurbleUI.cpp:1515`.

```
LIVE -> icon_my_location
HELD -> icon_location_searching
NONE -> icon_location_disabled
```

The GPS Data page (`src/FurbleUI.cpp:1548-1603`) already prints the fix age. Add
one line with the state and, when held, the remaining hold time. That is the page
a user checks when a photo has an unexpected position.

## Dependencies

- None hard. Tier 1 works on master plus the settings plumbing, which is why it
  is the first thing implemented.
- PR18 or PR20 for tier 2. Without a motion source, tier 2 is inert.
- PR15 raises the value of this PR a lot. GPS duty cycling makes short fix gaps
  normal rather than exceptional.
- PR14 is unrelated.
- If PR18 lands first, this PR removes its private fix cache.

## Risks

- Wrong position written to a photo. This is the core risk and it is inherent to
  the feature. Mitigation is the default of off, an explicit user chosen bound,
  and a visible icon state.
- Users do not notice the icon. The bound is the real protection, not the icon.
  Keep the default hold values short in the roller and do not offer an unbounded
  option.
- Timestamp handling. A held fix with a stale timestamp is worse than no fix,
  because EXIF then disagrees with the camera clock. Test this explicitly.
- Tier 3 in a curve or a stop. Extrapolating a vehicle that has turned or parked
  produces a confident wrong answer. Short cutoff, speed gate, stationary gate.
- Tier 2 with a false stationary reading. If the motion source is wrong, the hold
  clock never advances and a moving device replays an old position for a long
  time. The hard ceiling exists for this case.
- Interaction with PR15 and PR18. Once duty cycling is on, held becomes the
  common state rather than the exception. Make sure the hold clock is driven by
  real fix loss and not by an intentional standby window, otherwise the two
  features hide each other's failures. Log the reason for the hold.
- No accuracy channel to any supported camera. There is no way to mark a photo as
  approximately located. Say this plainly in the PR body.
- Only Fujifilm hardware is available. The Nikon satellites decision is reasoned,
  not tested.

## Verification

Build matrix:

```
pio run -e m5stick-c -e m5stick-c-plus -e m5stack-core -e m5stack-core2 -e m5stick-s3
```

Defaults regression: fresh NVS boot. `GPS_HOLD` 0, `GPS_EXTRAP` false. Behaviour
byte for byte identical to master. No cache used, icon has two states, nothing
sent without a live fix.

On device, M5StickS3 with GPS/BDS Unit v1.1 over USB:

1. `pio run -e m5stick-s3 -t upload`, then `pio device monitor`.
2. Acquire a fix outdoors. Confirm `LIVE` and the normal icon.
3. Set Fix Hold to 2 min. Simulate loss by cutting the 5 V rail with the existing
   GPS disable path, or by walking indoors. Confirm the state goes to `HELD`, the
   icon changes to the searching icon, and the logged position stays at the last
   fix.
4. Confirm sending stops exactly when the hold expires, and that the icon goes to
   disabled. Check the elapsed time against the setting.
5. Confirm the timestamp advances during hold. Log the `timesync_t` every 10 s
   and check it tracks wall clock, not the frozen fix time.
6. Restore GPS. Confirm the state returns to `LIVE` on the first fresh fix and
   the cache is refreshed.
7. Tier 2, with `IMU` and `GPS_MOTION` on. Put the device down, let it go
   stationary, then cut GPS. Confirm the hold does not expire while stationary
   and does expire after the hard ceiling.
8. Tier 2 exit. While held and stationary, pick the device up. Confirm the hold
   clock starts advancing immediately.
9. Tier 3, with `GPS_EXTRAP` on. Drive or cycle in a straight line at steady
   speed, cut GPS, and log the extrapolated track. Restore GPS and compare the
   extrapolated position with the first real fix. Record the error at 10 s, 20 s
   and 30 s. If the error is worse than plain hold, drop tier 3 from the PR.
10. Tier 3 rejection cases. Confirm no extrapolation below 2 m/s, none while
    stationary, and none past 30 s.

Camera checks, Fujifilm only, the only hardware available:

1. Connect a Fujifilm body, GPS on, Fix Hold 10 min. Get a fix, then cut GPS.
   Take a frame. Check the EXIF position on the card matches the held fix and the
   EXIF timestamp matches wall clock.
2. Let the hold expire, take another frame. Confirm no position is written and
   nothing else breaks.
3. Confirm the on request path still works while held. Watch for the
   `updateGeoData` log line at `src/FurbleControl.cpp:64`.
4. Repeat with Fix Hold off. Behaviour must match master.
5. Ten frame run alternating live and held, then compare all ten EXIF positions
   against a phone reference track. Report the largest error.

Battery impact: none expected. This PR adds no sensor polling and no extra BLE
traffic. State that in the PR body rather than running a drain test.

## Implementation state

Implemented:

- Added `GPS_HOLD` with key `gps_hold`, default `0`, and a five-position hold
  roller from off through 60 minutes.
- Added `GPS_EXTRAP` with key `gps_extrap`, default `false`, and an experimental
  switch that is disabled when fix hold is off.
- Added a complete fix cache with UTC timestamp advancement, cached satellites,
  cached course and speed, a separate `LIVE`, `HELD`, or `NONE` state, and the
  searching icon for held fixes.
- Added straight-line extrapolation only for valid course and speed data at or
  above 2 m/s. It stops before 30 seconds and uses the selected hold window.
- Added console and companion settings support. The provisional `DEAD_RECKON`
  wire id is 35 for `GPS_HOLD`. `GPS_EXTRAP` uses the next provisional id, 36.
- Updated the GPS Data page with the fix state and remaining hold time.

Deviations:

- This worktree does not contain the `GPS_MOTION` setting or a motion source
  from PR18 or PR20. Stationary-aware hold is therefore inactive. No stationary
  state is guessed, so tier 3 cannot extrapolate a claimed stationary fix.
- Companion-sourced live fixes keep the existing searching icon. This preserves
  the current companion UI while held wired fixes use the same icon.

Verification:

- clang-format 21 and `git diff --check` pass.
- `FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e m5stick-s3` was blocked before
  compilation because the sandbox denied the global PlatformIO lock. A retry
  with a worktree-local PlatformIO core reached dependency installation and
  failed with `HTTPClientError`. No firmware build result is available.

Hardware verification is pending, and no camera hardware test was performed.

## References

All links checked.

- TinyGPS++ documentation. Object model with `gps.course.deg()` and
  `gps.speed.kmph()`, and the validity, update status and age accessors:
  http://arduiniana.org/libraries/tinygpsplus/
- The gkoh TinyGPSPlus fork pinned in `platformio.ini:19`. `TinyGPSCourse::deg()`
  and `TinyGPSSpeed::knots/mph/mps/kmph` are present:
  https://github.com/gkoh/TinyGPSPlus
- M5Unified IMU class API, for the stationary input used by tier 2:
  https://docs.m5stack.com/en/arduino/m5unified/imu_class
- Bosch BMI270 sensor API. No-motion is the feature that backs tier 2 in PR20:
  https://github.com/boschsensortec/BMI270_SensorAPI
- ESP-IDF v5.4 sleep modes, background for the PR15 standby windows that make
  held the common state:
  https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/system/sleep_modes.html
- StickS3 product page: https://docs.m5stack.com/en/core/StickS3
