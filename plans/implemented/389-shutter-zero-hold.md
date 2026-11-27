# 389: a configured Shutter of 0

**State: implemented on `fix/shutter-zero-hold`.** 2026-11-27. Fixes the defect
recorded as finding 1 of the shutter-hold decision record. A value of 0 is now
what it says: the transition is due now, not at whatever period the timer
carried over from the previous state.

## The defect

`UI::intervalometer()` is a state machine driven by one LVGL timer. Each state
sets `next`, the delay until the following transition, and one tail schedules it:

```c
if (next > 0) {
  lv_timer_set_period(timer, next);
  m_IntervalNext = tick() + next;
}
```

One number answered two different questions: "is there a next transition" and
"how long until it". The run ends by setting `next = 0`, which is why the tail
skips the update. But 0 is also a legal configured duration for all three
intervalometer timings, and the documented range starts at 0. So a configured 0
fell into the "no transition" branch and the timer kept the period the previous
state had set.

What that meant in practice:

- **Shutter 0.** After a frame the period is the Delay, so the release waited out
  the delay. With the shipped 15 s delay, a user asking for no shutter hold got a
  15 s one, then the next frame on the following pass.
- **Wait 0.** The first frame reused the previous period. On the first run of a
  session that is the 100 ms creation period, which hid the defect; a later run
  inherited the last frame's delay.
- **Delay 0.** The next frame reused the Shutter period instead of following the
  release immediately.
- **Two places patched around it.** The Stop handler reset the period to a magic
  100 because the next run would otherwise inherit the delay, and the Start and
  resume paths resumed the timer without arming it, so a run started after a
  finished run waited out the leftover period.

0 is inside the documented range, so this was reachable from the UI with no
warning: `docs/settings-and-controls.md:225` offers 0 to 999.

## Why a zero period is not the fix

The obvious repair, always calling `lv_timer_set_period(timer, next)`, moves the
bug rather than fixing it. LVGL derives a timer's remaining time from its period,
so a period of 0 is permanently due. The timer would run on every UI pass with no
deadline at all, the run-page countdown would read zero forever, and the UI loop
would have nothing to sleep until. A magic floor of 1 ms has the same runtime
behaviour and hides the "no transition" case behind a number.

## What 0 means

- **0 means due now**, for Wait, Shutter and Delay alike. It is armed with
  `lv_timer_ready()`, which clears the remaining time once and leaves the period
  meaning "a deadline". The realised delay is one UI pass, 5 ms
  (`include/FurbleUI.h:1138`), which is the shortest wait this loop can honour.
- **No next transition** is the FINISHED state, and only that state. It pauses
  the timer explicitly and is no longer expressed as a duration.

Rejected alternatives: a zero period (permanently due, as above), a 1 ms clamp
(same runtime, still needs the flag to tell FINISHED apart), and refusing 0 in
the UI (0 is documented, and "no extra delay" and "tap the shutter" are the
useful settings).

## Implementation

One helper, one flag, and the per-call-site workarounds removed:

- `armIntervalTimer()` (`src/FurbleUI.cpp:7795`) is the single expression of
  "due on the next pass": `lv_timer_resume()` then `lv_timer_ready()`.
- `transition_pending` (`src/FurbleUI.cpp:7863`) is the "is there a next
  transition" answer. Only FINISHED clears it (`src/FurbleUI.cpp:7860`), and the
  tail reads it once (`src/FurbleUI.cpp:7871`) to choose between a period and an
  armed timer.
- The IDLE to WAIT hand-off no longer calls `lv_timer_ready()` itself; it is a
  pending transition with no duration and goes through the same tail
  (`src/FurbleUI.cpp:7774`).
- The Start handler (`src/FurbleUI.cpp:10886`) and the resume path
  (`src/FurbleUI.cpp:7713`) arm the run, so a run starts now rather than at the
  period the timer carried over.
- The Stop handler's `lv_timer_set_period(timer, 100)` is gone. It existed only
  because the start paths could not be trusted to arm the timer.

No behaviour changes for non-zero values: the same periods are set in the same
order.

## Tests

`sim/scenarios/e2e/intervalometer-zero-shutter.txt`, registered in
`sim/scenarios/manifest.json`, runs a two frame sequence with a 15 s Delay and a
Shutter of 0. The first frame cannot show the defect, because the timer still
carries its 100 ms creation period. The second frame is where a 0 Shutter used to
hold for the Delay, so the scenario asserts that the second release lands within
500 ms of the second press. A 15 s failure against a 500 ms bound is unambiguous.

Evidence from the simulator, one scenario and one binary:

- Unfixed `src/FurbleUI.cpp`, restored from `origin/master`: exit 1,
  `ASSERT-EVENTUALLY-VIRTUAL FAILED: camera.shutter_releases expected '2' got
  '1' after 500 virtual ms`. The second release never came inside the bound.
- With this change: exit 0, every assertion passes.

The same binary passes `intervalometer-timing`, `interval-stop-reset`,
`interval-back-trap`, `interval-spinner-pages` and `imu-gesture-interval`, and
`sim/scripts/run-deep-sleep.sh` passes with no failed assertion across its
intervalometer scenarios, including the two resume paths this change arms.

Existing scenarios that configure 0 keep working, and their assertions are
unchanged in kind:

- `interval-stop-reset`, `interval-back-trap` and `interval-spinner-pages` seed
  `interval_delay 0`. The next frame now follows the release immediately instead
  of after one Shutter period. All three assert pages, state and back
  behaviour, never the spacing.
- `intervalometer-timing` and `imu-gesture-interval` seed `interval_wait 0`. The
  first frame now starts on the next pass instead of after the creation period.
  Both assert counts within a one second budget, not the latency.

## Documents

Four surfaces state the meaning of 0 and the two consequences of the Shutter
value, because a 0 hold and a long hold are the same setting read two ways:

- `docs/settings-and-controls.md:235`
- `docs/wiki/Settings-Reference.md:225`
- `docs/ui-walkthrough.md:422`
- `docs/wiki/UI-Walkthrough.md:374`

The consequence note is the gap recorded as finding 2 of the shutter-hold
decision record: the value is the exposure in bulb on a body that latches the
remote press, and on a body in continuous drive it sets how long the burst runs.

## Limits

- With Shutter and Delay both 0 the run issues a press and release pair per UI
  pass. That is the configuration the user asked for, the command path is
  non-blocking, and a full per-target queue drops the command and logs it
  (`src/FurbleControl.cpp:131-134`). The transport, not furble, sets the real rate.
  Release durability on a dropped command is plans/374, not this change.
- No hardware capture is claimed. A 0 duration is scheduled through the same
  path as any other duration, and the press and release bytes are unchanged.
