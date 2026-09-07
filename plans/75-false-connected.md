# PR75: false Connected when the camera is not in pairing mode

## Motivation

User report: the Fujifilm X100VI was left in its SETTINGS menu, not on its
Bluetooth pairing or shooting screen. The user started a connection from furble.
furble reported CONNECTED, but the camera did not actually connect. In that state
the shutter does nothing.

This document records the diagnosis and implementation state for the
registration gate. The gate is implemented in both Fujifilm paths and covered
by production-path host tests. Hardware validation remains outstanding because
the cameras are currently unavailable.

## Where furble commits to Connected

The promotion to Connected is gated only on the BLE link and GATT plumbing. It
never waits for the camera to confirm that the app-level registration
succeeded.

The chain, from the UI back to the link layer:

1. `src/FurbleUI.cpp:1244` The UI switches to the Connected menu purely on
   `Control::STATE_ACTIVE`. There is no separate confirmation gate.

2. `src/FurbleControl.cpp:121-124` `connectAll()` returns `STATE_ACTIVE` as soon
   as `allConnected()` is true.

3. `src/FurbleControl.cpp:220-228` `allConnected()` is true when every target's
   `isConnected()` is true.

4. `lib/furble/Camera.cpp:92-99` `isConnected()` is
   `m_Connected && m_Client && m_Client->isConnected()`. This is pure link
   state. `m_Connected` is set true in `Camera::onConnect`
   (`lib/furble/Camera.cpp:14-19`) the moment the GATT link is up.

5. `lib/furble/Camera.cpp:50-58` `Camera::connect()` calls the vendor
   `_connect()`. On a true return it sets `m_Paired = true` and returns
   `m_Connected`. So the whole promotion depends on what `_connect()` returns.

The verdict hinges on what `_connect()` actually waits for.

## What the Fujifilm _connect() actually waits for

### FujifilmBasic (`lib/furble/FujifilmBasic.cpp:65-162`)

`_connect()` returns true after this sequence, and nothing more:

- `m_Client->connect(m_Address)` GATT link (line 72).
- discover pair service and characteristic (lines 77-84).
- write the stored pairing token, write with response (lines 86-90).
- write the identity string, write with response (lines 94-100).
- subscribe to the indication and notification characteristics (lines 106-141).
- fetch the shutter service and characteristic (lines 143-155).
- `return true` (line 161).

Every one of these is answered by the camera's ATT layer. A camera that has
accepted the LE link and kept its GATT server up will ACK the writes and accept
the subscriptions whether or not its companion-app logic has registered furble
as a remote. None of these steps proves the camera agreed to be controlled.

The actual app-level confirmation exists in the code and was deliberately not
waited on. The X100VI capture shows `CHR_NOT1_UUID` delivering `0x01 0x00`.
Older Basic bodies used `0x02 0x00`. Both payloads are accepted only on the
dedicated CHR_NOT1 characteristic.
That notification is the camera saying it accepted the registration. The wait
loop that would block the connect until `m_Configured` arrives is present but
compiled out:

```
lib/furble/FujifilmBasic.cpp:116-125
#if 0
  // wait for up to (10*500)ms callback
  for (unsigned int i = 0; i < 10; i++) {
    if (m_Configured) {
      break;
    }
    ...
  }
#endif
```

This `#if 0` block is the smoking gun. The confirmation is captured but the
promotion is not gated on it.

### FujifilmSecure (`lib/furble/FujifilmSecure.cpp:73-213`) - the X100VI path

The X100VI is a secured-Bluetooth camera, so the reported failure runs through
`FujifilmSecure::_connect()`. The same structural gap is present, with no wait
loop at all:

- scan for the camera advertising, keyed on the stored serial (lines 77-101).
  A camera with Bluetooth enabled in its settings menu still advertises, so this
  passes.
- `m_Client->connect` then `secureConnection()`. Security is re-established from
  the stored bond keys, so a camera that thinks it is already bonded lets this
  succeed even though its app never re-registered furble (lines 104-115).
- read `STATUS_CHR_UUID` and write a status response. The code only checks
  `status.size() == 4`, never the status content (lines 117-132). A "not ready"
  status of the right length still passes.
- write the identity, subscribe to twelve characteristics, configure the geotag
  interval, fetch the shutter service and characteristic (lines 135-208).
- `return true` (line 212).

Nothing here waits for the `CHR_NOT1_UUID` registration confirmation, or for any
other camera-side "registration accepted" signal. The secure path is arguably
worse than Basic because `secureConnection()` succeeding on a stale bond looks
like strong evidence of a real connection when it is not.

## Root cause verdict

furble promotes a target to Connected/ACTIVE after only the GATT link, security,
service and characteristic discovery, the token/identity/status writes, and the
subscribe calls. It does not wait for any camera-side confirmation that the
app-level pairing or registration succeeded. A camera sitting in its settings
menu, or holding a stale bond from a prior session, accepts the LE link and
answers every GATT operation at the ATT layer while never completing the
companion-app registration and never sending the confirming notification. furble
reads the link as up and reports Connected.

Confidence:

- The structural gap (promotion gated on plumbing, not on app-level
  confirmation) is confirmed in code with high confidence. The
  `#if 0` wait-for-`m_Configured` block in FujifilmBasic is direct evidence the
  confirmation exists and is intentionally not enforced.
- For the exact X100VI (Secure) case, that the missing gate is precisely the
  `CHR_NOT1_UUID` `0x01 0x00` notification is high confidence for Secure and
  medium-to-high for Basic, which also accepts the legacy `0x02 0x00` form.
  The X100VI capture is the source for the Secure characteristic and payload.

## Why the shutter silently does nothing

This matches the report and confirms the diagnosis. `sendShutterCommand`
(`lib/furble/Fujifilm.cpp:68-75`) only checks `m_Shutter != nullptr` and
`m_Shutter->canWrite()`. Both are true after discovery, so the write goes out
with response. A camera that never registered furble ignores the shutter write
at the application layer while still ATT-ACKing it. The write "succeeds" and the
camera does nothing. Connected but inert, exactly as reported.

## Fix design

Gate the Connected/ACTIVE promotion on the real vendor-handshake completion, not
on the GATT plumbing alone.

1. In both Fujifilm `_connect()` paths, after the subscribe sequence, wait for
   the camera-side confirmation notification with a bounded 25 s firmware
   timeout. Host builds use a short steady-clock seam for deterministic tests.
   `m_Configured` is set only by CHR_NOT1 with the captured `0x01 0x00` payload
   or the legacy `0x02 0x00` payload. This design originally treated the same
   bytes on GEOTAG_UPDATE as not being registration confirmation; hardware
   later proved saved reconnects confirm only through GEOTAG_UPDATE, see the
   deviation section below (PR #239, plan 151).
2. On timeout, return false from `_connect()` so `Camera::connect()` tears the
   link down (`lib/furble/Camera.cpp:53-54`) and Control does not promote to
   ACTIVE.
3. Return the failed handshake through the existing bounded connect-failure
   path. A distinct user-facing message such as "camera did not confirm, put
   it in pairing mode" remains a follow-up UI improvement, outside this
   protocol gate.
4. Keep the wait bounded and off the Control mutex. `_connect()` already runs
   outside the Control critical section, and the existing Basic loop used
   `vTaskDelay`, so this respects the "never hold the Control mutex across a
   delay" trap.

Risk and sequencing:

- The Basic and Secure paths now share the same dedicated-characteristic gate.
  The Secure identity comes from the captured X100VI `01 00` event. Basic keeps
  compatibility with the legacy `02 00` event.
- All new blocking behavior stays behind a bounded timeout so a slow but genuine
  camera is not falsely rejected. The implementation uses 25 s on firmware and
  a compile-time short timeout on host.

## Implementation state

Implemented in the follow-up registration-gate PR:

- Basic and Secure wait for CHR_NOT1 registration confirmation before shutter
  discovery and active promotion.
- A per-connection generation is captured by every callback. A notification
  queued by a previous NimBLE client cannot confirm a later reconnect.
- The confirmation flag is cleared at every connection attempt. Missing,
  malformed, empty, and unrelated notifications do not confirm registration.
- The wait exits on link loss or Control cancellation and has a 25 s firmware
  deadline. Host tests use a compile-time 100 ms timeout and a steady-clock
  shim, so timeout tests do not sleep for 25 s.
- The virtual peer drives the production Basic and Secure code paths. Tests
  cover positive `01 00`, legacy `02 00`, withheld registration, stale callback
  replay, reconnect reset, teardown, and bounded timeout behavior.
- Hardware status: not tested in this PR. Earlier X100VI captures provide the
  `01 00` payload evidence; a live camera sanity test is still required.

## Deviation: saved-camera reconnect confirmation (2026-08-28)

Hardware exposed a gap the day the gate merged: a saved X100VI reconnect does
not resend the `CHR_NOT1` confirmation. The camera goes straight to periodic
`01 00` geotag requests on `GEOTAG_UPDATE`, so the gate timed out after 25 s
on every reconnect and the connect retry loop never succeeded. A valid geotag
request now also confirms registration, because the camera only requests
geotag data from a client it has accepted. The original engineering-lessons
capture recorded acceptance notifications on both characteristics, which this
restores. The host suite covers the reconnect path with a virtual peer that
withholds `CHR_NOT1` and sends only a geotag request.

A second reconnect blocker found in the same bench session lives in the saved
scan itself, not in the registration gate: the SAVED-scan matcher rejected a
Secure-service-only advertisement, so the reconnect never even reached the
gate. That fix and its regression test are recorded in
plans/76-reconnect-stuck.md under "Deviation: saved-scan match never fires
when the camera advertises only the Secure service (2026-08-28)".
