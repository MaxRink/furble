# 173 - Scheduler-visible host mutex, one flash per simulated device

Plan numbers through 172 are taken. 168 and 170 are claimed by open PRs, so
this plan takes 173.

## Motivation

Two simulator fidelity defects, both found by review rather than by CI, both
about the simulator modelling something the device does not do.

**Issue #279.** The simulator scheduler cannot see a task waiting on a plain
host mutex. `Camera::connect()` holds `Camera::m_Mutex` for a whole attempt, so
a per-target task running `Camera::disconnect()` blocks there, stays marked
runnable, and keeps a turn it never gave back. Only the plan 161 deadlock
breaker frees it, on a two second host bound, and the UI handoff burns its
250 ms host ceiling on every slice until it does. `Control::disconnect()` polls
on the UI thread and every 20 ms slice advances the virtual clock, so the
number of slices a cancel cost was set by how long the host took to let the
connect task run. Virtual time became a function of host load.

The measured consequence, from the PR #278 delta review:
`cancel-sweep-fauxny-ui` failed its own 24000 ms bound in 2 of 65 idle runs and
in 4 to 6 of 8 at loadavg 80, once at 83330; `cancel-sweep-fuji-secure-ui`
failed 1 of 15 at 31085 against 31000; a certified 80x160 bughunt pass failed at
55615. PR #278 deleted every clock bound it had just added rather than ship a
flaky gate, and recorded that no clock bound ships until #279 closes. This plan
closes it and puts the bounds back.

**Issue #284.** `e2e/restart-persist.txt` failed about 5 percent of the time on
the alternate boards, always `setting.reconnect expected '1' got '0'` on the
assertion right after the `restart` re-exec. The issue guessed at a preferences
write racing the exec across the 200 ms virtual-time gap. That is not what it
is, and the fix that guess implies would not have touched it.

Issue #283 is also here, because the same path is where its one unexplained
SIGSEGV landed: a fatal-fault reporter, so a second sighting leaves a dump.

## Root cause 1: the scheduler could not see the wait

The simulator models a turn a task holds until its next scheduler boundary.
Every boundary is a call into the FreeRTOS shim: a delay, a queue wait, a
yield. A host mutex is not one of them, so `Camera::m_Mutex` was a hole in the
model. A task went into `lock()` still holding the turn, and nothing in the
scheduler could observe that it had stopped running.

Two mechanisms then leaked host time into virtual time.

`waitForTurnLocked()` has to take the turn away from a holder that stops
reaching boundaries, which it does after a two second host bound with no
scheduler progress. And `vTaskDelay()` on the UI thread waits for every task to
reach a boundary before it returns, up to `UI_HANDOFF_BOUND`, 250 ms host; a
task parked in a host mutex never reaches one, so that wait ran its whole
ceiling. Both are host clocks. The UI loop that owns those slices is also the
loop that advances the virtual clock, so a loaded host produced more slices and
therefore more virtual milliseconds for the same modelled work.

Both were written down as interim, with the fix named: a scheduler-visible
mutex, plan 158 Phase 3.

### The fix

`Camera::m_Mutex` is a `Furble::connect_mutex_t`. In every build except a
`FURBLE_SIM` one that is `std::mutex`, so firmware and the host test suite
compile the shipping type and the shipping lock discipline. Under `FURBLE_SIM`
it is `Sim::SchedulerMutex`, the same `std::mutex` with the contended wait
reported:

```
void lock(void) {
  if (m_Mutex.try_lock()) {
    return;
  }
  schedulerHostBlockBegin();
  m_Mutex.lock();
  schedulerHostBlockEnd();
}
```

`schedulerHostBlockBegin()` is the existing `setTaskBlockedLocked(true)`: the
waiter stops being runnable, gives up the turn, and the scheduler dispatches
the holder immediately. `schedulerHostBlockEnd()` marks it runnable again and
waits for its turn, exactly like the return from a queue wait. The wait carries
no deadline and no queue, so nothing else releases it; the task releases itself
when the real mutex is in hand, which is the real event the wait ends on.

An uncontended acquisition costs one `try_lock` and reports nothing, so the
common case never enters the scheduler.

This is the smallest seam that closes the measured defect. Only
`Camera::m_Mutex` changes type. It is the one host mutex production holds for
seconds; `m_ConnParamsMutex`, `Scan::m_StateMutex` and the rest are held for
microseconds and have never been measured to move a run. Widening the alias is
mechanical if one of them ever does.

**This is a change in `lib/furble`, and it is a type alias behind
`FURBLE_SIM`.** `lib/furble/Camera.h` gains the alias and the include that
declares it; five lock sites in `lib/furble/Camera.cpp` name the alias instead
of `std::mutex`. No lock is taken, released or ordered differently in any
build, and a firmware build compiles `std::mutex` exactly as before.
`FURBLE_SIM_NO_SCHED_MUTEX` restores the pre-change type inside a `FURBLE_SIM`
build, which is how the mutation below is built.

### What comes back

The three per-run cancel-latency bounds PR #278 removed:

All three legs are certified on all three panels, so all three panels are
measured. The slack is 6 to 12 percent.

| Scenario | Bound | 135x240 | 80x160 | 320x240 |
| --- | --- | --- | --- | --- |
| `cancel-sweep-fauxny-ui` | 24000 | 22525 to 22540 | 22525 | 22525 |
| `cancel-sweep-fuji-secure-ui` | 31000 | 28430 | 28545 | 28470 |
| `cancel-sweep-reconnect-entry` | 20000 | 17965 | 17850 | 17460 |

And the settle bounds, from 30000 back to the 6000 they were widened from, on
all ten scenarios that carry one. PR #278 widened them for the same reason it
deleted the clock bounds, and the reason is gone.

The two certified-false reproductions keep no clock bound. Both now fail on
master at `ble.secure_stall_aborted`, which is a stronger and earlier
assertion, and a clock bound underneath it would only move the documented
failure point. `cancel-sweep-autoconnect-entry` keeps none either, and its
header says why: every cancel on that leg lands outside a live handshake, so an
outwaited cancel and an aborted one cost the same virtual time and a bound
would separate nothing.

## Root cause 2: two devices shared one flash

The preferences store was a file named after the scenario,
`.pio/furble-sim-preferences-<scenario>.bin`. Every fresh boot deletes it and
writes its defaults; a boot resumed by the `restart` verb keeps it, because it
is the flash the reboot has to carry over.

Nothing keyed it to the process. Two simulators running the same script from
one working directory therefore shared one flash image, and each one's boot
erased the other one's. The failure is the interleaving where the second
simulator boots between the first one's `action toggle reconnect` and its
`restart`: the toggle is already in the first process's RAM, so the
pre-restart assertion passes, and the re-exec then reads back a file the other
device wiped. That is why the symptom is always the persisted setting, always
immediately after the re-exec, and always on a run that was one of several
started together.

It is not a write racing the exec. Every `Settings::save` is synchronous on the
caller's thread, and `restartProcess()` runs only after every task has joined
and the panel has closed, so no write is in flight at exec time. An explicit
flush before the exec would have changed nothing. 360 isolated runs on 80x160
and 80 on 320x240 at loadavg 90 produced no failure at all on master.

Confirmed by construction instead. Two simulators, the same script, one working
directory:

| Tree | 80x160 | 320x240 |
| --- | --- | --- |
| master 6245a301, loadavg 90 | 18 of 30 failed | 13 of 30 failed |
| master 6245a301, loadavg 250 | 5 of 30 and 9 of 30 failed (two 320x240 simulators) | |
| this branch, loadavg 85 | 0 of 100 | 0 of 100 |

Every failure is the reported assertion, `setting.reconnect expected '1' got
'0'`.

### The fix

One flash image per simulated device. The store is
`.pio/furble-sim-preferences-<scenario>-<pid>.bin`, and a boot resumed by
`restart` keeps the `FURBLE_SIM_PREFS` path it inherited rather than computing
a new one from its own process id. `removePreferences()` drops the file on an
orderly exit, so a scenario run leaves no more behind than before.

`saveValues()` also wrote through a fixed `<path>.tmp`, so two writers of one
store path could rename each other's half-written image into place. The temp
file is per process now. Nothing in tree shares a store path any more, but a
rename is only atomic against another writer if the file it renames is its own.

## Issue #283: a fatal fault now leaves a dump

`SIGSEGV`, `SIGBUS`, `SIGILL` and `SIGFPE` are caught in `sim/watchdog.cpp`,
next to the stall watchdog they complement. The handler prints the scenario
line being executed, the boot phase, the faulting thread and a native backtrace
(`-rdynamic` is already on), then re-raises so the process still ends with the
real fatal status a runner reports.

It reads two atomics and nothing else, so it takes no lock a faulted thread
could be holding. `Step` keeps the source line it was parsed from and the
driver publishes it once per tick; the steps vector is fixed after parsing, so
the pointer is valid for the run.

`FURBLE_SIM_CRASH_STEP=<index>` faults deliberately at a script step. It is the
self test for the reporter and has no other use. On `e2e/restart-persist.txt`
step 6:

```
SIM CRASH: SIGSEGV
SIM CRASH: scenario step: assert ui.page settings
SIM CRASH: phase: running
SIM CRASH: thread: simulator
/home/.../furble-sim(+0x1f0b10)[0xaaaaafc10b10]
linux-vdso.so.1(__kernel_rt_sigreturn+0x0)[0xffffb352f8c0]
/home/.../furble-sim(_ZN6Furble3Sim10driverTickEv+0x224)[0xaaaaafbcda84]
/home/.../furble-sim(_ZN6Furble2UI4taskEv+0x68)[0xaaaaafc66420]
...
```

with exit status 139.

## Verification

Debian bookworm arm64, 11 cores. Load is generated with spin loops.

| Gate | Result |
| --- | --- |
| clang-format 21 | clean on every changed source |
| Host suite, `tests/host` | 94 of 94 passed |
| `pytest tests` | 144 passed |
| `tools/check_sim_scenarios.py` | manifest complete |
| Certified e2e, 135x240 / 80x160 / 320x240 | 83 / 8 / 8 passed, 0 failed |
| Certified bughunt, 135x240 / 80x160 / 320x240 | 18 / 17 / 13 passed, 0 failed |
| `run-fuzz.sh` on 135x240, 8 seeds plus the seed 2 determinism replay | all seeds as expected, replay passed |
| Coverage floor, `tools/coverage.py --check` | at or above every floor: grand union 71.03 against 69.26, sim union 54.51 against 50.66 |
| Firmware, `FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e m5stick-s3-debug` | SUCCESS |
| `sdkconfig.*` | unchanged |

### Issue #279

`cancel-sweep-fauxny-ui` is the leg the issue measured. Its 24000 ms bound is
restored, and the mutation is the same binary built with
`FURBLE_SIM_NO_SCHED_MUTEX`, so the only difference between the columns is the
type of `Camera::m_Mutex`.

| Host | This branch | `FURBLE_SIM_NO_SCHED_MUTEX` |
| --- | --- | --- |
| idle, loadavg 4 | 0 of 50 failed | not run |
| loadavg 86 | 0 of 50 failed | 0 of 20 failed |
| loadavg 253 | 0 of 50 failed | 1 of 20 failed, `clock.ms expected <= 24000 got 24165` |

The mutation needs more load than the reporting host did to cross the bound,
which is a property of this machine, not of the defect. What the load
independence looks like directly, eight runs of the same leg with the bound
lifted so every run reports its clock:

| Build | clock.ms at loadavg 253 | spread |
| --- | --- | --- |
| this branch | 22525, 22525, 22530, 22530, 22535, 22535, 22535, 22535 | 10 ms |
| `FURBLE_SIM_NO_SCHED_MUTEX` | 22705, 23045, 23150, 23270, 23270, 23610, 23725, 23995 | 1290 ms |

At loadavg 85 the same comparison is 22535 / 22535 / 22540 against 22730 /
23095 / 23210, and the host runtime of one leg is 2 to 3 s against 7 to 12 s.
Plan 172 measured the two expensive Secure legs at 51 s and 50 s on an idle
135x240 host; they run in 35 s each here at loadavg 90.

### Issue #284

100 runs of `e2e/restart-persist.txt` on each alternate board, run as two
concurrent simulators in one working directory, which is the shape that
produces the failure. See the table under root cause 2. An earlier 200-run
batch in the same shape had one non-zero exit on 80x160 whose log was not
retained; it was not the reported assertion, and the reported assertion has not
appeared in 400 post-fix runs.

## What is not covered

- **The other host mutexes.** `m_ConnParamsMutex`, `Scan::m_StateMutex` and
  `Scan::m_DispatchMutex` are still invisible to the scheduler. They are held
  for microseconds and no run has been measured to move because of one, but
  they are why the fuzz determinism replay still compares the report lines
  rather than the whole log.
- **The deadlock breaker.** It stays, and it should. It is a backstop for a
  mutex nobody has instrumented now, rather than the thing the cancel path ran
  on every time.
- **The 200 ms virtual-time gap in the issue #284 report.** It is not a
  contributing factor and there is nothing to fix there. Recorded so the next
  reader does not go looking for it.
- **Concurrent runs of one scenario are isolated, not synchronised.** Two
  simulators now have their own flash. They still share a capture directory and
  a report directory, which no scenario asserts on today.
