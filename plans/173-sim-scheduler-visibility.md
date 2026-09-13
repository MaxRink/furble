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

### Environment ordering follow-up, 2026-09-08

The per-run `FURBLE_SIM_PREFS` path is now prepared on the main thread after
argument and scenario configuration, before SDL setup or the simulator thread
starts. This keeps the `setenv` and preference-file cleanup out of the interval
where SDL may read process environment state while bringing up or pumping the
panel. A `restart` step records its continuation index without changing the
environment; `main()` sets `FURBLE_SIM_RESTART_STEP` in `restartProcess()` only
after the simulator thread has joined and the SDL panel has closed. The resumed
boot still consumes and unsets that variable, and the existing
`restart-persist.txt` and `restart-post-failure.txt` fixtures remain the focused
behavioral checks for persistence and failure precedence.

The Linux ordering check in `sim/scripts/run-env-order.sh` compiles an
`LD_PRELOAD` interposer that rejects environment mutation after SDL reports
initialization. It runs a fresh smoke boot and `restart-persist.txt` against
the current binary, then expects the pre-fix binary to fail with the guard's
status. This is a deterministic ordering regression, not evidence that the
environment race caused an unrelated crash.

Root validation recorded the CMake configure and simulator build as passing at
`/home/a92615428/b/sim-environ-config.log` and
`/home/a92615428/b/sim-environ-build.log`, using the CMake-header fix at
`d646267f`. The guard wrapper passed both current positive legs and both exact
target negative legs, recorded in
`/home/a92615428/b/sim-environ-guard.log`. The negative executable was the
available production-UI simulator `249650a5`, not an exact `8a94` build, so the
negative result is evidence for the old mutation locations in that binary only.
Neither the build nor the guard run establishes SIGSEGV causality.

## Follow-up state: bounded crash diagnostics (#283/#289)

The simulator now installs fatal diagnostics before `configure()` parses
arguments or a scenario. Every simulator thread that enters the watchdog
registry receives a thread-local alternate signal stack, and fatal handlers
use `SA_ONSTACK`. Handler installation and normal-context unwinder warm-up are
idempotent. An already-enabled host or sanitizer alternate stack is preserved.
The handler prints the phase and scenario line when available before invoking
the best-effort unwinder, then re-raises the fatal signal so callers retain the
real signal status.

This is an observability improvement only. `backtrace` and symbol formatting
are not formally async-signal-safe, so the handler remains best effort and
does not establish the root cause of issue #283. The host regression
`sim_watchdog_test` covers caller and registered-worker alt-stack queries,
preservation of a preinstalled worker stack, and a forked SIGSEGV child that
must emit the signal, phase, and step banners. Actual stack-overflow coverage
remains future work. Preference sidecars, fairness changes, cancellation-bound
changes, and restart semantics are not part of this slice.

Root validation at `1311e02694b922242fb6673aeea22c2b1962c010` configured and
built `sim_watchdog_test` and `sim_scheduler_test` with two compiler jobs.
Both CTest cases passed (0.11 seconds total). The new regression checks
`SA_ONSTACK`, a fresh registered worker, preservation of an existing worker
stack, and native fatal-signal termination with diagnostic metadata.
Full simulator/CI validation remains pending; no physical device was accessed.

## Queued-waiter runtime follow-up

Current master still used the earlier `std::mutex` adapter. This follow-up
replaces that simulator-only adapter with the queued-waiter `SchedulerMutex`
runtime from `df40247f`. It serializes waiter selection with the scheduler,
reserves ownership before publishing a wake, cancels registered waiters during
task teardown, and cancels an unregistered host waiter with `SchedulerStopped`
when the scheduler stops. `sim/main.cpp` catches that exception around the
unregistered simulator thread and fails fast without claiming orderly cleanup.

The host regression in `tests/host/sim_scheduler_test.cpp` covers priority
selection, registered-waiter cancellation, survivor ownership, and
unregistered-host-waiter cancellation. Its `try_lock()` assertion runs after
the selected waiter has acquired the mutex, so it checks exclusion at that
point and does not independently prove the reservation-before-wake race. No
deterministic reservation seam exists in this test harness.

Root validation of this runtime and its new test path is recorded below. CI,
hardware, and physical scheduler-parity results remain separate gates. The
waiter-state dump and repeated high-load virtual-time-bound proof remain
separate open work.

## SchedulerStopped fail-fast boundary

An exception from a scheduler-visible mutex can arrive while `UI::task()` owns
the manual LVGL mutex. The simulator catches `SchedulerStopped` inside
`runSimulator()` while the `UI` object is still alive, writes a bounded
low-level failure banner, and calls `std::_Exit(1)`. It deliberately does not
attempt locks, joins, LVGL work, destructors, peer cleanup, rig cleanup, MQTT
shutdown, watchdog unregister, or recovery. This avoids claiming that those
operations are safe after the scheduler has stopped.

The outer simulator-thread boundary has the same fail-fast handling for a
`SchedulerStopped` escape that occurs before or after the UI phase. Normal
successful teardown is unchanged. A simulator-only
`FURBLE_SIM_TEST_SCHEDULER_STOP=1` trigger raises the same exception from the
first `driverTick()` inside the locked UI phase. The bounded regression is
`sim/scripts/assert-scheduler-stop-failfast.sh`; it expects diagnostic output,
explicit status 1, and neither a signal exit nor a timeout. The existing CI
`assert-exit-regression.sh` invokes this check against the same simulator
binary. It also runs the same small `smoke.txt` scenario with the trigger
disabled and expects status 0 without the fail-fast banner. Four tiny wrapper
fixtures are rejected when they return status 0, status 1 without the banner,
a signal status, or a timeout. The signal fixture must return 143. The timeout
fixture accepts 124 or the explicitly documented forced-kill status 137. The
other three emit the exact banner first, so a missing executable or unrelated
failure cannot masquerade as coverage.

Exception-safe cleanup for other UI or native MQTT exceptions remains a
separate gap. This fail-fast boundary is not hardware, scheduler-parity, or
recovery evidence.

## Follow-up state: safe preference ownership slice (#289)

The simulator now distinguishes caller-owned `FURBLE_SIM_PREFS` from its own
scratch store. An explicit path is retained for scripted and interactive runs,
so intentional NVS writes are not truncated at boot and `removePreferences()`
does not delete the caller's store. A fresh generated path is reserved with
`mkstemps()` and initialized with a four-byte zero entry count; an empty
`mkstemp` file would be parsed as an error by `PreferencesSim`.

Generated ownership is carried across the validated `FURBLE_SIM_RESTART_STEP`
re-exec with an internal exact-path-plus-origin-PID marker. `execvp()`
preserves the PID, and the resumed boot retains the same generated path only
when both path and origin PID match. A different PID treats the path as
caller-owned and does not adopt or remove it. Final orderly cleanup removes
only that owned primary and its existing PID-specific `.tmp.<pid>` file.

Cross-run stale sweeping, PID-liveness reclamation, sidecar locks, failed
restart cleanup, and abnormal-exit cleanup are deliberately deferred. A dead
encoded PID cannot prove that another process did not intentionally select the
same store, so this slice makes no cross-process deletion claim.

The focused subprocess gate is `sim/scripts/check-preferences-lifecycle.sh`.
It checks explicit-store use and persistence across independent launches,
stale-marker non-adoption, unique generated names after abnormal exits,
retention of unrelated crash leftovers, and same-PID restart cleanup. The source
cleanup also removes an existing owned
PID-specific temporary file, but this gate does not inject one across a live
process boundary. It requires an already-built simulator and was not run in
this handoff.

Root validation recorded at `dd0e39443a24ab30b0bab728e9712ad35f03deb5`
(2026-09-13) passed the simulator build, the preference lifecycle gate, and
all eight `actualPreferencesSim` CTest cases. The evidence is outside this
checkout in `~/b/prefs-ownership-build.log`,
`~/b/prefs-ownership-lifecycle.log`, and
`~/b/prefs-ownership-host-test.log`; this checkout did not rerun those gates.
The lifecycle script is now wired into the existing `sim-e2e` S3 job with a
two-minute step timeout; the next CI run remains pending.

## Validation update: frozen scheduler merge

On 2026-09-13, root validated the clean frozen commit
`8ac8b833ca4bf64af277813a9c8184b0f7140f6f` in
`~/wt/scheduler-merge-0913`, using the shared dependency cache from
`~/wt/c53-lto/.pio/libdeps/m5stack-core-debug`. The exact per-phase evidence is
in the unique output directory `~/b/scheduler-8ac/`:

- host configure and the two targeted scheduler/watchdog targets built with
  `--parallel 2`; `sim-scheduler` and `sim-watchdog` passed 2/2 in 0.13 s
  (`host-configure.log`, `host-build.log`, `scheduler-tests.log`);
- the M5StickS3 simulator build passed (`sim-s3-build.log`);
- assertion status passed for the positive case, injected scheduler-stop
  case, diagnostic banner, and all four negative fail-fast fixtures
  (`assert-exit.log`);
- simulator-owned preference lifecycle passed
  (`preferences-lifecycle.log`);
- the pinned eight-seed, 600-step fuzz run and seed-2 determinism replay
  passed (`fuzz.log`).

These are host/simulator contract results only. They do not certify physical
boards, radio timing, sensor behavior, power behavior, or full scheduler
parity; CI and the documented hardware gates remain pending.

The same frozen source subsequently passed the full host build and all 119
CTest cases in 188.38 s, serialized with at most two compiler jobs. Evidence:
`~/b/scheduler-8ac/host-full-build.log` and `host-full-test.log`. The publication
successor changes only this provenance and clang-format wrapping in the
fail-fast call; it does not change the validated behavior.

## Follow-up: host TSAN flag races

Five local Clang/aarch64 TSAN probes reported two concrete plain-flag races
outside the scheduler mutex path: `Target::m_Stopped` was written by the target task at
`src/FurbleControl.cpp:180` while `targetTasksStopped()` read it at line 649;
and `Control::m_State` was written by `setState()` at line 1112 while
`Control::task()` read it at line 373. The atomic follow-up changes only these
cross-thread representations: `m_Stopped` is an acquire/release atomic boolean,
and `m_State` is an acquire/release atomic enum. Existing mutexes still protect
compound transitions and associated power-lock operations.

This follow-up does not claim that every Control flag is race-free. The
remaining reconnect/session fields are separate audit items. Firmware, CI TSAN,
and hardware validation of this follow-up remain pending.

The first PR306 CI host run failed `control-connect-camera-race` under GCC
ThreadSanitizer. Its filtered output named the getter without identifying the
raced memory; the published c4d31 wrapper now prints the complete report on
that existing failure path while retaining its predicate and exit status.

Root validation of the atomic follow-up at `7648c251c71a4587f09065e84767b43cc1bdab4c`
then passed the focused host build and tests, the full host suite passed 119/119
in 187.12 s, and the raw Clang TSAN probe exited 0 with no warnings. Evidence
is retained in `~/b/scheduler-tsan-7648/{config,build,test,raw}.log` and
`~/b/scheduler-tsan-7648/full-{build,test}.log`. This is not a claim that all
Control state is race-free: the remaining flags listed above and GCC/CI TSAN
coverage remain separate follow-up work.

## Follow-up: remaining Control flag synchronization

The bounded static audit identified three additional plain cross-boundary
flags: `m_ConnectAbort` is written by UI/control-entry paths and read by the
control task and debug snapshot; `m_ConnectInProgress` is written by the
control task but read outside its `m_Mutex` snapshot sections by teardown
predicates and the debug snapshot; and `m_SleepLockHeld` is updated under
`m_StateMutex` but sampled unlocked by the debug snapshot. This follow-up
converts only those three flags to `std::atomic<bool>` with explicit
acquire/release operations. Existing mutex sections, state publication,
power-lock calls, queues, cancellation, and timing remain unchanged.

The change is based on the static access audit and existing concurrency
regressions; it is not raw TSAN proof for these three flags. Raw TSAN, firmware,
CI, and hardware validation remain pending, and reconnect fields are outside
this scope.

## TSAN probe coverage extension

The dedicated `control-connect-camera-race` target now defines
`FURBLE_CONSOLE`, so its existing production `getDebugState()` snapshot is
polled alongside `getConnectingCamera()`. The probe performs two bounded
FauxNY connect cycles, waits for `STATE_ACTIVE` and then checks the
`disconnect()` result and bounded return to `STATE_IDLE`; it no longer relies
on a fixed sleep that may end before activation. Snapshot fields and the
connecting camera strings are consumed so this remains a real concurrent
reader, not a compile-only call.

This is regression coverage for the atomic flag boundary only. It adds no
test-only accessor, barrier, suppression, scheduler policy, or hardware claim.
The raw TSAN result remains the deciding evidence; the shell wrapper contract
is not a whole-program race-free guarantee.

The TSAN wrapper is a fail-closed full-report diagnostic gate: every sanitizer
warning and every non-zero child status fails, regardless of report names or
the sanitizer's conventional exit code. It must never classify races by member
name or suppress unrelated reports. The exact completion marker is required.
The existing TestSync signal/wait barriers establish happens-before ordering
for operations performed around those waits; they are not a substitute for
atomic synchronization on flags read outside the barriers.

## Follow-up: reconnect state field races

The bounded follow-up audit found four additional plain fields crossing the
control-task boundary: `m_InfiniteReconnect`, `m_ReconnectBackoff`,
`m_ReconnectAttempt`, and `m_ReconnectHintLogged`. UI or headless request paths
write the requested mode and reset values, `disconnect()` resets the attempt,
the control task reads and updates retry state, and the debug snapshot reads
the exposed values. The follow-up changes only those four fields to independent
acquire/release atomics. The retry increment uses `fetch_add` at the existing
increment site. The hint remains a separate atomic load/store in the existing
log order; it is not an exchange. `m_ConnectFailCount` remains a control-task
owned plain field.

This removes C++ plain read/write races only. The four atomics do not form a
coherent multi-field request, do not guarantee that a reset wins over a
concurrent retry, and do not define a new request or hint policy. Existing
mutexes, queues, cancellation, reset positions, delays, and camera behavior
remain unchanged. The new four-field source, its seven-field regression
coverage, and the fail-closed wrapper contract are not executed in this
integration handoff. Physical hardware validation remains separate.

## Integration handoff: seven-field boundary

Current master `34975a33f010e94105f985abf9aade824cd77468` is the merged PR #307
publication. Root's owner evidence records 30 green checks, including
firmware builds and reproducible firmware coverage, for the existing three
atomic fields (`m_ConnectAbort`, `m_ConnectInProgress`, and
`m_SleepLockHeld`). This checkout does not rerun that evidence.

Source `90e753347fc47f06dd170823d8243d46eb1819fa` adds the four independent
reconnect atomics (`m_InfiniteReconnect`, `m_ReconnectBackoff`,
`m_ReconnectAttempt`, and `m_ReconnectHintLogged`) on top of that publication.
Test `0f763fd92749fa0cf36340b0e2dc95d62017a0c3` covers the public debug
snapshot plus successful FauxNY connect/disconnect cycles. It does not cover
retry/backoff or `hintLogged`, because the FauxNY success path never enters the
retry path. Existing failure/backoff functional tests remain preserved.
Wrapper `8d3ea42076ae96686a08da069e61a67ee10347e9` makes the shell gate fail on
any warning or non-zero status and preserves the complete diagnostic output.
Root separately ran the wrapper contract on macOS and got `PASS`; a copied old
wrapper falsely accepted an unrelated warning with status 66 and printed
`1 race report(s), 0 naming guarded accessor`, while the new contract rejected
that same fixture. This is shell-contract evidence only, not execution of this
integrated tree or of a TSAN binary/runtime.
The new seven-field source/test/wrapper combination is source-integrated here
but has not been executed. A future raw TSAN retry/backoff run remains
explicitly pending; this handoff makes no runtime or hardware claim.
