# 149 - Review feedback for the Ricoh GR IV standby wake

## Scope

This is the review-feedback record for
[149-ricoh-sleep-shutter-gate.md](149-ricoh-sleep-shutter-gate.md) phase 2, the
bounded relink wake, branch `fix/149-ricoh-wake`, PR 532 against fork master. The
review was asked to attack the wake rather than accept the green run: whether
`Camera::reconnectInPlace()` can race a disconnect, a cancel or a target reap,
whether it always returns the client to the pool or restores self-delete on every
path, whether the wake writes anything before a fresh CAPTURE read authorizes it,
whether the phase 1 refusal still holds when the wake fails, whether the three
budgets really bound the target task, and whether the test and the virtual peer
prove the standby path rather than only the awake one.

The branch carries no review threads on the pull request itself, so every finding
below is from this independent pass. Round 2 records the second pass, which
reviewed round 1's fixes instead of the original change. The head first
reviewed was `292962db`, on base `4043fba0`; master then moved twice (#493,
#530) and this record covers the tree that merges, that head rebased onto
`18bbcb5a` with the review commit on top. Line numbers are from that tree.

## What was reviewed

- Tree: a scratch worktree at `/tmp/furble-r532`, never the primary checkout.
- `lib/furble/Camera.{h,cpp}` and `lib/furble/Ricoh.{h,cpp}` against
  `origin/master` (`18bbcb5a`), plus the peer, the two host tests, the docs and
  the plan.
- The seams the wake depends on, read in full rather than inferred:
  `Camera::connect()` (`lib/furble/Camera.cpp:545`), `Camera::disconnect()`
  (`:1414`), `Camera::pollDelay()` (`:1431`), `Camera::reconnectInPlace()`
  (`:1455`), `Camera::abortBlockingConnect()` (`:1363`),
  `Camera::cancelConnect()` (`:1351`), `Camera::reclaimClient()` (`:1578`),
  `Camera::onDisconnect()` (`:219`), `Control::disconnect()`
  (`src/FurbleControl.cpp:965`) and `Control::reapZombieTargets()` (`:1216`).
- Vendored stack behaviour read from `components/esp-nimble-cpp`:
  `NimBLEClient::connect()`, `NimBLEClient::setConnectionParams()` and
  `NimBLEClient::secureConnection()`.
- Commands: `ctest -R '^ricoh-'` in a full host build, the red-first rebuild
  described below, and the regression probes below.

## Does the test fail without the change

Yes. Reverting only the four production files to `origin/master` and rebuilding
`ricoh_wake_test` and `ricoh_sleep_gate_test` in the same tree gives:

```text
$ ./ricoh_wake_test; echo EXIT=$?
FAIL: a sleeping body is woken by one reconnect
FAIL: wake emits flavor then OperationRequest in order
FAIL: the body reports CAPTURE after the wake
FAIL: the wake polls a fresh OperationMode read instead of reading once
FAIL: wake announces OperationMode CAPTURE on the notification stream
FAIL: wake announces CameraPower ON on the notification stream
FAIL: an awake body is not woken again
FAIL: a second press captures on the held link
FAIL: the wake still relinks a body that will not wake
FAIL: the wake waits the full probe budget before refusing
FAIL: a repeated press retries the wake on the relinked connection
FAIL: recovery reuses the relinked connection
FAIL: a failed reconnect reclaims the client instead of keeping a stale link
FAIL: the probe-time cancel was delivered on the live link
FAIL: a probe-time cancel had already relinked once
FAIL: the sampler case relinks exactly once
FAIL: the RSSI sampler skips the contended read instead of waiting
  (rssi sampler: 226 samples, 0 skipped, 0 ms)
  (PLAYBACK refused, 0 ms)
  (OTHER refused, 0 ms)
  (POWER_OFF_TRANSFER refused, 0 ms)
WAKE_EXIT=1

$ ./ricoh_sleep_gate_test; echo EXIT=$?
FAIL: the refusal path attempts exactly one wake reconnect
GATE_EXIT=1
```

That is the complete list, seventeen `ricoh_wake_test` lines plus the one
`ricoh_sleep_gate_test` line, measured on this tree rather than pasted from an
earlier run. Both required cases are red there (the successful wake and the
bounded timeout), and the three non-standby refusal cases pass, which is the
phase 1 behaviour that had to survive. Only the four production files are
reverted: the peer, the two tests and `tests/host/CMakeLists.txt` stay at the
branch head, which is why the wake constants still come from the CMake
definitions and the binaries link. Restoring the branch with `git checkout HEAD
--` makes both binaries exit 0.

Round 1 recorded thirteen lines here and the pull request body still pastes ten.
Both were stale in the same two ways. `a cancelled wake still relinked once` was
deleted by round 1 finding 3's fix and does not exist in the tree. And the
notification helper short-circuited on `&&`, so `wake announces CameraPower ON`
could never be printed; it is a real red line now that both assertions run. The
seventeen lines are the body's ten plus `a repeated press retries the wake on
the relinked connection`, `a failed reconnect reclaims the client instead of
keeping a stale link`, the two probe-cancel lines, the second notification line,
and the two sampler lines round 2 added.

`ricoh_wake_test` was run eight times in a row after the fixes in this record to
check the one threaded case for flake: eight passes.

## Does the peer model standby, or only the awake path

Standby, in both directions. `setSleepingBody()` puts the peer in
`OperationMode 0x02` BLE_STARTUP with `CameraPower 0x01` ON on a *held* link, so
the first connection sees exactly the state that wedges a real body. The peer
only leaves standby in `acceptConnection()` when `wake_on_reconnect` is set and
the link session count is past the first, so the modelled wake trigger is the
drop and re-establish the implementation performs, not a timer. A capture start
received while `operation_mode != CAPTURE` is counted in
`captureRequestsWhileAsleep()` and refused, so a gate regression is visible as a
number rather than as a silently recorded capture. `wake_on_reconnect` defaults
to false, so every existing fixture keeps its configured mode across a reconnect.

One limit worth recording: the peer's refusal covers the OperationRequest start
only. It accepts the ShootingFlavor write in any mode, so the test cannot show
that a real body tolerates one in standby. That is hardware gate 5 in the plan.

## Round 1 - findings and resolutions

| # | Source | Finding | Resolution | Evidence |
| ---: | :--- | :--- | :--- | :--- |
| 1 | Review | **`FAILED` does not write nothing.** The plan's state table said a failed wake writes nothing at all, and the phase 1 deviation text implied the only write class a wake adds is harmless. `Ricoh::_connect()` writes `PairedDeviceName` (Bluetooth Control service) and subscribes to four characteristics, and `reconnectInPlace()` reports failure when `m_Connected` is false even though `_connect()` returned true, which is exactly the window in which those writes have already gone out. | Fixed in the documents, not the code: the invariant is "no capture command", and the plan, `docs/protocols/camera-remotes.md` and the `Ricoh.cpp` comment now say which writes the handshake repeats and that hardware gate 5 owns the question. A regression probe confirmed the write exists: removing the new cancel pre-check makes `a cancelled press writes nothing at all` fail, because the cancelled press then relinks and writes the identity. | `lib/furble/Ricoh.cpp` `wakeFromStandby()` comment; `plans/149-ricoh-sleep-shutter-gate.md` invariant section and `FAILED` row; `docs/protocols/camera-remotes.md` wake-sequence row |
| 2 | Review | **The 5000 ms reconnect budget does not bound the reconnect.** `FURBLE_RICOH_WAKE_CONNECT_TIMEOUT_MS` reaches `NimBLEClient::setConnectTimeout()`, which bounds the GAP connect. The vendor `_connect()` also runs `secureConnection()`, which waits `BLE_NPL_TIME_FOREVER` on the GAP encryption event, and service discovery, the identity write and four subscriptions. So "a settle plus a connect that can reach its 5 s cap plus the 2 s probe" was not the worst case, and the real one can exceed `Control::disconnect()`'s 30 s `DISCONNECT_WAIT_MAX_MS`. | Fixed in the documents. The plan now states what each budget bounds, names `secureConnection()` and its `BLE_NPL_TIME_FOREVER` wait, cites the 29.85 s secure-timeout measurement already recorded in `plans/implemented/151-fujifilm-registration-followups.md:300`, and says the wake can consume the whole teardown wait. That 29.85 s is a plan 151 bench measurement and is the only occurrence of the figure: `lib/furble/Camera.cpp:1365-1366` records the same class as "up to 30 s", with no decimal value, so this row previously cited the wrong file. `README.md`, `docs/protocols/camera-remotes.md`, `lib/furble/CLAUDE.md` and `plans/CLAUDE.md` carry the same correction. No code change: the wait is inside the vendored stack, and `abortBlockingConnect()` already reaches it through `m_CancelClient`. | `components/esp-nimble-cpp/src/NimBLEClient.cpp` `secureConnection()` task-wait loop; `lib/furble/Camera.cpp:1381`; `include/FurbleControl.h:117` |
| 3 | Review | **A cancelled press still dropped and rebuilt a working link.** `connectCancelled()` was polled only inside `PROBE`, so a shutter command that had already been dequeued when `Control::disconnect()` armed the token ran the whole `RELINK`: it terminated a link the teardown then had to take down again and stretched the drain. The branch's own test asserted that behaviour (`a cancelled wake still relinked once`). | Fixed in the wake. `wakeFromStandby()` honours the token before it touches the link and transitions straight to `FAILED` with a `cancelled by the user` line, so a cancelled press writes nothing, relinks nothing and leaves the camera connected. The test now asserts zero reconnects, zero writes of any kind, `isConnected()` still true, and a return well inside the budget. | `lib/furble/Ricoh.cpp` `wakeFromStandby()`; `tests/host/ricoh_wake_test.cpp`; regression probe: with the check removed, `a cancelled press does not drop a working link to wake the body` and `a cancelled press writes nothing at all` both fail |
| 4 | Review | **The probe-time cancel lost its coverage.** The original test reached `PROBE` with the token already set, which is why it could see the probe's `connectCancelled()` branch. With finding 3 fixed, a pre-press token never reaches the probe, so the branch would have been untested. The host build compiles `Ricoh.cpp`'s wait on the wall clock (`__has_include(<freertos/FreeRTOS.h>)` is false for that target), so `furbleHostSetDelayHook()` cannot reach it. | Fixed in the test. A helper thread waits for the peer to accept the reconnect the wake performed and then calls `cancelConnect()`, which is the real `PROBE` state. The case asserts the token was delivered on a live link, one relink happened, no capture was written, and the press returned before the probe deadline. `tests/host/CLAUDE.md` records why the delay hook cannot be used. | `tests/host/ricoh_wake_test.cpp` `cancelDuringProbe()`; eight consecutive passes |
| 5 | Review | **A vendor `_disconnect()` could be skipped on the reclaim path.** `reconnectInPlace()` cleared `m_Connected` before calling `_disconnect()`. `Lumix::_disconnect()` and `Sony::_disconnect()` return early when `!m_Connected`, so on this seam they would leave their session state open and their GPS stop or release writes unwritten while the client was freed. Latent today because only Ricoh calls the seam, but it is a base-class contract, and `CLAUDE.md` invites vendors to use it. | Fixed in `Camera::reconnectInPlace()`. It runs `_disconnect()` while `m_Connected` still describes the link, exactly as `Camera::disconnect()` does, and clears the flag immediately after, before `setClientCallbacks(nullptr)` and `deleteClient()`. Nothing can race the free: `m_Mutex` is held for the whole sequence and `isConnected()` never dereferences the client. | `lib/furble/Camera.cpp` `reconnectInPlace()` failure branch; `lib/furble/Lumix.cpp:237`, `lib/furble/Sony.cpp:158`, compare `Camera::disconnect()` at `:1445` |
| 6 | Review | **A failed wake did not present as disconnected.** The reclaim path left `m_Progress` at whatever the aborted handshake had reached, while `Camera::disconnect()` zeroes it. | Fixed alongside finding 5: the failure branch clears `m_Progress` with `m_Connected`, so a failed wake looks like a failed connect to the status readers. The success path needs no change: a completed `_connect()` already ends at 100, so preserving the previous value is a no-op and is deliberately not implemented. | `lib/furble/Camera.cpp` `reconnectInPlace()` |
| 7 | Review | **`about 7 s of blocked target task` was stated three times** as the cost of a failed wake, and the UI-surfacing section used it to size the missing refusal surface. | Fixed. The plan now states that the cost is the settle, the reconnect and the probe, that the reconnect is the part that is not bounded by the connect timeout, and points at the budget section rather than repeating a single number. | `plans/149-ricoh-sleep-shutter-gate.md` "UI surfacing remaining" and hardware gate 8 |
| 8 | Review | **The plan's invariant contradicted its own phase 2.** The invariant section still read "BLE_STARTUP and POWER_OFF_TRANSFER must block all side-effect writes" while phase 2 relinks a BLE_STARTUP body and writes the identity during the handshake. | Fixed. The invariant is now "must block every capture write", it names the two capture writes, and a following paragraph states which writes the reconnect repeats and that the phase 1 test was narrowed to match. | `plans/149-ricoh-sleep-shutter-gate.md` "Invariant" |
| 9 | Review | **"no progress-0 blip is published to the UI" was false.** `Ricoh::_connect()` rewrites `m_Progress` from 0 to 100, and `FurbleCompanionService.cpp:399` and `FurbleMQTT.cpp:1769` publish it for a connected camera unconditionally. | Fixed in the wording, in all three statements that made the claim. The first pass called this fixed after correcting only the plan's state-machine notes, which left the claim standing twice: the phase 2 bullet in `plans/149-ricoh-sleep-shutter-gate.md:106` and the new `reconnectInPlace()` contract in `lib/furble/Camera.h:560`. Both now state the blip and why it is accepted instead of denying it, and the bullet names `Ricoh.cpp:186` and the two publishers. The claimed code fix was tried and rejected: preserving the old progress is a no-op because a completed `_connect()` ends at exactly 100, and the assertion a test could make cannot fail, so it was not kept. During the relink the link really is down, so a status reader sees the camera disconnected and its progress reset there, while Control stays active and publishes no connect state. | `plans/149-ricoh-sleep-shutter-gate.md` phase 2 bullet and state-machine notes; `lib/furble/Camera.h` `reconnectInPlace()` contract; `src/FurbleCompanionService.cpp:399`; `src/FurbleMQTT.cpp:1769` |

| 10 | CI | **Both new waits broke the plan 170 cancel contract.** `tests/test_connect_cancel_contract.py` fails a blocking function whose own braced body does not poll `connectCancelled()`, and the branch added two: `linkSettleDelay()` in `Camera.cpp` and `wakePause()` in `Ricoh.cpp`. Both sit on the reconnect path, which runs under `Camera::m_Mutex`, so `Control::disconnect()` could not settle the target until the whole settle or probe pause elapsed. The gate is body-local by design: a poll in the caller does not immunise the wait. | Fixed by making the waits poll in their own body and slicing. They were also merged into one seam, `Camera::pollDelay(milliseconds, sliceMs)`, which returns false when the token is set during the interval. The settle takes 20 ms slices, the probe pause 10 ms. `reconnectInPlace()` now skips the reconnect when the settle was cancelled and takes its ordinary failure path, and the probe fails with `cancelled during the probe`. | `lib/furble/Camera.{h,cpp}` `pollDelay()`; `lib/furble/Ricoh.{h,cpp}` `wakePause()`; `python3 -m unittest tests.test_connect_cancel_contract` 8/8 |
| 11 | CI | **`Ricoh.cpp` including `<freertos/FreeRTOS.h>` broke the static-analysis gate.** `cppcheck and clang-tidy` failed with "could not compile: clang-tidy exited 1 with build-static", reporting a typedef redefinition at `sim/shim/freertos/FreeRTOS.h` line 13 (`FurbleSimQueue *` against `FurbleHostQueue *`), on the `Ricoh.cpp` translation unit. The branch copied the `__has_include(<freertos/FreeRTOS.h>)` pattern into a vendor class, where the host compile database resolves it to the simulator shim while the same target also force-includes the host test shim, and the two `QueueHandle_t` typedefs collide. | Fixed by removing the include from `Ricoh.cpp` entirely: it calls the new `Camera::pollDelay()` instead, so exactly one translation unit owns the scheduler-clock rule, as `Camera.cpp` already did. `lib/furble/CLAUDE.md` records that a vendor class must not include the FreeRTOS headers for a wait. | `lib/furble/Ricoh.cpp` (no `freertos` include now); `lib/furble/Camera.cpp` `pollDelay()` |

## Findings judged false or already correct

| # | Candidate | Verdict | Evidence |
| ---: | :--- | :--- | :--- |
| 1 | `reconnectInPlace()` forgets `setConnectionParams()`, so the new link runs at NimBLE defaults and the conn-saver and dead-link detection break after a wake. | **False.** The parameters live on the `NimBLEClient` object (`m_connParams`) and are consumed at `ble_gap_connect()`, and the reconnect reuses the same client, so the new link is established with the values the last `connect()` or `setConnProfile()` set. | `components/esp-nimble-cpp/src/NimBLEClient.cpp:542-556` writes `m_connParams`; `:185-195` passes it to `ble_gap_connect` |
| 2 | A target reap can free the client under the probe, because `PROBE` reads `m_OperationMode` with no `Camera::m_Mutex` held. | **False.** `Control::reapZombieTargets()` frees a target only when `m_Stopped` is true, and the wake runs on that target's own task, which is inside `shutterPress()` and cannot have set it. `Control::teardownDraining()` also blocks a fresh connect while a drained target exists, so no new `connect()` can reset the camera underneath it. The one path that could reclaim the client, `reclaimClient()`, is reached only from that same stopped-target branch. | `src/FurbleControl.cpp:1216-1258`; `src/FurbleControl.cpp:1182`, `:1035` |
| 3 | A disconnect can free the client while `reconnectInPlace()` is between publishing and clearing `m_CancelClient`. | **False.** Both sides serialize on `m_CancelMutex`: `abortBlockingConnect()` holds it across its `disconnect()` call, and `reconnectInPlace()` takes it to clear the pointer before any free. The clear happens before the reclaim in every path. | `lib/furble/Camera.cpp:1381-1399`, `:1489-1497` |
| 4 | The wake can be entered with a stale `m_Paired` or a pending pairing request, and should call `clearPairingRequest()` like `connect()`. | **Not a defect.** A wake is not a connect: it keeps the existing bond and session, and the plan and `lib/furble/CLAUDE.md` record that `m_Paired`, `m_Active` and target ownership are deliberately untouched. A pending pairing request at wake time would mean a pairing dialog is open, which cannot be reached from the shutter path. | `lib/furble/Camera.h` `reconnectInPlace()` contract; `lib/furble/CLAUDE.md` |
| 5 | A wake blocks the UI or control task out of its watchdogs. | **Reclassified in round 2 as a real defect of this branch, and fixed there.** The original verdict called the control-task stall acceptable because the wake is bounded once a cancel is honoured, which covered only the cancel case: with no cancel, `Camera::m_Mutex` is held for the whole wake and the sampler waited on it for that long. `Camera::getRssi()` now `try_lock`s and returns 0 on contention, exactly as `Camera::updateConnStats()` already did, so the sampler skips a slice instead of stalling Control. Round 2 rows 1 and 3 own the fix and the wording. | `src/FurbleControl.cpp:556`, `:1555`; `lib/furble/Camera.cpp:1635-1650` (the `try_lock` is at `:1643`); `:954-957` (the `try_lock` is at `:956`); `src/FurbleCompanionService.cpp:400-402` |
| 6 | A failed wake leaves a dead session the user has to fix by hand. | **False.** `Control` re-checks `allConnectedLocked()` every 250 ms in `STATE_ACTIVE` and drops to `STATE_CONNECT` when a target reports disconnected, which runs the ordinary reconnect cycle with its existing backoff. `teardownDraining()` is clear because the target never stopped. | `src/FurbleControl.cpp:544-553` |

## Round 2 - review of the fixes in this record

A second pass checked the fixes above rather than trusting them, at head
`e2c8c2da` on base `18bbcb5a`. Line numbers are from that head. Its own record is
the review of PR 532 that prompted this section; the findings that survived are
below, most severe first.

| # | Severity | Finding | Resolution | Evidence |
| ---: | :--- | :--- | :--- | :--- |
| 1 | Blocker | Round 1 finding 9 was called fixed "in the wording only", and the false claim survived twice: the phase 2 bullet still read "no progress-0 blip is published to the UI" and the new `reconnectInPlace()` contract said a wake "does not publish progress 0". The mechanism is real: `Ricoh::_connect()` sets `m_Progress = 0` and both publishers send `getConnectProgress()` unconditionally, so a status reader does see the reset. | Fixed in all three statements that carry the claim. The bullet and the header contract now state the blip, name `Ricoh.cpp:186` and the two publishers, and say why it is accepted: the link really is down for the settle, so the reader already reports the camera disconnected there, and suppressing the reset would be a no-op because a completed `_connect()` ends at exactly 100. | `plans/149-ricoh-sleep-shutter-gate.md` phase 2 bullet; `lib/furble/Camera.h` `reconnectInPlace()` contract; `plans/149-review-feedback.md` round 1 finding 9; `lib/furble/Ricoh.cpp:186`; `src/FurbleCompanionService.cpp:399`; `src/FurbleMQTT.cpp:1769` |
| 2 | Blocker | Round 1 finding 2 cited the 29.85 s secure-timeout measurement to `lib/furble/Camera.cpp`, which does not contain that figure, and the plan repeated the citation. `Camera.cpp:1365-1366` records the same class as "up to 30 s". | Fixed in both places. 29.85 s is only in `plans/implemented/151-fujifilm-registration-followups.md:300`, a plan 151 X100VI bench measurement, and the row now cites that file and says separately what `Camera.cpp` records. The technical claim in the row was already correct, so only the citation moved. | `plans/149-review-feedback.md` round 1 finding 2; `plans/149-ricoh-sleep-shutter-gate.md` budget section; `lib/furble/Camera.cpp:1365-1366`; `plans/implemented/151-fujifilm-registration-followups.md:300` |
| 3 | Major | The wake stalls the control task, and the stall is new to this branch. `reconnectInPlace()` holds `Camera::m_Mutex` for the relink, `Camera::getRssi()` took the same mutex with a blocking lock, and `Control::sampleAdaptivePower()` calls it from `STATE_ACTIVE`, which is the only state a wake runs in. Round 1's verdict covered only the case where a cancel arrives. A cold connect could not hit it because the machine is in `STATE_CONNECTING` and does not sample. The companion task is already forbidden the same call, and `updateConnStats()` had already fixed this class with `try_lock`. | Fixed in `Camera::getRssi()`: it `try_lock`s and returns 0, the existing "no reading" value that `sampleAdaptivePower()` skips, so a skipped sample leaves the last filtered value in place. Pinned by a new `ricoh-wake` case, and the mutation isolates it: restoring the blocking lock makes exactly one assertion fail, `the RSSI sampler skips the contended read instead of waiting`. | `lib/furble/Camera.cpp:1635-1650` (the `try_lock` is at `:1643`); `:954-957`; `src/FurbleControl.cpp:556`, `:1555`; `src/FurbleCompanionService.cpp:400-402`; `tests/host/ricoh_wake_test.cpp` sampler case |
| 4 | Minor | Data race on the peer's `m_LinkSessions`: the probe-cancel helper thread reads it while `acceptConnection()` writes it, and the binary is in neither the ASan nor the TSAN set. | Fixed: `std::atomic<size_t>`. Each reader loads once (`reconnectCount()` no longer reads the counter twice for one answer) and `acceptConnection()` takes the incremented value from `++`. | `lib/testing/peer/RicohVirtualCamera.h`; `lib/testing/peer/RicohVirtualCamera.cpp:457-464`, `:474-492`; `tests/host/ricoh_wake_test.cpp` `cancelDuringProbe()` |
| 5 | Minor | A test comment claimed the delay hook delivers the probe-time cancel, contradicting the same file's other comment and `tests/host/CLAUDE.md`, which both say the hook cannot reach the wall-clock wait. | Fixed: the comment now says the host build compiles the wait on the wall clock, so the hook cannot reach it, and the helper thread delivers the token once the peer has accepted the reconnect. | `tests/host/ricoh_wake_test.cpp` probe-cancel case; `tests/host/CLAUDE.md` |
| 6 | Minor | The new rule "any wait on the connect path goes through `Camera::pollDelay()`, never a bare `vTaskDelay` or `sleep_for`" was stricter than the tree: seven waits already on that path do the job with a bare `vTaskDelay` and their own in-body poll, so the rule contradicted the code it was meant to describe. | Fixed by scoping the rule to new waits and listing the seven exceptions with their line numbers. Each already satisfies `tests/test_connect_cancel_contract.py`, which is body-local, so the gate never failed them and the rule said otherwise. The rule now also says to convert one only while already changing that function's body. | `lib/furble/CLAUDE.md` connect-path rule; `lib/furble/Camera.cpp:1245-1249`; `Fujifilm.cpp:155-159`; `FujifilmSecure.cpp:493`; `FujifilmBasic.cpp:141`; `CanonEOSSmart.cpp:131`; `DJIOsmo.cpp:326`; `Lumix.cpp:301` |
| 7 | Minor | The record was stale in four ways. The red-first list named `a cancelled wake still relinked once`, which round 1's own fix deleted. Two evidence line numbers pointed at the wrong statements: `Camera.cpp:1620` is `getType()`, and the `updateConnStats()` `try_lock` is at `:956`, not `:963`. The notification helper short-circuited on `&&`, so `wake announces CameraPower ON` could never appear in a red run. And the counts disagreed: body 220, record 222, tree 224. | Fixed and reconciled against the run above rather than against an older one: seventeen `ricoh_wake_test` lines plus one `ricoh_sleep_gate_test` line, the two evidence spans corrected, both notification assertions evaluated so the second line is a real red line, and 224/224 in the record and in the pull request body. The deleted test name is gone from both. | `plans/149-review-feedback.md` red-first section, round 1 finding 5 evidence, its Verification counts; `tests/host/ricoh_wake_test.cpp` `checkWakeNotification()` |
| 8 | Note | The pull request body still said a failed wake "writes nothing at all", which is the claim round 1 finding 1 corrected in the plan, and its red-first block pasted ten of the then-thirteen lines. | Fixed in the body while reconciling its test count: it now carries the plan's wording, "writes no capture command", and names the identity write and four subscriptions the handshake repeats. The red-first block lists the complete run. | `plans/149-ricoh-sleep-shutter-gate.md` `FAILED` row; pull request 532 body |

Two mechanical consequences of the fix are worth recording, because they touch
files the change does not otherwise own. Inserting lines in `Camera.h`,
`Camera.cpp` and `FurbleCompanionService.cpp` shifted six line-number citations
elsewhere in the tree, and `tools/check_doc_citations.py` fails on any of them,
so `docs/protocols/companion-gatt.md` (two), `plans/25-multiconnect-ui.md`,
`plans/51-app-feature-parity.md` and `plans/implemented/76-reconnect-stuck.md`
(two, both occurrences of `Camera::m_Mutex`) were re-pointed at the new lines.
No prose in those files changed. And `Camera::getRssi()` now reports 0 on
contention, so `src/CLAUDE.md` and the companion snapshot comment say that
instead of describing a blocking acquisition the code no longer makes.

Not redone, because the second pass confirmed them: no half-awake state on any
cancel path, no spin (the slice is clamped to the remainder), the disconnect
ordering fix and the pre-relink cancel check, and the 5000 ms correction in all
five claimed places. The "charging" overclaim has no referent in this tree, so
nothing was corrected for it.

## What the budgets really are

Recorded here so the plan and the code agree:

| Constant | Default | What it bounds |
| :--- | :--- | :--- |
| `FURBLE_RICOH_WAKE_SETTLE_MS` | 200 ms | A minimum. The link stays down at least this long; the camera's terminate latency is added. |
| `FURBLE_RICOH_WAKE_CONNECT_TIMEOUT_MS` | 5000 ms | The GAP connect only, through `NimBLEClient::setConnectTimeout()`. |
| `FURBLE_RICOH_WAKE_PROBE_TIMEOUT_MS` | 2000 ms | The poll loop, checked between reads. One blocking ATT read and the final 50 ms pause are outside it. |

`secureConnection()` inside the reconnect is not covered by any of them. This is
the one claim in the branch that was wrong in a way a reader would act on, and it
is now stated in the four documents that make it.

## Verification

- Full host suite on the rebased tree: `ctest -j 6`, 224/224 pass. Round 1
  recorded 222 and the pull request body still said 220; the tree has 224, which
  is what round 2 measured and what the body now says.
- `ctest -R '^ricoh-'`: 6/6 pass (`ricoh-time`, `ricoh-focus`,
  `ricoh-sleep-gate`, `ricoh-wake`, `ricoh-secure-timeout-uaf`,
  `ricoh-control-flap`).
- `tools/run_sanitized_host_tests.sh -R '^(ricoh-)'`: 6/6 pass under ASan,
  UBSan and LeakSanitizer, `ricoh-wake` included. That is the memory-safety
  check for the seam: the reclaim, the probe after it, and the threaded cancel
  case all run instrumented.
- `python3 -m unittest discover -s tests -p 'test_*.py'`: 636 tests, pass. That
  is what caught findings 10 and 11's contract half.
- `python3 tools/check_static_analysis.py --base origin/master --build-dir
  <host>`: cppcheck pass. clang-tidy is not installed in this environment, so
  the finding 11 fix is verified by removing the offending include rather than
  by re-running the analyzer; CI runs it on every push.
- Red-first and regression probes as recorded above.
- Round 2 re-measured the full suite at the final head: `ctest -j 6`, 224/224
  pass, 82 s, and `ctest -R '^ricoh-'` 6/6 pass, 13 s.
- `ricoh_wake_test` eight consecutive runs after round 2's changes: 8/8 exit 0.
  The new sampler case reported between 0.90 and 1.33 million contended reads
  per run against the assertion's floor of 50.
- `python3 -m unittest tests.test_connect_cancel_contract`: 8 tests, OK, so the
  new wait seam and the seven pre-existing waits still satisfy the body-local
  poll rule.
- `python3 tools/pre_review.py --base 18bbcb5a`: PASS, 13 gates, with
  clang-format 21.1.8 installed. The base is named as the commit rather than as
  `origin/master` because master moved past `18bbcb5a` while this branch sat.
  Without clang-format the formatting gate fails with exit 2, as round 1 noted.
- `sh sim/scripts/check-doc-tokens.sh`.

## Hardware gates that stay open

Unchanged from the plan and not narrowed by this review: whether a reconnect
wakes a GR IV at all, whether the body is connectable after furble terminates the
link, the real settle, connect and probe times, and whether the connect-path
identity write is harmless in standby. Nothing in this record claims otherwise.
