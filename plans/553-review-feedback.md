# PR 553 review feedback: pull request concurrency cancellation

Reviewed at `966c3b97` (`fix/ci-concurrency`) against `origin/master`
`789dd78c`, merge base `c8159ee9`, in a throwaway worktree under
`/tmp/furble-553`. Review posted as
https://github.com/MaxRink/furble/pull/553#pullrequestreview-6082892290 with
three inline comments. GitHub refuses a formal `REQUEST_CHANGES` from the
author account, so the review is a comment review carrying the same verdict.

The change is mechanically clean and the scope claim is true. It is not
mergeable as written because the group also cancels master push runs, which
the change, the plan and three guides say never happens.

## Blocker: the guarantee is about in-flight runs, not about push runs

`.github/workflows/main.yml:25`, repeated at `power-gate.yml:37`,
`reproducible.yml:63`, `sim-e2e.yml:66`, `coverage.yml:62`, `android.yml:32`,
`apple.yml:25`, `camera-tests.yml:29`, `protocol-tests.yml:34`,
`pre-review.yml:34`, `host-sanitized.yml:22`, `static-analysis.yml:30`,
`ui-screenshots.yml:38`:

```yaml
group: ${{ github.workflow }}-${{ github.ref }}
cancel-in-progress: ${{ github.event_name == 'pull_request' }}
```

`cancel-in-progress: false` stops GitHub from cancelling a run that is
*running*. It does not protect a run that is *pending*. The default
`queue: single` holds one running and one pending run per group, and the
GitHub docs page "Control the concurrency of workflows and jobs" says: "By
default, any existing `pending` job or workflow in the same concurrency group
will be canceled and the new queued job or workflow will take its place." The
`cancel-in-progress` sentence in the same paragraph only adds cancellation of
the currently running run. So a third run on the same ref replaces the pending
one and is marked `cancelled`.

That is the outcome `plans/392-ci-concurrency.md:100` rules out for master:
a merged commit with no run of its own. The measured size of the exposure, on
`refs/heads/master`, over the 11 hours before the plan's own sample (1000 runs,
`2026-11-26T15:49Z` to `2026-11-27T02:50Z`):

| Workflow | push runs | would be replaced while pending |
| --- | --- | --- |
| PlatformIO CI | 18 | 7 |
| Reproducible firmware builds | 11 | 3 |
| Simulator end-to-end | 11 | 2 |
| Power model gate | 19 | 1 |
| Coverage floor | 11 | 1 |

14 runs is a lower bound. The model gives a run the group from `created_at` to
`updated_at`, allows one running plus one pending run, and counts only runs
that have already finished. Runs that were still pending at the end of the
window are not counted at all.

The reason this is structural rather than bad luck is that the group occupancy
is longer than the merge cadence. `PlatformIO CI` on master is median 26
minutes and max 83 minutes from creation to finish; the gap between master
pushes is median 18 minutes and minimum 5 minutes. So
`plans/392-ci-concurrency.md:133`, "Pushes to master are merges and are spaced
out", is not true for this repository.

Two more surfaces of the same slot:

- `android.yml:32`: the weekly `schedule` run resolves to `refs/heads/master`,
  the same group as an Android master push, so either can replace the other
  while pending.
- `protocol-tests.yml:34`: the only unfiltered `push`, so feature-branch pushes
  share `refs/heads/<branch>` with a `workflow_dispatch` on that branch.

Suggested group, which keeps one constant group per pull request merge ref and
gives every non-PR event its own slot:

```yaml
group: ${{ github.workflow }}-${{ github.ref }}-${{ github.event_name == 'pull_request' && 'shared' || github.run_id }}
cancel-in-progress: ${{ github.event_name == 'pull_request' }}
```

`github.run_id` is in the `github` context, which `concurrency` allows.
`queue: max` is not a fix here: it cannot be combined with a `cancel-in-progress`
that evaluates true, and GitHub rejects the workflow at validation.

If serializing master pushes is actually wanted, that is a separate decision and
the "never cancelled" claims below have to change with it.

## Must fix: the contract is stated wrong in four places

- `AGENTS.md:48` "push, release tags and manual dispatch always finish"
- `CLAUDE.md:102` "push to master, release tags and dispatch finish"
- `CONTRIBUTING.md:380` "release tags and manual dispatch are never cancelled"
- `plans/392-ci-concurrency.md:132` "Nothing is cancelled, but the second run
  waits"

`plans/392-ci-concurrency.md:135` of the same document states the rule that
falsifies line 132, so the plan contradicts itself and the guides carry the
wrong half of it.

## Must fix: two measurements do not survive their own evidence

- `plans/392-ci-concurrency.md:29`, "Every one of the 16 superseded runs was
  still queued, so none of them had started and cancelling them loses no
  partial work". Its own cited run contradicts this. `45920271327` has 21 jobs,
  20 of them started before the `02:38Z` sample; its 14 `build` jobs ran
  `02:03:57Z` to `02:28:32Z`, and the run reached `cancelled` only at
  `02:50:10Z`. All 15 superseded runs I could reconstruct had started jobs.
  Cancelling a superseded pull request run mid-build is the intended behavior,
  so this does not argue against the change, but the sentence is false.
- `plans/392-ci-concurrency.md:27`, "Runner jobs held by those 16 superseded
  runs | 245". 245 is a job count, not occupancy. At the sample only 7 of those
  jobs were on a runner; the rest had completed. I reproduce 243 jobs across 15
  runs, so the count is right and the "held" framing overstates capacity by
  roughly 30x.

Two smaller slips:

- `plans/392-ci-concurrency.md:62`, "Twelve of the thirteen workflows with a
  `pull_request` trigger also run on `push`". It is 8: android, apple,
  camera-tests, coverage, main, power-gate, reproducible, sim-e2e.
  `host-sanitized`, `pre-review`, `static-analysis` and `ui-screenshots` have
  no `push` trigger.
- `plans/392-ci-concurrency.md:86`, `pages.yml` "triggers only on `push` of
  master and `v*` tags". `pages.yml:3-6` triggers on `v*` tags only. This makes
  the exclusion stronger, so it is only a correction.

## Residual risk not recorded anywhere

A "Re-run failed jobs" from the UI re-enters the same group, and on a pull
request that group evaluates `cancel-in-progress` to true, so re-running a stale
run while the current head's run is in progress can cancel the current head's
run. I could not find GitHub documenting this, so it is an inference from the
group model, not a verified fact. It is worth one line in `CONTRIBUTING.md`
telling reviewers to re-run the newest run only.

## Verified correct, do not churn

- Scope. The workflow diff is 91 added lines and 0 removed, 7 per file. Parsing
  each head workflow, dropping only the `concurrency` key and comparing with the
  merge base gives 13 of 13 identical documents, so no path filter, permission,
  job, step or in-job expression changed.
- Group uniqueness. 15 workflow files, 15 distinct `name:` values, still
  distinct case-insensitively (group names are case insensitive). Every ref
  starts with `refs/`, so `<workflow>-<ref>` cannot be split two ways.
  `pages.yml:12`'s literal `pages` group cannot collide with any
  `<workflow>-refs/...` string.
- push versus pull_request. `refs/heads/<branch>` and `refs/pull/<n>/merge` do
  not collide. Confirmed live: the `Companion protocol tests` push run for
  superseded head `b48979e7` finished `success` while the twelve pull request
  runs for that same head were `cancelled`.
- Stacked PRs. `refs/pull/<B>/merge` never contains the base branch, so a stack
  and its base never share a group. A push to A's branch that also fires a
  `synchronize` on B lands in B's group and supersedes only B's own run.
- Manual dispatch. `refs/heads/<branch>` with `cancel-in-progress` false, so it
  cannot cancel a pull request run and cannot be cancelled by one. It is still
  exposed to the pending slot above.
- Exclusions. `release.yml` has no `concurrency` key and `release: published` is
  its only trigger. `pages.yml` keeps `group: pages` with
  `cancel-in-progress: false`. Neither file is edited.
- No consumer. No `workflow_run`, no `pull_request_target`, no `workflow_call`
  and no `uses:` of a local workflow anywhere in `.github/workflows`, so nothing
  waits on a run this change can cancel.
- Fail closed. `tools/check_review_threads.py:100` accepts only `SUCCESS`,
  `NEUTRAL` and `SKIPPED`, so a `cancelled` check can never count as a pass.
- Numbers that re-derive exactly: 18 open pull requests; 286 `pull_request` and
  14 `push` in the 300 runs before `02:38Z`; the largest group `PlatformIO CI`
  on `refs/pull/545/merge` holding exactly `45925781507`, `45922116146`,
  `45920993837`, `45920271327`; `PlatformIO CI` at 21 jobs and 9 for a path
  filtered run. My 84 active runs, 9 multi-run groups and 15 superseded runs
  against the plan's 87, 10 and 16 is snapshot drift, not a discrepancy.

## Gates re-run at 966c3b97

- `python3 tools/check_ci_workflows.py`: passes for 13 stacked-PR and
  manual-dispatch workflows.
- `python3 -m unittest tests.test_check_ci_workflows`: 29 tests, OK.
- `python3 tools/pre_review.py --base origin/master`: 13 of 13 gates pass.

## On the recorded gap

Not a merge blocker by itself. The follow-up should carry the expression it
protects, because the group string is now load-bearing. Extend
`tools/check_ci_workflows.py` with a concurrency lint that asserts a top level
`concurrency` on every workflow with a `pull_request` trigger, asserts the exact
`group` and `cancel-in-progress` values including the non-PR discriminator, so a
later edit cannot silently reintroduce the master push hazard, forbids `queue:`
next to a `cancel-in-progress` that can evaluate true, and exempts `release.yml`
and `pages.yml`. The plan's cost note is fair: `tests/test_check_ci_workflows.py`
loads the real files and builds about twenty synthetic fixtures, so each fixture
either grows the block or the new lint is scoped to the real tree.

## Resolution

Re-checked against head `847449be` plus this revision, rebased onto
`origin/master` `55987a32`, which carries the plan renumbering from #538. The
review was written against `789dd78c`. The line references in the findings above
point at `966c3b97`, the commit reviewed. The fixes below moved
them, and git history keeps that commit.

| Finding | State | Fix |
| :--- | :--- | :--- |
| Blocker, the group key also cancels push runs | fixed | The group is now `<workflow>-<ref>-<discriminator>` with `${{ github.event_name == 'pull_request' && 'shared' || github.run_id }}` as the last component, in all 13 workflows. A `pull_request` run evaluates it to `shared` and shares one slot per merge ref, so two pushes to the same pull request still supersede. Every other event evaluates it to its own `github.run_id`, so a push, schedule or dispatch run cannot cancel, cannot be cancelled, and cannot replace a pending run of any event. `queue: max` was not used, as the review advised. The reviewed behaviour, master pushes replaced while pending, is now unreachable: no two non-PR runs share a group to compete for the pending slot. The discriminator is the documented fallback idiom: a concurrency expression may use the `github` context, and the same docs page builds a group as `${{ github.head_ref || github.run_id }}` because the run ID is "guaranteed to be both unique and defined for the run". |
| The four prose claims contradicted the plan | fixed | `AGENTS.md`, `CLAUDE.md` and `CONTRIBUTING.md` now state the mechanism instead of the outcome alone: the run key gives every non-PR event a private slot because `queue: single` replaces a pending run whatever `cancel-in-progress` says. The plan's behaviour section no longer says a master push serializes. The contradiction is gone rather than reworded. |
| `245 runner jobs held` was a job count | fixed | The table splits the two facts: 245 jobs defined by the 16 superseded runs, 7 of them on a runner at the sample. The paragraph says the rest had finished, cites the review's 15 run reconstruction (243 jobs) and drops the capacity framing. |
| Every superseded run was still queued and lost no partial work | fixed | Replaced with the evidence: run `45920271327` started 20 of its 21 jobs, its 14 `build` jobs ran `02:03:57Z` to `02:28:32Z`, and it was cancelled at `02:50:10Z`. The plan says superseded pull request work does burn runner time, that cancelling it mid-build is intended, and that every reconstructed superseded run had started jobs. |
| Twelve of thirteen workflows run on push | fixed | Corrected to 8 and the eight are listed: android, apple, camera-tests, coverage, main, power-gate, reproducible, sim-e2e. `host-sanitized`, `pre-review`, `static-analysis` and `ui-screenshots` have no `push` trigger. |
| `pages.yml` also runs on master pushes | fixed | Corrected to `v*` tags only. The exclusion is stronger than the plan said, and the plan now says so. |
| Residual risk of a UI re-run, unrecorded | recorded | `CONTRIBUTING.md` gains the line the review asked for: re-run the newest run only, because a re-run re-enters its group and re-running an older pull request run can cancel the current head. The plan repeats it under behaviour changes and still marks it an inference from the group model, not a documented fact. |
| The concurrency contract was not machine checked | fixed | `tools/check_ci_workflows.py` gains `_lint_concurrency`. It requires a top level `concurrency` mapping on every workflow with a `pull_request` trigger, asserts the exact `group` and `cancel-in-progress` strings from the constants `CONCURRENCY_GROUP` and `CONCURRENCY_CANCEL_IN_PROGRESS`, rejects `queue` beside any `cancel-in-progress` that is not the literal `false`, and returns no pull request finding for `release.yml` and `pages.yml`. `tests/test_check_ci_workflows.py` gains six tests: the contract passes, a missing block fails, the reviewed group shape fails, `queue` beside a cancellable run fails, `queue` beside `false` passes, and the two exempt real files pass. Three existing fixtures grew the block so their exact error lists still hold. |

The reviewed facts the review marked correct were not churned: the workflow diff
is still insertions only, the stacked pull request ref argument, the group
disjointness argument, the exclusions and the fail-closed reading of
`tools/check_review_threads.py` are unchanged.

## Gates at this revision

- `python3 tools/check_ci_workflows.py`: passed for 13 stacked-PR and
  manual-dispatch workflows.
- `python3 -m unittest tests.test_check_ci_workflows`: 35 tests, OK.
- `python3 tools/pre_review.py --base origin/master`: 13 of 13 gates pass.
- `actionlint` (not a repository gate): the same 7 pre-existing `SC2086` info
  findings as the base, none on a line this revision adds.
