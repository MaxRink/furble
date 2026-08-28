# 98b - power audit follow-up: GPS degraded lock recovery

## Goal

Fix one concrete product bug the plan 98 power audit surfaced: the GPS burst
windowed power management could latch a permanent NO_LIGHT_SLEEP lock with no
recovery path. This note records the root cause, the fix and its verification.
It is scoped to that single bug, not the wider plan 98 action list.

## Root cause

`src/FurbleGPS.cpp` ran the receiver on a burst windowed power cycle. Between
predicted bursts it released the NO_LIGHT_SLEEP lock and let the S3 light sleep,
which is where the power saving comes from. When it could no longer predict the
burst timing it called `enterPermanentLock()`, which set the state to
`PERMANENT_LOCK` and acquired the lock:

    void GPS::enterPermanentLock(void) {
      m_HavePrediction = false;
      m_WakeDeadline = 0;
      m_CycleState = cycle_state_t::PERMANENT_LOCK;
      acquirePowerLock();
    }

Both `serviceCycle()` and `cycleWait()` treated `PERMANENT_LOCK` as a no-op, so
once entered the state never left. The lock stayed held forever, the S3 never
light slept again, and the only exit was the user toggling GPS off and on.

The state was reachable whenever reception degraded: a failed interval
measurement (`finishMeasurement`), a duty cycle wake that returned no fix, a
burst wake deadline with no data, or a run of bad checksum bursts driving a
resync that could not re-predict. The plan 98 audit and the later Fable power
model both flagged it: about +37 mA held indefinitely on a 250 mAh S3, roughly
halving battery life after any spell of poor reception, for example indoors.

## Fix

`PERMANENT_LOCK` becomes a bounded, self recovering `DEGRADED` state.

- New pure policy `include/FurbleGPSPowerCycle.h` (`Furble::GpsDegradedRetry`):
  on entry it schedules a finite retry after an exponential backoff that starts
  at 10 s, doubles per repeated failure, and caps at 5 min. `reset()` clears the
  episode.
- `enterDegraded()` now releases the lock, logs a single WARN on entry, sets the
  `DEGRADED` state and schedules the retry. `cycleWait()` sleeps until the retry
  is due, so the S3 light sleeps through the backoff instead of holding the lock.
- When the backoff elapses, `serviceCycle()` re-acquires the lock and re-enters a
  time bounded acquisition probe (`m_ProbeDeadline`). A clean burst recovers the
  duty cycle and drops the lock; a probe that finds nothing re-enters
  `enterDegraded()` with a longer backoff. The probe timeout means the retry can
  never latch in `ACQUIRING` either.
- A healthy recovery (`finishBurst` clean predicted burst, `finishMeasurement`
  consistent measurement, resync back to `WAITING`) calls `m_Degraded.reset()`,
  so reception recovering clears the state.
- Late UART data during the backoff cannot reacquire the lock: the shared
  `gpsPowerLockRequired()` gate rejects lock acquisition while `DEGRADED`, and
  the retry changes to `ACQUIRING` before acquiring it. This closes a second
  lock-lifetime path that the first implementation missed.
- The healthy path is unchanged: a device that never degrades never enters this
  code, and the initial post-enable `ACQUIRING` stays unbounded so a cold start
  still holds the lock while it waits for the first fix.
- Diagnostics: `gps` console status prints `degraded:` and `retries:`, and the
  sim profiler sees a `degraded` GPS state.
- The end-to-end recovery scenario advances virtual time after restoring the
  UART. `assert-eventually` only yields wall time, so it cannot itself cross a
  pending exponential retry deadline. It checks the lock during the recovered
  interval rather than requiring an instantaneous final zero: the healthy duty
  cycle is allowed to reacquire `NO_LIGHT_SLEEP` for its next predicted window.

## On-screen indicator

The degraded state was console only, so a degraded, retrying GPS was invisible on
the device. A subtle, non-intrusive indicator now surfaces it on screen, without
a modal or a new widget.

- The pure `Furble::gpsIndicatorDegraded(enabled, degraded)` in
  `include/FurbleGPSPowerCycle.h` gates the indicator on GPS being enabled, so a
  default off GPS never trips it and a resync or a GPS off clears it.
- The header status icon reuses its existing glyph slot: `GPS::update()` layers a
  subtle amber warning tint on the icon while degraded, and shows the searching
  glyph rather than the disabled glyph when there is no fix, so a retrying GPS
  never looks switched off. The tint is a local recolor override cleared on
  recovery so the theme recolor returns. Both the tint and glyph updates keep the
  existing changed check, so the periodic poll only redraws on a state change.
- The GPS NMEA diagnostics page fix label gains a `degraded, retry N` line,
  guarded by the existing `setLabelTextFmtIfChanged` setter.

## Verification

- Host regression `tests/host/gps_power_cycle_test.cpp` (ctest
  `gps-power-cycle`) drives three plus bad checksum bursts into the shared
  `GpsDegradedRetry` policy and asserts a finite retry is scheduled, the retry is
  always due within the cap (never held forever), the backoff is bounded, and a
  healthy resync clears the state. Mutation check: removing the backoff cap makes
  the suite fail; restoring it returns to green. The same test also pins the
  on-screen indicator mapping: `gpsIndicatorDegraded()` lights only when GPS is
  enabled and the cycle is degraded, and clears on resync or GPS off.
- Host regression `tests/host/gps_power_profiler_test.cpp` (ctest
  `gps-power-profiler`) drives the same lock and virtual-clock API used by the
  SDL simulator. It asserts degraded residency is present in the report,
  light-sleep residency exists during the released-lock backoff, and the
  bounded probe leaves no lock held. The profiler models degraded receiver
  current as acquisition current while retaining a separate `degraded` counter.
- Firmware builds: five release envs plus `m5stick-s3-debug`.
- CI run `98783861972` exposed a scheduling race at the first degraded-state
  assertion on a loaded runner. The scenario now includes an explicit worker
  handoff before observing degradation and before observing recovery. Twelve
  consecutive isolated runs and the complete 62-scenario e2e suite pass. A
  healthy `tracking` state may hold `NO_LIGHT_SLEEP`, so the test asserts lock
  release at the degraded boundary rather than treating healthy tracking as a
  failure.
- Owed: an on-device confirmation on the M5StickS3 that the lock current drops
  during the degraded backoff and recovers on resync. The logic is sim testable
  and covered above; the mA recovery is the final on-device step.
