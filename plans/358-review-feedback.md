# 358 - Review feedback for the resilience round three fixes

## Scope

This is the review-feedback record for plan 358
([358-resilience-round3.md](358-resilience-round3.md)), branch
`fix/358-resilience-round3`, PR 516 against fork master. The review was asked to
attack the two fixes rather than accept the green run: whether the rename
sequence really preserves the previous backup on a full card, whether the
temporary file can be left behind or the `fsync` skipped, whether the keepalive
recovers a refused notification without spamming, and whether the companion test
proves the refusal case rather than only the happy path.

The branch carries no review threads on the pull request itself, so every
finding below is from this independent pass. It was rebased onto `b56acc88`
(#505), `a8082cc1` (#484) and the master it lands on before the review finished; line numbers are from the tree that merges.

Two rounds fed it:

1. the independent attack on both fixes, which found a defect in each that the
   branch's own tests could not see;
2. the rebase and verification pass, which re-ran every gate on the merged tree
   and corrected what master had moved underneath the branch.

## Round 1 - the two fixes

| # | Source | Finding | Resolution | Evidence |
| ---: | :--- | :--- | :--- | :--- |
| 1 | Review | **The replacement never happened on the card.** `SD::exportSettings()` replaced the backup with a bare `rename()`. FatFs `f_rename()` answers `FR_EXIST` when the destination name is already in use, and `vfs_fat_rename()` maps that to `EEXIST`; only a POSIX `rename()` replaces. Every export after the first on a card would have failed with `Unable to replace the settings export: File exists`, so the backup could never be updated and the fix only worked on the host. | Fixed in the export. The old copy is dropped and the rename retried when the first call reports `EEXIST`. A complete temporary file is kept when only the replace fails, because it is then the only copy; an incomplete one is still removed. | `src/FurbleSD.cpp:1101`, `:1106`, `components/fatfs/src/ff.c` `f_rename` name-collision branch, `components/fatfs/vfs/vfs_fat.c:332`. The case now defines `rename()` with FatFs semantics, so a bare rename fails it: `tests/host/boundaries/sd_driver_test.cpp` `runExportFull()` |
| 2 | Review | **The keepalive could not fire.** `notifyCameras()` refreshed `m_LastCameraNotificationMs` on every call that passed the rate gate, including the calls that found nothing to send. The companion loop calls it once a second, so the measured gap was one second forever and only a test that jumps the clock thirty seconds in one step could reach `CAMERA_KEEPALIVE_MS`. | Fixed in the service. The stamp is written only when at least one record is handed to the transport, which is what `notifyStatus()` has always done, and the keepalive measures from a second stamp written on a full publish. One stamp would still starve: a list that changes on every pass would hold it at one second. The test calls `notifyCameras()` once a second for thirty calls, which is the production cadence, and repeats that with one camera toggling every second. | `src/FurbleCompanionService.cpp:455`, `:481`, `:484`, compare `:284`; `include/FurbleCompanionService.h:307`; `tests/host/companion_gatt_test.cpp` `testCompanionCameras()` |
| 3 | Review | The companion test proved the mechanism, not a refusal: the mock recorded every notification, so nothing modelled the controller dropping one, and the assertion could not fail if the recovery path were wrong. | Fixed in the mock. `MockCentral::setRefuseNextCameraNotify()` drops the next camera notification, which is the only place a refusal can be modelled because the transport returns no delivery result. The test then requires the keepalive to put the record back on the phone. | `tests/host/companion_gatt_test.cpp`, `MockCentral::notify` |
| 4 | Review | The linked boundary case count moved from 18 to 19, but the tree this lands on has 26: #505 registered six more cases (`boundary-ir-polarity-*` and `boundary-shared-pin-*`) and #494 two more (`boundary-sd-shutdown-*`) after the branch's base, and the count includes `real-wifi-lifecycle`. Adding `boundary-sd-export-full` makes 27, not 19. The rebase caught the second half of the drift: the branch had already corrected 18 to 25, and master moved to 26 under it. | Fixed in all five documents, and the runner now reports `100% tests passed out of 27`. | `AGENTS.md:158`, `CLAUDE.md:273`, `docs/sim.md:1732`, `docs/sim-coverage.md:114`, `sim/CLAUDE.md:544`; the counting rule is in `plans/357-review-feedback.md:46` |
| 5 | Review | `sim/scripts/run-boundary-conformance.sh` selects `^(boundary-\|real-wifi-lifecycle$)` but did not build `boundary_shared_pin_test`, so the run would report that case as not found. Pre-existing, from #505, but it is the runner whose count this change corrects. | Fixed. The target is in the build list. | `sim/scripts/run-boundary-conformance.sh:28` |
| 6 | Review | The citation the branch re-pointed, `handleAuth()` at `src/FurbleCompanionService.cpp:600-657`, and the `SETTING_BAD_LENGTH` and `SETTING_UNKNOWN_ID` neighbours, would drift again with the keepalive change. One of them was already eight lines off before it. | Fixed after the code settled: `:606-663`, `:1049-1057`, `:1090-1094`, and `handleSettings` at `:1039-1200` in `plans/51-app-feature-parity.md`. | `python3 tools/check_doc_citations.py`, 0 errors |
| 7 | Review | The pre-existing ASan heap-use-after-free in the shared Fujifilm peer double was recorded as finding 9 and left open. | Fixed here rather than left recorded. See "The recorded test-double race" below. | `lib/testing/peer/FujifilmVirtualCamera.h:255` |

## The recorded test-double race

The author's pass saw `heap-use-after-free` once under ASan in
`shutterWriteCount()` (`tests/host/companion_gatt_test.cpp:74`) and recorded it
as finding 9 with a precise diagnosis: `FujifilmVirtualCamera::writes()` handed
out a reference to its write log while the peer write handler appended to it
from a Control task. It did not reproduce in 48 stress runs.

Recording it was the right call for that pass: a one-off abort with no
reproduction is not something to fix by guesswork, and the report named the
suspect rather than the cause. It is not acceptable to ship it, for three
reasons. The diagnosis is specific enough to act on without a reproduction,
because the race is structural: a const reference outliving a `push_back()` on
the same vector. The double backs `companion-gatt`, which gates every pull
request, and that abort already failed one suite run. And the fix is three
accessors returning a snapshot instead of a reference, with no behaviour change
at any call site.

So `writes()`, `notifications()` and `lastGeotag()` now return by value. The
finding named only the first; the peer write handler also assigns
`m_LastGeotag` and the notify path appends to `m_Notifications`, so the same
defect existed three times in the same class and fixing one would have left the
other two.

## Round 2 - the rebase onto `b56acc88`

The branch was based on `81139818`. Master added the StickC family work (#505)
in between, and a plain replay of the branch patch would have removed it.

| Item | Resolution |
| :--- | :--- |
| `tests/host/CMakeLists.txt` conflicted | Kept master's `pin_share_policy_test`, `boundary_shared_pin_test`, `boundary-ir-polarity-*` cases and `FURBLE_PIN_SHARE` definitions, and applied only the `export-full` additions |
| `AGENTS.md`, `CLAUDE.md` | Kept master's #478 workflow-edit guard text; only the case count changed |
| `include/CLAUDE.md` | Kept the `FurblePinShare.h` ownership paragraph; the keepalive paragraph is added beside it |
| `docs/sim.md` | Kept the RTC sidecar sweep sentences; only the case count changed |
| `docs/settings-and-controls.md`, `docs/wiki/Settings-Reference.md` | Kept the StickC Plus timed-wake correction from #505; only the `Export Settings` paragraph is added |
| `plans/README.md` | Kept both the 358 row and the rows master added |

## Verified true

These were attacked and hold:

- The refusal of `Settings::COMPANION_PASSWORD` is deliberate and symmetric:
  `serializeSetting()` returns `false` for it (`src/FurbleSD.cpp:266`) and
  `importSetting()` refuses it too (`src/FurbleSD.cpp:617`). So treating it as a
  serialization error did abort every export, and `Settings::getAll()` is an
  `std::unordered_map`, so the truncated file was an arbitrary prefix. The
  486-of-1575-byte figure in the plan is the same defect.
- Opening `settings.txt` with `"w"` truncated the previous backup before a byte
  of the new one existed. The temporary file removes that.
- The flush order is right: `fflush()`, then `fsync(fileno())`, then `fclose()`,
  then the replace. No path skips the `fsync()`; a `fclose()` failure fails the
  export and removes the temporary file.
- The temporary file cannot accumulate. A successful export renames it away, a
  failed one removes it, and a crash between the two leaves a file that the next
  export reopens with `"w"`.
- An export that writes no setting fails, so an empty file cannot overwrite a
  usable backup on import.
- The keepalive sits inside the existing one-second rate gate, so the cost is
  one full batch per thirty quiet seconds, not a stream.

## Judged, not changed

- The 30 s cadence is a judgement call that mirrors the status packet, not a
  measurement. It stays stated as such in the plan.
- `runExportFull()` models a full card with `RLIMIT_FSIZE` rather than a real
  `ENOSPC`. That is the seam the existing `full` GPX case uses, and the failure
  path through `fprintf`, `fflush` and `fclose` is the same one either way.
- A failed export is still reported in the log only, with no dialog. Adding one
  is a UI change with its own screenshot matrix, recorded as a gap in the plan.
- The discarded `indicate()` result on the settings and auth branches, and the
  ignored `esp_timer_start_once()` result, are findings 7 and 8 of the plan:
  unchanged and out of scope here.

## Gates

Run on the merged tree:

| Gate | Result |
| :--- | :--- |
| `boundary-sd-export-full` | PASS, `sd.export_first bytes=1575`, `sd.export_replace replaced=1 bytes=1574`, `sd.export_full backup_preserved=1 bytes=1574` |
| Same case with a bare `rename()` | FAIL, as it must: `a second settings export did not replace the previous backup` |
| `companion-gatt` | PASS |
| Same case with the stamp refreshed on every call | FAIL, as it must: `the keepalive re-published nothing over thirty one-second calls` |
| Full host suite, `ctest --test-dir /tmp/b516 -j 4` | PASS, `100% tests passed out of 212`; one rerun under four parallel jobs timed out `control-tsan-wrapper-contract` at its 10 s budget, which passes standalone in 1.8 s |
| `python3 tools/pre_review.py --base origin/master` | PASS, 13 gates |
| `sh sim/scripts/check-doc-tokens.sh` | PASS |
| `python3 tools/check_doc_citations.py` | PASS, 0 errors of 2895 citations |
| `python3 tools/check_pr_hygiene.py origin/master...HEAD` | PASS, 0 failures |
| `python3 tools/check_review_threads.py --repo MaxRink/furble --pr 516` | see the merge |
