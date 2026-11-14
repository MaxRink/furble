# 173 - Review feedback for the #283 and #289 follow-up

## Scope

This is the review-feedback record for plan 173's 2026-11-13 follow-up on
`fix/283-289-sim-scheduler`. That branch carries the environment growth
guard for issue #283, the SDL3 rpath for sanitized macOS builds, and the
issue #289 load proof. Every finding is recorded as fixed, already fixed or a
false positive, with evidence.

## Subagent review of `fix/283-289-sim-scheduler`

| # | Source | Finding | Resolution | Evidence |
| ---: | :--- | :--- | :--- | :--- |
| 1 | Subagent review | The master `TZ` growth was described as a live race. `Timezone::apply` runs in `runSimulator()` before `panelReady.store(release)`, and until then the main thread only waits on `panelReady.load(acquire)`. No other thread walks `environ`: the M5GFX `dbg` thread is suppressed, and the watchdog reads `getenv` once in `watchdogStart()`. | Fixed. Verified in `sim/main.cpp`: `Timezone::apply` precedes `panelReady.store`, and the main thread sleeps on the flag before `Panel_sdl::loop()`. The probe's 24 main-thread calls all came from `SDL_Init`. The `sim/main.cpp` comment, plan 173, `docs/sim.md`, `plans/95`, the README #283 row and `sim/CLAUDE.md` now say the growth is ordered and was not a live race. `main()` still creates `TZ` first, so the guard can enforce the stricter rule: no growth after `SDL_Init`. The historical root cause, the pre-`a57e77e7` `RESTART_STEP` growth in the middle of the loop, is unchanged. Commit `feb22632`'s message still calls it a live race; this record supersedes it. | `sim/main.cpp`, `plans/173-sim-scheduler-visibility.md`, `docs/sim.md`, `plans/95-engineering-lessons.md`, `plans/README.md`, `sim/CLAUDE.md` |
| 2 | Subagent review | In `CONTRIBUTING.md` the new rpath and `run-env-order.sh` sentences split "`sim/lsan.supp` and `sim/ubsan.supp`." from "Those files list only...", so "Those files" referred to the wrong thing. One line also ran past 80 columns. | Fixed. The two sentences now follow "...each with its reason.", and the paragraph is rewrapped. | `CONTRIBUTING.md` |
| 3 | Subagent review | The guard comment said removing a variable can reallocate `environ`. glibc `unsetenv` shifts entries in place and never reallocates. | Fixed. The comment now says removal shifts the array in place without a reader lock, so a concurrent walk can miss an entry, and that it is rejected for that reason. | `sim/env_mutation_guard.c` |
| 4 | Subagent review | The guard did not hook `putenv` or `clearenv`, so "never grow the environment" was not fully enforced. | Fixed. `putenv` takes the name up to `=` and is checked like `setenv`, or like `unsetenv` when there is no `=`. `clearenv` is rejected after `SDL_Init` under any target. | `sim/env_mutation_guard.c` |
| 5 | Subagent review | Nothing proved the growth branch fires: the positive legs would also pass if it never rejected anything. | Fixed. `run-env-order.sh` compiles a small program that calls `SDL_Init(SDL_INIT_EVENTS)` and then adds `FURBLE_GUARD_SELFTEST`. The program must exit 86 with the exact guard message before any simulator leg runs. This runs in the Linux sim-e2e job. | `sim/scripts/run-env-order.sh` |
| 6 | Subagent review | `sim/CLAUDE.md` had a line of about 110 columns in the sanitizer bullet, and plan 173 had an overlong line in the `TZ` paragraph. | Fixed. The macOS SDL3 text is its own bullet, and both passages are rewrapped. | `sim/CLAUDE.md`, `plans/173-sim-scheduler-visibility.md` |
| 7 | Subagent review | Record every finding with its resolution and index the record. | Fixed. This file, indexed in `plans/README.md`. | `plans/173-review-feedback.md`, `plans/README.md` |

## #289 follow-up: independent review of `fix/289-zombie-wait`

The review found the core fix correct. Every finding below is fixed in this
branch.

| # | Source | Finding | Resolution | Evidence |
| ---: | :--- | :--- | :--- | :--- |
| 1 | Independent review | The trace sends the watchdog dump signal, but the handler was installed only after `watchdogStart()` returned early for `FURBLE_SIM_WATCHDOG_SECONDS=0`. The default action of that signal ended the process (probe rc 159). | Fixed. `installDumpHandlerOnce()` uses `std::call_once`. `watchdogStart()` calls it before the bound check, and `watchdogDumpThread()` calls it too. | A host probe that calls `watchdogDumpThread` with the bound at 0 exits 159 on the old `sim/watchdog.cpp` and 0 on the new one, with or without `watchdogStart()`. In a container, a `FURBLE_SIM_NO_SCHED_MUTEX` build with the trace on and the bound at 0 ran `cancel-sweep-reconnect-entry` to rc 0 with 36 thread dumps. |
| 2 | Independent review | The handoff trace dumped `nextRunnableTaskLocked()`, the best ready candidate, not the turn holder. | Fixed. It traces `runningTask`, falling back to the next ready task when nothing holds the turn. Plan 173 now says the pass 3 build traced the best ready task. | Container run: every handoff event names the task with `running=yes`. `plans/173-sim-scheduler-visibility.md` |
| 3 | Independent review | `traceTurnLossLocked` built a `std::string` on device threads without a `HostAllocScope`, so it charged the modelled heap. The thread dump also polled up to 500 ms while holding the scheduler lock. | Fixed. `snapshotTurnLossLocked()` builds the text in a `HostAllocScope` under the lock. `emitTurnLoss()` prints and dumps after the lock is released, also in a host scope. The docs now say the trace changes no scheduling decision but costs host time. | `sim/freertos.cpp`, `docs/sim.md`. The container run above completed with the dumps outside the lock. |
| 4 | Independent review | `tests/test_sim_wait_visibility.py` could be bypassed in four ways: `#if defined(FURBLE_SIM) \|\| defined(X)`, a bare `condition_variable` after `using namespace std;`, a simulator alias placed anywhere (even in `#if 0`), and a `'"'` character literal. | Fixed. The positive directive regex is anchored to the whole directive and allows only `&&` terms. The use regex no longer requires `std::`. A simulator alias counts only when it is live and inside a positive `FURBLE_SIM` frame, and `#if 0` blocks are dead code. Character literals are skipped, and a digit separator is not taken for one. | Each bypass has a test case. All four return no finding on the old scanner, and all 18 tests pass on the new one. |
| 5 | Independent review | Three statements were inaccurate. `docs/sim.md` said a healthy run prints nothing, which contradicts the recorded handoff expiries. `plans/README.md` put all 6,000 runs at loadavg 81.5. Plan 173 said "within 80 ms", but the max of 18640 against a p50 of 18555 is 85 ms. | Fixed. The docs say a handoff expiry under load can be wake latency. The README says only the last 3,600 runs were at 81.5. Plan 173 says 85 ms. | `docs/sim.md`, `plans/README.md`, `plans/173-sim-scheduler-visibility.md` |
| 6 | Independent review (optional) | `notifyLocked` should skip stopping waiters, as `SchedulerMutex::releaseLocked` does. | Fixed. A stopping waiter is skipped and unlinks itself as it unwinds, so a `notify_one` reaches a waiter that can use it. | `sim/freertos.cpp` |
| 7 | Independent review (optional) | Add scheduler tests for cancel and teardown, notify before wait, a host-thread waiter, and a timing bound on the #289 case. | Fixed. `sim-scheduler` now covers notify before wait (no block, one wake), cancelling one of two waiters (the survivor gets the `notify_one`), a host-thread waiter released by a task, a host-thread waiter cancelled by `schedulerStop()`, and the #289 handoff finishing in under 1000 ms against the 2 s breaker. | 10 of 10 local runs. Sanitized host ctest, below. |
| 8 | Independent review (optional) | Record `Scan::m_DispatchMutex`, held by `stop()` across the visible wait, and `Camera::m_PairingMutex`, held across client operations that NimBLE callbacks re-enter, as known shapes. | Fixed. Plan 173 "What is still not covered" lists both as known shapes not measured as defects, to convert if a trace ever shows a park on either. | `plans/173-sim-scheduler-visibility.md` |

Gate note, not a review finding. The final sanitized host ctest on this
branch passed 182 of 183 tests. `control-e2e-multi-connect-fujifilm` aborted
once with an ASan heap-use-after-free inside the test double:
`FujifilmVirtualCamera::emitNotification()` on the control task read a
`m_Subscriptions` node that the test's main thread had just freed. The branch
does not touch `lib/testing` or `tests/host/control_e2e`, and the same test
passed in the 183 of 183 run on `a9492f3d`. Rerun alone, it passed 20 of 20,
and `sim-scheduler` passed 10 of 10. This is a race in the test double, not
in this change. It needs its own fix and an issue.
