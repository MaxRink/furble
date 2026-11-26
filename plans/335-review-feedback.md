# 335 - Review feedback

An independent adversarial review of the `control` stack cut, run after the
branch's own pre-PR audit and before merge. It rebuilt every env from the
rebased head, re-ran `tools/stack_budget.py` on all thirteen builds, printed
the deepest paths, and tested the walk for the ways a stack budget can be
silently wrong: a call the graph cannot see, a panic path that does not pay the
handler, or a rule that applies to one row and not another.

Two findings are blocking, and the first withdraws the cut. Five are
non-blocking and fixed or recorded. Three of the review's own hypotheses did not
hold and are recorded as false positives so nobody re-runs them.

| # | Severity | Finding | Resolution | Evidence |
| ---: | :--- | :--- | :--- | :--- |
| 1 | Blocking | The walk does not follow the task's main job. `Control::connectAll()` calls `Camera::connect()`, which calls the pure virtual `Camera::_connect()`; the manifest had no rule for that site, so the walk stopped at the wrapper and followed none of the ten vendor implementations. The row is a lower bound that omits 1,536 bytes on the ESP32 release builds and 1,952 on `m5stick-c-debug`. The 5,120-byte stack the cut creates is 960 bytes below the resolved `m5stick-c-debug` need. | Fixed by withdrawing the cut: `control` stays at 8,192 on every env. The manifest now resolves the dispatch, so the gate measures the connect path instead of dropping it. | `git diff` of `tools/stack_budget.json`; resolved path and numbers in [335-stack-cuts.md](335-stack-cuts.md); `lib/furble/Camera.cpp:531` and `:608`. |
| 2 | Blocking | The 22.5 percent headless row and five of six `-debug` rows (17.2 to 22.2 percent) are under the 25 percent line the same branch applies to the `main` cut, and the stated reason for the exception is false: it says the line was set for a walk that is a lower bound while `control` is exact. `control` is a lower bound, measured at 1,536 bytes on the ESP32 release builds. With the dispatch resolved, the same 8,192 stack clears the line on every env without an exception. | Fixed. The exception, the 22.5 percent row and the 512-byte debug-only margin proposal are gone, because the stack they were arguing about is gone. | Finding 1's path; `plans/335-stack-cuts.md` "The 25 percent line"; `plans/314-memory-resilience.md:1852`. |
| 3 | Non-blocking | `docs/stack-budget.md` carried the Waveshare `control` row at 3,536 bytes. #461 pinned that board's M5GFX driver, so the autodetect probe path left the image; the row is stale on master. | Fixed while regenerating the row: 2,704, which a build of this branch reproduces. | Reproduced with `tools/stack_budget.py --env waveshare-s3-eth` on this branch, [335-stack-cuts.md](335-stack-cuts.md). |
| 4 | Non-blocking | One `-debug` row stopped reproducing after the rebase: `m5stack-core-debug` walks 3,568, not the 3,984 the branch recorded. #518 changed that lane's `LOG_LOCAL_LEVEL` to `ESP_LOG_INFO`, which compiles most of the verbose log frames out of the protected build. | Fixed by regenerating the row on the rebased tree. The branch tables carry 3,568 and this record names the cause. | `platformio.ini:150` `-DLOG_LOCAL_LEVEL=ESP_LOG_INFO` after 4bf0b97d; `tools/stack_budget.py --env m5stack-core-debug`, `3568 of 5120`. |
| 5 | Non-blocking | The branch and the PR body called 3,184 "the exact call-graph walk". It is a lower bound: the gate itself reports 92 unresolved indirect sites on that row, and 201 once the connect path is included. | Fixed by wording: plan 335 and `docs/stack-budget.md` call the row a lower bound where the dispatch, the NimBLE cycle or an indirect site bounds it, and the hardware reading stays the evidence. | `tools/stack_budget.py --env m5stick-c` output, `92 unresolved indirect`; resolved, `201 unresolved indirect`. |

Verified, no change needed:

| Check | Result | Evidence |
| :--- | :--- | :--- |
| The panic handler is on every abort path | Holds. The handler is one node added as a callee of `panic_abort`, and `abort`, `__assert_func` and `esp_system_abort` all reach it through the compiled graph. The deepest `control` path on `m5stick-c` ends `multi_heap_free_impl -> assert_valid_block -> abort -> esp_system_abort -> panic_abort -> panic handler (1,216)`. Release builds set `CONFIG_COMPILER_OPTIMIZATION_ASSERTIONS_SILENT`, so the assert path aborts rather than compiling out. | Path print in [335-stack-cuts.md](335-stack-cuts.md); `sdkconfig.m5stick-c:445`; `tools/stack_budget.json` `panic`. |
| Recursion, VLA or alloca on the control path | None. The gate fails a task that reaches an unbounded recursion or a dynamic frame, and `control` reports neither on any of the thirteen envs. The two `dynamic_bounds` entries belong to the I2C driver and `M5.Log`, neither of which the control task calls. | `tools/stack_budget.py` on all thirteen builds; `plans/335-stack-cuts.md` commands table. |
| The `-debug` envs are checked against their own stacks | Holds. `control` has no `debug_stack_bytes`, so a `-debug` env reads the same 8,192 through `sdkconfig.debug`, and the walk reports the protected build's larger frames. That is the row that decides the stack. | `tools/stack_budget.json` task entry; the `-debug` column of the plan table. |
| `main` stays at 16,384 and the walk reads 7,888 | Reproduced on this branch. The cut stays withheld on the hardware reading, which the review did not re-measure. | `tools/stack_budget.py --env m5stick-c`, `main: 7888 of 16384`. |
| The manifest's stack citation still matches the source | Holds. The branch no longer moves the literal, so the `xTaskCreate\(control_task, "control", (\d+),` regex and `tests/test_stack_budget.py` keep agreeing on 8,192. | `tools/stack_budget.json`, `src/main.cpp:517`. |

Judged false, recorded so they are not re-run:

| Hypothesis | Why it does not hold | Evidence |
| :--- | :--- | :--- |
| The 5,120-byte stack already overflows on a release build too | It does not. With the connect path resolved the ESP32 release rows need 4,720, so 5,120 fits with 400 bytes. Only the `-debug` envs exceed it (6,080 on `m5stick-c-debug`). The cut is unsafe anyway, on 7.8 percent of headroom rather than on a hard overflow, and on the `-debug` rows outright. | Resolved table in [335-stack-cuts.md](335-stack-cuts.md). |
| A fault at an arbitrary point in the connect path enters the panic handler on top of the deepest path, so the model still under-counts | On `m5stick-c` the deepest path already ends in an abort and pays the 1,216-byte handler, so the fault-path worst case is the same number. It is not proved for the other envs; that stays a known limit in `docs/stack-budget.md`, where it already was. | Path print: `assert_valid_block -> abort -> panic_abort -> panic handler`. |
| The ISR and double-exception entry points are missing from the budget | They are, but no task stack pays them: an ISR runs on the interrupt stack, and a fault that reaches the handler from the vector code costs the handler plus the exception frame the port overhead already reserves. The documentation says so; the review found no row where it changes a pass or a fail. | `docs/stack-budget.md` "Known limits". |

The branch's own five pre-PR findings are moot where they concern the cut: the
regenerated heap tables, the six `-debug` walks and the 512-byte debug margin
all described a stack this review withdraws. The two rebase conflict
resolutions they record stay valid, and the Waveshare heap pair they corrected
is untouched because nothing moves the derived heap any more.
