# 111 - Review feedback for the idle power fixes

## Scope

This is the review-feedback record for branch `perf/power-idle-fixes`, the plan
111 idle power fixes. Every finding is recorded as fixed, already fixed or a
false positive, with evidence.

## Subagent review (2026-11-14)

An independent subagent review found the code correct apart from the items
below. Item 1 carries the owner's decision.

| # | Source | Finding | Resolution | Evidence |
| ---: | :--- | :--- | :--- | :--- |
| 1 | Subagent review | `boostAdvertising()` ran only after the display-wake gate, so a press with the panel on or dimmed, with Inactivity Never, or in blind-remote mode never reopened the 60 s fast window. The docs said a button wake. | Fixed, owner decision: boost on any physical press. `handleDisplayInput()` boosts on every press edge, including blind-remote and swallowed waking presses, before the unchanged display-wake logic. A held input boosts once. A handled left long press clears its edge bit, because its release skips `handleDisplayInput()`. The companion wakes its task only when running reconnect advertising must change interval; a press inside the window only extends it. README, wiki Home, `docs/companion.md` and `docs/power/ble.md` say any button press. | e2e `companion-boost-press` (panel on, held, blind remote and waking press each count once, with the `companion.boosts` query); `boundary-companion-gatt` (`companion.cadence window_closed=slow press=fast`) |
| 2 | Subagent review | `docs/power/opportunity-matrix.md` P-052 still said reconnect advertising runs at 1000 ms forever. | Fixed. P-052 is marked historical and has an implemented row with the R1 behavior and its tests. | `docs/power/opportunity-matrix.md` |
| 3 | Subagent review | The `TaskWatchdog::subscribe()` doc said "Call once, before its loop", but the disabled companion and WebUI tasks call it again after each blocked wait. | Fixed. The doc says to call it before the loop and again after each `unsubscribe()` wait. | `include/FurbleTaskWatchdog.h` |
| 4 | Subagent review | `compare.py` validated only the compared scenario's opt-out entry, so a bad or stale entry stayed hidden until its scenario ran. | Fixed. Every entry is validated on every comparison and must name a power-gate scenario from `sim/scenarios/manifest.json`. | `tests/test_power_compare.py`: `test_opt_out_validates_every_entry`, `test_opt_out_rejects_unknown_scenarios`, `test_committed_opt_out_is_valid` (33 tests pass) |
| 5 | Subagent review | The legacy quantum in `sim/power_profiler.cpp` used `clockElapsed(last_time_ms, cycle_end_ms)`, which wraps when virtual time advanced inside a UI cycle after the last integration and charges the whole interval as sleep. | Fixed. An interval that starts before the cycle ended counts as zero elapsed, so the quantum is still awake. The power-gate reports are unchanged, so no re-baseline was needed. | `sim-power-profiler` case `legacy-quantum-inside`: 23 ms with 18 ms light sleep; the wrapped value would be 23 |
| 6 | Subagent review | The `gps-hold-bound` comment blamed per-step overhead for a full second, which five 20 ms passes cannot explain. | Fixed. The comment now says the 25 s of waits landed on the bound edge, which the 1 s GPS service tick quantizes, and about 0.1 s of pass overshoot crosses it. Measured on this branch: 25 s remained at 41,085 ms. The master pacing then reads searching at the inclusive check; the new pacing reads held with 2 s left. | `sim/scenarios/e2e/gps-hold-bound.txt` |

## PR #454 (d11088f5)

| # | Source | Finding | Resolution | Evidence |
| ---: | :--- | :--- | :--- | :--- |
| 1 | Copilot review | `src/FurbleUI.cpp:1667`. On touch boards one tap drives the touch indev and a synthesized button indev. `m_PressedInputs` gave each its own bit, so one tap called `boostAdvertising()` twice, against the one-per-press-edge contract of `companion.boosts`. | Fixed. `PressEdge` in `include/FurblePressEdge.h` replaces the per-input bits: a press counts only while no input is held, so the second source of the same tap does not count. Two overlapping presses of different buttons now count once; they reopen the same window. The handled long press still clears its bit through `release()`. A simulator regression is not possible: every simulator binary resolves to a board without the Core2 touch indev (`m_Touch` is created only for Core2 and Tough). | `include/FurblePressEdge.h`, `src/FurbleUI.cpp`, `tests/host/press_edge_test.cpp` (`press-edge`: touch then button and button then touch each count once). The test fails on the per-input-bit rule. e2e `companion-boost-press` keeps its counts of 1 to 4. |
| 2 | Copilot review | `include/FurbleCompanion.h:186`. `m_ReconnectCadence` and `m_ReconnectInterval` are `m_Mutex`-owned, but `include/CLAUDE.md` did not say so. | Fixed. The `FurbleCompanion.h` ownership note lists both with the pairing-window and connection state. Every access in `src/FurbleCompanion.cpp` is under `m_Mutex`: `enable()`, `boostAdvertising()`, `startPairingWindow()`, `startReconnectAdvertising()`, the service task decision and `onDisconnect()`. | `include/CLAUDE.md` |

Validation for PR #454: `sh tools/run_sanitized_host_tests.sh --clean --jobs 4`
passes 185 of 185, `press-edge` included. `press-edge` fails on the old
per-input-bit rule. The plain StickS3 simulator passes all 211 e2e scenarios,
`companion-boost-press` included. `m5stick-s3` builds; size (flash 1,849,456
of 3,145,728) and stack budgets pass. `python3 tools/pre_review.py --base
origin/master` passes 8 gates and `sh sim/scripts/check-doc-tokens.sh`
passes.
