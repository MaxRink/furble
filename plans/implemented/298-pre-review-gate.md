# 298 - pre-review scanner gate

## Motivation

Recent PRs spent review rounds on defects that a scanner could have caught:
an absolute workstation path in a committed baseline, a vector fixture written
past its length, a filesystem sweep that removed an entry without checking its
type, a fixed-width sentinel asserted from one byte, a state machine that
reacquired a lock after the receiver was declared absent, a settings default
without its non-S3 test, and plan/docs claims that drifted from the tree.

The goal is not to replace review. It is to make the mechanical classes fail
before a reviewer spends time on them.

## Tools

| Tool | What it blocks |
| --- | --- |
| `tools/check_pr_hygiene.py` | Absolute workstation paths, whitespace errors, generated artifacts, debug leftovers, behavior changes without their docs and tests, broken plan links |
| `tools/check_settings_contracts.py` | Missing settings defaults, board-conditional branches without host coverage, documentation defaults that drift, wire-ID ledger mismatches, default changes without README/reference/test updates |
| `tools/check_cpp_safety.py` | Local container bounds, filesystem removal without a type filter, low-byte sentinel assertions, terminal-state lock acquisition |
| `tools/run_sanitized_host_tests.sh` | ASan/UBSan failures in a bounded host subset |
| `tools/pre_review.py` | Runs the scanner set, CI workflow contract, simulator manifest, and scanner unit tests |

## CI wiring

- `.github/workflows/pre-review.yml`: path-filtered, read-only pull-request
  job that runs the scanner unit tests and `tools/pre_review.py`.
- `.github/workflows/host-sanitized.yml`: path-filtered, read-only job that
  runs the ASan/UBSan subset. It is separate because the instrumented build is
  slower and must not share the normal host cache.

Both workflows expose `workflow_dispatch` so stacked or retargeted branches can
be validated manually without changing their base.

## Review thread closure

The scanners do not replace review. A PR must have zero unresolved review
threads before merge. Every Copilot or human finding must be recorded as fixed,
already fixed, or a false positive with evidence. Resolving a thread without a
fix is not acceptable. A remediation PR that closes review findings follows the
same rule. The online gate is
`python3 tools/check_review_threads.py --repo owner/name --pr N`; run it after
`python3 tools/pre_review.py --base origin/master` and
`sh sim/scripts/check-doc-tokens.sh` pass.

## Deliberate limits

- The scanners are conservative. Unresolved construct shapes are warnings, not
  silent passes.
- `check_cpp_safety.py` models local declarations only; member and parameter
  containers, helper-based filesystem filters, and cross-function state are
  warnings or out of scope.
- `check_settings_contracts.py` resolves scalar defaults only; aggregate
  defaults and generator expressions are named warnings.
- `check_pr_hygiene.py` checks that the required docs/tests were touched, not
  that their new text is accurate. Review still owns wording and intent.
- The sanitized subset is selected by regex, not the whole host suite. Expand
  the regex when a new state-machine or fixture test lands.

## Validation

```sh
python3 -m unittest tests.test_check_pr_hygiene \
  tests.test_check_settings_contracts tests.test_check_cpp_safety
python3 tools/pre_review.py --base origin/master
sh tools/run_sanitized_host_tests.sh --clean
python3 tools/check_ci_workflows.py
python3 tools/check_sim_scenarios.py
```
