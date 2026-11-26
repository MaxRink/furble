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
below is from this independent pass. The head first reviewed was `292962db`, on
base `4043fba0`; master then moved twice (#493, #530) and this record covers the
tree that merges, that head rebased onto `18bbcb5a` with the review commit on
top. Line numbers are from that tree.

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
  (PLAYBACK refused, 0 ms)
  (OTHER refused, 0 ms)
  (POWER_OFF_TRANSFER refused, 0 ms)
FAIL: a sleeping body is woken by one reconnect
FAIL: wake emits flavor then OperationRequest in order
FAIL: the body reports CAPTURE after the wake
FAIL: the wake polls a fresh OperationMode read instead of reading once
FAIL: wake announces OperationMode CAPTURE on the notification stream
FAIL: an awake body is not woken again
FAIL: a second press captures on the held link
FAIL: the wake still relinks a body that will not wake
FAIL: the wake waits the full probe budget before refusing
FAIL: a repeated press retries the wake on the relinked connection
FAIL: recovery reuses the relinked connection
FAIL: a failed reconnect reclaims the client instead of keeping a stale link
FAIL: a cancelled wake still relinked once
EXIT=1

$ ./ricoh_sleep_gate_test; echo EXIT=$?
FAIL: the refusal path attempts exactly one wake reconnect
EXIT=1
```

The pull request body pastes the first ten `ricoh_wake_test` lines. The run
produces three more, and the record above is the complete list. Both required
cases are red there (the successful wake and the bounded timeout), and the three
non-standby refusal cases pass, which is the phase 1 behaviour that had to
survive. Restoring the branch makes both binaries exit 0.

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
| 2 | Review | **The 5000 ms reconnect budget does not bound the reconnect.** `FURBLE_RICOH_WAKE_CONNECT_TIMEOUT_MS` reaches `NimBLEClient::setConnectTimeout()`, which bounds the GAP connect. The vendor `_connect()` also runs `secureConnection()`, which waits `BLE_NPL_TIME_FOREVER` on the GAP encryption event, and service discovery, the identity write and four subscriptions. So "a settle plus a connect that can reach its 5 s cap plus the 2 s probe" was not the worst case, and the real one can exceed `Control::disconnect()`'s 30 s `DISCONNECT_WAIT_MAX_MS`. | Fixed in the documents. The plan now states what each budget bounds, names `secureConnection()` and its `BLE_NPL_TIME_FOREVER` wait, cites the 29.85 s secure-timeout measurement already recorded in `lib/furble/Camera.cpp`, and says the wake can consume the whole teardown wait. `README.md`, `docs/protocols/camera-remotes.md`, `lib/furble/CLAUDE.md` and `plans/CLAUDE.md` carry the same correction. No code change: the wait is inside the vendored stack, and `abortBlockingConnect()` already reaches it through `m_CancelClient`. | `components/esp-nimble-cpp/src/NimBLEClient.cpp` `secureConnection()` task-wait loop; `lib/furble/Camera.cpp:1381`; `include/FurbleControl.h:117` |
| 3 | Review | **A cancelled press still dropped and rebuilt a working link.** `connectCancelled()` was polled only inside `PROBE`, so a shutter command that had already been dequeued when `Control::disconnect()` armed the token ran the whole `RELINK`: it terminated a link the teardown then had to take down again and stretched the drain. The branch's own test asserted that behaviour (`a cancelled wake still relinked once`). | Fixed in the wake. `wakeFromStandby()` honours the token before it touches the link and transitions straight to `FAILED` with a `cancelled by the user` line, so a cancelled press writes nothing, relinks nothing and leaves the camera connected. The test now asserts zero reconnects, zero writes of any kind, `isConnected()` still true, and a return well inside the budget. | `lib/furble/Ricoh.cpp` `wakeFromStandby()`; `tests/host/ricoh_wake_test.cpp`; regression probe: with the check removed, `a cancelled press does not drop a working link to wake the body` and `a cancelled press writes nothing at all` both fail |
| 4 | Review | **The probe-time cancel lost its coverage.** The original test reached `PROBE` with the token already set, which is why it could see the probe's `connectCancelled()` branch. With finding 3 fixed, a pre-press token never reaches the probe, so the branch would have been untested. The host build compiles `Ricoh.cpp`'s wait on the wall clock (`__has_include(<freertos/FreeRTOS.h>)` is false for that target), so `furbleHostSetDelayHook()` cannot reach it. | Fixed in the test. A helper thread waits for the peer to accept the reconnect the wake performed and then calls `cancelConnect()`, which is the real `PROBE` state. The case asserts the token was delivered on a live link, one relink happened, no capture was written, and the press returned before the probe deadline. `tests/host/CLAUDE.md` records why the delay hook cannot be used. | `tests/host/ricoh_wake_test.cpp` `cancelDuringProbe()`; eight consecutive passes |
| 5 | Review | **A vendor `_disconnect()` could be skipped on the reclaim path.** `reconnectInPlace()` cleared `m_Connected` before calling `_disconnect()`. `Lumix::_disconnect()` and `Sony::_disconnect()` return early when `!m_Connected`, so on this seam they would leave their session state open and their GPS stop or release writes unwritten while the client was freed. Latent today because only Ricoh calls the seam, but it is a base-class contract, and `CLAUDE.md` invites vendors to use it. | Fixed in `Camera::reconnectInPlace()`. It runs `_disconnect()` while `m_Connected` still describes the link, exactly as `Camera::disconnect()` does, and clears the flag immediately after, before `setClientCallbacks(nullptr)` and `deleteClient()`. Nothing can race the free: `m_Mutex` is held for the whole sequence and `isConnected()` never dereferences the client. | `lib/furble/Camera.cpp` `reconnectInPlace()` failure branch; `lib/furble/Lumix.cpp:237`, `lib/furble/Sony.cpp:158`, compare `Camera::disconnect()` at `:1445` |
| 6 | Review | **A failed wake did not present as disconnected.** The reclaim path left `m_Progress` at whatever the aborted handshake had reached, while `Camera::disconnect()` zeroes it. | Fixed alongside finding 5: the failure branch clears `m_Progress` with `m_Connected`, so a failed wake looks like a failed connect to the status readers. The success path needs no change: a completed `_connect()` already ends at 100, so preserving the previous value is a no-op and is deliberately not implemented. | `lib/furble/Camera.cpp` `reconnectInPlace()` |
| 7 | Review | **`about 7 s of blocked target task` was stated three times** as the cost of a failed wake, and the UI-surfacing section used it to size the missing refusal surface. | Fixed. The plan now states that the cost is the settle, the reconnect and the probe, that the reconnect is the part that is not bounded by the connect timeout, and points at the budget section rather than repeating a single number. | `plans/149-ricoh-sleep-shutter-gate.md` "UI surfacing remaining" and hardware gate 8 |
| 8 | Review | **The plan's invariant contradicted its own phase 2.** The invariant section still read "BLE_STARTUP and POWER_OFF_TRANSFER must block all side-effect writes" while phase 2 relinks a BLE_STARTUP body and writes the identity during the handshake. | Fixed. The invariant is now "must block every capture write", it names the two capture writes, and a following paragraph states which writes the reconnect repeats and that the phase 1 test was narrowed to match. | `plans/149-ricoh-sleep-shutter-gate.md` "Invariant" |
| 9 | Review | **"no progress-0 blip is published to the UI" was false.** `Ricoh::_connect()` rewrites `m_Progress` from 0 to 100, and `FurbleCompanionService.cpp:399` and `FurbleMQTT.cpp:1769` publish it for a connected camera unconditionally. | Fixed in the wording only, and the claimed code fix was tried and rejected. Preserving the old progress is a no-op because a completed `_connect()` ends at exactly 100, and the assertion that a test could make cannot fail, so it was not kept. During the relink the link really is down, and the plan now says so: a status reader sees the camera disconnected and its progress reset there, while Control stays active and publishes no connect state. | `plans/149-ricoh-sleep-shutter-gate.md` state-machine notes; `src/FurbleCompanionService.cpp:399`; `src/FurbleMQTT.cpp:1769` |

| 10 | CI | **Both new waits broke the plan 170 cancel contract.** `tests/test_connect_cancel_contract.py` fails a blocking function whose own braced body does not poll `connectCancelled()`, and the branch added two: `linkSettleDelay()` in `Camera.cpp` and `wakePause()` in `Ricoh.cpp`. Both sit on the reconnect path, which runs under `Camera::m_Mutex`, so `Control::disconnect()` could not settle the target until the whole settle or probe pause elapsed. The gate is body-local by design: a poll in the caller does not immunise the wait. | Fixed by making the waits poll in their own body and slicing. They were also merged into one seam, `Camera::pollDelay(milliseconds, sliceMs)`, which returns false when the token is set during the interval. The settle takes 20 ms slices, the probe pause 10 ms. `reconnectInPlace()` now skips the reconnect when the settle was cancelled and takes its ordinary failure path, and the probe fails with `cancelled during the probe`. | `lib/furble/Camera.{h,cpp}` `pollDelay()`; `lib/furble/Ricoh.{h,cpp}` `wakePause()`; `python3 -m unittest tests.test_connect_cancel_contract` 8/8 |
| 11 | CI | **`Ricoh.cpp` including `<freertos/FreeRTOS.h>` broke the static-analysis gate.** `cppcheck and clang-tidy` failed with "could not compile: clang-tidy exited 1 with build-static", reporting a typedef redefinition at `sim/shim/freertos/FreeRTOS.h` line 13 (`FurbleSimQueue *` against `FurbleHostQueue *`), on the `Ricoh.cpp` translation unit. The branch copied the `__has_include(<freertos/FreeRTOS.h>)` pattern into a vendor class, where the host compile database resolves it to the simulator shim while the same target also force-includes the host test shim, and the two `QueueHandle_t` typedefs collide. | Fixed by removing the include from `Ricoh.cpp` entirely: it calls the new `Camera::pollDelay()` instead, so exactly one translation unit owns the scheduler-clock rule, as `Camera.cpp` already did. `lib/furble/CLAUDE.md` records that a vendor class must not include the FreeRTOS headers for a wait. | `lib/furble/Ricoh.cpp` (no `freertos` include now); `lib/furble/Camera.cpp` `pollDelay()` |

## Findings judged false or already correct

| # | Candidate | Verdict | Evidence |
| ---: | :--- | :--- | :--- |
| 1 | `reconnectInPlace()` forgets `setConnectionParams()`, so the new link runs at NimBLE defaults and the conn-saver and dead-link detection break after a wake. | **False.** The parameters live on the `NimBLEClient` object (`m_connParams`) and are consumed at `ble_gap_connect()`, and the reconnect reuses the same client, so the new link is established with the values the last `connect()` or `setConnProfile()` set. | `components/esp-nimble-cpp/src/NimBLEClient.cpp:542-556` writes `m_connParams`; `:185-195` passes it to `ble_gap_connect` |
| 2 | A target reap can free the client under the probe, because `PROBE` reads `m_OperationMode` with no `Camera::m_Mutex` held. | **False.** `Control::reapZombieTargets()` frees a target only when `m_Stopped` is true, and the wake runs on that target's own task, which is inside `shutterPress()` and cannot have set it. `Control::teardownDraining()` also blocks a fresh connect while a drained target exists, so no new `connect()` can reset the camera underneath it. The one path that could reclaim the client, `reclaimClient()`, is reached only from that same stopped-target branch. | `src/FurbleControl.cpp:1216-1258`; `src/FurbleControl.cpp:1182`, `:1035` |
| 3 | A disconnect can free the client while `reconnectInPlace()` is between publishing and clearing `m_CancelClient`. | **False.** Both sides serialize on `m_CancelMutex`: `abortBlockingConnect()` holds it across its `disconnect()` call, and `reconnectInPlace()` takes it to clear the pointer before any free. The clear happens before the reclaim in every path. | `lib/furble/Camera.cpp:1381-1399`, `:1489-1497` |
| 4 | The wake can be entered with a stale `m_Paired` or a pending pairing request, and should call `clearPairingRequest()` like `connect()`. | **Not a defect.** A wake is not a connect: it keeps the existing bond and session, and the plan and `lib/furble/CLAUDE.md` record that `m_Paired`, `m_Active` and target ownership are deliberately untouched. A pending pairing request at wake time would mean a pairing dialog is open, which cannot be reached from the shutter path. | `lib/furble/Camera.h` `reconnectInPlace()` contract; `lib/furble/CLAUDE.md` |
| 5 | A wake blocks the UI or control task out of its watchdogs. | **Not a defect, but recorded.** `Camera::m_Mutex` is held for the whole wake, and `Control`'s RSSI sampler calls `Camera::getRssi()`, which blocks on it. `Control::disconnect()`'s own wait is 30 s and feeds the watchdog per slice, and `Control::updateConnStats()` uses `try_lock` and cannot stall. The exposure is a control-task stall for the wake duration, which is bounded by findings 2 and 3 once a cancel is honoured. | `src/FurbleControl.cpp:1554-1558`; `lib/furble/Camera.cpp:1620`, `:963` |
| 6 | A failed wake leaves a dead session the user has to fix by hand. | **False.** `Control` re-checks `allConnectedLocked()` every 250 ms in `STATE_ACTIVE` and drops to `STATE_CONNECT` when a target reports disconnected, which runs the ordinary reconnect cycle with its existing backoff. `teardownDraining()` is clear because the target never stopped. | `src/FurbleControl.cpp:544-553` |

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

- Full host suite on the rebased tree: `ctest -j 6`, 222/222 pass.
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
- `python3 tools/pre_review.py --base origin/master`.
- `sh sim/scripts/check-doc-tokens.sh`.

## Hardware gates that stay open

Unchanged from the plan and not narrowed by this review: whether a reconnect
wakes a GR IV at all, whether the body is connectable after furble terminates the
link, the real settle, connect and probe times, and whether the connect-path
identity write is harmless in standby. Nothing in this record claims otherwise.
