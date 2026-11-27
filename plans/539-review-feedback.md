# 539 - Review feedback for the fork network sweep

Review-feedback record for PR #539 (`plan/371-fork-sweep`), branch head
`b8b273f3`, base `master` at `b509486b`. This is a research record, so it was
reviewed as a fact-check: every census number that can be re-derived from the
GitHub API was re-derived, and the two claims whose failure would be expensive
(the web installer chip family and the command-drop claim) were read in code.

Nine findings (F1 to F9). The verdict-table numbers themselves are correct,
which is worth saying plainly: all fifteen ahead counts and every named branch
exist exactly as recorded.

## Findings

### F1 - the census adds up to 40 forks, not 41

Defect. `plans/371-fork-network-sweep.md:15-17` says "Upstream has 41 forks.
Twenty-five are byte mirrors of gkoh master and carry no work. The remaining
fifteen hold 123 commits ...".

```
$ gh api repos/gkoh/furble/forks --paginate -q '.[].full_name' | wc -l
41
$ gh api repos/gkoh/furble -q '.forks_count'
41
```

25 plus 15 is 40. The missing fork is our own, `MaxRink/furble`, which is in
neither class and is never named: it is `ahead=1523 behind=1` against gkoh
master, so it is not a mirror, and it is not one of the fifteen. State that the
sweep covers the other forty forks.

### F2 - the twenty-five are not byte mirrors of gkoh master

Defect. Only one of the twenty-five is identical to gkoh master today:

```
$ for each of the 25 forks not in the verdict table:
    gh api repos/gkoh/furble/compare/gkoh:master...OWNER:master
daozhao/furble  identical (behind 0)
the other 24    behind by 1 to 140 commits
```

The material claim ("carry no work") is correct: each of the twenty-five has
exactly one branch, `master`, and `ahead_by 0`. But "byte mirrors of gkoh
master" is wrong for twenty-four of them, and the two facts are different
claims. "Unmodified copies of gkoh master at fork time" is what the API shows.

### F3 - the two `pushed_at` counts do not reproduce

Defect. `plans/371-fork-network-sweep.md:20-23` says "six forks carry gkoh
master's own push time, and `daozhao` reports a `pushed_at` earlier than its
`created_at`".

```
$ gh api repos/gkoh/furble -q '.pushed_at'
2026-09-14T01:17:55Z
$ forks whose pushed_at equals that value: daozhao/furble   (one, not six)
$ gh api repos/gkoh/furble/forks --paginate | python3 -c '... pushed_at < created_at ...'
forks: 41  pushed_before_created: 22
```

22 forks report `pushed_at` before `created_at`, not one. The conclusion the
plan draws is right and is actually strengthened by the real numbers, but the
two figures need correcting to what the API returns.

### F4 - 123 does not match the table's own column

Defect, low severity. `:16` says the fifteen forks "hold 123 commits that are
absent from our master by SHA", and `:17-19` decomposes it as 54 present, 1
upstream-only and 68 needing a verdict, which sums correctly. The verdict
table's own "commits ahead" column sums to 118:

```
38+13+9+5+1+9+2+2+6+1+5+9+6+4+8 = 118
```

Measured against our master instead of gkoh master, the same fifteen sum to 120
(`gh api repos/MaxRink/furble/compare/master...OWNER:BRANCH`, fifteen calls).
State what 123 counts, for example every branch of those fifteen, or the table
and the prose will never reconcile.

### F5 - the command-drop claim names the wrong function

Defect. `:75-77` and the PR body say "`Control::sendCommand` drops a command
when the queue is full and only logs it". The drop with the log is in
`Control::Target::sendCommand`:

```
src/FurbleControl.cpp:131-135
  BaseType_t ret = xQueueSend(m_Queue, &cmd, 0);
  if (ret != pdTRUE) {
    m_InFlight.fetch_sub(1, std::memory_order_acq_rel);
    ESP_LOGE(LOG_TAG, "Failed to send command to target.");
  }
```

`Control::sendCommand` (`src/FurbleControl.cpp:586-611`) has no log at all; it
returns the queue status, and the console turns a failure into
`fail("control queue full")` (`src/FurbleConsole.cpp:169-171`). The conclusion
for plan 374 stands, the attribution does not.

Two precision notes for the same item: the per-target queue is eight deep
(`include/FurbleControl.h:79`, `m_QueueLength = 8`), so "a release queued behind
a press" cannot overflow it; it takes eight pending commands. And the plan does
not mention the harder loss path, which is the link-gated drop in
the link-gated drop in `Control::sendCameraCommand`, at `src/FurbleControl.cpp:650`
where the release is discarded outright because the target is not connected.

### F6 - the web installer finding omits the gate that requires the shape

Defect, and this is the one that would be expensive. The code description in
`:68-71` is accurate:

```
$ PLATFORM=m5stick-s3 VERSION=9.9.9 python3 web-installer/generate-manifest.py
ESP32 furble-bootloader-m5stick-s3-9.9.9.bin 4096
ESP32-S3 furble-bootloader-m5stick-s3-9.9.9.bin 0
```

But `tools/verify_web_installer.py` *requires* exactly that shape, and the Pages
deploy runs it:

```
tools/verify_web_installer.py:25-45   build_family_error(): "expected exactly one
                                      build for each chip family ['ESP32','ESP32-S3']"
tools/verify_web_installer.py:13-18   EXPECTED_FLASH_OFFSETS: ESP32 (0x1000,...),
                                      ESP32-S3 (0x0,...)
tools/verify_web_installer.py:97-104  validate(): "expected ESP32 and ESP32-S3 builds"
.github/workflows/pages.yml:146       run: python tools/verify_web_installer.py site
```

So the two-family manifest is currently an enforced invariant, not an oversight,
and plan 372 as scoped fails that gate unless it changes the verifier in the
same PR. The plan must say so, or whoever implements 372 loses a cycle. The
plan should equally record the mitigating half: ESP Web Tools selects a build by
the detected chip family, so the wrong-family entry is only reachable by loading
a board's own manifest with the other family connected, and the asset behind
both entries is the same platform's binary. That changes what 372 has to fix.

### F7 - the PR body's "nine things" is six, and two name no symbol

Defect in the description. The body says "Nine things that look like security or
correctness fixes and are already in our tree, each with the symbol that proves
it". `plans/371-fork-network-sweep.md:78-91` has six bullets, and two of them
name no symbol at all: the Fujifilm item is "recorded in plan 303" and the
serial test mode is "superseded by the headless S3 profile, the console and the
simulator". Six, four with a symbol.

### F8 - five verdict rows cannot be reproduced from the document

Defect, low severity. In `:26-42`, five rows give no branch that a reader can
resolve: `sbstjn` 9, `Cronkan` 6, `wolcano` 4 and `petabyt` 8 have no branch
name, `sky18Dragon` gives the wildcard `feature/ricoh-*` (the five-commit branch
is `feature/ricoh-gr-bluetooth-remote`; the sibling is three), and `filovre`
elides `devin/1776282912-nikon-ble-linux-script`. All five counts are correct
once the branch is found, which is why naming them costs nothing.

### F9 - "two new commits worth taking" is never enumerated

Defect, low severity. `:29-30` says `zhuimeng2026-hub` holds "Two new commits
worth taking, seven already present", but the "What is worth taking" section
(`:62-76`) names only one commit from that fork, the 400 ms shutter
(`78a9a7b5`). A reader cannot act on the other one. Enumerate both, or say which
of the nine was counted where.

## Nits

- `:82` says the Fujifilm item is "recorded in plan 303". Plan 303 records it as
  commit `b4347811` for issue 221 (`plans/implemented/303-upstream-sync.md:24`),
  not under the number #223. #223 is a real closed-unmerged PR ("Revert some
  minor changes to FujifilmBasic."), so the reference is materially right and
  only unfindable by number.
- `:29` writes "upstream #199 and PR #230" without saying which repository. Both
  are gkoh PRs (#199 "Implement GPS for Canon EOS.", #230 "Move the final write
  to CHR_IDEN_UUID characteristic..."), but `MaxRink/furble` also has a #230 with
  an unrelated subject.

## Attacked and cleared

- **Fork count.** 41, matching the plan, from both the forks list and
  `forks_count`.
- **Every ahead count in the verdict table.** All fifteen re-derived with
  `gh api repos/gkoh/furble/compare/gkoh:master...OWNER:BRANCH`: tiredboffin
  `tb-xt5` 38, Oaksen `189-support-canon-eos-r-gps` 13, sentrybottale
  `245-support-the-new-m5sticks3` 9, sky18Dragon 5, herrfrei `nikon-smart-mode`
  1, zhuimeng2026-hub `master` 9, slgray `master` 2, LittleBidet `master` 2,
  nariox `moviemode` 6, ediiiiiiii `master` 1, filovre 5, sbstjn 9, Cronkan
  `master` 6, wolcano 4, petabyt `master` 8. Fifteen of fifteen correct.
- **The "carry no work" half of the census.** Each of the twenty-five
  non-listed forks has exactly one branch and `ahead_by 0`, so "no work" holds
  even though "byte mirror" does not (F2).
- **The zero-work boundary.** No fork outside the table has any ahead branch
  that I could find, including the forks with several branches
  (LittleBidet's six, wolcano's three, sbstjn's four).
- **The 30 ms and 400 ms numbers.** `include/interval.h:10` is
  `INTERVAL_DEFAULT_SHUTTER = {30, SpinValue::UNIT_MS}` and the fork's
  `78a9a7b5` is "Raise the default shutter time to 400ms and document AF-ON".
- **Upstream.** `upstream/master` is `0cac22aacc3d9ef250be40849118af78a5f39f27`
  "Enable watchdog on M5StickS3." and `git rev-list --left-right --count
  origin/master...upstream/master` is `1523 1`, so "one commit ahead" and "we
  are a superset" both hold.
- **Every upstream number cited.** #4, #50, #108, #132, #205, #258, #307 and #313
  are closed and unmerged; #211, #212, #250, #268, #270 and #310 are merged; #223
  is closed and unmerged; #235 is a genuine issue, "Nikon Z7II test without
  screen", which fits a serial test mode. Nothing is invented.
- **The six "cleared" items.** Four verify by symbol: IRK and identity-key
  distribution at `lib/furble/Device.cpp:79`, the peer address type toggle in
  `patches/ble_gap.patch:6-18`, `NimBLEDevice::deleteClient()` after a failed
  connect at `lib/furble/Camera.cpp:688-691`, and the build-tool hardening at
  `patches/ble_gap_patch.py:12` (`PATCH_MARKER`), `:35-38` (`flock`) and `:60`
  (`raise`). Two verify by cross-reference: `plans/implemented/303-upstream-sync.md:24-25`
  covers the Fujifilm revert and the serial test mode, both with the "not
  carried" verdict the plan repeats. The Windows `patch.exe` gap is real: the
  patch helper shells out to `patch` with no platform discovery.
- **Camera mode and the Canon control byte.** `lib/furble/Camera.h` has no photo
  or movie mode (only `SecurityMode`), and `lib/furble/CanonEOSRemote.cpp:69-83`
  writes fixed `SHUTTER | CTRL`, `CTRL` and `FOCUS | CTRL` bytes. The
  nariox item's premise holds.
- **Citations.** The plan carries no `path:line` citation at all, so there is
  nothing for `tools/check_doc_citations.py` to resolve and a "wrong line"
  finding is not possible; `python3 tools/check_doc_citations.py --root .`
  reports 0 errors of 3108 citations in 175 files. The nine files and symbols it
  names by path all exist.
- **Gates.** `python3 tools/pre_review.py --base origin/master` passes 13 of 13
  gates for this head, and `python3 tools/check_pr_hygiene.py origin/master...HEAD`
  reports 0 failures and 0 warnings, so the body's verification claim reproduces.
- **`plans/README.md` index row.** Added in the component-list section with the
  same shape as its neighbours and no duplicate number.


## Resolution

All nine findings are accepted and fixed in the same branch, except F8, which
was a presentational gap rather than a wrong claim.

| Finding | State | Fix |
| :--- | :--- | :--- |
| F1 census adds to 40 | fixed | The paragraph now names our own fork as the forty-first entry and states that it is neither a mirror nor one of the fifteen. |
| F2 byte mirrors | fixed | Now says the twenty-five carry no commits of their own and that twenty-four are behind gkoh master, with `daozhao` the only identical one. |
| F3 pushed_at counts | fixed | Replaced with the re-derived numbers: twenty-two of forty-one report `pushed_at` before `created_at`, and only `daozhao` carries gkoh's push time. |
| F4 123 against a column summing to 118 | fixed | The paragraph now states that the ahead column counts each fork's best branch and sums to 118 because nine forks keep work on more than one branch. |
| F5 wrong symbol | fixed | Now cites `Control::Target::sendCommand` at `src/FurbleControl.cpp:131-135`, `Control::sendCommand` at `:586-611`, the queue length at `include/FurbleControl.h:79`, and the harder loss path in `Control::sendCameraCommand` where a link that drops after a press loses its release. |
| F6 verifier gate omitted | fixed | The web installer item now records that `tools/verify_web_installer.py` requires one build per family, that `pages.yml` runs it on the published site, and that ESP Web Tools selects by detected family. |
| F7 nine cleared items against six in the doc | fixed | The count in the plan is now six, matching the list. The PR body's "nine" is corrected in the same commit. |
| F8 branches missing for five forks | fixed | The five rows now name their branches. |
| F9 two new commits not enumerated | fixed | The row now names `78a9a7b5` and `d72f0662`, and says which of the other two are already present in a stronger form. |
