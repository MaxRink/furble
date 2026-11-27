# 326 - Documentation accuracy refresh

Status: documentation only. No code, setting, build or hardware behaviour
changes. Every corrected claim was checked against the file that now holds the
truth, and the review findings are recorded in
[326-review-feedback.md](326-review-feedback.md). Archived to
`plans/implemented/` on 2026-11-25.

Related: [322](322-hardware-abstraction.md) (the HAL status line),
[323](323-plans-refresh-review-feedback.md) (the plans refresh record),
[314](../314-memory-resilience.md) (the memory and fuzz evidence).

## Why

An audit of the reference docs found claims that the code and the committed
evidence contradict. The most expensive class is a stale status line: a reader
believes work is unmerged, or that a gate count is current, and plans around a
tree that no longer exists.

## What was wrong

| Claim | Truth | Evidence |
| :--- | :--- | :--- |
| Plan 322 says hal-1 and hal-2 are implemented but not merged, on branches `refactor/hal-1-drivers` and `refactor/hal-2-interfaces`. | Merged in #462 (`fd51e96c`) on the capability macros and ratchet from #460 (`f4cf0d32`). Both refs still exist and can be deleted. | `git show --stat fd51e96c`, `git branch -a --list '*hal*'` |
| Plan 322's `M5.getBoard()` census is a snapshot at `e66e8807` and gave no current count. | Eight call sites remain: six in `src/FurbleUI.cpp` (four geometry, two input wiring), the profile lookup at `src/FurbleBoard.cpp:83`, and one simulator check. | `grep -rn "M5.getBoard()" src sim` |
| The T-Beam profile row in plan 322 has 18 fields. | `Profile` has 19; the trailing `gpsRail` column was missing, so the row would not compile. | `include/FurbleBoard.h:36-54` |
| The T-Beam steps list `FURBLE_HAS_AXP2101_HAL` without saying it is new, and `FURBLE_PANEL_128X64` as new. | Neither macro exists; both are part of the proposed port. | `grep` over `include/` |
| Plan 323 says DJI Osmo and PENTAX K are missing from the hardware doc. | Both rows are present. | `docs/supported-hardware.md:72-73` |
| The wiki said the boot splash, Infrared and Storage pages had no screenshot: that the default simulator board has no IR LED or SD card, and that the splash is replaced before a script can capture it. | The capture script sets `FURBLE_SIM_IR`, `FURBLE_SIM_SD` and the splash hook, and the images are committed. | `sim/scripts/docs-capture.sh:77-96` |
| The UI walkthrough GPS paragraph repeated a stale fragment and named two live pages. | Three pages exist, and the row order includes Motion Adaptive before Fix Hold and Platform after Extrapolate. | `src/FurbleUI.cpp:9907-9953` |
| The fuzz corpus is quoted as 10 targets, 135 seed runs and 89,145 iterations. | 11 targets, 145 seed runs, 124,148 iterations. | `sim/coverage/fuzz_results.json` `summary` |
| The readiness paragraph cites master `2d2be868` and 188 hardware entries. | The manifests hold 198 modeled hardware entries and 6,912 permutations, and the boundary runner covers 18 ctest cases. | `sim/coverage/hardware_quirks.json`, `permutation_results.json`, `tests/host/CMakeLists.txt:2785-2802, :2311` |
| The hardware doc says every M5Stack GPS receiver is AT6668 so no per-unit protocol code exists, then describes the UBX path. | M5Stack's own receivers are AT6668; a u-blox M10 needs the UBX path. The wiki needed the same correction plus the u-blox rows. | `src/FurbleGPSUbx.cpp`, `lib/furble/protocol/GpsUbx.cpp` |

## What changed

- `plans/implemented/322-hardware-abstraction.md`: merged status, current
  `M5.getBoard()` census, 19-field row, explicit "new macro" wording, UBX stack
  marked merged.
- `plans/implemented/323-plans-refresh-review-feedback.md`: row 14 records the
  removed false note.
- `AGENTS.md`, `CLAUDE.md`, `CONTRIBUTING.md`, `sim/CLAUDE.md`,
  `tests/host/README.md`, `docs/sim-coverage.md`, `docs/sim.md`: the committed
  evidence numbers, with the boundary runner named separately because no
  manifest enumerates it.
- `docs/wiki/UI-Walkthrough.md`: the three screenshots are embedded, and the
  text says what the capture flags do instead of claiming a build variant.
- `docs/wiki/Supported-Hardware.md`: the M10 intro, three u-blox rows and the
  UBX feature paragraph, mirroring `docs/supported-hardware.md`.
- `docs/ui-walkthrough.md`: one GPS paragraph with the real row order.
- `docs/supported-hardware.md`: the M5Stack versus u-blox split.
- `docs/wiki/img/`: the three captured PNGs, byte identical to `docs/img`.

The wiki is published by hand from `docs/wiki/`, so the PR notes that the wiki
needs a push.

## Verification

- `python3 tools/pre_review.py --base origin/master`: 10 of 10 gates.
- `sh sim/scripts/check-doc-tokens.sh`: tokens present.
- Every image compared with `shasum -a 256` against its `docs/img` source and
  opened to confirm it is the page the text names.
- A link scan over the changed markdown files found no missing local target.

No hardware was involved. Nothing in this plan changes a device behaviour.
