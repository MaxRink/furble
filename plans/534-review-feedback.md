# 534 - Review feedback

An independent, adversarial review of the second tranche of
[97-followups.md](97-followups.md) Group 1 batches 4 to 7 (PR #534, branch
`fix/97-batch4-7`).

The review reverted each of the three fixes in its own build and watched the
named test fail, so the red-first evidence for the Canon identity writes, the
companion secret reads and the adaptive transmit floor holds. What did not hold
is the material around the code: a citation that pointed at the shutter timer,
a golden corpus that still pinned the answer the redaction replaced, a
define-gated branch with no test in any target, a pull request body that
miscounted its own commits and its overlap with #530, and an app-side follow-up
recorded only in that body. All five are fixed below.

The commit hashes here are the branch after it was rebased onto master at
`55987a32`, the plan renumbering (#538). A rebase rewrites every hash, so the
numbers this record carried before the rebase no longer resolve, and neither do
the ones an earlier revision of the pull request body listed.

| # | Severity | Finding | Resolution | Evidence |
| ---: | :--- | :--- | :--- | :--- |
| F1 | Blocking | `docs/protocols/companion-gatt.md` cited `src/FurbleCompanionService.cpp:139-155` for the set/unset answer. Those lines are the shutter timer `esp_timer_create()` and `deinit()`. The answer is the two `secretIndicator()` arms at :901-916, and the helper is at :109. | Fixed in `2cd3dac3`. The sentence now cites :901-916 for both arms and :109 for the helper. | `sed -n '139,155p' src/FurbleCompanionService.cpp` spans the tail of the shutter timer init and `CompanionService::deinit()`; `grep -n secretIndicator` reports 109, 901 and 915. `check_doc_citations.py` reports 0 errors before and after, which is the finding's point, not a refutation of it. |
| F2 | Blocking | The golden corpus still pinned the pre-redaction answer for the write-only settings: `tests/protocol/golden/corpus.json` carried `00 35 03 07 "Default"` for wire id 53 and `00 3b 03 07 "Default"` for id 59, and `protocol_common.h:61` treated only `COMPANION_PASSWORD` as write-only, so host protocol conformance asserted the stale shape and stayed green. The Android client contract read the same corpus. | Fixed in `8260c967`. The generator learned the indicator, both words are pinned per secret id, the corpus is regenerated, and the conformance test rejects a fixture that still carries the stored value. Choice and reason in the pull request body. | Restoring the previous corpus with the new test in place fails: `protocol conformance failed: MQTT_PASS unset get response header changed`. The regenerated corpus passes: `protocol conformance passed: 42-byte location, 20-byte status, settings TLVs, triggers, and UUIDs`. A second `generate` run leaves the tree byte identical, which is what the Companion protocol tests workflow diffs. `FurbleGoldenCorpusTest` (Android, run locally with `./gradlew :app:testDebugUnitTest`) passes. |
| F3 | Blocking | `MQTT_PASS` redaction had no test. `src/FurbleCompanionService.cpp:915-916` sits inside `#if defined(FURBLE_MQTT)`, and no host target compiled the companion service with it, so the arm was compiled out of every test. `tests/host/CMakeLists.txt:2178` is the target's compile definitions. | Fixed in `224dfe77`. `companion_gatt_test` defines `FURBLE_MQTT`, the doubles gained the six MQTT settings rows plus an inert `FurbleMQTT.h`, and a new case reads id 59 by op 1 and op 0, asserts `set`, the cleared `unset`, and that the broker password appears in no record. It also pins that the broker address stays readable. | Red-first, with the `MQTT_PASS` arm reverted to the raw read: 6 FAILED, exit 1 (output below). Restored: `companion mock-central tests: PASS`, exit 0. |
| F4 | Non-blocking | The pull request body said the branch "contains four commits and no changes to #530's files beyond the plan document". It was five commits, and the branch overlaps #530 in eight files, seven of them beyond `plans/97-followups.md`. The app-side read-only follow-up for wire ids 53 and 59 existed only in that body. | Body rewritten in this round. The follow-up is now a backlog line in `plans/97-followups.md` under C6, together with what the Android screen does today and what fixes it (`27d719e2`). | `gh pr view 534 --json files` against `gh pr view 530 --json files`: 8 shared paths, 7 beyond the plan document, 3 of those 7 being other plan documents. Commit count from `gh pr view 534 --json commits`: 5 at review time. |
| N1 | Non-blocking | `docs/power/wifi-coex.md:554` cited `src/FurbleControl.cpp:34-37` for the P3/P6/P9 RSSI hysteresis. Line 34 is the `#endif` closing the sample-interval block and line 41 opens `POWER_LEVELS`, so the range covered neither. | Fixed in `52961e89`, now `src/FurbleControl.cpp:35-45`, which holds the RSSI constants and the level table. | `sed -n '34,45p' src/FurbleControl.cpp`: `#endif` at 34, `RSSI_FILTER_ALPHA` to `RSSI_STEP_UP_SAMPLES` at 35-39, `POWER_LEVELS` at 41-45. |

## F2: which of the two options, and why

The review offered two: regenerate the corpus for ids 53 and 59, or teach the
generator about write-only settings. This PR does the second and then
regenerates.

Regenerating alone would not survive the next `make -C tests/protocol
generate`, because the generator derives every settings fixture from the
settings table and its own hardcoded assumption that a readable setting answers
status 0 plus `sampleValue()`. A hand-edited corpus would be reverted by the
next generation and the conformance test would then disagree with the firmware
again, which is the same defect with a shorter lifetime. The generator and the
test now share one predicate, `settingAnswersSecretIndicator()`, mirroring the
two arms in `settingValue()`, so a third secret setting cannot be added without
the corpus following it.

Both words are pinned per secret id, `settings/response-get-53.bin` carrying
`unset` and the `-set` suffixed pair carrying `set`. The two are different
lengths, so the value length and the trailing flags byte move with them, which
is exactly the positional arithmetic a client does.

## F3: red-first evidence

The production arm reverted to the raw read, nothing else changed:

```
test: companion MQTT password reads are redacted
  FAIL: the MQTT password read reports an OK SETTING_STRING record
  FAIL: a stored MQTT password reads back as set
  FAIL: the MQTT password itself never crosses the link
  FAIL: the MQTT catalog does not leak the broker password
  FAIL: the MQTT catalog reports the password as set
  FAIL: a cleared MQTT password reads back as unset
companion mock-central tests: 6 FAILED
TEST_EXIT=1
```

With the arm restored:

```
test: companion MQTT password reads are redacted
...
companion mock-central tests: PASS
TEST_EXIT=0
```

The new case is deliberately unguarded, so dropping `FURBLE_MQTT` from the
target is a compile error rather than a silent loss of the coverage. That is
the failure mode this finding is about.

## Not repeated here

This round did not revert the three code fixes again. The review already did
that for each of them, and none of the changes in this round touches the Canon
identity writes, the `settingValue()` arms or `sampleAdaptivePower()`. What this
round re-ran is the one red-first claim the review could not produce, because
the target never compiled the code (F3), and the corpus assertion that let F2
through.

## Rebase onto master

Master moved past this branch three times while it was in review. #530 merged
as `18bbcb5a`, a squash, so none of its commits are ancestors. #538 renumbered
seven plans and rewrote `plans/README.md`. Then #532, #537, #540, #543, #546,
#547, #548 and #553 landed within the hour, and the branch went CONFLICTING
against its stale base. It has since been rebased three times, the last onto
`6b2da833`, and it now targets `master`.

Two rebase notes that explain why this record looks the way it does. The first
rebase used `git rebase --onto origin/master caf683bc`, which replays only this
branch's commits and not #530's five; the later two were plain rebases because
the branch already sat on master. No rebase needed a `plans/README.md` merge:
this branch adds no row to that index, since its review record hangs off plan
97 and that plan's own record carries no row, and no file it touches cited a
plan that #538 moved.

The rebases did break citations, because this branch inserts lines above code
that other plans cite. The first pass fixed two master-side plans that cite
`Control::sendCommand`. The last pass fixed fifteen, and that set says what the
hazard is: the floor probe adds sixteen lines to `src/FurbleControl.cpp` and the
secret redaction adds twenty-eight to `src/FurbleCompanionService.cpp`, so every
citation into those files that master had made current moved. Two of the fifteen
are in plans this branch does not otherwise touch, `plans/374-control-release-durability.md`
and `plans/546-review-feedback.md`, which is the cost of a shared
`file:line` contract.

`check_doc_citations.py --root .` is the authority for the annotated ones and
now reports 0 errors of 3,732 citations. The unannotated ones cannot be checked
mechanically: a line map shows a citation moved, not that it was right before,
and a blind shift would have rewritten 207 citations that were stale before
this branch existed. The ones this branch itself made current were checked by
content instead, which is how `docs/power/soc.md` twice moved from the previous
`:505` to the 250 ms receive at `:585`.

The last rebase also produced one real content conflict rather than a number:
master added `Settings::validTheme` to the same host double where this branch
adds `Settings::validMQTTString`. Both declarations were kept, master's first.

## Gates run locally

The two gates below were re-run on this head after the last rebase, and so were
the two host tests this branch's C++ touches, because that rebase merged master's
changes into `src/FurbleControl.cpp` and `src/FurbleCompanionService.cpp`:
`companion-gatt` and `control-adaptive-floor` both pass, which is the check that
the merge of #546, #548 and #553 with the floor probe and the redaction did not
break either. The full host suite and the sanitized run were run on the previous
head and stand from there; the matrix re-runs both on this push.

`python3 tools/check_pr_hygiene.py origin/master...HEAD`:

```
PASS: no blocking findings
summary: 0 failure(s), 0 warning(s)
```

`python3 tools/check_doc_citations.py --root .`:

```
doc citations in the tree: 3306 line-checked (113 symbol-checked), 128 external, 298 unresolved basename, 0 errors of 3732 citations in 203 files
```

Host suite, `ctest --test-dir /tmp/furble-534-build --output-on-failure`:

```
225/225 Test #225: boundary-headless-service-watchdog-refused ......   Passed    2.54 sec

100% tests passed out of 225

Total Test time (real) = 1233.80 sec
```

`python3 tools/pre_review.py --base origin/master` with clang-format 21.1.8 on
`PATH` (`clang-format==21.*`, the version `.github/workflows/
clang-format-requirements.txt` pins; the host toolchain here only ships 23,
which `check_format.py` refuses):

```
PASS: whitespace
PASS: no blocking findings
PASS: diff hygiene
PASS: settings contracts
PASS: clang-format
PASS: C++ safety
PASS: CI workflow contract
PASS: simulator scenario manifest
PASS: simulator hardware coverage
PASS: coupling ratchet
PASS: datasheet citations
PASS: M5GFX board pinning
PASS: documentation citations
PASS: scanner unit tests
PASS: 13 gates
PREREVIEW_EXIT=0
```

The protocol suite, `make -C tests/protocol CXX=clang++ test`:

```
protocol conformance passed: 42-byte location, 20-byte status, settings TLVs, triggers, and UUIDs
```

Boundary conformance, `sh sim/scripts/run-boundary-conformance.sh`: 29 of 30, with
`boundary-platform-rail` reported **Not Run** because that script's fixed `--target`
list omits `boundary_platform_rail_test`. The test is registered on the base too and
passes as test 221 of the host suite above, so this is the runner gap the pull
request already recorded, not this PR.

Sanitizers: `sh tools/run_sanitized_host_tests.sh -R '^companion-gatt$'` and `-R
'^control-adaptive-floor$'` both pass under ASan and UBSan. The script disables leak
detection on Darwin because the Apple sanitizer runtime does not support it, so
LeakSanitizer coverage is the Linux `host-sanitized` job's.

## A gate blind spot, recorded not changed

Both citation findings are the same mechanical defect: a range that exists but
does not hold the code the prose describes. `check_doc_citations.py` cannot see
it by design, because a citation that names no symbol is checked for path and
range only.

Rejecting a range that starts on a preprocessor line was considered and
rejected on evidence: 316 of the 3,421 citations in the tree start on such a
line, and most of them legitimately name a conditional block or an include
group. The rule would fail the tree for a shape that is usually correct. Both
findings were fixed by hand instead, and this document records the check that
the gate does not make.
