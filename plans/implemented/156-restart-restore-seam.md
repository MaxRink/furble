# Plan 156: Restart and session-restore test seam

## Status

Implemented. Test and simulator infrastructure. The one production line it
touches is a counter promoted from a function local static to a member so a
reboot can clear it (deliverable 3); no firmware behavior changes. This is
the harness half of the reland gate for the boot-time session restore that
PR #159 introduced and PR #248 reverted.

Numbering note: 154 is claimed by PR #250 and 155 by PR #249, both merged.
156 is the next free number at branch time.

Rebased onto the plan 158 scheduler-parity foundation (PR #253) and the
scenario ownership manifest (plan 160, PR #258). Both changed how this seam
has to work; the adaptations are described under deliverables 1 and 2. A
review pass then tightened the reset ordering, the drain reap and the restart
shutdown request; those are recorded in deliverables 1 and 3.

## Motivation

The 2026-08-28 hardware incident (documented in PR #248) had two failure
modes. Plan 154 covered the first, the mid-cycle disconnect wedge. The second
was worse: after a reboot, the session restore re-armed both saved targets
(healthy X100VI plus a flappy standby GR IV) into an endless connect cycle,
so even a full restart did not recover the device. No harness could model
that, because nothing could model a reboot:

- The simulator is one process per scenario. `sim/PreferencesSim.cpp`
  already persists NVS to the `FURBLE_SIM_PREFS` file across processes, but
  no scenario verb could reboot the app.
- The host harness drives the singleton `Control::getInstance()` with no
  teardown or reinit, so a test could not run a pre-reboot session and a
  post-reboot session against the same machine.

This plan adds both seams and encodes the reboot-lockout invariant as a
permanent test, without needing the restore code itself. The restore reland
must then keep these green and add its own restore-behavior scenarios on
top.

## Deliverables

### 1. Simulator `restart` scenario verb

`sim/driver.cpp` gains a `restart` step. It models a device reboot by
re-executing the simulator binary (`execvp`) with the original arguments:

- All RAM state is wiped exactly as `esp_restart()` wipes it: every thread,
  singleton, LVGL, the virtual clock, driver counters.
- The NVS-backed preferences file persists exactly as flash does. A resumed
  boot skips the fresh-scenario preferences wipe in `preparePreferences()`.
- The resumed process parses the same script and continues at the step after
  `restart`, carried in the `FURBLE_SIM_RESTART_STEP` environment variable.
  All later verbs, queries, and asserts work unchanged.
- Parse-time validation: `restart` takes no arguments and must not be the
  final step (a resumed run starting past the last step would idle forever).
  A continuation index at or past the end of the script is rejected the same
  way. Both script gates have an `invalid` suite fixture,
  `restart-trailing.txt` and `restart-final-step.txt`, following the
  one-fixture-per-gate convention the strict parser (PR #257) established;
  the environment gate is checked alongside the other CLI and environment
  validation in `run-invalid.sh`.
- `FURBLE_SIM_RESTART_STEP` is consumed and unset on the boot that reads it.
  It suppresses the fresh-scenario preferences wipe, so a copy left exported
  in a shell would make every later scenario resume mid-script against a
  preserved store. It lives exactly one boot, and `docs/sim.md` says so.

Re-exec, not in-process teardown, is the reboot model. Plan 158 made an
in-process teardown possible (`furble_sim_reset_tasks()` stops and joins
every simulator task), but it would not be a reboot: the UI, LVGL, and every
singleton would survive in RAM, so a scenario could not tell a value that
crossed flash from one that was never cleared. Re-exec cannot lie about
that.

The re-exec itself runs in `sim/main.cpp`, after the plan 158 orderly
shutdown. The `restart` step only raises its own shutdown request, so the UI
task returns and `runSimulator()` stops the scan, quiesces the rig, stops and
joins every task through `furble_sim_stop_all_tasks()`, and closes the panel.
`main()` then calls `Sim::restartProcess()`. Execing straight from the step
would replace the process image out from under running tasks, which models a
crash rather than a reboot.

The step deliberately does not call `requestExit(0)` the way `exit` does.
`requestExit()` is first-wins, so a zero pinned there would swallow every
failure raised between the step and the re-exec, including the enforced
liveness invariant, and the simulator would reboot anyway. The pending flag
is its own shutdown request instead, `exitRequested()` honours it, and
`main()` re-execs only while `exitResult()` is still zero. Recorded
violations under `seed liveness_check false` deliberately do not cancel a
reboot: that scenario has opted out of failing, and cancelling would end the
run early with a success status and the rest of the script unrun.
Preferences need no flush before the exec: `sim/PreferencesSim.cpp` writes
the whole store to a temporary file and renames it on every put.

Seam limits, stated honestly:

- Seeds are re-applied on the resumed boot, exactly like boot-time
  configuration. A scenario asserting NVS persistence must therefore not
  seed the setting it toggles, or the seed masks the persistence.
- The sim `CameraList` was a process-local fake with no NVS backing when this
  plan was written. Plan 161 replaced it with the production `CameraList` over
  `lib/preferences` on the simulator preferences file, so a saved camera now
  does persist across a restart. The `saved_camera` and `autoconnect` seeds
  still re-create it at boot; that is idempotent because
  `CameraList::add_index()` overwrites an existing entry by name rather than
  appending a second one.
- Restart during a live BLE session is untested. Plan 161 made the path
  reachable (`sim/main.cpp` tears the control session down before exit), but no
  scenario drives it: `restart-persist.txt` restarts from idle. A restart
  mid-session touches teardown, the NimBLE client pool and the saved-camera
  reload at once and needs its own scenario.
- The virtual clock restarts at zero (unless `clock_ms` is seeded), like a
  reboot resetting uptime. Wall-clock state, liveness counters, capture and
  report accounting restart too.
- Environment-selected capabilities (`FURBLE_SIM_*`) persist across the
  re-exec, matching hardware that does not change between boots.

### 2. Foundation e2e scenario

`sim/scenarios/e2e/restart-persist.txt`, registered in
`sim/scenarios/manifest.json` as a certified `e2e` entry owned by `sim-e2e`
on `m5stick-s3` with the `fauxny` capability its `saved_camera` seed needs
and an expected exit of 0 (plan 160 replaced the CI glob with that manifest;
`tools/check_sim_scenarios.py` rejects an unlisted scenario). It toggles
the `reconnect` setting through its real switch widget, seeds a saved
camera, restarts, then asserts the setting survived through the preferences
file, the camera list is rebuilt, the UI booted to the main page, and
navigation still works. The restore-behavior scenarios come with the PR #159
reland and build on this verb.

### 3. Host-side `Control::resetForTest()`

`src/FurbleControl.cpp` gains a reset guarded by
`FURBLE_HOST_CONTROL_TEST`, defined only by the `control_e2e_test` target in
`tests/host/CMakeLists.txt`. Firmware builds never define it, so no reset
surface ships on device. Eight other host targets compile the same
`src/FurbleControl.cpp` without the define and link clean, which is the local
evidence that the guard preprocesses away; the firmware builds in CI are the
on-device check.

The reset simulates the RAM side of a reboot, in the order the RAM side
requires:

1. Drop the command queue first. `disconnect()` publishes `STATE_IDLE` as
   soon as it hands the teardown to the drain, which reopens the control task
   to a queued `CMD_CONNECT`. That connect would run against a machine whose
   targets are already gone, where `allConnected()` is vacuously true, and
   publish `STATE_ACTIVE` over the fresh `STATE_IDLE` the reset ends with.
2. Quiesce the per-target teardown tasks through the production
   `disconnect()` path. A host thread cannot be killed mid-flight the way a
   reboot kills a FreeRTOS task, so every task must have left
   `Camera::disconnect()` before the reset frees what it writes through.
3. Hold `STATE_DISCONNECTING`, the state the control task treats as terminal,
   for the rest of the reset, and drop the queue again. Together with step 1
   no command, queued before or during the reset, can start a connect against
   half-reset state.
4. Reap the drain on the resetting thread through the production
   `reapZombieTargets()` predicate, with the drain deadline expired. That
   predicate is what knows a stopped target whose link still reads up belongs
   to a gone peer and needs `reclaimClient()` before it is freed; freeing
   such a target directly leaves the orphaned NimBLE client pointing at
   destroyed memory for its late supervision-timeout callback to write
   through. Expiring the deadline is right for a reboot: the device is going
   down, so there is nothing to wait a peer's supervision timeout out for.
   Anything whose task has not stopped stays quarantined, never freed here.
5. Park the control task. `disconnect()` waits out `m_ConnectInProgress`, but
   `connectAll()` clears that flag before its interruptible retry wait, so
   the task can still be sleeping out a reconnect backoff. A probe command,
   dropped by the state held in step 3, proves the task has left
   `connectAll()` and is back in its queue receive, which is where a
   freshly created task sits.
6. Wipe the targets and every session flag a reboot clears, reload the
   transmit power cap from settings the way boot does, and publish the fresh
   `STATE_IDLE` last.

`uxQueueMessagesWaiting()`, which step 5 uses, is real FreeRTOS API; the host
`control_e2e` FreeRTOS shim gained the same two-line implementation the other
queue calls already had.

Seam limits, stated honestly:

- The control task thread keeps running rather than being recreated; it is
  parked in its queue receive exactly as a freshly created task is.
- The consecutive-failure counter `connectAll()` uses for its non-infinite
  retry budget is now the member `m_ConnectFailCount` rather than a function
  local static, so the reset clears it with the rest of the session state.
  This is the one production change in the plan and it is a rename: the
  counter had process lifetime before and session lifetime now, which is what
  a reboot gives it on device anyway.
- Peer-side state (mock radio, virtual cameras) deliberately survives, as
  the cameras do not reboot with the remote.

### 4. Lockout invariant scenario

`restart-restore-commandable` in `tests/host/control_e2e/control_e2e.cpp`
encodes the exact hardware lockout without the restore code: arm a healthy
FujifilmVirtualCamera plus an autonomous flappy standby peer
(`setFlappy(1, 800)` from plan 154), run the connect cycle into the churn,
queue a `CMD_CONNECT` the way a user press would, `resetForTest()` (the
reboot, bounded and landing in a fresh IDLE), re-create both targets from
fresh Camera objects the way a boot restore rebuilds them from the saved
list, start the cycle again, and then require the machine to stay
commandable:

- the queued command never starts a connect: a watcher samples the state for
  the whole reset and, from the first IDLE its disconnect publishes, only
  IDLE and the held DISCONNECTING are allowed,
- the restored cycle starts within 500 ms, because the reset left the control
  task parked rather than sleeping out a leftover backoff,
- a disconnect during the restored cycle returns to IDLE within 3 s, with
  no late state republish, and
- a manual connect to the healthy camera afterwards reaches ACTIVE.

### 5. Gone-peer reboot scenario

`restart-stalled-peer-reclaim` covers the drain half. A peer whose
`ble_gap_terminate` stalls (`NimBLEClient::mockStallTerminate()`) keeps its
link reading up and never fires `onDisconnect`, which is exactly the target
`reapZombieTargets()` defers instead of reaping. The scenario resets on top
of that peer, then requires that the reset finishes without waiting out the
drain deadline, lands in IDLE with no targets, that the late supervision
timeout resolving after the pre-reboot Camera is dropped is a no-op rather
than a use-after-free, and that a connect after the reboot still reaches
ACTIVE. The use-after-free step carries no `check()`: the assertion is the
sanitizer. `control_e2e_asan_test` in `tests/host/CMakeLists.txt` builds this
harness with `-fsanitize=address`, and both restart scenarios are registered
against it as `control-e2e-asan-*` tests. It compiles `MockNimBLE.cpp` and
`lib/furble/Camera.cpp` directly rather than linking `furble_host_camera`,
because the access is the mock loading the freed callbacks object's vptr to
dispatch the late `onDisconnect`, and both the read and the freed object live
in that unsanitized library. On a mutated build the report is a `READ of size
8` in `NimBLEClient::mockCompleteStalledTerminate`, freed by the
`FujifilmBasic` shared_ptr destroy. So CI reports the class as an ASan abort
rather than a segfault or a pass.

## Mutation verification

Both host scenarios were verified against a mutation that only they catch.
Every mutation was reverted; none is left in the tree.

`restart-restore-commandable`: the reset was returned to its pre-review
ordering, wiping the command queue only at the end and never holding
`STATE_DISCONNECTING` across the reset. The `CMD_CONNECT` queued before the
reboot is then dequeued the moment the internal `disconnect()` publishes
IDLE, connects against zero targets and publishes a live state mid-reset. The
scenario fails on `a command queued before the restart never starts a
connect` with `leaked state connecting`, three runs out of three, and it is
the only failure in the whole suite: 86 of 87 pass.

`restart-stalled-peer-reclaim`: the reset was returned to waiting out the
drain instead of reaping through the production predicate, and to freeing
every stopped zombie directly with no `reclaimClient()`. The gone-peer reboot
then costs the full `DISCONNECT_DRAIN_RECLAIM_MS` and the scenario fails on
`the gone-peer restart does not wait out the drain deadline`, again the only
failure in the suite: 86 of 87 pass.

Recorded because they cost time: the first mutation tried was suppressing the
interactive `disconnect()`'s `setState(STATE_IDLE)`. It does fail
`restart-restore-commandable` on three checks, but it also fails
`fresh-connect`, `dead-camera-disconnect-no-freeze`,
`multi-flappy-disconnect` and `flappy-peer-autonomous`, so it says nothing
about this scenario in particular. Removing the `m_ConnectAbort` break in
`connectAll()` and never arming `m_ConnectAbort` in `disconnect()` are both
absorbed by the state machine's redundant abort channels (the DISCONNECTING
state polls in the retry wait, plus the per-camera connect-cancel tokens), so
the scenario keeps passing. That redundancy is a robustness property of the
current design, not a gap in the test. Those runs did expose that the
original one-second late-republish spot check was shorter than the 2.5 s
first-retry backoff, so the scenario watches a continuous 3.5 s quiet window
instead.

Not directly reproducible, and stated as such: the branch where the reset's
drain wait times out with a target still quarantined needs the control task
parked inside a long connect for 30 s, which the harness cannot arrange
without a peer that blocks the handshake for that long. The fix removes the
unsafe free rather than the test proving that branch; what the scenario does
prove is that the gone-peer path reaps through the production predicate,
promptly, with no late-callback use-after-free.

## Verification

Re-verified after the rebase onto the scheduler-parity foundation and again
after the review fixes.

- Full host suite green (`ctest`, 90 of 90 as measured in CI, the 88 on master
  plus the two `control-e2e-asan-*` tests).
- The sanitizer coverage is a committed configuration, not a local run: the
  `control_e2e_asan_test` target builds this harness with
  `-fsanitize=address`, and both restart scenarios are registered against it,
  so `ctest` runs each of them twice, plain and instrumented.
- Full sim e2e suite green (`sim/scripts/run-e2e.sh`, 74 of 74, the 73 on
  master plus the new `restart-persist` scenario), whose log shows the
  orderly shutdown, the fresh boot after the re-exec, and the persisted
  setting surviving it.
- Full sim invalid suite green (`sim/scripts/run-invalid.sh`, 37 of 37, the 35
  on master plus the two new restart fixtures, plus the two command-line and
  environment checks: a continuation index past the end of the script is
  rejected with status 2, and a failing assert placed after a `restart` step
  exits 1 instead of being masked by the reboot). The bughunt (7) and
  power-gate (9) suites are green too, because the parse change is on the
  shared path.
- `tools/check_sim_scenarios.py` reports a complete manifest.
- The post-restart failure fixture lives in `sim/scripts/`, not
  `sim/scenarios/`, because every manifest suite declares a fixed exit status
  (0, or the parser's 2 for the invalid fixtures) and this fixture exits 1.
  Registering it would have meant loosening the invalid suite's contract from
  "the parser rejected this" to "this failed somehow", which is a worse
  trade than one fixture next to the other non-manifest scripts.
- The reset is preprocessed away outside its one target: eight other host
  targets compile the same `src/FurbleControl.cpp` without
  `FURBLE_HOST_CONTROL_TEST` and link clean, and the define appears in no
  firmware build file. CI's ten firmware builds are the on-device check; no
  firmware build was run locally for this branch.
- clang-format 21 clean.

## Follow-up

Review findings from the merge of PR #251, landed as a follow-up.

- The post-restart failure fixture (`sim/scripts/restart-post-failure.txt`)
  does not discriminate the pending-flag fix. Its failing assert runs in the
  resumed process, so it guards the re-exec chain against masking a failure
  that happens after the reboot, which is worth keeping, but it says nothing
  about a failure raised before the exec. That window is now essentially
  unreachable anyway: the `restart` step advances `stepIndex` and returns, and
  `UI::task()` checks `Sim::exitRequested()` on the line after `driverTick()`,
  so the UI task leaves at once and no further driver tick runs. The pending
  flag not pinning the exit code is still the right shape, because it is what
  keeps the `main()` gate meaningful for anything a background task might
  raise during the shutdown itself.
- `control_e2e_asan_test` now builds the end-to-end harness with
  `-fsanitize=address` and registers both restart scenarios as
  `control-e2e-asan-*`, so the gone-peer use-after-free class is a committed CI
  configuration rather than a manual local run. The step it guards has no
  `check()`; a comment says why, since the sanitizer is the assertion.
- `reapZombieTargets()` and `teardownDraining()` are no longer control-task
  only. Their header comments say so, and say that both take `m_Mutex`.
- The queue drop that closes the leftover-command window is not airtight: a
  command enqueued in the microsecond between `disconnect()`'s own
  `STATE_IDLE` publish and the `STATE_DISCONNECTING` hold could still be taken.
  Nothing enqueues there, because the reset models a reboot the whole device
  takes and the only queue writers are the UI and console tasks. Recorded in
  the code rather than papered over. The probe comment is likewise honest
  about what it proves: the task has left `connectAll()` and come back round
  through `xQueueReceive()`, and whatever is left of that iteration can do
  nothing in the terminal state being held.
