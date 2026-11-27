# 511 - Review feedback for the documentation citation gate

Status: review record and remediation. PR 511 (`cite/citation-gate`, head
`14cb52e6`) was squash-merged as `81139818` while this review was still running.
The review was therefore not recorded before that merge, which is the order the
repository requires. This file records what was checked and what was found, and
the branch that carries this file fixes the findings against `81139818` as PR
#522. No firmware, configuration, hardware or behavioral change is made or
claimed.

## What was reviewed

- PR head `14cb52e6`, merge base `c644f39e`, squash `81139818`, merged by the
  author at 2026-11-26T11:34:55Z. CI on the head: 24 checks, all passed or
  skipped. Review threads at merge: 0. `tools/check_review_threads.py --repo
  MaxRink/furble --pr 511` reports both facts after the merge as well, so the
  merge was CI-clean and thread-clean; the missing piece was this review.
- Tree used for every check: a scratch clone at `/tmp/furble-cite511`, never the
  primary checkout. The base figures were re-derived in detached worktrees of
  `bebf7caf`, `0c585de9`, `f354e3b3` and `c644f39e`, each with this PR's
  `tools/check_doc_citations.py` and `tools/doc_citations.json` copied in.
- Commands: the gate in `--all` and `--base` mode, its unit tests,
  `tools/pre_review.py --base c644f39e`, `sh sim/scripts/check-doc-tokens.sh`,
  and `tools/check_review_threads.py`.

## Re-derived finding count

The plan stated 80 findings against the merge base. The tool reports **86**:

```text
c644f39e (merge base): 2511 line-checked (94 symbol-checked), 99 external,
126 unresolved basename, 86 errors of 2736 citations in 144 files
  split: 78 symbol missing, 5 unresolved path, 3 line out of range
bebf7caf (first base): 1497 line-checked (84 symbol-checked), 79 external,
84 unresolved basename, 80 errors of 1660 citations in 98 files
  split: 74 symbol missing, 5 unresolved path, 1 line out of range
```

80 is the `bebf7caf` figure. The merge base gained six findings after that base:
five in the datasheet pages that #490 to #517 added and one in #504's IMU plan.
The plan's own pasted output already said 86 while its prose said 80, which is
exactly the sort of contradiction the reviewer has to resolve rather than trust.

Two more numbers in the module docstring were wrong. It claimed "51 citations
whose named symbol was not where the text said, and 84 paths that resolved to
nothing". 84 is the **symbol-checked** count at `bebf7caf`, not the number of
unresolved paths; the symbol findings are 74 there and 78 at the merge base, and
the unresolved path count is 5 at both. Both statements now carry the figures the
tool actually prints.

The historical attribution was checked rather than assumed. `git blame` on
`src/FurbleUI.cpp:1887` to `:1893` shows `370c0a99` (#473) authored the seven
line `UI::wakeDisplay` comment, and `6bf03a70` (#472) touches only `sim/`. The
corrected claim that #473, not #472, moved the citations by four is right. The
base-by-base chain in the plan also reproduces: `bebf7caf` 80 of 1660 in 98
files, `0c585de9` 81 of 2037 in 128, `f354e3b3` 86 of 2735 in 143, `c644f39e` 86
of 2736 in 144.

## Does the gate catch the drift it exists for?

Two mutations in a scratch worktree, both at the plan 338 citation that started
this work, `plans/338-e2e-page-coverage.md:86` citing
`src/FurbleUI.cpp:11344-11361` for `legendSelectable()`.

1. Four **code** lines inserted directly above `bool UI::legendSelectable`:
   the gate fails the citation, reports `legendSelectable` at 11348 to 11365, and
   the tree moves from 0 to 5 errors. Caught.
2. Four **comment** lines inserted in the same place: the citation still passes,
   because a definition's span covers the comment block attached above it and the
   old line now holds a comment. Not caught.

The second result contradicts the shipped contract sentence "A range that starts
above a multi line definition is the drift shape and fails". The gate cannot
know whether a comment block was always there or was inserted after the document
was written, so this is a genuine intent limit, not a fixable bug. It is now
stated in the module docstring, in `CONTRIBUTING.md`, in the plan, and pinned by
a unit test. Spelled out: of the 91 symbol-checked citations at the PR head, 8
pass only because the cited start line is inside such a comment block, so the
absorbed shape is not hypothetical.

`--base` was also exercised against a real edit: a source commit that moves a
symbol fails a citation in an untouched document, which is the #473 shape. The
unit tests cover the same path and the stale-allow rule.

## Blind spots, probed one by one

A probe document in the scratch tree, plus small throwaway repositories, gave:

| Shape | Result | Stated before this review? |
| --- | --- | --- |
| Bare filename `Camera.h:203` | counted unresolved, no finding | yes |
| URL fragment `...FurbleUI.cpp#L120` | not parsed at all | yes |
| Bare continuation `:1149` | not parsed | yes |
| `file:line:column` | line checked, column ignored | yes |
| No symbol in the clause | path and range only, so a wrong line in range passes | yes |
| Table cell holding several citations | not symbol-checked | yes |
| Two symbols in one clause | not symbol-checked | yes |
| Symbol only in a comment on the cited line | passes | **no, now documented and tested** |
| Comment inserted above a cited definition | passes | **no, now documented and tested** |
| Drift inside one definition | passes; the cited line stays inside the span | yes |
| Prose that contradicts a correct line | passes; the gate cannot read intent | yes |

The last two are the ones a reader could have been misled by, so they were added
to the docstring, `CONTRIBUTING.md` and the plan, and pinned with tests. The
probe also confirms the honest half: a citation that names no symbol and points
at the wrong line passes, which is how plan 303 shipped two wrong numbers.

## The symbol rule, sampled by hand

At the shipped tree the gate symbol-checks 91 citations (92 after the fixes
below). The shape that lets each one pass was computed, then checked by reading
the line:

- 72 pass because the symbol is on the cited start line.
- 8 pass because the start line is the attached comment head of the definition.
  Sample: `plans/implemented/15-gps-power.md:125` cites `GPS::update` at
  `src/FurbleGPS.cpp:2500-2771`, which starts on the attached comment at 2500
  above the definition at 2501.
- 5 pass because the start line is inside the definition body.
- 5 pass through the "file only uses this symbol" range rule.
- 1 passes through the one-line declaration rule.

Twelve were then checked against the code:

| Citation | Claim | Code at the line |
| --- | --- | --- |
| `plans/338-e2e-page-coverage.md:86` -> `src/FurbleUI.cpp:11344-11361` | `legendSelectable()` false off Stick | definition at 11344, returns true only for the four Stick boards |
| `docs/power/ble.md:341` -> `src/FurbleUI.cpp:9582` | `startScan()` clears `CameraList` | `CameraList::clear();` is at 9582 |
| `docs/power/ble.md:1272` -> `sim/power_profiler.cpp:1773-1778` | `profilerSetRadioConnected` sets the flag | the function body is 1773 to 1778 and assigns `state.radio_connected` |
| `docs/power/ble.md:205` -> `lib/furble/FujifilmSecure.h:105` | `SCAN_TIME_MS` is 60000 | the constant is at 105; `lib/furble/Nikon.h:59` is the same constant |
| `docs/protocols/companion-gatt.md:45` -> `include/FurbleCompanionService.h:181` | the size is pinned by a `static_assert` | the first size assert is at 181, three more follow |
| `docs/power/wifi-coex.md:510` -> `src/FurbleWiFi.cpp:608-660` | `scanForNetwork()` | definition at 608; the refusal and scan span the range |
| `docs/protocols/camera-remotes.md:29-34` -> `lib/furble/protocol/AdvertisementProtocol.h:12,42` and siblings | vendor rules | 12 is the Nikon company id, 42 `matchesNikonDiscovery`; 19 the device field and 44 `matchesNikonReconnect`; 14-15 the Sony id and type, 23-34 the struct; 13 the Lumix id, 36-40 the struct; 52 and 53 the DJI and Ricoh matchers |
| `plans/29-virtual-test-rig.md:106-113` -> `src/FurbleCompanion.cpp:928`, `src/FurbleCompanionService.cpp:304` | the NimBLE value is only passed on | 928 is inside `CompanionGatt::onWrite` (924), 304 is `CompanionService::handleLocation`; its continuations 215, 1027, 1213, 1226 and 1293 are the five `CompanionService` definition lines |
| `plans/implemented/76-reconnect-stuck.md:57,59` -> `lib/furble/Camera.cpp:1458`, `lib/furble/Fujifilm.h:112` | link state only; `m_Configured` | `Camera::isConnected` at 1458; `std::atomic<bool> m_Configured` at 112 |
| `plans/implemented/60-issue-257.md:135,192` -> `include/FurbleControl.h:100`, `lib/furble/NikonSmart.cpp:238-264` | 5 s timeout; the scale function | `TIMEOUT_INFINITE_MS` at 100; `degreesToDMSubMin` at 238 in a 266 line file |
| `plans/implemented/34-ota-partitions.md:424-440` -> `src/main.cpp:424-426`, `src/main.cpp:442`, `src/main.cpp:477`, `src/FurbleUI.cpp:13416-13474` | init order and the UI loop | `Settings::init()` at 426 in the cited range, `Platform::init()` at 442, `Device::init()` at 477, `UI::task` at 13416 |
| `plans/25-multiconnect-ui.md:246,324` -> `src/FurbleUI.cpp:8596-8630`, `components/esp-nimble-cpp/src/NimBLEClient.h:65` | `doConnect`; `getRssi()` | `UI::doConnect` at 8596, `int getRssi() const;` at 65 |

The symbol-less half is confirmed by the probe, not by assertion: a citation with
no code span in its clause is only path and range checked. That is why the two
plan 303 numbers (the settings roller and the shutter triggers) passed the gate
while they were wrong.

## Churn spot checks

Twelve corrections from the diff were checked against the code and against the
sentence they sit in. All twelve now point at what the sentence says:

1. `plans/338-e2e-page-coverage.md:86`: 11340-11357 to 11344-11361. The
   definition is at 11344 and the prose matches the board switch.
2. `plans/implemented/303-upstream-sync.md:39`: the settings roller moved to
   10392, which is the `addConnectMenu` roller creation.
3. `plans/implemented/303-upstream-sync.md:41`: the gesture shutter is in
   `UI::handleGesture` at 12947.
4. `docs/power/soc.md`: `FURBLE_CONSOLE` moved from 3 to 4, and line 4 is the
   `#if defined(FURBLE_CONSOLE)` guard.
5. `docs/power/soc.md`: the console APB lock moved from 2833-2844 to 3697, and
   3697 creates the `ESP_PM_APB_FREQ_MAX` lock.
6. `docs/power/soc.md`: `RSSI_SAMPLE_INTERVAL_MS` moved from 22 to 27, and 27 is
   `constexpr uint32_t RSSI_SAMPLE_INTERVAL_MS = 5000;`.
7. `docs/power/soc.md`: `m_LastRssiSample` is now cited at 1515, which is
   `m_LastRssiSample = now;` inside `Control::sampleAdaptivePower` (1499).
8. `docs/power/ble.md:341`: the `CameraList` clear moved from 6741-6752 to 9582,
   and 9582 is the clear.
9. `docs/protocols/camera-remotes.md`: all six vendor rows were wrong by six
   lines, and the new numbers are the constants and matchers listed above.
10. `docs/protocols/companion-gatt.md:45`: the `static_assert` moved from 169 to
    181, where the four size asserts begin.
11. `plans/29-virtual-test-rig.md`: the old `lib/furble/Companion.cpp` line 613
    became `src/FurbleCompanionService.cpp:304`, which is `handleLocation`.
12. `plans/implemented/60-issue-257.md:192`: 253-279 became 238-264, which fits
    the 266 line file and is the `degreesToDMSubMin` definition.

Five wrong citations and one imprecise line pointer that the gate could **not**
see were found by reading while doing these checks. They are listed under F5 to
F8 below and are fixed in this remediation. So the churn is not uniformly safe:
the mechanical 86 are sound, but the same edits sat next to citations that named
no symbol, used a bare continuation, or shared a clause with a second symbol.

## Allowlist and silences

`tools/doc_citations.json` ships `"allow": []`. The only `allow` text in the
diff is the implementation, its tests and the empty list. No finding was
suppressed: all 86 base findings are absent at the head because the citations
were repointed, and the tree's 0 errors at the head is not the result of a
silenced entry. The stale-entry rule (an entry that suppresses nothing is an
error) is implemented and has a test. The five wrong citations above were not
suppressed either; they were invisible to the gate.

The eleven external prefixes all carry a reason of substance. `components/` is
declared but a tracked path still wins, so `components/icons/` and
`components/esp-nimble-cpp/` are read from the tree, which is the right order.

## Findings and resolution

| ID | Finding | Resolution |
| --- | --- | --- |
| F1 | The plan and the module docstring stated 80 findings against the merge base; the tool reports 86 (78 symbol, 5 path, 3 range) | Plan and docstring restated; 80 is labelled as the `bebf7caf` figure |
| F2 | The docstring claimed 51 symbol findings and 84 unresolved paths; 84 was the symbol-checked count and the real path count is 5 | Docstring restated with the reproducible figures |
| F3 | "A range that starts above a multi line definition is the drift shape and fails" is false when the definition has an attached comment block; a four line comment insert passes | Contract corrected in the docstring, `CONTRIBUTING.md` and the plan; test added |
| F4 | A symbol mentioned only in a comment or a log string on the cited line satisfies the on-line rule; this was not stated | Stated in all three places; test added |
| F5 | `docs/power/soc.md` OS-02 cited `src/FurbleControl.cpp:379-386` for the 50 ms receive and the continuation `1149` for `m_LastRssiSample`, and its prose still said 50 ms and 20 wakes; the code is 250 ms at 484 after 797f01fa | Citations moved to 484, 1515 and 1227; section marked implemented at 250 ms; the matrix P-012 row and the summary list updated |
| F6 | `plans/implemented/76-reconnect-stuck.md` cited `Camera::m_Mutex` at `lib/furble/Camera.cpp:28`, which is an `#if`, while the lock is at 532; the duplicate claim in the next paragraph had been fixed | Moved to 532 |
| F7 | `plans/implemented/34-ota-partitions.md` kept "the task created at" `src/main.cpp:32`, an `#if`, while `xTaskCreate` is at 517; its `Control::sendCommand` citation pointed at a comment instead of the zero-timeout send | Moved to 517 and 605 |
| F8 | `plans/25-multiconnect-ui.md` put the `updateItems` scan-callback guard at `src/FurbleUI.cpp:943-945`, a `BootTimeline` mark; the hand-off is at 9638-9645 | Moved to 9638-9645 |
| F9 | The plan's own coverage sentence still said "1,510 of the citations resolve and 81 get the symbol rule", contradicting its adjacent summary block | Restated with the figures the tool prints |
| F10 | The PR was merged before this review was recorded | Recorded here; the fixes ship as a remediation branch, and this file is the record the merge should have carried |

F1 to F4 and F9 are honesty defects in the gate's and the plan's own numbers,
which is the failure mode the PR exists to prevent. F5 to F8 are exactly the
class of stale citation the PR set out to remove, left in files it edited.

## Gates

```text
python3 tools/check_doc_citations.py
0 errors

python3 -m unittest tests.test_check_doc_citations
Ran 33 tests
OK

python3 tools/pre_review.py --base c644f39e
PASS: 13 gates

sh sim/scripts/check-doc-tokens.sh
sim documentation tokens are present

python3 tools/check_review_threads.py --repo MaxRink/furble --pr 511
review threads: 0 total, all resolved
checks: 24 total, all passed or skipped on head 14cb52e6
```

PR #522 reruns the first four and reports its own CI and review gate on that
pull request. It touches documentation, the gate docstring and its
tests only; no firmware path is in the diff.

## Merge disposition

PR 511 itself is merged: `8113981851554b76c525084e6f493c4c8d67cf96`, squash, by
the author, at 2026-11-26T11:34:55Z, with 24 of 24 checks green and zero
unresolved threads. The review could not gate that merge because the merge
happened first. The fixes above cannot amend a merged squash commit, so they are
prepared as PR #522 against `81139818`, with this file in it, and that PR is held
for its own review rather than merged on the strength of this one.
