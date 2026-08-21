# PR76: fast reconnect to a Fujifilm Secure camera stalls in "connecting"

## Status

Diagnosis and fix plan only. No firmware change is made here. The exact failing
step needs one on-device serial capture to pick between the fix options, and the
board was not available to this work. Do not guess-implement.

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
