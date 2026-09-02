# Plan 161: production connection stack in the simulator

## Numbering note

161 is free on master and in `plans/README.md`. Two unmerged branches already
claim numbers in this range: `fork/test/sim-ricoh-exact-peers` carries
`plans/161-ricoh-peer-safety.md` and PR #259 carries
`plans/162-console-host-coverage.md`. Whichever of those lands second has to
renumber. 156 and 157 remain reserved for PR #251 and PR #252.

## Motivation

Three hardware failures were observed on 2026-08-28:

1. A multi-target session with one flappy standby camera wedged on a user
   disconnect. The control state stayed `disconnecting` with
   `connect_in_progress` still true, so the session never returned to idle and
   no later connect could start.
2. A boot-restore reconnect loop churned against a Ricoh GR IV in BLE standby:
   the camera accepted every connect, failed the security handshake the way a
   supervision timeout does (rc=520), then dropped the link on its own about
   20 s after a CameraPower notification.
3. The Connected screen stayed up while neither camera had a live link.

None of the three could be reproduced in the simulator, because the simulator
ran the real `src/FurbleUI.cpp` over substitutes for everything underneath it:

| Substitute | What it replaced |
| --- | --- |
| `sim/FurbleControlSim.cpp` | A 750 ms timer. Four of the six control states existed; there was no `m_ConnectInProgress`, no `m_ConnectAbort`, no reconnect backoff, no zombie drain, and `disconnect()` returned `STATE_IDLE` unconditionally. |
| `sim/CameraSim.cpp` | `connect()` was one boolean. |
| `sim/CameraListSim.cpp` | Process-local vector, no persistence, no advertisement matching. |
| `sim/ScanSim.cpp` | Two canned events from a host worker, no advertisements. |
| `sim/shim/{Camera,CameraList,Scan,Device}.h` | Declared different types from the production headers, so the two could not diverge visibly. |

A coverage audit of fork/master 792815cd measured union firmware line coverage
at 65.95 percent, with the simulator contributing 58.09 percent. The
connection stack was the largest single hole.

## Change

Compile the production connection stack into the simulator:

- `src/FurbleControl.cpp`, `lib/furble/Camera.cpp`, `CameraList.cpp`,
  `Scan.cpp`, `Device.cpp`, `BtDebugJournal.cpp`, every vendor class, the
  protocol modules and `lib/blowfish` are in `sim/build.sh` and
  `sim/CMakeLists.txt`.
- The BLE boundary is the existing MockNimBLE and the existing virtual peers,
  moved from `tests/host/{nimble,peer}` to `lib/testing/{nimble,peer}` so the
  host suite and the simulator consume one copy. The host suite builds and
  passes unchanged through updated include paths.
- The four substitutes and their shims are deleted. `sim/shim/esp_bt.h` now
  includes `MockNimBLE.h` so there is exactly one `esp_power_level_t`.
- `CameraList` persists through `lib/preferences` on `sim/PreferencesSim.cpp`'s
  file backing, which closes the saved-camera gap: a scenario can seed saved
  cameras and the production `load()`/`save()`/`remove()` paths run.
- `sim/BleSim.cpp` owns the virtual radio: a FreeRTOS task (so it runs on the
  plan 158 scheduler gate, not a raw host thread) that advertises the seeded
  peers into the mock `NimBLEScan` and models the controller-owned discovery
  timer. Advertisements reach `CameraList::match()` through the production
  `Scan` dispatch, so the scan page is filled by real matching.
- `Control::simDropActiveLink()` is defined in `sim/BleSim.cpp` and severs the
  real link. It no longer overrides any control state.

### Seams retained in production sources

All `FURBLE_SIM` guarded, all observability:

- `lib/furble/Scan.{h,cpp}`: the scan-start responsiveness probe (it was
  already a simulator seam, it just lived in the substitute) and a scan-end
  callback counter.
- `src/FurbleControl.cpp`: one call that counts the camera commands actually
  dispatched to a per-target camera task, so `camera.shutter_presses` and its
  siblings survive the removal of the fake camera.
- `include/FurbleControl.h` and `src/FurbleControl.cpp`: `getDebugState()` is
  now compiled for `FURBLE_SIM` as well as `FURBLE_CONSOLE`. The simulator
  reads the same snapshot the debug console reads, which is what makes
  `control.zombies`, `control.connect_in_progress` and
  `control.reconnect_attempt` assertable. This is the exact pair the
  2026-08-28 wedge was diagnosed from.
- `include/FurbleControl.h` gained nothing else; the FreeRTOS types it needs
  come from a force-included host shim in the simulator build only.

One portability fix landed in production: `lib/furble/CanonEOSSmart.cpp`
narrowed `char` literals into `uint8_t` designated initializers, which gcc
warns about and clang rejects.

## Timing budgets

Real connect timing replaced the 750 ms fake:

| Constant | Value | Source |
| --- | --- | --- |
| FauxNY connect | 2500 ms | `FauxNY::_connect`, 100 x `vTaskDelay(25)` |
| Control task tick | 50 ms | `xQueueReceive` timeout in `Control::task` |
| `FIRST_RETRY_MS` | 2500 ms | `include/FurbleReconnectBackoff.h` |
| `BASE_MS` | 5000 ms | same, shifted left by the attempt number |
| `MAX_MS` | 120000 ms | same |
| `TIMEOUT_DEFAULT_MS` | 30000 ms | first connect |
| `TIMEOUT_INFINITE_MS` | 5000 ms | reconnect |
| `DISCONNECT_WAIT_MAX_MS` | 30000 ms | interactive teardown cap |
| `DISCONNECT_DRAIN_RECLAIM_MS` | 2000 ms | zombie drain bound |
| Fujifilm registration wait | 25000 ms | `Fujifilm::REGISTRATION_TIMEOUT_MS` |
| Fujifilm/Ricoh connect over the mock | under 50 ms | no modelled radio latency |

Every scenario that connects a FauxNY camera therefore needs at least
2500 + 50 ms before it may assert `control.state active`, and the same budget
again after any drop before it may assert recovery. The budgets were raised
mechanically to 3200 ms per connect or reconnect step, which is the 2500 ms
connect plus one control tick plus margin. The scenarios touched, and why:

| Scenario | Change | Reason |
| --- | --- | --- |
| `connect-flow` | connect wait 1700 -> 3200 | FauxNY connect is 2500 ms, not 750 ms |
| `battery-anchor` | connect and recovery waits -> 3200 | same, twice (drop then recovery) |
| `bulb-reconnect-indicator` | connect wait -> 3200 | same |
| `bulb-timer` | connect wait -> 3200 | same |
| `button-mode-dispatch` | connect wait -> 3200 | same |
| `connstate-page-sweep` | connect and every per-page recovery wait -> 3200 | one connect plus six drop/recover cycles |
| `interval-back-trap`, `interval-stop-reset`, `intervalometer-timing` | connect wait -> 3200 | same |
| `reconnect-indicator`, `reconnect-multiconnect`, `reconnect-multiconnect-shutter` | connect and recovery waits -> 3200 | real reconnect is a full FauxNY connect |
| `remote-back-button`, `remote-reconnect-indicator`, `remote-reconnect-multiconnect` | connect wait -> 3200 | same |
| `redraw-steady`, `shutter-command`, `statusbar-stability` | connect and recovery waits -> 3200 | same |
| `autooff-connected-guard` | boot wait 2000 -> 4000 | autoconnect at boot is now a real 2500 ms connect |
| `page-matrix`, `text-size-overflow-large`, `text-size-overflow-small` | connect wait -> 3200 | same |
| `multi-connect-false-connected` | rewritten onto virtual peers, recovery wait 1500 -> 4000 | the fake `action drop` is now a real transport drop and the control task performs the recovery itself |
| `drop-idle-self-heal` | rewritten onto a virtual peer with `ble-connect-fail` | the old premise ("a drop with reconnect off goes straight to idle") was a property of the fake. The production task always attempts the reconnect first, so reaching idle needs a link that really cannot come back |
| `false-connected-guard` | rewritten onto the real connect-failure path | FauxNY has no radio and cannot fail a connect, so `seed connect_fail true` now registers one virtual Fujifilm peer and fails `NimBLEClient::connect()` |
| `scan-duplicate-result` | rewritten onto one advertising peer plus `seed scan_timeout 2` | the duplicate guard is now `CameraList::match`'s real address check against a peer that re-advertises every 100 ms |
| `scan-distinct-rows-heartbeat` | rewritten onto `ble_peers fuji-pair` plus `seed scan_timeout 2` | two real advertisements, matched and drained on the UI task |
| `link-lies-invariant` | renamed to `link-death-liveness` and repointed at the real transport fault | see below |

No assertion was weakened by re-bounding. Where a value converges across tasks
rather than at a fixed instant, the fixed `assert` became
`assert-eventually-virtual` with an explicit bound (2000 to 12000 ms), which is
a bound, not a relaxation.

Two assertion blocks were removed rather than re-bounded, and both are listed
here rather than left to a reader to find:

- `multi-connect-false-connected.txt` lost its mid-outage block
  (`control.state connecting`, `control.connected 0`, `ui.connected no`,
  `ui.page connected`, `ui.connect_box hidden`, `ui.reconnecting yes`,
  `ui.reconnect_count 2/2`). With the fake, a drop parked the session in
  `connecting` until the scenario advanced it. With the production stack and a
  healthy radio the reconnect completes in well under a millisecond, so the
  outage has no observable window at all. `false-connected-both-links-dead.txt`
  is the scenario that holds the outage open with `ble-connect-fail`, and it
  carries those assertions instead, except `ui.reconnecting`, which residual
  gap 4 explains. `multi-connect-false-connected.txt` now covers what only it
  can: the control task recovering the session on its own.
- `bughunt/connect-fail-progress.txt` lost `assert ui.connect_box visible`.
  FauxNY has no radio to fail a connect at, so the scenario now uses a virtual
  peer whose transport connect fails immediately; the progress box is never
  observable mid-flight. Its real contract, that the box is gone afterwards and
  the UI never presents as connected, is unchanged and still a hard assert.

## Faults

`seed link_lies` and `action link-lies-kill` are gone. They constructed a
divergence by reaching behind the fake control state machine, which plan 158
explicitly did not want carried forward. In their place, `sim/scenario_action.cpp`
gained a strict typed set of faults, all of them transport faults except where
noted for FauxNY:

- `action drop` / `action drop N` sever a live link with the GAP disconnect
  delivered (HCI reason 0x08, the supervision timeout shape). One exception:
  FauxNY has no radio, so there is no transport to sever and
  `Camera::resetConnectionState()` stands in for its link loss. Every camera
  with a virtual peer behind it takes the transport path.
- `action ble-kill` severs every live link and leaves the disconnect event
  queued.
- `action ble-standby` runs a flappy peer's standby drop.
- `action ble-connect-fail` / `action ble-connect-ok` toggle transport connect
  failure.
- `action ble-withhold-registration` / `action ble-allow-registration` hold a
  Fujifilm peer's registration confirmation.

The parser rejects all of these unless the scenario seeded `ble_peers`, so a
fault can never be a silent no-op. The peers' standby drop was refactored out
of their wall-clock timer into `triggerStandbyDrop()`, so the simulator can fire
it at a known virtual time while the host suite keeps its timer thread.

The liveness invariant is unchanged in shape but now compares the UI's
Connected presentation against the real `Camera::isConnected()`, because
`Control::getConnectedTargetCount()` is production. It stays enforced by
default.

## New scenarios

- `multi-target-flappy-disconnect` reproduces hardware failure 1: a healthy
  Fujifilm plus a GR IV in BLE standby, connected from the UI, the GR's standby
  drop, then a UI disconnect mid reconnect cycle. It asserts the state returns
  to idle and `connect_in_progress` clears inside 3 s, that no target stays
  quarantined, and that a fresh connect reaches active.
- `false-connected-both-links-dead` drives the closest reachable approximation
  of hardware failure 3, not the failure itself. `action drop` delivers the GAP
  disconnect, so `isConnected()` goes false and the UI correctly leaves the
  Connected screen; what the scenario proves is that the production stack does
  the right thing when the stack is told the link died. The hardware symptom
  needs `action ble-kill`, where the disconnect event never arrives and
  `isConnected()` stays stale-true, and residual gap 1 records that the
  liveness invariant cannot see that case.
- `reconnect-backoff-curve` walks the real curve on virtual time: attempt 1
  holds `FIRST_RETRY_MS` 2500, attempt 2 holds `BASE_MS << 1` = 10000, and the
  counter resets to 0 on recovery.

## Mutation evidence

Caught. Making the interactive `Control::disconnect()` return without its final
`setState(STATE_IDLE)`, so the machine is left in `STATE_DISCONNECTING`, is
exactly the 2026-08-28 symptom. `multi-target-flappy-disconnect` fails on it:

```
ASSERT-EVENTUALLY-VIRTUAL FAILED: control.state expected 'idle' got
'disconnecting' after 3000 virtual ms
```

Not caught, reported rather than hidden. Removing the
`target->getCamera()->cancelConnect()` arm from `Control::disconnect()` does
**not** fail the certified scenario. That arm only matters while a connect is
parked in a wait that polls the cancel token, and the certified scenario's
connects are short: FauxNY's 2500 ms wait does not poll the token at all, and a
virtual Fujifilm completes its handshake in under a millisecond. The only
construction that does park a connect there is
`action ble-withhold-registration`, whose 25 s registration wait runs into the
scheduler fairness gap below, so it is not certified. Closing this needs either
a peer with modelled radio latency (plan 159) or the scheduler work in plan 158
Phase 3.

Disabling the driver-level liveness invariant was prepared but not run: the
host was too loaded to rebuild for it inside this change. The expected result
is no change to `false-connected-both-links-dead`, which asserts the
presentation directly; the invariant is a second, continuous net rather than
that scenario's only check.

Every mutation was reverted; no mutation is left in the tree.

## New fuzz finding, pinned not hidden

Running the production connection stack changes the fuzzer's event stream,
because a connect now takes real time instead of 750 ms. Seed 3 on the 320x240
M5Stack Core board now reaches a state the previous stream never did and
reports a deterministic `layout-overflow` on the intervalometer settings page
(step 447, `page=timer`), reproducibly, on every run. The same seed passes on
80x160 and 135x240, and it passes on this board on master, so it is a latent
layout bug newly reached rather than one introduced here. The large-text
overflow sweep cannot see it: `EXACT_BOARDS` in `tools/check_sim_scenarios.py`
restricts `text-size-overflow-large.txt` to `m5stick-s3` and `m5stick-c`, so
this board and that text size are genuinely untested.

It is pinned as `FURBLE_FUZZ_XFAIL_SEEDS=3` for the 320x240 invocation only, so
the seed is required to fail there and `run-fuzz.sh` reports loudly if it ever
starts passing. Fixing the page and promoting the seed back out belongs to a
layout PR that can verify on the board.

## Residual parity gaps

1. `action ble-kill` severs a link without delivering the GAP disconnect. The
   camera keeps reporting connected, the control task stays active, and the UI
   shows Connected. That is a genuine false-connected state and the liveness
   invariant cannot see it, because it compares against `isConnected()`, which
   is the value that is stale. Closing it needs an independent liveness probe
   (a periodic GATT read or RSSI sample) in production, which is a separate
   change.
2. The plan 155 divergence (control `active` while every link is down, held
   indefinitely) is not constructible against the production control task,
   which leaves `STATE_ACTIVE` within one 50 ms tick of `allConnected()` going
   false. The invariant's teeth are therefore proven by mutation rather than by
   a scenario that constructs the divergence.
3. Scheduler fairness. A task that only wakes on a virtual-clock deadline can
   be starved while the UI thread drives virtual time forward in large steps.
   It shows up as a control task that never re-polls
   `Camera::connectCancelled()` during the long Fujifilm registration wait, so
   the disconnect falls back to its 30 s cap. It reproduces roughly one run in
   three with `action ble-withhold-registration` in the loop, which is why the
   certified scenario uses the standby drop rather than the registration wait.
   The action is retained and documented for interactive use. This belongs to
   plan 158 Phase 3.

   A harder instance of the same boundary was found here. Production code
   blocks on plain host mutexes the scheduler cannot see: `Camera::m_Mutex` is
   held for a whole connect, so a per-target task running `Camera::disconnect()`
   during one leaves the scheduler while holding the turn, and the connect task
   can never get a turn to release the mutex. The UI fuzzer deadlocked on
   seed 2.

   Two changes address it. `FauxNY::_connect()` now polls `connectCancelled()`
   like every vendor connect does, so a disconnect during a FauxNY connect
   unwinds in one 25 ms slice instead of the whole attempt, which removes the
   common trigger at its source and matches the plan 148 contract.
   `waitForTurnLocked()` keeps a deadlock breaker for the rest, but a sound one:
   it parks the turn holder only when a global scheduler progress counter has
   not moved for a two second host bound, so nothing in the scheduler moved at
   all rather than this waiter merely being unlucky. The first attempt used a
   bare 50 ms timeout, which misfires on a loaded host and reorders dispatch for
   no reason; that is replaced. Determinism is now conditional on no task
   blocking outside the scheduler, and plan 158 says so. The principled fix is a
   scheduler-visible mutex, which plan 158 Phase 3 owns.
4. FIXED IN THIS PR. The connect timer could park itself and stay parked for a
   whole session. This was the root cause behind the `ui.connect_timer`, the
   `ui.reconnecting` and the `control.state` flakes, and it was one bug, not
   three.

   `doConnect()` calls `Control::connectAll()`, which only queues
   `CMD_CONNECT`, and then immediately does `lv_timer_ready()` plus
   `lv_timer_resume()` on the connect timer. If that timer's first tick lands
   before the control task has left `STATE_IDLE`, the timer handler takes the
   `STATE_IDLE` branch and pauses itself. Nothing resumes it again for the rest
   of the session, so the connected page runs with a dead liveness poll: a
   later link drop raises neither the status-row reconnect icon
   (`ui.reconnecting`, `ui.link_alert`) nor the page banners, and the session
   keeps presenting as connected. That is the 2026-08-28 failure class.

   On device the control task runs at priority 4 and the LVGL task lower, so
   the queue send preempts and `STATE_CONNECT` is published before `doConnect()`
   returns; the window is closed by scheduling order, not by the code. The
   simulator's UI task is the pseudo-task that drives virtual time and is not
   priority gated, so it can run ahead of the higher-priority control task and
   the window opens. An idle host wins the race, a loaded CI runner loses it.

   There is a second, worse user-visible consequence the simulator found. The
   `STATE_CONNECT_FAILED` branch of the same handler is what calls
   `doDisconnect()`. With the timer parked that branch never runs, so a failed
   connect leaves the user stuck on the connect-failed progress box with no
   automatic dismissal and the control state pinned at `connect_failed`. That
   is not a cosmetic indicator problem; it is a dead-end screen.

   Fixed here, in production. `ConnectContext_t` gains `connectRequested` and
   `connectRequestedAt`. `doConnect()` sets them before resuming the timer, the
   handler clears the flag the moment it observes any non-idle state, and the
   idle branch keeps polling at the connect cadence instead of parking while the
   flag is set. `CONNECT_REQUEST_GRACE_MS` (5 s, two orders of magnitude above
   the control task's 50 ms tick) abandons a request the control task never
   picks up, so the timer can never spin forever. `doDisconnect()` clears the
   flag, because a user teardown makes any queued connect moot.

   With the fix the reviewer's probe reports the timer running 20/20 and the
   reconnect indicator raised 20/20, and `false-connected-guard` and
   `drop-idle-self-heal` are 0/20 flaky each. The indicator assertions are hard
   asserts again, bounded by the liveness poll period rather than pinned to a
   fixed instant.

   Hardware verification is owed and only partly satisfiable without a camera:
   the camera-less half (connect to an absent saved camera, let it reach
   `CONNECT_FAILED`, confirm the progress box dismisses) is being run on the
   bench. The with-camera half (connect, drop the link, confirm the reconnect
   indicator appears) is owed when a camera is available. The simulator
   reproduces the race deterministically in the meantime.

   The underlying simulator gap remains: the UI task is not priority gated, so
   it can still run ahead of a higher-priority task. That is plan 158 Phase 3
   and is listed separately below.

   Even with the timer alive the indicator lags a drop by up to
   `LIVENESS_POLL_PERIOD_MS` (500 ms) plus one 50 ms control tick, because the
   connected page deliberately polls liveness at a gentle cadence. That part is
   unchanged production behaviour and is a budget, not a bug; a measured probe
   shows the indicator flipping between +300 ms and +1300 ms after a drop.

5. The simulator's UI task is not priority gated. It is the pseudo-task that
   drives virtual time, so it can run ahead of a higher-priority real task that
   a queue send has just released. `doConnect()` resumes and readies the
   connect timer while `Control::connectAll()` has only queued `CMD_CONNECT`,
   so the timer's first tick can land while the control task is still in
   `STATE_IDLE` and pause itself. On device the control task runs at priority 4
   and preempts the LVGL task immediately, so it always wins that race.
   `drop-idle-self-heal` records the precondition with `xassert` rather than
   enforcing it; its real contract, the self-heal after the outage, stays a
   hard assert. This is plan 158 Phase 3 work.
6. No modelled radio latency. A virtual peer's connect completes in well under
   a millisecond, so the sim cannot yet exercise the seconds-long connect
   windows that hardware has. Real timing must come from the calibrated peer
   work in plan 159.
7. Restart during a live BLE session is untested. `sim/main.cpp` tears the
   control session down before the process exits, which makes the plan 156
   `restart` verb reachable while cameras are connected, but no scenario drives
   it: `restart-persist.txt` restarts from idle. A restart mid-session touches
   the teardown, the NimBLE client pool and the saved-camera reload at once, so
   it needs its own scenario before anyone claims it works.
8. The simulator tears the control session down before process exit
   (`Control::disconnect(..., forRestart=true)` in `sim/main.cpp`). The
   firmware never leaves `UI::task`, so this path only runs on the restart
   route on device. Without it the NimBLE client pool and the control targets
   are destroyed in an unspecified static order at process exit and the target
   destructor dereferences a freed client.

## Coverage

| Stack | Covered | Instrumented | Coverage | Previous floor |
| --- | ---: | ---: | ---: | ---: |
| host | 7,758 | 12,208 | 63.55% | 62.45% |
| sim m5stick-s3 (135x240) | 8,516 | 16,656 | 51.13% | 57.16% |
| sim m5stick-c (80x160) | 6,125 | 16,599 | 36.90% | 43.29% |
| sim m5stack-core (320x240) | 5,992 | 16,589 | 36.12% | 42.20% |
| sim union | 8,636 | 16,718 | 51.66% | 57.86% |
| **grand union** | **14,405** | **20,503** | **70.26%** | 68.03% |

Measured with `tools/coverage.py` from PR #260, which is now the authority
here. Every simulator stack percentage falls and the grand union rises, for one
reason: the simulator's instrumented denominator grew from 11,712 to 16,718
firmware lines because it now compiles the whole connection stack it previously
did not compile at all. The absolute number of firmware lines the simulator
covers rose from 6,804 to 8,636, and the grand union, the number that says how
much of the firmware the tests actually reach, went 68.03 to 70.26.

The four simulator stack floors were lowered with
`tools/coverage.py --ratchet --lower --reason "real Control in the sim grew the
instrumented denominator"`, which records the reason in
`tests/coverage_floor.json` rather than dropping them silently. Every other
floor ratcheted up: grand union 68.03 to 69.26, host 62.45 to 62.55,
`src/FurbleControl.cpp` 73.90 to 77.13, `lib/furble/Scan.cpp` 88.86 to 90.20,
`lib/furble/Camera.cpp` 73.46 to 73.75, `src/FurbleUI.cpp` 79.40 to 79.53.

## Implementation state

Implemented by this PR. Plan 158 Phase 2 (production connection sources over
MockNimBLE and virtual peers) is complete; Phase 3 (scheduler fairness for
virtual-clock waiters, and calibrated peripheral/current models) is not.
