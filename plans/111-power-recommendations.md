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
- Flash `m5stick-s3-debug` for synchronized firmware state and event logs. The
  console's `power stats` and `power log` values are supporting state evidence,
  not current measurements on this board.
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

## References

- `plans/98-power-optimization-audit.md` for the model table, the what-if runs,
  and the ranked action list this document draws on.
- `plans/77-battery-saver-profile.md` for the opt-in toggle that ships the win.
- `plans/07-ble-sleep.md` for the `SLEEP_CONN` design, the per-vendor stability
  risk, and the soak verification steps.
- `plans/00-hardware-experiments.md` for the 3.3 mA measured connected-idle
  floor and the confirmed absence of a 32 kHz crystal.
- `plans/15-gps-power.md` for the S3-only GPS burst power lock that keeps
  `SLEEP_CONN` off the other boards.
