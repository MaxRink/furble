# 384 - duplicate plan numbers

Status: docs-only, implemented in this PR. Seven plans move to 377 to 383, one
review record moves with its plan, and every reference that named an old number
is repointed. No plan's decision, evidence or claim is rewritten. The only prose
that changed besides a title line is four numbering preambles, which stated a
fact the move makes false.

## Problem

Four numbers were carried by more than one distinct plan on master:

| Number | Distinct plans | Review records at that number |
| --- | --- | --- |
| 332 | five | two |
| 335 | two | one |
| 354 | two | one |
| 364 | two | one |

A `plans/` reference is not always a full path. `include/CLAUDE.md` says "See
plan 332 before changing a count or a scale", `docs/power/board-power.md` writes
"(plan 354)" six times, `docs/stack-budget.md` links `plans/335-stack-cuts.md`,
and every index row and cross-plan link is a relative `NN-name.md`. At a
duplicate number each of those still resolved, so nothing failed loudly; it
resolved to whichever document the reader happened to open.

332 was worse than ambiguous. Both of its review records carried the number and
only one could carry the bare name, so `332-review-feedback.md` was the
simulator RTC review while the motion scale audit needed the longer
`332-imu-motion-scale-review-feedback.md`. A reviewer reading "332" in a source
comment had two implemented plans to choose between.

`plans/CLAUDE.md` says not to renumber an existing document. That rule protects
a merged plan's history and its citations. It is not a reason to leave two plans
on one number, because the history it protects is the one a stale reference can
no longer follow.

334 and 365 are not duplicates. Each is one plan plus its own
`NN-review-feedback.md`, which is the pattern `plans/CLAUDE.md` documents. 365
looked like a collision only while `fix/core-sim-align` was open; it merged as
#513 carrying both files.

## Numbers chosen

The allocation is 377 to 383, the first run above everything in use. "In use" is
three sources, not one, because a number committed to a branch that has not
merged yet is claimed just as firmly as one on master:

1. the tracked files on `origin/master`;
2. the tree of every open pull request branch, read with
   `gh pr list --state open` and `git ls-tree` on `refs/pull/<n>/head`;
3. the plans named in every open pull request body and title, which carry a
   claim before the file exists.

The highest number in those three sources was 376, so the run starts at 377.
473, 474, 475 and 511 sit above the free range but are PR-numbered review
records rather than plan slots, and nothing from 377 to 472 is claimed by any
local ref, remote ref or open pull request branch.

Two failed allocations are the reason this section reads sources rather than a
single high-water mark. The first draft of this change took 367 to 373 from
master alone; four of those were already claimed by open branches (#540 and #541
at 367, #539 at 371, #542 at 372), and merging it would have recreated the
duplicate at 367 that this plan removes. The second draft took 367 to 374 after
reading `#530` to `#534`, which had merged by then; 370, 374, 375 and 376 were
already claimed by pulls that opened while it was being written. The lesson is
in the sequence: re-derive the three sources immediately before the push, and
expect the answer to have changed.

The range has repeatedly been shared ground, which is why it was read ref by ref
rather than against master: 347, 355, 363, 364, 365 and 366 were each claimed by
different branches at different times. Plan 363 changed number twice for that
reason, plan 365 records a third collision in its own review table, and
[366-gps-receiver-selection.md](366-gps-receiver-selection.md) picks 366 because
364 and 365 were taken on the day it was written.

Nothing in the tooling catches a collision. `tools/check_pr_hygiene.py`
`check_plan_links` only tests that a link resolves, and a number two branches
both hold resolves perfectly. A later renumber stays safe the same way this one
does: read the three sources, allocate above the highest, re-derive before the
push, and record the mapping here and in the index so a stale reference can be
followed.

## Rule

The plan that keeps a number is the one the most files outside `plans/` cite,
counted by distinct file. Those are the references a rename has to rewrite by
hand, and they are the ones a reader is least likely to re-check. Within a
number the movers take consecutive numbers in the order they were added, so the
run is stable if a later duplicate has to join it. The counts that decide the
four shared numbers:

| Number | Plan kept | Files outside `plans/` |
| --- | --- | --- |
| 332 | `332-imu-motion-scale-audit.md` | 6 |
| 332 | `332-docs-gap-closure.md` | 4 |
| 332 | `332-gnss-vendor-audit.md` | 1 |
| 332 | `332-sim-rtc.md` | 0 |
| 332 | `332-vendor-document-verification.md` | 0 |
| 335 | `335-stack-cuts.md` | 1 |
| 335 | `335-display-touch-datasheet-promotions.md` | 0 |
| 354 | `354-core-family-fixes.md` | 7 |
| 354 | `354-gnss-datasheet-verification.md` | 1 |
| 364 | `364-ext5v-rail.md` | 16 |
| 364 | `364-hardware-candidates.md` | 0 |

The maximum is unique at all four numbers, so the rule names one plan rather
than breaking a tie. `332-vendor-document-verification.md` is the case that
shows why "cited somewhere outside `plans/`" is too weak a rule: it was cited
only from inside `plans/`, by two bare `plans/332` references in
`plans/357-review-feedback.md` that now name 378.

## Moves

| Old | New | Why |
| --- | --- | --- |
| 332-gnss-vendor-audit.md | [377-gnss-vendor-audit.md](377-gnss-vendor-audit.md) | Moved. 332 stays with the motion scale audit, which `include/CLAUDE.md`, `src/CLAUDE.md`, `include/FurbleIMUMotionScale.h` and the MPU-6886 driver cite |
| 332-vendor-document-verification.md | [378-vendor-document-verification.md](378-vendor-document-verification.md) | Moved. Nothing outside `plans/` refers to it |
| 332-docs-gap-closure.md | [379-docs-gap-closure.md](379-docs-gap-closure.md) | Moved. Cited only from `docs/components/` and `docs/protocols/`, never from source |
| 332-sim-rtc.md | [380-sim-rtc.md](380-sim-rtc.md) | Moved, with its review record, to [380-review-feedback.md](380-review-feedback.md) |
| 335-display-touch-datasheet-promotions.md | [381-display-touch-datasheet-promotions.md](381-display-touch-datasheet-promotions.md) | Moved. 335 stays with the withdrawn control stack cut that `docs/stack-budget.md` and plan 314 cite |
| 354-gnss-datasheet-verification.md | [382-gnss-datasheet-verification.md](382-gnss-datasheet-verification.md) | Moved. 354 stays with the Core family power fix cited from `docs/power/` and four source files |
| 364-hardware-candidates.md | [383-hardware-candidates.md](383-hardware-candidates.md) | Moved. 364 stays with the EXT_5V rail ownership plan cited from `include/`, `src/` and `docs/power/` |

Kept on their numbers: `332-imu-motion-scale-audit.md` with
`332-imu-motion-scale-review-feedback.md`, `335-stack-cuts.md` with
`335-review-feedback.md`, `354-core-family-fixes.md` with
`354-review-feedback.md`, and `364-ext5v-rail.md` with
`364-review-feedback.md`.

## References updated

- The file name and the title line of all seven moved plans, plus
  `380-review-feedback.md`.
- `plans/README.md`: the nine rows that named a moved path, the `#455` ownership
  row, the Next hardware paragraph, and a **Plan renumbering** section carrying
  the same old-to-new table so a reader arriving from a stale reference can
  follow it.
- `plans/346-plans-cleanup.md`: fourteen path mentions, including the Left alone
  table, which stays one row per file and is sorted again after the renames.
- `plans/346-review-feedback.md`, where the `#455` row that "belongs to plan 332"
  becomes plan 380.
- `plans/357-review-feedback.md`, whose two bare `plans/332` references mean the
  plan holding the SHA-256 hashes, not the motion scale audit, so they now name
  `plans/378`. That is the one reference the first draft of this change missed,
  because it named a number without naming a file.
- `plans/366-gps-receiver-selection.md`: its Related links and its numbering
  preamble, which asserted that 364 was claimed twice.
- `docs/components/README.md`, `docs/components/board-map.md`,
  `docs/protocols/README.md` and `docs/protocols/ble-hid.md`, the last two also
  carrying a `plan 332` label.
- `tests/test_gnss_command_contract.py` and `tools/power-model/README.md`.
- `plans/CLAUDE.md`, whose numbering bullet now states that a duplicate number is
  a defect the "do not renumber" rule does not protect, and how to choose the
  next number.

## What is deliberately unchanged

Nothing was archived. No two of the moved plans describe the same work: the four
documentation passes cover disjoint documents, and
[377-gnss-vendor-audit.md](377-gnss-vendor-audit.md) and
[382-gnss-datasheet-verification.md](382-gnss-datasheet-verification.md) call
each other parallel, the one auditing the configuration code and the other the
documents. `plans/346-plans-cleanup.md` already records that these plans stay at
top level on purpose.

Numbers that look like plan numbers and are not are untouched: the
`plans/98:364-367` line citation in `docs/power/soc.md`, the 9,364 byte figure
in `plans/implemented/305-general-internal-ram.md`, 214,748,364 in
`plans/320-rtc-use.md`, the 332 cell in `docs/hardware-catalog.md`, and the #364
pull request references in `plans/implemented/288-provisioning-schema.md` and
`plans/implemented/300-timezone-support.md`.

## Gates

- `python3 tools/check_pr_hygiene.py origin/master...HEAD --repo .`: 0 failures,
  0 warnings, including the `plan-links` check over `plans/README.md`
- `python3 tools/check_doc_citations.py`: 0 errors of 3,151 citations in 178 files
- `python3 tools/pre_review.py --base origin/master`: 13 of 13 gates pass, and
  the pre-push hook runs the same set
- `sh sim/scripts/check-doc-tokens.sh`: pass
- A per-number tally over `plans/` after the change finds no number carried by
  two distinct plans
- The three-source allocation was re-derived once more immediately before the
  push, at master `18bbcb5a` and the fourteen open pull requests from `#531` to
  `#545`: the highest number in use was 539 for a PR-numbered review record and
  375 for a plan slot, 377 to 472 was claimed by nothing, and 377 to 384 was
  clear
- Local Markdown link resolution compared against `origin/master`: eleven broken
  links before, the same eleven after, none new

No firmware, simulator, setting or doc claim changes, so no hardware gate
applies. The only non-Markdown edits are a Python docstring and one link in
`tools/power-model/README.md`.
