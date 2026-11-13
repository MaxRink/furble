# PR100: Expand host fault injection and hunt new BLE connect failure modes

## Goal

Grow the host camera harness (`tests/host`, the `host_camera` CI job) with fault
injection primitives that reach the vendor connect and command error paths, then
run an adversarial bug hunt with them. This mirrors PR112: it lands reusable
tooling plus regression tests that document findings. It does not fix product
code. Fixes are triaged into separate follow up PRs.

Line anchors were read on branch `sim-bughunt-fault-expand` off fork `master` at
`b8398f8`.

## Motivation

The two existing test vehicles hold complementary fault surfaces:

- Host harness (`tests/host`): real `lib/furble` `Camera` and `Fujifilm` against
  `MockNimBLE` and the `FujifilmVirtualCamera` peer. Existing faults:
  `NimBLEClient::mockDropLink`, `NimBLEDevice::setConnectShouldFail`,
  `setMaxClients`, `liveClientCount`, `lastClient`, and
  `FujifilmVirtualCamera::setStaleSubscribeSession`.
- SDL sim (`FURBLE_SIM`): real `FurbleUI` against a fake `Control` and `Camera`.
  Faults: the `connect_fail` seed and the companion rig options.

Neither vehicle could model a camera that connects but exposes an incomplete
GATT table, a write that the peer rejects mid handshake, or a link that
establishes only after a few misses. Those are exactly the shapes that expose
unguarded error paths in the vendor connect code.

## Fault primitives added (committed tooling)

All new surface lives under `tests/host`, so the release binary is unchanged.

- `NimBLEDevice::setConnectFailCount(size_t)`: fail the next N
  `NimBLEClient::connect()` calls, then let connects succeed. Models a transient
  link that misses a few attempts and recovers, so a test can drive reconnect
  churn and then confirm recovery. Drains, unlike `setConnectShouldFail`.
- `FujifilmVirtualCamera::suppressService(uuid)`: model a camera missing a GATT
  service, so `getService()` returns null.
- `FujifilmVirtualCamera::suppressCharacteristic(service, characteristic)`: model
  a present service missing one characteristic, so `getCharacteristic()` returns
  null.
- `FujifilmVirtualCamera::failWrite(service, characteristic)`: model an ATT write
  the peer rejects, so a handshake write returns false.

`fault_tooling_test` exercises all four and locks in the correct behavior of the
paths they reach (transient failures reclaim clients and recover, a rejected
handshake write leaves the camera disconnected and a later connect still works,
and suppressing an optional service does not break the connect).

## Findings

### Finding 1 (crash, connect path): FujifilmBasic null dereference on a missing shutter service

`FujifilmBasic::_connect()` (lib/furble/FujifilmBasic.cpp:150-164) only logs a
null shutter service and then dereferences it:

```
pSvc = m_Client->getService(SVC_SHUTTER_UUID);
if (pSvc == nullptr) {
  ESP_LOGI(LOG_TAG, "Failed to get shutter service");  // logs, does not return
}
...
m_Shutter = pSvc->getCharacteristic(CHR_SHUTTER_UUID);  // null dereference
```

A Fujifilm Basic camera that completes pairing and configuration but exposes no
shutter service crashes furble during the connect. The sibling vendors guard
this: `FujifilmSecure.cpp:224-227` and `Nikon.cpp:104-107` both `return false`.

- Repro: `fujifilm_missing_shutter_service_test` suppresses the shutter service
  and runs the connect in a forked child. Today the child crashes with SIGSEGV.
- Severity: high. Hard fault on the connect path.
- Class: unhandled error path, crash.

### Finding 2 (silent failure, connect path): FujifilmBasic reports connected with a dead shutter

When the shutter service is present but the shutter characteristic is missing,
`_connect()` leaves `m_Shutter` null, logs, and returns true. The camera reports
connected and active, but `sendShutterCommand()` short circuits on the null
`m_Shutter`, so every shutter and focus command is silently dropped with no error
surfaced. `FujifilmSecure.cpp:232-235` handles this correctly with a
`return false`; FujifilmBasic does not.

- Repro: `fujifilm_missing_shutter_char_test` suppresses the shutter
  characteristic, connects, and asserts a connected camera can fire the shutter.
  Today the connect returns true and zero shutter writes reach the peer.
- Severity: medium. Silent dead remote, no crash.
- Class: silent failure, missing error path.

Both findings share one root defect: the shutter block in
`FujifilmBasic::_connect()` does not fail the connect when the shutter GATT
element is absent, unlike the FujifilmSecure and Nikon siblings.

## CI wiring

`tests/host/CMakeLists.txt` registers each test with `add_test`, so the existing
`host_camera` CI job runs them with no workflow change.

- `fault-tooling` runs normally and must stay green.
- `fujifilm-missing-shutter-service` and `fujifilm-missing-shutter-char` are now
  registered as normal passing tests. They were originally `WILL_FAIL TRUE`
  while the finding was tracked, so the crashing or failing run counted as a
  pass and CI stayed green. The follow up fix added the missing guards to
  `FujifilmBasic::_connect`, so both tests return clean and run as real guards.

A mutation check confirmed both tests are load bearing: reverting the two
`return false` guards in `FujifilmBasic::_connect` flips both tests red, and
restoring the guards returns the suite to green.

## Fix landed (follow up PR)

Findings 1 and 2 are fixed. `FujifilmBasic::_connect` now `return false` when the
shutter service is null and when the shutter characteristic is null, mirroring
`FujifilmSecure::_connect`. No other vendor changed. The two regression tests are
flipped from `WILL_FAIL` to normal passing tests, so they now guard against the
crash and the silent dead remote regressing. This is a device behavior change on
the Fujifilm connect path and needs a code review plus an on device Fujifilm
connect sanity check before merge.

## Scope notes

- The SDL sim was not extended in this PR. Its `Control` and `Camera` are fakes,
  so a fault injected there exercises the fake state machine rather than the
  shipping code, with a real risk of a sim only false positive (see the PR113
  remote back trap that was dropped after hardware review). The defensible
  findings here come from the host harness driving the real vendor connect code.
- The dead camera disconnect 30 s freeze and stale target work is handled
  separately and is not duplicated here.
