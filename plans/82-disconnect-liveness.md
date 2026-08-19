# 82 - Disconnect liveness

## Motivation

Hardware testing on the M5StickS3 (during PR #107 work) surfaced two related
connect/disconnect liveness bugs that both trigger when the camera is powered
off while furble still holds the link.

### BUG A: camera power-off is detected too late

When the camera is switched off, furble keeps reporting the link as connected
until the BLE link supervision timeout fires. `onDisconnect` already clears the
connection state correctly, it just runs late.

The supervision timeout is set by the connection profile. The adaptive
connection parameter feature (the CONN_SAVER "idle" profile, introduced in
PR #24) moved a quiet link to `m_IdleTimeout`, which was 1600 units, that is
16 s. So after a power-off the camera stayed "connected" for up to 16 s. The
fast profile uses `m_FastTimeout`, which is 512 units, that is about 5.12 s.

### BUG B: the Disconnect button freezes the screen

The interactive disconnect (`Control::disconnect(timeout, forRestart=false)`,
the wait loop introduced in PR #62) busy-polls `disconnectComplete()` in 20 ms
slices up to `DISCONNECT_WAIT_MAX_MS` (30 s) on the LVGL/UI task, feeding the
watchdog each slice. The poll completes only once the link is really down, that
is once `Camera::isConnected()` returns false, which needs `m_Connected`
cleared in `onDisconnect`.

When the camera is already off, `m_Connected` is stale-true, so the host issued
`ble_gap_terminate` (`Fujifilm::_disconnect`) completes only at the supervision
timeout. Until then the LVGL task is stuck in the poll, so the screen and
buttons freeze for the whole detection window. The watchdog is fed, so there is
no reset, crash, or use-after-free, just a frozen UI.

BUG B's freeze length is exactly BUG A's detection window: capping the
supervision timeout shrinks the freeze in the same step.

### BUG C: a fresh connect reports connected instantly, with no BLE work

After selecting a camera and pressing Connect, furble jumps straight to
connected without scanning or doing any BLE work. A reboot fixes it, so it is a
stale in-RAM state.

`UI::doConnect` walks the persistent `CameraList` and calls
`Control::addActive` for every camera with `isActive()` set. Those `CameraList`
`Camera` objects outlive their `Control::Target`. `connectAll` skips any target
whose `camera->isConnected()` is already true and never calls
`Camera::connect()`. If every target looks connected, `allConnected()` is true
and the control state machine goes straight to `STATE_ACTIVE`. So a stale
`m_Connected == true` on a `CameraList` camera short-circuits the whole connect.

`m_Connected` is set in `onConnect` and is meant to be cleared only by
`onDisconnect`. Two paths leave it stale-true on the persistent object:

- A prior session ended without `onDisconnect` firing. A powered-off camera
  delays the callback to the supervision timeout (BUG A), and the teardown
  through `Control::disconnect` and `Camera::disconnect` does not clear
  `m_Connected` on the list object.
- `Camera::connect` returned `m_Connected` after the link came up (`onConnect`
  set the flag) but `_connect` failed registration. `connect` then called
  `_disconnect` and still returned true, so the caller recorded a connected
  camera even though registration failed.

PR #107 exposed this. Before #107, `isConnected()` also checked
`m_Client->isConnected()`, so a stale `m_Connected` was caught by the real link
state. #107 made `isConnected()` a lock-free read of `m_Connected` alone (to
fix a first-connect watchdog reset), which is correct only if `m_Connected` is
cleared on every teardown path. It is not, so the stale flag now reads through.
This branch is based on the pre-#107 master, but the fix keeps `m_Connected`
accurate so it is correct once this lands on top of #107.

## Fixes

### BUG A: cap the supervision timeout

`m_IdleTimeout` drops from 1600 (16 s) to 700 (7 s) in `lib/furble/Camera.h`.
The unit is 10 ms. `m_FastTimeout` stays at 512 (about 5.12 s), so a dead link is
now detected in about 7 s on the idle profile, a large cut from 16 s.

The idle value is deliberately at or above the fast value, and here a bit above
it. Supervision margin is really a count of missed connection events (timeout
divided by connection interval). The idle profile uses much longer intervals
(250 to 300 ms) than the fast profile (tens of ms), so a given timeout buys the
idle link far fewer events of margin. An idle timeout below fast would make the
idle link the twitchier of the two, the opposite of what we want, and that risk
is worst under BT modem sleep plus DFS, which is exactly where furble runs. 7 s
keeps the idle link no more drop-prone than fast (about 23 events of margin at a
300 ms interval) while still catching a power-off in about 7 s. The final value
is subject to the hardware stability soak.

This touches the adaptive connection parameter feature (PR #24). `onDisconnect`
needs no change. This shorter supervision timeout needs a multi-minute
connection-stability soak on hardware to confirm it does not cause spurious
drops on a real, slightly lossy link. PR #106 already confirmed the earlier
link-stability drops were a use-after-free, not the power or BLE settings, so
the soak is a safety check, not a suspected regression.

### BUG B: fast-complete, keep the safe teardown

The chosen approach is fast-complete plus the BUG A cap.

`disconnectComplete()` already returns true the instant the link is genuinely
down (every target task stopped, no connect in progress, `isConnected()` false
for all), so a healthy disconnect or an already-dead link exits the wait with
zero delay slices. That is the fast-complete, and it is already correct.

We deliberately do not complete any earlier than `isConnected()` clearing.
`m_Connected` drops in `onDisconnect`, right before NimBLE frees the
self-deleting client. Declaring the disconnect done before that would let a
follow-up connect allocate a fresh client while the old one is still being
freed, which is exactly the reconnect use-after-free that PR #62 was written to
close. So the fast-complete cannot be made more aggressive without reopening
that crash.

With the camera powered off, the residual wait is the host terminate completing
at the supervision timeout, now bounded by `m_IdleTimeout` to about 7 s instead
of about 16 s. The wait still runs on the LVGL task, so the screen is still
frozen for that bounded window.

Removing the freeze entirely, rather than bounding it, needs the completion wait
moved off the LVGL task: the UI would post the disconnect request, show a
"Disconnecting" state, and return to the menu when the Control state clears,
while the teardown wait (same `disconnectComplete()` criterion, same 30 s
backstop, same zombie quarantine and teardown-draining connect-gate) runs on a
non-UI task. That move changes which task drives the delicate PR #62 teardown
lifecycle, so it is deferred to its own change and its own hardware
verification rather than folded in here.

### BUG C: lock-free isConnected plus reliable m_Connected clearing

This PR supersedes PR #107. It brings in #107's watchdog fix and adds the
reliable clearing that #107 was missing.

`m_Connected` and `m_Active` become `std::atomic<bool>`, and `isConnected()`
becomes a pure lock-free read:

```
return m_Connected.load();
```

Dropping `m_Mutex` here is the real first-connect watchdog fix: a cold connect
holds `m_Mutex` across the ~60 s secure scan, the UI task blocked on the same
mutex in `isConnected()`, and the M5PM1 watchdog fed from the UI task starved
until the device reset. `isConnected()` deliberately does not dereference
`m_Client`. NimBLE frees the self-deleting client on the host task inside the
disconnect callback and on a failed connect, so a lock-free reader that touched
`m_Client` would race that free (a use-after-free). A live-client cross-check
would also buy nothing on real hardware: on a silent drop (camera off) the local
client keeps reporting connected until the supervision timeout fires, so it
cannot see a dead link any sooner than the flag does once that timeout is capped
(BUG A). The mock in the host tests can force the client to report disconnected
without firing the callback, but real hardware cannot, so the cross-check is a
mock artifact, not a real detector.

Correctness therefore rests entirely on `m_Connected` being cleared on every
teardown path. The clearers are:

1. graceful disconnect: `onDisconnect` clears it.
2. supervision timeout expiry: `onDisconnect` clears it, now within about 7 s
   because of the BUG A cap instead of about 16 s.
3. a fresh connect after a session that left the flag stale: the new
   `Camera::resetConnectionState`, called from `Control::addActive`.
4. a partially established connect that failed registration: `Camera::connect`.

The stale-flag path that reproduces the user report (reports connected with no
BLE work, cleared only by reboot) is a session that ends without `onDisconnect`
ever firing. The `Camera` lives on in the persistent `CameraList`, so its
stale-true flag survives into the next connect, where `connectAll` reads
`isConnected()` true, skips the real connect, and jumps to active. A reboot
rebuilds the list with fresh objects, which is why it clears on reboot. Paths 3
and 4 close it:

- `Camera::connect` clears `m_Connected` when the link came up but `_connect`
  failed registration, right after the `_disconnect` call in the failure branch.
  `connect` no longer returns true for a half-open session.
- New `Camera::resetConnectionState` clears the atomic flag (and the stats
  snapshot) lock-free, without touching `m_Client`. It runs on the UI task, so
  it must not take `m_Mutex`, for the same watchdog reason as `isConnected()`.
- `Control::addActive` calls `resetConnectionState` on each camera as it joins a
  user requested connect. At that point the camera is not part of a live
  controlled session, so clearing the flag is safe, and it guarantees
  `connectAll` cannot skip the real connect on a stale flag.

The reconnect loop is untouched: it re-uses existing targets and never calls
`addActive`, so it still correctly skips a genuinely connected camera and
reconnects a dropped one.

### addActive deduplication (task #51 and the resetConnectionState safety claim)

`Control::addActive` gained a dedup guard: if the camera is already an active
target (pointer identity against `m_Targets`), it logs and returns without
adding a second target and without calling `resetConnectionState`. This closes
two things at the root, in the file the BUG C fix already touches:

- task #51: repeated connect requests (the console `connect <index>` path, a
  double tap) stacked duplicate `Target` objects on one `Camera`, each with its
  own task and queue, until the device ran out of memory and rebooted.
- the `resetConnectionState` safety claim: without the guard, a duplicate connect
  on an already live camera would clear the connected flag on a live link, so
  `connectAll` would re-run `connect()` and `createClient()` and orphan the
  still-live NimBLE client. The dedup guard removes that path, so the
  "not part of a live controlled session" assumption in `resetConnectionState`
  now holds by construction.

The reconnect loop is unaffected: it never calls `addActive`, and multi-select
adds each distinct camera once, so the guard only ever fires on a true duplicate.

There is no host test for the dedup: the host harness compiles only the
`Camera`/`Fujifilm` layer against MockNimBLE and does not build `FurbleControl`
or provide the FreeRTOS, Settings, and Power shims that `addActive` needs.
Adding a Control-level host test would be a separate harness expansion, so the
guard is covered by review here and the dedup is a candidate for a future
Control host harness.

### Tests: a targeted BUG C host regression

`tests/host/stale_connect_test.cpp` (CTest `stale-connect`) is the new guard for
BUG C. It connects, drops the link silently (the mock drops the link without
firing `onDisconnect`, so `m_Connected` is left stale-true), confirms the
lock-free `isConnected()` cannot see the dead link, then calls
`resetConnectionState()` (what `Control::addActive` does) and asserts the flag
clears and a fresh connect runs the real GATT handshake. Verified teeth:
neutralising the `resetConnectionState` body fails the clear assertion.

The pre-existing `camera-lifecycle` suite is co-evolving with the lock-free
change: its stale-flag checks (2b, 3, 5) were written against the old cross-check
`isConnected()` and fail against the mandated pure lock-free read. The
lifecycle-tests author is adjusting those checks to the post-event invariant
(assert clearing after `onDisconnect`, not a live cross-check on a silent drop).
This PR lands on top of that adjusted suite, so the final branch runs
`camera-lifecycle` and `stale-connect` both green once the adjusted lifecycle
tests are in place.

### Telemetry (console only)

`cameras status` now prints a per-target `conn` line with the live profile,
interval, latency, supervision timeout, and RSSI when a snapshot is available.
The supervision timeout is the value to watch during the BUG A soak. The line
is compiled only in `FURBLE_CONSOLE` (debug) builds and reuses the existing
`Camera::getConnParams` snapshot, so it makes no radio calls.

## Deviations

- The off-LVGL move of the disconnect wait (the near-zero-freeze fix for BUG B)
  is not implemented here. It is described above as a follow-up so it does not
  disturb the PR #62 teardown lifecycle in the same change.
