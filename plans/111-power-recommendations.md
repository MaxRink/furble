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
call.

All milliamp figures are the plan 63 energy-model estimates for the S3 from the
audit unless noted as measured. The one cross-checked point is connected-idle:
3.6 mA modeled against 3.3 mA measured (`plans/00-hardware-experiments.md`).

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
- The audit's 3.6 mA is a model estimate. It matches the 3.3 mA measured floor,
  but the measurement was at stock connection parameters and screen-on. The
  connected-plus-screen-off-plus-sleep floor has never been measured directly.
  Section 3 closes that gap.

### Recommendation

Do not flip the `SLEEP_CONN` code default now. Ship the win through plan 77
instead:

1. Land plan 77 (opt-in Battery Saver). It forces `SLEEP_CONN` on the S3 as
   part of the bundle, so a user who wants the runtime gets it with one switch
   and today's out-of-the-box behaviour is unchanged. This is the low-risk path
   and it is ready now.
2. Run the section 3 measurement and a 30 minute Fujifilm soak with the profile
   on. Confirm no disconnects, no reconnect log entries, and acceptable first
   shutter latency after a 30 s idle gap (the plan 07 verification steps).
3. Only after that soak, consider defaulting `SLEEP_CONN` on for the S3
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

The audit numbers are model estimates. The console `power` command makes the
real device self-report, so the model can be validated without an external
meter. This protocol is what turns "3.6 mA modeled" into "measured on my S3".

### Tooling

- Flash the console build: `m5stick-s3-debug` (release builds do not expose the
  console). Instantaneous `power stats` reads are capturable over USB while the
  device is plugged in. Drain runs (`power log`) must be unplugged, because
  charging masks the draw, so those need the user to unplug and later replug to
  read the captured log.
- `power stats` prints the current sample: voltage_mv, level_pct, current_ma,
  and the EWMA current.
- `power log <seconds>` starts a periodic logger. Columns:
  `timestamp_s,voltage_mv,level_pct,current_ma,current_ewma_ma,` plus a computed
  drain percent per hour. `power log off` stops it.
- The `furble-worktrees/serctl.py` serial driver can interleave console
  commands with `sleep:N` delays and capture timestamped logs, which suits an
  unattended drain run.

### States to capture

For each state, record `current_ewma_ma` from `power stats` after the reading
settles (about 20 to 30 s), and separately run an unplugged `power log 30` for
several minutes to get the drain-percent-per-hour that the audit asked for.
Cross-check against the model column.

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
bench is what validates both the model and plan 77 before any default is
touched.

### Procedure per state

1. Boot with the intended settings. For the saver pair, toggle Battery Saver in
   Settings > Power and reboot (the display bundle is read at UI init).
2. Reach the target state (connect the camera, blank the screen, attach GPS).
3. Plugged in: `power stats`, wait for the EWMA to settle, record it. Repeat
   three times.
4. Unplugged: `power log 30`, leave it for at least 10 minutes untouched so the
   inactivity and any auto behaviours are exercised, replug, read the log,
   record the median drain percent per hour.
5. Compare measured against the model column. A gap larger than model tolerance
   at state 4 (the validated point) is the signal to investigate before
   trusting any default flip.

### What the measurement gates

- State 4 confirming near the 3.6 mA floor is the precondition in section 1 for
  considering an S3 `SLEEP_CONN` default flip.
- State 5 minus state 2 isolates the GPS term (modeled 23 mA), the input to any
  future GPS duty rework (audit action 7).
- State 3 minus state 4 is the raw value of `SLEEP_CONN` on this specific cell,
  which is the number to quote in release notes rather than the model estimate.

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
