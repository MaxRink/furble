# 127 - Review feedback for the port stack

## Scope

This is the review-feedback record for the plan 127 port stack:
`refactor/port-1-ratchet` (coupling ratchet), `refactor/port-2-caps`
(capability macros), `refactor/port-3-routing` (battery and GPS rail
routing) and `refactor/port-4-camera-types` (camera library public types).
Every finding is recorded as fixed, already fixed or a false positive, with
evidence. The follow-ups are new commits on `refactor/port-4-camera-types`;
steps 1 to 3 are not rewritten because other branches build on step 3.

## Independent review of steps 1 to 4

The review found no firmware behavior change.

| # | Source | Finding | Resolution | Evidence |
| ---: | :--- | :--- | :--- | :--- |
| 1 | Subagent review | `check_coupling_ratchet.py --update` rewrites the manifest from the tree, so it also absorbs growth. | Fixed. `--base REV` loads the manifest committed at REV and fails on any per-file count above it; a base without a manifest skips the comparison and an unknown revision fails. pre_review passes its base through. A test grows a file, runs `--update` and still fails against the base. Running it against step 1 exposed a real growth: step 4 had added one ESP type name to `include/FurbleControl.h`. That is fixed too (finding 2). | `tools/check_coupling_ratchet.py`, `tools/pre_review.py`, `tests/test_check_coupling_ratchet.py` |
| 2 | Subagent review, via finding 1 | Step 4 declared `Control::txPower(esp_power_level_t)` in a public header, so `FurbleControl.h` grew from 4 to 5. | Fixed. The mapping is file-local in `FurbleControl.cpp`; boot code calls `Control::savedTxPower()`. The manifest entry is 4 again and the public header total is 345. `--base` against steps 1, 2 and 3 is clean. | `include/FurbleControl.h`, `src/FurbleControl.cpp`, `src/main.cpp`, `sim/main.cpp` |
| 3 | Subagent review | Board guards were counted per line, so widening `#if A` to `#if A \|\| B`, a backslash continuation or a `#define` alias did not count as growth. | Fixed. Continuations are joined, each board macro on an `#if`, `#ifdef`, `#ifndef` or `#elif` line counts, and a `#define` naming a board macro counts once. Tests cover a widened `\|\|`, a continuation and an alias. The current manifest is unchanged at 26, as the reviewer said. On the step 1 tree the stricter count would have been higher (`FurbleUI.cpp` 41 to 49, `FurbleUI.h` 3 to 5, `FurbleSD.cpp` 3 to 4); step 2 removed those multi-board lines. | `tools/check_coupling_ratchet.py`, `tests/test_check_coupling_ratchet.py` |
| 4 | Subagent review | The capability misuse scan skipped `sim/` and the host doubles, and a misspelled macro such as `FURBLE_HAS_SDD` evaluated silently to 0. | Fixed. The scan covers `sim/` and `tests/host/`, and any `FURBLE_HAS_*` or `FURBLE_PANEL_*` name not `#define`d in `include/FurbleBoardCaps.h` fails. Both have tests. The real tree is clean. | `tools/check_coupling_ratchet.py`, `tests/test_check_coupling_ratchet.py` |
| 5 | Subagent review | `include/CLAUDE.md`, `sim/CLAUDE.md` and `tests/host/CLAUDE.md` did not record the new seams. | Fixed. One bullet each for the FurbleBoardCaps.h contract and the power mapping, the simulator battery gating and BLE init, and the moved host doubles. | `include/CLAUDE.md`, `sim/CLAUDE.md`, `tests/host/CLAUDE.md` |
| 6 | Subagent review | Plan 287 section 2.3 and its implementation state still described the header type leak as open. | Fixed. Both point to plan 127 and mark the leak removed. | `plans/287-mcu-portability-and-bundle-hardware.md` |
| 7 | Subagent review | Plan 127 said 43 host tests changed for `TxPower`. | Fixed. 45 sources: 44 test files plus `tests/host/boundaries/companion_gatt_transport_test.cpp`. | `git show 45b15000 --name-only` |
| 8 | Subagent review | Step 2 equivalence had no build evidence. | Fixed. All six release envs, `esp32-s3-headless` and `waveshare-s3-eth-debug` were built at steps 1 and 2. The images differ by up to 1,264 bytes, all from Xtensa longcall relaxation and literal removal, shown in untouched NimBLE code; data differs only by alignment. A step 1 rebuild was byte-identical. | Plan 127, step 2 build evidence |
| 9 | Subagent review | The `/** Refresh the setting from NVS. */` comment in `src/FurbleGPS.cpp` sat above `setRailPower()`. | Fixed. It is back above `reloadSetting()`. | `src/FurbleGPS.cpp` |
| 10 | Subagent review | The `battery_status_t` comment described per-field gating, but the code gates level, voltage, current and charging together. | Fixed. Comment only; behavior unchanged. | `include/FurblePlatform.h` |
| 11 | Subagent review | The simulator battery status gating uses `Sim::hwGauge*()`, which follow the runtime battery state, unlike the board-static firmware capabilities. | Fixed in the docs. Plan 127 and `sim/CLAUDE.md` state it, and that no scenario asserts the companion battery fields. | `plans/127-nordic-port-baseline.md`, `sim/CLAUDE.md` |
| 12 | Subagent review | No test pinned the power mapping or the Ricoh IO capability. | Fixed. `tx-power-mapping` checks P3, P6 and P9 reach the controller as 3, 6 and 9 dBm through `Control::setPower()`, that an unsupported level falls back to 3 and that the saved setting maps to `TxPower::P3`. The NimBLE mock records `setSecurityIOCap()`, and `ricoh-focus` checks the connect passes `BLE_HS_IO_KEYBOARD_DISPLAY` and then restores `BLE_HS_IO_DISPLAY_YESNO`. | `tests/host/tx_power_mapping_test.cpp`, `tests/host/ricoh_focus_test.cpp`, `lib/testing/nimble/MockNimBLE.*` |
