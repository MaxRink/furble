# PR76: fast reconnect to a Fujifilm Secure camera stalls in "connecting"

## Status

Fix implemented. The on-device serial capture that was pending has been taken
(see "Hardware evidence" below) and confirms the root cause is the CCCD
re-subscribe writes on a stale-session reconnect. The fix bounds those writes so
they cannot block and makes every subscribe failure non-fatal. A host regression
test reproduces the stall and mutation-verifies the fix. Firmware and Basic-path
retest on the M5StickS3 is the remaining gate before merge.

This is a bug fix, not a feature, so it adds no new opt-in setting or toggle.

All line anchors below were read at commit `44404da` on `master` (includes the
#120 NimBLE client leak fix).

## Motivation

Hardware repro on an M5StickS3 with a Fujifilm X100VI (Secure mode), over the USB
console:

1. `connect 1` -> `Scanning` -> `Connected, transmit power requested 6 dBm` ->
   `Connected` -> state becomes `active`. Shutter works. Normal.
2. `disconnect` -> `Disconnected`.
3. An immediate `connect 1` (within one to two seconds): -> `Scanning` ->
   `Connected` (the BLE link is re-established) BUT the control state stays
   `connecting` and never promotes to `active`. Observed stuck for 90 seconds
   and longer. A `shutter` in that window does nothing because the state is not
   active.

So on a fast reconnect to a camera that still holds the previous bond and
session, furble gets the BLE link but the Fujifilm Secure registration handshake
does not complete the way it does on a fresh connect. This is a large part of
the "connecting is flaky, needs retries" experience. It is not the #120 client
leak (already fixed) and not a false-connected report (that does not reproduce
on master).

## How promotion to active actually works on master

This is the key correction to the original hypothesis. On master there is no
registration-confirmation gate. Promotion to active is driven purely by the BLE
link being up.

- `Control::task` state machine: `STATE_CONNECT` runs
  `setState(STATE_CONNECTING); setState(connectAll());`
  (`src/FurbleControl.cpp:156-159`). `connectAll()` runs synchronously in the
  control task.
- `Control::connectAll(void)` iterates targets and calls
  `camera->connect(m_Power, timeout)` inline (`src/FurbleControl.cpp:112`). If
  every camera reports connected, it returns `STATE_ACTIVE`
  (`src/FurbleControl.cpp:121-124`), keyed on `allConnected()`.
- `allConnected()` is `isConnected()` for every target
  (`src/FurbleControl.cpp:220-228`).
- `Camera::isConnected()` is `m_Connected && m_Client && m_Client->isConnected()`
  (`lib/furble/Camera.cpp:92-99`). It is purely link state. Nothing about
  registration or `m_Configured`.
- `Fujifilm::m_Configured` (`lib/furble/Fujifilm.h:67`) is set only by the
  `CHR_NOT1_UUID` notification handler (`lib/furble/Fujifilm.cpp:24-27`) and is
  read only inside a `#if 0` block in the Basic path
  (`lib/furble/FujifilmBasic.cpp:116-125`). The Secure path never reads it. A
  registration gate was proposed in PR #93 (plan 75) but was not merged and
  carries a Basic-model regression risk, so it must not be depended on here.
- Extra corroboration that master does not and cannot gate on that
  notification: `Fujifilm::notify` treats `CHR_NOT1_UUID` as configuration only
  when the payload is `0x02 0x00` (`lib/furble/Fujifilm.cpp:25`), but the
  X100VI golden capture shows the real payload is `0x01 0x00`. So even the flag
  itself is essentially never set on the Secure path today.

Conclusion: the app cannot be "waiting for a notification that never arrives on
reconnect", because master does not wait for any notification. The state stays
`connecting` for one reason only: the control task has not returned from
`connectAll()` yet, because `camera->connect()` has not returned.

## Root cause

The control task is blocked, or spinning a failing retry loop, synchronously
inside `FujifilmSecure::_connect()` after the link is up.

Call chain during the stall:

`Control::task` (`STATE_CONNECTING` already set, `src/FurbleControl.cpp:157`)
-> `Control::connectAll(void)` holding `Control::m_Mutex`
   (`src/FurbleControl.cpp:104`)
-> `Camera::connect()` holding `Camera::m_Mutex`
   (`lib/furble/Camera.cpp:28`)
-> `FujifilmSecure::_connect()` (`lib/furble/Camera.cpp:50`).

`_connect()` prints `Connected` at `lib/furble/FujifilmSecure.cpp:107` right
after `m_Client->connect(m_Address)` succeeds. That is the `Connected` line seen
on the console during the stuck reconnect. Everything after it is the Secure
registration handshake, and every step is a blocking GATT operation that waits
on the camera:

- `m_Client->secureConnection()` (`:111`), encryption using the stored bond.
- `getValue(PAIR_SVC_UUID, STATUS_CHR_UUID)` (`:118`), read camera status.
- `setValue(PAIR_SVC_UUID, STATUS_CHR_UUID, ack, true)` (`:125`), write ack with
  response.
- `setValue(PAIR_SVC_UUID, IDENT_CHR_UUID, name, true)` (`:137`), identify with
  response.
- twelve CCCD subscribe writes with response
  (`:155-161` and `:174-184`, via `Fujifilm::subscribe`,
  `lib/furble/Fujifilm.cpp:37-54`, `pChr->subscribe(..., true)`).
- `setValue(NOTX_SVC_UUID, GEOTAG_SYNC_INTERVAL_UUID, ..., true)` (`:189`).

On a fresh connect the camera is freshly registering furble and answers each of
these. On a fast reconnect the camera still holds the previous session and bond
(furble only drops the link on disconnect, `Camera::disconnect` ->
`Fujifilm::_disconnect` -> `m_Client->disconnect()`,
`lib/furble/Fujifilm.cpp:139-141`; it never removes the bond), and one of these
steps behaves differently. Two shapes are possible and only an on-device capture
can tell them apart:

1. One step blocks on its NimBLE completion semaphore. The state reads
   `connecting` for the whole block. If the operation eventually times out at
   the ATT layer it returns false and the attempt fails.
2. One step returns false quickly, `_connect()` returns false, `Camera::connect`
   tears the link down (`lib/furble/Camera.cpp:53-54`), `connectAll` counts a
   failure and returns `STATE_CONNECT` (`src/FurbleControl.cpp:130-135`), and
   the control task retries. With reconnect enabled
   (`Settings::RECONNECT`, `src/FurbleUI.cpp:1438`) it retries forever, so the
   UI shows `connecting` indefinitely; each retry hits the same stale-session
   condition. This matches "90 seconds and longer" as a few retry cycles.

Either shape is the same root cause: the Secure registration handshake in
`_connect()` does not complete on a reconnect where the camera still holds the
prior session, so `_connect()` never returns true and the state never leaves
`connecting`.

Why the Basic path does not show this: `FujifilmBasic::_connect` re-writes the
pairing token on every connect (`lib/furble/FujifilmBasic.cpp:89`), which the
camera accepts idempotently on both first pair and re-pair. The Secure path uses
BLE bonding plus a status/identify handshake on `PAIR_SVC_UUID`, which is where
the stale-session sensitivity lives.

### Confirming side effect

During the stall the control task holds both `Control::m_Mutex`
(`src/FurbleControl.cpp:104`) and `Camera::m_Mutex` (`lib/furble/Camera.cpp:28`).
`Control::getTargetStatus` takes `Control::m_Mutex`
(`src/FurbleControl.cpp:235`) and per-camera `isConnected` takes `Camera::m_Mutex`
(`lib/furble/Camera.cpp:93`), so the console `status` command should block while
`state` (which only takes `m_StateMutex`, `src/FurbleControl.cpp:312-313`) still
returns `connecting`. If the operator confirms `status` hangs but `state`
answers during the stall, that pins the block inside `_connect()` under both
mutexes. This is also why this fix must not reintroduce any mutex-across-delay
behavior (root trap).

## Fix options

Pick after the on-device capture below identifies the exact failing step. Do not
implement blind.

- (a) Treat an already-registered reconnect as complete. If, on the SAVED or
  paired path, the bond is present and the GATT services and characteristics
  resolve, skip or best-effort the status/identify re-handshake and promote once
  the subscriptions and shutter characteristic resolve. Lowest disruption if the
  failing step is the status/identify write, but risky: it must not skip a step
  that a genuine reconnect still needs, and must not change the first-connect
  path.
- (b) Bound the handshake and recover. Wrap the handshake steps in a bounded
  timeout, and on timeout either re-trigger the identify write once or tear down
  cleanly so Control settles to `STATE_CONNECT_FAILED` (or a clean retry)
  instead of appearing stuck. This removes the perpetual `connecting` symptom
  even if the underlying camera quirk remains. Note NimBLE already bounds each
  op at the ATT layer, so this mostly helps the retry-loop shape, not a single
  short op.
- (c) Force a clean session before reconnect. If the camera holds a stale
  session that blocks re-registration, drop the bond and force the camera to
  re-advertise and re-register. This is the most invasive and most likely to
  regress the normal reconnect, since Secure re-pairing needs the camera in
  pairing mode. Least preferred; only if (a) and (b) cannot work.

Constraints for whichever option lands (from CLAUDE.md hard traps and prior
work):

- Never hold `Control::m_Mutex` or `Camera::m_Mutex` across a delay (brick
  trap). Any bounded wait must run where `_connect` already runs, and must not
  add a delay under a newly widened lock.
- Keep `isConnected()` a link-state read; do not couple promotion to a
  notification flag in a way that regresses first-connect (that is the #93
  hazard).
- Free the NimBLE client on every failure path; do not reintroduce the #120
  leak. `Camera::connect` uses `setSelfDelete(true, true)` and calls
  `_disconnect()` on failure (`lib/furble/Camera.cpp:39,53-54`); preserve that.
- Must not regress the working first-connect, nor the Fujifilm Basic path.

## Exact on-device test to disambiguate

Run with a debug build and the USB console (serctl), capturing the serial log.

1. Reproduce: `connect 1`, wait for `active`, `shutter` (confirm it fires),
   `disconnect`, then within one to two seconds `connect 1` again. Capture the
   full serial log of the second connect.
2. Read the last `FujifilmSecure` `ESP_LOGI` line printed before the stall. The
   handshake logs each step by name: `Securing` / `Secured!`,
   `Requesting status`, `Responding status`, `Identifying` / `Identified!`,
   `Subscribing to <name>`, `Configuring ... geotag sync interval`. The last
   line before the stall names the exact failing step.
3. Note whether the stall is a single long block on one step (shape 1) or a
   repeating `Connecting -> Connected -> <same step> -> retry` cycle (shape 2).
   Check the `Settings::RECONNECT` value; if reconnect is on, expect shape 2.
4. Optional corroboration: during the stall, issue `state` (should answer
   `connecting`) and `status` (expected to hang under the held mutexes).

That single capture selects the fix option: a write step that returns false
points to (a) or (b); a step that blocks for the full duration points to (b); a
security/bond failure points to (b) or (c).

## Regression test to add once the fix lands

Add a host test under `tests/host` using MockNimBLE that models a reconnect
where the Secure registration handshake does not complete the way a fresh
connect does (the identified failing step returns false, or its completion
notification never arrives). Assert that furble either promotes to active when
the camera is bonded and known, or fails cleanly to a retry or
`STATE_CONNECT_FAILED`, rather than hanging in `connecting` forever.
Mutation-verify: the test must fail on master (hang or never-active) and pass
with the fix. Model both the first-connect and the Basic path in the same suite
to guard against regressions in those paths.

## Retest acceptance criteria

- Fast disconnect then immediate reconnect on the X100VI reaches `active` and a
  subsequent `shutter` fires.
- Normal first-connect after a fresh boot still reaches `active` and fires.
- Fujifilm Basic connect and reconnect still work.

## Hardware evidence (captured 2026-08-21, M5StickS3 + X100VI Secure, serial)

The pending on-device capture was taken. On a fast disconnect then reconnect the
second connect ran the full Secure registration handshake after the link was up:

    Securing -> Secured! -> Requesting status -> Responding status ->
    Identifying -> Identified! ->
    Subscribing to indication 1, 2 ->
    Subscribing to notification 1, 2, 4, 5, 6, 7, 8, 9, 10, 11 ->
    Configuring geotag sync interval -> Getting shutter service / characteristic

Two failure shapes appeared, both in the CCCD subscribe phase, because the camera
still held the previous session and thus the previous CCCD subscriptions:

- Non-fatal but slow: `Failed to subscribe to notification 8` and
  `Failed to subscribe to notification 10` were logged mid-handshake. Those two
  are in the second subscribe loop, which already tolerated a failure, so the
  handshake still finished, just slowly.
- Fatal stall (the reported bug): intermittently one CCCD subscribe write
  blocked. The write is an acknowledged CCCD descriptor write that waits for an
  ATT write response the stale-session camera never sends. The library wait is
  unbounded (`NimBLERemoteValueAttribute::writeValue` ->
  `NimBLEUtils::taskWait(taskData, BLE_NPL_TIME_FOREVER)`), so `_connect()` never
  returned and the state stayed `connecting` for 90 s and longer. Promotion to
  active is link-state only, so nothing else could rescue it while the control
  task sat inside the blocked write.

This selects fix option (b) plus the "already-subscribed is non-fatal" half of
(a). Option (c), a clean bond teardown, was rejected: Secure re-pairing needs the
camera in pairing mode, so forcing a fresh pair would break the normal reconnect
that the user expects to be automatic.

## Implemented fix

Two small, coupled changes. No new setting. No mutex is held across a delay
(the bounded write runs where `_connect()` already runs). `isConnected()` stays
lock-free. The connect failure and client-reclaim paths are untouched, so the
#120 leak fix is preserved.

1. Bound the CCCD subscribe writes by making them unacknowledged, scoped to
   Fujifilm only.
   - `Camera::gattSubscribe` gains a `response` parameter that defaults to
     `true`, the proven acknowledged behaviour every vendor relies on
     (`lib/furble/Camera.cpp`, `lib/furble/Camera.h`). It passes the value to
     `NimBLERemoteCharacteristic::subscribe`. This shared seam is used by all
     vendors (Canon EOS smart, Nikon base/smart/remote, Ricoh), so the default
     must stay `true` to leave their pairing and connect subscribe writes
     acknowledged and unchanged. None of those callers pass a `response`
     argument, so they all resolve to `true`.
   - `Fujifilm::subscribe` carries its own `response` parameter that defaults to
     `false` and forwards it explicitly into `gattSubscribe`
     (`lib/furble/Fujifilm.cpp`, `lib/furble/Fujifilm.h`). Only the Fujifilm
     path therefore issues the unacknowledged CCCD write.
   - An unacknowledged CCCD write takes the library `ble_gattc_write_no_rsp_flat`
     fast path with no `taskWait`, so it can never block on a write response the
     stale-session camera withholds. The notify callback is registered locally
     before the descriptor write, so notifications still arrive, and the write is
     delivered reliably at the link layer. Both the Fujifilm Secure and Fujifilm
     Basic subscribe loops go through `Fujifilm::subscribe`, so both now issue
     unacknowledged CCCD writes. This is a deliberate behaviour change on the
     Basic path too (an earlier note that "Basic is unaffected" was wrong: Basic
     shares `Fujifilm::subscribe`, so its CCCD writes did change to
     unacknowledged); it is covered by the Fujifilm Basic hardware retest below.
     Canon, Nikon, and Ricoh are unchanged: they keep acknowledged CCCD writes
     via the restored `true` default and have no coverage or behaviour delta
     from this PR.

2. Make every subscribe failure non-fatal in `FujifilmSecure::_connect`
   (`lib/furble/FujifilmSecure.cpp`).
   - The first subscribe loop previously did `return false` on any failure. It
     now logs and continues, matching the second loop.
   - The second loop previously hard-failed the geotag subscription. That
     hard-fail is removed; geotag sync is best-effort and does not gate the
     shutter. On a stale-session reconnect where a CCCD is already subscribed the
     handshake now always reaches the shutter characteristic and returns true.

The status read, the status ack write, the identify write, and the geotag sync
interval write are left acknowledged and unchanged. The capture showed those
steps completing on the stale session, so widening the change to them would only
add first-connect risk. If a future capture shows one of them blocking, it needs
the same unacknowledged treatment.

## Host regression test

`tests/host/reconnect_stuck_test.cpp` (registered as `reconnect-stuck` in
`tests/host/CMakeLists.txt`). It uses the MockNimBLE harness and the
`FujifilmVirtualCamera` peer, which gained a `setStaleSubscribeSession(bool)`
hook: when enabled the peer rejects an acknowledged CCCD subscribe write
(response = true), modelling the stale-session block, and accepts an
unacknowledged one (response = false), the bounded path the fix uses.

The test connects once (fresh session), disconnects, enables the stale-session
hook, then reconnects and asserts the reconnect completes within a bounded wall
clock, reaches connected, and can fire the shutter. A second case guards the
fresh first connect.

Mutation result: reverting the Fujifilm subscribe `response` to acknowledged
(setting `Fujifilm::subscribe`'s default back to `true`, the pre-fix behaviour)
makes the stale-session peer reject the write, the Basic handshake aborts,
`connect()` returns false, and the `reconnect-stuck` test fails on exactly the
stale-session assertions while the fresh-connect case still passes. Restoring the
`false` default returns the suite to green (9/9). Note the mutation is on the
Fujifilm path specifically: the `Camera::gattSubscribe` base default is `true`,
so the mutation that exercises the tooth is the Fujifilm-scoped one.

The Secure `_connect` scan path pulls in `Scan` and FreeRTOS, which the host
harness does not stub, so the test exercises the shared
`Fujifilm::subscribe` -> `Camera::gattSubscribe` seam through the Basic peer.
That seam is where the load-bearing bounded-write fix lives. The Secure-only
non-fatal loop change is covered by code review and the on-device retest below.

## On-device retest (M5StickS3 + X100VI Secure, over the USB console)

1. `connect 1`, wait for `active`, `shutter` (confirm it fires), `disconnect`,
   then within one to two seconds `connect 1` again. The reconnect must reach
   `active` and a `shutter` must fire, with no 90 s hang.
2. Repeat the fast disconnect/reconnect several times to exercise the
   intermittent stale-session block; every cycle must reach `active`.
3. Fresh boot, first `connect 1` still reaches `active` and fires.
4. Fujifilm Basic camera: connect and fast reconnect still work.
5. Geotag notification after reconnect. Because the CCCD subscribe write is now
   unacknowledged on the Fujifilm path, confirm the geotag subscription still
   delivers notifications after a fast reconnect: with GPS enabled (or a fixed
   test fix), reconnect and confirm the camera's geotag request notification is
   received and furble writes geotag data back. The notify callback is
   registered locally before the unacknowledged write, so notifications must
   still arrive; this step proves that end to end on the stale-session path.

## Deviation: saved-scan match never fires when the camera advertises only the Secure service (2026-08-28)

A second, independent reconnect stall was root caused on the bench on
2026-08-28 and fixed in `fix/secure-saved-scan-match`. It sits before the CCCD
work above: the SAVED reconnect scan itself never matches, so the console shows
`Scanning` and then nothing until `Timeout waiting for camera` at 60 s.

Root cause: `FujifilmSecure::onResult` accepted a saved-reconnect advertisement
only when it parsed as a Secure advertisement AND the device advertised the
pairing service (`123d8f06-...`). The X100VI in reconnect standby advertises
either the pairing service or ONLY the Secure service (`a9d2b304-...`),
depending on its session state. The reject was `ESP_LOGD` (compiled out) and
the scan runs with duplicates filtered, so one silent reject muted the whole
60 s window. This also blocked and now exonerates the plan 75 hardware gate
(false-connected doc, see plans/75-false-connected.md): the stalled reconnect
there was this scan-match bug, not a false-connected regression.

Fix: accept either service UUID in `onResult` and keep the serial compare as
the identity check. The first rejected Fujifilm-parsed advertisement per scan
window is now logged once at INFO so a silent window is diagnosable on the
bench.

Test gap closed: the host virtual camera built the Secure manufacturer data
without the 0x02 type byte (7 bytes instead of 8), so
`parseSecureAdvertisement` always failed against the mock and no host test ever
exercised a successful saved-scan match. The mock now emits the real 8-byte
form and the new `saved-scan-delivery` test covers pair, interactive
disconnect, and reconnect with the advertisement arriving about one second into
the scan, for both the pairing-service and the Secure-service-only variants.
Mutation checks: reverting the UUID OR fails the Secure-only variant; removing
the 0x02 byte fails the match test. Bench reconnect verification is pending and
camera-state dependent.
