# 326 - Review feedback

Status: implemented on master (archived 2026-11-25).

Two pre-merge subagent reviews of `docs/accuracy-refresh` returned CHANGES
REQUIRED: eleven findings against `0f19e2f1`, then eight against the staged
corrections that became `316225a4`. All nineteen are recorded here as fixed,
with the evidence that closed each. None was resolved without a change.

| # | Source | Finding | Resolution | Evidence |
| ---: | :--- | :--- | :--- | :--- |
| 1 | Pre-merge review | The rewritten GPS paragraph listed Platform before Fix Hold and omitted Motion Adaptive, so the row order was wrong. | Fixed. The row list now reads Baud, Update rate, Sentences, Constellation, Power saving, Assisted start, Motion Adaptive, Fix Hold, Extrapolate, Platform, then the three pages. | `src/FurbleUI.cpp:9767-9813`, `docs/img/scroll/default/gps-2.png` |
| 2 | Pre-merge review | Plan 322 cited `include/FurbleBoard.h:37-56` for the 19 fields; the struct runs 36 to 54. | Fixed. The citation is `36-54`. | python scan of the struct body, 19 fields |
| 3 | Pre-merge review | "The committed manifests hold ... 18 linked boundaries" was false provenance: no manifest lists 18, the runner selects 18 ctest cases. | Fixed in all four files. The manifest count ends at 13 memory profile scenarios, and a separate sentence names `sh sim/scripts/run-boundary-conformance.sh` and the 18 ctest cases. | `sim/scripts/run-boundary-conformance.sh:34`, `tests/host/CMakeLists.txt:2785-2802, :2311` |
| 4 | Pre-merge review | `sim/CLAUDE.md` still carried 188 entries and 10 fuzz targets, which this PR replaces elsewhere. | Fixed. The bullet mirrors the new `AGENTS.md` wording. | `sim/CLAUDE.md`, `python3 tools/check_sim_coverage.py` |
| 5 | Pre-merge review | `tests/host/README.md` still said the corpus passes 10 targets, 135 seed runs and 89,145 iterations. | Fixed. It cites the committed artifact with 11, 145 and 124,148. | `sim/coverage/fuzz_results.json` |
| 6 | Pre-merge review | The wiki hardware page still said every M5Stack receiver is AT6668, and had no u-blox rows, while this PR changed the main page. | Fixed. The wiki carries the M5Stack versus u-blox split, the three u-blox rows and the UBX feature paragraph. | `docs/wiki/Supported-Hardware.md`, `docs/supported-hardware.md:78-110` |
| 7 | Pre-merge review | Plan 322 called all six `src/FurbleUI.cpp` switches geometry; two are input wiring. | Fixed. The text names four geometry switches and two input-wiring switches with their lines. | `src/FurbleUI.cpp:843, 2000, 10560, 10668, 11348, 13536` |
| 8 | Pre-merge review | The wiki said the screenshots come from a build with the IR LED or SD slot enabled; the flags are run-time capture switches. | Fixed. The text says the simulator reports the device behind the capture flag, matching the main walkthrough. | `sim/scripts/docs-capture.sh:77-84`, `sim/shim/FurbleSimCaps.h:23-26` |
| 9 | Pre-merge review | `docs/sim-coverage.md` listed ten target areas while the same file now states 11 targets. | Fixed. GPS UBX is added to the list. | `sim/coverage/fuzz_targets.json` |
| 10 | Pre-merge review | "13 memory scenarios" was loose; eight are per-board heap profile boots. | Fixed in all four files: "13 memory profile scenarios". | `sim/coverage/memory_budget.json` |
| 11 | Pre-merge review | The three embedded PNGs were untracked, and the review rows were not recorded. | Fixed. The images are committed and this file records every finding. | `git show --stat`, this file |
| 12 | Second pre-merge review | The eleven fixes were staged but never committed, so HEAD still held the rejected text. | Fixed. A second commit carries the whole change. | `git diff origin/master...HEAD --stat` |
| 13 | Second pre-merge review | Plan 326 said the two hal branches no longer exist. Both refs are still present. | Fixed. The row says both refs still exist and can be deleted. | `git branch -a --list '*hal*'` |
| 14 | Second pre-merge review | Plan 322 phase 4 still said 31 calls to 7 with six geometry switches, contradicting the corrected census above it. | Fixed. Phase 4 now splits four geometry and two input-wiring switches and names the simulator check as the eighth site. | `plans/implemented/322-hardware-abstraction.md:254-259` |
| 15 | Second pre-merge review | Two claim cells in plan 326 overstated the old text: the 31 figure was correct at `e66e8807`, and only one macro lacked the new marker. | Fixed. The cells say the census was a snapshot with no current count, and name the one macro that lacked the marker. | `git grep` at `e66e8807`, `plans/322` at origin/master |
| 16 | Second pre-merge review | The 18-case citation stopped at `tests/host/CMakeLists.txt:2785-2802`, which holds 17; `real-wifi-lifecycle` is at `:2311`. | Fixed in both plan files. | `tests/host/CMakeLists.txt:2311` |
| 17 | Second pre-merge review | Plan 326 described the old wiki text wrongly for the IR and Storage pages; they blamed the default simulator board, not the capture script. | Fixed. The row names the default board lacking the IR LED and SD card, and the splash being replaced before capture. | `docs/wiki/UI-Walkthrough.md` at origin/master |
| 18 | Second pre-merge review | `plans/README.md` had no rows for the two new plan documents. | Fixed. Both rows added next to 325. | `plans/README.md` |
| 19 | Second pre-merge review | The PR body must say the wiki needs a manual push, and this round's findings must be recorded. | Fixed. The PR body says it and this table records the round. | `CLAUDE.md:312-314` |
