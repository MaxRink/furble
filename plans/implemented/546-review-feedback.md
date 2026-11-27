# 546 - Review feedback

An independent, adversarial review of the release-durability fix: the
`isReleaseCommand` predicate, the eviction rule in
`Control::Target::sendCommand`, the new `Target::m_DisconnectPending`, the host
test `tests/host/control_release_durability_test.cpp`, plan 374 and the four
documentation surfaces it changes.

Reviewed at head `da5fad13`, base `18bbcb5a`. Fixed in this pass on the same branch; every row below is
closed and carries the evidence that closes it.
Master moved to `789dd78c` while this branch was open, so the fix now merges it
and carries the citation rotation that merge needed. The review re-derived the defect
from the base sources, attacked the eviction rule from the queue's own contract
(what the oldest item may be, and who may be waiting on it), traced
`m_InFlight` through every branch including the receive-then-send pair failing
twice, timed the disputed `control-tsan-wrapper-contract` claim on both trees,
and built a probe that drives the unmodified branch into the state the rule
must not produce. The rule is the right shape and the accounting is exact; one
gap in what the rule may discard blocks the merge, because it can lose the
command the change exists to protect.

| # | Severity | Finding | Resolution | Evidence |
| ---: | :--- | :--- | :--- | :--- |
| 1 | Blocking | A release can evict a queued release. `src/FurbleControl.cpp:172-181` receives the head of the queue and treats it as disposable without inspecting it, while `src/FurbleControl.cpp:162-165`, `src/CLAUDE.md:25-30`, `include/CLAUDE.md:30-31` and `docs/console-commands.md:458-461` all assume the head is a request. A queued `CMD_SHUTTER_RELEASE` or `CMD_FOCUS_RELEASE` is not disposable: it can be the only release for a held button. Reachable from `WebUI::releaseAll()` (`src/FurbleWebUI.cpp:1539-1557`), which queues a shutter release and then a focus release back to back, and from any second producer during the stall that queues seven commands behind a release waiting at the head. | Fixed. `Target::sendCommand()` now frees one slot on a full queue and inspects what occupied it (`src/FurbleControl.cpp:157-211`). A request is displaced, its reservation retired and the release appended at the tail (`:199-210`). A release is never displaced: it goes back to the front with `xQueueSendToFront` into the slot the receive just freed, which keeps its reservation and its place ahead of the requests behind it, and the incoming release is the one refused (`:170-198`). The stronger rule was taken over the same-class interchange suggested here, because a queued release that is never displaced is one sentence and one that is displaced unless an identical release covers it is two, and because evicting a release is the class of loss this plan exists to remove. The cost is the same-class corner: `[R, P...]` plus a second `R` refuses the new release where a swap would have kept a release at the tail, so that drain still ends on a press. That is what the base code did in the same corner, the queue is stalled by definition when it is full, and MQTT and the WebUI already retry a refused release. Two more branches: the receive can now find the queue empty, because the target task drained it between the refused send and the receive, and then the release takes the free slot instead of being refused (`:165-169`); and the two attempts are now one, since the `m_Mutex` invariant in finding 5 makes a second one unreachable. The regression is test step 7 and mutations 2 and 3 below. | Reproduction: park the target task after one delivered press with the `target_command_complete` barrier, queue `CMD_SHUTTER_RELEASE`, fill the other seven slots with presses, then send `CMD_FOCUS_RELEASE`. The focus release returns `pdTRUE` and the shutter release is discarded: `FAIL: the queued shutter release still reaches the camera: expected 16 accepted shutter writes after the press, saw 15 (the focus release evicted it)`. 16 = seven presses x2 + shutter release x2, 15 = seven presses x2 + focus release x1. The new test cannot see it because `lib/furble/Fujifilm.cpp:204-210` makes Fujifilm's focus release write the same `SHUTTER_RELEASE` frame; the drivers where the two differ are Lumix (`lib/furble/Lumix.cpp:337`, ignored), Sony (`lib/furble/Sony.cpp:192`, `FOCUS_UP`), Canon EOS Smart (`lib/furble/CanonEOSSmart.cpp:212`, `AF_OFF`), Nikon Remote (`lib/furble/NikonRemote.cpp:100`, empty), Ricoh (`lib/furble/Ricoh.cpp:465`) and DJI (`lib/furble/DJIOsmo.cpp:469`). On those the drain ends with the shutter held, which is the failure this PR removes. |
| 2 | Blocking (docs) | `docs/console-commands.md:458` and its wiki mirror `docs/wiki/Console-Commands.md:439` state "A full per-camera command queue never drops a release." Two paths still drop one: finding 1, and the eviction bound at `src/FurbleControl.cpp:172`, where two failed attempts fall through to the old `Failed to send command to target.` path at `:184-185`. The second is unreachable while every producer holds `Control::m_Mutex` (finding 5) but nothing enforces or states that. `include/CLAUDE.md:30-31` inherits the same premise with "an evicted request". | Fixed. All four surfaces now state the rule the code implements. `docs/console-commands.md:458-467` and `docs/wiki/Console-Commands.md:439-448`: a full queue makes room for a release instead of dropping it, it displaces the oldest waiting command and only if that command is a press or another request, a release already in the queue is never displaced, and the one command a full queue can still refuse is a release that arrives with another release at the head, after which `shutter` and `focus` report it. `src/CLAUDE.md:25-35` and `include/CLAUDE.md:25-43` say the same, name the six vendors on which the two release kinds are not interchangeable, drop the unreachable two-attempt bound, and add that every producer posts under `m_Mutex`. `plans/implemented/374-control-release-durability.md:172-180` carries a note that its quoted rule and its quoted code are the reviewed head, not the current file. | `sed -n '455,465p' docs/console-commands.md`, `sed -n '436,446p' docs/wiki/Console-Commands.md`, `sed -n '24,34p' include/CLAUDE.md`. |
| 3 | Non-blocking | Test step 7 names a mechanism it does not exercise. `tests/host/control_release_durability_test.cpp:194-196` says the release "would be accepted by evicting the disconnect", but at that point the queue holds exactly the one item the drain left at the front (`src/FurbleControl.cpp:132-141`), so the release would be appended behind it. `tests/host/control_release_durability_test.cpp:198-206` is also satisfied by a full queue rather than by the teardown rule, and the loop can only observe refusal. | Fixed. Step 7 is now the release-evicts-release regression (`tests/host/control_release_durability_test.cpp:202-229`): a delivered press, a queued shutter release, seven presses, then a focus release that must be refused, with the peer's accepted shutter write count, 16, as the check that the release survived. Step 8 is rewritten (`:231-256`): the comment no longer claims the release would evict the teardown, and both checks now run at a moment the test can prove. A new sync point, `disconnect_teardown_queued` (`src/FurbleControl.cpp:1130`, registered in `lib/furble/FurbleTestSync.h:61-69`), fires once the teardown fan-out has run and `m_Mutex` is released, so the queue holds exactly the one `CMD_DISCONNECT` and seven slots are free. Each check then fails for its own reason: mutation 4 forces the guard at `:144` false and both `a press is refused while the queue has free slots` and `a release is refused while the queue has free slots` fail, with no other check failing. The press check no longer loops, so a full queue cannot satisfy it. | Mutation 2 (guard at `src/FurbleControl.cpp:144` forced false, release rule intact): `a press is refused once the teardown is queued` still **passes** (the loop fills eight slots and the ninth is refused), and only `a release cannot evict a queued teardown` fails, after which the process dies in `~Control()` -> `~Target()` -> `Camera::disconnect()` on the destroyed mock peer. The plan quotes one failure and claims an outcome failure as well. |
| 4 | Non-blocking | Two inline citations moved to the wrong line. `include/FurbleControl.h:74` and `plans/327-control-gps-race.md:146` cite `FurbleControl.cpp:236`, which is `m_Camera->noteConnActivity(true)` in the `CMD_FOCUS_PRESS` case. The unlocked GPS read is `src/FurbleControl.cpp:249`, case at 247. | Fixed. The unlocked read is at `src/FurbleControl.cpp:278` (`m_Camera->updateGeoData(m_GPS, m_Timesync)`, case at 276 and its log at 277) on this head, which was 249 and 247 at the reviewed head. `include/FurbleControl.h:83` and `plans/327-control-gps-race.md:146` cite 278 now. The pass also moved every citation this branch itself had made current, because the insertion shifted them: `docs/power/ble.md:1136` (247-249 to 276-278), `docs/power/soc.md:544-545` (1571 and 1161 to 1607 and 1197), `plans/33-wifi-hub.md:1267` and `:1392` (637 and 637-662 to 666 and 666-691), `plans/implemented/34-ota-partitions.md:436` (656 to 690), `plans/50-companion-app-design.md:339` (`include/FurbleControl.h` 154 to 174), `plans/97-followups.md:82` (1267 to 1303), `plans/implemented/09-reconnect-backoff.md:86` (1016-1231 to 1045-1267) and `plans/implemented/76-reconnect-stuck.md:123` (503-506 to 532-535), plus `plans/374` itself (1383, 247-249, 777-807 and its `include/FurbleControl.h:79`, which was already off by ten). Citations this branch never made current, mostly in plans/96, plans/97-review-feedback and the implemented series, were left alone: they were stale before this PR, and a blind shift would have made some accidentally right and others more wrong.

Second rotation, after master moved. `plans/371-fork-network-sweep.md`, `plans/implemented/373-interval-shutter-hold.md` and `plans/539-review-feedback.md` landed on master after this branch was written and cite the pre-fix file, so the merge of `origin/master` at `789dd78c` broke them: the gate re-derives the defining line and a range that no longer contains the definition fails, which is how CI found `plans/371:76` and `plans/539:98`. This pass rotates all of them, not only the two the gate names: `plans/371:73-76` now cites `:156` and `:212-215` for the send and its failure log, `include/FurbleControl.h:98` for the queue of eight and `:666-691` for `Control::sendCommand`; `plans/373:36` and `:41` cite `:240-303` and `:680-688`; `plans/539:98`, `:104`, `:107` and its summary row cite `:666-691`, `include/FurbleControl.h:98`, `:730` and the same set. Two labels stay as they are on purpose: the quoted snippet fence in `plans/539:90` now reads `at the reviewed head`, and the ThreadSanitizer and ASan report quotes in `plans/327:89` and `:124` keep their numbers, because both are quotations of what a review or a run printed rather than pointers into today's file; `plans/327:10-12` already says the numbers in that section are the parent commit's. | `sed -n '236p;247,249p' src/FurbleControl.cpp`; base `198` was the `updateGeoData` line (`git show 18bbcb5a:src/FurbleControl.cpp | sed -n '196,199p'`). Neither is caught by `tools/check_doc_citations.py`: a bare `FurbleControl.cpp` does not resolve, and the header comment is not Markdown. |
| 5 | Non-blocking | The new contract omits the lock it rests on. Every caller of `Target::sendCommand` holds `Control::m_Mutex` (`src/FurbleControl.cpp:620`, `:688`, `:889`, `:1076`, `:1247`), and that is what makes the flag window atomic and the second eviction attempt dead code. `include/FurbleControl.h:56-64` does not say so, although `updateGPS` states the same requirement for itself at `include/FurbleControl.h:70`. | Fixed, and recorded rather than relied on. `include/FurbleControl.h:56-74` now opens the `sendCommand` contract with the requirement that the caller holds `Control::m_Mutex`, says that the drain and the `m_DisconnectPending` check sit in the same critical section as every other producer's check and send, and that a caller without the lock voids both guarantees. The five call sites are `src/FurbleControl.cpp:649`, `:717`, `:918`, `:1105` and `:1283`, all under a `lock_guard`. The release branch says at `:182-186` that no producer can take the freed slot, which is also why the second attempt was removed rather than kept as defence in depth. `src/CLAUDE.md` and `include/CLAUDE.md` repeat the requirement. | `grep -n 'lock_guard' src/FurbleControl.cpp` around each call site; `include/FurbleControl.h:56-76`. |
| 6 | Non-blocking | `m_DisconnectPending` is only ever stored `true` (`src/FurbleControl.cpp:120`) and never cleared on a failed teardown or a reconnect. Safe today only because both teardown call sites drop the Target from `m_Targets` immediately (`src/FurbleControl.cpp:1166-1171`, `:1247-1248`) and `addActive()` builds a fresh Target next session. Nothing states that invariant, and `sendTargetCommand(id, CMD_DISCONNECT)` is the shape that would break it: a live, connected target that refuses every command forever. | Fixed, documented and not changed. `include/FurbleControl.h:102-117` now records next to the field that it is only ever stored true, that this is terminal for the `Target` it is set on, that both call sites that post a teardown drop the target from `m_Targets` in the same critical section (`src/FurbleControl.cpp:1201-1206` moves every target to the drain set before `m_Targets.clear()`, and `:1283-1284` moves the cancelled target into the drain set and erases it), that `m_Targets` is the only route `sendCommand` has, and that `addActive()` builds a fresh target next session. It closes with the shape that would break the invariant: a `Target` that outlived its teardown while still in `m_Targets` would refuse every command forever while `allConnectedLocked()` still reports the camera up. | `grep -rn m_DisconnectPending src include` -> one store, one load, one declaration. `tests/host/control_e2e/control_e2e.cpp:245` already drives `sendTargetCommand` with a camera id. |

Line numbers inside the Finding and Evidence columns above, and in the verified
table, are the reviewed head `da5fad13`: they are the record of what was read,
not pointers into the current file. The Resolution column and the section below
give the numbers on this branch.

Verified, no change needed:

| Check | Result | Evidence |
| :--- | :--- | :--- |
| The defect is real on the base | Holds. A full 8-slot target queue drops a release and only logs it. | Base `sendCommand` at `18bbcb5a:src/FurbleControl.cpp:130-135` retires the reservation and logs; mutation 1 on the PR head reproduces the consequence (`FAIL: the camera ends with the shutter released`). |
| Eviction cannot reorder a press and its release | Holds. The evicted item is the oldest and the release is appended at the tail, so a surviving press is still followed by its own release. Only the evicted press is lost, which the release behind it answers. | `src/FurbleControl.cpp:174-181`; probe: seven queued presses plus a shutter release drain in order and end released. |
| `m_InFlight` stays exact through the receive-then-send pair | Holds in every branch. The reservation is taken before the send (`:155`), the evicted item's is retired by the receive (`:181`), ours is retired only if the final send failed (`:184-185`). Receive-succeeds/send-fails-twice leaves the count equal to queued plus executing. | Traced all four outcomes; probe soaked 20 forced evictions (park after a press, fill eight slots, send the release, drain) and `waitForCameraCommands()` converged every round, `idle()` neither hung nor returned early. |
| No livelock when producers race | Holds. The loop is bounded at two attempts and each is non-blocking; no unbounded retry exists. | `src/FurbleControl.cpp:172`; 20 forced evictions with a concurrent drain completed. |
| No deadlock with the control task | Holds. Both queue calls are zero-timeout, and the target task never takes `Control::m_Mutex`, so the queue lock inside the eviction is never held across a wait on the control task. | `src/FurbleControl.cpp:208-250` (target task) takes no `m_Mutex`; `tests/host/control_e2e/freertos_shim.cpp:202-230` unlocks the queue mutex while it waits. |
| The new test fails without the fix | Holds, and the quoted output matches. | Mutation 1 (release rule disabled): `FAIL: shutter release is accepted by a full queue`, `FAIL: focus release is accepted by a full queue`, `FAIL: the camera ends with the shutter released`, `control-release-durability: FAIL (3 checks)`. `a request is still bounded by the full queue` keeps passing. |
| The new test is deterministic | Holds. The barrier is one-shot and disarms on release, the park is proven by the peer's `kShutterFullPress`, every wait is bounded and `anyTimedOut()` is asserted. | `tests/host/testsync/TestSyncController.h:26-33`; 11 consecutive runs pass, 0.64 s to 1.09 s; ASan/UBSan build of the same test exits 0. |
| The guard is needed, not padding | Holds. Without it a producer that arrives after the drain appends work behind the teardown, which the exiting task never runs; with eight of them queued a ninth release could take the teardown's slot. | `src/FurbleControl.cpp:115-150`; the drain leaves one item and only the flag keeps it that way for the next producer. |
| `m_DisconnectPending` cannot wedge a session today | Holds for the current call graph. | Both teardown call sites remove the Target from `m_Targets` in the same critical section (`:1085-1092`, `:1247-1248`), and `addActive()` (`:1320-1390`) always constructs a new Target. |
| `control-tsan-wrapper-contract` is a pre-existing timeout | Holds, and the reproduction is load-dependent rather than a fixed cost. The script is untouched by this PR. | `git diff 18bbcb5a HEAD -- tests/host/run_tsan_race_contract.sh` is empty. Under a full host build: rc 0 in 71.5 s, 84.1 s, 50.7 s on the PR tree and 34.9 s on base `18bbcb5a`. Idle host: 1.83 s, 1.66 s on the PR tree and 1.47 s on base, and the CTest entry passes in 1.32 s. Timeout `tests/host/CMakeLists.txt:2792` unchanged at 10 s. |
| The wider ctest selection | Holds, and better than claimed once every binary is built. | `ctest -R 'control-|console-commands|companion-gatt|mqtt-owner|ricoh-control-flap'` in `/tmp/furble-546-build` after a full `cmake --build . -j8`: 46 tests, 46 passed, 380.58 s. The ten the PR reports as `Not Run` all ran and passed, as did `control-tsan-wrapper-contract` (1.32 s). The selection enumerates 46 tests here, not the 47 the PR tallies. |
| The pre-review gates | Hold. | `python3 tools/pre_review.py --base origin/master`: 13 gates pass. `CI workflow contract` needs the system `python3` (PyYAML), and passes there. `check_format` against clang-format 21.1.8 passes on 3 changed files. |

The diagnosis, the rejected alternatives, the in-flight argument and the
teardown guard are all correct and better argued than the average plan in this
tree. Finding 1 is the one place the rule's own justification ("every command
still in the queue is worth less than the release") is not true, and it is the
same class of loss the PR removes, so it is worth the ten lines before merge.

## After the fixes

The rule now reads four ways. A request is bounded by the 8-slot queue and
refused when it is full. A release that finds a request at the head displaces it,
retires its reservation, and is appended at the tail. A release that finds a
release at the head puts it back at the front with `xQueueSendToFront` and is
refused itself. A queued `CMD_DISCONNECT` refuses every later command, unchanged.
`m_InFlight` carries exactly one reservation per queued or executing command in
every branch, including the new empty-queue retry.

The probe from finding 1, unchanged except for the expectation the rule now
gives, on the reviewed shape and on this branch:

```
$ ./control_release_eviction_probe_test          # reviewed shape, mutation 2
probe: 15 of 16 accepted shutter writes after the press
  FAIL: the focus release is refused rather than evicting the queued shutter release
  FAIL: the queued shutter release still reaches the camera: expected 16 accepted
        shutter writes after the press, saw 15 (the focus release evicted it)
control-release-eviction-probe: FAIL (2 checks)

$ ./control_release_eviction_probe_test          # this branch
probe: 16 of 16 accepted shutter writes after the press
control-release-eviction-probe: PASS
```

Mutation proofs, each mutation failing only its own step:

| Mutation | Result |
| :--- | :--- |
| 1. no release rule at all, the base `sendCommand` | `FAIL: shutter release is accepted by a full queue`, `FAIL: focus release is accepted by a full queue`, `FAIL: the camera ends with the shutter released`, `FAIL (3 checks)`. Steps 7 and 8 keep passing, so neither can stand in for steps 3 to 5. |
| 2. the reviewed shape: receive the head, re-send it unconditionally, twice | `FAIL: a focus release is refused rather than evicting the queued shutter release`, `FAIL: the queued shutter release still reaches the camera: expected 16 accepted shutter writes after the press, saw 15`, `FAIL: the queued shutter release kept its place ahead of the presses behind it`, `FAIL (3 checks)`. Nothing outside step 7 fails. |
| 3. same as 2 with the second attempt kept | identical output, the retry never runs. |
| 4. `m_DisconnectPending` guard forced false at `src/FurbleControl.cpp:144` | `FAIL: a press is refused while the queue has free slots`, `FAIL: a release is refused while the queue has free slots`, `FAIL (2 checks)`. Both step 8 checks fail on their own, which the old loop-based press check did not do. |
| 5. the push-back uses `xQueueSend` instead of `xQueueSendToFront` | `FAIL: the queued shutter release kept its place ahead of the presses behind it`, `FAIL (1 check)`. This one needed the position check added with it: the write count alone cannot see the release move to the tail. |

Runs on this branch, all with every host binary built:

- `control_release_durability_test` 10 times: PASS each, 0.34 s to 0.58 s.
- `ctest -R 'control-|console-commands|companion-gatt|mqtt-owner|ricoh-control-flap'`:
  46 tests, 46 passed, 386.01 s with every host binary built, and
  `control-release-durability` 0.49 s inside it. An earlier run of the same
  selection while the tree was still building timed out
  `control-tsan-wrapper-contract` against its unchanged 10 s CTest limit; the
  script passes standalone in 1.55 s, the entry passed in 1.53 s on its own and
  in 2.04 s in the run above, which is the load sensitivity recorded in the
  verified table and not a change on this branch.
- `python3 tools/pre_review.py --base origin/master`: `PASS: 13 gates`, including
  `PASS: documentation citations`.
- `python3 tools/check_doc_citations.py --root .`: 0 errors over 3313 citations in
  185 files. It reports symbol mismatches for a citation whose line does not
  start the symbol, which is how the shifted citations in both passes were found
  and corrected. A citation whose sentence names no symbol is not covered by the
  gate, so the same numbers were re-derived by content as well, which is how the
  off-by-one on the GPS read (277 against 278) and the two that the gate accepts
  because the range still falls inside the function (`plans/implemented/34-ota-partitions.md:436`
  and `plans/374:126`) were caught.

Known trade-offs, recorded rather than hidden:

- The same-class corner: `[R, P...]` plus a second `R` refuses the new release
  where the review's suggested same-class interchange would have swapped them
  and kept a release at the tail. The strict rule is the one implemented, for the
  reason in finding 1, and the corner is what the base code did too.
- The refusal is visible to callers: `Control::sendCommand` returns `pdFALSE` for
  the target that refused, so the console reports it and MQTT and the WebUI retry.
  Nothing in the firmware treats the refusal as success.
- No hardware run. This pass, like plan 374, claims source, host-test and
  mutation evidence only.
