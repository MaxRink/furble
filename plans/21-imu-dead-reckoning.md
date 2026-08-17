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

- `GPS_HOLD`, key `gps_hold`, wire id 67, default `0`. A five-position roller:
  off, 30 s, 2 min, 10 min, 60 min. While a wired or companion fix is lost, the
  last one keeps reaching the camera for the selected window and then stops.
- `GPS_EXTRAP`, key `gps_extrap`, wire id 68, default `false`. While a fix is
  held, the position is projected along the last measured course and speed. The
  switch is greyed out until fix hold is set, because it has nothing to project
  without a held fix.
- Both ids come from the reservation table in issue #280, which this PR adds to
  `include/CLAUDE.md`. The golden corpus was regenerated for 67 and 68.
- Both ids also need a row in `SETTING_SCHEMAS` in
  `lib/furble/protocol/ProvisionTLV.cpp`, the dependency-free mirror of the
  settings table. Without one, `schemaForSetting()` returns nullptr and a
  provisioning bundle carrying the setting is rejected as
  `UNSUPPORTED_SETTING` before any domain rule runs. Nothing in the settings
  table knows the mirror exists, so `provision_apply_test`
  `testEverySettingHasASchemaRow` now walks every nonzero wire id and requires a
  row. Wire ids 43 (`AUTO_OFF_CHARGING`) and 46 (`IMU`) are already missing on
  master; they are named as a known gap in that test rather than registered
  here, because that changes the provisioning surface for settings this PR did
  not add. This was review finding 8.
- The hold and dead reckoning arithmetic lives in `include/FurbleGPSHold.h`,
  free of FreeRTOS, LVGL, NVS and the camera headers, so the host test pins the
  exact production behaviour. It follows the `include/FurbleGPSPowerCycle.h`
  precedent. `GPS::update()` is now the state machine and nothing else.
- A held fix advances its UTC. A camera stamps the photo with the time it is
  handed, so a repeated timestamp would date every later photo to the moment the
  fix was lost. newlib has no `timegm`, so the conversion is days-from-civil
  plus `gmtime_r`.
- The fix cache is anchored on when the receiver produced the fix, not on when
  `update()` noticed it: `now_tick - status.time_age` for the wired path and
  `now_tick - (external.age_ms + elapsed_ms)` for the companion. A fix is only
  declared stale after the whole freshness window, so anchoring on `now_tick`
  would silently drop up to 30 s from every held timestamp and from every
  projected distance. This was review finding 1.
- The longitude normalisation is a remainder, not a loop. Removing the pole
  guard while testing turned the old `while` loops into a hang rather than a
  wrong answer, because near a pole the longitude step is divided by a
  vanishing cosine. A single `fmod` is bounded and is one line shorter.
- Extrapolation requires a valid course and speed at 2 m/s or more, so a
  stationary user is never moved. Two clocks drive it and they are not the same.
  The distance is integrated from the fix time, because that is when the user
  was last actually at that position. The horizon is measured from when the fix
  was declared stale, because that is what "stop trusting the projection after
  30 s" means; measuring the horizon from the fix time instead would exhaust it
  during the freshness window and the feature would never project anything. Past
  the horizon the projection freezes where it reached rather than being
  abandoned, which keeps the geotag stream monotonic instead of jumping hundreds
  of metres back to the measured point.
- The hold window is measured from the moment the fix is declared stale, which
  is `MAX_AGE_MS` after the receiver's last good reading. A 30 s hold can
  therefore hand the camera a position up to a minute old. Starting the window
  at the fix time instead would make the shortest setting expire before it ever
  engaged, so it is documented in `docs/settings-and-controls.md` rather than
  changed. The timestamp sent alongside counts the whole elapsed time, so the
  camera is never told a held fix is fresher than it is.
- The GPS Data page gained one row: `fix: live`, `fix: held, Ns left`, or
  `fix: searching`. It is hidden while fix hold is off, so a default build
  renders the page exactly as master does.
- The status icon shows the searching glyph for a held fix, alongside the
  existing companion and degraded cases.
- `gps` on the console reports `fix_state`, `hold` and `hold_remaining`. The GPS
  Data page is not reachable from a bench script, so without these the hold
  state had no scriptable surface at all.

Fixed while harvesting, each a real defect in the branch as it arrived:

- The rebase left `src/FurbleConsole.cpp` with a duplicated `||` chain and
  `src/FurbleGPS.cpp` with two accessors pasted inside the body of
  `GPS::processSerial`. Neither compiled.
- `printValue` was missing `GPS_HOLD`, so `settings get gps_hold` printed
  `<unsupported type>`.
- `appliesWhen` was missing `GPS_EXTRAP`, so it claimed a reboot was needed when
  the console already queued a live reload.
- `reloadProvisionSetting` was missing both, so a provisioned value only took
  effect after a reboot.
- A held fix was being written into the GPX track. The camera geotag wants the
  last known position, but a track log is a record of where the user was, and an
  hour of frozen or projected points is indistinguishable from measured ones.
  Only a live fix is logged now. This also removes the `altitudeValid` question,
  because a held point was carrying a false altitude flag. The host `SD` shim counts the points the firmware
  queues, exposed as `gpx.points`, so the split is asserted rather than assumed.
  This was review finding 3.
- `clearExternalFix` left the cache intact, so tearing down the companion could
  keep replaying its last position for up to an hour. The cache is now dropped,
  through an atomic flag consumed by `update()`, because the cache belongs to
  the `update()` caller and `clearExternalFix` runs on the companion task.
- Six `-Werror=switch` sites had to gain both settings: `src/FurbleSD.cpp`
  (serialize and import), `src/FurbleProvision.cpp` (runtime type and range
  check), `src/FurbleSettings.cpp` (`appliesImmediately` and `isDangerous`), and
  the host doubles. This is the "five places" trap from
  `plans/95-engineering-lessons.md`, and there are more than five.

Simulator work, and one shared fix it needed:

- TinyGPSPlus ages every reading against a global `millis()`. Its non-Arduino
  fallback reads the host wall clock, so a scenario that advanced an hour of
  virtual time still saw a fix that had aged by the few seconds the run took,
  and nothing that depends on fix age could be tested or reproduced at all.
  `__AVR__` guards exactly one thing in `TinyGPS++.cpp`, that fallback, so the
  simulator now builds that one translation unit with it defined and supplies
  the virtual clock from `sim/clock.cpp`. This is what makes fix age, and
  therefore fix hold, testable. It should also make the `gps.png` capture
  reproducible, which `sim/CLAUDE.md` records as an exception; that claim is not
  made here because it was not measured.
- New seeds `gps_hold`, `gps_extrap` and `gps_stationary`. The last one selects
  a second canned NMEA track at 0.412 knots, which is below the motion floor.
- New query keys `ui.gps_fix_state` and `ui.gps_hold_remaining`, read back from
  the rendered row, and `camera.geo_count`, `camera.geo_lat_e5`,
  `camera.geo_lon_e5`, `camera.geo_utc_s`, which report the geotag that actually
  reached the simulated camera through the production path. Fix hold exists so
  those keep arriving after the fix is lost, so that is where the scenarios
  assert rather than inferring from the page.
- New `nav gps_hold` / `page gps_hold` route and `ui.page gps_hold` identity, so
  the roller page joins `page-matrix.txt` and `overflow-sweep.txt`.

Deviations:

- No `GPS_MOTION` setting or motion source exists yet, from PR #65 or PR #48.
  Stationary-aware hold is therefore inactive, and the reported speed is the
  only evidence of motion available. No stationary state is guessed.
- Companion-sourced live fixes keep the existing searching icon. A held fix uses
  the same glyph, so the icon does not distinguish the two.
- Requirement (c) asks for a byte-identical GPS Data page render against master.
  That is not achievable as a PNG comparison: the page renders the TinyGPSPlus
  fix age, and the capture was already documented as non-reproducible for that
  reason. The virtual `millis()` fix above may have closed it, but it was not
  measured. The equivalent used instead is a query-level comparison against a
  simulator built from `fork/master` 6245a301 with the same script, covering
  `ui.visible_objects` (10 on both), `ui.gps_speed`, `ui.gps_lat`, `ui.gps_lon`,
  `ui.gps_satellites`, `ui.gps_fix`, `ui.gps_source` and `ui.overflow`. The
  object count is the guard that catches the new row leaking into a default
  build.

## Simulator coverage

Certified scenarios, all in `sim/scenarios/manifest.json`:

| Scenario | Suite | Boards | Covers |
| --- | --- | --- | --- |
| `e2e/gps-hold-bound.txt` | e2e | s3 | (a) the bound, the advancing held UTC reaching the camera, the searching state |
| `e2e/gps-hold-extrapolate.txt` | e2e | s3 | (b) the projection and its horizon cut-off |
| `e2e/gps-hold-stationary.txt` | e2e | s3 | (b) a stationary track is never projected |
| `e2e/gps-hold-default-off.txt` | e2e | s3 | (c) defaults off leaves the page and the camera stream unchanged |
| `bughunt/gps-hold-rows-{small,normal,large}-{touch,buttons}.txt` | bughunt | all three | (d) both rows render on every panel, layout and text size |

Console `settings get` and `settings set` for both settings, and the three new
`gps` status lines, are covered by `tests/host/console_commands_test.cpp`, which
drives the real console against the real settings store. That is (e); the
console is not reachable from a simulator scenario.

Every assertion has a recorded killing mutation. Each was applied, the scenario
or test was run, and the named check failed:

Observed values below are bounds, not literals. Staleness is detected on an
update tick, so the measured freshness window is 30 to 31 s and every quantity
derived from it moves by one service period between runs. Each mutation is
recorded by the assertion that fires, which is a floor or a ceiling.

| Mutation | Fails |
| --- | --- |
| `gpsHoldInBound` always true, the bound removed | `gps-hold-bound.txt` reports `held` where it must report `searching`, and `gps_hold_test` `testBoundIsInclusive` |
| the held UTC frozen, `gpsAdvanceUtc` skipped | `gps-hold-bound.txt` at the `camera.geo_utc_s` floor of 45345, which reads the fix's own 45319 |
| the held fix computed but never sent | `gps-hold-bound.txt`, same floor, same value |
| the fix cache anchored on `now_tick` instead of the fix time | `gps-hold-bound.txt` and `gps-hold-extrapolate.txt`, both `camera.geo_utc_s` floors, and the first projected `camera.geo_lat_e5` floor |
| a wire id with no `SETTING_SCHEMAS` row | `provision_apply_test` `testDomainValidation` reports a missing schema row instead of the setting's own rule, and `testEverySettingHasASchemaRow` names the id |
| extrapolation never applied | `gps-hold-extrapolate.txt` at the first `camera.geo_lat_e5` floor of 4812050 |
| the horizon clamp removed from `gpsExtrapolateElapsedMs` | `gps-hold-extrapolate.txt` at the frozen `camera.geo_lat_e5` ceiling of 4812375, and `gps_hold_test` `testHorizonFreezes` |
| the two new schema rows removed from `SETTING_SCHEMAS` | `provision_apply_test`, four checks across the two tests |
| the 2 m/s motion floor removed | `gps-hold-stationary.txt` at `camera.geo_lat_e5 4811730`, and `gps_hold_test` `testStationaryIsNotMoved` |
| the `fix == Fix::LIVE` guard dropped, so held fixes reach GPX | `gps-hold-bound.txt` and `gps-hold-extrapolate.txt` at `gpx.points 1`, which climb into double figures |
| the Extrapolate switch never greyed out | `gps-hold-default-off.txt` at `ui.gps_extrap_enabled no` |
| the fix row rendered unconditionally | `gps-hold-default-off.txt` at `ui.gps_fix_state hidden` and `ui.visible_objects 10` |
| `GPS_HOLD` defaulted to anything but 0 | `gps-hold-default-off.txt` at `ui.gps_fix_state hidden` |
| either row given a fixed width instead of wrapping | the six `gps-hold-rows-*` files at `ui.overflow no` |
| the Fix Hold page dropped from the `ui.page` identity map | the six `gps-hold-rows-*` files at `ui.page gps_hold` |
| `gpsHoldRemainingMs` saturation removed | `gps_hold_test` `testRemainingSaturates` |
| `localtime_r` instead of `gmtime_r` | `gps_hold_test` `testUtcRollovers`, outside UTC |
| the days-from-civil leap year term dropped | `gps_hold_test` `testUtcRollovers` on 2024 and 2100 |
| `gpsAdvanceUtc` input validation dropped | `gps_hold_test` `testUtcRejectsGarbage` |
| sin and cos swapped in `gpsExtrapolate` | `gps_hold_test` `testCourseIntegration` |
| the pole guard dropped | `gps_hold_test` `testPoleIsRejected` |
| `printValue` left without `GPS_HOLD` | `console_commands_test`, `settings get gps_hold` prints `<unsupported type>` |
| the `gps` status hold lines removed | `console_commands_test` at `fix_state: held` |

Pre-existing findings, recorded rather than fixed here, both confirmed against a
simulator built from `fork/master` 6245a301:

- Floating navigation indicators overlap the GPS settings page on the 80x160
  M5StickC at every text size, on master too. The 320x240 M5Stack Core never
  overlaps.
- Every GPS roller page (`gps_rate`, `gps_assist`, and now `gps_hold`) overlaps
  the indicator on the 80x160 panel at every text size, and on the 135x240 panel
  at the Large text size. All three share `addGPSOptionMenu`.

Both are recorded as `xassert board-varies ui.indicator_clearance clear` in the
button-layout row scenarios, so the panels that are correct today are guarded
and the gap is on record. Promote them in the PR that fixes the shared roller
layout.

## Verification

- `tests/host`, all 95 tests pass.
- `tests/protocol` conformance passes with the regenerated corpus.
- `tools/check_sim_scenarios.py` reports a complete manifest.
- `sim/scripts/check-doc-tokens.sh` passes.
- The certified e2e suite on the M5StickS3, and the certified bughunt suite on
  all three panels, pass.
- The seeded UI fuzzer passes on one panel.
- clang-format 21 clean, no em-dashes, no sdkconfig changes.
- `FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e m5stick-s3-debug` builds in the
  OrbStack VM.

## Hardware gate

None of this has been run. It is the gate the feature actually turns on, and
steps 5, 6, 12, 13 and 14 are the ones that check the behaviours the simulator
can only model. M5StickS3 with the GPS/BDS Unit v1.1 and the X100VI.

1. Flash `m5stick-s3-debug`, attach the GPS/BDS Unit v1.1, open the USB console.
2. `gps on`, then `gps` until `fix_state: live` and `source: uart`. Confirm `hold: 0` and `hold_remaining: 0`.
3. `settings set gps_hold 2`, `settings get gps_hold` reads back 2, `gps` reports `hold: 120000`.
4. Connect the X100VI, confirm a geotag write in the log (`updateGeoData`), and take a frame outdoors. Note the EXIF position and time from the card.
5. Walk indoors or into a garage. Poll `gps` once a second and record the moment `fix_state` goes `live` to `held`. Measure it against the last NMEA sentence: this is finding 5, expect roughly 30 s of freshness before the hold clock even starts.
6. While held, log `timesync_t` every 10 s from the geotag path and compare with a phone clock. This is finding 1. Expect the held stamp to run consistently late by the age the fix had when the hold engaged.
7. Take a frame while held. Confirm the EXIF position is the last measured one and note how far the EXIF time is from the real time.
8. Watch `hold_remaining` count down and confirm it reaches `fix_state: none` within a second or so of 120 s from the transition in step 5, and that the status icon goes to the disabled glyph.
9. Take a frame after expiry. Confirm no new position is written and the camera link is otherwise unaffected.
10. Walk back outside. Confirm `fix_state` returns to `live` on the first fresh fix, the icon returns to `my_location`, and the next frame carries a live position and a correct time.
11. Repeat 4 to 10 with `settings set gps_hold 0`. Behaviour must be indistinguishable from master.
12. With `SD_GPX` on, do one held run and pull the `.gpx`. Confirm the track has no points across the held window and resumes on the first live fix. This is the behaviour finding 3 says nothing tests.
13. `settings set gps_hold 1`, `settings set gps_extrap on`. Cycle or drive a straight line at steady speed, cut the receiver, and log the projected track for 60 s. Record the error against the first real fix on restore at 10, 20 and 30 s, and confirm the position stops moving at the horizon. Plan 21 says to drop tier 3 if that error is worse than plain hold; that decision is still open.
14. Open the GPS settings page with `gps_hold` at 0 and confirm the `Extrapolate` switch is both greyed and genuinely not togglable on the button layout, then set Fix Hold and confirm it becomes live without a reboot. This is finding 7.
15. Battery: the body claims no impact. One 30 minute held run beside one 30 minute live run, comparing the runtime estimate, is enough to say that rather than reason it.

Every other vendor is code review plus FauxNY, as usual. The change is vendor
neutral: it feeds the same `Control::updateGPS` every vendor already consumes.

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
