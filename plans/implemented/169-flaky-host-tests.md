# 169 - deterministic host tests and a coverage run that cannot lose a profile

166 is the last number taken on master. 167 is taken by PR #266
(fujifilm device name, approved, merging before this one) and 168 by PR #273
(merging last). Both already own their numbers, so this plan takes 169.

## Motivation

Issue #269. Three host tests fail on a loaded runner without any code change,
and the coverage tool silently accepts a scenario that never finished. Measured
in the Linux VM on 2026-09-02, 20 repetitions each under CPU load:

| Test | master f425fd38 | PR #266 head 26dc4cbc |
| --- | --- | --- |
| sim-scheduler | 5/20 fail | 2/20 fail |
| fujifilm-registration-gate | 4/20 fail | 1/20 fail |
| control-e2e-flappy-cancel-stress | flaky | flaky |

The cost is not only a red run. `tools/coverage.py` aborts at exit 8 when any
host test fails, so a flake takes the coverage report with it. And the same
commit reported different coverage on different runs: 503 of 697 lines in
`lib/furble/Camera.cpp` on one run and 521 of 697 on the next, because a
simulator scenario was killed on its 600 s timeout and its profile was quietly
missing from the merge. The same mechanism moved `Camera.cpp` between 532 and
548 lines on PR #245 and the union between 71.88 and 74.46 percent on PR #273.
A number that moves without a diff cannot gate anything.

None of the three is a timing-tolerance problem. Each is a specific defect, and
one of them is a production wedge.

## Root causes

### sim-scheduler: spin budgets that are not waits, and one order-dependent setup

`tests/host/sim_scheduler_test.cpp` waited with

```
for (unsigned int attempt = 0; attempt < 10000 && !condition; ++attempt) {
  std::this_thread::yield();
}
```

That is a spin budget, not a wait. `yield()` returns immediately when the
runnable set is already full, so on a loaded host the whole budget is spent in
microseconds while the thread being waited for has not been scheduled at all.
The helpers returned `void`, so the caller carried on as though the wait had
succeeded.

Where the next statement was `advanceClock()`, that is fatal rather than
merely wrong. `waitUntilLocked()` in `sim/freertos.cpp` computes
`deadline = clockMillis() + ticks` when the task actually enters the wait. If
the clock has already been advanced past that point, the task parks on a
deadline nothing will ever reach, and the test blocks forever on the event the
task was supposed to signal. That is the hang, at
`tests/host/sim_scheduler_test.cpp` line 824 on master:

```
waitForBlocked(deadlineTask);   // gives up silently
advanceClock(5);                // virtual time passes the deadline
deadlineWait.finished.wait();   // never returns
```

Reproduced here 6 times in 20 under 10 CPU hogs, every failure a 20 s timeout
kill rather than an assertion. gdb on a hung process shows exactly this: the
main thread in `TestEvent::wait()` from `main`, the worker in
`xQueueReceive(queue, &value, 5)` inside `waitUntilLocked`, parked on an
untimed `condition_variable::wait`.

The second defect is an ordering one. The queue-send preemption case created
the receiver and the sender in one condition, then waited for the receiver to
block. The sender could win, satisfy the receive before the receiver ever
blocked, and leave no queue boundary to preempt at. The assertion that followed
then read whatever order the host scheduler produced.

### fujifilm-registration-gate: fixed sleeps against a 100 ms gate

Four checks injected an event that only means something while
`Fujifilm::waitForRegistration()` is polling, and reached that window with
`std::this_thread::sleep_for(10ms)` or `30ms` after starting the connect
thread. The host build gives that gate a 100 ms deadline
(`FURBLE_HOST_REGISTRATION_TIMEOUT_MS=100`). On a loaded runner the connecting
thread has not necessarily reached the wait after 30 ms, so the injected stale
registration, geotag request or link drop either goes nowhere or lands after
the gate has already given up. Reproduced 3 times in 20, and every failure was
one of those four checks:

```
FAIL: virtual peer can replay a stale callback
FAIL: virtual peer can send a geotag request mid-wait
FAIL: geotag request confirms registration on a reconnect
FAIL: secure virtual peer can send a geotag request mid-wait
```

### control-e2e-flappy-cancel-stress: a real wedge, plus queue latency

This one is not a test defect. The recorded failures were

```
FAIL: no late DISCONNECTING republish in stress iteration
  iteration 7 late state connecting
  iteration 12 late state connecting
```

`Control::disconnect()` arms `m_ConnectAbort` one statement before it publishes
`STATE_DISCONNECTING`. `Control::connectAll()` returned `m_State` on both of
its abort paths, so a read landing inside that window returned
`STATE_CONNECTING`. `Control::task()` refused to republish `STATE_DISCONNECTING`
(plan 157) but published everything else, so it stamped `STATE_CONNECTING` back
over the `STATE_IDLE` that `disconnect()` ends with. `STATE_CONNECTING` is as
terminal for the control task as `STATE_DISCONNECTING` is: both fall through to
`break`, no command is ever dequeued again, and the device is wedged until
reboot. It is the same hardware failure class the plan 157 guard exists to
prevent, reached through the other half of the same window.

A second, benign mechanism also shows up as a late state. `connectAll()` only
enqueues `CMD_CONNECT`; the control task dequeues on a 50 ms tick. With the
disconnect landing at a swept offset from 20 ms, the command could still be
queued when `disconnect()` published `STATE_IDLE`, and the task then left idle
on its own afterwards. That reads as a late republish and is not one, and it
means the sweep was sweeping queue latency rather than the reconnect cycle it
claims to sweep.

### tools/coverage.py: a killed scenario counted as zero lines

`measure_sim_board()` caught `subprocess.TimeoutExpired`, printed
`note: ... contributed no profile`, and carried on. The killed process writes
no profile, so its lines are simply absent from the merge and the percentage
drops for a reason no diff explains. A crash is the same class: a scenario
killed by a signal leaves no complete profile either.

## Change

### Real waits in the simulator scheduler shim

Three blocking waits on the scheduler condition variable, next to the queries
they replace:

- `furble_sim_wait_task_blocked(task, timeout_ms)`
- `furble_sim_wait_task_lifecycle(task, expected, timeout_ms)`
- `furble_sim_wait_task_join_waiters(task, expected, timeout_ms)`

Each takes `schedulerMutex()` and waits on `schedulerCondition()`, which every
scheduler state change already notifies. A descheduled thread costs nothing to
wait for, and the wait returns the moment the state is reached. The timeout is
a backstop for a wedged scheduler, not a race margin, so it is 5 s in the test:
below the ctest `TIMEOUT 10` for `sim-scheduler`, so a wedge names the wait that
failed instead of dying anonymously.

`sim_scheduler_test.cpp` now uses those, every wait returns `bool`, and every
one of the 38 call sites is checked with `return fail(__LINE__)`. Waits on
plain test-owned flags have no scheduler event to wait on, so they still poll,
but against a wall-clock deadline instead of an iteration count and sleeping
rather than spinning against the thread they wait for. The queue-send
preemption case creates the receiver, waits for it to block, and only then
creates the sender.

One spin loop is deliberately replaced by a fixed 50 ms window rather than a
wait: the probe that gives `furble_sim_stop_all_timers()` a chance to return
early while a callback is still running. Correct behaviour there is that
nothing happens, so there is nothing to wait for. A window that is too short on
a loaded host misses a violation; it cannot invent one, so it cannot flake.

### A registration sync point instead of sleeps

A fourth entry in the plan 157 registry,
`"fujifilm_registration_wait"`, in `Fujifilm::waitForRegistration()` after the
subscription is live and before the confirmation poll starts. It fires before
the registration deadline is taken, so a parked test thread does not spend the
timeout it is about to observe. Firmware never defines `FURBLE_TEST_SYNC`, so
the macro expands to nothing.

`furble_host_camera` now compiles with `FURBLE_TEST_SYNC` and links
`testsync/TestSyncController.cpp`. A point with nothing armed on its name
returns immediately, so nothing changes for the other tests that link the
library. `fujifilm-registration-gate` arms a barrier, waits for the connecting
thread to park, injects, and releases. The four sleeps are gone, and a parked
thread that is never released fails the test through `anyTimedOut()` rather
than hanging it.

`FurbleTestSync.h` moves from `include/` to `lib/furble/`. `src/` already sees
`lib/furble/`, but a `lib/furble` source cannot rely on seeing `include/` in the
PlatformIO library build, and the point now has a call site in each.

### connectAll reports the abort, not the state it happened to read

Both abort paths in `Control::connectAll()` now return `STATE_DISCONNECTING`,
which is the one value that means disconnect owns the state and the task
already refuses to republish. Nothing else changes: the task's guard is
unmodified, and the only other returns are `STATE_ACTIVE`, `STATE_CONNECT` and
`STATE_CONNECT_FAILED`, none of which is reachable with the abort armed.

`control-e2e-flappy-cancel-stress` additionally waits for the state to leave
idle before its swept sleep, so the sweep sweeps the reconnect cycle rather
than the command queue latency in front of it.

### An incomplete scenario fails the coverage run

`incomplete_scenarios(results, timeout_s)` classifies each scenario outcome:
`None` for a timeout kill, a negative code for a signal death. Either means no
usable profile, and `measure_sim_board()` raises `CoverageError` naming every
one of them, which exits 2. A scenario that merely exits non-zero still
contributes a complete profile and is still only noted, because whether it
should have passed is the sim-e2e workflow's gate, not this one.

The floor file is untouched. No floor is lowered and no measurement is claimed
to be inside its noise band; the point of this change is that the noise band
was an artifact.

## Evidence

All numbers from the Linux VM, 20 repetitions per batch under 10 CPU hogs,
which is the load the issue's numbers were taken under. The VM is shared, so
the ambient load is not constant between batches; each batch below ran its
before and after on the same binaries and the same hog count.

| Test | master 77aa113f | this branch |
| --- | --- | --- |
| sim-scheduler | 6/20 fail, all 20 s timeout kills | 0/20 |
| fujifilm-registration-gate | 3/20 fail | 0/20 |
| control-e2e-flappy-cancel-stress | 2/20 fail | 0/20 |
| control-abort-republish (new) | n/a | 0/20 |

`control-e2e-flappy-cancel-stress` is the weakest of the three as a stochastic
measurement. After that first batch it did not fail again in 165 further runs
of the unmodified master binary across four load profiles, including 45 runs
with three copies in parallel and 20 runs under another agent's full parallel
build. Its real rate is nearer 1 percent than 10, which is exactly why the
deterministic test below matters more than the batch numbers.

### Mutation proofs

`control-abort-republish`, restoring `return m_State;` on either abort path in
`Control::connectAll()` and rebuilding only that target:

```
  FAIL: no late CONNECTING republish after the aborted connect
  FAIL: state is idle after the release
  FAIL: follow-up connect reaches active
  FAIL: follow-up connect is connected
control-abort-republish: FAIL (4 checks)
```

5 of 5 runs fail, identically. The last two failures are the wedge itself: the
machine no longer accepts a connect. Restoring the fix returns the test to
green in 3.3 s.

`sim-scheduler`, master's `tests/host/sim_scheduler_test.cpp` compiled against
this branch's shim (the new waits are additive, so it builds unchanged): 11 of
20 runs hang and are killed at 25 s. The fixed file in the same conditions is 0
of 20. Reverting only `waitForBlocked` to its spin budget, and leaving the other
checked waits in place, was not enough to reproduce in 20 runs, which is the
point: the fix is that every wait is a wait and every caller checks it, not any
single helper.

`fujifilm-registration-gate`, master's file compiled against this branch (the
sync point is inert with no barrier armed): 2 of 20 fail, on
`virtual peer can replay a stale callback`,
`virtual peer can send a geotag request mid-wait` and
`geotag request confirms registration on a reconnect`. The fixed file is 0 of
20.

## Verification

Linux VM, GCC 12 for the host suite, clang 18 for coverage, 2026-09-03.

- Host suite: 93 of 93 pass. Master has 92; `control-abort-republish` is the
  only added test.
- `python3 -m unittest discover -s tests`: 138 tests, all pass. That includes
  the five new `ScenarioOutcomeTest` cases.
- `tools/check_sim_scenarios.py`, `tools/check_ci_workflows.py` and
  `tools/check_portability_inventory.py --check`: clean.
- Certified e2e on the 135x240 panel: 82 of 82 scenarios pass.
- `tools/coverage.py --check`: at or above every floor. The floor file is
  unchanged.
- clang-format 21.1.5 `--dry-run --Werror --style=file`: clean on every file
  this branch touches.
- No sdkconfig changed.

## Coordination with the open branches

Merge order is #266, then this PR, then #245, #272, #265, #63, #273.

PR #245 and PR #272 both edit `Control::connectAll()` and `Control::disconnect()`
and rebase over this one. The lines below are the whole production change here,
and they must survive those merges.

- `Control::connectAll()`, the early abort path under `m_Mutex`: keep
  `return STATE_DISCONNECTING;`. A rebase that reinstates `return m_State;`
  reinstates the wedge.
- `Control::connectAll()`, the tail after the interruptible retry wait: keep
  `return (m_ConnectAbort || m_State == STATE_DISCONNECTING) ? STATE_DISCONNECTING : STATE_CONNECT;`.
  A rebase that reinstates
  `return m_ConnectAbort ? m_State : (m_State == STATE_DISCONNECTING ? STATE_DISCONNECTING : STATE_CONNECT);`
  reinstates the same wedge through the other path.
- `Control::task()`, the `if (next != STATE_DISCONNECTING)` guard: unchanged by
  this PR, but it is what both returns above depend on. Widening or removing it
  undoes them.

`control-abort-republish` is the check. If a rebase reinstates either `m_State`
return it fails on every run, and the failure names the wedge directly:
`follow-up connect reaches active`. Run it after resolving any conflict in
`src/FurbleControl.cpp`.

`FurbleTestSync.h` moves from `include/` to `lib/furble/` in this PR. The
`#include "FurbleTestSync.h"` line is unchanged in every consumer, so a branch
that only includes the header rebases cleanly. A branch that adds a sync point,
adds a host target listing the header's directory, or names
`include/FurbleTestSync.h` in a document must follow the move.

## Implementation state

Implemented as described.

`control-abort-republish` is a new deterministic test rather than a change to
`control-interleave`. Both drive the same window from opposite sides, but they
need different peers and different settings, and the existing test is a single
`main()` over process-global control state.
