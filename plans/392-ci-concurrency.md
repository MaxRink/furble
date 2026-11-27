# 392 - pull request concurrency cancellation

Status: implemented in this PR, branched from master `c8159ee9`. Thirteen of the
fifteen workflow files under `.github/workflows` gain a workflow level
`concurrency` block. The two that do not are `release.yml`, the only workflow
with no `pull_request` trigger and no group of its own, and `pages.yml`, which
already carried one. No path filter, permission, job, step or expression inside
a job changes: the workflow diff is 91 added lines, seven per file, and no
deleted line.

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
| Runner jobs held by those 16 superseded runs | 245 |

Every one of the 16 superseded runs was still queued, so none of them had
started and cancelling them loses no partial work. One `PlatformIO CI` run is 21
runner jobs; a path filtered run of the same workflow is 9. The largest single
group was `PlatformIO CI` on `refs/pull/545/merge`, which held four queued runs
(45920271327, 45920993837, 45922116146, 45925781507), three of them superseded
by the fourth.

A manual cancel of a batch freed runner capacity immediately. The change below
makes that automatic and scoped to one superseded run set.

## Exact expressions

Every pull request workflow now carries the same top level block, placed after
`permissions:` and before the next top level key:

```yaml
concurrency:
  group: ${{ github.workflow }}-${{ github.ref }}
  cancel-in-progress: ${{ github.event_name == 'pull_request' }}
```

`cancel-in-progress` is an expression rather than a literal, so one block is
safe for workflows that also run on push, on a schedule, or on dispatch:

| Event | `github.ref` | `cancel-in-progress` | Effect |
| --- | --- | --- | --- |
| `pull_request` | `refs/pull/<number>/merge` | `true` | A newer run on the same pull request cancels the older one |
| `push` to master or `combined` | `refs/heads/<branch>` | `false` | No in-flight run is cancelled |
| `push` to a feature branch (`protocol-tests.yml` only) | `refs/heads/<branch>` | `false` | No in-flight run is cancelled |
| `workflow_dispatch` on a branch | `refs/heads/<branch>` | `false` | No in-flight run is cancelled |
| `schedule` (`android.yml`) | `refs/heads/master` | `false` | No in-flight run is cancelled |
| `release` | not applicable | not applicable | `release.yml` has no `pull_request` trigger and is untouched |

Twelve of the thirteen workflows with a `pull_request` trigger also run on
`push` with a branch filter of `master`, plus `combined` for `main.yml` and
`power-gate.yml`. `protocol-tests.yml` is the exception: its `push` trigger has
no branch filter, so a feature branch push keeps its own `refs/heads/<branch>`
group. That group never collides with the `refs/pull/<number>/merge` group of
the same branch's pull request, and neither cancels the other. This change adds
no push trigger and removes none.

## Why push and release are excluded

A push to master is the merge of an already reviewed pull request, and its run
is the record that master is green at that commit. A later master push is a
different commit, so cancelling the in-flight run would leave the merged commit
with no verdict that any later run reproduces, because that run tests a tree the
merged commit does not describe. The `release` event is stricter still: it fires
once per published tag, so a cancelled release run is never retried by any later
event and the tag would end up without its build. `cancel-in-progress` is
therefore true only for `pull_request` and false for every other event in these
workflows, which covers master, `combined`, `workflow_dispatch` and the
`android.yml` schedule.

`release.yml` triggers on `release: published` only. It has no `pull_request`
trigger, so this change adds nothing to it and it is not otherwise edited.
`pages.yml` already carried `concurrency: {group: pages, cancel-in-progress:
false}`, triggers only on `push` of master and `v*` tags, and is also not
edited: its own group already keeps a tag publish from being cancelled.

## Stacked pull requests keep working

A stacked pull request B targets the head branch of pull request A. For a
`pull_request` event, GitHub sets `github.ref` to the merge ref of the pull
request that fired the event, `refs/pull/<B number>/merge`, and the base branch
name appears nowhere in that ref. A's runs carry `refs/pull/<A number>/merge`.
The group is `<workflow name>-<that ref>`, so:

* A and B are in different groups even though B's commits contain A's, and even
  though both descend from the same base branch. Neither can cancel the other.
* A push to A's branch starts runs for A only. B is untouched.
* When B is rebased onto a new A, GitHub rebuilds B's merge ref and starts a new
  run inside B's group. That cancels only B's previous run, which is the stale
  one this change exists to remove.
* A manual dispatch on A's branch uses `refs/heads/<branch>`, a third distinct
  key, so it never collides with either pull request group.

Path filters are unchanged, so a stacked pull request whose diff touches nothing
a workflow filters on still triggers nothing there. Read-only permissions are
unchanged, so fork safety is unchanged: the added block reads event context only
and grants no permission.

## Double cancellation audit

* The group embeds `github.workflow`, which is the workflow's `name:`. All 15
  workflow names in the repository are unique (checked), so the thirteen groups
  are disjoint and a run can only ever be cancelled by another run of the same
  workflow on the same ref.
* `pages.yml` keeps its own `pages` group and has no `pull_request` trigger, so
  the new groups cannot shadow it.
* No job declares its own `concurrency`, so no job opts out of the workflow
  group and none creates a second group.
* No workflow uses `workflow_run` and none uses `pull_request_target`, so no
  workflow consumes another workflow's run, and nothing waits on a run this
  change can cancel.
* `tools/check_review_threads.py` reads the pull request head commit and treats
  only success, neutral and skipped conclusions as a pass. A cancelled check can
  therefore make the gate fail closed for as long as the superseding run has not
  reported that check, but it can never count as a pass.

## Behavior changes besides cancellation

* On master, `combined`, dispatch and the weekly schedule, two runs of the same
  workflow now serialize instead of overlapping. Nothing is cancelled, but the
  second run waits. Pushes to master are merges and are spaced out; a weekly
  `android.yml` run can queue behind a master push.
* GitHub keeps at most one pending run per group, so a third run in a
  non-cancelling group replaces a run that has not started yet. No in-flight run
  is touched.

## Gates

* `python3 tools/check_ci_workflows.py`: passed for 13 stacked-PR and
  manual-dispatch workflows.
* `python3 -m unittest tests.test_check_ci_workflows`: 29 tests, OK.
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
  `AGENTS.md`, `CLAUDE.md` and `CONTRIBUTING.md`, and this PR adds the
  concurrency rule there.
* `.github/workflows/CLAUDE.md` does not exist in this repository, at this commit
  or on `origin/master`, so the workflow directory rules were read from
  `AGENTS.md`, `CLAUDE.md` and `CONTRIBUTING.md`.

## Follow-up

The concurrency contract is not machine checked yet.
`tools/check_ci_workflows.py` enforces the path filter, the absence of a base
branch filter, the `workflow_dispatch` requirement and the read-only permission
rule, but it does not know about `concurrency`. Adding that rule needs the
roughly twenty synthetic workflow fixtures in `tests/test_check_ci_workflows.py`
to grow a concurrency block each, so it is left to its own change rather than
mixed into this one.
