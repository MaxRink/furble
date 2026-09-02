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

No assertion was weakened. Where a value converges across tasks rather than at
a fixed instant, the fixed `assert` became `assert-eventually-virtual` with an
explicit bound (2000 to 12000 ms), which is a bound, not a relaxation.

## Faults

`seed link_lies` and `action link-lies-kill` are gone. They constructed a
divergence by reaching behind the fake control state machine, which plan 158
explicitly did not want carried forward. In their place, `sim/scenario_action.cpp`
gained a strict typed set of transport faults:

- `action drop` / `action drop N` sever a live link with the GAP disconnect
  delivered (HCI reason 0x08, the supervision timeout shape).
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
- `false-connected-both-links-dead` reproduces hardware failure 3: two
  connected cameras, both links killed at the transport with the radio refusing
  reconnects, and the UI must leave the Connected screen inside the grace and
  show the reconnect indication with the correct count.
- `reconnect-backoff-curve` walks the real curve on virtual time: attempt 1
  holds `FIRST_RETRY_MS` 2500, attempt 2 holds `BASE_MS << 1` = 10000, and the
  counter resets to 0 on recovery.

## Mutation evidence

- Removing the `target->getCamera()->cancelConnect()` arm from
  `Control::disconnect()` (the #159 wedge class) leaves
  `control.connect_in_progress` at `yes` past the bound and fails
  `multi-target-flappy-disconnect`.
- Disabling the liveness invariant does not change
  `false-connected-both-links-dead`, because that scenario asserts the
  presentation directly; the invariant is a second, continuous net.

Both mutations were run and reverted; no mutation is left in the tree.

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
4. No modelled radio latency. A virtual peer's connect completes in well under
   a millisecond, so the sim cannot yet exercise the seconds-long connect
   windows that hardware has. Real timing must come from the calibrated peer
   work in plan 159.
5. The simulator tears the control session down before process exit
   (`Control::disconnect(..., forRestart=true)` in `sim/main.cpp`). The
   firmware never leaves `UI::task`, so this path only runs on the restart
   route on device. Without it the NimBLE client pool and the control targets
   are destroyed in an unspecified static order at process exit and the target
   destructor dereferences a freed client.

## Implementation state

Implemented by this PR. Plan 158 Phase 2 (production connection sources over
MockNimBLE and virtual peers) is complete; Phase 3 (scheduler fairness for
virtual-clock waiters, and calibrated peripheral/current models) is not.
