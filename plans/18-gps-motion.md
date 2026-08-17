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
| `GPS_MOTION` | `gps_motion` (10) | `FURBLE_STR` | `bool` | `false` | False leaves current GPS behavior unchanged. Future PR15 policy integration remains unchanged. |

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

## Implementation state

Phase 1 only: the detector and its setting, with no receiver behaviour change.

Implemented:

- `GPS_MOTION`, NVS key `gps_motion`, wire id 66, default false. The id was
  claimed at the rebase onto fork/master 6245a301 per issue #280, and the five
  golden fixtures under `tests/protocol/golden/settings/` were regenerated.
  `include/CLAUDE.md` records the claim.
- `include/FurbleMotion.h` holds the detector as a pure header-only class, in
  the same shape as `include/FurbleAutoOff.h` and
  `include/FurbleGPSPowerCycle.h`. It keeps a 50 sample magnitude window at
  10 Hz, enters stationary after a continuous 60 s under a 0.02 squared-g
  variance threshold, and exits on the first sample past the threshold.
  `GPS::updateMotion()` is the thin adapter that reads the IMU and the
  monotonic clock and feeds it.
- `GPS::isStationary()` is the hook PR15 consumes. `GPS::isMotionEnabled()`
  reports whether the detector is running at all. Transitions are logged.
- `GPS::reloadMotionSetting()` is the entry point every GPS_MOTION write uses,
  from the UI switch, the console and the companion alike. `reloadSetting()`
  calls `enable()` unconditionally, which parks the GPS task, re-sets the UART
  baud, resets the parser, cycles the rail, re-enters ACQUIRING and marks the
  $PCAS configuration pending. Routing an advisory detector toggle through that
  cost a full re-acquisition, and on the v1.1 unit a rail cut costs a ~108 s
  cold start. `reloadSetting()` still calls the new method, because turning the
  receiver off has to take the detector with it.
- The 100 ms motion timer reports through `FURBLE_SIM_TIMER_FIRE` like the
  other thirteen LVGL timers and is registered in `sim/power_profiler.cpp`, so
  the power gate reports can see it. The committed baselines were not
  regenerated: `compare.py` gates on `estimated_mA` and tolerates the new key,
  and no power-gate scenario seeds `gps_motion`, so the timer reads zero in all
  nine of them.
- Settings > GPS carries a Motion Adaptive switch. It hides with the rest of
  the GPS rows when the receiver is off and renders disabled when the IMU is
  off.
- Console and companion plumbing, SD import and export, provisioning, and the
  NVS roundtrip case.

Deliberately not implemented, all of it phase 2 and PR15:

- No receiver power policy. No standby, no rail cycling, no rate change, no
  cached fix forwarding, no wake on a camera geodata request. GPS stays always
  on and `MAX_AGE_MS` behaviour is untouched.
- Nothing consumes `isStationary()`. The merge point for PR15 is the state
  transition in `GPS::updateMotion()`.
- No interaction with the PR19 deep-sleep intervalometer, because neither the
  policy nor PR19 exists yet.

Deviations from the plan above:

- The IMU gate on the menu row is evaluated when the row is constructed, not on
  every page display. The IMU capability is brought up during Platform init and
  the Sensors page says "Restart to apply", so it cannot change under a live
  page. Re-gating through `showIMUWidgets()` would be dead code on hardware.
- The plan asked for a 10 Hz poll shared with PR17. PR17 is not merged, so the
  detector owns its own 100 ms LVGL timer, created and destroyed by
  `GPS::syncMotionTimer()` as the setting changes.

Ceilings. These are properties of the method, not bugs, and any receiver
policy built on this detector has to survive all of them:

- **Constant acceleration and slow rotation are invisible.** The detector
  watches the variance of the acceleration magnitude, and neither changes it.
  A vehicle cruising at a steady speed reads stationary, and so does a device
  turning slowly on a tripod head. This is the important one: it is exactly the
  case where a stale fix is most wrong, and it is why plan 15 must keep a
  bounded cached-fix lifetime rather than trusting `isStationary()`.
- **"Moving within one poll" has a floor.** The immediate exit fires on a
  sample more than 0.141 g off the running mean, which is the square root of
  the 0.02 threshold. A gentler start is not reported until the window variance
  crosses, which can take up to the full 5 s window. The plan text above says
  moving is reported within one poll period; that is true only above that
  deviation.
- **Low-amplitude vibration reads as stationary.** A sustained 0.2 g peak to
  peak shake settles to a squared deviation of 0.01, half the threshold.
  `testVibrationUnderTheThresholdReadsStationary` pins it, so moving the
  threshold is a deliberate change with a measurement behind it. The risk list
  above expects wind on a tripod to keep the detector moving; that holds only
  above roughly 0.3 g peak to peak.
- **No number here has met a sensor.** The threshold, the window and the dwell
  were all derived on paper. They are a starting point for the hardware gate
  below, not a result.

## Test coverage

Host, `tests/host/motion_detector_test.cpp` (ctest name `motion-detector`), ten
cases over the pure detector: the full dwell, the partial-window guard, the
immediate exit, a nudge under the threshold, re-entry paying the dwell again,
sustained vibration, `reset()`, non-finite samples, the magnitude helper, and a
`now_ms` of exactly zero.

Console, `tests/host/console_commands_test.cpp`: `settings set gps_motion
on|off`, the readback, the real NVS store, the `applies: immediately` line, and
a rejected non-boolean value. It also carries the console half of the receiver
guard: writing `gps_motion` must bump `reloadMotionSettingCalls`, must leave
`reloadSettingCalls` alone, and must queue no `GPS_RELOAD` UI request, while
writing `gps_baud` must still queue one. The simulator has no USB console, so
this is where the console verb is covered.

The companion path calls the same `reloadMotionSetting()` entry point.
`companion_gatt_test` links a GPS double whose methods are both no-ops, so it
cannot tell the two apart; that path rests on the shared entry point and code
review, not on its own assertion.

Simulator, all certified on the 80x160 M5StickC, the 135x240 M5StickS3 and the
320x240 M5Stack Core:

| Scenario | What it pins |
|---|---|
| `e2e/gps-motion-detector.txt` | Slow entry and immediate exit, and the phase 1 contract: `gps.state`, `gps.degraded`, `uart.count` and `power.no_light_sleep` all read the same on both sides of a transition. |
| `e2e/gps-motion-setting.txt` | Default off, the real Settings > GPS switch turning the detector on and off, and the receiver still tracking 50 ms after each flip. The 50 ms is the point: the old code re-acquired and was back at tracking within a second, so a settled assertion could not see it. |
| `bughunt/gps-motion-row.txt` | The row at Normal text size: renders, both scroll extents reachable, no indicator overlap, and it follows the GPS visibility gate. |
| `bughunt/gps-motion-row-small.txt` | The same at Small. |
| `bughunt/gps-motion-row-large.txt` | The same at Large. |
| `bughunt/gps-motion-row-gates.txt` | IMU off: the row renders disabled and the detector reports off even with the setting on. |
| `bughunt/stick-notouch-layout-135.txt`, `bughunt/stick-notouch-layout-80.txt`, `bughunt/core-notouch-layout.txt` | The row on the physical-button layout each board actually ships, with the indicator clearance assertions those files own. |

New simulator seams: seed `gps_motion`, `action toggle gps_motion`, queries
`gps.motion_state` and `ui.gps_motion_row`, and `setting.gps_motion`. All are
documented in `docs/sim.md`.

`sim/scripts/ui-screenshots.txt` gained one `key down`: the new row sits above
GPS Data and that screenshot route counts focus steps.

Two things the simulator does not cover, on purpose:

- The 80x160 physical-button GPS list already reports one widget under an
  indicator at scroll top. That is a pre-existing gap on the longest settings
  list, not this row, which is the eighth entry and sits below the fold. It is
  recorded as a `xassert` WILL_FAIL in `stick-notouch-layout-80.txt`, matching
  every other WILL_FAIL in that file.
- `power.no_light_sleep_acquires` is board dependent, so the phase 1 contract
  asserts the held lock count rather than the acquire tally.

## Mutation testing

Every assertion above was checked against a mutation that must kill it. Each
mutation was applied on its own, the affected suite run, and the mutation
reverted.

| # | Mutation | Killed by |
|---|---|---|
| M1 | `Detector::sample()` never sets `m_Stationary = true` | host: the dwell, `isStationary()` agreement, and every case that starts from a stationary state |
| M2 | The immediate-exit term is disabled (`outlier` threshold raised to 1e9) | host: one sample past the threshold exits, the exit is visible, moving after the spike, and the re-entry dwell |
| M3 | `VARIANCE_THRESHOLD` 0.02 raised to 2.0 | host: the immediate exit and the re-entry dwell |
| M4 | `STATIONARY_MS` 60000 lowered to 1000 | host: half the dwell must not re-enter stationary |
| M5 | The partial-window guard is removed | host: a partial window must never enter stationary, and the one-period-short case |
| M6 | `updateMotion()` takes the GPS power lock on a transition | sim `e2e/gps-motion-detector.txt`: `power.no_light_sleep` |
| M7 | The IMU terms are dropped from the `reloadSetting()` gate | sim `bughunt/gps-motion-row-gates.txt`: `gps.motion_state` |
| M8 | The row is not pushed into `gpsWidgets` | sim `bughunt/gps-motion-row.txt`: `ui.gps_motion_row` |
| M9 | The row is never given `LV_STATE_DISABLED` | sim `bughunt/gps-motion-row-gates.txt`: `ui.gps_motion_row` |
| M10 | The switch callback skips the reload entirely | sim `e2e/gps-motion-setting.txt`: `gps.motion_state` |
| M11 | The UI switch calls `reloadSetting()` instead of `reloadMotionSetting()`, which is the bug the review found | sim `e2e/gps-motion-setting.txt`: `gps.state` expected `tracking`, got `acquiring` |
| M12 | The console puts GPS_MOTION back in the `GPS_RELOAD` group | host `console_commands_test`: the receiver restart and the queued UI request, 674 of 676 checks |

Twelve of twelve mutations were killed. M2 first reported as surviving because the
unused-variable build failure left the previous binary in place; rerun with the
threshold raised instead of the term deleted, it is killed by four cases. A
mutation harness that hides its build failures reports false survivors, so the
build result is checked before the suite result.


## Validation

- Host suite: 95 of 95 ctest cases pass, including the new `motion-detector`.
- Protocol conformance passes after regenerating the wire id 66 fixtures. The
  five wire id 48 files were deleted; `protocol_test` fails on a stale golden
  binary, so leaving them behind would not have been silent.
- Python suite: 144 tests, all pass.
- `python3 tools/check_sim_scenarios.py` reports the manifest complete, and
  `sim/scripts/check-doc-tokens.sh` passes.
- Simulator: 165 certified scenario runs across the 80x160, 135x240 and
  320x240 panel classes, all pass.
- Fuzzer: the pinned eight-seed set plus the determinism replay pass on the
  135x240 panel, zero findings.
- clang-format 21 clean. No sdkconfig changes. No em-dashes. Two-space indent.
- Firmware: `m5stick-s3-debug` builds in the OrbStack VM.

## UI evidence

`plans/18-evidence/<panel>/<before|after>/` holds Settings > GPS captured from
fork/master and from this branch on all three panel classes, at the top of the
list and scrolled to the bottom where the new row sits.

The 320x240 capture is the one that reads: the row draws as `Motion Adaptive`
with its switch. On the two Stick panels the label does not fit beside a switch
and LVGL marquees it, so a capture catches it mid-scroll. That is not new
behaviour from this row: the existing `GPS baud 115200` switch row two entries
above marquees the same way on the same panels, visible in the same captures.
A shorter name was tried and rejected: the label box next to a switch is about
five characters wide at 135x240, so even `Motion` marquees, and the longer name
is the one that reads on the panel where the whole label fits.


## Hardware gate

Nothing in this PR has run on a device, and this is the merge gate. On the
M5StickS3 with its BMI270 and the GPS/BDS Unit v1.1:

1. With `IMU` off, confirm Settings > GPS shows Motion Adaptive greyed out and
   that `settings get gps_motion` still reads the stored value back.
2. Turn `IMU` on, restart, turn `gps_motion` on. Confirm the console reports
   `applies: immediately`.
3. **With a fix held, toggle `gps_motion` on and off and watch the GPS Data
   page.** The satellite count, the sentence age and the fix must not reset.
   This is the regression the review caught in the simulator; on hardware it
   would cost a rail cycle and a ~108 s cold refix on a unit with no backup
   supply. Run it from all three surfaces: the UI switch, `settings set
   gps_motion`, and the companion app.
4. Set the device down. The log should report `GPS motion: stationary` about
   65 seconds later: 5 s to fill the window, then the 60 s dwell.
5. Pick it up sharply. `GPS motion: moving` should appear within one 100 ms
   poll. Then repeat with a slow, gentle lift: that is expected to take longer,
   up to the 5 s window, and the point of the step is to find out how much
   longer on a real BMI270.
6. Blind spots, both of which need a car or a bike. Carry the device at a
   steady speed and confirm it reports stationary, because it will, and PR15
   has to be built knowing that. Then rotate it slowly on a tripod head and
   confirm the same. Record both, because they bound what a receiver policy is
   allowed to assume.

The battery comparison runs from the plan above are phase 2 work. Phase 1 saves
nothing by design, so there is nothing to measure yet.

## Sequencing

PR #48 lands before this one. After that, this branch rebases onto #48 and the
detector consumes `IMU::MotionSource` instead of polling `M5.Imu` itself, so
the device ends up with one IMU poller and one detector rather than two timers
reading the same sensor. Until #48 is ready this branch stays on master, and
the `imuSensor*` helpers in `src/FurbleGPS.cpp` are the seam that gets replaced
by that consumption.

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
