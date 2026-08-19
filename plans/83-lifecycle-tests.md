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
- False-connected on a stale flag: after a session tore down without a clean
  `onDisconnect` (camera powered off, link lost), a leftover `m_Connected`
  made `isConnected()` report connected on a dead link, and a new connect
  short-circuited. `Camera::isConnected()` must consult the live client.
- Disconnect on a dead link: with a stale connected flag the interactive
  disconnect could spin to its backstop instead of completing promptly.
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
spontaneous link loss, a connect failure, or a stale connected flag. Three
small test hooks were added:

- `NimBLEClient::mockDropLink(reason, fire_callback)`: sever the peer link and
  clear the client connected flag. With `fire_callback` true the disconnect
  callback runs, mirroring the stack detecting the drop. With it false the link
  is down but no callback fires, modelling the window where the peer is gone
  yet the app still holds a stale connected flag.
- `NimBLEDevice::setConnectShouldFail(bool)`: force the next connect to fail,
  modelling a connect that never establishes.
- `NimBLEDevice::lastClient()`: reach the client a Camera created internally,
  so a test can drive link loss on it.

## Tests

`tests/host/lifecycle_test.cpp`, wired into CTest as `camera-lifecycle`:

1. Shared ownership survives a camera list clear (#106). A held Camera stays
   alive and valid after the owning list is cleared.
2. A failed connect stays disconnected and a later connect does real work.
3. A stale connected flag does not mask a dead link. `isConnected()` consults
   the live client, not just the cached flag.
4. Disconnect on a dead link completes promptly and reports disconnected at
   once, so the interactive disconnect never spins to its backstop.
5. `onDisconnect` clears the connected state and the camera reconnects for
   real afterwards.
6. A camera-off supervision timeout eventually clears state, through both the
   silent-loss window and the later callback.

## Status on master

All tests pass on master (c95ac37), which already carries the shared ownership
fix and the live-client cross-check in `isConnected()`. They are regression
guards. Reverting `Camera::isConnected()` to the cached-flag-only form fails
tests 3, 4 (dead-link assertions), and 6, which confirms they catch the
false-connected regression and would guard a future lock-free refactor.

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
