# 83 - host regression tests for the connect and disconnect lifecycle

## Goal

Reproduce the connect and disconnect bugs found only on hardware inside the
host camera harness, so CI catches them without a bench. This extends the
plan 36 harness (`tests/host`, MockNimBLE, the Fujifilm virtual camera) with a
lifecycle test that builds the real `lib/furble/Camera` against the mock.

## Motivation

Several lifecycle bugs surfaced only during on-device testing:

- #106: `CameraList` owned cameras by raw pointer, so `load()`/`clear()` freed
  a Camera while an in-flight connect still held it. Fixed by shared ownership.
- False-connected: after a disconnect event is delivered, the camera must not
  report connected and the UI must read no RSSI, so a torn-down session never
  finalises as connected.
- Disconnect after a drop: once the drop is handled the interactive disconnect
  must complete promptly instead of spinning to its backstop.
- `onDisconnect` must clear the connected state so the next connect does real
  work.

The SDL simulator cannot cover these. It shims out `Camera` and `Control`
(`sim/CameraListSim.cpp`, `sim/FurbleControlSim.cpp`), so it never runs the
real lifecycle logic. The plan 36 host harness does: it compiles the real
`Camera` and vendor code against MockNimBLE. That is the right vehicle.

See also plans 75 (false-connected) and 80 (camera lifetime).

## Harness extensions

MockNimBLE could already model a successful connect (client connect, service
discovery, notifications) and a local `disconnect()`. It could not drive a
spontaneous link loss or a connect failure. Three small test hooks were added:

- `NimBLEClient::mockDropLink(reason, fire_callback)`: sever the peer link and
  clear the client connected flag. With `fire_callback` true the disconnect
  callback runs, mirroring the stack detecting the drop and notifying the app.
  The lifecycle tests always deliver the callback so they assert the post-event
  invariant. The `fire_callback` false form (link down, no callback) is left in
  the hook for the liveness fix to drive the exact stale teardown path.
- `NimBLEDevice::setConnectShouldFail(bool)`: force the next connect to fail,
  modelling a connect that never establishes.
- `NimBLEDevice::lastClient()`: reach the client a Camera created internally,
  so a test can drive link loss on it.

## Tests

`tests/host/lifecycle_test.cpp`, wired into CTest as `camera-lifecycle`:

1. Shared ownership survives a camera list clear (#106). A held Camera stays
   alive and valid after the owning list is cleared.
2. A failed connect stays disconnected and a later connect does real work.
3. A delivered disconnect is not reported as connected. After `onDisconnect`
   runs, `isConnected()` is false and no RSSI is read.
4. Disconnect after a drop completes promptly and reports disconnected, so the
   interactive disconnect never spins to its backstop.
5. `onDisconnect` clears the connected state and the camera reconnects for
   real afterwards.
6. A camera-off supervision timeout clears state and allows recovery.

Tests 3, 4 and 6 assert the post-event invariant that `onDisconnect` clears
state. They deliberately do not model a silent pre-callback link-loss window.
On hardware the link is only known dead once the supervision timeout fires the
disconnect event, so that window is not observable, and modelling it would bake
in the live-client cross-check strategy. The correct liveness fix drops that
cross-check (a lock-free `isConnected()` must not deref a client another task
may free), so these tests stay green on both master and the correct fix. The
targeted test for the exact stale teardown path belongs with the liveness fix.

## Status on master

All tests pass on master (c95ac37), which already clears `m_Connected` in
`onDisconnect`. They are regression guards for the callback-path clearing.
Commenting out the `m_Connected` clear in `Camera::onDisconnect` while also
dropping the live-client cross-check (the shape of the upcoming lock-free fix)
fails tests 3, 4 and 6, which confirms they guard the clearing requirement the
fix relies on. With the cross-check still present the clear is masked, so the
teeth are demonstrated against the correct-fix shape, not against master.

## CI

`main.yml` already runs `cmake -S tests/host` plus `ctest` on every push and
pull request, and its clang-format discovery covers `tests/host`. Adding the
test to `tests/host/CMakeLists.txt` wires it into that job with no workflow
change.

## Coverage boundaries

The interactive disconnect backstop lives in `Control::disconnect()`
(`src/FurbleControl.cpp`), which depends on FreeRTOS tasks, Settings, Power and
Platform. Building it on the host needs a threaded FreeRTOS shim and is out of
scope here. The lifecycle test covers the Camera-level predicate that
`Control::disconnectComplete()` polls: a dead link reports disconnected at
once, which is what lets the interactive disconnect finish without hitting the
backstop. A future slice can add a threaded Control harness for the full path.

Two caveats to keep in mind when reading these tests:

1. The mock's `setSelfDelete` is a no-op, so the client object is never freed.
   These tests therefore do not exercise the freed-client use-after-free crash
   sites, the `m_Connected`-guarded `m_Client` derefs in `_disconnect()` and the
   connect-fail cleanup. Future work: model `setSelfDelete` freeing the client
   and assert every `m_Client` deref is `m_Connected`-guarded.
2. These tests guard code structure and teardown-path clearing, not on-device
   BUG A supervision timing. Whether `onDisconnect` actually fires promptly when
   a camera powers off is a hardware property the host harness cannot measure.
