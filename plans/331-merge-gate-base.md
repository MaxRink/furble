# 331 - Merge gate reads workflow filters at the PR base

Status: implemented on `fix/464-base-workflows`, closing issue #464. Every
result below is a local run on that branch. No CI run is claimed.

## Defect

`tools/check_review_threads.py --allow-no-checks REASON` lets a head with no
check runs pass, but only when no pull-request workflow would have run on the
changed paths. It decided that from the local checkout at `4af87727`:

- `WORKFLOW_DIR = Path(__file__).resolve().parents[1] / ".github" / "workflows"`
  pinned the directory to the checkout (`:144`).
- `covering_workflows` globbed that directory (`:204`) and `main` called it with
  no ref (`:356`).
- `baseRefOid` appeared nowhere in `tools/` or `tests/`.

So the gate judged a pull request against whatever tree happened to be on
disk. When the clone was older than the base, a filter that master had added or
broadened was invisible, and the gate accepted a head whose workflow should
have run. The #450 review summary raised this on `474cb89f` without a thread,
and it became issue #464.

The guard added for #450 review item 11 refused any PR that changed a file
under `.github/workflows/`. That guard reasoned about the PR's own branch copy.
It never addressed a stale checkout of the base, which is the actual defect.

Reproduced on this branch before the change, with a base workflow whose
`paths` covers `web-installer/**`, changed files `["web-installer/index.html"]`
and a checkout whose filters cover nothing:

```
old gate version: tools/check_review_threads.py at 4af87727
WORKFLOW_DIR: /private/tmp/furble-464/.github/workflows
changed files: ['web-installer/index.html']
filters in the checkout: covering=[]
filters at the base commit: covering=['main.yml']
gh api requests the old gate made:
  gh api graphql review-thread query
  gh api graphql checks query
  gh api --paginate repos/MaxRink/furble/pulls/464/files --jq .[].filename
exit=0
stdout: review threads: 1 total, all resolved on MaxRink/furble#464
checks: none on head 0123456789ab, and no workflow covers the changed paths; accepted: installer only
```

The base covered the changed file, the checkout did not, and the gate accepted
the override.

## Design

- `run_base_ref_oid` reads `baseRefOid` from a small GraphQL query on the pull
  request. A missing field, a non-string or a value that is not 40 or 64 hex
  characters raises `ReviewQueryError`.
- `fetch_workflows(owner, name, ref)` lists
  `repos/{owner}/{repo}/contents/.github/workflows?ref=<ref>` and then reads
  every `.yml` and `.yaml` file through
  `repos/{owner}/{repo}/contents/<path>?ref=<ref>`. Each file comes back as
  base64 text, which the function decodes. The directory listing itself carries
  no `content` field, so it cannot supply the texts in one call.
- The gate wires those two together and passes the texts into
  `covering_workflows(changed, workflows=workflows)`. When texts are supplied,
  that function reads no directory at all.
- The #450 item 11 guard is gone. A pull request cannot edit the commit it
  targets, so an edit to its branch copy of a workflow no longer changes the
  decision and no longer has to disqualify the override. A PR that deletes a
  workflow on its branch now fails closed whenever the base filters covered the
  paths that workflow would have checked.
- Local mode is kept and named: `covering_workflows` with no texts calls
  `read_local_workflows(workflow_dir)` and reads `.github/workflows` from the
  checkout. It is a test-only fallback. `--pr` is mandatory, so the gate cannot
  reach it, and the CLI has no flag that selects it. The gate prints the base
  commit it used in both the accepted and the refused line, so the decision is
  traceable.

## Fail-closed cases

Each case exits 2 with `review-thread check failed closed` on stderr:

- `baseRefOid` is missing or malformed (`run_base_ref_oid`).
- `gh` is missing, exits non-zero, or returns invalid JSON (`api_json`).
- The workflow directory listing at the base is not a directory (`fetch_workflows`).
- A contents entry has no name, an unrecognized type, or a `.yml` name that is
  not a file. An unknown type could hide a workflow, so it is never skipped
  (`fetch_workflows`).
- A workflow file cannot be read, or has no base64 text. The contents API sends
  an empty `content` and `encoding: "none"` above 1 MB, so the gate refuses
  instead of treating the file as empty (`fetch_workflows`).
- Base64 decoding fails, or the text is not UTF-8 (`fetch_workflows`).
- A base workflow does not parse as YAML, or is not a mapping
  (`covering_workflows`).
- The changed-file list cannot be fetched (`run_changed_files`, unchanged).

A missing base commit in the API is a real example: reading
`b4b6f0d0c3b52f7da0ab7cf18b2f6a2f62e1c8db` returned

```
gh api the workflow directory at b4b6f0d0c3b5 failed with exit 1: gh: No commit found for the ref b4b6f0d0c3b52f7da0ab7cf18b2f6a2f62e1c8db (HTTP 404)
```

## Evidence

The ref really is honored. Reading `3a7195902823`, the parent of `c0842492`,
returned 9 workflows while this checkout has 15. The six that exist only in the
checkout are `apple.yml`, `coverage.yml`, `host-sanitized.yml`, `pre-review.yml`,
`reproducible.yml` and `static-analysis.yml`. A checkout read cannot produce
that count. Reading the base of #461, `592ec019c5fa...`, returned the 15
workflows at that commit, which matches its tree.

The stale-clone direction is visible in real history. Against the same
`3a7195902823` base:

| Changed path | Filters in this checkout | Filters at that base |
| --- | --- | --- |
| `web-installer/index.html` | none | `main.yml` |
| `tools/check_pr_hygiene.py` | `main.yml`, `pre-review.yml` | `main.yml` |
| `CMakeLists.txt` | `main.yml`, `reproducible.yml` | `main.yml` |
| `src/FurbleUI.cpp` | 9 workflows | `main.yml`, `power-gate.yml`, `sim-e2e.yml`, `ui-screenshots.yml` |

The first row is the defect with historical data: the old gate accepted a
`web-installer/**` PR whose base `main.yml` covers that path.

## Tests

`python3 -m unittest tests.test_check_review_threads` runs 40 tests, all
offline. Before the change the new suite reported 20 errors, because the fetch
seam did not exist. The new and changed tests:

- `test_override_is_refused_when_the_base_workflow_covers_a_changed_path`
  covers issue #464's acceptance. It uses a synthetic clone copy of `main.yml`
  that covers nothing for `web-installer/index.html` and a synthetic base copy
  that does, then requires exit 1. Neither side reads the checkout's workflow
  set, so the test does not carry the coupling this plan removes.
- `test_gate_fetches_the_workflows_at_the_base_ref` binds the seam: it requires
  one `fetch_workflows` call with the oid the payload returned.
- `test_override_holds_when_the_pr_broadens_its_own_workflow_copy` replaces
  `test_no_checks_override_is_refused_when_the_pr_edits_a_workflow`. The old
  test encoded the local-tree rule. The base commit cannot be edited by the PR,
  so the branch copy no longer decides and the override holds.
- `test_missing_base_oid_fails_closed` requires exit 2 and proves the workflow
  fetch is never reached.
- `test_workflow_fetch_error_fails_closed` and
  `test_unreadable_base_workflow_fails_closed` cover an API error and a base
  workflow that does not parse.
- `test_non_object_graphql_payload_fails_closed` covers a payload that is not a
  JSON object, so it cannot reach `.get` as an `AttributeError`.
- `test_local_mode_reads_the_checkout_when_no_texts_are_given` and
  `test_supplied_texts_replace_the_checkout_entirely` pin the two modes.
- `run_override` patches `read_local_workflows` to raise, so every override test
  fails if the gate touches the checkout. That is the regression guard for the
  defect itself.
- `BaseRefOidTest` and `BaseWorkflowFetchTest` cover the payload field, the ref
  in every request, the base64 decode, a directory entry, a workflow entry with
  no type, an unknown type, a `.yml` name that is not a file, a truncated file,
  an API error, a non-directory listing and a missing `gh`.

The network is behind one seam, `fetch_workflows`, which the gate tests stub.
`pre_review` stays offline and still passes.

## Documentation

`AGENTS.md`, `CLAUDE.md`, `CONTRIBUTING.md` and the tool's module docstring all
describe the base-ref read, the fail-closed cases and the test-only local
fallback. `plans/314-review-feedback.md` row 11 keeps its original text and
carries a dated note that the guard and its test were removed.
