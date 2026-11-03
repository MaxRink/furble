# 171 - the console suite exits while the control task is still running

169 is the last number on master. 167, 168 and 170 are claimed by the open
PRs #266, #273 and #272. This plan therefore takes 171.

## Motivation

Issue #275. `tests/host` `console-commands` segfaults about one run in two under
coverage instrumentation, and only there. Uninstrumented the suite is 93 of 93.
The fault is on a background thread in `Furble::Control::reapZombieTargets()`.
It cost two coverage runs on PR #273 before one completed, and the coverage
report the run would have published is lost with it.

It is not an instrumentation artefact. It is a real teardown ordering bug that
instrumentation only makes likely enough to see.

## Root cause

`Control::getInstance()` returns a function-local static. Its constructor
registers `~Control` with `__cxa_atexit`, so the destructor runs during
`exit()`, and it frees `m_ZombieTargets` along with every target still in the
drain.

`testDebugWithLiveCamera()` started the control task as a detached
`std::thread` and nothing ever stopped it. `Control::task()` calls
`reapZombieTargets()` at the top of every 50 ms tick, whatever the state. So
after `main()` returns, a live thread keeps walking a vector that static
destruction is in the middle of freeing.

The drain is not empty at that point, and never is. The test ends
`testDebugWithLiveCamera()` with `control.disconnect()`, and the non-restart
path always hands its stopped targets to `m_ZombieTargets` with a
`DISCONNECT_DRAIN_RECLAIM_MS` (2 s) deadline. The suite finishes and exits well
inside those 2 s, so exactly one target is still quarantined when `~Control`
runs.

Instrumentation is what makes the window visible rather than theoretical. The
profile runtime registers `__llvm_profile_write_file` with `atexit` from its
own constructor, so it runs after every C++ static destructor. Writing the
284 KB raw profile then holds the process open for milliseconds after
`~Control` has already freed the drain, and the control task gets a whole 50 ms
tick to land in that gap. Uninstrumented the same gap is microseconds wide.

The core dump says it directly. Main thread:

```
#7  fileWriter ()
#8  lprofWriteDataImpl ()
#11 __llvm_profile_write_file ()
#12 __run_exit_handlers (...) at ./stdlib/exit.c:116
#13 __GI_exit (status=<optimized out>) at ./stdlib/exit.c:146
```

Faulting thread, at `src/FurbleControl.cpp:674`, which is
`if (!target->m_Stopped)` in the reaper predicate:

```
$1 = std::vector of length 1, capacity 1 = {
       std::unique_ptr<Furble::Control::Target> = {get() = 0xffff8cc70b50 <main_arena+96>}}
$2 = (std::vector<...> *) 0xaaaabf15d320 <Furble::Control::getInstance()::instance+96>
```

The vector still reports length 1, because `~vector` deallocates without
clearing `_M_start` and `_M_finish`, and the element now reads
`main_arena+96`: the free-list pointer glibc writes into a chunk it has just
freed. The reaper is iterating a range whose backing store is gone.

ThreadSanitizer names the writer with no inference required:

```
WARNING: ThreadSanitizer: heap-use-after-free
  Read of size 8 by thread T2 (mutexes: write M0):
    #9  Furble::Control::reapZombieTargets() src/FurbleControl.cpp:672:25
    #10 Furble::Control::task() src/FurbleControl.cpp:307:5
    #11 control_task src/FurbleControl.cpp:1170:12

  Previous write of size 8 by main thread:
    #0  operator delete(void*)
    #6  Furble::Control::~Control() include/FurbleControl.h:16:7
    #7  cxa_at_exit_callback_installed_at(void*)
    #8  Furble::Control::getInstance() src/FurbleControl.cpp:178:3

  Thread T2 (running) created by main thread at:
    #2  testDebugWithLiveCamera() tests/host/console_commands_test.cpp:1191:5
```

Nothing in `src/FurbleControl.cpp` or `lib/furble` is wrong here. The
production task is immortal by design, and on device it is: the device never
runs static destructors. The defect is that the host harness let the process
exit around a task it never stopped.

`main()` already knew this. It parked the console task before returning, with a
comment saying that the runtime would otherwise destroy the globals it is
blocked on and the process would segfault on its way out. Test 14 added the
control task afterwards and did not get the same treatment.

## Change

The console harness adopts the contract the control end-to-end harness has
used since plan 123, rather than growing a second park for the second task.

### One shutdown mechanism in the console FreeRTOS shim

`tests/host/console/freertos/FreeRTOS.h` declares `furbleHostStopTasks()` and
the `FurbleHostTaskScope` guard, matching
`tests/host/control_e2e/freertos/FreeRTOS.h` name for name.

In `console_shim.cpp`:

- Tasks created through `xTaskCreate()` stay joinable instead of being
  detached, and the task function runs inside a `try` that catches the shim's
  internal `StopTask`.
- The blocking primitives a task can be parked in, `xQueueReceive()`,
  `vTaskDelay()` and `usb_serial_jtag_read_bytes()`, throw `StopTask` once
  shutdown has begun, and each wait predicate reads the stop flag so a task
  cannot miss the notify. A blocking primitive is a suspension point in the
  production code, holding no production lock, so the stack unwinds through
  ordinary RAII.
- Only threads the shim created ever throw, through a `thread_local` flag. The
  main thread also calls these primitives (`Control::disconnect()` runs there
  and sleeps in `vTaskDelay()`), and it owns the shutdown, so it must never be
  unwound.
- Queues register themselves so shutdown can notify every one of them, and
  `vQueueDelete()` unregisters.
- `furbleHostStopTasks()` sets the flag, notifies, and joins. It copies the
  task list before joining: the console task reads that list under
  `g_TasksMutex` to answer `perf tasks`, so joining under the mutex would
  deadlock against the thread being joined.

`ConsoleHost::parkConsoleTask()` is gone. The park left the console task
sleeping forever, which was enough for that one task, but it cannot join and it
does not generalise. The stop-and-join covers both tasks and the per-target
tasks with one mechanism.

### The test stops what it starts

`console_commands_test.cpp` starts the control task through `xTaskCreate()`,
the way `main()` does on device and the way every other host harness does, so
the shim owns the thread. `main()` opens with a `FurbleHostTaskScope`, so the
tasks are stopped and joined on every exit path before any static destructor
runs.

There are no sleeps and no retries in the fix. The join is the synchronisation.

The suite reports 666 checks rather than 667: the console park assertion is
gone with the park it asserted.

### tools/coverage.py names a crashed host test

`crashed_host_tests(ctest_output)` parses the block ctest ends a failing run
with and returns each test whose reason is not `Failed`. `Failed` means the
binary ran to its own exit and flushed a complete profile. Every other reason
(`SEGFAULT`, `Timeout`, `Subprocess aborted`, `Not Run`, `ILLEGAL`) means the
process was lost, and a lost process writes no profile or a truncated one. That
is the same classification `incomplete_scenarios()` already applies to
simulator scenarios since #274.

`measure_host()` runs ctest through a new `run_streamed()` helper, which echoes
output line by line while keeping a copy, so a minutes-long parallel ctest is
still live. On a non-zero exit it raises `CoverageError` naming the lost tests.

Before, the run failed with `command failed with exit 8` and named nothing, so
this crash was indistinguishable from an assertion failure in a suite of 93.

## Mutation proof

The reverted state is master 2e986fe6 itself, built from a second worktree with
the same compiler and the same flags.

Coverage instrumentation only, clang 18, `-fprofile-instr-generate
-fcoverage-mapping`, 20 consecutive runs:

| Build | segfaults |
| --- | --- |
| master 2e986fe6 | 3 of 20 |
| this branch | 0 of 20 |

Every master failure printed `PASS: 667/667 checks` first and then died, which
is the shape of a crash after the last check rather than a failing one.

AddressSanitizer plus coverage, 20 runs:

| Build | heap-use-after-free |
| --- | --- |
| master 2e986fe6 | 3 of 20 |
| this branch | 0 of 20 |

ThreadSanitizer plus coverage, 20 runs each, counting reports by their SUMMARY
line. Only the rows this bug is about:

| Report | master 2e986fe6 | this branch |
| --- | --- | --- |
| `heap-use-after-free ... in reapZombieTargets()` | 2 | 0 |
| `SEGV ... in reapZombieTargets()` | 2 | 0 |
| `data race ... in Furble::Control::~Control()` | 17 | 0 |
| `data race ... in Control::Target::getCamera()` | 2 | 0 |

ASAN alone, with no coverage instrumentation, does not reproduce in 20 runs on
master. That is the issue's own observation restated: without the profile write
holding the process open, the window is too narrow to hit. The sanitizers are
only useful here on top of the instrumentation.

## Evidence for the fix

Linux VM in OrbStack, clang 18, 2026-09-03. Twenty consecutive runs per batch.

| Batch | result |
| --- | --- |
| coverage instrumentation, 20 runs | 20 of 20 pass, 666 of 666 checks |
| ASAN plus coverage, 20 runs | 20 of 20 pass, no ASAN report |
| ASAN plus coverage, `detect_stack_use_after_return=1`, 10 runs | 10 of 10 pass, no report |
| TSAN plus coverage, 20 runs | no report naming the reaper, `~Control` or the drain |

The stack-use-after-return batch is there because the virtual camera peer is a
stack local of `testDebugWithLiveCamera()` and the tasks now outlive that frame
by design. Nothing reaches it.

TSAN still reports races that master also reports, in `FurbleConsole`'s power
log, `Control::setState()`, `Control::Target::task()`, `NimBLEDevice::resetMock()`
and the virtual peer destructors. They are unchanged by this branch and are not
in this bug's path. This PR does not claim a TSAN-clean console suite.

One new signature appears in 2 of 20 TSAN runs, at
`furbleHostStopTasks()`. It is stack slot reuse: the local task list in
`furbleHostStopTasks()` lands on the stack address the virtual camera peer
occupied inside `testDebugWithLiveCamera()`, and TSAN has no happens-before
edge to that earlier write because the wait that ordered it is
`Control::disconnect()` polling with `vTaskDelay()`. TSAN says so itself, with
`As if synchronized via sleep`. Both writes are to main's own stack, the
earlier one happened while the peer was alive, and the ASAN batch above with
stack-use-after-return detection on finds nothing. It is the same family as the
`~FujifilmVirtualCamera` and `~Subscription` reports that master already
produces.

## Verification

- Host suite: 93 of 93 pass. No test added or removed.
- `python3 -m unittest discover -s tests`: passes, including the six new
  `CrashedHostTestTest` cases.
- `tools/coverage.py --check`: at or above every floor. The floor file is
  unchanged.
- clang-format 21.1.5 `--dry-run --Werror --style=file`: clean on every file
  this branch touches.
- No sdkconfig changed.

## Hardware

`src/FurbleControl.cpp` is not modified, and neither is any other firmware
source. The whole change is `tests/host/` and `tools/coverage.py`, none of
which is compiled into a firmware image. No hardware verification is owed.

## Implementation state

Implemented as described.

The console shim now carries the same shutdown contract as the control end-to-end
shim, which is deliberate duplication of a contract rather than of code: the two
shims model different slices of FreeRTOS and share no translation unit. A host
target that starts a firmware-lifetime task and does not stop it will fail the
same way. `FurbleHostTaskScope` in `main()` is the pattern to copy.
