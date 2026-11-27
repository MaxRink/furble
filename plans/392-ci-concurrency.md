# 392 - pull request concurrency cancellation

Status: implemented in this PR, branched from master `789dd78c` and rebased onto
`55987a32` (#538, the plan renumbering). Thirteen of the
fifteen workflow files under `.github/workflows` gain a workflow level
`concurrency` block. The two that do not are `release.yml`, the only workflow
with no `pull_request` trigger and no group of its own, and `pages.yml`, which
already carried one. No path filter, permission, job, step or expression inside
a job changes.

The first revision keyed the group on the workflow and the ref alone. Review
showed that shape still cancels push runs, so the group now carries a run key
for every event that is not a pull request. The corrections are listed at the
end.

## Problem

A pull request run set is recreated on every head push and nothing retires the
previous set. The superseded set keeps its place in the runner queue, so a
docs-only pull request waits behind work that can never be the verdict for the
current head.

Sampled read-only with `gh` at 2026-11-27T02:38Z:

| Measurement | Value |
| --- | --- |
| Open pull requests | 18 |
| Events in the last 300 runs | 286 `pull_request`, 14 `push` |
| Active runs at the sample | 87 (71 queued, 16 in progress) |
| Groups holding more than one active run | 10 |
| Superseded active runs inside those groups | 16 |
| Runner jobs defined by those 16 superseded runs | 245 |
| Of those jobs, still on a runner at the sample | 7 |

245 is a job count, not occupancy. The 16 superseded runs defined 245 jobs, and
only 7 of them were running at the sample; the rest had already finished. The
review reconstructed 15 of those runs and counted 243 jobs across them, and every
one of the 15 had started jobs. Superseded pull request work therefore does burn
runner time, which is the reason for the change: run 45920271327 started 20 of
its 21 jobs, its 14 `build` jobs ran from 02:03:57Z to 02:28:32Z, and the run was
cancelled at 02:50:10Z. Cancelling a superseded pull request run mid-build is the
intended behavior. What the change removes is the rest of that run and the queue
slot it holds, not the part that already ran.

One `PlatformIO CI` run is 21 runner jobs; a path filtered run of the same
workflow is 9. The largest single group was `PlatformIO CI` on
`refs/pull/545/merge`, which held four queued runs (45920271327, 45920993837,
45922116146, 45925781507), three of them superseded by the fourth. A manual
cancel of a batch freed runner capacity immediately; this makes that automatic.

## Why `cancel-in-progress` alone does not protect a push run

`cancel-in-progress` decides what happens to a run that is already **running**.
The default `queue: single` holds one running and one pending run per group, and
GitHub replaces an existing **pending** run in the same group when a new run is
queued, independently of `cancel-in-progress`. So a group of
`<workflow>-<ref>` cancels the third run on a ref whichever event started it.

That is not hypothetical for master. Over the 1000 runs from 2026-11-26T15:49Z to
2026-11-27T02:50Z, modelling the group from `created_at` to `updated_at` with one
running and one pending slot and counting only finished runs:

| Workflow | `push` runs | Would be replaced while pending |
| --- | --- | --- |
| PlatformIO CI | 18 | 7 |
| Reproducible firmware builds | 11 | 3 |
| Simulator end-to-end | 11 | 2 |
| Power model gate | 19 | 1 |
| Coverage floor | 11 | 1 |

14 runs is a lower bound: runs still pending at the end of the window are not
counted at all. The exposure is structural rather than bad luck because
occupancy outlasts the merge cadence. `PlatformIO CI` on master takes a median 26
minutes and at most 83 minutes from creation to finish, while the gap between
master pushes is a median 18 minutes and at least 5 minutes. Two more surfaces
share the same slot: the weekly `android.yml` schedule resolves to
`refs/heads/master`, the same ref as an Android master push, and
`protocol-tests.yml` is the only unfiltered `push`, so a feature branch push
shares `refs/heads/<branch>` with a `workflow_dispatch` of that branch.

GitHub's own docs state both halves. For the pending slot: "Any existing
`pending` job or workflow in the same concurrency group, if it exists, will be
canceled and the new queued job or workflow will take its place." For the
combination: "The combination of `queue: max` and `cancel-in-progress: true` is
not allowed and will result in a workflow validation error." So `queue: max` is
not available here either.

The discriminator is the documented fallback idiom. A workflow level
`concurrency` expression may use the `github`, `inputs` and `vars` contexts, and
the same docs page builds a group as `${{ github.head_ref || github.run_id }}`
because the run ID "is guaranteed to be both unique and defined for the run".
This change applies that fallback to the event name rather than to `head_ref`, so
the constant branch covers pull requests and every other event falls back.

## Exact expressions

Every pull request workflow carries the same top level block, placed after
`permissions:` and before the next top level key:

```yaml
concurrency:
  group: ${{ github.workflow }}-${{ github.ref }}-${{ github.event_name == 'pull_request' && 'shared' || github.run_id }}
  cancel-in-progress: ${{ github.event_name == 'pull_request' }}
```

The third component is the discriminator. A `pull_request` event evaluates it to
the literal `shared`, so all runs of one workflow on one merge ref share a group
and a newer head cancels the run it supersedes. Every other event evaluates it to
`github.run_id`, which is unique to the run, so push, tag, schedule and dispatch
runs each get a private slot. A private slot can neither cancel nor replace the
pending run of another event, and nothing can replace it.

| Event | `github.ref` | Group suffix | Effect |
| --- | --- | --- | --- |
| `pull_request` | `refs/pull/<number>/merge` | `shared` | A newer run on the same pull request cancels the older one |
| `push` to master or `combined` | `refs/heads/<branch>` | `github.run_id` | Own slot; never cancelled and never replaced |
| `push` to a feature branch (`protocol-tests.yml` only) | `refs/heads/<branch>` | `github.run_id` | Own slot; never cancelled and never replaced |
| `workflow_dispatch` on a branch | `refs/heads/<branch>` | `github.run_id` | Own slot; never cancelled and never replaced |
| `schedule` (`android.yml`) | `refs/heads/master` | `github.run_id` | Own slot; cannot collide with a master push |
| `release` | not applicable | not applicable | `release.yml` has no `pull_request` trigger and is untouched |

## Why push and release are excluded

A push to master run is the record that master is green at the merged commit, and
the next master push tests a different tree. A release run is the only run a
published tag will ever get, so losing it leaves the tag without its build. Both
events therefore keep a private group: they cannot cancel anything, cannot be
cancelled by the newer run of any event, and cannot be replaced while pending.

`release.yml` triggers on `release: published` only. It has no `pull_request`
trigger, so this change adds nothing to it and it is not otherwise edited.
`pages.yml` already carried `concurrency: {group: pages, cancel-in-progress:
false}`, triggers only on `push` of `v*` tags, and is also not edited: its own
literal group already isolates a tag publish.

## Stacked pull requests keep working

A stacked pull request B targets the head branch of pull request A. For a
`pull_request` event, GitHub sets `github.ref` to the merge ref of the pull
request that fired the event, `refs/pull/<B number>/merge`, and the base branch
name appears nowhere in that ref. A's runs carry `refs/pull/<A number>/merge`.
The group is `<workflow name>-<that ref>-shared`, so:

* A and B are in different groups even though B's commits contain A's, and even
  though both descend from the same base branch. Neither can cancel the other.
* A push to A's branch starts runs for A only. B is untouched.
* When B is rebased onto a new A, GitHub rebuilds B's merge ref and starts a new
  run inside B's group. That cancels only B's previous run, which is the stale
  one this change exists to remove.
* A manual dispatch on A's branch keys its group on `github.run_id`, so it
  belongs to the branch alone and never joins a pull request group.

Two pushes to the same pull request still supersede: both evaluate the
discriminator to `shared`, both carry the same merge ref, and the newer run
cancels the older one. Path filters are unchanged, so a stacked pull request
whose diff touches nothing a workflow filters on still triggers nothing there.
Permissions stay `contents: read`, so fork safety is unchanged.

## Double cancellation audit

* The group embeds `github.workflow`, which is the workflow's `name:`. All 15
  workflow names in the repository are unique, still unique case-insensitively,
  so the thirteen groups are disjoint. Every ref starts with `refs/`, so a
  `<workflow>-<ref>-<suffix>` string cannot be split two ways.
* The group also embeds `github.run_id` for every event that is not a
  `pull_request`, so a push, tag, schedule or dispatch run has a group of its own
  for its whole life. It cannot cancel a pull request run, cannot be cancelled by
  one, and cannot replace a pending run of any event.
* `pages.yml` keeps its literal `pages` group, which no
  `<workflow>-refs/...-<suffix>` string can equal, and has no `pull_request`
  trigger, so the new groups cannot shadow it.
* No job declares its own `concurrency`, so no job opts out of the workflow group
  and none creates a second group.
* No workflow uses `workflow_run`, `pull_request_target` or `workflow_call`, and
  none uses `uses:` of a local workflow, so no workflow consumes another
  workflow's run and nothing waits on a run this change can cancel.
* `tools/check_review_threads.py` passes only success, neutral and skipped
  conclusions. A cancelled check can make the gate fail closed until the
  superseding run reports that check, but it can never count as a pass.

## Behavior changes besides cancellation

* A superseded pull request run stops, including a run that is mid-build. That is
  the point of the change and the only place GitHub cancels anything.
* Push, tag, schedule and dispatch runs keep their previous behavior and no
  longer share a queue slot with each other, so a master push is neither
  cancelled nor held behind an Android schedule run.
* A "Re-run failed jobs" from the UI re-enters the group of the run it re-runs.
  On a pull request that group is shared and cancellable, so re-running an old
  run while the current head runs can cancel the current head. Re-run the newest
  run only; this is recorded in `CONTRIBUTING.md`.

## Machine check

`tools/check_ci_workflows.py` now enforces the block, so a later edit cannot
quietly reintroduce the master push hazard:

* every workflow with a `pull_request` trigger must carry a top level
  `concurrency` mapping;
* `group` must equal `${{ github.workflow }}-${{ github.ref }}-${{
  github.event_name == 'pull_request' && 'shared' || github.run_id }}`, which
  is the exact string in the workflows, discriminator included;
* `cancel-in-progress` must equal `${{ github.event_name == 'pull_request' }}`;
* `queue` is rejected next to any `cancel-in-progress` that is not the literal
  `false`, because GitHub refuses that combination;
* `release.yml` and `pages.yml` are exempt, since neither has a `pull_request`
  trigger. `tests/test_check_ci_workflows.py` asserts that exemption on the real
  files.

## Gates

* `python3 tools/check_ci_workflows.py`: passed for 13 stacked-PR and
  manual-dispatch workflows.
* `python3 -m unittest tests.test_check_ci_workflows`: 35 tests, OK.
* `python3 tools/pre_review.py --base origin/master`: 13 of 13 gates pass,
  including whitespace, diff hygiene, clang-format, C++ safety, the CI workflow
  contract and the documentation citation gate.
* `actionlint` is not a repository gate. Run locally, it reports 7 pre-existing
  `SC2086` info findings in `release.yml` and `static-analysis.yml` run scripts.
  The same 7 appear on the base workflows, none is in a line this change adds,
  and none is on a `concurrency` block. `shellcheck` reaches the workflows only
  through `actionlint`; there is no separate shellcheck gate.

## Not changed

* `docs/ci-lock.md` describes GitHub Action SHA provenance only, and
  `docs/README.md` indexes it as "Pinned action versions". It does not describe
  the trigger contract, so it is unchanged. The trigger contract lives in
  `AGENTS.md`, `CLAUDE.md` and `CONTRIBUTING.md`, and this PR states the
  concurrency rule there.
* `.github/workflows/CLAUDE.md` does not exist in this repository, at this commit
  or on `origin/master`, so the workflow directory rules were read from
  `AGENTS.md`, `CLAUDE.md` and `CONTRIBUTING.md`.

## Corrections in this revision

Recorded in `plans/553-review-feedback.md` with the evidence for each:

* The group keyed on the ref alone still cancelled master push runs, because the
  pending slot is replaced regardless of `cancel-in-progress`. The discriminator
  above fixes it. This was a merge blocker.
* The claim that the 16 superseded runs were all queued and lost no partial work
  was false; run 45920271327 had started 20 of its 21 jobs, and every superseded
  run the review reconstructed had started jobs.
* The 245 figure is a job count, not occupancy; 7 of those jobs were on a runner
  at the sample.
* Twelve of thirteen workflows run on push was wrong; it is 8: android, apple,
  camera-tests, coverage, main, power-gate, reproducible and sim-e2e. The other
  four have no `push` trigger.
* `pages.yml` triggers on `v*` tags only, not on master pushes.
* The four prose files promised push is never cancelled while this plan said a
  master push serializes. With the discriminator both statements agree: push runs
  are never cancelled and are never held behind another event.
