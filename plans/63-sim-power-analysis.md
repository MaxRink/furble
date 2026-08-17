# 63 - simulator based power analysis

## Goal

Estimate furble's power behavior from the host simulator: per scenario, per
board, in seconds, with no device attached. Report activity, lock hold times,
sleep residency and an estimated average current. Gate CI on relative
regressions against a checked-in baseline, so the power bug classes that
reviews currently catch by hand are caught by a machine before a human reads
the diff.

Line anchors below were read at `f455b0b` on fork `master` and at `7487f64` on
`feat/28-emulator`.

## Motivation

Reviews keep catching power bugs of the same three classes before merge. Each
class has already happened in this tree at least once.

**Timers running while their page is hidden.** The UI owns ten LVGL timers:
the inactivity timer at `src/FurbleUI.cpp:154`, the battery timer at `:230`,
the diagnostics timer at `:233`, the icon timer at `:237`, the connect timer
at `:385`, the intervalometer timer at `:389`, plus the GPS data, interval
page, bulb and bulb page timers declared at `:54-62`. The discipline is
manual: the diagnostics timer is paused when its page closes at `:991`, and
nothing checks that any other timer follows the same rule. A timer that keeps
firing behind a hidden page redraws for nobody and holds the CPU out of its
sleep floor. Nothing in CI can see it.

**Unconditional LVGL setters.** The hard-won rule in the root CLAUDE.md:
every periodic `lv_image_set_src` and `lv_label_set_text` must sit behind a
changed check, because an unconditional setter invalidates and redraws even
when the value is identical. Icons are compressed and cost a 12.3 KB
decompress per 64x64 draw unless the image cache covers them. The diagnosis
tools today are `CONFIG_LV_USE_REFR_DEBUG` and the console gated invalidation
logger, both of which need a flashed device and a person watching.

**Power management lock leaks.** The GPS RESYNC leak is the type specimen.
In the GPS power PR (fork PR #27), `MEASURING` and `RESYNC` became terminal
states once a burst went active: `finishMeasurement()` was unreachable, the
`NO_LIGHT_SLEEP` lock acquired in `GPS::beginResync` (`src/FurbleGPS.cpp:557`)
leaked, and a stale measure deadline busy-spun the GPS task. Review caught
it and `a16046a` fixed it. A leaked `NO_LIGHT_SLEEP` lock erases light sleep
entirely, the firmware behaves normally, and the only symptom is a battery
that drains in hours instead of days.

The hardware answer to all three is a current measurement: a device on the
bench, a stable setup, and a 30 to 60 minute soak per configuration. That is
the right tool for calibration and the wrong tool for regression detection.
The simulator from [28-emulator.md](28-emulator.md) already runs the shipping
`src/FurbleUI.cpp`, `src/FurbleGPS.cpp` and `src/FurbleSettings.cpp` on a
virtual clock under a scripted driver. All three bug classes are visible in
that build as counters: timer fires, invalidated pixels, lock hold time. The
sim can catch all three in seconds in CI.

## Design

### Principle: policy versus physics

The split that makes the model honest. The simulator models every
firmware-driven state machine: which timers fired, which locks were held,
which link parameters were requested, which GPS duty state was entered, which
frequency the governor would choose. Hardware calibrates a per-state current
table and validates timing. The sim answers "which states did the firmware
choose and for how long". The table answers "what does each state cost".
The energy model multiplies the two. Anything electrical or timing-accurate
stays a hardware question.

### Pillar 1: activity profiler

Per-scenario counters, all recorded in the sim shim with no shipping source
change:

- LVGL timer callback fires per second, per timer, named. The profiler keeps
  a table mapping callback pointers to names for the known UI callbacks, so
  the report reads `icon_timer: 4.0/s`, not an address.
- Invalidated area per second. The sim registers the same invalidation hook
  the console gated invalidation logger uses on hardware, so the two tools
  report the same quantity.
- Flushed pixels per second, counted in the sim display flush path.
- Task and scheduler wakeups in the sim FreeRTOS shim (`sim/freertos.cpp`):
  queue receives that returned data, delay expiries, per named task.

Scenarios reuse the plan 28 scripted navigation (`sim/driver.cpp`: `wait`,
`key`, `capture`, `exit` on the virtual clock from `sim/clock.h`). The driver
gains a `report <file>` step that dumps and resets the counters. The scenario
set starts from this sketch:

- menu idle, 30 s
- connected idle against the FauxNY fake camera, 30 s
- GPS page open
- screen dimmed
- screen off

The normative suite, with initial state, scripted steps and duration per
scenario, is the Usage tests section below.

The virtual clock makes 30 s of scenario time milliseconds of wall time, and
makes two runs of the same scenario produce identical reports. Output is one
machine-readable JSON report per scenario.

### Pillar 2: sleep-opportunity estimator

`Furble::Power` (`include/FurblePower.h`, `src/FurblePower.cpp`) compiles
into the sim compile set unchanged. The current `sim/shim/esp_pm.h` is a
no-op that only defines `esp_pm_configure`. It grows into a recording stub:
`esp_pm_lock_create`, `esp_pm_lock_acquire` and `esp_pm_lock_release` record
every transition with a virtual clock timestamp and the holder name. The
Power module already passes static owner strings and lock names to every
call, so the stub gets attribution for free.

The report adds, per lock: total hold time, acquire count, and a hold-time
histogram. On top of that the estimator computes light sleep residency: the
percentage of scenario time with zero locks held, the LVGL timer queue idle,
and the task shim idle. A leaked lock shows up as residency collapse. Replay
the RESYNC leak against this estimator and connected-idle residency drops
from a healthy figure to zero in one run, which is exactly the signal the
review had to find by reading.

### Pillar 3: modeled hardware subsystems

Four subsystems move from out of scope to modeled with calibration. In every
case the sim runs the real firmware policy code and charges its state choices
against calibrated constants. The constants live in the shared current table
described in pillar 4.

**BLE radio duty.** The sim integrates radio-on time from the link state the
real firmware state machine chooses. Scan duty comes from the scan mode
window and interval presets (plan 08). Connected duty is connection interval
times latency times a per-event cost, with a calibrated 1 to 2 ms per empty
connection event and a larger cost for notification traffic. Advertising
bursts, the idle versus fast profile switching from the connection saver
(plan 10) and the reconnect backoff timing (plan 09) all feed the same
integral. The model runs against the mock NimBLE client layer from
[36-camera-test-harness.md](36-camera-test-harness.md) tier B, shared with
that plan rather than duplicated. CI catches duty regressions this way: a
fast profile pinned after a shutter press, or a scan stuck at 100% duty,
both show as a step in integrated radio-on time.

**GPS receiver model.** Reuses the plan 29 decision: the sim provides fake
`driver/uart.h` functions and compiles the real `src/FurbleGPS.cpp`, which
`sim/build.sh` already does. On top sits a receiver state model with
calibrated currents for acquisition, tracking, `$PCAS12` timed standby and
rail off, plus the measured reacquisition penalty curve: warm restart in
seconds, cold start around 108 s per
[00-hardware-experiments.md](00-hardware-experiments.md) Experiment B. The
output is net energy per duty policy including reacquire penalties. That
catches policy bugs (standby never entered, wake flapping between bursts)
and answers "is rail cycling worth it at this fix interval" before any
hardware run.

**PMIC behavioral stub.** A behavioral M5PM1 stub emulates the documented
quirks for correctness testing: the first I2C transaction after its idle
sleep fails and only wakes it, watchdog arm, feed and expiry (plan 26), and
`timerSet` plus shutdown for deep sleep wake (plan 19). Firmware logic
against the PMIC becomes CI-testable: deep sleep must disarm or account for
the watchdog, and every access must retry once. A synthetic battery
discharge curve drives the runtime estimator so its math is testable without
waiting for a real battery to drain. Quiescent currents are table entries,
not simulated.

**DFS policy stub.** The esp_pm stub implements the real governor semantics,
read from the pinned ESP-IDF 5.x `esp_pm` source: CPU frequency is the
configured maximum while `CPU_FREQ_MAX` is held and the minimum otherwise,
APB holds its maximum while `APB_FREQ_MAX` is held, and light sleep entry
requires an idle system with no `NO_LIGHT_SLEEP` lock. The stub reports
frequency residency per scenario: time at 80 MHz, at 160 MHz, and in light
sleep. The energy model weights residency by per-frequency currents. Stated
plainly: the electrical and timing side effects of DFS, clock switch
glitches, the UART corruption class from the root CLAUDE.md, and wake
latencies remain hardware only. The stub models the policy, never the
silicon.

### Pillar 4: energy model

The per-board current table is checked into the repository as data at
`tools/power-model/board-currents.yaml`. It is being seeded from datasheets
and published measurements in parallel with this plan. Schema: per board,
entries for mcu, radio, display, gps, pmic and peripherals, each with value,
min, max, source, and a confidence tag out of `datasheet`,
`published-measurement`, `estimated`, `measured-local`. The table carries the
base currents (active mA at 80 and 160 MHz, light sleep mA, display on/off
delta, backlight levels) and the calibration constants for the pillar 3
subsystem models (per-connection-event cost, scan current, GPS state
currents, PMIC quiescent).

The model combines the activity report, the lock and residency report, and
the subsystem duty integrals with the table to output an estimated average
mA per scenario per board.

Absolute accuracy is secondary and the plan says so plainly. The CI gate is
RELATIVE: fail when estimated idle-scenario drain regresses more than a
threshold against a checked-in baseline JSON. A relative gate tolerates a
table that is wrong by a constant factor, and datasheet confidence is enough
for it. The gate therefore goes live with datasheet numbers as soon as the
profiler exists, and does not wait for the hardware calibration below.

### Pillar 5: CI job shape

Extends the plan 28 CI sim build (fork PR #39). The job builds the sim, runs
the scenario suite headless, compares each scenario report against
`sim/power/baseline/*.json`, and uploads the JSON reports as an artifact.

The gate, precisely:

- For each scenario, fail if `estimated_mA > baseline_mA * (1 + threshold)`.
  Threshold starts at 10% and is tuned once a few weeks of reports exist.
- Everything else in the report (per-timer fires, invalidated area, lock
  histograms, duty integrals, residency) is an artifact and a review aid,
  not a gate.
- A baseline update is an explicit commit that a reviewer approves with the
  regression numbers in front of them. The job never rewrites baselines.

Dependency and consolidation. Plan 28 (fork PR #39) must merge first, since
the profiler instruments its shim and its driver. The ui-audit walker
(`tools/ui-audit.md`, fork PR #42) and the screenshot CI job (fork PR #44)
share the same scripted-navigation infrastructure this plan extends. There
should be one walker that audits layout, captures screenshots and profiles
power on the same pass, not three copies of the navigation logic. Whichever
of the three lands last consolidates.

### Sticky analysis comments

The firmware size report in `.github/workflows/main.yml` is the first instance
of the sticky analysis-comment pattern, keeping one marker-backed PR comment
updated with machine-generated review data.

### Out of scope, stated honestly

- RF physics: interference, retransmissions, coded PHY range behavior. The
  duty model integrates requested radio time, not air time.
- Real DFS timing and its electrical side effects. See the DFS stub note.
- Actual PMIC quiescent behavior and charger behavior. Table entries only.
- GPS receiver internals beyond the four-state model. The AT6668 is modeled
  as a current per state plus a reacquisition penalty, nothing finer.
- Temperature, battery aging, and the discharge curve of a specific cell.

Those remain hardware measurements.
[00-hardware-experiments.md](00-hardware-experiments.md) and the battery
drain console logging from PR02 ([02-battery-display.md](02-battery-display.md))
are the hardware complement to this plan. Task 14, the assisted-refix
retest, stays hardware.

## Usage tests

The named scenario suite. Each script is the sim's model of one way the
device is actually used, and every number in the CI report is "estimated mA
while doing X" for one of these names. The suite is normative: the reports,
the baselines and the gate all key on these scenario names.

Conventions, fixed here so an implementer needs no further decisions:

- Scripts live at `sim/scripts/power/<name>.txt` and use the plan 28 driver
  verbs `wait`, `key`, `capture`, `exit` plus the `report <file>` step from
  pillar 1. `wait` takes milliseconds of virtual time. Key names follow the
  M5Unified PC mapping: `left` is BtnA, `down` is BtnB, `right` is BtnC,
  `up` is BtnPWR.
- Every script is a setup phase, then `report` to a throwaway file to reset
  the counters, then the measured window, then `report <name>.json`. Only
  the final report is scored.
- Durations are virtual scenario time. The wall cost stays milliseconds.
- Seeded state comes from the plan 28 preferences file. Each scenario names
  the settings it seeds; everything else stays at defaults.
- Connected scenarios seed `FAUXNY` on and share one connect prologue: boot
  to the menu, navigate to Connect and select the FauxNY entry with the same
  key walk the ui-screenshots script uses, then `wait 5000`, which is past
  the end of the deterministic FauxNY connect ramp, landing on the Connected
  page.
- GPS scenarios feed the sim GPS a recorded NMEA file with a valid fix at
  1 Hz, the plan 28 `FurbleGPSSim` path.

The baseline suite:

**menu-idle-30s.** Initial state: fresh boot, main menu, no camera, GPS
off, display on at full brightness. Steps: `wait 30000`. Measured window
30 s. Captures the idle floor: per-timer fires, invalidated and flushed
pixels, lock holds, residency. Every other scenario is read against this
one.

**connected-idle-30s.** Initial state: connect prologue, Connected page, no
GPS. Steps: `wait 30000`. Measured window 30 s. Captures the connected
floor: the BLE duty integral at the idle connection parameters, residency
with the link up. The RESYNC leak class and a pinned fast profile both show
here.

**connected-gps-active-30s.** Initial state: connect prologue with GPS
seeded on, duty cycling off, fake NMEA at 1 Hz. Steps: `wait 30000`.
Measured window 30 s. Captures GPS tracking cost on top of connected idle:
UART and GPS task wakeups per second, receiver model residency in tracking,
location notification traffic in the BLE duty integral.

**screen-dimmed-30s.** Initial state: menu idle with the display dim
timeout seeded to 10 s and the display off timeout seeded to 120 s. Steps:
`wait 15000` so the dim engages, reset, `wait 30000`. Measured window 30 s.
Captures the dimmed delta: backlight level, the `APB_FREQ_MAX` hold from
the backlight PWM, timer behavior while dimmed.

**screen-off-30s.** Initial state: menu idle with the display off timeout
seeded to 10 s. Steps: `wait 15000` so the display turns off, reset,
`wait 30000`. Measured window 30 s. Captures the deepest reachable floor:
flushed pixels must be zero, and residency should be the best of any
scenario.

**blind-remote-shutter.** Initial state: connect prologue, display off
timeout seeded to 10 s, then `wait 15000` so the screen is off with the
link up. Steps: ten repetitions of `key left` then `wait 3000`, ten shutter
presses over 30 s. Measured window 30 s. Captures the cost of one blind
press times ten: per-press lock acquire and release, per-press BLE
notification cost, and whether a press wakes the display. Flushed pixels
during the window are reported so review sees a press that redraws for
nobody.

**intervalometer-5-frames.** Initial state: connect prologue with the
intervalometer seeded to count 5, interval 2 s, no bulb. Steps: navigate to
the intervalometer page with the ui-screenshots key walk, start it, reset,
`wait 15000`. Measured window 15 s, which covers the 8 s run plus margin.
Captures: exactly five shutter commands in the report, the interval timer
fire pattern, residency between frames, and what the display does mid-run.

**gps-duty-standby-cycle.** Initial state: menu idle with GPS seeded on,
duty cycling on at a 5 s fix interval, `$PCAS12` standby enabled, fake NMEA
at 1 Hz. Steps: `wait 15000` so the first fix and the first standby entry
have happened, reset, `wait 15000`. Measured window 15 s, which contains at
least one full standby, wake, reacquire, fix, standby cycle. Captures:
receiver model residency per state, at least one standby entry, zero wake
flapping, and `NO_LIGHT_SLEEP` held only inside bursts.

**settings-navigation-sweep.** Initial state: fresh boot, main menu, no
camera. Steps: open every settings page once with the ui-screenshots key
walk, `wait 2000` on each page, back out to the menu after each, end on the
menu, then `wait 5000`. Measured window is the whole sweep, roughly 60 s.
Captures the page hygiene report: per-timer fires attributed to the
interval each page was open, so a timer that keeps firing after its page
closed is named in the report. This scenario catches the hidden page timer
class directly.

Growth rule, enforced in review: a feature that adds a power-relevant state
MUST add or extend a scenario in the same PR. A new page extends the sweep.
A new duty policy, link mode or sleep state gets a named scenario. A PR
that adds such a state without touching `sim/scripts/power/` is incomplete,
the same way a PR without its plans/NN update is incomplete.

## CI reporting

The sim CI job runs the suite on every PR that touches firmware or UI
paths: `src/**`, `include/**`, `lib/**`, `components/icons/**`, `sim/**`,
`sdkconfig.*`, `tools/power-model/**`. Docs-only PRs skip it.

The job maintains exactly one sticky PR comment, using the marker-comment
pattern the ui-screenshots workflow already implements: a hidden HTML
marker in the comment body, find the existing comment by marker, update it
in place, create it only when absent. The comment never stacks. Every push
rewrites it.

The comment contains, in order:

- A table of estimated mA per scenario per board class, S3 first. Columns:
  scenario, estimated mA, baseline mA, delta percent. A row past the 10%
  gate threshold is flagged: bold, with a FAIL marker in the delta column.
  Markdown tables carry no color, so the marker carries the signal.
- One lock-hold-time anomalies line: any lock whose total hold time moved
  past the threshold against baseline, or any lock still held at scenario
  end. Reads "none" when clean.
- A link to the full JSON report artifact for the run.

Consolidation. Three PR-facing sim reports exist or are planned: the
screenshot comment (fork PR #44, branch `feat/ui-screenshot-ci`), the
ui-audit overlap findings (fork PR #42), and this power table. They
consolidate into one combined sim-report workflow with one sticky comment
holding three sections, in order: power table, overlap findings, screenshot
links. One walker pass produces all three, per the pillar 5 rule. The
standalone ui-screenshots workflow on `feat/ui-screenshot-ci` is folded
into the combined job when this plan's phases land, and its marker comment
retires in the same PR.

The pattern generalizes. The firmware size-report job being added to main
CI follows the same sticky-comment shape: flash and RAM per release
environment, delta versus the base branch, its own marker and its own
comment. It has no sim dependency and does not join the combined sim
comment. Same reporting pattern, applied independently.

## Hardware calibration micro-task

One-time measurement run on the StickS3 using the existing battery
instrumentation: the console `status` command already reports battery
percent and voltage (`src/FurbleConsole.cpp:534-535`). Log voltage and
percent, EWMA-smoothed, over 30 to 60 minutes per state, in four states:

1. Menu idle, display on, 80 MHz.
2. Menu idle, display on, 160 MHz. The delta calibrates the frequency rows.
3. Display dimmed.
4. Display off. This is the light sleep floor.

Extended states, optional for the first table:

5. Connected idle at the fast versus the idle connection parameters, if a
   camera is available. Calibrates the per-event cost split.
6. GPS tracking versus `$PCAS12` standby deltas, with the unit attached.

The percent-per-hour slope times the nameplate cell capacity gives a rough
mA per state. Rough is fine: the gate is relative.

This run is not a prerequisite for anything. The table ships seeded from
datasheets and published measurements. The run upgrades entries from
`datasheet` and `estimated` confidence to `measured-local`, and it validates
the composed model against the one hard number already on file: the 3.3 mA
S3 connected-idle floor from
[00-hardware-experiments.md](00-hardware-experiments.md) Experiment A. If
the composed model and that floor disagree badly, the model is wrong and the
disagreement is the bug report.

## Phases

### Phase A: activity profiler and scenario suite

Depends on plan 28 (fork PR #39) merging. Counters in `sim/freertos.cpp`,
the flush path and an LVGL invalidation hook; the timer name table; the
`report` driver step; the five scenario scripts under `sim/scripts/power/`;
the JSON schema. Effort: one to two days.

### Phase B: sleep-opportunity estimator

`src/FurblePower.cpp` joins the sim compile set. `sim/shim/esp_pm.h` grows
the recording lock stubs. Hold-time accounting, histograms, residency
computation, report integration. Effort: one day.

### Phase C: DFS governor and PMIC stubs

The esp_pm stub gains the governor semantics and frequency residency
reporting. The M5PM1 behavioral stub with the wake quirk, the watchdog and
the sleep timer, plus the synthetic discharge curve. Correctness tests for
the plan 19 and plan 26 interactions. Effort: one to two days.

### Phase D: BLE duty and GPS receiver models

The BLE duty integrator over the plans/36 mock NimBLE layer. The GPS
receiver state model over the real `src/FurbleGPS.cpp` policy code. Both
feed duty integrals into the report. This phase depends on the plans/36
tier B mock existing; if it does not yet, the GPS half lands alone. Effort:
two to three days.

### Phase E: energy model and gate

Consume `tools/power-model/board-currents.yaml`, compute estimated mA per
scenario per board, write baselines, extend the plan 28 CI job with the
comparison and the artifact upload. Gate goes live with datasheet numbers.
Effort: one day.

### Phase F: hardware calibration, in parallel

The micro-task above. Independent of every other phase, needs only a
StickS3 and the debug console. Upgrades table confidence and validates the
composed model against the Experiment A floor. Effort: one bench session
plus soak time.

### Phase G: usage test suite and PR reporting

Depends on phases A and E. Grow the phase A sketch scripts into the nine
scenario suite from the Usage tests section, wire the sticky PR comment
from the CI reporting section, and consolidate: the power table, the
ui-audit overlap findings and the screenshot links merge into the one
sim-report workflow with one marker comment, and the standalone
ui-screenshots workflow from `feat/ui-screenshot-ci` is folded in and
retired. The growth rule enters review practice with this phase. The
firmware size-report job is independent of the sim and can land at any
time. Effort: one to two days.

## Risks

- **The model is believed too literally.** It reports what the firmware
  requested, not what silicon did. Mitigate: the report carries a banner
  line stating the numbers are relative estimates, and the gate compares
  sim against sim, never sim against hardware.
- **Table drift.** Datasheet numbers age and confidence tags rot. Mitigate:
  the schema forces a source per entry, and the Experiment A floor check in
  phase F pins the composed model to one measured truth.
- **Baseline churn.** If reports are not deterministic the gate becomes
  noise. Mitigate: everything runs on the virtual clock, and verification
  requires two runs to produce byte-identical JSON before the gate turns on.
- **Threshold tuning.** Too tight flags noise, too loose misses leaks.
  Mitigate: start as a soft failure that uploads artifacts, make it
  blocking after a few weeks of stable reports, same path plan 28 took for
  golden images.
- **Three walkers.** PRs #39, #42 and #44 each carry scripted navigation.
  Without consolidation this plan adds a fourth. Mitigate: the consolidation
  rule in pillar 5, enforced at review.
- **More of the tree compiles twice.** Power and the mock BLE layer join
  the sim build. A firmware-only change can break the sim build. Mitigate:
  the sim builds in CI on every push, so the break is caught with the
  change, the same trade plan 28 accepted.
- **A stub models the quirk wrong.** The PMIC stub is a model of documented
  behavior, and a model can be wrong in the same direction as the code it
  tests. Mitigate: every stub behavior cites the hardware finding it
  encodes, and hardware-owed walks stay on the books.
- **Scope growth in the subsystem models.** A receiver model can absorb
  weeks. Cap it: four GPS states, one penalty curve, one BLE duty integral,
  no RF, no protocol timing.

## Verification

- The scenario suite runs headless on macOS and Ubuntu, and two runs on the
  same machine produce byte-identical JSON.
- Reintroduce the RESYNC leak (revert `a16046a` locally): connected-idle
  residency collapses and the gate fails.
- Add an unconditional `lv_label_set_text` to a periodic timer: invalidated
  area per second jumps and the gate fails on the idle scenarios.
- Resume the diagnostics timer without opening its page: the per-timer fire
  report names it.
- Pin the fast connection profile after a shutter press: the BLE duty
  integral steps up and the gate fails.
- Disable standby entry in the GPS duty policy: the receiver model shows
  tracking current across the whole scenario.
- The composed model's connected-idle estimate for the S3 is compared
  against the 3.3 mA Experiment A floor after phase F. The ratio is
  documented. It is not a gate.
- The five release environments build unchanged, and no release binary
  contains any sim symbol. Docs and sim-only changes cannot regress
  firmware.

## Relationship to other plans

- [28-emulator.md](28-emulator.md) (fork PR #39) is the hard prerequisite.
  This plan instruments its shim, extends its driver and its CI job.
- [29-virtual-test-rig.md](29-virtual-test-rig.md) established the fake
  UART plus real `FurbleGPS.cpp` decision this plan's GPS model relies on,
  and shares the scripted navigation infrastructure.
- [36-camera-test-harness.md](36-camera-test-harness.md) provides the mock
  NimBLE layer the BLE duty model runs against. Shared, not duplicated.
- [06-power-module.md](06-power-module.md) built the counted, named lock
  API that makes the sleep estimator attributable.
- [08-scan-tuning.md](08-scan-tuning.md),
  [09-reconnect-backoff.md](09-reconnect-backoff.md),
  [10-adaptive-conn-params.md](10-adaptive-conn-params.md) and
  [11-adaptive-tx-power.md](11-adaptive-tx-power.md) define the policies
  the BLE duty model integrates.
- [12-display-off.md](12-display-off.md) and
  [13-auto-off-low-batt.md](13-auto-off-low-batt.md) define the dimmed and
  off scenarios.
- [14-gps-pcas.md](14-gps-pcas.md), [15-gps-power.md](15-gps-power.md) and
  [18-gps-motion.md](18-gps-motion.md) define the GPS duty policies the
  receiver model scores.
- [19-interval-deep-sleep.md](19-interval-deep-sleep.md) and
  [26-pm1-watchdog.md](26-pm1-watchdog.md) interact through the PMIC; the
  behavioral stub makes that interaction CI-testable.
- [00-hardware-experiments.md](00-hardware-experiments.md) supplies the
  Experiment A floor and the Experiment B cold start penalty.
- [02-battery-display.md](02-battery-display.md) built the battery
  instrumentation the calibration micro-task reads.
- The ui-audit walker (`tools/ui-audit.md`, fork PR #42) and the screenshot
  CI job (fork PR #44) share the walker infrastructure. One walker,
  consolidated.

## References

Read from source, at `f455b0b` on fork `master` unless noted:

- `include/FurblePower.h` and `src/FurblePower.cpp`: the three counted
  locks (`NO_LIGHT_SLEEP`, `CPU_FREQ_MAX`, `APB_FREQ_MAX`), named, with
  static owner strings on every acquire and release.
- `src/FurbleGPS.cpp`: `beginResync` acquires the power lock at `:557`; the
  burst window, `$PCAS` configuration and duty cycle states the receiver
  model scores.
- `src/FurbleUI.cpp`: the ten LVGL timers cited in the motivation.
- `src/FurbleConsole.cpp:534-535`: battery percent and voltage in the
  `status` command, the calibration instrument.
- Commit `a16046a`: the RESYNC lock leak fix, the type specimen for the
  leak class.
- `feat/28-emulator` at `7487f64`: `sim/build.sh` compiles the real
  `FurbleUI.cpp`, `FurbleGPS.cpp` and `FurbleSettings.cpp`; `sim/clock.h`
  is the virtual clock; `sim/driver.cpp` implements `wait`, `key`,
  `capture`, `exit`; `sim/shim/esp_pm.h` is currently a no-op
  `esp_pm_configure` and is the file pillar 2 grows.
- Commit `893f657` on `feat/ui-text-scaling`: the ui-audit walker and
  `tools/ui-audit.md`, one of the three walkers to consolidate.
- The pinned ESP-IDF 5.x `esp_pm` component source: the governor semantics
  the DFS stub implements.
- `tools/power-model/board-currents.yaml`: the current table, seeded in
  parallel from datasheets and published measurements, schema described in
  pillar 4.
