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
