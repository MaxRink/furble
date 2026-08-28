# 151: Fujifilm registration follow-ups

Follow-up bundle for the PR #232 registration confirmation gate and its PR #239
saved-reconnect deviation. Three items: Secure stale-bond recovery, a public
registration timeout define, and reconciliation of the plans docs with the
implemented ad06c7b7 (GEOTAG_UPDATE) semantics.

## Motivation

PR #232 landed the registration gate: a Fujifilm connect only promotes to an
active shutter target once the camera confirms app-level registration on
CHR_NOT1 (f9150137, payload 01 00 on the X100VI, legacy 02 00 on older Basic
bodies). PR #239 added the saved-reconnect deviation: the camera skips CHR_NOT1
on a saved reconnect and pulses 01 00 on GEOTAG_UPDATE (ad06c7b7) every 10 s,
which is implicit acceptance because the camera only requests geotag data from
a client it has accepted.

Three loose ends remained:

1. A saved Secure camera whose camera-side pairing was deleted wedges forever.
   The local bond survives, encryption with the dead keys never succeeds, and
   no reconnect can ever succeed until the user manually forgets the camera.
   PR #93 identified this on the X100VI and carried a recovery that was never
   merged.
2. The registration wait timeout was a private class constant. Test builds
   already overrode it through the ad hoc FURBLE_HOST_REGISTRATION_TIMEOUT_MS
   command-line define, which doubled as the host/firmware code-path switch.
3. The plans docs describing the registration protocol predate the #239
   discovery and still describe CHR_NOT1 as the only acceptance signal.

## Data sources

- PR #93 hardware trace on the X100VI: camera-side bond deletion leaves the
  camera on the link while it refuses encryption; the local bond then fails
  every reconnect. Recovery diff salvaged and re-derived against current
  master.
- PR #232 registration gate semantics and the golden X100VI capture
  (plans/95-engineering-lessons.md): registration-accept notifications arrive
  as 01 00 on service 4c0020fe, characteristics f9150137 and ad06c7b7.
- PR #239 hardware trace 2026-08-28: saved reconnect stalls the full 25 s
  without the geotag acceptance path while 01 00 arrives on ad06c7b7 every
  10 s (plans/75-false-connected.md, deviation section).
- Ricoh rc=520 behavior (plans/147-connect-reclaim-order.md): a standby camera
  timing out the encryption handshake kills the link. A single occurrence of
  that shape is transient, not bond loss.
- X100VI bench run 2026-09-02, firmware dev+g337f6259
  (bench-logs/stale-bond-245-run2-0902-1113.log). furble's pairing was deleted
  on the camera only. Saved reconnect attempts 1 to 4 failed at the connect
  level ("Connection failed; status=13 Operation timed out"). Attempts 5 to 7
  got the link up ("Connected, Securing") and the handshake then failed
  "secureConnection: failed rc=13 Operation timed out" after ~30 s or
  "rc=520 Connection Timeout" after ~5 s, followed by "Disconnected" and
  another retry, past reconnect_attempt 7 with no end.

## Changes

### 1. Secure stale-bond recovery (lib/furble/FujifilmSecure.cpp)

`_connect()` snapshots `NimBLEDevice::isBonded(m_Address)` before connecting.
A `secureConnection()` failure on an attempt that started bonded increments a
per-camera run counter, `m_SecureFailures`. Below `SECURE_FAILURE_LIMIT` the
attempt just fails and the bond is untouched. On the second consecutive
failure the recovery runs:

1. delete the stale local bond, once, with a log line;
2. try exactly one fresh `secureConnection()` on the same link, which succeeds
   if the camera is already in pairing mode and then proceeds to registration;
3. otherwise call `Camera::setNeedsRepair()`, which tells Control to stop the
   reconnect cycle and prompt the user.

The counter resets on a completed handshake and on any attempt that starts
unbonded, so it always measures a run against the same keys. Attempts that
never got the link up never reach the securing step at all, so a camera that is
simply out of range or asleep contributes nothing to the run.

`m_Connected` no longer gates the recovery; it only guards the `m_Client`
dereference for the in-link retry (the #62 lifecycle rule: a security failure
that dropped the link can free a self-deleting client). The #232/#239
registration gate still runs afterwards, so a secure link alone never promotes
to active.

`SECURE_FAILURE_LIMIT` is two. One failed handshake is indistinguishable from a
lost pairing PDU, and deleting a good bond over it costs the user a needless
re-pair. Two in a row is systematic. Waiting longer is not free either: every
extra attempt is a ~30 s (rc=13) or ~5 s (rc=520) stall inside a loop the user
cannot escape.

### 1b. Re-pair outcome (Camera, Control, UI, console)

`Camera::needsRepair()` carries the verdict out of the vendor connect path. It
is lock-free, set only after the local bond has been deleted and the in-link
fresh pair has also failed, and cleared at the start of every attempt and by
`resetConnectionState()` on a fresh user connect request.

`Control::connectAll()` reads it after a failed attempt. When set it records a
user-facing reason naming the camera, logs it, and returns
`STATE_CONNECT_FAILED` immediately, bypassing both the infinite-reconnect
branch and the retry budget. `Control::getConnectFailReason()` exposes the
string; it is cleared when a new connect cycle starts.

The UI's `STATE_CONNECT_FAILED` handler reads the reason and, when it is
non-empty, raises a "Pairing lost" message box telling the user to put the
camera into pairing mode and connect again. The box follows the companion
pairing dialog pattern: its OK button joins the input group so it is operable
on non-touch devices, and closing it restores the previous focus. An ordinary
connect failure still carries no reason and behaves exactly as before.

Debug builds print `control.connect_fail_reason` in the console status block,
so the bench can assert the outcome without reading logs.

Other vendors are unaffected. The recovery lives entirely in
`FujifilmSecure::_connect()`, and Ricoh keeps its own, separate bond clear,
which is scoped to `PairType::NEW` (a live-scan pairing) and never fires on a
saved reconnect.

### 1c. Cancel during the security handshake (lib/furble/Camera.cpp)

The 2026-09-02 follow-up bench run found the other half of the problem. With
the camera-side pairing deleted, a UI-driven connect "locked the stick": the
user could not pair and could not cancel, twice in a row, and only deleting the
saved entry and pairing from scratch recovered it.

Cause: NimBLE holds the connect task inside `secureConnection()` for the whole
pairing timeout, up to 30 s for rc=13. The plan-148 cancel token is polled
around that call, never inside it, so a user cancel did nothing for the whole
wait, the interactive disconnect sat waiting for the attempt to unwind, and the
device looked dead. `ble_gap_conn_cancel()` in `Control::disconnect()` already
covered the earlier GAP connect phase; nothing covered the phase after the link
came up.

`Camera` now publishes the in-flight client to the cancel path in
`m_CancelClient`, guarded by a dedicated `m_CancelMutex`. The pointer is set
right after `createClient()` and withdrawn the moment `_connect()` returns, so
it is non-null exactly across the window where NimBLE self-delete is off (the
#62 lifecycle rule) and no other task can free the client.
`Camera::abortBlockingConnect()` takes that mutex and terminates the link,
which makes the blocking call fail at once; the vendor path then sees the
token, or the cleared connected flag, on its next check and unwinds exactly as
it does for any other mid-connect link loss. The mutex nests no other lock, so
there is no ordering hazard with `Camera::m_Mutex`, which the attempt holds
throughout.

The terminate is deliberately not folded into `cancelConnect()`.
`Control::disconnect()` calls that under its own `m_Mutex`, where the
"state only, no radio calls under m_Mutex" rule applies. It therefore sets the
token in that block, snapshots the cameras, and calls `abortBlockingConnect()`
after releasing the mutex. Ordering still holds: the token is what the vendor
waits poll, and the terminate is only the wake-up for a call that is not
polling at all.

### 1d. Dismissable connect errors and the already-saved pairing refusal

A failed connect used to drop straight back to the menu with only a log line,
so the user could not tell a camera that was out of range from one that had
lost its pairing. `UI::showConnectError(title, text)` now puts up a message box
that stays until it is dismissed, following the companion pairing dialog
pattern: the OK button joins the input group so it works on non-touch devices,
and closing it restores the previous focus. Three cases use it:

- "Pairing lost" with the Control reason, for the stale-bond outcome above;
- "Connect failed" naming the camera, for every other connect failure;
- "Already saved", for a refused duplicate pairing.

The last one is new behaviour, not just a message. Pairing a camera the saved
list already holds used to start a connect and then save a second record,
because `CameraList`'s index is keyed on the BLE address and a Fujifilm Secure
body advertises a resolvable private address that changes with every pairing.
The result was two entries for one camera, with the saved reconnect picking
whichever the index happened to hold.

`CameraListProtocol::sameSavedIdentity()` is the identity rule: same vendor
type, plus either the same address or the same advertised name. An empty name
never matches, so an unnamed advertisement can still be paired.
`CameraList::isSaved()` applies it over the saved records, rebuilt into a local
vector rather than through `load()` so the live scan list survives the check.
`UI::beginPairing()` is the single entry point for "the user asked to pair this
scan result", used by the Scan page row and, once PR #265 lands, by the console
`pair <scan-index>` verb, so the refusal is written once and both paths get it.
The Connect page keeps the old direct path: everything on it is saved by
definition.

Two identical bodies advertising the same name do collide. Refusing is the safe
side of that trade, since the user can delete the saved entry and pair again,
whereas a silent duplicate quietly corrupts the list.

### 2. Public registration timeout define (lib/furble/Fujifilm.h)

`FURBLE_HOST_REGISTRATION_TIMEOUT_MS` is now a header-level public define with
a 25000 ms default, wrapped in `#ifndef` so build flags override it. The class
constant `REGISTRATION_TIMEOUT_MS` aliases it. Because the macro is now always
defined, the host/firmware code-path split in Fujifilm.cpp keys off
`ESP_PLATFORM` (the existing lib/furble convention, see BtDebugJournal) instead
of the macro's presence. Host test targets keep their 100 ms and 10 s
overrides unchanged.

### 3. Doc reconciliation

- plans/75-false-connected.md: the fix-design section no longer claims that
  01 00 on GEOTAG_UPDATE is never registration confirmation; it points at the
  implemented #239 deviation.
- plans/76-reconnect-stuck.md: the diagnosis-era statement that the PR #93
  gate "was not merged and must not be depended on" carries an update note:
  the gate merged as #232 and the geotag acceptance followed in #239.
- plans/95-engineering-lessons.md: the golden capture bullet now records the
  saved-reconnect semantics: CHR_NOT1 fires on fresh registration only, and a
  saved reconnect confirms through the periodic ad06c7b7 geotag request.

## Tests

- tests/host/fujifilm_stale_bond_test.cpp (fujifilm-stale-bond), Camera level:
  a bonded camera whose handshake times out every attempt keeps its bond after
  one failure and loses it on the second, exactly once, and is then flagged for
  a re-pair; the same verdict is reached through the shared `SecureTimeoutPeer`
  decorator for the rc=520 shape (failure delivered with the disconnect event
  still queued); a single timeout followed by success keeps the bond and raises
  no prompt; a refusal that leaves the link up recovers through the in-link
  fresh pair and proceeds to registration with no prompt; an unbonded refusal
  never touches the bond store however often it is retried.
- tests/host/fujifilm_repair_needed_test.cpp (fujifilm-repair-needed), through
  the REAL Control: a SAVED camera rebuilt from its NVS record, the production
  `Scan` fed by a background advertiser, and `connectAll(true)` (infinite
  reconnect, the mode the bench ran in). Asserts the cycle stops in
  `connect_failed` within bounds instead of looping, that it stays stopped and
  deletes no further bonds, that the reason names the camera and says to use
  pairing mode, and exactly one `deleteBond`. Two more scenarios cover the
  single-timeout recovery to ACTIVE with the bond intact, and the in-link fresh
  pair recovering to ACTIVE with no prompt.
- tests/host/ricoh_control_flap_test.cpp (ricoh-control-flap): the standby GR IV
  flap now also asserts that no camera is ever flagged for a re-pair, plus a
  new saved-bond scenario where a PairType::SAVED GR IV fails two consecutive
  handshakes (the run length that trips the Fujifilm recovery) and keeps its
  bond, is not flagged, and keeps retrying rather than stopping.
- tests/host/fujifilm_registration_cancel_test.cpp
  (fujifilm-registration-cancel): unchanged. Camera::cancelConnect() (the PR
  #242 token) lands mid registration wait for Basic and Secure; the wait aborts
  within one poll slice (observed 2 to 10 ms) instead of the 10 s build
  timeout.
- fujifilm-stale-bond also covers the cancel during the security handshake:
  SecureTimeoutPeer blocks the handshake for 10 s, the test cancels 200 ms in,
  and the attempt must return within 1 s and leave the camera disconnected.
- tests/host/camera_list_protocol_test.cpp (camera-list-protocol): the
  already-saved identity rule. Same address and type matches; a moved address
  still matches on the advertised name (the Fujifilm Secure re-pair case); a
  different name at a different address does not; a different vendor mode does
  not; an empty name never matches in either direction.
- sim/scenarios/e2e/scan-already-saved.txt (certified, m5stick-s3): seeds a
  saved camera, opens the Scan page, taps the row for the same camera, and
  asserts `ui.connect_error already_saved`, one live modal, `control.state
  idle` and no targets. New `ui.connect_error` query, documented in docs/sim.md.

The mock peer gained `setSecureTimeouts()` (a bounded run of handshake timeouts
that take the link with them, the bench shape) and `setRefuseWhileBonded()` (a
camera in pairing mode: dead keys refused on a live link, fresh pairing
accepted once the stale bond is gone).

Mutation evidence:

- `SECURE_FAILURE_LIMIT` 2 to 1: fujifilm-stale-bond fails 12 checks and
  fujifilm-repair-needed fails the whole single-timeout scenario (bond deleted,
  camera flagged, session never reaches active).
- Removing `setNeedsRepair()`: fujifilm-stale-bond fails both re-pair
  assertions and fujifilm-repair-needed times out waiting for `connect_failed`,
  which is the infinite loop the bench hit.
- Disabling the `repairReason` branch in `Control::connectAll()`: same
  fujifilm-repair-needed failure, the cycle never stops.
- Widening Ricoh's bond clear from `PairType::NEW` to any bonded camera:
  ricoh-control-flap fails the saved-bond scenario, proving that guard is live.
- Removing the link terminate from `Camera::cancelConnect()`: the cancel test
  takes 9797 ms instead of well under 1 s and fails the bound. That number is
  the frozen UI the bench hit, measured.
- Removing the name fallback from `sameSavedIdentity()`: camera-list-protocol
  fails "a moved address still matches on the advertised name", which is the
  only case that matters for Fujifilm Secure.
- Disabling the `CameraList::isSaved()` check in `UI::beginPairing()`: the
  scan-already-saved scenario exits 1, the connect starts and no box appears.

Verification:

- Host suite 95/95 ctests green (94 before, plus fujifilm-repair-needed).
- Simulator scenario manifest complete, scan-already-saved.txt certified.
- Python suite 133 passed.
- clang-format 21.1.2 clean, no em-dashes.
- Simulator builds and end-to-end scenarios green on all three modeled panels
  (135x240 M5StickS3, 80x160 M5StickC, 320x240 M5Stack Core).
- m5stick-s3-debug firmware compile succeeds, no sdkconfig drift.
- Coverage floor green: `tools/coverage.py --check` reports "Coverage is at or
  above every floor". src/FurbleUI.cpp 80.27% against a 79.40 floor,
  src/FurbleControl.cpp 77.38% against 73.90.

## Implementation state

Implemented, revised after the 2026-09-02 hardware gate.

Deviation from the original design: the refusal-only trigger does not fire on
the X100VI. The 2026-09-02 bench run (log cited under Data sources) shows that
after deleting furble's pairing on the camera only, the camera never issues a
definitive refusal. Every attempt that got the link up ended in a handshake
timeout, rc=13 or rc=520, and the previous trigger classified both as transient
because it required the link to still be up. On real hardware the recovery
therefore never ran and the user was left in an infinite reconnect loop with no
path back to a pairing except deleting the camera in the furble UI.

A follow-up run the same day, on the #261 firmware with reconnect off, added
the UX half: a UI-driven connect against the same camera locked the device.
Pairing did not work and the user "straight up can't cancel", twice, and only
deleting the saved entry and pairing from scratch recovered it. That is the
blocking `secureConnection()` described in section 1c, and it is why the
redesign has to cover cancel and the on-screen error, not only the bond.

Two further findings from that run shaped the redesign. Link state is not a
usable discriminator: rc=520 wakes the connect task with the disconnect event
still queued, so `m_Connected` reads true for a link that is already dead.
And a stopping condition was missing entirely: even with a correct trigger, a
deleted bond alone does not end an infinite reconnect cycle, so the user needs
an explicit re-pair outcome rather than a quieter loop.

The refusal case is not lost. It is now one member of the failure run rather
than a separate fast path, and the in-link fresh pair still recovers it inside
a single attempt when the camera is in pairing mode.

Sim follow-up: the simulator still runs the real UI against `FurbleControlSim`,
a fake Control, so there is no seam to inject a stale-bond fault and assert the
"Pairing lost" message box end to end. The dismissable error box itself is
covered by scan-already-saved.txt through the real `UI::showConnectError()`
path, so only the stale-bond trigger is missing. Once #261 lands the real
Control in the sim, add an e2e scenario driving the seeded Fuji peer with the
secure-timeout fault and asserting `ui.connect_error pairing_lost`.

Coordination: PR #265 adds the console `pair <scan-index>` verb. It routes
through the same UI request path, so pointing its handler at
`UI::beginPairing()` gives it the already-saved refusal with no duplicated
logic. Documented in docs/console-commands.md.

Hardware verification owed before merge, on the X100VI:

1. Pair the camera through the furble UI and confirm a normal shutter session.
2. Delete furble's pairing on the camera only. Leave furble's saved entry and
   local bond alone.
3. Reconnect from the console. Expect two link-up attempts whose handshake
   times out, then the "deleting the stale local bond" log line, then the cycle
   stopping in `connect_failed` with `control.connect_fail_reason` naming the
   camera and asking for pairing mode, and the "Pairing lost" box on screen.
   `control.reconnect_attempt` must stop climbing.
4. Repeat step 3 from the UI rather than the console, and confirm the same
   "Pairing lost" box appears. Same Control code, but the bench failure was
   UI-driven.
5. During one of those security waits, press Cancel. The device must respond
   immediately instead of freezing for the ~30 s handshake timeout.
6. Put the camera into pairing mode and connect again. Expect a fresh pairing
   and a normal registration through to an active shutter target.
7. On the Scan page, select a camera that is already saved. Expect the
   "Already saved" box and no connect.
8. Regression: a normal saved reconnect on a healthy pairing still connects,
   the bond is never deleted, and a connect to a powered-off camera raises the
   dismissable "Connect failed" box.
