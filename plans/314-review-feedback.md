# 314 - Review feedback for memory resilience

## Scope

This is the review-feedback record for plan 314. Each PR in the series adds a
section. Every Copilot or human finding is recorded as fixed, already fixed or
a false positive, with evidence.

## PR 1 (#411, `feat/314-sim-config-parity`)

| # | Finding | Resolution | Evidence |
| ---: | :--- | :--- | :--- |
| 1 | `heap_caps_aligned_alloc` applied the abort policy only to injected failures. A real host `posix_memalign` failure returned NULL into a fallback the device cannot reach. | Fixed. The host failure branch also calls `furble_sim_heap_alloc_failed()`, so it aborts under the board policy. | `sim/shim/esp_heap_caps.h` |
| 2 | The panic runner checked its tokens against the combined log of all fixtures, so one fixture could abort on the wrong allocation and borrow another fixture's tokens. | Fixed. `run_fixture` takes the expected abort line, task and record task, and checks them against that fixture's own run and record logs. A mutated expectation (`size 0x3004`) fails the icon fixture alone. The combined-log tokens remain as coverage evidence. | `sim/scripts/run-panic-fixtures.sh` |

Validation: `sh sim/scripts/run-panic-fixtures.sh` passes 3 of 3 after both
fixes.
