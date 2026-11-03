# 294 - bound GPS acquisition for a silent receiver

## Motivation

A stored fixed baud skips the autobaud ladder and publishes the receiver as
`PRESENT` immediately. If nothing answers, the GPS task stays in `ACQUIRING`
forever: only a valid sentence leaves the state, so `NO_LIGHT_SLEEP` stays held
and the Grove 5 V rail stays on. The same hole exists after an autobaud lock if
the receiver goes silent. On the StickS3 this is a fault path worth up to about
37 mA until the user toggles GPS.

The normal autobaud ladder already reaches `ABSENT`; this plan covers the fixed
baud and lock-then-silence paths.

## Implementation state

- `enable()` and `onProbeLocked()` arm a 30 s acquisition deadline and record
  the valid-sentence counter at entry.
- `beginBurst()` clears the deadline as soon as a valid sentence arrives.
- `cycleWait()` sleeps until the acquisition deadline instead of polling.
- On expiry, if the valid-sentence counter has not moved, `serviceCycle()`
  releases the cycle lock and calls the existing `onProbeFailed()` path: drop
  the rail, release the power lock, publish `ABSENT`, and schedule the single
  60 s retry.
- The absent log no longer says "autobaud" when a fixed baud timed out.
- Master carried a separate 6 s fixed-baud probe (`m_FixedBaudProbe`). The
  rebase folds that path into the same 30 s acquisition deadline: one timer
  covers a stored fixed baud, an autobaud lock that goes silent, and the
  existing degraded retry, so two expiries cannot race. The richer simulator
  scenario replaces the shorter master fixture and still asserts recovery.

The timeout is deliberately 30 s: NMEA normally starts immediately after power
up, so this is generous for a real receiver while still bounding a silent one.
Any valid sentence cancels the deadline, so line noise cannot keep the state
alive.

## Validation

- `e2e/gps-fixed-baud-absent.txt`: fixed 9600 baud, no receiver, asserts
  `gps.receiver absent`, `power.no_light_sleep 0`, the absent UI row, and
  recovery through the 60 s retry when a receiver appears.
- Full host CTest: 140/140 after the rebase onto the static-analysis gate.
- Static analysis: PASS, including the GPS time-loop bound and the two string
  append fixes the rebased file now sees.
- `gps-fixed-baud-absent`, `gps-no-receiver`, `gps-degraded-recovery`,
  `gps-duty-no-fix`, `gps-autobaud-ladder`, and `gps-monhw-poll` pass on the
  M5StickS3 build. The rich recovery scenario is certified on StickS3 and Core;
  the 80x160 layout has no GPS cycle row, so `gps-fixed-baud-absent-stickc`
  keeps the panel-independent absent and power-lock assertions there.

## Remaining gate

Bench E33: enable GPS with no unit at a fixed 9600 baud, confirm the lock is
released and the rail drops, then plug the unit in and confirm the retry
recovers it. A receiver that is present but unusually slow to emit its first
sentence must still be accepted; the valid-sentence criterion is the guard.
