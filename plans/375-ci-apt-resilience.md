# Plan 375: CI apt installs survive a broken third-party repository

## Failure

Run 45916366927 on PR 540, job "Simulator power regression" (workflow
`.github/workflows/power-gate.yml`), failed in its apt step with:

```
Err:2 https://packages.microsoft.com/ubuntu/26.04/prod resolute InRelease
  403  Forbidden [IP: 13.107.246.40 443]
E: Failed to fetch https://packages.microsoft.com/ubuntu/26.04/prod/dists/resolute/InRelease
E: The repository packages.microsoft.com/ubuntu/26.04/prod resolute InRelease is no longer signed.
##[error]Process completed with exit code 100
```

The step installs `clang` and `libsdl2-dev`, both from the Ubuntu archive. The
Microsoft repository is baked into the runner image and nothing in the job
uses it. `apt-get update` fails the whole step when any configured source
cannot be fetched, so an unrelated repository killed the job before the
simulator ran one scenario.

This is not hypothetical and not caused by anything in the pull request. Any
lane that runs `sudo apt-get update` was exposed to the same image.

## Why the obvious repairs were rejected

- `sudo apt-get update || true` hides a genuinely broken Ubuntu archive. The
  install that follows would resolve against the package index the image
  cached at build time, which can quietly be older than the compiler the
  workflow names, and the failure then surfaces later as a link error.
- `--fix-missing` and `-o Acquire::AllowInsecureRepositories=true` weaken the
  check for every source, including the archive.
- Pinning `runs-on: ubuntu-24.04` hides the problem until that image also
  changes. The next image brings the same class of breakage.

## Fix

One helper, `tools/ci-apt-install.sh`, owned by `tools/` like the other CI
gates, called from every step that used to run `sudo apt-get update`:

```yaml
- name: Install SDL2 and Clang
  run: sh tools/ci-apt-install.sh clang libsdl2-dev
```

It was written as a `tools/` script rather than a composite action because
this repository has no `.github/actions/` tree, and because a script can be
run by hand, be linted by `shellcheck` and be unit tested with a fake root,
none of which a composite action does here. A composite action would also add
a `uses:` reference that `docs/ci-lock.md` and its SHA pinning rules do not
cover.

What the helper does, in order:

1. Reads every enabled source entry in `/etc/apt/sources.list` and
   `/etc/apt/sources.list.d/*.{list,sources}` (`tools/ci-apt-install.sh:214`).
2. Treats a source as Ubuntu archive when every URI in it has a host under
   `ubuntu.com`. This covers `archive.`, `security.`, `ports.` and the cloud
   mirrors such as `azure.archive.ubuntu.com`. Everything else is third party
   for this purpose: `packages.microsoft.com`, `dl.google.com`,
   `apt.postgresql.org`, `cli.github.com` and Canonical-hosted PPAs on
   `ppa.launchpadcontent.net` (`tools/ci-apt-install.sh:136`).
3. Comments out the offending entries of one-line files, so an archive entry
   that shares the file survives (`tools/ci-apt-install.sh:247`). A deb822
   `.sources` file has the offending file moved out of the apt configuration
   rather than edited, because its stanzas cannot be dropped entry by entry.
4. Fails when no archive source is left, instead of continuing to an install
   that cannot be satisfied (`tools/ci-apt-install.sh:372`).
5. Runs `apt-get update` with no suppression and `Acquire::Retries=3`
   (`tools/ci-apt-install.sh:381`).
6. Runs `apt-get install --no-install-recommends -y` with no suppression.
7. Asks `dpkg-query` whether every named package reports `install ok installed`
   and fails the step when one does not (`tools/ci-apt-install.sh:391`).

The allowlist can be widened with `CI_APT_ARCHIVE_HOSTS`, which is additive
only. Widening it re-enables a fetch that is allowed to fail the step, so the
escape hatch cannot turn into a bypass.

## What the real runs found

The real runs drove two fixes that no local test would have produced. Both
came from the archive check refusing to install against an index it could not
read, which is the behaviour this plan wants.

The first push ran the image and failed the "Simulator power regression" job
with:

```
ci-apt-install: dropped third-party source packages.microsoft.com in /etc/apt/sources.list.d/microsoft-prod.list
ci-apt-install: FAIL: no Ubuntu archive source is enabled under /etc/apt; refusing to install against an empty package index (set CI_APT_ARCHIVE_HOSTS if the archive moved)
ci-apt-install: disabled ubuntu.sources: third-party host(s) mirror+file mirror+file
```

The Microsoft list was dropped as intended. The second line is the guard doing
its job: `ubuntu.sources` on this image does not name a hostname, it carries
`URIs: mirror+file:/etc/apt/apt-mirrors.txt`, so the host a naive reader sees
is the literal string `mirror+file` and the archive looked third party.

Two things changed because of it, and both are tested now:

- `expand_uri` (`tools/ci-apt-install.sh:153`) reads the `mirror+file:` list and
  classifies the hosts it names. A `mirror+` indirection this script cannot
  read, such as a remote list, leaves the source enabled with a warning instead
  of dropping a source the job may need.
- Every diagnostic goes to stderr. The failure line appeared above the line
  that explained it, because the two streams are captured separately.

The second push read the list but reported `third-party host(s) priority
priority priority priority priority priority`: each line of `apt-mirrors.txt` is
a preference followed by the URI, not a bare URI, so the first token was being
taken as a host. The reader now takes the first token that is a URI
(`tools/ci-apt-install.sh:153`), which covers both forms, and every run logs the
mirror list it resolved, so the next image shape is visible in the log instead
of a guessing game.

Without the archive check both runs would have gone on to `apt-get install`
with no index at all, which is the silent-looking failure this plan refuses.

## Per-workflow table

Line numbers are the `run:` line on `origin/master` before this change and on
this branch after it.

| Workflow | Job | Before | After | Packages |
| --- | --- | --- | --- | --- |
| `.github/workflows/power-gate.yml` | `power-gate` ("Simulator power regression") | `power-gate.yml:51` | `.github/workflows/power-gate.yml:53` | `clang libsdl2-dev` |
| `.github/workflows/ui-screenshots.yml` | `ui-screenshots` | `ui-screenshots.yml:51` | `.github/workflows/ui-screenshots.yml:53` | `clang libsdl2-dev xvfb` |
| `.github/workflows/sim-e2e.yml` | `sim-e2e` | `sim-e2e.yml:79` | `.github/workflows/sim-e2e.yml:82` | `clang libsdl2-dev libmosquitto-dev libcjson-dev` |
| `.github/workflows/sim-e2e.yml` | `sim-sanitizers` (sanitizer job) | `sim-e2e.yml:636` | `.github/workflows/sim-e2e.yml:641` | `clang libclang-rt-dev llvm libsdl2-dev` |
| `.github/workflows/coverage.yml` | `coverage` | `coverage.yml:100` | `.github/workflows/coverage.yml:105` | `clang-18 llvm-18 libclang-rt-18-dev libsdl2-dev libmosquitto-dev libcjson-dev` |
| `.github/workflows/static-analysis.yml` | `static-analysis` | `static-analysis.yml:40` | `.github/workflows/static-analysis.yml:43` | `clang-tidy cppcheck cmake ninja-build clang libsdl2-dev` |

Six steps in five workflows, which is every `sudo apt-get update` in
`.github/workflows`. `coverage.yml` keeps its post-install assertions
(`clang-18 -print-file-name`, `pkg-config --exists libmosquitto`, version
prints) inside the same step, after the helper returns.

The helper is also added to the `pull_request` and `push` path filters of all
five workflows, so a change to it re-runs the jobs that depend on it.
`tests/test_sim_ci_contract.py` asserts that the two `sim-e2e.yml` path lists
are exact and identical, so its expected list gained the same entry in this
change.

The firmware jobs (`host_camera`, `python-tools` and the PlatformIO matrix in
`.github/workflows/main.yml`, plus `.github/workflows/reproducible.yml`) have
no apt step, and `.github/workflows/host-sanitized.yml` uses the compiler and
cmake that the image already carries. They are covered by the gate below
rather than by an edit: a future apt step there fails the checker.

## The gate that keeps it fixed

`tools/check_ci_workflows.py` gains `_lint_apt_through_helper`
(`tools/check_ci_workflows.py:220`), which rejects any `apt-get`, `apt` or
`aptitude` update or install in a workflow `run:` block that does not go
through `tools/ci-apt-install.sh`. It runs before the pull-request trigger
check, so a workflow without a `pull_request` trigger is covered too. Comment
lines are exempt, so the reason for the helper can be written next to the
step.

Against `origin/master` the new gate reports 12 findings: 2 in each of
`coverage.yml`, `static-analysis.yml`, `power-gate.yml` and
`ui-screenshots.yml`, and 4 in `sim-e2e.yml` (two jobs, update plus install
each). Against this branch it reports none.

## Which gate catches a genuinely missing package

Three, at different levels:

1. `apt-get install` itself, with no `|| true` above it. A package that is not
   in the archive makes apt exit 100 and the helper re-exits non-zero.
2. `dpkg-query` after the install, which fails the step even if apt exits 0
   without the package being configured (`tools/ci-apt-install.sh:391`).
3. `tests/test_ci_apt_install.py` pins both, plus the broken-archive case, so
   a future edit to the helper that starts suppressing failures fails the test
   job instead of quietly weakening every workflow:
   - `test_missing_package_fails_the_step`
   - `test_package_dpkg_does_not_report_installed_fails_the_step`
   - `test_broken_archive_fails_the_step`
   - `test_runner_with_no_archive_source_fails_before_any_update`
   - `test_no_apt_line_suppresses_its_exit_status`

   Four more pin the indirection the real image uses:
   `test_mirror_file_indirection_is_read`, which carries the `priority:1` line
   the 26.04 image writes, `test_unreadable_mirror_indirection_is_left_enabled`,
   `test_remote_mirror_list_is_left_enabled` and
   `test_mirror_list_without_a_readable_uri_is_left_enabled`.

The behaviour change is exactly this: a third-party source can no longer fail
a step, and a package the step names still does.

## Other setup steps audited

The same class of flake is "a repository the step does not need, configured
globally on the runner, that fails the step". Each provider was checked for
that shape, not for transient network failure, which retries already cover.

| Provider | Where | Real exposure | Change |
| --- | --- | --- | --- |
| pip | `python -m pip install -r requirements.txt` in 6 workflows, `-r .github/workflows/*-requirements.txt` in 3 | None. `requirements.txt` names `platformio==6.1.19` and `pyserial==3.5`. No `pip.conf`, no `PIP_EXTRA_INDEX_URL`, no `--index-url` or `--extra-index-url` anywhere in the repository, so pip talks only to PyPI, which every step needs. The runner can set no global source that the job does not use. | None, and `|| true` would be the only alternative, which is what this plan rejects. |
| npm | none | No `npm`, `yarn` or `npx` invocation in any workflow, so there is no registry to be broken. | None. |
| PlatformIO | `platformio run` in `main.yml`, `release.yml`, `pages.yml`, `reproducible.yml`, `static-analysis.yml` | The registry, GitHub and Espressif are the sources the build needs. `platformio.ini` sets no `board_manager.additional_urls` and no extra package index. A broken registry must fail the build. | None. |
| Homebrew | none | No `brew` invocation in any workflow. `apple.yml` runs `sudo xcode-select` and `swift test`, and the Swift package dependencies are the ones the package needs. | None. |
| Gradle | `android.yml` | `companion/android/settings.gradle.kts` lists `google()`, `mavenCentral()` and `gradlePluginPortal()`, all needed to resolve the Android plugin and its dependencies. | None. |
| sdkmanager | `android.yml` | `sdkmanager "platforms;android-37.0" "build-tools;37.0.0"` talks to the Android SDK repository the job needs. | None. |

Only apt had an unrelated repository configured on the image, so only apt is
changed. Adding failure suppression to pip or PlatformIO would weaken a gate
without removing any exposure.

## Documentation

`docs/ci-lock.md` was read and deliberately not changed. It states that it is
limited to CI action provenance and SHA pinning, and this change adds no
action reference: the helper is a script invoked from a `run:` block. The
documented setup steps are described here instead.

## Gates run

| Gate | Result |
| --- | --- |
| `python3 tools/check_ci_workflows.py` | pass, 13 workflows |
| `python3 -m unittest tests.test_ci_apt_install` | pass, 18 tests |
| `python3 -m unittest tests.test_check_ci_workflows` | pass, 34 tests |
| `python3 tools/check_doc_citations.py` | pass |
| `shellcheck -s sh -S style tools/ci-apt-install.sh` | pass, no findings |
| `actionlint` 1.7.12, every workflow | 7 pre-existing `SC2086` findings, the identical 7 on `origin/master`, none on a line this branch touched |
| `python3 tools/pre_review.py` | pass, every gate |
| `sh sim/scripts/check-doc-tokens.sh` | pass |

No workflow in this repository runs `shellcheck` or `actionlint` as a gate, so
both were run by hand. The `actionlint` findings in `release.yml` and
`static-analysis.yml` predate this change, so they are left alone rather than
fixed inside an apt change. `check_ci_workflows.py` is the workflow structural
gate that does run in CI, through `pre_review.py`.

## Implementation state

Landed on the branch `fix/ci-apt-resilience`: the helper, the six workflow
call sites, the path filter entries, the checker rule with its regression
tests, and the helper's own behaviour tests. Nothing else in the workflows was
touched.
