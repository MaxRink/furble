# 545 - CI apt resilience, review feedback

Review of PR 545 (`fix/ci-apt-resilience`, head `38cdeaf1`, previously
`376f2cc5`) against `origin/master`. Verdict: request changes on two small
items. The core claim holds: the helper removes unrelated sources, keeps the
Ubuntu archive and still fails on a genuinely missing package. Both named red
checks are now settled: `sim-sanitizers` passes on the current head through the
step that used to fail, and `sim-e2e` was still running when I finished.

Posted as a review comment. The authenticated account is the PR author, so
GitHub refuses a formal request-changes from it.

## 1. A mixed deb822 `.sources` file loses its archive stanzas without saying so (tools/ci-apt-install.sh:343-353)

One-line `.list` files are filtered entry by entry, so an archive entry sharing
the file survives (`tools/ci-apt-install.sh:245-284`). A `.sources` file is moved
out whole on any third-party URI (`:343-353`). The comment at `:345-349` says the
archive check will name it, but the check at `:372-373` only fires when no
archive source is left at all. If another file still supplies an archive, the
step passes with less archive than the image shipped.

Reproduced against a fake root with a `.sources` file carrying an archive stanza
plus a `packages.microsoft.com` stanza, alongside a clean `archive.list`:

```
--- A mixed .sources + separate archive .list: exit=0
   ci-apt-install: disabled ubuntu.sources: third-party host(s) packages.microsoft.com
   ci-apt-install: kept the Ubuntu archive, disabled 1 unrelated source(s)
   ci-apt-install: installed and verified: clang
   sources.list.d after: ['archive.list']
```

Exit 0, "kept the Ubuntu archive", and the `archive.ubuntu.com` stanza that was
in `ubuntu.sources` is gone. When the same mixed file is the only archive
provider the script does fail loudly (`exit=1`, "no Ubuntu archive source is
enabled"), so only the partial case is silent.

This does not fire on the current image: `ubuntu.sources` there is archive only
and survived in jobs 140251431084 and 140255012893. It is the one path in the
helper that can shrink the archive and report success, which is the class of
failure this PR exists to remove. Either filter `.sources` stanzas the way the
one-line path does, or state the partial case in the comment and in
`plans/375:64-66` so the next reader knows it is accepted rather than overlooked.

## 2. The allowlist is implemented twice, against the comment that says it is not (tools/ci-apt-install.sh:134-135)

```
# The allowlist lives in one place. Everything else, in shell and in awk, asks
# this function, so the rule cannot drift between the two.
```

`filter_one_line_file` reimplements it in awk at `:249-257` (`function
is_archive(host, ...)`). Two copies of the rule that must be kept in sync by
hand, and nothing tests that they agree: a change to `is_archive_host`
(`:136-146`) alone would leave the rewrite path classifying by the old rule.
Pass the decision from the shell function into the rewrite, or drop the sentence.

## Minors

- **The new enumeration and the old audit table now disagree.** The commit
  `38cdeaf1` added a correct table at `plans/375:140-147`: line 143 lists five
  workflows for `pip install -r requirements.txt` (`main`, `reproducible`,
  `pages`, `release`, `static-analysis`) and line 144 lists two for the
  `.github/workflows/*-requirements.txt` form (`main`, `pre-review`). Both counts
  match what I found. But the older "Other setup steps audited" row at
  `plans/375:233` still says "in 6 workflows" and "in 3". The same document now
  states two different counts; correct line 233 to 5 and 2, or the reader has to
  decide which one to believe.
- `plans/375:172-173` and the description say the helper went into "the
  `pull_request` and `push` path filters of all five workflows". Only
  `coverage.yml` and `sim-e2e.yml` have push path filters. `power-gate.yml` has a
  push trigger with no paths filter, and `static-analysis.yml` and
  `ui-screenshots.yml` have no push trigger. The helper is in every path filter
  that exists, which is the substantive point, so just say that.
- `plans/375:155-156` says the table's numbers are "the `run:` line". The Before
  values point at the `apt-get install` line instead, for example
  `power-gate.yml:51` on master where the `run:` is `:49`. The After values are
  correct `run:` lines.
- `tools/ci-apt-install.sh:184-185` says "Name the upstreams once per list", but
  `expand_uri` runs per URI, so the real log printed the same mirror list twice
  in job 140251431084 (01:17:50.393 and 01:17:50.401).

## What passed, do not redo

- **Every apt site is covered.** Six on master across five workflows, all
  converted: `coverage.yml:99`, `power-gate.yml:50`, `sim-e2e.yml:78` and
  `:635`, `static-analysis.yml:39`, `ui-screenshots.yml:50`. No bare `apt-get`,
  `apt` or `aptitude` remains in any of the 17 workflow files. `main.yml`,
  `reproducible.yml`, `host-sanitized.yml`, `camera-tests.yml`,
  `protocol-tests.yml`, `pre-review.yml`, `release.yml`, `pages.yml`, `apple.yml`
  and `android.yml` have no apt step, so nothing was missed. The new section at
  `plans/375:134-151` enumerates this correctly and the claim at `:149-150` that
  no `sudo apt-get install` remains is true.
- **A genuinely missing package still fails the job.** Fake root, intact archive,
  `nosuchpkg`: exit 1, `apt-get install failed`, sources untouched. Also pinned
  by `tests/test_ci_apt_install.py:231`
  (`test_missing_package_fails_the_step`), `:245`
  (`test_package_dpkg_does_not_report_installed_fails_the_step`) and `:255`
  (`test_package_reported_half_configured_fails_the_step`). Those tests are real:
  each drives a fake `apt-get` or `dpkg-query` that returns a failure and asserts
  a non-zero exit.
- **No exit status is ignored anywhere.** `set -eu` at `:77`; `apt-get update`
  and `apt-get install` both go through `|| die` at `:381-386`; the dpkg loop
  dies on anything but `install ok installed` at `:391-399`. `|| true`,
  `--fix-missing` and `AllowInsecureRepositories` appear only in comments
  (`:22`, `:377`). The only `return 0` paths, in `expand_uri` (`:162-194`), set
  `opaque=1` at `:316` and leave the source enabled, so a source it cannot read
  still fails at update instead of being silently dropped.
- **A one-line file keeps its archive entry.** `:277-283` comments out only the
  third-party line. My fixture leaves `ubuntu.sources` untouched and comments out
  only the Microsoft entry, which is what the real runs did too.
- **The archive is intact on the real image.** Job 140251431084 at head
  `376f2cc5` logs the resolved list (`azure.archive.ubuntu.com`,
  `archive.ubuntu.com`, `security.ubuntu.com`), then "kept the Ubuntu archive,
  disabled 1 unrelated source(s)" and "installed and verified: clang
  libsdl2-dev". Job 140255012893 at `38cdeaf1` repeats the green result.
- **The checker rule does what the description says.**
  `tools/check_ci_workflows.py:220` reports exactly 12 findings against
  `origin/master` with the stated 2/2/4/2/2 split across `coverage`,
  `static-analysis`, `power-gate`, `ui-screenshots` and `sim-e2e` (two jobs), and
  none against this branch.
- **The new test file really runs in CI.** `.github/workflows/main.yml:120` runs
  `python -m unittest discover -s tests -p 'test_*.py'` in the "Python tooling
  tests" job, which has no `needs`/`if` and so is not gated on firmware changes.
  The third gate in the description is real.

## Gates I ran

```
python3 tools/check_ci_workflows.py                  pass, 13 workflows
shellcheck -s sh -S style tools/ci-apt-install.sh    exit 0, no findings
actionlint                                           7 SC2086, identical to origin/master
                                                     (5 release.yml, 2 static-analysis.yml), 0 added
python3 -m unittest tests.test_ci_apt_install        Ran 18 tests, OK
python3 -m unittest tests.test_check_ci_workflows    Ran 34 tests, OK
```

I re-ran the branch checker against a checkout of `origin/master` (copying the
new `check_ci_workflows.py` in) to confirm the 12 findings rather than take the
number from the description.

## CI state at head 38cdeaf1

The two checks the description discusses, `sim-e2e` and `sim-sanitizers`, failed
together on the previous head `45806ed6` in the install step, jobs 140249072399
and 140249072383:

```
ci-apt-install: disabled ubuntu.sources: third-party host(s) priority priority priority priority priority priority
ci-apt-install: FAIL: no Ubuntu archive source is enabled under /etc/apt; refusing to install against an empty package index
```

That is the `apt-mirrors.txt` token bug the third commit (`376f2cc5`, "take the
URI token from each mirror list line") fixes.

On the current head `38cdeaf1`:

- `sim-sanitizers` passes in 9m5s, job 140255012767. The job's step list is
  `1 Set up job` through `4 Install SDL2, Clang and the sanitizer runtime
  success`, then `5` to `8` all success. Step 4 is the exact step that printed
  the `priority priority` failure at `45806ed6`, so this check is fixed on the
  head, not merely skipped.
- `sim-e2e` was still running when I finished the review, so it has no result on
  this head. It calls the same helper (`sim-e2e.yml:82`) with the same image, and
  the sibling job plus the power gate both pass, so I expect it green. Worth
  confirming before merge rather than assuming.
- `Simulator power regression` (140255012893, 2m52s), `ui-screenshots`
  (140255012777, 4m5s), `Static analysis` and `Pre-review scanners` all pass at
  this head. `Measure firmware coverage and enforce the floor` was still pending.

No check on the PR was failing when I finished.

## Implementation state

Both findings are fixed and the four minors are corrected, on
`fix/ci-apt-resilience`. The line numbers inside the findings above describe
head `38cdeaf1`, the state the review read, and are kept as written; every
location named here is on the fixed head.

### 1. A mixed deb822 `.sources` file keeps its archive stanzas

Fixed. `.sources` files are filtered stanza by stanza instead of being removed
whole (`tools/ci-apt-install.sh:351`), so an archive stanza beside a third-party
stanza keeps working. A stanza that carries an entry the script cannot classify
is left enabled and reported, never dropped.

The review's own reproduction, rerun on the fixed helper with the same fake
root shape:

```
ci-apt-install: disabled third-party source(s) in ubuntu.sources: packages.microsoft.com
ci-apt-install: kept 1 source file(s), dropped 1 third-party source file(s)
ci-apt-install: apt-get update
ci-apt-install: apt-get install --no-install-recommends -y clang
ci-apt-install: installed and verified: clang

--- ubuntu.sources after ---
Types: deb
URIs: http://archive.ubuntu.com/ubuntu/
Suites: resolute
Components: main universe

#Types: deb
#URIs: https://packages.microsoft.com/ubuntu/26.04/prod
#Suites: resolute
#Components: main
```

The archive stanza is untouched, the third-party stanza is commented out, the
file is not removed, and the message no longer claims more than it did. Pinned
by `tests/test_ci_apt_install.py:220`
(`test_mixed_deb822_file_keeps_its_archive_stanza`) and
`tests/test_ci_apt_install.py:252`
(`test_a_deb822_file_of_only_third_party_stanzas_is_disabled`).

Two things came with it. The files are read back after the rewrite, so the exit
status comes from what is actually enabled rather than from what the rewrite
believes it did (`tools/ci-apt-install.sh:447`). And when the only sources left
are ones the script cannot classify, it warns instead of claiming the archive is
present.

### 2. The allowlist exists once

Fixed by deleting the second implementation. The rule is
`tools/ci-apt-install.sh:145`, plain shell with no awk, and both rewrite paths
ask it through `tools/ci-apt-install.sh:233`. The comment that claimed one place
is now true.

`grep -n 'ubuntu\.com' tools/ci-apt-install.sh` returns one code line,
`152:    ubuntu.com|*.ubuntu.com) return 0 ;;`. The new
`tests/test_ci_apt_install.py:388`
(`test_the_archive_rule_is_implemented_once`) asserts the rule appears once and
never inside an awk program, so a future second copy fails the test job.

### Minors

- The pip counts now agree: the older audit row says 5 workflows and 2, matching
  the enumeration table added in `38cdeaf1`.
- The path filter sentence now says the helper is in every filter those five
  workflows have, names which have two, and records that `power-gate.yml` pushes
  without a path filter and `ui-screenshots.yml` and `static-analysis.yml` have
  no push trigger.
- The per-workflow table's Before column now cites the `run:` line on master
  (`49`, `49`, `77`, `634`, `98`, `38`) instead of the install line.
- The mirror list log is printed once per list, through a marker file in the
  work directory, instead of once per stanza that points at the same list. Job
  140251431084 printed it twice.

### Gates on the fixed head

```
python3 tools/check_ci_workflows.py                    pass, 13 workflows
python3 -m unittest tests.test_ci_apt_install          Ran 21 tests, OK
python3 -m unittest tests.test_check_ci_workflows      Ran 34 tests, OK
python3 -m unittest tests.test_sim_ci_contract         Ran 11 tests, OK
python3 tools/check_doc_citations.py                   0 errors
shellcheck -s sh -S style tools/ci-apt-install.sh      exit 0, no findings
actionlint                                             the same 7 as origin/master, none added
python3 tools/pre_review.py                            pass, every gate
```

### The two checks this review left open

`sim-sanitizers` passed on `38cdeaf1` in job 140255012767, through the step that
printed the `priority priority` failure on the previous head.

`sim-e2e` at `38cdeaf1`, job 140255013042: "Install SDL2 and Clang" completed
successfully and the job was still running when this was written, past the
install and working through its fuzz gates. The install step is the one that
failed on `45806ed6`, so that failure is settled on the head as well.
