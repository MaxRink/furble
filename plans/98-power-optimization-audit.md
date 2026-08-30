# 98 - power optimization audit

## Goal

Answer two questions. Are we doing everything possible on power, per board.
Do the plans enumerate the full opportunity space. The answer to both is no.
This document lists what is implemented, what each gap costs, which plans are
missing, and a ranked action list.

Audit base: fork master `fefbd05`, 2026-08-18. All simulator numbers below
were produced with the plan 63 profiler from this tree. The sim was built
with `sim/build.sh` and every checked-in scenario reproduced its baseline
byte for byte (`tools/power-model/compare.py`, all nine PASS at +0.00%).
The numbers are relative model estimates for the S3, not measurements. This
audit was refreshed on 2026-08-30: the figures remain useful for finding
relative opportunities, but they are not hardware evidence or release gates.
The current SDL build uses fake connection Control, Camera, CameraList, and
Scan policy, mixes host and virtual timing in places, and does not yet model
all wake, radio, GPS, display, PMIC, and board-specific costs.

The simulator fidelity work is now sequenced by
[158-sim-scheduler-parity.md](158-sim-scheduler-parity.md). Until its scheduler
and production connection vertical slice land, treat every absolute mA,
runtime, residency, and model-to-bench comparison in this document as
advisory. Re-run the optimization analysis after each calibrated model phase.

## Method

- Ran all nine `sim/scenarios/*.txt` through `sim/build/furble-sim` and
  compared against `tools/power-model/baseline/*.json`.
- Ran four extra what-if scenarios (scratch scripts, not committed):
  connected with screen off, the same with `sleep_conn` on, connected idle
  with `sleep_conn` on, and GPS duty at an out-of-range 30 s interval.
- Read the merged power code: `FurblePower`, `FurbleControl`, `FurbleGPS`,
  `FurbleUI`, `FurbleConsole` (`power stats`, `power log`), all five
  `sdkconfig.*`, `platformio.ini`, and `lib/furble` connection code.
- Read every power plan (00 to 19, 26, 32, 33, 63, 64, 71) plus the plans
  README.

## 2026-08-30 review refresh

The additional power review found these blockers to a hardware-faithful
optimization decision:

- The fake Control duplicates connection truth and does not exercise the
  production Control, Camera, CameraList, Scan, or NimBLE ownership and
  cancellation paths. A green connection scenario cannot validate radio duty
  or sleep-lock behavior.
- Before plan 158 the host had detached task threads, wall-clock queue waits,
  host timers, and virtual firmware time. The scheduler foundation now uses
  joinable workers and virtual waits, but same-tick worker dispatch is still
  host-scheduled. `getState()` and some query paths also change observable
  state, so profiler reads are not always pure observations.
- The power shim is policy-only and incomplete. It does not yet charge wake
  overhead, BLE event duty by negotiated parameters, GPS cold and warm start,
  brightness, all sleep states, or board-specific PMIC and peripheral rails.
  Invalid settings and failed model inputs must fail closed instead of silently
  producing plausible current.
- Existing invalidation and flush counters are valuable diagnostics, but a
  settings navigation run showed that more invalidation and flush work does
  not map directly to the current table. The model must retain per-state
  attribution before optimization choices are ranked.

The ranked optimization questions remain useful, but no default should be
changed from these modeled numbers alone. The next analysis must use the
production connection stack, exact board profiles, calibrated peripheral
states, and differential traces against the same scenario on hardware.

### Critical model errors found in the refresh

1. GPS rail off and on transitions are not profiled, and the configured
   107.7-second cold-start refix cost is never charged. Rail cycling can look
   cheaper than continuous tracking even when reacquisition dominates.
2. The gate compares only total estimated mA. Independent regressions in wake
   count, light-sleep residency, lock ownership, pixels, GPS state, or radio
   work can cancel numerically and pass.
3. Timer, queue, and task wake transitions are free. A wake storm can preserve
   total sleep time and report no energy change.
4. Active display brightness is absent. The model assumes full brightness for
   on and a fixed 32/255 duty for dim, regardless of the effective setting.
5. Active CPU time bottoms out at 80 MHz even though production configures a
   40 MHz minimum.
6. GPS terms mix module-level 3.3 V estimates with whole-device 5 V input
   measurements and omit conversion efficiency and rail-off leakage.
7. BLE accounting charges generic application events instead of captured radio
   airtime, retries, PHY, payload, and negotiated connection intervals. Its
   connected floor may already include traffic that is then double counted.
8. Each state duration is rounded independently to one second before energy is
   integrated. A 499 ms state becomes zero and a 500 ms state becomes one
   second, so component totals can exceed the real window.
9. Missing or malformed current-model input falls back silently to built-in
   defaults while the report still names the YAML source. Loading must fail
   closed and include the exact model digest.
10. CI and report identity are S3-only despite per-board tables. StickC, Plus,
    Core, and Core2 regressions are not power gated.
11. Simulator PM diagnostics report an 80 MHz minimum and light sleep disabled,
    unlike production's 40 MHz minimum and enabled policy.
12. Unknown display or GPS state names disappear from serialization and energy
    instead of invalidating the report.

### Revised implementation order

1. Accounting correctness: integrate raw durations, fail closed on model load,
   record a model digest, reject unknown states, track effective brightness,
   align PM configuration, and choose one electrical boundary.
2. Independent regression guards: gate sleep residency, lock residency,
   timer/queue/task wake rates, pixels, radio event classes, GPS rail changes,
   and required state coverage separately from estimated mA.
3. Physics proxies: derive BLE airtime from captured link parameters, display
   cost from transferred pixels and duty, GPS/UART cost from bytes and rail
   state, and scheduler transition cost from wake counters.
4. Scenario and board matrix: add scan and reconnect, connected screen-off with
   saver on and off, brightness sweep, GPS standby and rail-cycle recovery,
   degraded retry, and every exact board profile.
5. Hardware calibration: use an external analyzer to calibrate the highest
   impact terms first, then retain raw traces and tolerances with each model
   revision.

The optimization priority after those corrections remains display duty first,
then safe connected sleep, screen-off housekeeping wakeups, BLE idle interval,
GPS duty policy with cold-start cost, and APB-lock scope. The current numbers
cannot reliably rank changes within one of those categories.

## Baseline numbers, S3 energy model

| Scenario | est mA | mcu | radio | display | gps |
|---|---|---|---|---|---|
| menu-idle-30s | 81.2 | 40.2 | 0 | 41.0 | 0 |
| connected-idle-30s | 84.5 | 40.2 | 3.3 | 41.0 | 0 |
| connected-gps-active-30s | 107.5 | 40.2 | 3.3 | 41.0 | 23.0 |
| gps-duty-standby-cycle | 102.7 | 40.2 | 0 | 41.0 | 21.5 |
| settings-navigation-sweep | 80.7 | 39.9 | 0 | 40.7 | 0 |
| intervalometer-5-frames | 84.7 | 40.2 | 3.5 | 41.0 | 0 |
| screen-dimmed-30s | 50.6 | 40.2 | 0 | 10.4 | 0 |
| blind-remote-shutter | 43.6 | 40.1 | 3.5 | 0.01 | 0 |
| screen-off-30s | 0.25 | 0.24 | 0 | 0.01 | 0 |

What-if runs against the same binary:

| Scenario | est mA | note |
|---|---|---|
| connected, screen off, defaults | 43.6 | `no_light_sleep` held by control for the whole window |
| connected, screen off, `sleep_conn` on | 3.61 | light sleep residency 99.97% |
| connected idle, screen on, `sleep_conn` on | 84.5 | unchanged, the display APB lock pins the MCU |
| gps duty seeded 30 s | 104.2 | standby residency 0, value outside `DUTY_SECONDS` |

The 3.61 mA what-if is numerically close to the 3.3 mA connected-idle value
reported in plans/00, but the states, board conditions, and measurement method
are not equivalent enough to validate the model. Keep this as a hypothesis to
retest with a calibrated external measurement after plan 158.

Two conclusions fall straight out of the table. First, the display plus the
MCU-held-active pattern is 81 of the 84 mA in every screen-on scenario.
Second, one settings default (`SLEEP_CONN` off) is worth a factor of twelve
whenever the device is connected with the screen off. On the 250 mAh
StickS3 cell that is about 6 hours versus 69 hours.

## What is implemented, and whether it is optimal

### Plan 01, CPU frequency

Implemented. `CPU_FREQ` setting 80/160/240, default 160, min pinned at
40 MHz (`Power::CPU_MIN_FREQ_MHZ`). All five boards share the same table.
Left on the table:

- Default 160 costs nothing at idle under DFS, but boot runs a fixed 160
  window before the stored setting applies. Minor.
- Min frequency below 40 was declared out of scope in plan 01 and never
  revisited. ESP32 supports 40/20/13/10 as XTAL dividers with esp_pm; the
  S3 supports 40 down to 10. Worth one hardware experiment. Savings small
  because light sleep already covers true idle.

### Plan 06, power module

Implemented and good. Three counted named locks, owner attribution, no
manual `esp_light_sleep_start()` anywhere. `CPU_FREQ_MAX` is never
acquired by any code path; that is fine (diagnostic only) but means DFS is
purely load-driven. `Platform::setSleep()` deletion promised in plan 06 is
still owed.

### Plan 07, BLE modem sleep and light sleep while connected

Implemented. S3 sdkconfig has modem sleep, main-XTAL LPCLK and MAC/BB power
down. ESP32 boards run `BTDM_CTRL_MODEM_SLEEP_MODE_ORIG` on the main XTAL.
Not optimal:

- **`SLEEP_CONN` defaults to false.** The profiler puts the cost at
  43.6 mA versus 3.6 mA for connected with the screen off. This is the
  single largest addressable number in the whole audit. Either flip the
  default after a soak, or ship it on in a battery build profile.
- The 0.23 mA 32 kHz mode is dead on all boards (no crystal, proven in
  plans/00). Nothing more to do at the config level on the S3.

### Plan 08, scan tuning

Implemented, three presets (100/25/5% duty), reconnect always forced to
Full. Left on the table:

- Default is Full. Balanced costs little in discovery latency.
- Scan is always active-mode (`setActiveScan(true)`). Passive scanning
  halves radio-on time per window and furble does not need scan responses
  for reconnect-by-address. Worth a measurement.
- No decay schedule (start Full, drop to Low after n seconds). Plan 08
  never mentions it.
- The profiler has no scan state at all (radio model is connected-time
  plus TX events only), so none of this is CI-guarded. No scan scenario
  exists in `sim/scenarios/`.

### Plan 09, reconnect backoff

Implemented, 5 to 120 s doubling, default off. Fine. The plan's own "add
jitter later" note was never picked up. Default off means the flat 5 s
retry burns scan-at-full-duty forever when a camera stays off; consider
defaulting on in a battery profile.

### Plan 10, adaptive connection parameters

Implemented and hardware verified. Idle profile 250 to 300 ms, latency 0
with a correct central-role justification, 10 s idle threshold, guard
timer. Left on the table:

- **Default off.** With it off the link sits at 37.5 to 62.5 ms forever.
- The 3.3 mA floor was measured at stock parameters. At a 300 ms idle
  interval the per-event cost drops by roughly 6x; the true floor with
  `CONN_SAVER` on is likely below 3.3 mA. The energy model cannot see
  this (it charges a constant connected floor), so the saver's radio win
  is invisible in CI. Model gap and a missing hardware measurement.
- The documented geotag defeat (GPS writes keep the link fast) has no
  follow-up plan. Batching location writes to one per n seconds while
  idle would restore the saver under GPS.

### Plan 11, adaptive TX power

Implemented, P3/P6/P9 with RSSI hysteresis, default off. The dBm mapping
bug fix was the real win here. Remaining value is small; the profiler
charges TX events at 176 mA peak but they are rare. Negative TX levels
(N0, N3) below P3 are not offered; for a remote sitting on the camera
that is free range margin to spend. Low priority.

### Plan 12, display off

Implemented. True panel sleep-in, APB lock released on off, icon timer
paused, S3 green LED handled (plan 71). Not optimal:

- **Inactivity default is Never and `DISPLAY_OFF` default is Dim.** The
  display is 41.0 mA modeled at default brightness on the S3, half the
  screen-on total. A device that never blanks by default forfeits the
  best implemented saving. 30 s or 60 s default plus mode Off is worth
  roughly 40 mA whenever the user forgets the device.
- **Dim keeps the APB lock and the MCU stays fully active.** Dimmed is
  50.6 mA, of which only 10.4 is display. The MCU cannot light sleep
  because the lock never drops while the backlight PWM runs.
- Only the icon timer pauses when the display is off. Battery (5 s),
  inactivity (1 s), connect (50 ms) and companion pairing (250 ms)
  timers keep firing, and the UI task keeps its 5 ms loop. The sim
  models this as sleepable; on hardware every wake has entry/exit
  overhead. Lengthen the UI loop and pause the connect timer when the
  screen is off and no connect is in flight.

### Plan 13, auto off and low battery

**Not implemented.** No `AUTO_OFF`, no `LOW_BATT`, no power-off policy in
the tree. Only a low-battery feedback signal exists (10%, 6 samples), and
it is inert with the default `FB_OUTPUT` off. The plan is written,
reviewed, and stalled. A forgotten device today runs 81 mA to empty.

### Plan 15, GPS power

Implemented on the S3 and hardware verified. `$PCAS12` standby, burst
windows, rail cycling behind a warning. Two large gaps:

- **Duty cycling barely ducks.** In the checked-in baseline the receiver
  spends 14 of 15 s tracking and 1 s in standby; GPS averages 21.5 mA
  against 23.0 always-on. Root cause is structural:
  `MIN_WAKE_WAIT_MS = 5000` and `DUTY_SECONDS = {0, 5, 10, 15}` mean the
  wake dwell eats most or all of the interval. Best case at 15 s duty is
  about 50% standby, roughly 12 mA. The knobs that would matter (longer
  intervals with a cached-fix policy, plan 18 motion gating, plan 32
  ephemeris cache for rail cycling) are all unimplemented. A 60 s
  interval with a 5 s burst would model at about 4 mA.
- **The GPS power lock is S3-only.** `acquirePowerLock` is compiled out
  on the other four boards. With `SLEEP_CONN` on and GPS attached, ESP32
  boards will light sleep through NMEA bursts and lose them (UART RX
  does not survive light sleep, plan 15's own finding). Correctness gap
  that blocks recommending `SLEEP_CONN` plus GPS off-S3.
- The `$PCAS12` in-circuit standby current is still an estimate
  (0.5 mA, range 10 uA to 2 mA). It is one of the two entries the model
  README flags as highest impact and it is measurable on the bench.

### Plan 19, interval deep sleep

**Not implemented.** No `esp_deep_sleep` call exists anywhere in the
tree. This is the largest absolute win available for timelapse: 84.5 mA
connected during a 2 min interval versus about 10 uA asleep between
frames, with M5PM1 `timerSet` wake on the S3 and the BM8563 alarm on the
Plus2. It also has a documented unresolved conflict with the merged plan
26 watchdog. Nobody owns the resolution.

### Plans 63 and 64, tooling

Both merged and working. The profiler reproduced every baseline and the
`power stats` and `power log` console surfaces exist. Gaps:

- **The CI gate is not wired.** No workflow runs `sim/scenarios/*` or
  `tools/power-model/compare.py`. The comparator, baselines and scenario
  suite all exist; the plan 63 pillar 5 job was deferred and never
  landed. Every number in this audit is currently unguarded.
- The energy model is S3-only. `board-currents.yaml` carries all five
  boards but the profiler reads only the `esp32s3` and shared anchors.
  No per-board reports, contrary to the plan 63 comment design.
- The radio model has no scan, advertising or reconnect states, so plans
  08 and 09 are unguarded by construction. No scan scenario exists.
- The model charges a constant connected floor, so plan 10's interval
  change is invisible.
- The two highest-impact calibration entries (backlight mA per board,
  `$PCAS12` standby mA) remain `estimated`.

## Per-board notes

- **M5StickC, StickC Plus (AXP192).** The AXP192 quiescent floor of
  about 2 mA bounds every sleep state. Light sleep works (modem sleep
  ORIG on main XTAL) but no external 32k crystal, so 1.9 mA BLE light
  sleep is unreachable; 14.1 mA modem sleep is the connected floor. No
  AXP192 rail audit exists: LDO2/LDO3/DCDC blocks that M5Unified leaves
  enabled cost an estimated 150 to 300 uA. GPS power lock absent (see
  above). No timed wake, so plan 19 does not apply.
- **M5StickS3 (M5PM1).** Best instrumented board and the only one with
  the GPS window lock, LED-off handling and a true shutdown (14 uA).
  Deep sleep 7 uA plus PM1 L1 52 uA available and unused (plan 19).
  Watchdog feed keeps the PM1 out of its I2C idle sleep once per
  second; the `SLP_TO` register read promised in plan 26 is still owed.
  `CONFIG_USJ_NO_AUTO_LS_ON_CONNECTION` applies to debug use only.
- **M5Stack Core (IP5306).** The boost converter hard-powers-off after
  32 s below 45 mA. Light sleep floors, screen-off idling and any deep
  sleep are all unreachable as shipped; the only honest policy is
  screen-on or off. Plans acknowledge this. Nothing more to do beyond
  documenting that battery features exclude the Core.
- **M5Stack Core2 (AXP192).** Same AXP192 floor as the Sticks. The
  touch controller is deliberately left running (plan 12 decision,
  reasonable). Vibration motor and NS4168 amp are correctly shut down
  between uses. MPU6886 is untouched because `internal_imu = false`;
  its power-on default is sleep, but nothing asserts that.

## Is 3.3 mA truly the S3 floor?

Not established by this audit. The measured 3.3 mA value, the Espressif
power-save reference, and the 3.61 mA composed estimate were collected or
derived under conditions that are not yet trace-equivalent. The 0.23 mA mode
also requires an external 32.768 kHz crystal the board does not have. Treat
the values in plans/00 as hardware observations to reproduce, not as a
validated universal floor.

But the floor is parameter-relative, not absolute:

- With `CONN_SAVER` idle parameters (300 ms interval) the connection
  event rate drops about 6x versus stock. The radio share of the floor
  should drop with it. Expected result is somewhere between 0.5 and
  2 mA connected idle. This is unmeasured and the model cannot predict
  it. One bench soak with `power log` settles it.
- Below that sits disconnected light sleep at about 0.3 mA modeled, and
  deep sleep at 7 uA plus PM1 overhead. Reaching those while still
  being useful is plan 19 territory (sleep between frames, timed wake),
  not connection tuning.
- Residual nibbles at the floor: USB serial JTAG PHY (debug only),
  the PM1 wake once per second for the watchdog feed, and
  `MAIN_XTAL_PU_DURING_LIGHT_SLEEP` which is the price of BLE and not
  removable while connected.

So: 3.3 mA is the floor for the current parameter set. It is not the
floor for the board.

## Do the plans cover the full space?

No. The numbered plans cover CPU frequency, locks, BLE sleep, scan,
backoff, connection parameters, TX power, display off, auto off, GPS
power, motion GPS, deep sleep, the watchdog and the tooling. The
following are in no plan at all.

Missing plans, in priority order:

1. **Power-optimized build profile (the missing plan 77).** No
  `77-*.md` exists and no `-lowpower` env exists in `platformio.ini`.
  The requirement is on record: a power-optimized build for every
  battery platform. Today the entire battery story ships as seven
  runtime settings that all default off (`SLEEP_CONN`, `CONN_SAVER`,
  `TX_ADAPTIVE`, `RECON_BACKOFF`, `SCAN_MODE`, `DISPLAY_OFF` beyond
  dim, `GPS_POWER`), spread over four submenus. Two viable shapes:
  a `-lowpower` env per battery board that changes the setting
  defaults (and can also drop `CPU_FREQ_DEFAULT` to 80), or a single
  "Battery saver" master toggle that flips the set at runtime. The
  runtime toggle serves users better and needs no extra CI envs; the
  build profile serves the web flasher. The plan should choose.
2. **Defaults review.** Distinct from the profile: which of the seven
  should simply default on for everyone after soak evidence.
  `SLEEP_CONN` and `CONN_SAVER` are the candidates with the largest
  numbers attached (40 mA and the sub-3.3 mA floor).
3. **Tickless and RTOS tuning.** `CONFIG_PM_RTOS_IDLE_OPT` is unset on
  all five boards. `CONFIG_FREERTOS_HZ` and
  `FREERTOS_IDLE_TIME_BEFORE_SLEEP=3` were never examined as power
  levers. The UI task 5 ms loop and the 50 ms connect timer bound the
  practical sleep window; no plan states that. Includes lengthening
  the UI loop when the display is off.
4. **Backlight clock source and APB lock scope.** The APB lock exists
  for the S3 LEDC flicker. On StickC, StickC Plus and Core2 the
  backlight is a voltage-dimmed PMIC rail with no LEDC, yet the UI
  holds the lock on every board. On the S3, LEDC can clock from
  RC_FAST and keep its output through light sleep on the S3 core.
  Scoping or removing that lock is the only path to cutting the 40 mA
  MCU term while the screen is on. High risk (the flicker class),
  high reward, needs the bench.
5. **GPS duty rework.** Longer intervals with a cached-fix freshness
  policy, shorter wake dwell, and per-state numbers in the report.
  Plan 15 clamps to the top `DUTY_SECONDS` entry of 15 s, well inside the
  30 s `MAX_AGE_MS` freshness budget; the clamp is the thing to redesign,
  together with plan 18 (motion) and plan 32
  (ephemeris cache) which both remain unimplemented.
6. **AXP192 rail and peripheral audit.** Disable unused LDO/DCDC
  blocks on StickC/Plus/Core2, assert IMU suspend state at boot,
  confirm the PM1 `SLP_TO` value. Sum is a few hundred uA; cheap,
  low risk.
7. **BLE link efficiency.** LE 2M PHY, MTU/data length extension for
  the two-write Fujifilm shutter and geotag traffic, geotag write
  batching so GPS stops defeating the connection saver, passive scan.
  No plan mentions any of these.
8. **Power gate CI wiring.** Land the deferred plan 63 pillar 5 job:
  run the nine scenarios, compare, sticky comment. Add scan and
  reconnect scenarios and per-board model output while at it. Without
  this, every optimization above is one silent regression away from
  undone.
9. **Calibration debt.** Backlight mA per board and `$PCAS12` standby
  mA, the two entries the model README flags as highest impact, plus
  the plan 10 idle-parameter floor soak and the plan 26 watchdog
  on/off delta. All bench work with existing tools.

Explicitly checked and not worth a plan: WiFi power save
(`CONFIG_ESP_WIFI_ENABLED=y` is config residue; no code initializes
WiFi, the linker drops it, there is no runtime cost, so `WIFI_PS_*`
and coexistence tuning are moot until plan 33), brownout levels
(safety, not power), ADC (unused directly), speaker/vibration/IR
(already power-disciplined with enable/disable brackets).

## Prerequisite for the action list

Complete plan 158 Phase 1 before using the modeled ranking to choose a power
default or quote a runtime. Then complete the production connection vertical
slice and the relevant board calibration for any action involving BLE, sleep,
GPS, display, or absolute current. The ranking below remains a hypothesis
list until those gates pass.

## Top 10 actions

1. Ship a battery-saver profile (new plan 77): one switch or one env
  that enables `SLEEP_CONN`, `CONN_SAVER`, `DISPLAY_OFF` off-mode with
  a 60 s inactivity default, `RECON_BACKOFF`, Balanced scan. Worth
  40+ mA connected, 12x on the blind-remote case. Effort small, risk
  low, all mechanisms already merged and verified.
2. Wire the plan 63 power gate into CI (scenarios, compare.py, sticky
  comment). Effort small, risk none, protects everything else.
3. Implement plan 19 interval deep sleep on S3 and Plus2, resolving
  the plan 26 watchdog conflict. Worth 84 mA to 10 uA between
  timelapse frames. Effort large, risk medium (bricking traps are
  documented), biggest absolute win.
4. Extend the GPS burst power lock to all boards (drop the
  `FURBLE_M5STICKS3` guard around lock handling, keep the UART clock
  split). Correctness prerequisite for `SLEEP_CONN` off-S3. Effort
  small, risk low.
5. Implement plan 13 auto off and low battery power off. Turns the
  81 mA forgotten-device case into 14 uA. Effort medium, risk low,
  plan is fully written.
6. Bench-measure connected idle with `CONN_SAVER` idle parameters via
  `power log`; if the floor drops as expected, prefer defaulting
  `CONN_SAVER` on and teach the model an interval-scaled radio term.
  Effort small, risk none.
7. Rework GPS duty for real ducking: longer intervals plus cached-fix
  policy, shorter wake dwell, measure `$PCAS12` standby current and
  replace the estimate. Worth up to ~19 mA of the 23 mA GPS budget.
  Effort medium, risk medium (fix freshness).
8. Audit the APB lock scope per board and prototype RC_FAST LEDC on
  the S3; on success, dimmed idle drops from 50.6 mA toward 11 mA.
  Effort medium, risk high (flicker class), do behind the gate from
  action 2.
9. Reduce screen-off housekeeping: pause the connect timer when idle
  and disconnected, lengthen the UI task delay while the display is
  off, set `CONFIG_PM_RTOS_IDLE_OPT`, and review `FREERTOS_HZ`.
  Effort small, risk medium (watchdog feed margin, input latency).
10. Peripheral sweep: AXP192 unused rails off, IMU suspend asserted,
  PM1 `SLP_TO` confirmed. A few hundred uA on the 2 mA-floor AXP192
  boards where every 100 uA is 5% of the floor. Effort small, risk
  low.

## Verification

- All nine committed scenarios rerun against `fefbd05` reproduce their
  baselines exactly (compare.py PASS at +0.00%).
- The what-if scripts used for the 43.6 versus 3.61 mA and GPS duty
  numbers are described inline above and are reproducible with the
  driver's `seed` verb; they were intentionally not committed.
- No firmware, sim, or tooling file changes in this PR. Docs only.
