# 297 - GPS improvement roadmap after the deep review

## State

Two deep reviews ran against master `a3f29dc1`/`8054d183`: one on receiver
power and one on accuracy, fix quality, and protocol. This plan records what
they found, what is already fixed, and the sequence for the rest.

Implemented on master:

- **PR #325 / plan 293** corrects the `CFG-NAVX` `navSystem` offset (13, not 6)
  and the `MON-HW` offsets. Constellation selection may have been a silent
  no-op.
- **PR #326 / plan 294** bounds the `ACQUIRING` state so a silent fixed-baud
  receiver cannot hold `NO_LIGHT_SLEEP` and the 5 V rail forever.
- **PR #328 / plan 296** carries the companion phone's horizontal accuracy into
  the GPS fix.

### Fixed after the indoor hardware check

`gps` reported `fix=true` with `satellites=0` and `fix_state=none` long after
the last sentence. TinyGPSPlus keeps the last valid location and quality
indefinitely, so the status needed its own age bound. `getStatusSnapshot()`
now requires `location.age() < MAX_AGE_MS` for `fix`; the indoor baseline then
reported no fix once the 30 s freshness window and the Fix Hold window ended.
**PR #356** closes the remaining two-read race: `GPSFreshness::evaluate()`
samples the age once and returns both the verdict and the reported age, and the
host test counts the reads so a regression back to two calls fails.

## Verified findings still open

### 1. The duty state machine is unreliable before 30/60 s is attempted

- A duty wake outside the 50 ms prediction window calls `beginResync()`, which
  holds `NO_LIGHT_SLEEP` for a full duty interval. A late/early wake can cost
  5-15 s today and 60 s after a range extension.
- `beginWindow()` uses `max(5 s, duty/2)` as the wake timeout, so 15 s duty can
  hold the lock 7.5 s and 60 s duty 30 s on a failed wake.
- With `GPS_RATE=0` (the default), a successful standby wake sets the expected
  interval to zero and enters `DEGRADED` every cycle.
- Duty re-arm depends on a fresh valid UART fix; in marginal sky the receiver
  can silently stay always-on.

This is the top code fix because it decides whether the existing 5/10/15 s
standby saving is actually delivered. It is host/sim testable and needs no new
hardware to implement, although G-05 still decides the real current.

### 2. There is no fix-quality gate

`wiredFixIsFresh()` accepts a one-satellite, HDOP-20 fix as long as GGA quality
is non-zero and the age is under 30 s. HDOP is parsed but never used. Companion
accuracy was dropped until 296; it now reaches `external_fix_t` but no threshold
uses it. The default sentence set still includes GSA, but the fix path ignores
GSA's fix-type field; with pruning enabled GSA is not requested at all. A 2D fix
is therefore indistinguishable from 3D during normal geotagging.

The proposed rule is a fixed `satellites >= 4 && hdop_valid && hdop > 0 &&
hdop <= T` gate, plus the companion accuracy when valid. It must fall through
to fix hold, not send a bad point. The threshold needs an outdoor bench run;
the code and simulator fixtures can land first behind the fixed rule.

### 3. Rate and baud are independent

Unpruned output is measured at 710 chars/s. A 9600 8N1 line carries 960 B/s,
so 1 Hz is already 74 percent and 2 Hz is over capacity. The rate roller and
baud roller are independent, and pruning is optional. The next PR should clamp
or auto-enable pruning when the combination cannot fit.

### 4. Motion adaptive saves zero

`isStationary()` has no consumer. The detector is implemented, the UI exposes
`GPS_MOTION`, and the policy is described, but nothing changes the receiver
state. This is a bench-gated policy change, not a missing detector.

### 5. No fix-aware backoff

A receiver that cannot fix stays at full current indefinitely; only the CPU
power lock backs off in `DEGRADED`. Backing off the receiver after repeated
no-fix windows is the largest real-world saving for indoor users, but it is
opt-in and bench-gated by G-04/G-07.

### 6. Every GPS setting write restarts the receiver

`reloadSetting()` runs for `GPS_HOLD`, `GPS_EXTRAP`, `GPS_POWER`, and
`GPS_DUTY`, not only receiver-affecting settings. Changing a purely local policy
can force a re-acquisition, and in standby can hold the lock for the remaining
interval. Split `reloadPowerSetting()` / `reloadFixSettings()` from the
receiver reload.

### 7. Protocol version skew

Plan 32 cited CASIC v3.6 for claims that only hold for v3.6. CASIC v4.2 adds
`$PCAS12` (in use), `$PCAS15` for SBAS and QZSS masks, `$PCAS60`, and GST
accuracy. The unit's firmware version is not recorded because `$PCAS06` is not
implemented. Add a console-only `$PCAS06` identity probe before writing any
v4.2 feature. Do not ship a blind `$PCAS15` mask: a wrong mask can disable GPS.

## Ranked sequence

| Order | PR | Value | Risk | Gate |
| --- | --- | --- | --- | --- |
| 1 | Duty state machine correctness | Makes the existing standby saving real; prerequisite for 30/60 s | Medium | Host/sim now; G05 standby current |
| 2 | Fix-quality gate + honest age display | Stops bad geotags; uses companion accuracy | Medium | Host/sim now; outdoor threshold |
| 3 | GPS defaults: Auto baud and RMC+GGA prune | Fixes a fresh install at the v1.1 unit's 115200 and cuts 710 to 91 chars/s | Low | Host/sim; E29/E32 |
| 4 | Split settings reload | Avoids re-acquisition on hold/power/duty writes | Low | Host now |
| 5 | Rate/baud consistency guard | Prevents a configuration the UART cannot carry | Low | Host settings test |
| 6 | Fix-aware backoff (P-084) | Modeled ~31 mA module input current (Unit GPS v1.1 at 5 V) while a no-fix receiver stays powered; the saving depends on the backoff duty | Medium | Opt-in now; G07 degraded backoff |
| 7 | Motion consumer (P-085) | 15-22 mA while stationary, modelled | High | Plan 18 motion-pattern gate + E38; steady motion may read stationary |
| 8 | 30/60 s duty with hold | Modeled ~19 mA of the 23 mA AT6558 chip proxy; the in-circuit term is G05 | Medium-high | After 1 and 3; G05 current + G-06 long-duty experiment |
| 9 | Receiver identity and GST | Unlocks version-gated accuracy work | Medium | Bench `$PCAS06` first |

## Bench gates that decide the rest

Two documents use similar IDs: `docs/power/gps-imu.md` numbers optimization
opportunities as `G-05`/`G-06`, while `docs/power/measurement-plan.md` numbers
bench tests as `G05`/`G06`. The roadmap names the document whenever the
distinction matters.

- **G05** (measurement-plan) at B1 and B2: in-circuit standby current,
  wake-to-first-fix, wake jitter, active/standby residency at 5/10/15 s. The
  60 s arm needs the firmware change in row 8.
- **G-06** (gps-imu): the 30/60 s duty-with-hold experiment and an early-wake
  command. Its current is measured by G05; the acceptance gate is the same
  standby residency plus a held position that stays within the configured
  accuracy.
- **G06** (measurement-plan): five `PCAS12` rail cycles at 15 s duty; re-fix
  stays within 90-130 s and charge per cycle within 20 percent of the sum of
  tracking and cold-start windows. This decides whether rail cycling is viable
  at all.
- **G07** (measurement-plan): receiver backoff leaves the lock released and the
  rail in the expected state.
- **E29**: a fresh NVS boot fixes with no user action; 9600 still locks; an
  absent unit reaches absent.
- **E32**: prune reduces character rate without losing the fix.
- **E38 + plan 18 gate**: motion state is correct at rest and through the plan
  18 motion pattern. The detector is a heuristic, so a steady cruise may read
  stationary; the gate bounds false-stationary windows and checks the first
  fix and geotag after each transition, rather than claiming none.
- Outdoor threshold calibration for the quality gate.

## Bottom line

The installed GPS transport is stronger than the policy on top of it. The
largest immediate code wins are the duty state-machine fixes and the quality
gate, in that order. The largest bench decision is G05: without the in-circuit
standby current and wake-to-first-fix, neither 60 s duty nor the receiver
backoff can be promoted from opt-in.
