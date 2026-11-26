# 346 - Review feedback for the plans cleanup

## Scope

This is the review-feedback record for plan 346
([346-plans-cleanup.md](346-plans-cleanup.md)), branch `docs/plans-cleanup`,
PR 484 against fork master. The review was asked to attack the claims rather
than accept the green run: whether any moved plan leaves a dead link from code,
docs, `CLAUDE.md`, `CONTRIBUTING.md` or another plan; whether the counts are
arithmetically true of the tree it lands on; and whether the plan 167
justification is true in the code at the lines it cites.

The pull request carries no review threads, so every finding below is from this
independent pass. The branch was written against `4af87727`, rebased onto
`9a617355` eight times as master moved, and rebased again onto `b56acc88`
(#505) and `85b89931` (#494) for this review. Line numbers are from the tree
that merges.

Two rounds fed it:

1. the independent attack, which found two dead-link classes and a false count;
2. the rebases onto `b56acc88` and `85b89931`, which changed the arithmetic the
   document is built on and would have reverted three of master's corrections.

## Round 1 - dead links, counts and the plan 167 claim

| # | Source | Finding | Resolution | Evidence |
| ---: | :--- | :--- | :--- | :--- |
| 1 | Review | **Ten references to a moved plan arrived after the branch's base and were left pointing at the old path.** `plans/313-sim-only-status.md`, `plans/332-imu-motion-scale-audit.md`, `plans/332-vendor-document-verification.md`, `plans/335-display-touch-datasheet-promotions.md`, `plans/350-stickc-family-fixes.md`, `plans/352-m5gfx-board-pin.md`, `plans/354-gnss-datasheet-verification.md`, `docs/components/bq27220.md`, `docs/components/sx1262.md` and `docs/protocols/gnss-ubx.md` name a moved plan. | Fixed. All ten repointed, which makes the table 38 files rather than the 29 the first draft counted. | `git grep` for each moved name outside `plans/implemented/`; the table in the plan now lists all 38 |
| 2 | Review | **A relative link inside a moved plan resolved against the wrong directory.** `plans/implemented/322-hardware-abstraction.md` linked `[352](352-m5gfx-board-pin.md)`, which from `implemented/` is `plans/implemented/352-m5gfx-board-pin.md` and does not exist. The link arrived with #499, after the move was written, so the branch's own sweep could not have seen it. | Fixed to `[352](../352-m5gfx-board-pin.md)`. The other five relative links in that plan already carried `../`. | `plans/implemented/322-hardware-abstraction.md`, `352-m5gfx-board-pin.md` exists at `plans/` only |
| 3 | Review | **The counts were not arithmetically true of the landing tree.** The plan said master "carries 91", listed 80 documents left alone and called itself "the eighty-first at top level". `85b89931` carries 117 numbered documents at top level, and eleven move, so 106 stay; with this plan and its review record the directory holds 108. Twenty-six plans arrived between `9a617355` and `85b89931`. | Fixed. The prose names 68 at `4af87727`, 91 at `9a617355` and 117 at `85b89931`, and states that 106 rows plus the two new files make 108. The left-alone table has one row per remaining file (106), and the twenty-six late arrivals carry the reason that this change did not review them. | `git ls-tree --name-only <rev>:plans/` filtered to numbered `.md`, compared with the table's rows |
| 4 | Review | **Duplicate plan number.** `plans/345-doc-citation-gate.md` merged as #511 while this branch was open, and the branch adds `plans/345-plans-cleanup.md`. Two documents cannot share a number, and a future `plans/345-review-feedback.md` would be ambiguous. | Fixed. The plan and its two index rows are now 346, which is free between 345 and 347. The document keeps its history; only the number changed. | `plans/` numbering; `plans/CLAUDE.md` "do not renumber existing docs" applies to merged documents, which this one is not |
| 5 | Review | **A drifted line citation in a status correction.** `plans/33-wifi-hub.md` cited `platformio.ini:69` for `-DEXCLUDE_COMPONENTS=icons`. Line 69 was right at `9a617355`; two env rows landed above it and the flag is line 74 here. The citation gate cannot see this: it checks path and range only for a citation with no symbol, which is the blind spot its own docstring names. | Fixed to `:74`, and the plan records that the line moved. | `platformio.ini:74` holds `-DEXCLUDE_COMPONENTS=icons`; `platformio.ini:71` opens `[env:esp32-s3-headless]` |
| 6 | Review | **A repointed citation pointed at the wrong text.** `plans/implemented/326-review-feedback.md` cited plan 322's phase 4 as `:216-223`, a uniform `+13` bump of the base's `:203-210`. Both are wrong: the phase 4 census ("31 calls to 7", four geometry and two input-wiring switches, the eighth site in the simulator) is at `:254-259` here. | Fixed to `:254-259`. | `plans/implemented/322-hardware-abstraction.md:254` to `:259` |
| 7 | Review | **Eight moved documents gained a status line the corrections table did not list.** The section said "Only status lines changed" and named four plans, while the move also adds `Status: implemented on master (archived 2026-11-25).` to eight documents that had none. | Fixed. The eight are listed beside the table, as new lines rather than corrections. | `head -6` of each moved document |

## Round 2 - the rebase onto `b56acc88`

The branch was based on `9a617355`. Nine commits on the branch were themselves
rebases, so replaying them produced nested conflicts; the net change was applied
to `b56acc88` as one patch instead. Three of master's corrections would have
been reverted by a naive replay. Master moved again before the merge, to
`85b89931`, and the rebase onto it conflicted only on the ownership table: the
`upstream #316` row for plan 359, added by #508, is kept and the `#464` row is
retired with `#451` as the plan documents.

| Item | Resolution |
| :--- | :--- |
| `plans/README.md` ownership rows | Kept master's `#455` row, which belongs to plan 332 and does not move. Only the `#451` and `#464` rows are retired into the closed-issue sentence, which is what the plan documents |
| `plans/314-review-feedback.md` row 11 | Kept master's 2026-11-26 text from the #478 review and repointed only `plans/331-merge-gate-base.md`. The branch's version was older and dropped that note |
| `docs/components/*.md` | Kept master's newer bodies from #504 and #519 and applied only the path repoint; the branch's copies were the older text |
| `plans/331-merge-gate-base.md` | Moved with master's merged status line, plus the archive sentence |
| `plans/README.md` sim-only note | Updated from `b57f0118` to `85b89931`, the commit it actually lands on |

## Verified true

- **The plan 167 justification holds at the lines it cites.** `src/FurbleUI.cpp:2816`
  is `if (checkbox) {`, and the branch returns at `:2822`, before
  `} else if (wrapText) {` opens at `:2843`, whose `LV_LABEL_LONG_WRAP` is set at
  `:2858`. `addCameraItem()` passes `checkbox = (mode == MODE_MULTICONNECT)` and
  `wrapText = true` to the same call (`src/FurbleUI.cpp:3231`, `:3237`), so the
  wrap branch is genuinely unreachable for those rows. Plan 167 also carries a
  `## Follow-up, not in this change` section with four open items, so it still
  owes implementation work and `plans/CLAUDE.md` keeps it at top level.
- **The gps-ubx scenario count is 20.** `sim/scenarios/e2e/gps-ubx-*` expands to
  exactly twenty files.
- **68 and 91 are true of the revisions they name.** `4af87727` has 68 numbered
  top-level plans and `9a617355` has 91.
- **All eleven moves are backed by a merge.** `cd04c38a`/`511c11d3` for 321,
  `fd51e96c` for 322, `a26e73f8` for 323, `663d106f` for 324, `139d884a` for
  325, `92618bff` for 326 and `b57f0118` for 331, all present in `git log`.
- **The archive sentence on plan 322 leaves its status text verbatim.**
  `diff` against `origin/master:plans/322-hardware-abstraction.md` shows the one
  added line and nothing else in the status paragraph.

## Judged, not changed

- Three links in `plans/` are broken on master too and are not this change's:
  `plans/332-imu-motion-scale-audit.md:10` to `implemented/18-gps-motion.md`,
  and `plans/352-m5gfx-board-pin.md:7` and `:8` to `../implemented/42-waveshare-eth-node.md`
  and `../314-memory-resilience.md`. Fixing another plan's body is outside a
  docs sweep whose stated rule is that no moved text is rewritten. They are
  recorded here so the next sweep starts with them.
- The `## Moves` table does not claim any plan stops being complete. Hardware
  and soak gates inside the archived plans stay where they are, which
  `plans/CLAUDE.md` allows.

## Plan links and citations

The repository's plan-link check is `check_plan_links()` inside
`tools/check_pr_hygiene.py`: it reads `plans/README.md` and fails a local
Markdown link whose target does not exist. It passes, and so does a wider sweep
of every Markdown link in the tree, once the pre-existing `docs/wiki/`
page-name links and the vendored `components/esp-nimble-cpp/` README are set
aside.

| Gate | Result |
| :--- | :--- |
| `python3 tools/check_pr_hygiene.py origin/master...HEAD` | PASS, `plan-links`: 0 failures, 0 warnings |
| `python3 tools/check_doc_citations.py` | PASS, 0 errors of 2899 citations in 162 files |
| `sh sim/scripts/check-doc-tokens.sh` | PASS, simulator documentation tokens are present |
| `python3 tools/pre_review.py --base origin/master` | PASS, 13 gates |
| `python3 tools/check_review_threads.py --repo MaxRink/furble --pr 484` | see the merge |
