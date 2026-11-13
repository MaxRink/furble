# 323 - Review feedback for the plans refresh

Implementation state (2026-11-13): this is the review-feedback record for
PR #441, which archives implemented plans and corrects stale status. Every
Copilot, human and pre-push subagent finding is recorded as fixed, already
fixed or a false positive, with evidence.

## PR #441 (`docs/plans-archive-refresh`)

| # | Source | Finding | Resolution | Evidence |
| ---: | :--- | :--- | :--- | :--- |
| 1 | Copilot | `plans/implemented/32-gps-advanced.md` still said plan 14 was implemented on `feat/14-gps-pcas`. | Fixed. The row names the fork merge `51a6f28a` and says upstream gkoh/furble#303 closed unmerged. Lines 20 and 21 say the same. | `git log --oneline 51a6f28a -1` |
| 2 | Subagent review | `plans/304-webui-stick-memory.md` linked plan 305 at its old path. | Fixed. The link points into `implemented/`. | Relative link check |
| 3 | Subagent review | Plans 308 and 310 said the main stack reverted in #311. PR #311 is a release permissions fix. | Fixed. They name #407 (`0883a31e`). `docs/sim-coverage.md` had the same mix-up and is fixed too. | `git log -S'CONFIG_ESP_MAIN_TASK_STACK_SIZE=12288' -- sdkconfig.m5stick-s3` |
| 4 | Subagent review | Plan 32 listed plan 18 as planned and Experiment B as pending. | Fixed. Plan 18's detector merged as #65. Experiments A and B are recorded in plan 00. | `plans/18-gps-motion.md`, `plans/00-hardware-experiments.md` |
| 5 | Subagent review | Plans 28 and 63 named the closed PR #44 and branch `feat/ui-screenshot-ci`. | Fixed. Screenshot CI merged as #67 (`f43910ac`). | `.github/workflows/ui-screenshots.yml` |
| 6 | Subagent review | Plan 151 said `PeerStall.h` lived on branch `feat/sim-cancel-sweep`. | Fixed. It is on master since `2dc652a7` under plan 172. | `lib/testing/peer/PeerStall.h` |
| 7 | Subagent review | Plan 314's review record named only the follow-up branch for row 18's sibling fix. | Fixed. It names #428. | `git log --oneline origin/master \| grep '#428'` |
| 8 | Subagent review | The LUMIX footnote left out the S5II first-pair risk. | Fixed. The footnote says a fresh S5II may still need a Lumix Sync first-pair flow. | `lib/furble/Lumix.cpp`, `plans/implemented/72-lumix.md` |
| 9 | Subagent review | Plan 86 said F2 was still unfixed, while #116 showed its stated root cause did not hold. | Fixed. The header says the static cause did not hold and the #32 symptom stays open as a hardware investigation. | PR #116 body |
| 10 | Subagent review | Older archived plans (12, 13, 68, 89, 100, 101, 112, 122, 123, 150, 174, 300) and plan 29 still said "implemented on" a feature branch. | Fixed on owner request. Each line names its merged PR and commit. Plan 170's reproduction branch still exists on origin and stays. | `gh pr list --state merged --head <branch>` |

DJI Osmo and Pentax K are missing from `docs/supported-hardware.md`; a
follow-up branch adds them.
