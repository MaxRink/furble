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
it is `Sim::SchedulerMutex`, whose ownership and waiter list are serialized by
the virtual scheduler.

The first implementation wrapped a native `std::mutex`: a contended task marked
itself blocked before the native lock and runnable after acquiring it. That
closed the long invisible wait, but left a smaller wake gap. On seed 31337 the
control task released the camera mutex at virtual tick 470; the target host
thread sometimes had not republished itself when the UI checked for runnable
work, so the UI entered one extra 20 ms disconnect slice. Identical events then
reached different timer phases and page-coverage counts.

Unlock now selects exactly one registered waiter by task priority then wait
order, reserves mutex ownership for it, and publishes it as runnable while the
scheduler mutex is still held. Only then is the scheduler condition signalled.
A newcomer therefore cannot barge into the wake gap, and UI quiescence cannot
complete while the selected waiter is unpublished. Cancellation unlinks an
unselected waiter or transfers a selected reservation before unwinding. A
global stop unwinds a registered task with the scheduler's task-exit exception
and an unregistered UI waiter with the matching simulator-thread exception.

This is the smallest seam that closes the measured defect. Only
`Camera::m_Mutex` changes type. It is the one host mutex production holds for
seconds; `m_ConnParamsMutex`, `Scan::m_StateMutex` and the rest are held for
microseconds and have never been measured to move a run. Widening the alias is
mechanical if one of them ever does.

The simulator selects mutex waiters by priority but does not implement FreeRTOS
mutex priority inheritance. Instruction-level preemption, SMP/core affinity,
and CPU-time accounting remain explicit limits; this seam is not complete
hardware mutex parity.

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

The guarded replay later exposed a separate UI ordering defect after the mutex
wake gap was fixed. Companion pairing and connect progress could both be
visible, while their footer buttons shared the page's encoder group. PREV moved
from pairing Accept to the background connect Cancel; SELECT therefore either
closed pairing or cancelled the camera according to timer order, changing the
visited-page report with the same event stream. Real dialogs now own stacked
encoder groups containing only their footer controls. The focused regression
covers both connect/pairing creation orders, no-touch PRESET and SHUTTER mode
restoration, and out-of-order closure of an underlying low-battery warning.

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

The final integration also passes the repository-wide clang-format check for
the simulator and host test sources. This is formatting-only and does not
change simulator behavior.
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
the selected waiter has acquired the mutex. The same case now also calls
`try_lock()` immediately after `unlock()`, while the granted waiter is not yet
runnable, so the reservation-before-wake race is covered without a separate
seam.

Root validation of this runtime and its new test path is recorded below. CI,
hardware, and physical scheduler-parity results remain separate gates. The
repeated high-load virtual-time-bound proof remains separate open work.

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

`preparePreferences()` reclaims generated stores whose encoded pid is dead
when it is about to create its own store. Live, self, and foreign-owner stores
are kept, and a caller-provided `FURBLE_SIM_PREFS` path returns before the
sweep. Sidecar locks, failed-restart cleanup, and deletion of a store whose pid
is live remain deferred; an encoded pid cannot prove more than liveness.

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

## Follow-up state: docs capture readiness, 2026-09-13

The Connected screenshots previously used a fixed virtual-time sleep followed
immediately by `capture`. A capture could therefore preserve the Connecting
modal while the Control task was still in `STATE_CONNECTING`, and the later
script steps could cancel that incomplete connection. All six capture scripts
that produce a Connected frame now put `assert-eventually-virtual 60000 ui.connected yes`
immediately before the capture. The query is the existing composite predicate:
the Connected page is current, the progress box is hidden, and Control reports
`STATE_ACTIVE`. The 60000 ms ceiling is a finite virtual-time wait that keeps
the UI and connection tasks running, not an unbounded sleep or a claim that a
connection will succeed. Remote captures also assert `ui.page shutter` after
the blind-entry action.

This gates the screenshot artifact only. It does not alter production
connection behavior, and this handoff did not rerun the gallery build or any
tests.

## Fuzz restart checkpoint follow-up

The simulator now carries only fuzz harness state across a real
`Platform::restart()` re-exec. The checkpoint preserves the configured seed and
budget identity, PRNG state, pending event metadata, recent history, findings,
aggregate counters, and FuzzMachine phase/counters. UI, Control, LVGL, task, and
application RAM are intentionally not restored. The driver owns PID-scoped
checkpoint files, ownership markers, bounded reads, atomic publication,
consumption, and cleanup. The fresh process gives the real UI one boot cycle
before resuming the saved APPLY, SETTLE, CHECK, or ESCAPE phase.

`sim/scripts/run-fuzz-restart.sh` is the pending execution gate. It must prove
two exact seed-2/600-event boots, one exact 600-event aggregate summary,
malformed and foreign checkpoint rejection with status 2 and unchanged bytes,
and cleanup of the generated checkpoint. No execution result is claimed by
this plan yet.

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
raced memory. The wrapper now prints the complete report on that existing
failure path, retaining its predicate and exit status. Five local Clang probes
each reported two other races, on target `m_Stopped` and Control `m_State`;
the local passing wrapper therefore is not evidence of a race-free program.
CI diagnosis and those production races remain unresolved at this checkpoint.

## Restart validation update

Root's exact runtime evidence now covers the restart harness and S3 matrix:
`~/b/pr273-057-host-test.log` records the host restart regression,
`~/b/pr273-c190-restart.log` records the real seed-2 re-exec, and
`~/b/pr273-c190-fuzz-s3.log` records eight S3 seeds at 600 events plus replay.
These are external validation artifacts; this checkout did not rerun them.

Root's final formatting check found clang-format 21 violations in the new
checkpoint and restart code. The publication successor applies formatting
only to `driver.cpp`, `fuzz.cpp`, and `fuzz_machine.cpp`. The three-panel
build and runtime matrix for the combined master/restart/legend source remain
pending; the earlier S3 results above are not substitutes for that matrix.
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
remain unchanged. The combined source for the three remaining flags and four
reconnect fields, its seven-field regression coverage, and the fail-closed
wrapper contract are not executed in this integration handoff. Physical
hardware validation remains separate.

## Integration handoff: seven-field boundary

Published master `0844360be35db547eb68ea6b56ef4560dccc8b59` is the base for this
follow-up. PR #307 separately merged only `Control::m_State` and
`Target::m_Stopped`; its 30 green checks, including firmware builds and
reproducible firmware coverage, do not cover the seven fields below.

Relative to that published master, source
`90e753347fc47f06dd170823d8243d46eb1819fa` makes the three remaining
cross-boundary fields (`m_ConnectAbort`, `m_ConnectInProgress`, and
`m_SleepLockHeld`) acquire/release atomics and adds four independent reconnect
atomics (`m_InfiniteReconnect`, `m_ReconnectBackoff`, `m_ReconnectAttempt`, and
`m_ReconnectHintLogged`). Thus the integrated source covers all seven remaining
fields relative to master; it does not relabel PR #307's two already-merged
fields as part of this change.
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
At this handoff, the seven-field source/test/wrapper combination was
source-integrated but had not yet been executed; the completed root validation
is recorded below. A raw TSAN retry/backoff run remains explicitly pending;
this handoff makes no runtime or hardware claim.
The compiler-free wrapper contract stays in the normal host CTest set but is
excluded from `FURBLE_COVERAGE`, because its shell command cannot emit a
`.profraw` file; the standalone `sh tests/host/run_tsan_race_contract.sh`
check remains available.

## Publication update

Published master `0844360be35db547eb68ea6b56ef4560dccc8b59` merged PR #308
after the seven-field integration was prepared. PR #308's reported 24 CI
checks are green, and its simulator power-accounting source is now part of the
master base used by this checkout. At that publication handoff, the
seven-field source, regression, and wrapper integration above had not yet been
executed; root's completed full-host validation is recorded below. Physical
camera, radio, and power accuracy remain outside this evidence.

## Host link follow-up

Root's full-host build at `bc5f5240` stopped while linking the TSAN target
because enabling `FURBLE_CONSOLE` exposed the production `BtDebugJournal`
symbols without linking `lib/furble/BtDebugJournal.cpp`. The TSAN target now
uses the same production source already linked by `control_abort_republish_test`.
The failed build log is `~/b/control-atomics-bc5-build.log`; root's corrected
rerun is recorded below. The original failure was a host-link issue only.

## Root validation of the integrated source

Root validated clean commit `70844df1485a39a075bb74345f69e6f8615a8844` in the
fresh frozen VM checkout. The full Clang 14 host build and all 120/120 tests
passed in 195.05 s. Three raw TSAN runs exited 0 with zero warnings and the
exact `control-connect-camera-race: PASS` marker. Evidence is in
`~/b/control-atomics-708-{build,test,raw-1,raw-2,raw-3}.log`.

The coverage configure listed 118 instrumented tests. It excluded both the
TSAN race target and the shell-only wrapper contract, and the existing floors
remained intact. The compiler-free wrapper contract passed separately; the old
wrapper negative control failed as expected.

The nonpublishable mutant `4485df074` reverted only the seven atomic fields and
their accesses to the published-master form. It built and ran with status 66,
reported five TSAN races, and still emitted the exact PASS marker. Evidence is
in `~/b/control-atomics-mutant-708-{config,build,raw}.log`. This is aggregate
coverage evidence only. It does not exercise retry/backoff or `hintLogged=true`
and does not prove each of the seven fields separately.

Firmware CI and physical hardware validation remain pending. These host and
TSAN results make no camera, radio, or hardware-parity claim.

## Open issue disposition (2026-11-02)

The scheduler-visible `Camera::m_Mutex`, the queued-waiter runtime, fatal
diagnostics, and the preference-ownership slice are merged on master, including
PR #286 (`96a05b38`) and its follow-ups. This plan stays the tracker for the
residual items below; it is not an unmerged implementation plan.

### Issue #283

Fatal diagnostics are merged: argument and scenario parsing are covered, every
registered simulator thread gets an alternate signal stack, handlers use
`SA_ONSTACK`, and the handler prints the phase and scenario line before a
best-effort unwinder. True stack-overflow coverage remains future work. The
root cause is now identified as an environment use-after-free; see the
2026-11-13 section below.

### Issue #289 follow-ups

| Item | State |
| --- | --- |
| 1. Comment rot naming plan 158 Phase 3 | Done. `sim/freertos.cpp` and `sim/scripts/run-fuzz.sh` now describe the scheduler-visible mutex and the remaining plain host locks. |
| 2. Per-PID preferences leak on abnormal exit | Done. Generated stores have an ownership marker, same-PID restart adoption, and orderly cleanup. `preparePreferences()` now sweeps `.pio` and removes a store only when its encoded pid is dead; a live, self, or foreign-owner store is kept. `preferences-sweep` covers the dead, live, self, and unrelated cases. |
| 3. Crash handler hardening | Done for alt-stack, early installation, and normal-context unwinder warm-up. Stack-exhaustion coverage and formal async-signal safety remain open. |
| 4. 50-run load proof on the restored bounds | Done, 2026-11-14. The 2026-11-13 settle failure was a scheduler-invisible wait: `Scan::stop()` waited on a plain condition variable for a callback on the radio task. It is now `scan_condition_t`, scheduler visible under `FURBLE_SIM`. 6,000 container runs at 200 to 600 per cell on all three panels had zero failures, no breaker parks, and every clock value within 80 ms of the median. See the 2026-11-14 section. |
| 5. Fairness and waiter-state dump | Done. A task blocked on a scheduler-visible mutex reports `wait=mutex` in the task dump, and `sim-scheduler` checks the dump plus a `try_lock()` during the reservation-before-wake gap; the fast path must fail rather than starve the granted waiter. |

## Issue #283 root cause and the #289 load proof, 2026-11-13

### #283: a growing setenv frees `environ` under the SDL loop

glibc's `getenv` walks `environ` without a lock. A `setenv` that adds a
variable reallocates that array. The first growth in a process copies the
initial stack array and frees nothing. Every later growth calls `realloc`, which
frees the old array whenever it moves. The SDL main loop on the main thread
calls `getenv` for SDL hints thousands of times a second. A growth on another
thread can therefore free the array that walk is reading. The freed chunk's
first words become tcache pointers, and dereferencing them is a SIGSEGV. The
window is one short array walk per call, so it takes a preempted walk on a
loaded host to hit it. That fits a single shot under parallel workers that
goes green on rerun.

Measured with an `LD_PRELOAD` probe that counts `getenv` calls after
`SDL_Init` and reports every `setenv` with its thread. The runs were in an
ubuntu:24.04 container (glibc 2.39, clang 18, arm64), against
`bughunt/power-off-state-matrix.txt` on the 320x240 build:

| Build | First boot, simulator thread, SDL live | Main thread getenv calls before the last growth |
| --- | --- | --- |
| pre-fix `8a94d2cb`, sim-e2e shape | `setenv(FURBLE_SIM_PREFS)` adds, then `setenv(FURBLE_SIM_RESTART_STEP)` adds | 35,971 |
| pre-fix `8a94d2cb`, coverage build, caller `FURBLE_SIM_PREFS` | the coverage runtime's own `setenv(__LLVM_PROFILE_RT_INIT_ONCE)` adds on the main thread before SDL, then `setenv(FURBLE_SIM_RESTART_STEP)` adds | 23,584 |
| master `e66e8807`, both shapes | `setenv(TZ)` adds, from production `Timezone`, after `SDL_Init` but before `panelReady` releases the loop | 24, all from `SDL_Init` itself; the loop has not started |

Both CI shapes on the pre-fix tree therefore had exactly one freeing growth, at
the first `restart` step, on the simulator thread, with SDL live. A second
preload that logs whether `environ` moved recorded `moved` for that
`FURBLE_SIM_RESTART_STEP` growth on 3 of 3 pre-fix runs. That is the step the
first sighting named. `a57e77e7` (2026-09-08) moved the Furble `setenv`
calls out of the SDL window. All three sightings were on 2026-09-06 heads that
predate it (`188b603c`, `cf5ec483`, `3c87f819`), and there has been no
sighting since. The coverage runtime explains the two coverage sightings: with
a caller-owned `FURBLE_SIM_PREFS`, the Furble code alone grew the environment
only once, which frees nothing.

Master has one remaining growth after `SDL_Init`, and it is not a live race.
Production `Timezone` (`f10b35ef`, 2026-11-04) calls `setenv("TZ")` in
`runSimulator()` at boot. `TZ` is normally unset on CI runners, so that call
adds a variable, and it can free the array. It runs before
`panelReady.store(release)`, though, and until then the main thread only
spins on `panelReady.load(acquire)`. No other thread walks `environ` at that
point: the M5GFX `dbg` thread is suppressed, and the watchdog reads its one
variable in `watchdogStart()`. The growth is therefore ordered before any loop
`getenv`. The 24 main-thread calls the probe counted all came from `SDL_Init`
itself. `main()` now creates `TZ` with `setenv("TZ", "UTC0", 0)` before SDL
starts, so every `TZ` write is a pointer-sized in-place replace. That lets the
guard enforce the stricter rule, no growth after `SDL_Init`, without an
exception for a boot-ordered call. A caller's `TZ` is kept, and the firmware
replaces it with the selected zone at boot either way.

The regression is the existing preload guard, widened and wired into CI.
`sim/env_mutation_guard.c` gains a `growth` target that rejects any variable
added by `setenv` or `putenv`, any `unsetenv` or value-less `putenv`, and
`clearenv`, from any caller, while SDL is initialized. It prints an armed line
so a guard that never loaded cannot pass. `sim/scripts/run-env-order.sh` first
compiles a small program that calls `SDL_Init` and then adds a variable, and
requires the guard to stop it with status 86 and the exact message. It then
runs smoke and `restart-persist.txt` under every target with `TZ` unset. The
old-binary legs are now optional. The sim-e2e S3 job runs it.

| Guard run, Linux container | Result |
| --- | --- |
| master `e66e8807` 320x240, `growth`, smoke | status 86, `sim env guard: setenv(TZ) after SDL initialization`. The stricter rule fires on the boot-ordered `TZ` growth; it was not a live race. |
| this branch 135x240, all targets, smoke and `restart-persist.txt` | passed |
| this branch with `FURBLE_SIM_ENV_GUARD_OLD_BIN` = `8a94d2cb` | passed, old binary rejected at both named targets |

The crash itself was not reproduced on demand. What was run:

| Runs | Build | Shape | Load | SIGSEGV |
| --- | --- | --- | --- | --- |
| 160 `power-off-state-matrix`, 8 workers | pre-fix `8a94d2cb` 320x240, Linux | sim-e2e | container loadavg 5 to 8 | 0 |
| 1500 one-restart probe (`wait 300`, `restart`, `wait 10`), 16 workers, plus six concurrent builds | pre-fix `8a94d2cb` 320x240, Linux | sim-e2e | container loadavg 17 to 48 | 0 |
| 3 one-restart runs with `getenv` stretched to 50 us per entry on the main thread | pre-fix `8a94d2cb` 320x240, Linux | sim-e2e | as above | 0 |

The stretch preload walks `environ` itself. The freed chunk loses only its
first two slots until another allocation reuses it, so a wider walk still
rarely reads a clobbered slot. A first 1500-run attempt is discarded: it ran
with its working directory on the host bind mount, and all 200 of its failures
were stalls in `open()` of the preferences store for the full 120 s watchdog
bound, none a crash. A macOS ASan batch of 120 runs is also discarded. All
120 ended in SIGABRT from sdl2-compat, or were killed while its dialog was up,
before reaching a scenario step. That is the sdl2-compat trap below. The runs
were stopped because each failure raised a desktop dialog.

So the mechanism is shown directly: the free happened on every probed pre-fix
run, SDL calls `getenv` constantly, and glibc's `getenv` takes no lock. Its
timing matches every sighting. `a57e77e7` removed the mid-loop growth, and the
guard now keeps every growth out of the window after `SDL_Init`. The crash
rate per run is too low to reproduce here. Reopen #283 if a
`SIM CRASH` dump appears on a head that contains this change. Treat a dump
whose faulting frame is in `getenv`, `secure_getenv` or SDL's hint lookup as
this class, and look for a new environment mutation the guard does not
exercise.

### sdl2-compat and ASan on macOS

Homebrew SDL2 is now sdl2-compat. It dlopens `libSDL3.dylib` and finds it
through its own `LC_RPATH`. The ASan runtime intercepts `dlopen`, so the
search runs with the executable's and the ASan runtime's rpaths instead.
SDL3 is not found, and sdl2-compat shows a modal "Fatal error! Cannot
continue! Failed loading SDL3 library" dialog and aborts. A five-line `dlopen`
probe reproduces this without SDL: the leaf lookup fails under ASan and
succeeds once linked with `-Wl,-rpath` to the SDL3 directory. `sim/build.sh`
now adds that rpath to sanitized Darwin links, resolved from
`pkg-config --variable=libdir sdl3` with `brew --prefix sdl3` as the
fallback. It was verified with `otool -l` only; no simulator was launched on
the host.

### #289 item 4: Secure 31000 ms and reconnect 20000 ms under load

Each scenario ran as an unchanged copy with `print clock.ms` added before its
final `assert_max clock.ms`. Every cell had 50 runs and three workers, and all
six cells ran at once. The two hosts are reported separately. They differ in
OS, libc, scheduler and the source of load, and the macOS batch lost runs to
boot stalls. Merged figures are given only as a cross-check.

**macOS host.** Plain builds of `e66e8807`, 12 cores. 36 spin loops ran for
the first part. Other agents' builds shared the host throughout. The sampled
1-minute loadavg was 36 to 142, mean about 80. 34 runs exited 3 on the stall
watchdog with `phase=settings` and virtual clock 0. They stopped in boot
before any scenario step, blocked in a filesystem call on the shared host
disk, and are not counted.

**Linux container.** ubuntu:24.04, glibc 2.39, clang 18, arm64, 12 vCPUs.
Plain builds of `e66e8807` were built and run on the container's own
filesystem. 24 spin loops (2x the vCPUs) ran throughout. The VM's 1-minute
loadavg was 63 to 99, median 76, over 88 samples. The VM also hosted other
agents' containers.

| Scenario, bound | Panel | macOS completed / failed | macOS p95 / max | container completed / failed | container p95 / max |
| --- | --- | --- | --- | --- | --- |
| `cancel-sweep-fuji-secure-ui`, 31000 | 135x240 | 43 / 0 | 18785 / 18875 | 50 / 0 | 18740 / 19665 |
| | 80x160 | 45 / 0 | 18800 / 18835 | 49 / 1 | 18735 / 18840 |
| | 320x240 | 45 / 0 | 18725 / 18815 | 50 / 0 | 18745 / 20960 |
| `cancel-sweep-reconnect-entry`, 20000 | 135x240 | 44 / 0 | 7285 / 7485 | 50 / 0 | 7435 / 8020 |
| | 80x160 | 45 / 0 | 7275 / 7480 | 50 / 0 | 7545 / 17850 |
| | 320x240 | 44 / 0 | 7325 / 7655 | 50 / 0 | 7635 / 8630 |

Medians are 18545 to 18610 on Secure and 7130 to 7190 on reconnect on both
hosts. Merged, every cell has 93 to 95 completed runs.

The clock bounds held on all 565 completed runs. They are not tight against
load in the tail, though, and the container run produced one failure:

- One Secure run on 80x160 failed the 6000 ms settle bound,
  `control.zombies expected '0' got '1'`, on its second cancel (offset 900
  ms). The log shows `Connected, transmit power requested ... set ok` from
  `Camera::connect()`, then the cancel. The UI's teardown then reached its
  30000 ms interactive cap with the target still running. Only after that did
  the target log `Connected`, `Securing` and a failed connect. The whole run
  took 18 s of host time. So the 30 virtual seconds were not spent waiting on
  host time slice by slice. The target task sat in some wait the scheduler
  counted as blocked while the UI advanced the clock. Between those two log
  lines it takes the plain `m_ConnParamsMutex` and runs the Fujifilm
  identity and bond lookups. Which wait it was is not established.
- One reconnect run on 80x160 reported 17850 against a 7135 median, 89
  percent of its bound. Single Secure runs at 19665 and 20960, and a
  reconnect run at 8630, show the same tail more mildly.

No bound was changed. Widening the clock bounds would hide the tail without
explaining it. The settle failure is the same class of defect issue #279
closed for `Camera::m_Mutex`: a wait the virtual scheduler does not see
correctly lets the clock run ahead of a task that is not progressing. Item 4
therefore stays open. The next step is to instrument the target task between
`Camera.cpp`'s transmit-power log and `FujifilmSecure`'s `Securing` log under
the same container load. Then make the offending wait scheduler-visible, as
was done for `Camera::m_Mutex`.

## Issue #289 root cause: the scan callback wait, 2026-11-14

### The wait

`FURBLE_SIM_SCHED_TRACE=1` (`sim/freertos.cpp`) prints the task table and a
native backtrace whenever the scheduler stops waiting for a task. That happens
when the 2 s stall breaker parks the turn holder, or when a UI handoff runs
out its 250 ms host bound with a task still runnable. It changes no scheduling
decision. On the first unloaded run of `cancel-sweep-fuji-secure-ui` on the
80x160 build, every cancel produced both events, all for the control task in
the same place:

```
SIM SCHED TRACE: stall breaker parked task=control clock_ms=700
  task prio=3 ... running=no blocked=no runnable=yes ... name=ble-radio
  task prio=4 ... running=yes blocked=no runnable=yes ... name=control
-- thread control --
pthread_cond_wait
Furble::Scan::stop()
Furble::FujifilmSecure::_connect()
Furble::Camera::connect(esp_power_level_t, unsigned int)
Furble::Control::connectAll()
```

The sequence is:

1. The radio task delivers the reconnect advertisement through the custom
   scan callback, `FujifilmSecure::onResult()`, which posts to the camera's
   queue. `Scan::handleResult()` counts that callback in flight.
2. The queue post wakes the control task. It has the higher priority, so it
   takes the turn before the radio task has left the callback.
3. The control task calls `scan.stop()`. `Scan::stop()` waits on the plain
   `std::condition_variable m_CallbackIdle` until no callback is in flight.
4. The control task is still runnable and still holds the turn, so the radio
   task never runs to finish the callback. Only the stall breaker breaks it,
   after 2 s of host time with no scheduler progress.
5. A parked task runs outside the turn, and the UI handoff ignores it. The UI
   therefore advances the virtual clock without waiting for the control task.
   That task continues through `Connecting`, `Connected, transmit power ...`,
   `Connected` and `Securing` before its next scheduler boundary.

On a loaded host, step 5 is the race in the failed run. The cancel landed
while the parked control task was between the transmit-power log and the
handshake. The UI's interactive teardown polled in 20 ms slices that no longer
waited for that task, so it ran out the 30000 ms cap in host milliseconds.
The task had not reached a boundary yet. The reconnect leg goes through the
same `Scan::stop()` on every cancel. The same trace there shows 14 of 14
parks on that stack, and that explains the 17850 ms reconnect outlier too. The
gap was never in `m_ConnParamsMutex` or the bond lookups. On firmware the
condition variable is a FreeRTOS block, so the radio task runs at once and
there is no defect.

### The fix

- `Sim::SchedulerConditionVariable` (`sim/clock.h`, `sim/freertos.cpp`): a
  waiter registers under the scheduler lock before it drops the caller's
  lock, blocks with `wait=condition`, and gets the turn back through the
  normal priority gate. A notify releases waiters by priority, then wait
  order. It does not preempt the notifier, because the notifier still holds
  the lock the waiter must take next and priority inheritance is not modeled.
  The waiter runs at the notifier's next scheduler boundary. Stop and task
  cancellation unwind the way `SchedulerMutex` does.
- `Furble::scan_condition_t` (`lib/furble/Scan.h`) is
  `std::condition_variable` in firmware and the host tests, and the scheduler
  type in a `FURBLE_SIM` build. This follows the `connect_mutex_t` pattern.
  Production lock discipline is unchanged.
- `sim-scheduler` adds the #289 shape: a priority 4 task waits on a
  condition that only a priority 3 task can make true. The waiter must report
  blocked with `wait=condition`. The notifier must run first and finish its
  turn before the waiter resumes.
- `tests/test_sim_wait_visibility.py` rejects any production
  `std::condition_variable` in `src/`, `include/` or `lib/furble/` that is
  neither sim-only nor aliased to the scheduler type. It flags `Scan.h:198`
  on the pre-fix header. Plain `std::mutex` is not covered. A short critical
  section that never crosses a scheduler boundary is harmless, and a mutex
  held across a boundary cannot be recognised by declaration. The trace is
  the tool for that class.

The other plain locks on the Control, Camera and Scan connect paths were
inventoried: `Control::m_Mutex`, `Control::m_StateMutex`,
`Camera::m_PairingMutex`, `Camera::m_CancelMutex`, `Camera::m_ConnParamsMutex`,
`CameraList::m_Mutex`, `CameraList::m_PersistenceMutex`,
`Scan::m_StateMutex` and `Scan::m_DispatchMutex`. None was converted. With
the fix, the trace recorded no breaker park in any of the 6,000 proof runs.
The full certified suites are below. The only remaining trace events are
described under "What is still not covered".

### Proof

ubuntu:24.04 container, glibc 2.39, clang 18, arm64, 12 vCPUs. Plain builds
of this branch were built and run on the container's own filesystem with
`FURBLE_SIM_SCHED_TRACE=1`. Each scenario ran as an unchanged copy with
`print clock.ms` before its final `assert_max clock.ms`, three workers per
cell, and all six cells at once.

Pre-fix baseline, same container, pre-fix 80x160 binary, Secure leg: 50 runs
had no failure, but there were 400 breaker parks, eight per run, one per
cancel. That harness did not hold its worker cap, so those 50 ran at once. It
is mechanism evidence, not load evidence.

| Pass | Load (1-minute loadavg) | Runs per cell | Result |
| --- | --- | --- | --- |
| 1 | 24 spinners, still decaying from a harness spike | 200 | 1,200 of 1,200 passed, 0 trace events |
| 2 | 36 spinners, median 49 over 30 samples | 200 | 1,200 of 1,200 passed, 0 trace events |
| 3 | 60 spinners, median 81.5 (41.7 to 89.1) over 208 samples at 5 s | 600 | 3,600 of 3,600 passed, 0 parks |

| Pass 3 cell | Bound | p50 | p95 | max | Handoff expiries |
| --- | --- | --- | --- | --- | --- |
| `cancel-sweep-fuji-secure-ui` 80x160 | 31000 | 18555 | 18560 | 18640 | 13 |
| 135x240 | | 18555 | 18560 | 18640 | 13 |
| 320x240 | | 18555 | 18560 | 18560 | 7 |
| `cancel-sweep-reconnect-entry` 80x160 | 20000 | 7115 | 7120 | 7120 | 5 |
| 135x240 | | 7115 | 7120 | 7125 | 3 |
| 320x240 | | 7115 | 7120 | 7120 | 4 |

That makes 6,000 runs and 1,000 per scenario and panel. None failed the 6000
ms settle bound, none had `control.zombies` above 0, and no clock value was
more than 80 ms above its median. Before the fix the same container produced
20960 and 17850. A run now takes a median of 2 host seconds instead of about
18, because no cancel waits 2 s for the breaker.

The full certified suites ran in the same container afterwards, unloaded and
one scenario at a time, with the trace on. End-to-end: 210 of 210 on 135x240,
65 of 65 on 80x160 and 69 of 69 on 320x240. Bug-hunt: 57 of 57 on 135x240,
54 of 55 on 80x160 and 50 of 51 on 320x240. The two failures are both
`timer-overflow-no-timed-wake` (`ui.overflow yes`). It fails the same way on
the pre-fix 80x160 binary, and plan 317 records it as a pre-existing failure
on those two panels. Across the whole suite the trace logged no breaker park
and one handoff expiry. That expiry was in `remote-reconnect-indicator`: the
control task held the turn inside its own `vTaskDelay` wait in
`FauxNY::_connect()`, which is host wake latency.

### What is still not covered

The 45 pass 3 handoff expiries are host wake latency at load 81, not hidden
waits. In each backtrace, the task holding the turn is in the scheduler's own
condition wait (`vTaskDelay`, `waitForTurnLocked`), or it is a newly created
task thread that has not reached its entry point. The UI waits its 250 ms
bound and then moves 20 ms of virtual time. That cost at most four slices,
80 ms, in 3,600 runs. Closing it fully would mean the UI waits without a host
bound for a task that holds the turn. That needs every remaining plain host
lock to be scheduler visible first, and it is not attempted here.
