# 111 - Power optimization recommendations

## Purpose

The power audit in `plans/98-power-optimization-audit.md` enumerated every
merged power mechanism and the cost of its default. Plan 77 turns the safe
bundle into one opt-in switch. Three questions were left for the maintainer to
decide because they are policy, not code:

1. Should `SLEEP_CONN` (light sleep while connected) default on, given a 12x
   runtime win against a per-vendor BLE stability risk.
2. Is a dedicated power-optimized build environment per battery board worth it,
   or does the runtime Battery Saver toggle already serve that need.
3. How to validate the audit's model numbers on the attached M5StickS3 before
   any default is flipped.

This document is a recommendation only. It does not change any setting default
in code. Every default change below is explicitly marked as the maintainer's
call. The 2026-08-30 simulator review found that the underlying connection,
scheduler, and electrical models are incomplete. Plan 158 must land before a
sim result is used as a hardware or release decision. The figures below are
advisory hypotheses, not validated device measurements.

All milliamp figures are the plan 63 energy-model estimates for the S3 from the
audit unless noted as measured. The 3.6 mA modeled value and 3.3 mA value in
`plans/00-hardware-experiments.md` were collected under different conditions,
so their numerical proximity is not a model validation.

## 1. The `SLEEP_CONN` default decision

### The number

Connected with the screen off:

| `SLEEP_CONN` | est mA | S3 250 mAh runtime |
|---|---|---|
| off (today's default) | 43.6 | about 6 hours |
| on | 3.6 | about 69 hours |

This is the single largest addressable number in the audit. With the screen on
it changes nothing, because the display APB lock pins the MCU active either way.
The win only appears once the screen blanks, which is why plan 77 bundles
`SLEEP_CONN` with a real screen-off, not Dim.

### The risk

Plan 07 flagged the risk before the feature merged, and it is real:

- Modem sleep plus automatic light sleep adds wake latency to every BLE event.
  A camera with a short supervision timeout can drop the link, and the first
  shutter after an idle gap can be slower.
- This is per-vendor. Only Fujifilm is available for hardware soak. Sony, Nikon,
  Canon, Ricoh and Pentax connection behaviour under modem sleep is unverified.
- `SLEEP_CONN` is S3-only for a reason. On the four ESP32/AXP192 boards the
  GPS burst power lock is compiled out (`FURBLE_M5STICKS3` guard, plan 15), so
  light sleep during a connection would kill the GPS UART clock and drop NMEA
  sentences. The setting is correctly hidden on those boards today.
- The audit's 3.6 mA is a model estimate. It is numerically close to a 3.3 mA
  value measured under different connection and display conditions. The
  connected-plus-screen-off-plus-sleep floor has never been measured directly.
  Section 3 defines the measurement needed to close that gap.

### Recommendation

Do not flip the `SLEEP_CONN` code default now. Ship the win through plan 77
instead:

1. Keep plan 77's Battery Saver opt-in. It forces `SLEEP_CONN` on the S3 as
   part of the bundle, so today's out-of-the-box behaviour is unchanged. Treat
   the modeled runtime benefit as experimental until the gates below pass.
2. Run the section 3 external measurement and repeated per-vendor camera soaks
   with the profile on. Confirm no disconnects, no reconnect log entries, and
   acceptable first shutter latency after a 30 s idle gap. A Fujifilm-only soak
   cannot establish a safe default for every supported camera family.
3. Only after calibrated measurement and exact-profile camera evidence,
   consider defaulting `SLEEP_CONN` on for the S3
   specifically, with a documented opt-out in Settings > Power. Keep it opt-in
   on all other boards. This stays the maintainer's decision; the soak evidence
   is the gate, not this document.

Rationale: the profile captures the entire win for the users who ask for it,
with zero regression risk to the default install, while the raw default stays
conservative until per-vendor soak evidence exists. Flipping the default is a
one-line change to the `save<bool>(false)` group in `src/FurbleSettings.cpp`
when the evidence is in; there is no need to take that risk before it is.

## 2. Power-optimized build profile vs the runtime toggle

### The requirement

A power-optimized build for every battery platform is on record. The question
is whether that means a dedicated `-power` PlatformIO env per board that ships
different setting defaults, or whether the runtime Battery Saver toggle already
satisfies it.

### Assessment

The two shapes and what each costs:

- Runtime toggle (plan 77). One switch, override semantics, no stored setting is
  clobbered, works on every board today, zero new CI envs. The user opts in from
  the menu. Cost: the user has to find and flip one switch.
- Build env per board (`m5stick-s3-power`, `m5stick-c-power`, and so on). Ships
  the power defaults baked in, so a freshly flashed device is already saving
  without touching a menu. Cost: five more build envs, five more CI jobs, five
  more release artifacts and web-flasher manifest entries to maintain, and a
  second axis of "which build am I running" support confusion. It also splits
  the default-behaviour question per artifact instead of settling it once.

The audit already weighed these and concluded the runtime toggle "serves users
better and needs no extra CI envs"; the build profile "serves the web flasher".
Plan 77 implements the toggle for exactly that reason.

### Recommendation

Take the lighter path. The runtime Battery Saver toggle (plan 77) is the
power-optimized profile for every battery platform. Do not add five `-power`
build envs.

If a pre-configured artifact is later wanted for the web flasher, so a user can
flash a battery-first build without opening the menu, do it as a single
build-time default macro rather than five hand-maintained envs. Sketch:

- Add one flag, for example `-DFURBLE_POWER_DEFAULTS`, consumed only by the
  default-value switch in `src/FurbleSettings.cpp`. When defined, the defaults
  for `BATTERY_SAVER` (or the individual bundle members) load true instead of
  false. No new source paths, no behaviour change to any existing env.
- Expose it through one extra web-flasher manifest variant per battery board
  rather than a full parallel env matrix, or gate it behind an existing debug
  or release distinction so the CI job count does not grow.
- Leave `CPU_FREQ_DEFAULT` alone unless a measurement shows a win: under DFS the
  160 MHz default already costs nothing at idle (audit, plan 01), so dropping it
  to 80 in a power build buys little and risks UI responsiveness.

Net: ship the toggle now, keep the build-time macro as a documented future
option for the flasher, and do not grow the env matrix for it.

## 3. On-device measurement protocol (M5StickS3)

The audit numbers are model estimates. M5StickS3's M5PM1 has no battery-current
backend in M5Unified and no documented current register. The console can report
voltage, charge state, coarse battery level, and firmware state, but not the
instantaneous current needed to calibrate this model. A calibrated external
measurement is mandatory before claiming accuracy or flipping a default.

### Tooling

- Use a calibrated inline power analyzer at one documented electrical boundary,
  preferably the complete device input with charging disabled. Record analyzer
  model, firmware, range, sample rate, supply voltage, cable, battery presence,
  and whether USB data is attached.
- Measure current on the release image, `m5stick-s3`. The debug profile is a
  debug build with verbose logging, the console, the debug task watchdog, PM
  profiling, FreeRTOS run-time stats, heap tracing, the stack protector and
  the LVGL performance monitor (`sdkconfig.debug`). The monitor alone keeps the
  LVGL refresh timer running to count frames. Its draw is not the shipped
  device's draw.
- Use `m5stick-s3-debug` only for state correlation: a separate run with the
  same setup gives synchronized firmware state and event logs. The console's
  `power stats` and `power log` values are supporting state evidence, not
  current measurements on this board. Never mix a debug-image current into a
  release comparison.
- Capture analyzer samples and the firmware log from one shared timestamp or a
  visible marker event. Retain raw samples, not only a displayed average.
- Use the same supply, display brightness, radio peer, connection parameters,
  GPS rail, room conditions, and soak duration for baseline and candidate.

### States to capture

For each state, record external input current after the state settles, retain
the full trace, and compute mean, median, p95, wake-event energy, and confidence
interval over the same window. Use `power log 30` only to correlate voltage,
level, charge, and firmware state. Cross-check against the advisory model
column at the same electrical boundary.

| # | State | Setup | Model est mA | Audit source |
|---|---|---|---|---|
| 1 | Menu idle, screen on | Disconnected, on a menu page | 81.2 | menu-idle-30s |
| 2 | Connected idle, screen on | Fujifilm connected, idle | 84.5 | connected-idle-30s |
| 3 | Connected, screen off, saver OFF | Connected, blank the screen | 43.6 | what-if |
| 4 | Connected, screen off, saver ON | Same, Battery Saver on | 3.6 | what-if, headline |
| 5 | Connected + GPS active | GPS unit attached, tracking | 107.5 | connected-gps-active-30s |
| 6 | Screen off, disconnected | Idle, screen blank | 0.25 | screen-off-30s |

States 3 and 4 are the headline pair. The expected result is the
connected-plus-screen-off draw dropping from about 43.6 mA toward the 3.6 mA
light-sleep floor when Battery Saver is on. Reproducing that end to end on the
bench is a prerequisite to evaluating both the model and plan 77. It does not
validate the model without a calibrated current measurement.

### Procedure per state

1. Boot with the intended settings. For the saver pair, toggle Battery Saver in
   Settings > Power and reboot (the display bundle is read at UI init).
2. Reach the target state (connect the camera, blank the screen, attach GPS).
3. Start the external capture and firmware log, wait for the state to settle,
   then retain at least 10 minutes. Repeat each state at least three times.
4. Annotate every display, GPS, reconnect, shutter, and connection-parameter
   transition. Reject a run that silently changes state.
5. Compare external measurements against the model at the same boundary. A gap
   larger than model tolerance
   at state 4 (the candidate headline point) is the signal to investigate before
   trusting any default flip.

### What the measurement gates

- A stable state 4 with calibrated current, acceptable shutter latency, and no
  exact-profile camera regressions is a precondition for considering an S3
  `SLEEP_CONN` default flip.
- State 5 minus state 2 isolates the GPS term (modeled 23 mA), the input to any
  future GPS duty rework (audit action 7).
- State 3 minus state 4 is the measured value of `SLEEP_CONN` for this exact
  hardware, firmware, camera, and setup. Do not generalize it without another
  matching trace.

## 4. Idle power fixes (2026-11-13)

The 2026-11-13 power re-analysis re-ran every power-gate scenario and the
synthetic accounting what-if models. It found idle work that needs no hardware
evidence to remove. Branch `perf/power-idle-fixes` implements those findings.
All numbers below are simulator evidence on the StickS3 build, not current
measurements.

### Implementation state

"Before" is master at `e66e8807` and "after" is this branch, both built as the
plain StickS3 simulator and run with the certified power-gate scenarios. The
legacy model charges no work per timer fire, UI pass or pixel, so its mA
column barely moves. The activity counters and the synthetic accounting
what-if models carry the evidence. The what-if models charge 20, 75 or 300 us
per UI pass (`ui_poll_active_us_per_cycle`) and 100 us per timer fire. They
are uncalibrated.

- N2, done. The intervalometer state, count and countdown labels and the bulb
  countdown use the changed-check helpers. `intervalometer-5-frames`
  invalidated 774,840 px before and 574,212 after, and flushed 268,106 px
  before and 192,992 after.
- N3, done. `sleepDisplay()` disables LVGL invalidation and `wakeDisplay()`
  re-enables it before the full redraw. LVGL counts the enable calls; the
  `m_DisplayOff` guards keep them balanced. `blind-remote-shutter` invalidated
  534,151 px with the panel off before and 0 after. `screen-off-flush-gate`
  asserts 0 invalidated px and 0 suppressed flushes with the panel off, and
  `camera-pairing-notouch` asserts the wake press redraws.
- P-070, done. With the panel off the UI loop sleeps for
  `lv_timer_handler()`'s next-timer value, clamped to 5 to 20 ms; the panel-on
  loop keeps 5 ms. LVGL reads input every 33 ms and the M5PM1 button poll is
  50 Hz, so no edge is lost. The 1 s M5PM1 watchdog feed, the CrashLog and
  MemoryPressure ticks and `serviceRequests()` still run every pass, at most
  20 ms apart. Shutter, bulb, intervalometer, GPS service and gesture work are
  LVGL timers and wake the loop when due. UI passes in 30 s:

  | Scenario | Before | After |
  |---|---|---|
  | screen-off-30s | 6,002 | 1,844 |
  | gesture-idle-30s | 6,002 | 2,356 |
  | blind-remote-shutter | 6,022 | 1,896 |

  Accounting what-if, modeled mA before and after:

  | Scenario | 20 us | 75 us | 300 us |
  |---|---|---|---|
  | screen-off-30s | 0.480 to 0.369 | 0.919 to 0.504 | 2.717 to 1.056 |
  | gesture-idle-30s | 0.679 to 0.576 | 1.119 to 0.749 | 2.917 to 1.454 |
  | blind-remote-shutter | 4.021 to 3.909 | 4.461 to 4.046 | 6.259 to 4.606 |

  The legacy model now charges only the historical 5 ms quantum after a timer
  fire, so a 20 ms pass is not billed as awake. Its `screen-off-30s` figure
  still rose from 0.511 to 0.550 mA: the run ends up to one 20 ms pass later and
  the model charges that tail as awake. Two e2e scenarios were re-paced for
  the longer pass. `imu-gesture-wake-off` holds each tap across a 1 ms wait,
  which passes at five different phases. `gps-hold-bound` runs out its bound
  a second earlier.
- N4, done. The companion and WebUI tasks unsubscribe from the task watchdog
  and block on their task notification while disabled, instead of waking 10
  and 2 times per second. `enable()`, `reloadSetting()` and the WebUI hold and
  release timers give the notification. `boundary-companion-gatt` asserts the
  disabled task blocks and that re-enable wakes it. `stack_budget.py` passes:
  companion 2,560 of 3,072 bytes, webui 2,928 of 3,584 on the StickS3.
- R1, done. Reconnect advertising runs at 100 ms for 60 s after enable, boot,
  any disconnect or a button wake, then at 1022.5 ms. The bounded fast window
  cuts the long-run advertising duty about tenfold (`docs/power/ble.md`). The
  `companion-advertising` host test covers the cadence state machine, and
  `boundary-companion-gatt` checks that re-enable starts fast. Untested on iOS
  and macOS; see the hardware-gated list.
- N5, skipped. `CrashLog::update()` and `MemoryPressure::tick()` each sample
  the internal heap once a second on independent timers. Sharing one sample
  would change both APIs, both UI loops and the crash-log tests to save one
  heap walk per second, which the model cannot measure. That is not small and
  clean.
- Power gate, done. `compare.py` also fails timer-fire and invalidated or
  flushed pixel drift beyond 25 percent plus an absolute slack, with
  per-scenario opt-outs in `tools/power-model/activity-optout.json`. The
  committed baselines were already stale against master (for example
  `connect_timer` 240 against 60 fires), and the guard caught that. The
  baselines are regenerated from this branch. Run against them, master's
  `blind-remote-shutter` passes the mA check at -0.55 percent and fails the
  activity check on its 534,151 invalidated px.

### Hardware-gated (2026-11-13)

These were deliberately not changed. Each needs a release-image current
measurement (section 3) or a hardware soak first.

- Setting defaults: `CONN_SAVER`, `AUTO_OFF`, `SCAN_MODE` and brightness keep
  their current defaults. The model has no interval-scaled radio term and no
  effective-brightness term (plan 98, model gaps 3 and 4), so it cannot rank a
  default flip. Section 1 already keeps `SLEEP_CONN` behind Battery Saver. The
  re-analysis showed connected-idle at 84.6 mA and menu-idle at 81.3 mA
  modeled, both dominated by the display and the APB-pinned MCU, which a
  default change to these settings does not move in the model.
- Display APB lock, P-071 and P-072. The lock pins the MCU at full APB while
  the panel is on or dimmed: screen-dimmed still models 50.6 mA against 0.51 mA
  with the panel off. Releasing it needs the E14 and E15 experiments in
  `docs/power/measurement-plan.md` (SPI clock, LEDC flicker, light-sleep
  residency) on each board.
- `cfg.pmic_button` (N1, `src/FurblePlatform.cpp`). `M5.update()` reads the
  PMIC power-button state on every UI pass while it is set. P-070 cuts the
  screen-off pass rate, which also cuts these reads, but turning the option off
  or throttling it changes power-button handling and needs hardware.
- GPS duty rework, P-086. The re-analysis kept `gps-duty-standby-cycle` at
  86.1 mA modeled with the GPS term unmeasured. The `$PCAS12` standby current
  and the rail-cycle cold start (about 108 s, `plans/00-hardware-experiments.md`)
  must be measured first (section 3, state 5, and plan 98 action 7). The
  matrix row asks for a 1 h live fix at 5, 10, 15 and 60 s duty.
- RTC calibration, P-062. Every sdkconfig sets
  `CONFIG_PM_LIGHTSLEEP_RTC_OSC_CAL_INTERVAL=1`, so the RTC clocks recalibrate
  on every light sleep, about 194 us each (inferred from Kconfig and the IDF
  source). The StickS3 has no 32 kHz crystal and runs
  `CONFIG_RTC_CLK_SRC_INT_RC`, so the BLE sleep clock and wake accuracy depend
  on that calibration. P-070 already cuts the screen-off sleep count, which
  shrinks this cost. A larger interval needs the 1/8/16/32 sweep and the
  wake-lateness and BLE soaks in the matrix row.
- P-070 latency and watchdog margin. The simulator proves the loop shape, not
  physical button latency or the M5PM1 feed margin; see the P-070 entry.
- R1 on Apple hardware. The 1022.5 ms fallback is untested on iOS and macOS.
  Commit `c0024761` moved to 100 ms because a 1 s interval was invisible to
  some macOS scan windows. Bench check: bond a Mac, let the 60 s window
  expire, then time the reconnect with and without a button wake.

## References

- `plans/98-power-optimization-audit.md` for the model table, the what-if runs,
  and the ranked action list this document draws on.
- `plans/implemented/77-battery-saver-profile.md` for the opt-in toggle that ships the win.
- `plans/implemented/07-ble-sleep.md` for the `SLEEP_CONN` design, the per-vendor stability
  risk, and the soak verification steps.
- `plans/00-hardware-experiments.md` for the 3.3 mA measured connected-idle
  floor and the confirmed absence of a 32 kHz crystal.
- `plans/15-gps-power.md` for the S3-only GPS burst power lock that keeps
  `SLEEP_CONN` off the other boards.
