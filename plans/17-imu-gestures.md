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

- On a gesture matching `IMU_WAKE`, call the display sleep/wake state machine
  already integrated from PR12,
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

- Repaired PR28 (display off). The display sleep/wake state machine is present
  in this branch; gesture wake must use its shared power-lock and panel path.
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

Implemented and rebased onto `fork/master` 6245a301.

- `IMU_WAKE` (`imu_wake`, wire id 72, `uint8_t`, default 0) and `IMU_TRIG`
  (`imu_trigger`, wire id 73, `bool`, default false). Both defaults reproduce
  master exactly.
- `Furble::GestureDetector` in `src/FurbleUIGesture.cpp`: software tap, double
  tap and shake on the accelerometer stream, with a per-sensor gain and a
  console calibration scale.
- `Settings > Sensors > Gestures`: the Wake Gesture roller, the Double-Tap
  Shutter switch and the false-trigger warning on one page. Every control, and
  the Gestures entry itself, is disabled when the effective IMU is off, and the
  poll timer stops in that state.
- Wake through the shared `wakeDisplay()` path, inactivity reset, refractory
  period, the connected/remote page and intervalometer guards, and the short
  press/release pair.
- Console (`settings get/set`, plus `imu scale`), companion, provisioning and
  SD import/export.
- Simulator: eleven certified e2e scenarios, the Gestures and Sensors routes in the
  bughunt page matrix, overflow sweep and text-size sweeps, and a power baseline
  with the detector on.

## Behaviour as implemented

### Detector, `src/FurbleUIGesture.cpp`

`GestureDetector` is a pure state machine over one accelerometer sample plus a
millisecond timestamp. `poll()` is the only hardware-facing method: it checks
`M5.Imu.isEnabled()`, calls `update()` and `getAccel()`, reads the per-sensor
gain, converts `esp_timer_get_time()` to milliseconds and delegates to
`sample()`. Under `FURBLE_SIM` the same three calls go to the shared `M5.Imu`
seam the spirit level already uses.

`sample(x, y, z, now, doubleTap, gesture)` is the deterministic seam. Host tests
and the simulator both drive it, so no IMU is needed for coverage.

- Non-finite components, a non-finite magnitude, or a magnitude above 16 G are
  discarded before any state is touched.
- The first accepted sample only seeds the baseline and returns false.
- `deviation = fabs(magnitude - baseline)`.
- Shake: an EWMA (alpha 0.5) of the deviation, plus a run counter that
  increments while the deviation exceeds 0.6 G and saturates at 3. SHAKE fires
  when the EWMA is above the bar, the run counter is at 3, and the latch is
  clear. The latch clears only on a quiet sample, so a sustained shake reports
  once.
- Tap: a candidate opens at 1.5 G, is abandoned after 120 ms without a release,
  and releases at 0.5 G. With double-tap mode off a release reports TAP. With it
  on, a release 80 to 400 ms after a pending tap reports DOUBLE_TAP; otherwise it
  becomes the pending tap.
- A pending tap older than 400 ms retires as a plain TAP, so a single tap still
  wakes when Double-Tap Shutter is enabled. It just arrives up to 400 ms late.
- Refractory: 750 ms after any reported gesture, blocking new candidates, shake
  reports and pending-tap retirement.
- Baseline tracking: `0.95 * baseline + 0.05 * magnitude`, only while there is
  no tap candidate, no pending tap, and the signal is quiet.
- All time comparisons use unsigned subtraction and are wrap safe.

At the 50 Hz poll rate a tap is one sample. Two consecutive high samples is a
shove and three is a shake, so an impulse longer than about 40 ms is classified
as a shake by design. The simulator scenarios encode that boundary.

### Per-sensor gain and the calibration knob

The three amplitude thresholds are multiplied by `m_TypeGain * s_Scale`.

`m_TypeGain` comes from `M5.Imu.getType()`: 1.25 for the MPU6886 boards, 1.0 for
the BMI270. The MPU6886 reads noisier at the same physical impulse, so it needs
a higher bar for the same false-positive rate. This is a starting estimate; the
hardware gate below is where the real number comes from.

`s_Scale` is the runtime calibration knob, default 1.0, clamped to 0.25 through
4.0. It is set over the console with `imu scale <value>` and read back with
`imu scale`. A real sensor in a real case never matches the paper thresholds:
the case damps the impulse, a strap adds mass, and the two supported sensors
disagree. The knob is deliberately runtime only and not a persisted setting: a
tuning session is one USB session, and it costs no wire id. If it turns out that
a board needs a permanent offset, the honest fix is a measured change to the
per-type table rather than asking every user to persist a magic number.

### Timers and task ownership

- `m_GestureTimer` is an `lv_timer` at `GESTURE_POLL_MS` (20 ms, 50 Hz), created
  only when the effective IMU is on and at least one gesture feature is enabled,
  and deleted as soon as both are off. It runs inside `lv_task_handler()` on the
  UI task, under `UI::m_Mutex`.
- `pollGesture()` re-reads the effective IMU state every tick and deletes its own
  timer if the sensor has gone away, so a failed sensor cannot burn the timer.
- Cross-task setting changes never touch LVGL. `notifyGestureSettingsChanged()`
  bumps an atomic generation; `UI::task()` reconciles it once per 5 ms iteration
  inside the mutex. The console, the companion GATT service and the UI controls
  are all writers. Headless builds get an inline no-op.
- `m_GestureShutterTimer` is a one-shot 30 ms timer that sends the release and
  deletes itself. While it exists a further gesture is ignored, so one gesture
  can never produce two frames.

### Wake

TAP and DOUBLE_TAP map to wake modes 1 and 3, SHAKE to 2 and 3.
`displayIsInactive()` is sampled before waking. If a wake gesture arrives while
the display is inactive, `handleGesture()` returns immediately: the waking
gesture is swallowed and cannot also fire the shutter.

`wakeDisplayFromGesture()` calls the shared `wakeDisplay()` rather than
`M5.Display.wakeup()`, restores `BRIGHTNESS` and sets `DisplayState::ACTIVE` if
the display was OFF or DIM, then triggers LVGL activity so `processInactivity()`
does not immediately dim again. This is the same panel, APB lock and PMIC path a
button wake takes.

### Shutter guards

`canTriggerGesture()` requires the control state to be `STATE_ACTIVE`, the
shutter lock clear, the current page to be Connected or Remote shutter, and the
intervalometer to be IDLE or FINISHED. The two missing
`m_IntervalometerState.store()` calls that made that last check honest are part
of this change.

### Power cost: a model ceiling, not a measurement

The gesture path takes no power lock. It calls neither `Power::acquire` nor
`Power::release`, so it does not itself hold `APB_FREQ_MAX`. The wake path takes
the display lock through `wakeDisplay()`, exactly as a button wake does. No
mutex is held across a delay: the shutter send is a non-blocking queue send from
the UI task and the 30 ms hold is a timer, not a sleep.

`sim/scenarios/gesture-idle-30s.txt` runs both features on with the display off,
checked against its own baseline in `tools/power-model/baseline/`. On the
M5StickS3 with the display off the model reports:

| Poll rate | Modelled standing current | Light-sleep residency |
| --- | --- | --- |
| Off (master default) | 0.311 mA | 100.0 % |
| 10 Hz (100 ms) | 1.642 mA | 96.6 % |
| 25 Hz (40 ms) | 5.637 mA | 86.6 % |
| 50 Hz (20 ms) | 9.632 mA | 76.6 % |

**These are a ceiling with roughly an order of magnitude of headroom, not an
estimate of the work the timer does.** `sim/power_profiler.cpp` marks a UI cycle
awake if any registered timer fired in it, then bills the whole interval at
`mcu_80` = 40.2 mA. The report shows 1500 fires in 30 s charged as 7000 ms
awake: 4.67 ms per fire, which is the UI task's 5 ms loop quantum, not the cost
of one I2C read plus a few dozen float operations. The 0.311 mA baseline is
artificial in the other direction, because `UI::task()` already runs
`lv_task_handler()` every 5 ms on master and the model still calls that full
light-sleep residency. Enabling the IMU in the scenario changes nothing either:
`model.peripheral` in the profiler is a hardcoded 0.0035 and never reads the
`peripherals` section of `board-currents.yaml`, so the 0.685 mA `bmi270_normal`
figure a 50 Hz read actually needs is charged nowhere. Both limitations are
written down in `tools/power-model/README.md` and tracked in issue #285.

So the table above says the poll is not free and bounds how bad it can be. It
does not say how bad it is.

Decision on the 25 Hz display-off fallback the scope section proposed: **not
implemented, and deliberately not decided on this table.** The model cannot
separate a 20 ms poll from a 40 ms one by anything except how often the 5 ms
quantum is charged, so choosing a rate from it would be choosing from an
artefact. What settles the rate is step 8 of the hardware gate below, the hour
drain run. Under about 2 mA of measured delta and both the "the cost is large"
framing and the rejection of 25 Hz have to be rewritten; near 9 mA and the INT1
follow-up becomes urgent. The shipped position until that run is the
conservative one: 50 Hz, off by default, and the real fix for wake-while-off is
the BMI270 any-motion interrupt on INT1, which stays scoped out.

Note for whoever reads the power gate: `compare.py` only fails on increases, so
this baseline guards against the rate going up and cannot catch it going down.
A rate reduction prints `-41.47% PASS`.

One follow-up is worth recording rather than doing here. When only Double-Tap
Shutter is enabled, the detector does not need to run while the display is off
at all, because `handleGesture()` already refuses to fire the shutter in that
state. Stopping the timer for that configuration needs a hook in `wakeDisplay()`
and `processInactivity()`, which are hot shared paths, and it does not help the
headline wake case. It belongs in the same PR as the interrupt work.

### Thresholds are hand-written and unvalidated against a real sensor

This is the largest open risk in the change and it is not visible from the test
results.

Every threshold, every host fixture and every scenario waveform in this branch
is a hand-written step function. No accelerometer trace from real hardware
exists anywhere in the tree, so the numbers 1.5 g, 0.6 g, 0.5 g, 120 ms, 80 to
400 ms and 750 ms are reasoned, not fitted, and the MPU6886 gain of 1.25 is an
educated guess.

The shipped shape rests on one physical claim, stated in `docs/sim.md` and
above: at the 50 Hz poll rate a tap is one sample. `M5.Imu.getAccel()` returns
the latest register value with no peak hold, and a finger tap on a cased Stick
is a few milliseconds, so a 20 ms sampling window can alias the impulse away and
drop taps at random. The scenarios cannot see this, because they hold the high
value for a full sample period by construction. If taps prove unreliable at
every `imu scale`, the answer is a higher poll rate or a hardware peak hold, not
a different constant.

The hardware gate below therefore starts with trace capture, not with pass/fail
checks. Those traces must be committed as simulator fixtures and the tap path
re-derived against them before any of these constants can be called tuned.

## Simulator coverage and killing mutations

Every assertion below is paired with the mutation that kills it. The mutation
column is the source change that makes the assertion fail. A representative
mutation from each family was applied, confirmed to fail, and reverted; the
confirmed set is listed after the table.

New query seams, all `FURBLE_SIM` only: `ui.gesture_timer`,
`ui.gesture_period_ms`, `ui.gesture_events`, `ui.gesture_last`,
`ui.gesture_shutter_sends` and `ui.display_state`. `ui.display_state` reports
the value production already feeds `profilerSetDisplayState()`, so it adds no
policy. `ui.gesture_shutter_sends` counts the shutter decisions the gesture path
made: the camera counters cannot see a command that was blocked before it was
sent, and an intervalometer run dominates them. Gesture injection uses the
existing `imu.accel`, `imu.enable` and `imu.disable` actions.

| Scenario | What it pins | Killing mutation | Verified |
| --- | --- | --- | --- |
| `imu-gesture-detect.txt` | a 20 ms impulse is a tap | `TAP_THRESHOLD` 1.5 to 3.0 | killed |
| | one knock is one event | `REFRACTORY_MS` 750 to 0 | killed |
| | a sustained 1.8 g hold is one shake | drop `m_ShakeReported` | recorded |
| | walking at 0.3 g reports nothing | `SHAKE_THRESHOLD` 0.6 to 0.3 | killed |
| | a 0.9 g table bump reports nothing | `TAP_THRESHOLD` 1.5 to 0.8 | recorded |
| | the 50 Hz timer exists and is 20 ms | `GESTURE_POLL_MS` 20 to 40 | killed |
| `imu-gesture-doubletap.txt` | two taps 120 ms apart are one double tap | `DOUBLE_TAP_MAX_MS` 400 to 100 | killed |
| | a 40 ms gap is not a double tap | `DOUBLE_TAP_MIN_MS` 80 to 0 | killed |
| | a 600 ms gap is not a double tap | `DOUBLE_TAP_MAX_MS` 400 to 1000 | recorded |
| `imu-gesture-wake-tap.txt` | a tap wakes in mode 1, a shake does not | widen the TAP mask to any mode | killed |
| | the wake resets the inactivity counter | drop `lv_display_trigger_activity()` | killed |
| `imu-gesture-wake-shake.txt` | a shake wakes in mode 2, a tap does not | widen the TAP mask to any mode | killed |
| `imu-gesture-wake-off.txt` | nothing wakes in mode 0, and the detector still runs | wake unconditionally in `handleGesture()` | killed |
| `imu-gesture-shutter.txt` | one double tap is exactly one frame | `REFRACTORY_MS` 750 to 0 | killed |
| | the Remote shutter page also triggers | drop the page check | killed |
| | a settings page does not | drop the page check | killed |
| `imu-gesture-shutter-blocked.txt` | disconnected takes no frame, on the shutter page after a drop | drop the `STATE_ACTIVE` check | killed |
| `imu-gesture-interval.txt` | a running intervalometer gains no frame, from the shutter page | `case STATE_WAIT/SHUTTER_OPEN/DELAY` return to break | killed |
| `imu-gesture-defaults.txt` | no timer, no events, no frames at defaults | create the timer unconditionally | killed |
| | no extra redraw work at defaults | create the timer unconditionally | killed |
| `imu-gesture-disabled.txt` | the IMU setting gate beats stored gesture settings | make `imuEnabledForUI()` ignore the setting | killed |
| `imu-gesture-gating.txt` | Sensors fits, Gestures is fully reachable | the pre-fix Sensors layout | killed |
| | losing the sensor disables every control and stops the timer | drop the live check in `pollGesture()` | killed |
| `bughunt/page-matrix.txt` | Sensors fits and Gestures scrolls cleanly on all three panels | the pre-fix Sensors layout | killed |
| `bughunt/stick-notouch-layout-135.txt` | Sensors fits the non-touch layout too | the pre-fix Sensors layout | killed |
| `gesture-idle-30s.txt` | the poll rate is a tracked power number | none: `compare.py` only fails on increases, so a rate reduction prints `-41.47% PASS`. `GESTURE_POLL_MS` 20 to 40 is killed by `assert ui.gesture_period_ms 20` in `imu-gesture-detect.txt`, not by this gate | n/a |

Sixteen mutations were built and run; all sixteen fail the named scenario. Four
of them survived on the first pass and each survivor was a real weakness in the
test, not in the product:

- The one-shot `m_GestureShutterTimer` check could not be killed at all. The
  detector's 750 ms refractory period already guarantees one gesture is one
  frame, so nothing can reach that branch. It stays as a second line with a
  comment saying so, and the refractory carries the recorded mutation instead.
- The disconnected and intervalometer cases were asserted with the camera
  counters, which cannot see a blocked command. They now assert
  `ui.gesture_shutter_sends`, and the counter records the decision rather than
  the queue result, because a send on a dropped link fails on its own and would
  mask a missing guard.
- The disconnected and intervalometer cases were also masked by the page check,
  which blocked the gesture before the guard under test could. Both scenarios now
  sit on the Remote shutter page, so each guard is reached in isolation.
- The IMU setting term could not be killed because `imuEnabledForUI()` already
  ANDs the setting with live sensor presence: the call site's extra
  `&& Settings::load<Settings::IMU>()` was a redundant double-check in five
  places. It is deleted, and the mutation now targets the single real gate.

Three rows are marked `recorded` rather than `killed`: they are the second
mutation for a constant whose other direction was already built and killed, so
building them adds nothing.

Layout note. The first implementation put the roller entry, the switch and the
warning paragraph directly on Sensors. That overflowed by 9 px in the touch
layout and 37 px in the non-touch 135x240 layout, and the branch's own scenario
asserted the overflow rather than fixing it. Sensors now gains exactly one row,
the `Gestures` entry, and the `Restart to apply` caption was folded into the
button it describes, which said the same thing in a second row. Gestures itself
holds a roller and is an intentional-scroll page, asserted with the
scroll-extent contract the other multi-control settings pages use.

Measured fit after the fix, at Normal text with both gesture settings on:

| Panel | Touch | Non-touch |
| --- | --- | --- |
| 80x160 M5StickC | fits | 7 px over |
| 135x240 M5StickS3 | fits | fits |
| 320x240 M5Stack Core | fits | fits |

The one remaining case is a pre-existing condition, not a regression.
`bughunt/stick-notouch-layout-80.txt` already carried
`xassert ui.overflow no` on Sensors with the comment "overflows by 10 px on this
panel" before this branch. Folding the caption took it from 10 px to 7 px, so
the page got closer to fitting while gaining a row. The remaining gap is switch
and button row padding at that font size, which belongs to whoever promotes that
`xassert`; the comment now records the new measurement.

## Host tests

`tests/host/gesture_detector_test.cpp`, registered as the `gesture-detector`
ctest. The branch built the binary and never ran it; that is fixed.

Covered: stationary quiet, the exact tap and shake threshold boundaries,
immediate tap, double tap, lone-tap retirement, shake latching, uint32 wrap,
non-finite and implausible input, reset, a 2 Hz 0.3 g walking waveform over five
seconds, a 1.0 to 1.1 g calibration ramp that the baseline must follow, the
console scale in both directions plus its clamping of zero, NaN and 100, and the
`poll()` hardware path including a disabled sensor.

### Provisioning and the wire contract

Three guards outside the detector, each verified by deleting it rather than by
reading the code.

- The `SETTING_SCHEMAS` rows in `lib/furble/protocol/ProvisionTLV.cpp`. A wire
  id with no row is rejected at `UNSUPPORTED_SETTING` before any validation
  runs, so a setting can be fully wired everywhere else and still be
  unprovisionable. `{72, U8, 1, 1}` and `{73, BOOL, 1, 1}` are present.
  Deleting row 72 fails thirteen assertions and deleting row 73 fails eleven,
  across three tests. The first to fire in both cases is
  `provision_apply_test`'s "valid settings apply successfully", because
  `apply()` consults the schema. Row 72 additionally fails that test's
  "setting 72 reports BAD_SETTING, not a missing schema row" and "setting 72
  names its own rule", which are the sharper guards: they distinguish a domain
  rejection from a setting the bundle can never carry at all. Both rows fail
  #47's `testEverySettingHasASchemaRow` at "wire id N has a SETTING_SCHEMAS
  row", and `console_commands_test` at "the gesture blob encodes", because
  `encode()` consults the schema too. `{46, BOOL, 1, 1}` is the same row for
  the shipped `IMU` setting, which this branch exposes to provisioning for the
  first time, and which is why 46 is no longer listed in that test's
  `KNOWN_MISSING` gap list.
- The runtime hook. `reloadProvisionSetting()` is exercised end to end by the
  console suite's `provision <hex>` case; deleting its IMU cases fails "a
  provisioned gesture setting notifies the UI task".
- The frozen ids. `tests/protocol/protocol_test.cpp` pins `IMU` to 46,
  `IMU_WAKE` to 72 and `IMU_TRIG` to 73 with their wire types. Renumbering a
  shipped id and regenerating its fixtures yields a self-consistent corpus that
  silently breaks deployed clients, which is exactly what this branch did before
  the rebase. A clean renumber plus regenerate now fails with "IMU wire id
  moved", and dropping `case Settings::IMU:` from
  `CompanionService::settingType` fails with "no wire type for setting IMU".

## Hardware gate, executable

Hardware gate (eight steps) owed post-merge; shipping with both settings
off changes nothing.

Reproduced verbatim from the PR #45 review on `dce53bf5`.

M5StickS3 with the X100VI. Steps 1 and 8 are the two that can still change the shipped design.

```
0  FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e m5stick-s3-debug -t upload
   pio device monitor -e m5stick-s3-debug   (log to a file for every step)
   settings set imu on
   restart
   imu status        expect bmi270 and live reads
   imu scale         expect 1.00

1  Trace capture, the missing fixture data.
   settings set imu_wake 3
   settings set imu_trigger off
   Tap once, wait 2 s. Double tap, wait 2 s. Shake 2 s, wait 2 s.
   Repeat the whole sequence at imu scale 0.5, 1.0 and 2.0.
   Deliverable: every "IMU gesture:" line with its timestamp, and the scale
   that gives one event per intended gesture and nothing else. If no scale
   makes single taps reliable, that is the 20 ms aliasing prediction in
   finding 3 and the poll rate has to change before merge.

2  Single events.
   For imu_wake 1, 2 and 3: ten repetitions of the matching gesture,
   expect exactly ten lines, one class each, no bursts.
   settings set imu_wake 0, then shake: expect nothing logged.

3  Wake from display off, including after a long sleep.
   settings set inactivity 1
   settings set display_off 1
   settings set imu_wake 2
   Wait for the panel to go dark, shake once. The panel must return at the
   configured brightness and stay on for the full inactivity window rather
   than dimming immediately. Repeat with imu_wake 1 and a tap.
   Then leave it idle five minutes and repeat once. A missed first gesture
   after a long light-sleep window is the report that matters here.

4  Shutter, X100VI connected.
   settings set imu_trigger on ; settings set imu_wake 0
   20 double taps on Connected      expect 20 frames, 20 double_tap lines
   20 single taps on Connected      expect 0 frames
   10 double taps on Remote         expect 10 frames
   10 double taps on Settings>Sensors  expect 0 frames
   5 frame intervalometer, 2 double taps during the run  expect 5 frames
   disconnect, 10 double taps       expect 0 frames, 10 gesture lines

5  False triggers.
   imu_wake 2, imu_trigger off, device in a trouser pocket, walk 5 minutes.
   Count wakes.
   imu_wake 0, imu_trigger on, connected, device in a camera bag, carry
   5 minutes. Count frames.
   Report both numbers including zero. If either is non-zero, record the
   imu scale that makes it zero and whether tap still works at that scale.

6  Mounted recoil.
   Strap the Stick to the camera, connect, imu_trigger on, fire 20 frames
   from the Remote page button. Count gesture lines. Non-zero means the
   camera's own shutter can re-trigger the remote.

7  Per-sensor gain.
   Repeat steps 1 and 2 on an MPU6886 board (StickC Plus or Core2) and
   record the working imu scale against the shipped 1.25 typeGain.

8  Hour drain, the number that decides the poll rate.
   USB unplugged, full charge, connected to the X100VI, display off.
   power log 30    (CSV to the captured monitor log)
   Run A  imu_wake 0, imu_trigger off, 60 minutes
   Run B  imu_wake 3, imu_trigger on, 60 minutes
   Deliverable: both slopes in mA and the measured delta against the model's
   9.32 mA. Under about 2 mA and the plan's "the cost is large" plus the
   rejection of 25 Hz both need rewriting. Near 9 mA and the INT1 follow-up
   becomes urgent and finding 7's docs warning becomes required.
```

## Recorded, not fixed here

- The refractory period is global rather than per gesture class. With Wake
  Gesture on Shake and Double-Tap Shutter off, `doubleTap` is false, so every
  tap release reports TAP, the wake mask discards it, and `recordGesture()`
  still arms the 750 ms window. A tap shortly before a genuine shake swallows
  the shake. It fails safe, in the direction of fewer false wakes, but it needs
  a deliberate trial in step 2 of the gate above before it is called intended.
- `m_IntervalometerState` now pre-announces WAIT at the start callback so the
  gesture guard sees a run immediately. `UI::getIntervalometerState()` is also
  what the console `status` and the companion read, so both report WAIT for one
  5 ms tick before the state machine gets there. There is a comment at the
  store.
- On the 80x160 non-touch panel the `Gestures` row breaks mid-word, rendering as
  "Gesture" over "s". The 7 px overflow forces a scrollbar, the scrollbar
  narrows the row, and the label wraps inside the word. In the touch layout on
  the same panel, where the page fits, it sits on one line. This is the residue
  of the pre-existing overflow that `bughunt/stick-notouch-layout-80.txt`
  already carries as an `xassert`, and it belongs to whoever promotes that
  assertion. `imu-gesture-gating.txt` carries a comment saying so, because the
  scenario fails its fit assertion if it is run in the non-touch layout on that
  panel.
- No power warning reaches the user. The Gestures page says only "A knock can
  trigger a frame." and the settings docs describe Wake Gesture with no cost
  note. Wake Gesture is the one setting in furble whose cost is a standing
  current increase while the display is off, so the docs row wants a sentence
  once step 8 of the gate produces a real number. A second on-screen warning
  line is the wrong answer: that page is already the tightest thing on 80x160.
- `lib/furble/protocol/ProvisionTLV.cpp` also gains wire id 46, so the shipped
  `IMU` setting becomes provisionable for the first time. That is a deliberate
  behaviour change beyond the two new settings, recorded here because it is
  easy to miss in the diff.

## Deviations

- Observed while adding the text-size routes, and left alone: seeding the IMU in
  `bughunt/text-size-overflow-large.txt` makes the Connected page overflow on
  80x160, because the Level row PR16 adds does not fit at Large text on that
  panel. That is master behaviour with the IMU on, not something this branch
  introduces, so the seed was reverted rather than the assertion weakened. It
  wants its own issue.
- The per-sensor gain ships as a two-entry table (MPU6886 1.25, everything else
  1.0) rather than a full per-board threshold set. One scalar covers the noise
  floor difference the plan describes, and the console scale covers the rest.
  Both numbers are estimates until the hardware gate runs.
- The 25 Hz display-off fallback is not implemented, and the decision is
  deferred to step 8 of the hardware gate rather than taken from the model
  table. The model cannot separate a 20 ms poll from a 40 ms one except by how
  often it charges the 5 ms UI quantum.
- Deep wake through the M5PM1 remains documented only, as scoped.
- The Core settings grid keeps master's `{3, 0}` tile for Sensors. PR #273 adds
  a fourth grid row; whichever of the two lands second re-checks the tile.
- Hardware verification is outstanding and the eight-step gate is owed
  post-merge, not blocking it: both settings default off, so shipping them
  changes nothing until a user turns one on. The executable
  eight-step version is above. Steps 1 and 8 are the two that can still change
  the shipped design: step 1 because no real accelerometer trace exists and the
  one-sample tap claim may alias, step 8 because the poll rate decision is
  waiting on a measured drain rather than a model ceiling.
- All thresholds are hand-written. Nothing in this branch has been fitted to a
  real sensor reading, and the traces from step 1 have to be committed as
  simulator fixtures before they can be.

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
