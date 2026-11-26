# 320 - Review feedback for the timed-wake wall clock

## Scope

This is the review-feedback record for
[320-rtc-use.md](320-rtc-use.md) C2, keeping the wall clock across a timed wake,
branch `feat/rtc-domain-320`, PR 533 against fork master. The review was asked to
attack the accuracy claim rather than accept it: what happens after a power loss
versus a soft reset, how drift is bounded, how a stale or invalid value is
detected and rejected, whether the change trades NVS wear for a wrong time
anywhere a user would notice (a GPS log with wrong timestamps, an intervalometer
firing early), whether the simulator model covers the new behaviour so a
regression would fail a scenario, and whether the docs state the accuracy
honestly.

The branch carries no review threads on the pull request itself, so every finding
below is from this independent pass. The head first reviewed was `322859fe`; the
author then force-pushed `ac604abf`, the same content reformatted, and this record
covers the tree that merges: `ac604abf` rebased onto `18bbcb5a` with the review
commit on top. Master moved twice while this review ran (#493, #530); the branch
was rebased onto the new master each time rather than merged against a stale
base. Line numbers are from the tree that merges.

## What was reviewed

- Tree: a scratch worktree at `/tmp/furble-r533`, never the primary checkout.
- `src/FurblePlatform.{h,cpp}`, `src/FurbleTimeKeeper.cpp`,
  `src/FurbleTimeKeeperPolicy.cpp`, `include/FurbleTimeKeeper.h` against
  `origin/master` (`18bbcb5a`), plus the simulator model, the two scenarios, the
  scenario manifest and runner, and every document the change touches.
- The consumers, read rather than assumed: `UI::Intervalometer::startResume()`
  and `validResume()` (`src/FurbleUIIntervalometer.cpp`), `trustedWallClock()`,
  `TimeKeeper::flush()` and `retainCurrent()` (`src/FurbleTimeKeeper.cpp`),
  `Camera` time sync, and the companion and MQTT status paths.
- Commands: `time_keeper_policy_test` and `time_keeper_persistence_test` in a
  host build, the `-Wswitch` probe below, `python3 tools/pre_review.py --base
  origin/master`, `sh sim/scripts/check-doc-tokens.sh`.

## Does the simulator model cover the new behaviour

Yes for the adoption, and it is wired into CI. `intervalometer-deep-sleep-clock-start.txt`
seeds a wall clock through the production `TimeKeeper`, arms a timed power-off,
and the runner re-execs the simulator with the prefs preserved;
`-clock-resume.txt` then asserts `time.valid yes`, `time.source timed_wake`, the
epoch inside the expected window, `time.uncertainty_ms` inside the claimed
window, `platform.timed_wake yes`, `platform.wake_marker no`, and that the saved
resume deadline runs instead of a boot-relative wait (one shutter press,
`ui.interval_state finished`). Both are registered in
`sim/scenarios/manifest.json` and executed by `sim/scripts/run-deep-sleep.sh`,
which `.github/workflows/sim-e2e.yml:328` runs. Removing the adoption would fail
`time.source` and the epoch window; removing the buffer clear would fail
`platform.wake_marker`.

Two of the firmware's properties are **not** covered by it, and the documents
now say so rather than leaving a reader to assume otherwise:

1. **NVS wear.** On device the record lives in the M5PM1 RTC buffer and costs no
   flash write. The simulator has no PM1 buffer, so its copy is a prefs write,
   which means a wear regression cannot fail a scenario. The property is held by
   `time_keeper_persistence_test`, which asserts `nvs_test_commit_count() == 1`
   before and after the adoption.
2. **The marker gate.** The simulator drives its own `Platform` double, so the
   scenario proves the double's guard, not production's
   `Platform::consumeTimedWake()`. That gate is not host-testable at all.

## Round 1 - findings and resolutions

| # | Source | Finding | Resolution | Evidence |
| ---: | :--- | :--- | :--- | :--- |
| 1 | Review | **The stated uncertainty was too small, and the error compounds.** `README.md`, `docs/settings-and-controls.md` and the wiki mirror described the adopted clock as carrying "2 seconds plus 2 percent of the powered-off interval". The code adds that to the uncertainty the clock *already* carried, and because each sleep re-predicts from the previous prediction, a timelapse adds the same amount again on every cycle. A run started from an NVS-restored clock is nearly an hour out of the stated window on its first cycle, and a long run drifts by the per-cycle term. | Fixed in the three documents. They now state the full formula (the uncertainty at shutdown plus 2 s plus 2 percent), that it grows once per cycle, that the 2 percent is an estimate because the M5PM1 timer's accuracy is unpublished, and that a GPS, NTP or companion sample replaces it. | `README.md`; `docs/settings-and-controls.md`; `docs/wiki/Settings-Reference.md`; `src/FurbleTimeKeeperPolicy.cpp` `timedWakeUncertaintyMs()`; `plans/320-rtc-use.md` |
| 2 | Review | **Nothing said what the accumulated error does to a shot or a timestamp**, which is the question a user would ask and the one the review was pointed at. The answer is not uniform: the intervalometer's *relative* timing is immune because the saved deadline and the adopted clock come from the same prediction chain, but an absolute timestamp carries the whole window. | Fixed in `plans/320-rtc-use.md`, which now derives both cases, and in `README.md` and the two settings documents. Also recorded there that `trustedWallClock()` has no uncertainty threshold, so the window is reported rather than enforced. | `src/FurbleUIIntervalometer.cpp` `startResume()` (`state.wake_time - now`), `saveResume()` (`now_s + next_ms / 1000`), `trustedWallClock()` |
| 3 | Review | **`sourcePriority()` took its authority from the enum's position for anything not explicitly listed.** The default branch returned `static_cast<uint8_t>(source)`, so a source appended at 7 would have outranked GPS and NTP without anyone deciding it. The header states the opposite contract: a new source "takes its authority from sourcePriority() rather than from its position". | Fixed. `sourcePriority()` now lists every enumerator with no `default`, so appending a source is a `-Wswitch` build failure. Behaviour is unchanged for every existing value. Verified with a probe: adding `FUTURE = 7` fails the build with `error: enumeration value 'FUTURE' not handled in switch [-Werror,-Wswitch]` at `src/FurbleTimeKeeperPolicy.cpp:70`. | `src/FurbleTimeKeeperPolicy.cpp` `sourcePriority()`; `include/FurbleTimeKeeper.h` |
| 4 | Review | **"Five gates, and each is host-tested" was false.** Gate 3, the board's wake marker, lives in `Platform::consumeTimedWake()`, which needs the M5PM1. No host test reaches it; `TimeKeeper::adoptTimedWake()` is called directly in `time_keeper_persistence_test`. | Fixed in `plans/320-rtc-use.md`, which now names which gates are host-tested (1, 2, 4, 5) and states that gate 3 is structural and scenario-visible only. `docs/sim.md` says the same from the simulator's side. | `src/FurblePlatform.cpp` `consumeTimedWake()`; `tests/host/time_keeper_persistence_test.cpp` |
| 5 | Review | **A record could outlive the boot that should have consumed it.** `consumeTimedWake()` returned before clearing the buffer when the record read failed, so an uninterpretable record survived; and `powerOffUntil()` wrote the marker without clearing the record region when it could not build a prediction, so a stale record from an earlier shutdown could pair with the new marker. Both contradict the "no later boot may pair one with the other's absence" comment above `TIMED_WAKE_CLEAR`. | Fixed in both directions: `consumeTimedWake()` clears the whole buffer on every boot and returns false afterwards if the read failed, and `powerOffUntil()` clears the record region when `prepareTimedWake()` produces nothing. The simulator double already removed its key unconditionally, so it matches. | `src/FurblePlatform.cpp` `powerOffUntil()`, `consumeTimedWake()`; `sim/FurblePlatformSim.cpp` |
| 6 | Review | **The head first reviewed (`322859fe`) failed the repository's own clang-format gate.** Four files it changes did not match clang-format 21.1.2: `src/FurblePlatform.cpp`, `src/FurbleTimeKeeper.cpp`, `sim/FurblePlatformSim.cpp` and `sim/hardware_model.cpp`, while the pull request body claimed `pre_review.py --base origin/master: PASS, 13 gates`. | Already fixed upstream. The author force-pushed `ac604abf`, which is the same content reformatted: the diff over those source files is whitespace and line-wrap only. This review re-checked the replacement head and it passes, so no change was carried for it. Recorded because the claim was wrong at the head that was reviewed. | `git diff 322859fe ac604abf -- src/ sim/`; `tools/check_format.py --base origin/master` on the merged tree |
| 7 | Review | **The simulator's own limits were not stated**, so a green e2e run could be read as covering the no-flash-write property and the marker gate, which it cannot. | Fixed in `docs/sim.md`: the storage choice is named as the reason the wear property is out of scope there, and the host commit counter is named as what holds it. | `docs/sim.md`; `sim/FurblePlatformSim.cpp` `TIMED_WAKE_RECORD_KEY` |

## Findings judged false or already correct

| # | Candidate | Verdict | Evidence |
| ---: | :--- | :--- | :--- |
| 1 | A lower-authority sample can displace the adopted prediction, or the prediction can displace a GPS or NTP fix. | **False.** `shouldAccept()` compares `sourcePriority()` both ways. `TIMED_WAKE` is 2, so it beats `NVS` (1) and loses to `RTC` (3), `COMPANION` (4) and GPS/NTP (5). `time_keeper_policy_test` asserts both orderings. | `src/FurbleTimeKeeperPolicy.cpp` `shouldAccept()`; `tests/host/time_keeper_policy_test.cpp` |
| 2 | A power loss can make the board adopt an interval that never elapsed. | **False.** Adoption requires `(wakeSource & M5PM1_WAKE_SRC_TIM) != 0` **and** the marker, and the buffer is cleared on every boot. A battery pull loses the PM1 RTC buffer as well as the wake source. A soft reset keeps the buffer but has no timer wake source, so it clears without adopting. `time_keeper_persistence_test` covers the record-present-but-not-a-timed-wake case at the policy level, and the scenario asserts `platform.wake_marker no` after a confirmed one. | `src/FurblePlatform.cpp` `consumeTimedWake()`; `docs/components/m5pm1.md` register 7 |
| 3 | The change trades NVS wear for a wrong clock. | **False, and it reduces NVS writes.** The record is a PM1 RTC-buffer write. `flush()` at the shutdown still checkpoints NVS under its existing `NVS_CHECKPOINT_MIN_WRITE_INTERVAL_MS` rate limit, unchanged by this branch, and adoption writes nothing: `time_keeper_persistence_test` asserts the commit count is unchanged across it. | `src/FurbleTimeKeeper.cpp` `flush()`, `retainCurrent()` (`RTC_NOINIT` record only); `tests/host/time_keeper_persistence_test.cpp` |
| 4 | The intervalometer can fire early because of the predicted clock. | **Not by the accumulated error.** `startResume()` computes `wake_time - now`, and both values come from the same prediction chain, so a constant offset cancels. It can fire early only by the clock's error at the moment `saveResume()` ran, and only when another source re-anchors the clock before `startResume()`, which is bounded by the stated window and is now documented. | `src/FurbleUIIntervalometer.cpp` `startResume()`, `saveResume()`; `plans/320-rtc-use.md` |
| 5 | A GPS log can carry a timestamp from the wrong prediction for a long time. | **Not for long.** A GPX point needs a fix, and a fix is a GPS time sample at priority 5, which replaces the prediction through `TimeKeeper::update()`. The exposure is the interval between the wake and the first fix, inside the stated window. | `src/FurbleTimeKeeper.cpp` `update()`; `src/FurbleGPS.cpp` time path |
| 6 | `valid()` and `decode()` now accept a source above NTP because `TIMED_WAKE` is the largest enumerator, so the validity ceiling is positional too. | **True but not a defect.** These are allowlists by ordinal and `TIMED_WAKE` is the last enumerator, which is correct today. The header's "not from its position" promise is about authority, which `sourcePriority()` now enforces structurally. Recorded rather than changed, so the two checks do not diverge for a reader. | `src/FurbleTimeKeeperPolicy.cpp` `valid()`, `decode()` |
| 7 | The record write is skipped when the shutdown later fails, leaving a prediction that a later boot adopts. | **False.** Every failure return after the marker write clears the whole buffer, including the SD refusal and the `shutdown()` failure, and `prepareTimedWake()` runs after those. | `src/FurblePlatform.cpp` `powerOffUntil()` |
| 8 | The 28-byte record can overrun the 32-byte PM1 buffer. | **False.** A `static_assert` fails the build if the record plus the marker passes 32 bytes, and the buffer size is cited to the manual. | `src/FurblePlatform.cpp` `static_assert`; `docs/components/m5pm1.md` fact 8 |

## Accuracy, stated plainly

A clock adopted after a verified timed wake is the shutdown prediction advanced
by the armed interval, plus the time measured since the rail came back. Its
uncertainty is the uncertainty it had at shutdown, plus 2 s, plus 2 percent of
the armed interval. The 2 percent is an estimate: the M5PM1 timer counts whole
seconds and neither its clock source nor its accuracy is published. The term is
conservative on purpose, so the window is more likely to overstate the error than
to understate it, and the earlier documents understated the window by omitting
both the input uncertainty and the per-cycle growth.

Nothing in this record narrows a hardware gate. The bench run that would replace
2 percent with a measured figure stays open, as does C1's RTC-retention matrix.

## Verification

- `time_keeper_policy_test` and `time_keeper_persistence_test`: pass.
- `tools/run_sanitized_host_tests.sh -R '^(time-keeper)'`: 2/2 pass under ASan,
  UBSan and LeakSanitizer.
- `-Wswitch` probe as recorded in finding 3.
- `tools/check_doc_citations.py`: 0 errors of 3,104 citations. Two citations in
  `plans/implemented/` were re-pointed after this branch's own additions moved
  them, and the `docs/components/m5pm1.md` table was recomputed against the
  merged tree.
- `python3 tools/pre_review.py --base origin/master`.
- `sh sim/scripts/check-doc-tokens.sh`.
