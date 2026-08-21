# PR99: Fix the NimBLE client leak that breaks all connects until a reboot

## Goal

Stop furble leaking a NimBLE client on every failed connect or reconnect
attempt. The leak exhausts the fixed NimBLE client pool after nine failures, so
every later connect fails at `NimBLEDevice::createClient()` and the device
cannot talk to any camera until it is rebooted.

Line anchors below were read on branch `fix/nimble-client-leak` off fork
`master` at `734b759`.

## Motivation

The long standing symptom was "connection establishment is just broken, but a
reboot fixes it, it is a state issue". On hardware (M5StickS3 + Fujifilm X100VI
Secure) the failure is deterministic. When a Fujifilm camera still holds its
previous BLE session, each reconnect attempt fails and furble enters its
reconnect backoff loop. After a handful of attempts the serial log shows:

```
E NimBLEDevice: Unable to create client; already at max: 9
I furble: Failed to create client
W furble: Reconnect failed; camera may still hold the previous session. Waiting 17000 ms before the first retry.
I furble: Reconnect retry 1, waiting 17000 ms.
... (retry 2 waiting 5000 ms, retry 3, ... up to 8+)
```

Once "already at max: 9" appears, every further connect fails at
`createClient()` returning null, for any camera, until a reboot. After a reboot
(uptime reset) a single `connect 0` succeeds immediately: Scanning -> Connecting
-> Connected -> shutter works. That proves the leak is the sole cause: the pool
is full of orphaned clients from the failed attempts, and only a reboot clears
it.

## Root cause

`Camera::connect()` (lib/furble/Camera.cpp) creates a client and arms
self-delete:

```cpp
m_Client = NimBLEDevice::createClient();      // Camera.cpp:207
...
m_Client->setSelfDelete(true, true);          // Camera.cpp:216
```

`setSelfDelete(deleteOnDisconnect, deleteOnConnectFail)` only frees the client
through a NimBLE event:

- `deleteOnConnectFail` frees it inside `NimBLEClient::connect()` when the
  connection procedure is actually started and then fails (the synchronous
  `error:` path, NimBLEClient.cpp:321 `deleteClient(this)`).
- `deleteOnDisconnect` frees it inside the disconnect callback when a live link
  drops (NimBLEClient.cpp:1090).

Both require that `NimBLEClient::connect()` was reached. The Fujifilm secure
reconnect never gets there. `FujifilmSecure::_connect()` scans for the
advertising camera first and only calls `m_Client->connect()` afterwards:

```cpp
// FujifilmSecure.cpp
scan.start(this, SCAN_TIME_MS);               // :102
...
if (timeout == pdFALSE) {
  ESP_LOGI(LOG_TAG, "Timeout waiting for camera");
  return false;                               // :114  no connect() yet
}
if (!success) {
  ESP_LOGI(LOG_TAG, "Failed to scan paired camera");
  return false;                               // :119  no connect() yet
}
ESP_LOGI(LOG_TAG, "Connecting to %s", ...);
if (!m_Client->connect(m_Address))            // :124  first connect() call
  return false;
```

When the camera still holds its previous session it does not advertise for
pairing, so the scan times out and `_connect()` returns false at line 114 (or
119), before `m_Client->connect()` is ever called. No connect or disconnect
event can fire for a client that never started a connection procedure, so
`setSelfDelete` never runs.

Back in `Camera::connect()`, the failure handler had only two branches:

```cpp
if (connected) { ... }
else if (m_Connected) { this->_disconnect(); m_Connected = false; }
```

On this path `connected` is false and `m_Connected` is false (onConnect never
ran), so neither branch executes. `m_Client` still points at a live, never
freed NimBLE client. The next attempt overwrites `m_Client` with a fresh
`createClient()` and orphans the previous one. NimBLE caps the pool at
`CONFIG_BT_NIMBLE_MAX_CONNECTIONS` (9). After nine orphaned clients the pool is
full and every `createClient()` returns null, which is the "already at max: 9"
log and the reboot-to-fix symptom.

## The fix

Add the missing failure branch in `Camera::connect()` (lib/furble/Camera.cpp).
When the attempt failed before the link ever came up, reclaim the client:

```cpp
} else {
  // onConnect never ran, so m_Connected is false and setSelfDelete never
  // fired. Reclaim the client so it does not leak from the fixed pool.
  NimBLEDevice::deleteClient(m_Client);
  m_Client = nullptr;
}
```

`NimBLEDevice::deleteClient()` is safe on both failure classes that reach this
branch because it first checks the client is still in the live client list:

- Scan timed out before `connect()` was called: the client is still live, so
  `deleteClient()` frees it. This is the leak that is fixed.
- `NimBLEClient::connect()` was reached and failed: `setSelfDelete` already
  freed the client, so it is no longer in the list and `deleteClient()` is a
  no-op. No double free.

`setSelfDelete(true, true)` is left unchanged, so the existing, hardware
verified connect-fail and disconnect self-delete behaviour (the cancel
mid-connect and disconnect lifetime handling) is untouched. This is purely the
missing reclaim on the never-started-connect path. It does not change the
supervision timeout or the lock-free `isConnected()` design.

## Regression test

`tests/host/client_leak_test.cpp` drives the real `Camera::connect()` against
MockNimBLE, which was extended to model the client pool the way the controller
does:

- `NimBLEDevice::deleteClient()` returns a client to the pool and is a safe
  no-op on a client already removed (models the membership check).
- `NimBLEDevice::liveClientCount()` reports created minus deleted.
- `NimBLEDevice::setMaxClients()` caps the pool, so `createClient()` returns
  null past the cap exactly like "already at max".
- `setConnectShouldFail(true)` models the failure class that leaks on hardware:
  a connect that never establishes and never triggers self-delete.

The test caps the pool at 9 and runs 12 consecutive failed connects, then a real
connect:

- The live client count never grows past one and returns to zero after each
  failed attempt (no leak).
- A genuine connect still succeeds afterwards (the pool is not exhausted).

A second case asserts `deleteClient()` reclaims once and is a safe no-op on a
second call, proving the fix cannot double free.

Teeth: with the `deleteClient()` fix removed, the test fails five checks. The
live count climbs every attempt and sticks at the pool cap, `createClient()`
then returns null, and the final real connect fails. Restoring the fix makes it
pass.

## Verification

- Host suite (PlatformIO cmake): 8/8 pass, including the new `client-leak`
  test.
- Mutation check: fix reverted -> `client-leak` FAILS (5 checks, live count
  grows to the cap, pool exhausted); fix restored -> PASS.
- Firmware build: `FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e m5stick-s3`
  succeeds.
- `clang-format` (21) clean on all changed C++ files.
- Hardware re-verification of the reconnect loop on the M5StickS3 is pending;
  the board is owned by another session. The host regression test reproduces
  the exact leak and pool exhaustion.

## Files

- `lib/furble/Camera.cpp`: reclaim the client on the never-started-connect
  failure path.
- `tests/host/nimble/MockNimBLE.h`, `tests/host/nimble/MockNimBLE.cpp`: model
  the client pool (deleteClient, live count, cap).
- `tests/host/client_leak_test.cpp`: new regression test.
- `tests/host/CMakeLists.txt`: register the test.
