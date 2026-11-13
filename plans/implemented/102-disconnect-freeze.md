# 102 - Disconnect freeze off the UI task

## Motivation

Hardware testing on the M5StickS3 with an X100VI surfaced a UI freeze when the
camera is powered off or out of range and the user taps Disconnect (or an
auto-reconnect is running). The screen freezes for up to about 30 s. The serial
signature captured live:

```
disconnect -> state: disconnecting
W furble: Camera disconnect timed out after 30000 ms, forcing completion.
-> state: idle
```

This is the residual freeze that plan 82 (PR #109, BUG B) deliberately left in
place. PR #109 capped the supervision timeout (BUG A) so the freeze was bounded
by the detection window, and its own Deviations section named the real fix and
deferred it:

> Removing the freeze entirely, rather than bounding it, needs the completion
> wait moved off the LVGL task ... deferred to its own change and its own
> hardware verification.

This plan is that change.

## Root cause

`Control::disconnect()` runs on the caller's task. The interactive Disconnect
button reaches it through `UI::doDisconnect()` on the LVGL/UI task. The old body
polled `disconnectComplete()` in 20 ms slices, feeding the watchdog, up to
`DISCONNECT_WAIT_MAX_MS` (30 s):

`disconnectComplete()` returns true only once every target task has stopped, no
connect is in progress, and `camera->isConnected()` is false for all. The last
term is the sticking point: `isConnected()` reads the cached `m_Connected` flag,
which is cleared by `onDisconnect`. `Camera::disconnect()` only issues an
asynchronous `ble_gap_terminate`; it does not clear the flag. For a powered-off
or out-of-range camera the peer never acknowledges the terminate, so
`onDisconnect` fires only at the link supervision timeout, or not within the
interactive window at all. Until then `isConnected()` stays true, so the wait
spins on the LVGL task and the screen is frozen for the whole window, then force
completes at the 30 s backstop.

The freeze is entirely a "which task runs the wait" problem. The completion
criterion itself is correct and must not change: `m_Connected` drops in
`onDisconnect` right before NimBLE frees the self-deleting client, so declaring
the disconnect done any earlier lets a follow-up connect allocate a fresh client
while the old one is still being freed, which is the reconnect use-after-free
that the PR #62 wait and the teardown-draining connect gate exist to close.
Clearing `m_Connected` inside `Camera::disconnect()` would remove the freeze but
reopen exactly that race on the common present-camera reconnect, so it is not
the fix.

## Fix: move the interactive wait off the UI task

`Control::disconnect()` splits cleanly into two paths that were already distinct
callers.

The interactive path (`forRestart == false`) stops blocking on the link going
down. It still aborts the connect, cancels any in-flight GAP connect, and sends
the undroppable `CMD_DISCONNECT` to every target. It then waits only for the
per-target teardown tasks to stop (`targetTasksStopped()`: every target
`m_Stopped` and no connect in progress), not for `isConnected()` to clear. A
target task runs `Camera::disconnect()`, which just issues the asynchronous
`ble_gap_terminate` and returns, so it stops in a few milliseconds even for a
dead peer. This bounded wait, not the 30 s `isConnected()` spin, is what caused
the freeze, and dropping the `isConnected()` requirement removes it. The wait
also guarantees no target task is still inside `Camera::disconnect()`
dereferencing the link when `disconnect()` returns, so a caller that tears down
BLE state right after does not race the teardown (on device the peer persists; in
the host e2e harness the virtual peer is freed at scope exit, and this barrier is
what keeps that safe).

Once the tasks have stopped it hands every target to the drain set
`m_ZombieTargets`, records a backstop deadline `m_ZombieDeadline`, clears
`m_Targets`, sets `STATE_IDLE`, and returns. The UI task is free immediately:
`getState()` is idle and `getTargets()` is empty the moment the call returns, so
there is no freeze and no stale target.

The control task finishes the teardown. `reapZombieTargets()` already runs every
50 ms loop. It now frees a drained target only once its task has stopped
(`m_Stopped`, so the task can never touch the object again and `~Target()` skips
its radio call) AND the link is really down (`!isConnected()`, so the
self-deleting NimBLE client has been freed). `teardownDraining()` (a non-empty
drain set) already gates `STATE_CONNECT`, so a follow-up connect cannot allocate
a fresh client while NimBLE is still freeing the old one. That is the same
completion criterion the old on-UI-task wait enforced, now on the control task.
A gone peer whose link never reads down is bounded at `m_ZombieDeadline`
(see the follow-up below) so a later connect never wedges behind a dead-peer
teardown.

Moving every target (stopped or not) into the drain and clearing the emptied
`m_Targets` is the zombie-reaping ownership pattern: no live `Target` is
destroyed by the `clear()`, because they were all moved out first, so a task
still inside `Camera::disconnect()` keeps writing `m_Stopped` through a live
`this`.

The restart path (`forRestart == true`, `Platform` auto-off/restart) keeps the
old synchronous force-complete wait loop. `esp_restart()` runs immediately after
and kills any in-flight teardown, so a force-complete there cannot race a later
connect. Keeping the two paths separate is deliberate: sharing a single
force-complete for both is the pattern that crashed hardware before.

The only vendor-layer change is the new base `Camera::reclaimClient()` below, so
Canon, Nikon, Sony, Ricoh and Fujifilm behave identically. `isConnected()` stays
a lock-free read of `m_Connected`; no live-client cross-check is reintroduced.

## Follow-up: reclaim a gone-peer client so reconnect is not gated ~30 s

The first bench test confirmed the freeze was gone but surfaced a regression: a
`connect` right after disconnecting a powered-off camera stuck in "connecting"
for 30 s with no BLE activity. Root cause: for a gone peer the `ble_gap_terminate`
that `Camera::disconnect()` issues never completes, so `onDisconnect` never fires
and `isConnected()` never clears. The drained target then sat until the full
`DISCONNECT_WAIT_MAX_MS` backstop, and `teardownDraining()` gated the fresh
connect that whole time. The completion criterion is right, but for a gone peer
the event it waits on never arrives.

Fix: bound the drain. `reapZombieTargets()` still reaps a stopped target the
instant its link is really down (a live peer whose onDisconnect fired and whose
client self-deleted). If the link is still up after a short bound
(`DISCONNECT_DRAIN_RECLAIM_MS`, 2 s) the control task reclaims the orphaned client
itself with the new `Camera::reclaimClient()` (`NimBLEDevice::deleteClient()`, the
same reclaim the never-linked orphan path uses), then reaps. So the connect gate
releases in about 2 s for a gone peer instead of 30 s, and the client is freed
rather than leaked from the fixed NimBLE pool. The reconnect use-after-free guard
still holds: a live peer still reaps only on the real link-down (never on this
reclaim, since 2 s is well above a healthy teardown), and the reclaim runs on the
control task after the target task has stopped, so a following connect allocates
its client with no free in flight to race.

### Deferred-delete detach (second review round)

Code review caught a latent use-after-free in the reclaim. `reclaimClient()`
runs while the link still reports up, so the client is still connected, and
`NimBLEDevice::deleteClient()` on a still-connected client does not free it
synchronously: the real stack sets deleteOnDisconnect and re-issues the
terminate, so the client outlives this Camera while still holding the raw
callback pointer set at connect (`setClientCallbacks(this)`). The drained Camera
is freed as soon as its target reaps and the next connect rebuilds the
CameraList, so the late `onDisconnect`, fired seconds later when the stalled
terminate finally resolves, would call `Camera::onDisconnect()` on freed memory.
This differs from the never-linked orphan reclaim, where the client is not
connected and `deleteClient()` frees synchronously.

`reclaimClient()` now detaches the Camera from the client first, with
`setClientCallbacks(nullptr, false)`, before `deleteClient()`. NimBLE points a
null callback at its default no-op, so the late `onDisconnect` is harmless. The
old ordering (v1) reaped only after `onDisconnect`, which never opened this
window; the reclaim reaps before it, so the detach is what keeps the ordering
safe.

## Tests

`tests/host/control_disconnect_test.cpp` (CTest `control-disconnect`) compiles
the real `src/FurbleControl.cpp` against MockNimBLE and the real `Camera`,
behind minimal host shims for FreeRTOS, `Platform`, `Power`, `Settings` and
`ble_gap_conn_cancel`. It connects a camera, simulates a power-off with
`mockDropLink(reason, fire_callback=false)` (link severed, no `onDisconnect`, so
the camera keeps its stale connected flag), then calls the interactive
`disconnect()` and asserts it returns promptly (well under a second, not the old
30 s), with `getState() == STATE_IDLE` and `getTargetCount() == 0` at once. It
also confirms the drain reaps once the delivered disconnect frees the link, and
that a clean present-camera disconnect still ends idle with no targets. A third
case drives the hardware regression directly: connect, power-off (silent drop,
no onDisconnect), interactive disconnect, then an immediate reconnect that must
reach active well within the 30 s backstop (it clears the gate at the ~2 s
reclaim bound).

`tests/host/control_reclaim_uaf_test.cpp` (CTest `control-reclaim-uaf`) guards the
deferred-delete detach under AddressSanitizer. It compiles the real `Control` and
`Camera` with `-fsanitize=address` and uses a mock deferred-delete model
(`mockStallTerminate`, `mockRequestDelete`, `mockCompleteStalledTerminate`) that
keeps a still-connected client alive on `deleteClient()` exactly as the real
stack does. It connects a gone peer, lets the reclaim run, frees the Camera as a
reconnect would, then fires the late `onDisconnect` and asserts no use-after-free.

The mock's deferred-delete model is kept additive: `deleteClient()` defers only a
client marked by `mockStallTerminate()`, so the synchronous-free path the other
host suites (including PR #131's `control-e2e` scenarios) rely on is unchanged.
This branch coexists with #131's real-Control `control-e2e` harness: both compile
the production `src/FurbleControl.cpp`, and all seven #131 scenarios still pass on
this branch (the `targetTasksStopped()` barrier is what keeps them safe, since
they free their virtual peer right after `disconnect()` returns). The two
harnesses stay separate ctest targets; deduplicating their shims is a later
cleanup.

Mutation checks, all three verified: waiting on `disconnectComplete()` instead of
`targetTasksStopped()` makes the dead-camera `control-disconnect` block ~30 s and
fail the prompt-return assertion; restoring the 30 s drain deadline makes the
reconnect case gate for 30 s and time out; removing the
`setClientCallbacks(nullptr)` detach makes `control-reclaim-uaf` fault under ASan
on the late `onDisconnect`.

## Needs on-device retest

This changes which task drives the delicate teardown lifecycle, so it needs
bench verification on the M5StickS3 + X100VI:

- Connect, power the camera off, tap Disconnect: the screen must return to the
  menu immediately with no ~30 s (or any) freeze and no stale target
  (`targets: 0`, `state: idle`).
- Power the camera off, tap Disconnect, then immediately `connect` the same
  camera: the connect must not stall ~30 s. It should proceed within a couple of
  seconds (the gone-peer client reclaim) once the camera is back, not wedge
  behind the drain. This is the regression the bench found.
- Power the camera off, tap Disconnect, reclaim and reconnect, then let the
  device sit 30 to 40 s so the powered-off camera's supervision timeout fires the
  late `onDisconnect` on the reclaimed client. Confirm no crash or reset (the
  detach must have pointed that late callback at the default no-op).
- Immediately reconnect after a normal (present-camera) disconnect: must still
  connect and do real BLE work, with no reconnect crash (the drain gate must
  hold connect until the client is freed).
- Power the camera off mid-session while auto-reconnect is running, then tap
  Disconnect: no freeze, returns to idle.

## Deviations

- The residual bounded freeze from plan 82 is now removed rather than only
  shortened. The supervision-timeout cap from plan 82 still applies and is
  unchanged.
