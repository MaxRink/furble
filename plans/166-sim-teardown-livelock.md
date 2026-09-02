# 166 - simulator teardown timeout and boot livelock

## Motivation

Two master-level simulator defects, filed as issues 267 and 268, both hid
behind bounds that are denominated in virtual time and behind exit codes that
nothing checked.

Issue 268 wedged runs for hours. Six simulator processes across five unrelated
worktrees were found spinning between 45 minutes and 2 hours at 70 to 100
percent CPU each, and each wedged run raised the host load that made the next
run wedge, so a machine could saturate on nothing but boot deadlocks.

Issue 267 logged `Camera disconnect timed out after 30000 ms, forcing
completion.` at teardown on every fuzz seed of the 320x240 M5Stack Core binary.
The leg stayed green because a forced completion only wrote a log line.

## Root cause: issue 268, boot livelock

A boot-order deadlock between the SDL render pump and the simulator thread,
triggered by M5GFX's debugger detector misfiring under host load.

1. The main thread busy-spun `std::this_thread::yield()` until `panelReady`
   (`sim/main.cpp`).
2. `panelReady` is stored only after `Platform::init()` returns.
3. `Platform::init()` reaches `M5Unified::begin()`, `LGFX_Device::init_impl()`,
   `endWrite()` and `Panel_sdl::display()`. With step-exec mode latched, that
   function spins `SDL_SemPost(in); SDL_SemWaitTimeout(out, 1);` until
   `_display_counter` catches up with `_modified_counter`.
4. `_display_counter` advances only in `Panel_sdl::_update_proc()`, reached only
   from `Panel_sdl::loop()`, run only by the main thread, and only after
   `panelReady`.
5. Step-exec mode is latched by M5GFX's `dbg` thread. It sleeps 1 ms and, when
   the observed interval exceeds 64 ms, concludes that a debugger stopped the
   process and holds step-exec for the next 512 ms. A loaded host overshoots a
   1 ms sleep past 64 ms routinely, so the inference is a false positive.

Both threads then spin forever. The three-thread signature was identical in
every gdb dump: main in `sched_yield` at `sim/main.cpp`, the simulator thread
in `sem_wait` inside `Panel_sdl::display`, and `dbg` in `nanosleep` inside
`detectDebugger`. The wedged runs were scripted scenarios and a fuzz seed
alike, which is what a pre-UI boot deadlock predicts.

### Why neither existing net caught it

The plan 155 liveness invariant runs on the driver tick inside the UI task and
is evaluated in virtual time. This deadlock happens inside `Platform::init()`,
before the UI, the driver and any FreeRTOS task exist, and virtual time never
advances at all, so a virtual-time invariant is structurally blind to it. No
amount of tightening it would help.

`run-e2e.sh` and `run-watchdog.sh` ran `"$BIN" --script ...` with no wall-clock
bound of any kind. Only `run-fuzz.sh` bounded a run, which is why a wedged fuzz
seed at least died while a wedged scenario hung the whole job with no output
naming it.

## Root cause: issue 267, teardown timeout

Not a Control defect and not the virtual radio. It is the plan 158 Phase 3
scheduler fairness gap, reached on every seed of this board.

Instrumenting `Control::disconnect()` at the timeout gave the same picture on
4 of 4 runs: `targets=0 zombies=1..2 connectInProgress=1 state=disconnecting
abort=1 connectCamera=none`, with the drained target's teardown task never
publishing `m_Stopped`, after 1501 wait slices. So `disconnectComplete()` was
false for exactly one reason: a connect was still in progress. The control task
was inside `connectAll()` holding `Camera::m_Mutex` for the whole connect, and
the per-target task was blocked on that plain host mutex.

Neither could make progress because the teardown loop runs on the simulator
`ui` thread, and `vTaskDelay` short-circuited for that thread to
`advanceClock(ticks)` plus a fixed 50 microsecond host sleep. On FreeRTOS a
20 ms `vTaskDelay` blocks the caller and every ready task runs; here the clock
jumped 20 ms and the other threads got 50 microseconds of host time. 1501
slices bought 75 ms of host time to unwind a connect. The connect therefore ran
to its own `TIMEOUT_DEFAULT_MS` of 30000 ms of virtual time, which is the same
30000 ms as `DISCONNECT_WAIT_MAX_MS`, and that is why the message reads as an
exact timeout.

Raising only that handoff from 50 to 3000 microseconds made the forced
completion disappear on 3 of 3 runs against 4 of 4 forced completions at 50
microseconds, with nothing else changed. That isolates the starvation as the
cause.

## Change

Simulator only. No firmware source is touched, so release binaries are
unchanged.

### Boot livelock

`sim/sdl_lifecycle.cpp` already interposes on `SDL_CreateThread` to retain the
`dbg` thread handle. It now declines to start that thread at all, and
`FURBLE_SIM_SDL_STEP_DETECT=1` restores it for an interactive debugging
session. Step-exec then stays off, `Panel_sdl::display()` is a no-op, and the
deadlock is unreachable. This is exactly the behaviour every healthy run
already had; the change is that a loaded host can no longer flip it.

`sim/main.cpp` sleeps rather than spinning while it waits for the panel, so a
stalled bring-up no longer burns a core and worsens the load that caused it.

### Host wall-clock stall watchdog

`sim/watchdog.cpp` adds the one bound in the simulator that is measured against
the host clock, because a stall that stops virtual time cannot be seen by any
virtual-time bound. Progress is the virtual clock plus the scheduler progress
counter plus the recorded phase, so a merely slow run keeps resetting it. The
bound is `FURBLE_SIM_WATCHDOG_SECONDS` host seconds, default 120, and 0
disables it. On a trip it prints the phase, the virtual clock, the scheduler
task table and a native backtrace of every registered thread, then exits
non-zero. Threads register at creation, including every simulator FreeRTOS
task, so the report names them.

The bound has to be generous, and the reason is worth stating because it
constrains anyone tempted to tighten it. Boot runs before any simulator task
exists and advances neither the virtual clock nor the scheduler counter, so
across `Settings::init()` and the rest of bring-up the recorded phase is the
only progress the watchdog can see. Deliberately abusing a host with ten
spinners and three concurrent simulators made that boot exceed 30 seconds, and
a 30 second bound then reported a perfectly healthy run, correctly dumping a
thread sitting in `Preferences::put`. Each boot step now records its own phase,
so a slow but progressing boot keeps resetting the bound and a wedged one names
the step it stopped at, and the default stays at 120 seconds. A wedged run is
permanent, measured in hours, so a generous bound costs nothing and a tight one
buys a flaky gate.

`schedulerProgress` in `sim/freertos.cpp` is now atomic and readable through
`furble_sim_scheduler_progress()` without the scheduler lock, which a stalled
run may never release. `furble_sim_report_tasks()` takes that lock only if it
is free and says so plainly when it is not.

### Scheduler fairness

The `ui` thread's `vTaskDelay` now models the FreeRTOS contract: after
advancing the clock it hands the scheduler over until no task is runnable,
meaning every task released by that advance has taken its turn and blocked
again. The turn holder is deliberately not part of the condition: a task
blocked outside the scheduler on a plain host mutex holds the turn without
being runnable, and advancing virtual time is exactly what releases the task
holding that mutex, so returning is what makes progress. A 250 ms host ceiling
keeps a scheduler defect from wedging the UI thread silently; a run that keeps
hitting it blows the per-scenario and per-seed wall-clock bounds and fails.

### Making both failures fail

- A forced completion now fails the run. `sim/main.cpp` already had the boolean
  return of `Control::disconnect()` and discarded it; it now reports and calls
  `requestFailureExit()`.
- `run-e2e.sh` and `run-watchdog.sh` bound every scenario with `timeout -k 10`,
  default 300 s, overridable with `FURBLE_SIM_SCENARIO_TIMEOUT`.
- `run-fuzz.sh` replays one guarded seed and requires the two runs to be
  identical. The fuzzer drives the whole app, so host timing leaking back into
  firmware behaviour shows up as a diverging log line rather than as an
  unreproducible finding weeks later.

## Determinism, stated exactly

The replay asserts what was measured to hold, and no more.

What holds: the `FUZZ EVENTS`, `FUZZ COVERAGE` and `FUZZ SUMMARY` lines match
across two runs of the same seed on the same binary, with the two observation
counters masked. That was verified three times on each of the three panels.
The same seed drives the same event stream and reaches the same pages.

What does not hold, and is therefore not asserted: firmware behaviour under the
fuzzer is not yet reproducible line for line. Seed 2 on 320x240 does reproduce
exactly, all 215 log lines identical apart from the two counters, but seed 1 on
the same binary can still differ by one connect attempt between runs. The cause
is the same one that produced issue 267: production code blocks on plain host
mutexes the simulator scheduler cannot see, so how far a connect gets before a
disconnect lands is still host timed. That is the scheduler-visible mutex plan
158 Phase 3 owns.

An earlier revision of this change asserted byte equality of the whole log as a
sorted multiset. It was removed after it caught that divergence, because a gate
that fails on a known unfixed gap is a flaky gate, and a flaky gate is worse
than none. The masked counters record whether a visible change had landed by
the end of a settle window, and that boundary moves by one step for the same
reason.

## Expected-fail seed 3 on 320x240, promoted

Plan 161 pinned seed 3 expected-fail on the 320x240 board for a deterministic
layout overflow on the intervalometer settings page at step 447. It now passes
on three of three runs with zero findings, so `run-fuzz.sh` reported `XPASS`
and failed the leg, exactly as designed, and the seed is promoted back into the
guarded set.

State the reason precisely: the page was not fixed. The scheduler fairness
change alters how far the control task gets per UI slice, so the event stream
reaches a different page at step 447 and no longer walks into the overflow. The
320x240 layout bug is still there and still uncovered, because `EXACT_BOARDS`
in `tools/check_sim_scenarios.py` restricts `text-size-overflow-large.txt` to
`m5stick-s3` and `m5stick-c`. Closing that gap belongs to a layout PR that can
verify on the board.

## Deliberately not in this PR

Two genuine Control gaps were found while root-causing issue 267 and are
tracked separately, with their own plan, host tests and a hardware gate:

1. The interactive disconnect path carries the same 30 s cap and breaks out of
   it silently, with no log at all.
2. A target drained into `m_ZombieTargets` while its connect is still in flight
   can never be cancelled by a later `disconnect()`, because that loop walks
   only `m_Targets`. This is the reconnect-cancel deadlock family recorded in
   `CLAUDE.md`.

Neither is the cause of issue 267, which is host starvation, and neither can be
verified without hardware. Mixing them into a simulator-only change would put a
firmware teardown edit behind a gate that cannot exercise it.

## Verification

- Certified e2e on all three panels: 82 on 135x240, 7 on 80x160, 7 on 320x240.
- `run-fuzz.sh` on all three panels, exit 0, zero forced completions, plus the
  determinism replay.
- Host suite, python tooling tests, `tools/check_sim_scenarios.py`, coverage
  floor, clang-format 21.
- The parallel-load reproduction, three repetitions, clean.
- Mutations: reverting each fix makes the matching new detection fail.
