# 167 - uncancellable connect attempts and the draining camera

## Motivation

Issue 271. Two firmware gaps in the reconnect-cancel deadlock family recorded
in `CLAUDE.md`, found while root-causing the simulator teardown timeout in plan
166. The simulator defect there was host starvation and is fixed; these are
separate, and they are reachable on hardware.

The entry point is a vendor wait that does not honour the plan 148 cancel
contract. `Camera::connect()` holds `Camera::m_Mutex` for the whole attempt, so
the target task's `Camera::disconnect()` blocks behind it, and the only way to
release it early is for the wait to poll `connectCancelled()`.
`CanonEOSSmart::_connect()` waited 60 s for the user to confirm pairing without
polling. That is twice `Control::DISCONNECT_WAIT_MAX_MS`, so a user disconnect
during Canon pairing could not abort the attempt and was guaranteed to burn the
whole 30 s cap. `DJIOsmo::_connect()` waited the full 30 s on its protocol
handshake the same way.

What happened next was invisible and then harmful:

1. The interactive teardown broke out of its cap with no log line at all, so a
   30 s freeze looked like a slow disconnect rather than a defect.
2. It drained the target into `m_ZombieTargets` with the attempt still running.
3. `Control::addActive()` deduplicates against `m_Targets` only, so tapping
   connect again handed that same camera a fresh target, and
   `Control::connectAll(bool)` then cleared the very cancel token the teardown
   had set. The attempt became uncancellable for the rest of its vendor
   timeout. `resetConnectionState()` also ran on a camera mid-attempt.
4. The drained target's task stays blocked on `Camera::m_Mutex`, so it never
   publishes `m_Stopped`, the zombie is never reaped, and `teardownDraining()`
   holds the connect gate shut for up to a minute.

The user-visible result on a Canon: disconnect during pairing, tap connect
again, and the device refuses to connect for the best part of a minute with
nothing in the log explaining it.

## What was corrected from the issue text

The issue described gap 1 as a drained target that "can never be cancelled by a
later `disconnect()`". Read literally that is not reachable: the cancel loop
runs before the drain in the same call, and `clearConnectCancel()` walks only
`m_Targets`, so a zombie always carries a set token. The reachable route is
step 3 above, where a later connect cycle *clears* a token that was correctly
set. The fix closes both the route and the underlying assumption.

## Change

### Vendor waits honour the cancel contract

`CanonEOSSmart` and `DJIOsmo` poll `connectCancelled()` inside their waits and
abort. Only the poll is added. The waits themselves, their durations, their
poll intervals and every protocol step around them are untouched; the vendor
behaviour they implement comes from the sources already cited in
[61-camera-compatibility.md](61-camera-compatibility.md), and this change adds
no protocol knowledge. Both vendors are declared untested on hardware: only
Fujifilm is available here.

`tests/test_connect_cancel_contract.py` holds the rule for every camera class,
so the next vendor with a long wait cannot reintroduce it. A camera source that
waits inside a connect must also poll; a source with no wait is exempt, and
code inside `#if 0` is not code.

### The cap break-out is logged

`Control::disconnect()` records when the interactive wait expires, naming the
bound and how many targets it is draining with an attempt still in flight.

### The interactive path honours timeout_ms

That path ignored its `timeout_ms` parameter and always waited
`DISCONNECT_WAIT_MAX_MS`, which is the parameter's default, so every existing
caller is unchanged and a caller asking for a shorter cap now gets one. This is
also what lets the regression test drive the cap in milliseconds instead of
waiting 30 s.

### A draining camera is refused a fresh target

`addActive()` now checks the drain set as well, and the refusal re-arms the
cancel on that camera.

Refusing is the choice that unblocks the user fastest, which is why it was
taken over silently deduplicating. Both avoid the fresh target, but only the
refusal cancels the attempt: deduplicating alone would leave it running to its
own vendor timeout, up to 60 s on a Canon pairing wait, whereas cancelling
makes it unwind within one vendor poll, the zombie reaps, and the next connect
goes through in about a second. Surfacing "still disconnecting" on the UI is
additive and deliberately not done here: the existing duplicate-connect refusal
immediately above is equally silent today, and adding a one-off surface for
only the new case would be inconsistent. A UI surface for both belongs to a
UI change.

### The teardown no longer depends on an earlier call

`Control::disconnect()` cancels the drain set and the attempt in flight as well
as the live targets, so a teardown is self-sufficient rather than relying on a
token some earlier call happened to set.

### m_ConnectCamera is guarded

It was published unlocked from `connectAll()` and read unlocked in
`getConnectingCamera()`, while `disconnect()` and `getDebugState()` read it
under `m_Mutex`. That is a data race on a `shared_ptr`: a torn read hands out a
control block that is being replaced. Publication now goes through
`setConnectCamera()` and every access takes the mutex. This is a correctness fix
verified by inspection of all six access sites, not by a test; a race test would
be flaky and would prove less than the exhaustive check.

## Verification

Host regression `tests/host/control_zombie_cancel_test.cpp` builds the state
deliberately, in milliseconds. Its camera models the pre-fix Canon shape: a
blind window inside `_connect()` that ignores the cancel token, then a polling
window that honours it. Three phases isolate three fixes, and each has a
mutation:

| Mutation | Failing check |
| --- | --- |
| Interactive path ignores `timeout_ms` again | `interactive disconnect honours its 50 ms cap, not the blind wait` |
| `addActive()` stops checking the drain set | `a draining camera is refused a fresh target` |
| `disconnect()` stops cancelling the drain set | `a teardown cancels a drained attempt whose token was cleared` |
| Canon poll removed | contract gate names `lib/furble/CanonEOSSmart.cpp` |
| DJI poll removed | contract gate names `lib/furble/DJIOsmo.cpp` |

No mutation is left in the tree.

## Not covered, and why

There is no simulator scenario for the zombie route, and that is a real gap
rather than an oversight.

Reaching it needs the interactive cap to expire, which needs a camera whose
connect ignores the cancel token. After this change every camera the simulator
can drive honours the contract, which is the point of the change. The two ways
to build the state in the simulator are to compile the plan 157 sync points
into it and park the control task at `disconnect_abort_armed` from a scenario
verb, or to add a virtual peer for a vendor with a long confirmation wait.
Neither is small, the first is simulator scheduling work that belongs with plan
158, and the second needs the capture-backed peer evidence plan 159 requires.
Adding a deliberately non-polling camera purely to make a scenario reachable
would be simulator-only behaviour with no production counterpart, which
`sim/CLAUDE.md` forbids.

Plan 166 already recorded the other half of this: before that change the fuzzer
walked into the drained-with-attempt-in-flight state by accident on every seed,
and a settled teardown no longer reaches it. So the host regression is the only
coverage of this route today, and it is deliberately exhaustive about the three
paths rather than incidental.

## Hardware gate, owed

Not run. Only Fujifilm is available here, and the Canon and DJI edits are
declared untested.

On the X100VI: connect, disconnect during a pairing or registration wait,
reconnect immediately, and the connect must proceed within a couple of seconds
rather than stalling behind the previous attempt. Any teardown that breaks out
of its cap must now say so in the log; a run with no such line and a prompt
reconnect is the pass.
