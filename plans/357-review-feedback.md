# 357 - Retrospective review of seven merged pull requests

Status: review record and the fixes it produced. Every finding is listed against
the pull request it belongs to, with what held and what was wrong, and the change
that closes it. Nothing here reopens a merged commit: the fixes land in one
follow-up pull request, and every behaviour change carries a test.

Reviewed: #471, #472, #478, #492, #495, #503, #506 and #517. Each was read at
its merge commit, and each claim was checked against the tree that commit
produced, not against the branch. Vendor documents were re-fetched and hashed
against the SHA-256 the plans record, so a page citation is checked against the
same copy the original read.

Method: for each pull request, take the artefact the change claims (a count, a
gate number, a page, a compiler rule, a link line) and try to falsify it. Run
the gate the change modifies, and mutate the change to see whether the new test
notices. A claim that survives is recorded as held; a claim that does not is a
finding, and the smallest change that closes it goes in the follow-up.

| PR | Verdict | Findings |
| --- | --- | --- |
| #471 | Held. Every corrected count and gate number is true of `92618bff`. | 1 minor: the closeout status line it did not sweep |
| #472 | Held on behaviour. No set reports more than the board has; the caps gate every pool the getters touch. | 2 wording: a missing over-report class, and a wrong reason for one matching rule |
| #478 | **Fail open** on a pull request that broadens its own workflow filter. | 1 behaviour (fixed with a test), 1 status line |
| #492 | Held. The four scenarios are certified and run in CI; no floor value moved. | 1 documentation: a stale reason for one unwritten floor |
| #495 | Held. All three page citations are exact against the hashed documents. | 1 wrong fact: two board FCC IDs were reported missing |
| #503 | **Wrong when it landed.** It recorded the CMake abort as an open defect, and the tree it merged into already carried the #506 fix. | 1 documentation (fixed by #517) |
| #506 | Held. The realloc and calloc semantics match ESP-IDF; the SDL fix is the root cause, not a patch over it. | 1 behaviour: one `heap_caps_get_info` field ignored the pool |
| #517 | Held. The correction is complete. | none |

## PR 471 - documentation accuracy refresh (`92618bff`)

Attacked: every number and status line the pull request corrects, read against
the merge tree, plus a scan for the same stale numbers in files it did not
touch.

Held, with the command or file that settles each one:

| Claim in the change | Verified against |
| --- | --- |
| 11 fuzz targets, 145 seed runs, 124,148 iterations | `sim/coverage/fuzz_results.json` `summary`: 11, 145, 124148, `status: pass` |
| 198 modeled hardware entries | `sim/coverage/hardware_quirks.json`: 198 entries, all `status: modeled` |
| 20 crash entries | `sim/coverage/crashes.json`: 20 |
| 6,912 permutations | `sim/coverage/permutations.json` `expected_count: 6912` |
| 13 memory profile scenarios | `sim/coverage/memory_budget.json`: 13 scenario keys |
| 18 linked boundary ctest cases | `tests/host/CMakeLists.txt` expands 4 `boundary-ir` plus 4 `boundary-sd` plus 5 `boundary-feedback` plus four singles plus `real-wifi-lifecycle`, which is 18, and `sim/scripts/run-boundary-conformance.sh` selects exactly `^(boundary-|real-wifi-lifecycle$)` |
| Eight `M5.getBoard()` call sites, six in `src/FurbleUI.cpp` | `git grep -n "M5.getBoard()" 92618bff -- src sim`: `src/FurbleUI.cpp` 843, 2000, 10560, 10668, 11348, 13536, `src/FurbleBoard.cpp:83`, `sim/FurblePlatformSim.cpp:289` |
| `Profile` has 19 columns and the T-Beam row now compiles | `include/FurbleBoard.h:35-54`: 19 fields, `gpsRail` last; the plan's new row has 19 initialisers |
| The citation `include/FurbleBoard.h:36-54` | field list runs 36 to 54, with `struct Profile {` on 35 |
| hal-1 and hal-2 merged in #462 on the #460 macros | `fd51e96c` is #462, `f4cf0d32` is #460; both are ancestors of `92618bff` |
| PENTAX K and DJI Osmo rows exist | `docs/supported-hardware.md:72-73`, `docs/wiki/Supported-Hardware.md:41-42` |
| The three wiki images are the captured pages | `git ls-tree`: `settings-infrared.png` and `settings-storage.png` are the same blob as `docs/img/`, and `boot-splash.png` matches `docs/img/boot-splash.png` and `docs/img/s3/boot-splash.png` |
| GPS row order and "GPS Baud (Auto, 9600, or 115200)" | `src/FurbleUI.cpp:9717-9813` in `addGPSMenu`, and `m_GPSBaudOptions` is `"Auto\n9600\n115200"` at `include/FurbleUI.h:855` |
| ten pre-review gates | `tools/pre_review.py` at `92618bff` registers 9 fixed gates plus the scanner unit tests |

Finding: `plans/313-sim-only-status.md` still opened with "Status: master is at
`e66e8807` (#439) on 2026-11-13", which was false on `92618bff`, and
`plans/CLAUDE.md` calls that file the current sim-only closeout state. This pull
request's own subject is stale status lines, and it swept plan 322 and plan 323
but not this file. Fixed by dating the sentence and pointing at the newer counts;
no number in the table changed, because those numbers are honest for
`2d2be868`.

## PR 472 - the heap capability set (`6bf03a70`)

Attacked: whether the model can now answer with more memory than the board has,
and whether every pool access is gated by the capability set.

Measured, with both pools seeded 60,000 internal and 1,000,000 PSRAM, so the
seeded total is 1,060,000. `furble_sim_heap_free()` for every single bit and for
`MALLOC_CAP_SPIRAM | MALLOC_CAP_INTERNAL`:

| Capability set | Model | Device |
| --- | ---: | --- |
| `MALLOC_CAP_DEFAULT`, `8BIT`, `32BIT`, empty set | 1,060,000 | 1,060,000: those bits are on both heaps |
| `MALLOC_CAP_INTERNAL`, `DMA` | 60,000 | 60,000 |
| `MALLOC_CAP_SPIRAM` | 1,000,000 | 1,000,000 |
| `MALLOC_CAP_SPIRAM \| MALLOC_CAP_INTERNAL` | 1,000,000 | 0, no heap carries both |
| `MALLOC_CAP_EXEC`, `IRAM_8BIT`, `TCM` | 60,000 | 0 under `CONFIG_ESP_SYSTEM_MEMPROT_FEATURE` |
| `MALLOC_CAP_RETENTION` | 60,000 | DIRAM's free size |
| `MALLOC_CAP_RTCRAM` | 60,000 | the 8 KiB RTC region's free size |

So the answer to the first question is narrower than the title suggests and
wider than the change says. No query can exceed the seeded total, because the
largest sum is internal plus PSRAM and nothing else is seeded. What can be wrong
is which pool answers:

- `MALLOC_CAP_SPIRAM` with `MALLOC_CAP_INTERNAL` resolves to PSRAM here and to
  zero on the device. Disclosed and pinned by a test.
- `MALLOC_CAP_RETENTION` answers from the merged internal pool where the device
  answers from DIRAM. Disclosed.
- `MALLOC_CAP_RTCRAM` does the same, where the device answers from its own
  8 KiB region (`memory_layout.c:66`, `:106`). **Not disclosed.** That is a
  finding: the section says "two ways" and there are three.
- `MALLOC_CAP_EXEC`, `MALLOC_CAP_IRAM_8BIT`, `MALLOC_CAP_TCM`,
  `MALLOC_CAP_CACHE_ALIGNED`, `MALLOC_CAP_DMA_DESC_*`, `MALLOC_CAP_INVALID` and
  `MALLOC_CAP_PID2..7` all resolve to a pool here and to zero on the device.
  Disclosed. `grep -rn MALLOC_CAP_ src include lib` finds no caller of any of
  them, so "no caller uses any of them today" holds.

The answer to the second question is yes for every path the change touches:
`furble_sim_heap_free`, `_largest`, `_minimum`, `_fits` and the counted
allocation all run through `capsPools()`, and `heap_caps_malloc`,
`_calloc`, `_realloc` and `_malloc_prefer` compose on top of them. The one
exception is `heap_caps_get_info`, added later by #506, and that is recorded
under #506 below.

Second finding, wording: the change explains `capsPools()` from the
allocation-priority table and says SPIRAM carries `MALLOC_CAP_DEFAULT` "only at
priority 2". The table says that, but `heap_caps_get_free_size()` does not use
the table: `heap_caps_match()` tests the OR of a heap's whole prioritised set
(`heap_private.h` `get_ored_caps`, used at `heap_caps.c:73`), and `esp_psram.c:470`
puts `MALLOC_CAP_SPIRAM | MALLOC_CAP_DEFAULT` in PSRAM's first slot. The model's
answer is right; the reason given for it is not the device's mechanism. Both
points are now in the code comment, `sim/CLAUDE.md` and plan 314.

Behaviour is unchanged by this review, so no test changed for it.

## PR 478 - merge gate reads base workflow filters (`b57f0118`)

Attacked: the case the review asked for, a pull request that changes a workflow
filter, and the fail-closed cases the plan lists.

Held: the base read is real. `run_base_ref_oid` takes `baseRefOid` from a
GraphQL query, `fetch_workflows` reads the directory and every file through the
contents API at that sha, and each fail-closed case the plan lists has a test.
The mutation is genuine too: patching `read_local_workflows` to raise inside
`run_override` proves the gate does not touch the checkout.

Wrong: the override still accepts a pull request that edits a workflow. The
#450 item 11 finding was that a pull request editing `.github/workflows` could
broaden its filters and pass during an outage. #478 removed that guard on the
argument that a pull request cannot edit the commit it targets. That argument
covers the stale-clone direction only. A pull request that broadens its own
filter is invisible at the base, and GitHub evaluates the pull request's own
copy of a workflow for its `pull_request` run, so such a head should have had
checks.

Reproduction, offline, from the tool's own test harness: stub the base to a
`main.yml` that covers `src/**`, change `web-installer/index.html` and
`.github/workflows/main.yml`, and pass `--allow-no-checks`. Before the fix the
gate exits 0 and prints "accepted". After it, exit 1 naming the workflow.

How it survived: on this repository the case is refused today only because
`pre-review.yml` lists `.github/workflows/**` in its `paths`, and
`tools/check_ci_workflows.py` does not require that entry, so one edit to that
list reopens the hole. `test_override_holds_when_the_pr_broadens_its_own_workflow_copy`
recorded the acceptance as intended behaviour.

Fixed in this pull request: the #450 guard is back, beside the base read rather
than instead of it, so a changed path that is `.github/workflows/` or below
refuses the override and names the files. Two tests replace the one that
encoded the acceptance, and the suite runs 41 tests offline. With the guard
removed again, three cases exit 0 where they must exit 1. `AGENTS.md`,
`CLAUDE.md`, `CONTRIBUTING.md`, the tool docstring, plan 331 and the note on
`plans/314-review-feedback.md` row 11 describe the rule as it now is.

Second finding, wording: plan 331's status line said "implemented on
`fix/464-base-workflows`" after the merge, which reads as an unmerged branch.
Fixed to name #478 and `b57f0118`.

## PR 492 - certify the MQTT memory scenarios (`9b404837`)

Attacked: whether the four scenarios are really certified, whether any floor
moved, and what the coverage tool counts.

Held. The four entries are `certified: true` with `transport: mqtt`
(`sim/scenarios/manifest.json`), no scenario file changed, so no assertion was
weakened. They are not merely labelled: `tools/coverage.py` selects them through
`--list-certified --suite e2e --board m5stick-s3 --transport mqtt`, runs them
from the `sim-mqtt` stack, and `incomplete_scenarios()` fails the stack on a
scenario that does not finish, so the coverage job is their gate.
`sim/scripts/run-memory.sh` reads the same selection and fails on an empty one.
No floor value moved: `tests/coverage_floor.json` is byte-identical to its
pre-change content, and the union rows the first commit expected to need
`--lower` came in at 62.51 and 79.90 against 50.66 and 69.26.

The coverage tool does not count lines it cannot execute: `parse_lcov` reads
`DA:` records, and `llvm-cov` only emits those for instrumented lines of
compiled code. In this stack that means `src/FurbleMQTT.cpp`'s 1,727 lines,
because `FURBLE_MQTT` is what makes the translation unit non-empty. What the
number cannot say is how much of that file the simulator can reach at all, and
the plan states the limit.

Finding, documentation: item 2 of "Remaining gaps" says the MQTT stack floor is
unwritten because "the stack floor needs its own ratchet", and item 1 of the
same list says the `src/FurbleMQTT.cpp` floor would fail partial runs. The
second reason is the real one, for both: `floor_failures()` fails a floor entry
the run did not measure, so a floor on a stack that `--stack host` skips is a
guaranteed red. The first reason is wrong, and so is the comment on
`CommittedFloorTest.test_shape`, which says "the number has to come from the
clang-18 run": the plan's own evidence table is that clang-18 run, 4,915 of
28,651, and it reproduces the local figure exactly. Fixed by naming the real
constraint in both places and recording that 16.15 is available and blocked by
the tool, not by a compiler.

Second finding, wording: the status line said "implemented on
`test/sim-memory-certify`" after the merge. Fixed.

## PR 495 - SoC, radio and FCC pages (`c36ab12e`)

Attacked: the page citations, then the FCC claim's scope.

Held, and this is the strongest part of the change. The vendor documents were
re-fetched and hashed against the SHA-256 in `plans/378` before any page was
read, so these are the same copies the original pass read:

- `DS_SX1261_2_V2-2.pdf` `6d783125...4306c2`, 118 pages. PDF page 56 is Table
  8-2 with the switching times the plan quotes, value for value: 3500, 340, 31,
  50, 83, 126, 40, 105, 62, 41, 76, 15, 92. Section 15 starts on page 109
  (15.1), 15.2 is on 110, and 15.3 and 15.4 are on 111, so "section 15 runs to
  page 111, not page 110" is exact. 8.1 is on 53, Table 8-1 and 8.2.2 on 54,
  8.3.1 on 55, 13.1.1 on 72.
- `esp32-s3_technical_reference_manual_en.pdf` `4484bf8a...3f444f`. 26.3 opens
  on page 925, which holds a figure and no body text, and the three-clock-source
  sentence is the first paragraph of page 926, word for word as quoted. 37.2
  Features is on 1415 with "Four TX channels", "Four RX channels" and "Eight
  channels share a 384 x 32-bit RAM"; 37.1 on the same page names channels 0 to 3
  as TX and 4 to 7 as RX; 37.3.2.2 "Use of RAM" is on 1417 with "The RAM is
  divided into eight 48 x 32-bit blocks"; `UART_SCLK_SEL` on 961 reads
  "1: APB_CLK; 2: RC_FAST_CLK; 3: XTAL_CLK".

Wrong: the FCC section reported no board-level filing for the StickC family, and
the plan stated outright that the family has none. Two exist:

| FCC ID | Device | Original grant | Grantee |
| --- | --- | --- | --- |
| 2AN3WM5STICKC | M5Stick | 2019-06-04 | M5Stack Technology Co.,Ltd |
| 2AN3WM5STICKCPLUS | M5StickC-Plus SE | 2026-01-23 | M5Stack Technology Co.,Ltd |

The original pass looked only at the vendor-hosted certification files and
stopped at the FCC databases, which returned HTTP 403. That 403 is on the search
endpoint; a per-ID page is served, which is how these two were read on
2026-11-26. The same read confirms the four IDs the plan already had and dates
them to their original grants, not to later permissive changes: 2AN3WM5STICKS3
2025-12-16, 2AN3WM5STACK-CORE2 2020-08-20 (a class II change follows on
2023-01-10), 2AN3W-M5STACK-TOUGH 2021-04-19, 2AC7Z-ESPS3WROOM1 2022.

The Waveshare claim is scoped too widely for its evidence.
`docs/power/datasheet-register.md` said "no board-level filing" and the summary
said "the Waveshare board is recorded as having no board-level filing", but the
evidence is that the ESP32-S3-ETH product page never says FCC. Silence on a
product page is not a database search, and the search endpoint is blocked, so
the row is a not-found. Fixed to say so, with the method's limit recorded.

Not changed, and why: the Plus2, the Core Basic, the Waveshare board and the
headless board keep a not-found status. Only IDs the naming scheme made
guessable could be checked without a working search, and `2AN3WM5STICKCPLUS2`,
`2AN3WM5STACKBASIC`, `2AN3WCORE` and `2AN3WM5STACKC` return no exact ID.

## PR 503 - the CMake entry point finding (`588e2d36`)

Attacked: whether the plan described a defect as current when it was already
fixed, in the tree #503 landed on.

Wrong, and this is the finding the review was asked to construct. #506 merged at
06:48 and #503 at 07:38 on the same day, so #503's merge commit already
contained the fix: `git merge-base --is-ancestor b549d36c 588e2d36` is true. The
two sentences #503 landed were therefore false on their own tree:

- `docs/sim.md`: "`sim/CMakeLists.txt` exists, but the binary it builds does not
  run: on a stock scenario it aborts in `lgfx::v1::Panel_sdl::_event_proc()` on
  unmodified master, where the `sim/build.sh` binary runs the same script, so
  the two paths are not equivalent."
- `plans/355`: "State: finding recorded, build path unfixed."

Both were written in the present tense about master. The plan does name the tree
its reproduction ran on, `a5cb2e14`, but neither the status line nor the docs
paragraph quoted the tree it landed on.

This is not a wording preference. The plan is the only record of the abort, and
a reader deciding whether to trust `cmake -S sim` would have concluded, wrongly,
that the documented entry point was still broken.

Fixed by #517, which rewrote both to "State: resolved", named #506 and
`b549d36c`, kept the pre-fix reproduction labelled as the pre-fix tree, and
described what remains open. Nothing further was needed here, and this follow-up
changes nothing for #503.

## PR 506 - heap_caps shims and the SDL link order (`b549d36c`)

Attacked: the three questions the review asked, against the pinned sources.

`heap_caps_realloc` on failure: the shim allocates the new block first, returns
NULL when that fails, and reaches neither the copy nor the free, so the old
block is untouched. ESP-IDF's `heap_caps_realloc_base` does the same
(`heap_caps_base.c`: the fallback allocates, and the copy and free are inside
`if (new_p != NULL)`). Under
`CONFIG_HEAP_ABORT_WHEN_ALLOCATION_FAILS`, which every board sets, the caller
never sees the NULL: the failed-alloc hook aborts first, which is why the
scenario-level test observes the abort and not the block. That is what hardware
does too, so the model is right and the guarantee is unobservable rather than
untested.

`heap_caps_calloc` on an overflowing count: `heap_caps_base.c`
`heap_caps_calloc_base` rejects `size != 0 && n > SIZE_MAX / size` by returning
NULL, and `heap_caps_calloc` reports `n * size` through the failed-alloc hook,
which aborts. The shim rejects the same condition, reports the same wrapped
total and aborts through the same policy. Same behaviour, same size in the log.

The SDL fix is justified, not a patch over the symptom. The change moves one
flag from `target_link_options` to `target_link_libraries`, which changes the
order ld64 sees and therefore which dylib binds `SDL_PollEvent`. That is a
root-cause fix: the alternative would suppress the stack check or enlarge a
buffer that has the wrong size only because the wrong library wrote it. I
configured `sim/` on macOS and read the generated link line, which is
`libSDL2-2.0.0.dylib -framework Cocoa -Wl,-needed_library,.../libSDL3.dylib ...`,
so SDL2 precedes SDL3 exactly as `sim/build.sh` orders the two.

Finding, behaviour: `furble_sim_heap_info()` took `minimum_free_bytes` from the
internal pool whichever pool the caps resolved. `heap_caps_get_info(&info,
MALLOC_CAP_SPIRAM).minimum_free_bytes` therefore returned the internal
watermark while `heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM)` returned
the PSRAM one, and the comment above the function claimed the field matched the
getter "for every caps set". ESP-IDF sums the matching heaps and each reports
its own `multi_heap_get_info()` watermark. Fixed by selecting
`heapPsramMinimumFree()` for an external read, which the model already had from
#472. `sim_heap_caps_shim_test` now asserts the field against the getter for
both pools and that the two differ; with the fix reverted the shim test fails
two checks.

## PR 517 - correct the CMake entry point status (`3284b617`)

Attacked: whether the correction is complete, and whether anything still
describes the abort as current.

Held, and complete. `plans/355` now says "State: resolved", names #506 and
`b549d36c`, keeps the pre-fix reproduction labelled as the pre-fix tree, and
states what remains open. The `docs/sim.md` paragraph was the remaining place
that read as current, and it now says the target builds and runs "again since
the #506 link-order fix" and that no workflow builds or runs it. A repository
wide search for the abort wording, for `134` and for `__stack_chk_fail` finds
`plans/337`, which says the abort was found and fixed there with both `nm -m`
bindings, and no doc that still claims a current abort. `plans/README.md`'s row
matches the plan. The two claims that remain in the present tense, the LVGL
duplication and the extra `LC_RPATH`, are differences that still exist.

Finding: none.

## What this follow-up changes

| File | Change |
| --- | --- |
| `tools/check_review_threads.py` | refuses `--allow-no-checks` when a changed path is `.github/workflows/` or below, beside the base-ref read |
| `tests/test_check_review_threads.py` | two tests replace the one that encoded the fail-open case |
| `AGENTS.md`, `CLAUDE.md`, `CONTRIBUTING.md` | the guard is part of the rule again |
| `sim/heap_model.cpp` | `heap_caps_get_info` watermark follows the resolved pool; the cap-comment records the third over-report and the real matching rule |
| `tests/host/sim_heap_caps_shim_test.cpp` | pins the info watermark against the per-caps getter for both pools |
| `sim/CLAUDE.md` | the same two heap corrections |
| `docs/power/datasheet-register.md` | the two StickC FCC IDs; the Waveshare row scoped to a not-found |
| `plans/331`, `plans/337`, `plans/356`, `plans/314`, `plans/378`, `plans/313`, `plans/314-review-feedback.md` | the findings above, each with its evidence, and the three stale status lines |
| `plans/355`, `docs/sim.md` | no change: #517 already corrected both, and this review confirms it |

No merged history was rewritten. No hardware was involved.

## Gates

- `python3 tools/pre_review.py --base origin/master`.
- `python3 -m unittest tests.test_check_review_threads`: 41 tests.
- `cmake --build` of `sim_heap_caps_test` and `sim_heap_caps_shim_test`, run
  through `ctest -R sim-heap-caps`.
- `python3 -m unittest tests.test_coverage_tooling`.
- `sh sim/scripts/check-doc-tokens.sh`.
