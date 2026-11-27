# 97 - Review feedback

An independent, adversarial review of the first tranche of
[97-followups.md](97-followups.md) Group 1 batches 1 to 3, and of the two fixes
that carry it (PR #530).

The review re-read every claimed item against master at `4043fba0` rather than
against the plan text, reproduced both red-first claims by reverting each fix in
its own build and watching the regression fail, ran the full host suite and the
linked boundary cases, and attacked the two changed code paths where a BLE
lifecycle defect would show up: a leaked client, a double-freed target, a
dropped bond, and a lock-order inversion against `Control::m_Mutex` and
`Camera::m_Mutex`.

| # | Severity | Finding | Resolution | Evidence |
| ---: | :--- | :--- | :--- | :--- |
| 1 | Blocking | The branch was cut from `253d9cbe`, one commit behind master. The pull request recorded the resulting coupling-ratchet failure as a master defect, pushed with `--no-verify`, and split the "fix" into a second pull request. It is not a master defect: `check_coupling_ratchet.py --base origin/master` reports `coupling ratchet: clean` on a pristine `4043fba0` worktree, and the growth is the branch carrying the pre-#488 `src/FurbleFeedback.cpp`, whose `M5.` sites PR #488 reduced from 15 to 14. The stale base alone produced `GREW m5_calls: src/FurbleFeedback.cpp grew from 14 to 15`, so the gate was reporting a revert, not growth. | Fixed by rebasing onto `4043fba0`. The same command now prints `coupling ratchet: clean`, and `pre_review.py --base origin/master` passes 13 of 13 gates with no `--no-verify`. The claim in the pull request that the ratchet fails on pristine master, and the conclusion that the fix belongs in its own pull request, are both withdrawn. | `git merge-base pr530-review origin/master` was `253d9cbe`; pristine-master run at `4043fba0` printed `m5_calls 132 in 11 files` and `coupling ratchet: clean`; the branch printed `GREW m5_calls ... 14 to 15`; after the rebase, `PREREVIEW_EXIT=0` and `PASS: 13 gates`. |
| 2 | Non-blocking | No `plans/NN-review-feedback.md` record existed, and the repository requires one for every merge. Green CI is not a review, and the author's own new tests are not a review of the author's own new tests. | Fixed. This document is that record. | This file. |

Verified, no change needed:

| Claim | Result | Evidence |
| :--- | :--- | :--- |
| B1, lock-free `isConnected` and the `Camera::m_Mutex` split | Holds. `isConnected()` returns the cached `std::atomic<bool>` with no mutex and no `m_Client` dereference, and the comment records why a live-client cross-check would be a use-after-free. The companion status path reaches it through Control's own mutex, so the cached-snapshot half is unnecessary. | `lib/furble/Camera.cpp:1533`; `std::atomic<bool> m_Connected` in `lib/furble/Camera.h`; `ce710521`. |
| B2, non-droppable `CMD_DISCONNECT` plus bounded disconnect | Holds. `Target::sendCommand()` drains the queue with non-blocking receives, retires one in-flight reservation per removed command, and posts the disconnect at the front; `Control::disconnect()` honours the caller's `timeout_ms` instead of always waiting the maximum. | `src/FurbleControl.cpp:102`, `src/FurbleControl.cpp:965`; `332f711a`, `633fd8f4`. |
| B3, an exit edge out of `STATE_DISCONNECTING` | Holds. The timeout path force-completes and moves any target whose task has not stopped into the drain set rather than freeing it, and every exit publishes `STATE_IDLE`. | `src/FurbleControl.cpp:965`; `control_teardown_wedge_test`. |
| B4, targets destroyed outside the Control mutex | Holds. `reapZombieTargets()` frees only a target whose task has published `m_Stopped`, so the destructor skips its radio call while the lock is held. | `src/FurbleControl.cpp:1216`; `633fd8f4`. |
| A3a, cancel and disconnect checks in `waitForRegistration` | Holds. The wait polls the link, the cancel token and the active flag on every slice. | `lib/furble/Fujifilm.cpp:120`; `bca68c2a`. |
| A1, `FujifilmBasic` null-deref hardening | Holds. A null shutter service and a null shutter characteristic each return false. | `lib/furble/FujifilmBasic.cpp:170`, `lib/furble/FujifilmBasic.cpp:179`; `a79bdae0`. |
| A3d, `m_Configured` as `std::atomic<bool>` | Holds. | `lib/furble/Fujifilm.h:112`; `bca68c2a`. |
| A2, Ricoh stale-bond delete-and-retry | Holds. Only a fresh live-scan pairing deletes the matching bond; a saved reconnect keeps it. | `lib/furble/Ricoh.cpp:202`; `01e97e3d`. |
| B5, the three unlocked `m_Targets` walks | Holds. `updateGPS`, the ACTIVE fan-out and `allConnected` all take the Control mutex. | `bbe4d22c`; `control_gps_race`. |
| B7, atomics for `m_State` and `m_ConnectCamera` | Holds. `m_State` is atomic, and `m_ConnectCamera` is published under the mutex through a generation-stamped setter and cleared on both connect outcomes. | `cbb61ce9`, `5e1ac1dd`. |
| B10, single cancel-callback registration | Holds. The handler is registered once when the connect message box is built. | `src/FurbleUI.cpp:9558`; `b90d5666`. |
| A8, two of three small library fixes | Holds. `gattRead` treats an empty read as a failure, and `Scan` dispatches through a state mutex with an in-flight count that `stop()` waits for. The scan-end success drain is still open and is recorded as open rather than re-fixed. | `13653c24`; `lib/furble/Scan.cpp:121`. |

Judged false:

| Hypothesis | Why it does not hold | Evidence |
| :--- | :--- | :--- |
| Adopting the advertised address adds a cross-task write on `Camera::m_Address` | The write is on the connect task, under `Camera::m_Mutex`, before the link is up. The only reader that crosses tasks is `Camera::serialise()`, reached from `CameraList::save()` on the LVGL task at `STATE_ACTIVE`, which by definition is a state where every target is already connected and no connect for that camera is in flight. Nikon writes the same member from the NimBLE host task unlocked; this change is strictly narrower than the precedent it mirrors. | `lib/furble/FujifilmSecure.cpp:214`; `src/FurbleUI.cpp:7566` `CameraList::save()` inside `case Control::STATE_ACTIVE`; `lib/furble/Nikon.cpp:70`. |
| Adopting the advertised address can delete the wrong bond, or stop deleting a stale one | The stale-bond path never uses `m_Address`. It snapshots `m_Client->getConnInfo().getIdAddress()` after the link is up, because an RPA cannot be matched against the bond store, and deletes by that identity address. The adoption changes what is dialled, not what is unpaired. | `lib/furble/FujifilmSecure.cpp:242`, `lib/furble/FujifilmSecure.cpp:250`. |
| The deferred connection-saver fan-out can deadlock against `Control::m_Targets` and the camera mutex | No inversion is possible: `lib/furble` has no reference to `Control`, so no path takes `Camera::m_Mutex` and then `Control::m_Mutex`. `applyPendingConnSaver()` takes Control's mutex only to snapshot the target list, releases it, and then enters each camera; `Camera::setConnSaverEnabled()` releases `m_ConnParamsMutex` before calling `setConnProfile()`. | `grep -rn "Control::getInstance" lib/furble/` is empty; `src/FurbleControl.cpp:1708`. |
| The fan-out can touch a target that is being freed | `reapZombieTargets()` runs before `applyPendingConnSaver()` on every pass, and a target only leaves `m_Targets` for the drain set under the mutex, so the snapshot can only contain live `shared_ptr`s. | `src/FurbleControl.cpp:461` `Control::task()`. |
| The request can be lost, or applied twice with the wrong value | The pending flag is claimed before the value is read, so a request that lands after the claim re-arms the flag for the next pass. Coalescing two toggles into the last value is the correct outcome for a setting. `CMD_ERROR` really is a no-op on the control queue: `STATE_ACTIVE` skips it explicitly, and the other states only fall through to work the tick would have done anyway. | `src/FurbleControl.cpp:1691`, `src/FurbleControl.cpp:1708`; `cmd != CMD_ERROR` guard in the `STATE_ACTIVE` fan-out. |
| `control-conn-saver` passes even if the fan-out is deleted, so it does not pin the fix | It does pin it. The test turns the saver on, moves the link to the idle profile, clears the recorded threads, and then requires that a connection-parameter update still appears within 5 s. The target task's own idle timer cannot supply one inside that window because `m_ConnSaverIdleMs` is 10 s, so a deleted fan-out fails the `requested` check. | `tests/host/control_conn_saver_test.cpp`; `m_ConnSaverIdleMs` in `lib/furble/Camera.h`. |
| The pull request's Gates section shipped unfilled placeholder tokens | False, and worth recording because it was checked wrongly the first time. A truncated tool read of the description elided two long log blocks as `RESULT_PLACEHOLDER` and `BOUNDARY_PLACEHOLDER`, and the first pass of this review repeated that as a finding. The description carried `100% tests passed out of 216` and `100% tests passed out of 27` from the start; only the numbers were stale after the rebase, and they are updated to the rebased results. | `grep -c PLACEHOLDER` on the description body is 0 at every revision read. |

Red-first verification. Both fixes were reverted separately in a second build
tree; each revert produced exactly the failure the pull request documents, and
each restored cleanly (`git status` empty, no experiment markers left).

Fujifilm Secure address adoption removed from `_connect()`:

```
  FAIL: the saved reconnect did not complete (elapsed 502 ms)
  FAIL: the reconnect adopts the address the camera advertised
  FAIL: the serialised record carries the live address
EXIT=1
```

Connection-saver fan-out put back on the caller's task:

```
  FAIL: the connection-saver fan-out does not run on the caller's task
control-conn-saver: FAIL (1 checks)
EXIT=1
```

Both regressions pass on the branch as pushed: `fujifilm-secure-address` 0.73 s,
`control-conn-saver` 2.30 s.

Item-by-item verdict for the sixteen items the tranche covers, matching the
state lines now in the plan:

| Batch | Item | Verdict |
| :--- | :--- | :--- |
| 1 | B1 camera mutex split plus lock-free `isConnected` | Already fixed before this tranche |
| 1 | B2 non-droppable disconnect plus a bounded wait | Already fixed before this tranche |
| 1 | B3 exit edge out of `STATE_DISCONNECTING` | Already fixed before this tranche |
| 1 | B4 destroy targets outside the Control mutex | Already fixed before this tranche |
| 1 | A3a cancel and disconnect checks in the registration wait | Already fixed before this tranche |
| 2 | A1 `FujifilmBasic` null-deref hardening | Already fixed before this tranche |
| 2 | A3d `m_Configured` atomic | Already fixed before this tranche |
| 2 | A2 Ricoh stale-bond delete-and-retry | Already fixed before this tranche |
| 2 | A7a Fujifilm Secure address update from the rescan | Fixed in this tranche, verified red-first |
| 2 | A3b Basic registration-gate scope | Still open, needs a Basic-protocol body |
| 3 | B5 lock the three unlocked `m_Targets` walks | Already fixed before this tranche |
| 3 | B7 atomics for `m_State` and `m_ConnectCamera` | Already fixed before this tranche |
| 3 | B6 synthesized shutter release on leaving `STATE_ACTIVE` | Still open, needs the multi-connect session and a device answer |
| 3 | B9 connection-saver fan-out off the LVGL task | Fixed in this tranche, verified red-first |
| 3 | B10 single cancel-callback registration | Already fixed before this tranche |
| 3 | A8 small library correctness | Two of three already fixed; the scan-end success drain is still open |

The tranche fixed two items and invented no work for the eleven that had already
landed, which is the outcome the backlog existed to produce. The three items that
are still open say so, and say what they are waiting on.

Gates after the rebase, all run in a worktree at the rebased head: full host
suite `223/223` via `ctest --output-on-failure`; linked boundary conformance
`29/29` via `sh sim/scripts/run-boundary-conformance.sh`; `pre_review.py --base
origin/master` 13 of 13 gates; `check-doc-citations.py` 0 errors;
`sh sim/scripts/check-doc-tokens.sh` passes.
