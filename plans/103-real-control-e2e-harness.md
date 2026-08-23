# PR103: Exercise the real Control connect/disconnect state machine end to end

## Goal

Run the production `src/FurbleControl.cpp` connect/disconnect/reconnect state
machine end to end in the host test suite, against the shared `MockNimBLE` peer
and the real `lib/furble` `Camera` lifecycle, on real threads. Until now the two
existing test vehicles each covered only half of this:

- The SDL sim (`FURBLE_SIM`) runs the real `FurbleUI` over a fake `Control`
  (`sim/FurbleControlSim.cpp`) and a fake `Camera` (`sim/CameraSim.cpp`). A fault
  injected there exercises the fake state machine, not the shipping code.
- The host camera tests (`tests/host`) drive the real `Camera` and vendor
  classes directly against `MockNimBLE`, but never instantiate `Furble::Control`.
  The connect flow (`addActive` -> `connectAll`, `disconnect`) and its threading,
  teardown, zombie reap and sleep-lock logic were unit tested nowhere.

This PR closes that gap with a headless end-to-end harness that compiles the real
`FurbleControl.cpp` and runs it on a real-thread FreeRTOS shim, driven exactly as
the device drives it, so the connect/disconnect/reconnect scenarios we previously
could only test on hardware now run in CI.

Line anchors read on branch `feat/sim-real-control` off fork `master` at
`b71d895`.

## What is real vs shimmed

The harness (`tests/host/control_e2e/`) links, unchanged:

- `src/FurbleControl.cpp` (the real control task, target tasks, teardown, zombie
  reap, adaptive power, sleep lock).
- `lib/furble` `Camera`, `Device`, `Fujifilm`, `FujifilmBasic`, protocol (via the
  existing `furble_host_camera` static library).
- `tests/host/nimble/MockNimBLE.cpp` and `tests/host/peer/FujifilmVirtualCamera`
  as the one shared mock BLE layer.

It provides only the small seams the real control needs off-target, all host-test
only, none touching the firmware binary:

- `control_e2e/freertos_shim.cpp` plus `freertos/` headers: real `std::thread`
  tasks and mutex or condition-variable queues, so the control task, the
  per-target tasks and the scenario thread run concurrently like the device.
- `control_e2e/doubles/` slim `Platform`, `Power` and `Settings` doubles: the
  real control only calls `Platform::tick`/`watchdogFeed`, `Power::acquire`/
  `release` and five `Settings::load` keys, so the harness backs exactly those
  rather than dragging in M5PM1, `esp_pm` and NVS. The `Power` double exposes a
  lock count so a scenario can assert the sleep-inhibit lock is balanced.
- Three additive symbols on the shared mock, used by `FurbleControl.cpp`:
  `ble_gap_conn_cancel()`, the two-argument `NimBLEDevice::setPower` and
  `NimBLETxPowerType`. Additive only, so the existing host tests are unchanged.

## Scenarios

Each scenario drives the real control and asserts real state, target counts, the
sleep-lock balance and timing bounds. Registered individually so a failure names
the scenario. All run under the existing `host_camera` CI job (no workflow edit).

- `fresh-connect`: connect reaches ACTIVE, holds the sleep lock, interactive
  disconnect returns under 3 s, sleep lock released, no client leak.
- `dead-camera-disconnect-no-freeze`: silent link drop leaves a stale connected
  flag; an interactive disconnect returns promptly once the (simulated)
  supervision timeout fires, while a background reader confirms the state
  observer never stalls. Guards the ~30 s freeze class.
- `connect-after-dead-disconnect`: a fresh connect after a dead-camera disconnect
  completes within about 2 s, inheriting no stuck state.
- `stale-session-reconnect`: reconnect against a stale CCCD subscribe session
  completes, bounded.
- `false-connected-guard`: a stale connected flag on a persisted camera is
  cleared by `addActive` -> `resetConnectionState` so the fresh connect runs a
  real handshake (BUG C). Mutation confirmed: neutering `resetConnectionState`
  fails this scenario.
- `transient-connect-recovers`: a connect that fails once then succeeds recovers
  without leaking a client.
- `client-pool-exhaustion`: with the pool capped and every connect failing, the
  repeated retries never leak beyond one live client and the machine lands in
  CONNECT_FAILED, then recovers once connects succeed.
- `multi-connect-fujifilm`: route two virtual Fujifilm peers by BLE address,
  connect both in one real Control session, fan shutter commands out to both,
  drop only one link, keep the survivor usable, identify the dropped camera,
  and reconnect only that target back to a two-camera active session.

## Verification

- `cmake -S tests/host` plus `ctest`: 21 of 21 tests pass (14 pre-existing plus
  the 7 new `control-e2e-*`). Full suite runs in about 6 s.
- 15 back-to-back runs of the harness binary: 0 failures (no threading flakiness).
- Mutation check: neutering `Camera::resetConnectionState` flips
  `false-connected-guard` to red, then restores.
- `clang-format` clean. No firmware, `include/`, `lib/` or `sim/` source changed,
  so the device binary and the sim build are untouched.

The multiconnect follow-up adds one host scenario and an address-to-peer routing
table to the test-only NimBLE mock. The full host suite passes 37 of 37 tests,
including the new two-peer lifecycle. The unchanged simulator also passes its
full end-to-end suite on the exact PR commit. Physical radio scheduling and two
simultaneous camera links remain hardware checks.

## Honest gaps and next steps

- The mock has no supervision-timeout timer, so a silently dropped link never
  fires `onDisconnect` on its own. The dead-camera scenarios stand a helper
  thread in for the supervision timeout to bound the interactive disconnect the
  way the ~7 s `m_IdleTimeout` bounds it on hardware. A real mock supervision
  timer is a future refinement.
- Multi-connect partial-drop is covered at the real Control boundary. The mock
  still does not model radio scheduling, connection-parameter contention or
  two physical peripherals transmitting concurrently.
- The real UI is not layered on top here. This harness proves the real control
  state machine and the freeze-class timing at the control boundary. Driving the
  same real control under the real LVGL UI in the sim additionally needs a
  `NimBLEScan` mock (or a real-camera-emitting fake `CameraList`/`Scan`) and
  thread-safety on the mock globals, tracked as follow-up sim work.
