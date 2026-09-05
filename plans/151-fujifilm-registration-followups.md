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

The counter resets on a completed handshake, on any attempt that starts
unbonded, and in `Camera::resetConnectionState()`, so it always measures a run
against the same keys inside one connect cycle. Attempts that never got the
link up never reach the securing step at all, so a camera that is simply out of
range or asleep contributes nothing to the run.

The counter lives on `Camera` rather than on `FujifilmSecure` for that last
reset. `resetConnectionState()` already clears `m_NeedsRepair` on a fresh user
connect request, and the run has to go with it. Otherwise "two consecutive
failures" spans days: one transient timeout on Monday, the user gives up, one
transient timeout on Friday, and a healthy bond is deleted behind a "Pairing
lost" box that is factually wrong. All the hardware evidence for the recovery
is back to back failures inside a single reconnect cycle (bench attempts 5, 6
and 7), which is exactly the window the counter now measures. `Camera` exposes
it as `noteSecureFailure()` and `clearSecureFailures()`; the limit stays vendor
policy in `FujifilmSecure`.

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

Multi-connect: one stale camera ends the whole session, deliberately.
`connectAll()` breaks out of the camera loop on the first failure and returns
`STATE_CONNECT_FAILED`, and the UI handler for that state calls
`doDisconnect()` before it raises the box, so a camera that connected earlier
in the same cycle loses its live session too. Control itself never touches the
healthy link: it leaves the session up and reports the outcome, and the reason
names only the camera that actually lost its pairing. Ending the session is the
UI decision, taken once on a state that is terminal anyway. The alternative is a
half connected session sitting behind a modal with no way to resume the cycle,
which is a worse contract than one clear stop with the offending camera named.
Pinned by the multi-connect scenario in `fujifilm_repair_needed_test.cpp` and by
the comment on the `STATE_CONNECT_FAILED` arm.

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

### 1c-bis. The 2026-09-04 bench wedge, and the regression that pins it

Section 1c argued from one bench observation that a cancel could not reach a
parked `secureConnection()`. The 2026-09-04 run measured the consequence at
scale: twenty console cycles of `connect 0` then `disconnect` at 2, 4 and 6
seconds left Control at `state disconnecting`, `connect_in_progress true`,
`connecting none`, the zombie count climbing, a fresh `connect 0` refused with
"already connecting, ignoring duplicate connect", and `task.control` at 0.0
percent CPU for more than two minutes until a reboot.

The chain is three links, and the first is the one this PR breaks:

1. `FujifilmSecure::_connect()` calls `m_Client->secureConnection()`. That is a
   blocking NimBLE call with its own internal timeout, not a poll loop, and
   `Camera::connect()` holds `Camera::m_Mutex` for all of it. The plan-148
   cancel token cannot reach it: the polls sit in the scan phase before the
   call, never inside it.
2. `m_ConnectInProgress` therefore stays true, so `targetTasksStopped()` and
   `disconnectComplete()` are both false and every interactive disconnect burns
   its whole cap and drains its targets into `m_ZombieTargets`.
3. `reapZombieTargets()` frees a drained target only once its task publishes
   `m_Stopped`. That task is blocked on the same `Camera::m_Mutex`, so it never
   does. Zombies accumulate and `teardownDraining()` gates `STATE_CONNECT`.

`tests/host/control_secure_stall_test.cpp` (ctest `control-secure-stall`) pins
it through the real Control. It came from the bench owner on
`test/secure-stall-regression` as an expected-to-fail harness and is
cherry-picked here; two changes were needed to make it measure what it claims.

**The peer has to be woken, not just blocked.** As handed over, the stall was
`std::this_thread::sleep_for`, which models the block and not the escape: a
terminate cannot shorten a sleep, so every disconnect in the harness would pass
by outwaiting the stall and the run would prove nothing about
`abortBlockingConnect()`. `setSecureConnectionStallMs()` now parks on a
condition variable that the peer's own `disconnect()` releases, which is what
`NimBLEClient::disconnect()` calls on a terminate. That models both halves:
NimBLE parks the caller, and NimBLE returns to it when the link dies underneath.
`secureStallWasAborted()` and `secureStallEntries()` make the difference
assertable, so a pass cannot come from the deadline expiring.

**The harness has to reach the handshake at all.** `Camera::connect()` sets
`m_Paired` on the first success, and `FujifilmSecure::_connect()` scans for the
advertisement before every attempt once that is set. With no radio answering,
every cycle after the baseline connect died in the scan wait: measured, the
stall was entered **0 times out of 9**, so the harness reported the wedge
whatever the connect path did. It now runs the same background `Advertiser` the
other Secure Control tests use, and the stall is entered 9 times out of 9.

What it asserts, on top of the handed-over baseline and fresh-connect checks:
every one of the nine disconnects ended the parked handshake by terminating the
link rather than outwaiting it, and none of them had to run the stall down to
get there.

The zombie count is deliberately not asserted directly, and the handed-over
harness was right to avoid it. `getDebugState()` is `FURBLE_CONSOLE` only, and
defining that for this target compiles eleven otherwise unbuilt blocks of
`lib/furble/Scan.cpp` into the coverage union as instrumented but uncovered
lines, which drops that file from 90.20 to 88.01 percent and fails its floor.
Measured, twice, on CI. The user-visible consequence stands in for it and is
what the bench measured anyway: a drain that never reaps keeps
`teardownDraining()` true, that gates `STATE_CONNECT`, and the fresh connect at
the end can then never reach active. For the record the quarantine does drain,
measured at 46 ms with the console surface temporarily enabled; a brief
quarantine is correct because freeing a target earlier would race the NimBLE
client its task is still releasing. The bench symptom was not that the count
went up, it was that it never came back down.

Measured on this branch: 9 entries, 9 aborts, slowest disconnect 23 ms against a
3000 ms stall. Mutation: delete the terminate from
`Camera::abortBlockingConnect()` and the run fails on
"every disconnect ended the handshake by terminating the link", because every
disconnect then has to outwait the stall. Reverted, green.

No simulator scenario in this PR, and that is scheduling rather than a
structural bar. The earlier claim here that it was structural was wrong. The
seam already exists: `lib/testing/nimble/MockNimBLE.cpp` guards its
`esp_timer_get_time()` use with `#if !defined(FURBLE_SIM)` and the simulator
supplies its own in `sim/shim/esp_timer.h`, and wall-clock peers already run in
the simulator today. Branch `feat/sim-cancel-sweep` carries
`lib/testing/peer/PeerStall.h`, which is the shared stall this lane wants.
Tracked as **plan 172**, the sim cancel-sweep lane, which owns driving this
stall from a scenario; this PR's host regression drives the production Control
end to end in the meantime.

### 1c-ter. Why the recovery never fired on hardware, and the two defects behind it

Bench step 3 on 2026-09-05, X100VI in pairing mode, firmware dev+g7987529d:
eight reconnects, rc=13 and rc=520 throughout, and the recovery never ran. No
`Security handshake failed (1 of 2)` line, no bond delete, no `Fresh pair
failed`, no "Pairing lost" box, `control.connect_fail_reason` still `none`. Two
independent defects, both now fixed.

**Defect 1: the bond was looked up under the wrong address.** The counter logic
was fine; it was never reached. `NimBLEDevice::isBonded()` compares against
`ble_store_util_bonded_peers()`, which returns **identity** addresses, and a
Fujifilm Secure body advertises a resolvable private address. So
`isBonded(m_Address)` answered false on every saved reconnect with the bond
sitting right there, and every attempt took the "nothing stale to measure"
early return, clearing the run on the way in and never counting a failure at
all. The recovery was unreachable on precisely the camera it was written for,
and the absence of any "(1 of 2)" line in the bench log is the tell.

The snapshot now happens after the link is up, where the controller has
resolved the RPA, and reads `m_Client->getConnInfo().getIdAddress()`. The bond
delete uses the same address; deleting by the advertised one would have left the
stale bond in the store even once the counter worked. With no bond, and so no
IRK to resolve with, the identity address is the advertised address and the
unbonded path is unchanged.

**Defect 2: a redundant security initiate parks the connect task forever.**
Every session that did manage to pair died the same way: `Secured!`,
`Requesting status`, `ble_gap_security_initiate: rc=2`, then half a minute of
nothing, then `Disconnected` and `readValue failed rc=271`.

The first reading of this was wrong and worth correcting, because the wrong
mechanism suggests a different fix. rc=2 is `BLE_HS_EALREADY`, a **local**
return from `ble_gap_security_initiate()` when the link's security is already
established. Nothing goes on air, so the camera never sees it and cannot
terminate over it. The damage is entirely on our side:
`NimBLEDevice::startSecurity()` maps `EALREADY` to success
(`NimBLEDevice.cpp:1267`, `return rc == 0 || rc == BLE_HS_EALREADY;`), so the
blocking `secureConnection()` arms its task data and waits
`BLE_NPL_TIME_FOREVER` for a `BLE_GAP_EVENT_ENC_CHANGE` that can never arrive,
because nothing was started. On the bench that held `Camera::m_Mutex` for
29.85 s, t=1638312 to t=1668162, until the camera gave up and dropped the link.

That makes this the **same unbounded-block class as the stale-bond stall in
section 1c-bis**, reached from a different direction: a bare blocking call with
no cancel path, parked on an event that is not coming. Two independent ways into
the same failure mode is the reason the plan treats "does this wait have a
bound" as the question worth asking of every NimBLE call, not just the ones the
bench has already hit.

`NimBLERemoteValueAttribute` retries a read or write that answers insufficient
encryption by calling straight back into `NimBLEClient::secureConnection()`,
which is how an already encrypted link comes to be asked at all.

Fixed in the vendored `components/esp-nimble-cpp`, in `secureConnection()`
itself rather than at either retry site: an already encrypted link reports
success without initiating, so the park is never entered. That fixes every
caller at once, both attribute retries and the async event path, and the
caller's retry then re-issues its read once more and gives up on its own terms.
Vendoring a fix here has precedent: plan 150 did the same for the task-data
race.

Upstream state, which plan 150's process asks for and this section owes:
upstream esp-nimble-cpp master has no equivalent guard. `startSecurity()` still
maps `EALREADY` to success and `secureConnection()` still waits forever on the
event it implies. So this is a divergence to carry, not a local backport of
something already fixed upstream, and it is a candidate to raise there. Plan
150's own un-vendoring exit condition is stale for the same reason: it is
written as though the task-data race were the only reason we hold a fork, and
it now needs this guard in its list before the fork can be dropped.

**Not fixed, and worth naming.** `CameraList::remove()` deletes the bond by
`camera->getAddress()` (`lib/furble/CameraList.cpp:138`), which is the advertised
RPA, so deleting a saved Fujifilm Secure camera in the UI leaves its bond in the
store. That is the same defect-1 class on a different path. It is not fixed here
because that path has no live link to resolve an identity address from: the fix
wants the identity address stored in the saved record, which is a stored-format
change and belongs with PR #266's naming work rather than bolted on here. The
consequence is a leaked bond entry, not a wedged session.

**Why the simulator cannot see defect 1.** The `fuji-secure-stale` topology
calls `NimBLEDevice::setBonded(true)`, which files the bond against every
address, so `isBonded(advertised)` answers true there and the sim would pass with
or without the fix. Only the host regression, which sets an identity address
distinct from the advertised one, can tell them apart. Worth knowing before
anyone reads a green sim run as coverage of this.

Regressions, both in `fujifilm-stale-bond`:

- the bench replayed with the advertised and identity addresses deliberately
  different, and the bond filed under the identity one: rc=13, then rc=520, and
  the bond is deleted exactly once with the re-pair prompt raised, then the
  third link-up pairs fresh and completes registration. Reading the bond by the
  advertised address again fails four checks, exactly as the bench did; deleting
  by it fails three.
- a successful connect establishes security exactly once, counted on the peer.
  Adding a redundant `secureConnection()` before the status read, which is what
  the library retry did, fails it. The library-side guard itself is not host
  reachable, since the host harness replaces `NimBLEClient` with MockNimBLE; the
  half that lives in furble is what this pins.

The mock bond store is address aware for this: `setBondedAddress()` files the
bond under one identity address and `setMockIdAddress()` gives the link an
identity distinct from the advertised address. Left alone it behaves exactly as
before, so no existing test changes.

### 1c-quater. A sim-fidelity regression this PR introduced, and the rule it teaches

Found by the #278 lane against head 3a9a3c44: `reconnect-registration-delay`,
which models the real X100VI confirming registration 30 to 40 s after a
reconnect, passed on master and on #272 and failed here. After the withheld
confirmation was released the session stayed `connecting` past 100 virtual
seconds with `reconnect_attempt` 0, stuck inside one attempt.

This one was ours, and it came in with the #266 conflict resolution rather than
with any of the recovery work. `Fujifilm::waitForRegistration()` chooses between
a FreeRTOS tick clock and `std::chrono::steady_clock`. Resolving that conflict
moved the selector from `#if defined(FURBLE_HOST_REGISTRATION_TIMEOUT_MS)` to
`#if defined(ESP_PLATFORM)`, because the timeout macro had become always defined
once `Fujifilm.h` carried its default and so was useless as a selector. The
simulator defines neither macro, so the two selectors send it opposite ways:

| build | old selector | new selector |
| --- | --- | --- |
| firmware | ticks | ticks |
| host harness | chrono | chrono |
| simulator | ticks | **chrono** |

The simulator runs on a virtual clock its driver advances. On the wall-clock
branch the loop sleeps with `std::this_thread::sleep_for` and measures
`steady_clock` against the 25 s timeout, so a scenario that releases the
confirmation and advances a hundred virtual seconds passes almost no real time:
the attempt neither sees the confirmation nor times out. Nothing to do with the
bonded snapshot or the failure counter; it sits on the reconnect path generally
and the stale-bond scenario merely walks through it.

**The rule: a clock selector keys on the clock's presence, never on a platform
macro.** `#if __has_include(<freertos/FreeRTOS.h>)` cannot be wrong the way
`defined(ESP_PLATFORM)` was, because the thing it tests is the thing the code
needs. Firmware and the simulator both have the header and both want ticks, and
the simulator's tick is the virtual clock its scenarios move. The host unit
targets that link no FreeRTOS shim keep the wall clock, which is right there
because nothing advances a virtual one for them. Deleting the branch outright
does not build: `furble_host_camera` has no shim on its include path.

Firmware behaviour is unchanged. ESP-IDF defines `ESP_PLATFORM` and provides the
header, so both the old and the new selector pick ticks, which is why the
95c66af1 bench stays valid until the post-#278 rebuild.

Regression: `fujifilm-stale-bond` gains a delayed-confirmation scenario carrying
the hardware fact. The 2026-09-04 console capture on a bonded X100VI reached
progress 85 at +25 s and active at +40 to +45 s against a 25 s
`REGISTRATION_TIMEOUT_MS`, so the first attempt after a reconnect can
legitimately time out and a later retry is what completes. It asserts that a
slow confirmation fails its own attempt on the timeout, never touches the bond,
never counts toward the stale-bond run and never asks for a re-pair, and that
the retry after the confirmation lands completes registration. Mutation: make
the wait never time out and the test hangs, which is the wedge. The simulator
scenario itself belongs to the #278 lane; it was borrowed to reproduce and not
committed here.

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

### 1e. Making the box fit the panel

The first version of that box was never sized. An LVGL message box defaults to
LV_SIZE_CONTENT, so a prose string made it wider and taller than the display. On
the 135x240 StickS3, the bench device, the title rendered as "lost" and the body
as "M X100VI no long / his pairing. Put th", cut on both edges; on the 80x160
StickC the OK button was drawn off the bottom entirely. The box existed, carried
the right text and was operable, and the instruction the user has to act on was
unreadable.

Sizing alone was not enough, and neither was "the box ends inside the display".
A message box clips its content area, so the box can end on the last row of the
panel while the body label runs six pixels past that clip box and the last line
is drawn at half height and cut. That is what 80x160 did while every assertion
passed. The acceptance test is therefore the content's scroll extent: anything
hidden means not fitted. `showConnectError()` now does this, in order:

1. Bind the box to the display width and wrap the body, the shape the low
   battery box already uses.
2. Split the message on its first ": ". Every caller composes it as
   `<camera>: <instruction>`, so the camera gets a line of its own. This matters
   because PR #266 grows a name to model plus serial, up to 25 characters, which
   is three wrapped lines on 80x160 and pushes the instruction out of the box.
3. If it does not fit, drop the title, name and body to the board's small font,
   the same font the Small text size setting selects there, and trim the theme
   padding to two pixels.
4. If it still does not fit, shrink the chrome. The header and footer rows are
   sized by the theme, and the flex-grown title stretches to whatever the row
   decided: measured on 80x160 they were 43 px each, 86 px of a 160 px panel,
   while the whole instruction had 62 px to live in. The footer row and its
   button get the small font and an explicit height; the header is trimmed and
   made content sized so a wrapped title can still take its second line.
5. If it still does not fit, pin the camera name to one ellipsized line. This is
   last because it is the only step that loses information: a panel with room
   shows the whole name over as many lines as it takes, a panel without room
   trades the tail of the name for the whole instruction. No message reaches
   this step today, on any panel, at any name length the code can produce.
6. Backstop: give the surplus back to the content area so the box ends inside
   the display and the OK button stays reachable. That leaves a scroll extent
   behind, which `ui.modal_overflow` reports as a failure. A box nobody can
   fully read is a bug and the scenarios should say so rather than hide it.

The title is wrapped rather than dotted on purpose. LVGL rewrites a
`LV_LABEL_LONG_DOT` label's own text to insert the ellipsis, so a dotted title
is invisible to `ui.connect_error`, which reads that text back. "Already saved"
is 13 characters and does not fit one 80x160 line, so it would have been
silently truncated on the smallest panel with nothing to catch it.

The messages themselves were shortened to match and all three share the
`<camera>: <instruction>` shape that step 2 splits on. All three also name the
camera through `Camera::getDisplayName()`, which substitutes
`Camera::DISPLAY_NAME_FALLBACK` ("The camera") when the body advertised no name:
the split gives the name a line of its own, so a raw empty name opens the box
with a blank line above the instruction. A saved record carries whatever the
body advertised, including nothing, so that is reachable rather than theoretical;
`tests/host/camera_regression_test.cpp` pins both halves. `Control`'s re-pair
reason is `<name>: put it in pairing mode, then connect.`, which is also what
the console prints as `control.connect_fail_reason`, so it still stands on its
own there. That exact string is pinned by
`tests/host/fujifilm_repair_needed_test.cpp`, not by substring, because the
separator is a contract between Control and the UI rather than formatting.
Control substitutes "The camera" for an empty name, matching the connect-failed
text, so the box can never open with a blank first line.

Rendered evidence, captured from the scenarios on each panel build. PR #266 has
merged, so the first three columns are rendered with the composed name it
produces, `FUJIFILM X100VI 1C4F9`, rather than the bare model; the last column
is the longest name that code path can produce, the hex fallback:

| Panel | Pairing lost | Connect failed | Already saved | Longest #266 name |
| --- | --- | --- | --- | --- |
| 320x240 M5Stack Core | full text | full text | full text | full name, full instruction |
| 135x240 M5StickS3 | full text | full text | full text | full name over three lines |
| 80x160 M5StickC | full text, small font | full text, small font | full text, small font | full name over three lines, full instruction |

Nothing is ellipsized on any panel today, including 80x160 with the composed
`FUJIFILM X100VI 1C4F9`, which wraps to two lines there, and with the longest
hex fallback `FUJIFILM X-H2S 3143344639`. Step 5 is a guard that no message currently
reaches: once the chrome shrink freed the header and footer rows, the name had
room to wrap. It stays because a longer name or a larger text size can still get
there, and the alternative to ellipsizing the name is losing the instruction.

All three scenarios carry a `capture`, so the render is evidence rather than an
assumption, and every run asserts `ui.modal_overflow no`.

### 1f. Catching a clipped modal at all

Nothing could see the bug above, which is the more interesting half.
`ui.overflow` measures the current menu page's scroll extent, and a message box
lives on the top layer outside any page. `ui.connect_error` reads the title
label out of the widget tree, which proves the widget exists with the right text
and says nothing about where it was drawn. So three certified scenarios passed
on three panels while the instruction was cut off at both edges.

`ui.modal_overflow` is the missing measurement. It walks the top layer and
reports `yes` in three shapes: a descendant drawn outside the display, a
descendant drawn outside its own parent's content box, and a scrollable with a
non-zero scroll extent. The third is the one a size-only comparison missed. The
first version of this query compared each label's size against its parent's
content size and never compared position, so two stacked labels that overflowed
in aggregate were invisible: on 80x160 the "Connect failed" box ended on the
last row of the display with `lv_obj_get_scroll_bottom(content) = 6`, the last
line of the instruction cut, and the query answered `no`. It now answers `none`
rather than `no` when the top layer has no visible children, so a scenario that
forgets to raise its modal cannot read a pass.

It is asserted `no` in all four connect-error scenarios, on every panel, in both
input layouts.

Known limit, by construction: a `LV_LABEL_LONG_DOT` label always fits its own
box, so an ellipsized label is reported clean. The camera name is deliberately
dotted, which is the trade step 5 makes, and the instruction beside it is a
separate wrapped label that is measured. Any label whose truncation must be
caught has to be wrapped rather than dotted, which is why the title was changed.

The last one is new behaviour, not just a message. Pairing a camera the saved
list already holds used to start a connect and then save a second record,
because `CameraList`'s index is keyed on the BLE address and a Fujifilm Secure
body advertises a resolvable private address that changes with every pairing.
The result was two entries for one camera, with the saved reconnect picking
whichever the index happened to hold.

`CameraListProtocol::sameSavedIdentity()` is the identity rule: same vendor
type, plus the same address, and for `Camera::Type::FUJIFILM_SECURE` only, the
same advertised name as a fallback. An empty name never matches, so an unnamed
advertisement can still be paired. `CameraList::isSaved()` applies it over the
saved records, rebuilt into a local vector rather than through `load()` so the
live scan list survives the check. `UI::beginPairing()` is the single entry
point for "the user asked to pair this scan result", used by the Scan page row
and, once PR #265 lands, by the console `pair <scan-index>` verb, so the refusal
is written once and both paths get it. The Connect page keeps the old direct
path: everything on it is saved by definition.

The name fallback is scoped to that one vendor on purpose. The rotating
resolvable private address is a Fujifilm Secure property; every other supported
camera keeps a stable address, so for them the address alone is a complete
identity and the name buys nothing. Applying it to them refuses a user who owns
two bodies of one model: on master the advertised name is the bare model, so a
second X-T5, a second GR IV or a second Sony body reads as already saved, with
no override and no way to add it. Multi-connect with two identical bodies, which
the saved list supports, would become unreachable. The protocol module carries
no `Camera` dependency, so the type code lives there as
`ROTATING_ADDRESS_TYPE` and `CameraList.cpp` carries the `static_assert` that
pins it to `Camera::Type::FUJIFILM_SECURE`.

Two Fujifilm Secure bodies advertising the same name still collide. Refusing is
the safe side of that trade, since the user can delete the saved entry and pair
again, whereas a silent duplicate quietly corrupts the list. PR #266 puts the
body serial in the advertised name, which makes the fallback discriminate
between bodies and closes the collision. #266 merges before this PR.

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
- tests/host/fujifilm_stale_bond_test.cpp also pins the three reset semantics
  the SECURE_FAILURE_LIMIT argument depends on, each verified by mutation:
  a completed handshake ends the run (fail, connect normally, fail again, and
  the bond survives); a user cancel during the security wait is not a failed
  handshake (cancel twice on a bonded camera and the bond survives with no
  prompt); and an unbonded attempt clears the run (so a leftover count cannot
  delete a freshly made bond on its first failure). The first of these needs a
  real reconnect after a real session, so `advertisement_scan_stub.cpp` gained
  `Furble::Host::setScanAdvertisement()`, which answers the saved-reconnect
  scan; the default nullptr keeps every other user of the stub silent. A fourth
  scenario pins the cycle bound: one failure, then `resetConnectionState()` as a
  fresh user connect request does, then another failure must still keep the
  bond.
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
  not; an empty name never matches in either direction. Plus the vendor gate: a
  second GR IV, a second Fujifilm Basic X-T5 and a second Sony body of the same
  model at their own addresses are all still pairable, while two Fujifilm Secure
  records with one name at two addresses stay one camera.
- tests/host/fujifilm_repair_needed_test.cpp also covers the multi-connect
  session: a healthy Fujifilm Basic body connects first, the stale Secure body
  then fails, and the cycle stops with a reason that names the stale camera and
  never the healthy one, exactly one bond is deleted, and Control leaves the
  healthy link up so the whole-session teardown is visibly the UI decision.
- sim/scenarios/e2e/scan-already-saved.txt (certified, all three panels): the
  already-saved refusal, end to end. It seeds `ble_peers fuji` plus `ble_saved
  true`, so the scan row and the saved record are one camera matched through the
  production `CameraList::match` and `save`, then activates the row and asserts
  `ui.connect_error already_saved`, one live modal, `control.state idle`, no
  targets, `ui.modal_overflow no`, and the dismissal.

  It was de-certified for one round because the row could not be activated
  reliably. Before PR #261 the row came from the FauxNY setting and was created
  inside `startScan()`, so it held the focus when the page loaded and a bare
  `action select` activated it. With the production `CameraList` the FauxNY row
  and the seeded saved camera are two different cameras, so the row has to come
  from the virtual radio instead, which materializes it after the page has
  already focused its back button. Moving the focus onto it is not reproducible:
  `key down` drives GPIO 38 for 80 virtual ms and the UI samples the button on
  its own cadence, so on a page busy draining scan results the press is missed
  about half the time (measured 3 of 6, 4 of 8 and 5 of 8 across variants, and
  0 of 6 under load; repeating the press makes it worse, because a press that
  does land walks the focus back off the row), and `btn a` and `btn b` do not
  navigate on that page in either layout.

  The fix is the `action scan-row N` verb the previous revision named as the
  follow-up: it dispatches the row's own click handler, `UI::beginPairing()`, so
  the scenario runs the production entry point without touching the focus. It is
  parsed and validated like every other parameterized verb: `sim-action-parser`
  covers the accepted and rejected spellings and the forged typed value, the
  four `sim/scenarios/invalid/action-scan-row-*.txt` fixtures pin the malformed
  forms to DSL exit 2, and the scenario itself asserts that an index past the
  end of the scan list reports `unavailable` rather than `applied`. That
  also matters beyond this scenario: "Already saved" is the longest of the three
  messages, and while it had no scenario it was the only box with no rendered
  evidence on any panel. It was hiding about two and a half lines on 80x160.
- sim/scenarios/e2e/stale-bond-pairing-lost.txt (certified, all three panels):
  the bench failure end to end through the production Control, Camera and UI.
  The new `fuji-secure-stale` topology is a Fujifilm Secure body the central is
  still bonded to and that no longer holds the pairing, so every handshake times
  out and takes the link with it. With infinite reconnect on, the scenario
  asserts the cycle ends in `ui.connect_error pairing_lost` with one live modal
  and `control.state idle`, then dismisses the box.
- sim/scenarios/bughunt/connect-fail-progress.txt (certified, all three panels)
  now also asserts the third call site: a camera that never links raises
  `ui.connect_error connect_failed`, and the box dismisses to `none`.
- sim/scenarios/e2e/connect-error-notouch-dismiss.txt (new, certified, all
  three panels): the same failed connect, with `seed no_touch true` so the sim
  builds the physical-button layout and the SDL touch device is detached. The
  box must appear and then clear through the button path alone. This is the
  scenario-seed form PR #264 established, so CI gates it; a
  `FURBLE_SIM_NO_TOUCH` environment override is a local convenience and no
  workflow sets it.
- All four certified scenarios also run green under the
  `FURBLE_SIM_NO_TOUCH=1` environment override on every declared panel.

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
- Removing the link terminate from `Camera::abortBlockingConnect()`: the cancel
  test takes 9797 ms instead of well under 1 s and fails the bound. That number
  is the frozen UI the bench hit, measured. `cancelConnect()` is token only by
  design, which is the whole point of the split; the terminate has never been in
  it.
- Removing the name fallback from `sameSavedIdentity()`: camera-list-protocol
  fails "a moved address still matches on the advertised name", which is the
  only case that matters for Fujifilm Secure.
- Removing the `ROTATING_ADDRESS_TYPE` gate from `sameSavedIdentity()`, so the
  name fallback applies to every vendor again: camera-list-protocol fails all
  three second-body cases (GR IV, Fujifilm Basic X-T5, Sony), which is the
  regression that made a second body of any model unpairable.
- Deleting `clearSecureFailures()` after a successful handshake:
  fujifilm-stale-bond fails "a success between two failures is not a run, so the
  bond survives", plus the bond and prompt assertions beside it. This is the
  mutation that survived the first review.
- Deleting the `connectCancelled()` guard from the failure branch:
  fujifilm-stale-bond fails "two user cancels never delete a healthy bond", the
  bond assertion and the prompt assertion. That is bench steps 4 and 5.
- Deleting the unbonded reset: fujifilm-stale-bond fails "the unbonded attempt
  cleared the run, so the fresh bond survives one failure" and the two
  assertions beside it.
- Deleting `m_SecureFailures = 0` from `resetConnectionState()`, so the run
  spans connect cycles again: fujifilm-stale-bond fails "the run does not span
  connect cycles, so the bond survives" and the two assertions beside it.
- Disabling the `needsRepair()` read in `Control::connectAll()`:
  fujifilm-repair-needed fails 8 checks across two scenarios, including the
  cycle never stopping and the reason never being recorded.

- Reverting the `lv_obj_set_width(m_ConnectErrorDialog, LV_PCT(100))` in
  `UI::showConnectError()`, so the box is LV_SIZE_CONTENT again:
  stale-bond-pairing-lost fails `ui.modal_overflow expected 'no' got 'yes'` on
  135x240 and on 80x160, and passes on 320x240, which is exactly the panel split
  the review measured by eye.

- Dropping the chrome-shrink step from the fit pass, so the header and footer
  keep the theme font and padding: on 80x160 all three of
  connect-fail-progress, scan-already-saved and stale-bond-pairing-lost fail
  `ui.modal_overflow expected 'no' got 'yes'`.
- With that mutation in place, additionally reverting the scroll-extent
  handling in both the fit pass and the query: stale-bond-pairing-lost **stops
  failing** on 80x160 while its box is still clipped. That is the blind spot
  exactly: a box that ends on the last row of the display with its last line
  below the content clip box is invisible to a bounds-only check, and the scroll
  extent is what turns it into a failing assertion. The other two keep failing
  because they also break the display bounds, which the position checks catch.
- Disabling the `CameraList::isSaved()` check in `UI::beginPairing()`:
  scan-already-saved exits 1, the connect starts and no box appears. Covered
  again now that the scenario is certified.

Every mutation above was run from a verified green baseline and reverted with a
verified green revert check, so no result is an artifact of a stale object.

Verification, on the head rebased onto master 8bdc52e4 (PRs #261, #264, #270,
#274 and #266):

- Host suite 97/97 ctests green, zero failures, `control-abort-republish`
  included. Earlier revisions of this plan carried a caveat about
  `sim-scheduler` failing under load; PR #274 fixed that, and it no longer
  applies.
- Python suite 144 passed.
- Simulator scenario manifest complete (`check_sim_scenarios.py`), portability
  contract clean, CI workflow trigger check passed.
- Simulator builds green on all three modeled panels (135x240 M5StickS3, 80x160
  M5StickC, 320x240 M5Stack Core). Certified suites on M5StickS3: e2e 86/86 and
  bughunt 8/8, no failures. The four connect-error scenarios pass on every panel
  in the touch layout and under the `FURBLE_SIM_NO_TOUCH=1` environment
  override, 24 runs in all.
- Coverage floor green, and worth a note: the CI coverage job failed once on an
  earlier head with `lib/furble/Camera.cpp: 73.28% below floor 73.75%`, then
  passed on a re-run of the identical commit with no change. Measured locally on
  that tree the host stack gave Camera.cpp 548 of 726 lines, 75.48%. Camera.cpp
  holds the timing-sensitive cancel and abort paths, so a loaded runner can take
  a different path through them while every test still passes; 532 against 548
  covered lines is that wobble. The floor for this file sits inside that noise
  band, which is worth knowing before someone ratchets it.
- clang-format 21.1.5 clean on every changed source, no em-dashes in the diff,
  no sdkconfig drift, plans/README.md row unchanged.
- Firmware compile is built and run by the bench owner rather than here.

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
than a separate fast path.

The in-link fresh pair after the bond delete, however, is unreachable on
hardware, and this paragraph used to claim otherwise.
`NimBLEDevice::deleteBond()` is `ble_gap_unpair()`, which terminates the live
link, so by the time the fresh pair is attempted there is no link left to pair
on and `Fresh pair failed` is the expected outcome. It survives as the correct
answer for the one shape that does keep the link, a camera refusing the dead
keys without dropping, which is what `setRefuseWhileBonded()` models and what
the in-link recovery scenario covers. MockNimBLE's `deleteBond` does not
terminate, which is why the host tests pass through that path and the bench does
not. Closing that gap means teaching the mock to terminate on unpair, which
would flip the in-link scenario to the prompt outcome; worth doing, and left for
the mock's own change rather than folded in here.

Sim coverage: closed. The earlier revision of this plan noted that the
simulator ran the real UI against `FurbleControlSim`, a fake Control, so there
was no seam to inject a stale-bond fault. PR #261 removed those substitutes, so
this PR rebases onto the real Control, Camera, CameraList and Scan in the
simulator and adds the `fuji-secure-stale` topology and
`stale-bond-pairing-lost.txt`. All three connect-error call sites are now
asserted end to end, each with its dismissal and each with `ui.modal_overflow
no`, on all three panels and in both input layouts. The last of them,
already-saved, needed the `action scan-row` verb to be reachable at all.

Coordination: PR #266 has merged and this PR is rebased on it. The predicted
conflict at the `logFirstReject` anchor in `lib/furble/FujifilmSecure.h`
happened exactly as described, and both insertions were kept: #266's
`composeName()` and this PR's `SECURE_FAILURE_LIMIT`. Three more came with it,
all additive: the `ble_peers` allowlist and its documentation in
`sim/CLAUDE.md`, where #266's `fuji-secure` and this PR's `fuji-secure-stale`
both belong, and the simulator query block in `src/FurbleUI.cpp`, where #266
adds `row_text` and `row_scrolling` and this PR adds `modal_overflow`.

One behavioural interaction, which is the point of the merge order: #266's
composed names now flow into these error boxes. A Secure body that advertised
`FUJIFILM X100VI` is shown as `FUJIFILM X100VI 1C4F9`, so the camera line in
every box is longer than it was. That is why `fujifilm-repair-needed` pins the
reason against `Camera::getDisplayName()` rather than the advertised string:
the two are no longer the same, and the displayed one is what the box has to
fit. Re-captured on all three panels; 80x160 wraps the composed name to two
lines and still renders the whole instruction with `ui.modal_overflow no`.

Coordination: PR #265 adds the console `pair <scan-index>` verb. It routes
through the same UI request path, so pointing its handler at
`UI::beginPairing()` gives it the already-saved refusal with no duplicated
logic. Documented in docs/console-commands.md.

Hardware verification owed before merge, on the X100VI:

1. Pair the camera through the furble UI and confirm a normal shutter session.
2. Delete furble's pairing on the camera only. Leave furble's saved entry and
   local bond alone.
3. Open the camera's own Bluetooth pairing screen and leave it there. This is
   not optional and it is not the same as step 6. Deleting the pairing on an
   X100VI stops it advertising until that screen is open, so with the screen
   closed furble's saved reconnect never finds the camera and the stale-bond
   path is never entered: the scan simply times out. The camera has to be
   advertising for any of steps 4 to 6 to mean anything.
4. Reconnect from the console with `connect 0`, then poll `debug control`.
   (`status` prints unprefixed lines; the `control.*` fields come from `debug
   control`.) The expected sequence, and these are the lines to check off:

   - attempt 1: `Connected`, `Securing`, a `secureConnection: failed` of any
     shape, then
     `Security handshake failed (1 of 2); retrying before any bond change`;
   - attempt 2: `Connected`, `Securing`, another failure, then
     `Security handshake failed 2 times on a bonded camera; deleting the stale
     local bond`, then
     `Fresh pair failed; put the camera in pairing mode and reconnect`.

   Then `debug control` reports `control.state: connect_failed`,
   `control.connect_fail_reason:` naming the camera and asking for pairing
   mode, a `control.reconnect_attempt` that has stopped climbing, and
   `control.zombies: 0`, with the "Pairing lost" box on screen and its full
   text readable on the 135x240 panel.

   The `Fresh pair failed` line is expected here, not a fault.
   `NimBLEDevice::deleteBond()` is `ble_gap_unpair()`, which terminates the live
   link, so by the time the in-link fresh pair is attempted there is no link
   left to pair on. The in-link fast path is unreachable on hardware for this
   shape; MockNimBLE's `deleteBond` does not terminate, which is why the host
   tests do not show it. An earlier revision of this plan predicted an in-link
   pair ending active at this step and was wrong.

   Legitimate alternative on attempt 1: `Secured!` roughly 200 ms after
   `Securing` with no furble line between them. That is NimBLE resolving
   `PINKEY_MISSING` and re-pairing by itself, below our visibility. It is a
   pass; carry on to step 6 and note it.
5. During one of those security waits, press Cancel. The device must respond
   immediately instead of freezing for the ~30 s handshake timeout. Do it a
   second time on the next attempt, then check `cameras list`: the camera must
   still be listed. The bond evidence is negative and it is the point of the
   step: no `deleting the stale local bond` line and no "Pairing lost" box.
   `cameras list` enumerates saved records, not NimBLE bonds, so it proves the
   saved entry survived and nothing more. Expect
   `Fujifilm Secure registration aborted by user cancel` or the vendor unwind.
6. Dismiss the "Pairing lost" box and run `connect 0` again. The bond is gone
   now, so this is a first pairing: `Securing`, `Secured!`, `Requesting
   status`, `Status`, `Identifying`, through to an active shutter target, with
   `debug control` reporting `control.state: active` and
   `control.connect_fail_reason: none`.

   Three things must **not** appear between `Requesting status` and `Status`,
   and they are the whole of defect 2: no `ble_gap_security_initiate: rc=2`, no
   gap of roughly 30 s, and no `readValue failed rc=271 Insufficient
   encryption`. Any of them means the redundant-initiate guard is not doing its
   job.

   Repeat the pair once from the UI rather than the console. Same Control code,
   but the original bench failure was UI-driven.
7. On the Scan page, select a camera that is already saved. Expect the
   "Already saved" box and no connect. Dismiss it with the physical buttons
   only, not the touchscreen: the OK button joins the input group precisely so
   the non-touch bodies can clear it, and a box that cannot be dismissed is the
   lockup this PR is about. `debug control` must still report
   `control.state: idle` with `control.targets: 0`.
8. Regression: a normal saved reconnect on a healthy pairing still connects,
   the bond is never deleted, and `connect 0` to a powered-off camera raises the
   dismissable "Connect failed" box with `debug control` reporting
   `control.connect_fail_reason: none`.
9. Regression on the identity rule, if a second body of any model is available:
   pair it and confirm it is accepted rather than refused as "Already saved".
10. The 2026-09-04 wedge, which is the step this PR now has a host regression
    for. With the camera's own pairing deleted so every handshake stalls, run
    twenty console cycles of `connect 0` then `disconnect`, varying the
    disconnect between roughly 2, 4 and 6 seconds into the attempt so it lands
    inside the handshake rather than between phases. After the loop
    `debug control` must report `control.state: idle`,
    `control.connect_in_progress: false` and `control.zombies: 0`, and a fresh
    `connect 0` must reach an active shutter target. The failure signature to
    watch for is the bench's own: `control.state: disconnecting`,
    `control.connect_in_progress: true`, `control.connecting: none`,
    zombies climbing, `task.control` at 0.0 percent CPU, and every later connect
    refused with "already connecting, ignoring duplicate connect".
