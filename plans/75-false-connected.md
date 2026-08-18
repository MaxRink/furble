# PR75: false Connected when the camera is not in pairing mode

## Motivation

User report: the Fujifilm X100VI was left in its SETTINGS menu, not on its
Bluetooth pairing or shooting screen. The user started a connection from furble.
furble reported CONNECTED, but the camera did not actually connect. In that state
the shutter does nothing.

This is a diagnosis document. It traces exactly where furble commits to the
connected state, shows that the commit happens before any camera-side
confirmation, names the missing confirmation, and proposes a fix. No code change
lands with this document.

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

The actual app-level confirmation exists in the code and is deliberately not
waited on. `Fujifilm::notify` (`lib/furble/Fujifilm.cpp:24-27`) sets
`m_Configured = true` when `CHR_NOT1_UUID` delivers the two bytes `0x02 0x00`.
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

Nothing here waits for the `CHR_NOT1_UUID` `0x02 0x00` confirmation, or for any
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
  `CHR_NOT1_UUID` `0x02 0x00` notification is high confidence for Basic and
  medium-to-high for Secure. Secure subscribes to twelve notifications; which
  one (or which status value) uniquely distinguishes "registered" from "link up
  in settings menu" needs the live capture to confirm. The hardware agent's
  `false-connected-settings.log` capture is the input that identifies it: it
  records which notifications, if any, the camera sends while in the settings
  menu versus on the pairing screen.

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

1. In the Fujifilm `_connect()` paths, after the subscribe sequence, wait for the
   camera-side confirmation notification with a bounded timeout. Basic already
   has the mechanism: re-enable a variant of the `#if 0` loop that blocks on
   `m_Configured` (set by the `CHR_NOT1_UUID` `0x02 0x00` notification). Secure
   needs the equivalent confirmation identified from the capture, then the same
   bounded wait.
2. On timeout, return false from `_connect()` so `Camera::connect()` tears the
   link down (`lib/furble/Camera.cpp:53-54`) and Control does not promote to
   ACTIVE.
3. Report a distinct failure, not a generic connect fail: "camera did not
   confirm, put it in pairing mode." The current `STATE_CONNECT_FAILED` path in
   the UI (`src/FurbleUI.cpp:1239-1242`) can carry a specific message for this
   case so the user knows to leave the settings menu and enter the pairing or
   remote screen.
4. Keep the wait bounded and off the Control mutex. `_connect()` already runs
   outside the Control critical section, and the existing Basic loop used
   `vTaskDelay`, so this respects the "never hold the Control mutex across a
   delay" trap.

Risk and sequencing:

- The Basic-path re-enable is small and low-risk and could land next on its own.
  The confirmation mechanism (`m_Configured`) already exists; the change is to
  re-enable a bounded wait and fail on timeout.
- The Secure path (the actual X100VI failure) needs the confirming-notification
  identity that only a live capture gives. The hardware agent's
  `false-connected-settings.log` is the input: compare the notification traffic
  in the settings-menu case against a known-good pairing-screen connect to find
  the notification (or status value) that is present only on a real
  registration. Do not guess which of the twelve Secure notifications is the
  gate; confirm it from the capture first.
- All new blocking behavior stays behind a bounded timeout so a slow but genuine
  camera is not falsely rejected. Tune the timeout from the capture timings.

## Implementation state

Implemented. The defensive registration-confirm gate is in place for both the
Basic and Secure Fujifilm paths.

What landed:

- `Fujifilm::waitForRegistration(progress)` (`lib/furble/Fujifilm.cpp`): a
  bounded poll on `m_Configured` with `vTaskDelay`, 25 s timeout, 250 ms poll.
  It runs inside `_connect()`, which the Control layer calls after releasing its
  mutex (`src/FurbleControl.cpp` snapshots the targets, then drops the lock), so
  the wait never holds the Control mutex across a delay. On success it returns
  true; on timeout it logs a distinct warning ("Registration not confirmed ...
  put the camera in pairing mode") and returns false.
- `Fujifilm::notify` now sets `m_Configured` on the arrival of any notification
  on `CHR_NOT1_UUID` (service 4c0020fe char f9150137). The golden X100VI capture
  records that notification carrying payload `0100`, not the `0x02 0x00` the old
  `isConfigurationNotification` predicate expected, so gating on the predicate
  alone would have rejected a healthy connect. The arrival of the dedicated
  config/registration characteristic is the signal; the payload is not.
  `m_Configured` is now `volatile` for cross-task visibility and is cleared at
  the top of each `_connect()` so a stale confirmation cannot pass the gate on a
  reconnect.
- FujifilmBasic (`lib/furble/FujifilmBasic.cpp`): the `#if 0` wait loop is
  removed. It sat at progress 50, before `CHR_NOT1_UUID` was subscribed, so a
  naive re-enable could never have seen the notification. The gate now runs
  after the notification subscriptions, at progress 85, before shutter discovery.
- FujifilmSecure (`lib/furble/FujifilmSecure.cpp`): the same gate is added after
  the twelve notification subscriptions, before the geotag interval write, which
  matches the golden capture ordering (config notification at ~12 s, geotag write
  at ~15 s). Secure previously had no confirmation wait at all.

On timeout `_connect()` returns false, `Camera::connect()` tears the link down,
and Control settles to `STATE_CONNECT_FAILED` instead of `STATE_ACTIVE`. furble
no longer reports Connected when the camera never accepted registration.

Note on the settings-menu capture: `false-connected-settings.log` showed a
connect started from the camera settings menu that was genuine (both furble and
the camera reported connected), and the two `0100` notifications were not
observed inside that ~16 s capture window. That window ended at ~14.6 s, before
the golden capture's ~12 s notification plus margin, so the notifications most
likely arrived later. The 25 s timeout is deliberately generous to avoid
rejecting a slow but genuine camera. This is a defensive gate keyed off the
confirmation notifications; hardware verification on the X100VI is owed to
confirm it neither regresses a healthy connect nor lets the false-connected case
through.

A dedicated UI reason string (a distinct message on the connect-failed screen)
is a possible follow-up. This PR surfaces the distinct failure through the log
and a clean teardown.
