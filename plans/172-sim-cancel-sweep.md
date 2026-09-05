# 172 - Simulator cancel sweep: the wedge the sim could not reach

Plan numbers through 171 are taken. 168 and 170 are claimed by open PRs, so
this plan takes 172.

## Motivation

On 2026-09-04 the bench wedged an M5StickS3 running master 8bdc52e4 with an
X100VI bonded. Twenty console cycles of `connect 0` then `disconnect` at 2 s,
4 s or 6 s into the connect, infinite reconnect on. Afterwards Control sat at
`disconnecting` with `connect_in_progress true` and `connecting none`, zombies
climbing 3 to 7 with no further commands, a fresh `connect 0` refused with
`already connecting, ignoring duplicate connect`, and `task.control` at 0.0
percent CPU, for more than two minutes until a reboot. Bench logs:
`bench-logs/cancel20-0904-2106.log`, `cancel20-final-0904-2109.log`,
`wedge-0904-2110.log`. Issue #271, fix in flight as PR #272 and PR #245.

The simulator runs the production `Control`, `Camera`, `CameraList` and `Scan`
over MockNimBLE and virtual peers (plan 161), has a scenario for a user
disconnect mid reconnect (`multi-target-flappy-disconnect`), has a cancel
scenario (`cancel-single-disconnect`), and it did not catch this. This plan is
about why, and about the class rather than the instance.

## The miss

Four ingredients. The hardware needed all four; no certified scenario had more
than two.

### 1. The cancel never landed inside a live connect

This is the one that made the rest unreachable. `Camera::connect()` holds
`Camera::m_Mutex` for the whole attempt, so a target task's
`Camera::disconnect()` blocks behind it, and the only early release is a wait
that polls `connectCancelled()`. `NimBLEClient::secureConnection()` is not such
a wait: it takes no cancel token and returns only when the controller finishes
the encryption procedure or the link goes away. That is the residual hole PR
#272 writes down and does not close.

`FujifilmVirtualCamera::secureConnection()` answered it in under a millisecond.
Plan 161 recorded this as residual gap 6, "no modelled radio latency", and its
consequence in the mutation section: removing the `cancelConnect()` arm from
`Control::disconnect()` does not fail any certified scenario, "because the
certified scenario's connects are short". Every certified cancel therefore
landed either before the attempt started or after it had finished. The window
the bug lives in did not exist in the simulator.

### 2. No repetition

`cancel-single-disconnect` clicks Cancel once. The bench needed a loop: each
cycle drains one more target that cannot be reaped, and the zombie count is
what climbs. A single cancel cannot show a count climbing.

### 3. Infinite reconnect was never on during a drain

The scenarios that seed `reconnect true` do it to watch a recovery, and cancel
nothing while it runs. On the bench the retry loop was running underneath every
teardown, which is what kept creating targets that drained into the zombie
list.

### 4. The peer answered the wrong shape

Even with a modelled duration, a handshake that ends on its own timer does not
model the stale-bond case, where the camera never finishes and only the link
supervision timeout releases the call. That is the case that outlasts
`DISCONNECT_WAIT_MAX_MS` and makes the interactive teardown break out of its cap
and drain a target whose connect is still running. `connecting none` together
with `connect_in_progress true` in the bench log is the proof that this is the
branch the hardware took: `m_ConnectCamera` is only cleared there while
`connectAll()` is still on the stack.

### Confirmed: the fuzzer had been standing on it, and the signal was discarded

Plan 166 root-caused issue #267 by instrumenting `Control::disconnect()` at its
timeout. On 4 of 4 runs of the 320x240 fuzz seeds it reported:

```
targets=0 zombies=1..2 connectInProgress=1 state=disconnecting abort=1 connectCamera=none
```

That is the bench signature of #271, field for field. The state was reached on
every seed of that board.

Two things kept it from being a finding.

First, `sim/main.cpp` discarded the boolean `Control::disconnect()` returns, so
the force-completion printed a line nobody failed on. PR #270 fixed that: it
now calls `Sim::requestFailureExit()`.

Second, and this is why the answer is "the same state, never the same bug": the
fuzz teardown reached it through `disconnect(..., forRestart=true)` at process
exit. That branch builds the identical object state, moving every target with
`m_Stopped` false into `m_ZombieTargets` under the same deadline, but nothing
runs after it. The wedge is not the drain; the wedge is what a live device does
next, when `teardownDraining()` holds the connect gate shut and the next
`connectAll(bool)` clears the in-flight attempt's abort. The interactive branch
is the one that returns to a running device, and the fuzzer never took it.

So: confirmed that the state was reached, on every seed, before #270; refuted
that the bug was ever exercised. Plan 166 already wrote down that fixing the
starvation would cost that accidental coverage and that #271 would have to
build the state deliberately. This plan is that.

## Change

Simulator and test-peer only. No firmware source is touched.
`src/FurbleControl.cpp` and `lib/furble/` are untouched here on purpose: PR
#272 and PR #245 own the fix.

### The peer models the blocking handshake

`FujifilmVirtualCamera::setSecureConnectionStallMs()` is cherry-picked from PR
#245's harness commit 9021924f, so the knob, its members, its abort accounting
and the host test `control_secure_stall_test.cpp` are literally the same objects
in both branches. Three members that commit carries for #245's own stale-bond
work and that nothing here uses (`m_SecureConnectionDropsLink`,
`m_SecureTimeoutsRemaining`, `m_RefuseWhileBonded`) are dropped in the pick,
because they belong to the firmware commit underneath it.

The wait is a condition variable, not a sleep, and that matters: the block is
only half the behaviour. NimBLE returns from a parked `secureConnection()` when
the link is terminated under it, which is exactly what
`Camera::abortBlockingConnect()` issues and what makes PR #245 the fix. A
sleeping peer models the wedge but not the escape, so a reproduction built on
one would fail on master and on the fix alike and prove nothing. The peer's own
`disconnect()` releases the wait.

What this branch adds is the clock. `lib/testing/peer/PeerStall.h` is a new
header-only hook: with nothing installed the peer parks on its condition
variable on the host clock, which is what the host harness wants;
`Sim::bleSetSecureStallMs()` installs a `vTaskDelay` and the peer spends its
deadline in 50 ms slices on that clock instead, re-reading the terminate flag
between them. The semantics are identical, expire on the deadline or wake early
on the terminate and report the abort, and only the clock differs.

This is the seam PR #245 records as the reason it has no simulator scenario: its
peer wait is wall-clock and the simulator runs a virtual clock, so the two could
not meet. They meet here.

`seed secure_stall_ms N` drives it, applied to every Fujifilm peer after
`bleStartPeers()`, and `action ble-secure-stall N` is its runtime form, a new
typed action kind with its own range check and its own invalid fixture. Clearing
the stall models the stale bond being refreshed, which is what lets a scenario
end with a connect that actually establishes a session after a run of cancels.
The default 0 keeps the instant handshake every existing scenario is timed
against, so no certified timing budget moves.

Values used: 3500 ms is the bench signature of a healthy X100VI Secure connect
(3 to 5 s with the link up throughout); 32000 ms is the BLE link supervision
timeout bound, the stale-bond case.

### The reproduction

`sim/scenarios/bughunt/cancel-secure-window-wedge.txt`. Three cycles at the
bench offsets 2000, 4000 and 6000 ms, infinite reconnect on, `secure_stall_ms
32000`, then the liveness the hardware violated: the drain empties inside
`DISCONNECT_DRAIN_RECLAIM_MS`, no connect is left in flight, Control is at idle,
and a fresh connect reaches active.

It fails on master 8bdc52e4 at the first assertion after the first cancel:

```
assert ok: control.state = connecting
ASSERT_MAX FAILED: clock.ms expected <= 8000 got 32330
```

The run is 2320 virtual ms old when the cancel is issued and the teardown
returns at 32330: the interactive cap expiring, silently, exactly as the bench
did. Letting the run continue past that point (an earlier revision without the
`assert_max` line) shows the rest of the chain in the log, in order:

```
[I] furble: Securing
[D] furble: state connecting -> disconnecting
[D] furble: state disconnecting -> idle          <- the cap broke out and drained
state control.zombies = 1
state control.connect_in_progress = yes          <- a live connect in the drain
[W] furble: Fujifilm Secure registration aborted after link loss
[I] furble: Reconnect retry 1, waiting 2500 ms.  <- the abort was cleared
[I] furble: Reconnect retry 2, waiting 5000 ms.
```

The user's second connect cleared `m_ConnectAbort` for the attempt that was
still in flight, so when that attempt finally unwound it took the
infinite-reconnect retry branch instead of reporting the abort, and churned
against a camera that still held the abandoned session. The user's own connect
never reached active. That is PR #272's step 3, reproduced.

The scenario ships **certified false** with the reason recorded in the manifest.
Promote it, and widen it to all three panels, in the PR that lands the fix.

### The sweep

Seven certified scenarios, `sim/scenarios/bughunt/cancel-sweep-*.txt`, all three
panels. Each one cancels at a list of fixed virtual-time offsets across the
connect window, repeats the pass, and after every single cancel asserts the same
settle invariant, then ends by proving the connect gate reopened:

```
assert-eventually-virtual 6000 control.zombies 0
assert control.connect_in_progress no
assert control.state idle
```

| Scenario | Topology | Connect entry | Cancels | Window |
| --- | --- | --- | --- | --- |
| `cancel-sweep-fuji-secure-ui` | fuji-secure | UI Connect | 8 | 3500 ms, no token polled |
| `cancel-sweep-fuji-ui` | fuji | UI Connect | 14 | sub-ms |
| `cancel-sweep-fuji-pair-ui` | fuji-pair | UI multi-connect | 14 | sub-ms, two targets |
| `cancel-sweep-fuji-ricoh-flappy-ui` | fuji-ricoh-flappy | UI Connect | 14 | sub-ms, one peer retrying |
| `cancel-sweep-fauxny-ui` | none (FauxNY) | UI Connect | 14 | 2500 ms, token polled |
| `cancel-sweep-reconnect-entry` | fuji-secure | automatic reconnect | 4 | 3500 ms, no token polled |
| `cancel-sweep-autoconnect-entry` | none (FauxNY) | boot autoconnect | 2 | 2500 ms, token polled |

Four more scenarios come from the same bench session and are described under
"Two more bench failures" below: `reconnect-after-disconnect-sweep`,
`reconnect-registration-delay` and `power-off-state-matrix`, all certified, plus
the non-certified `power-off-during-connect-hang`.

Two of the legs have a connect window a cancel can land inside without polling
(`fuji-secure`), one has a window that does poll (`FauxNY`, the control case),
and the rest are the boundary shape the certified suite already had, kept so a
regression cannot hide by only breaking the expensive one.

The generated form was chosen over a new sweep verb. The strict typed action
model in `sim/scenario_action.cpp` parses every action once, before any thread
starts, and a sweep verb would have to grow a loop construct into a DSL that
deliberately has none. One manifest entry per scenario also lets each leg carry
its own board matrix and its own reason, and lets the reproduction ship
non-certified next to certified siblings.

### Two more bench failures from the same session

The 2026-09-04 device walk on master (plus PR #273, UI driven, X100VI) produced
two more symptoms, and they are in this plan because they are the same class:
a UI action that inherits an unbounded teardown.

**(b) Off hung, and the next boot reported brownout.** Reproduced.
`UI::doPowerOff()` runs on the UI task and calls `doDisconnect()` before
`Platform::powerOff()`. `doDisconnect()` calls `Control::disconnect()`, whose
interactive path waits on `targetTasksStopped()`, and `m_ConnectInProgress` is
part of that predicate, so the wait cannot finish while a connect is in flight.
An attempt parked in `secureConnection()` cannot be cancelled, so the wait runs
to `DISCONNECT_WAIT_MAX_MS` and Off takes thirty seconds with a frozen UI and no
screen. Holding the power button through that is what makes the PMIC cut the
rail, which is the brownout the next boot reported. There is no bound on the
power-off path of its own: it inherits that one, and nothing else.

`bughunt/power-off-during-connect-hang.txt` is the reproduction and ships
certified false. On master:

```
ASSERT_MAX FAILED: clock.ms expected <= 6000 got 32330
```

`bughunt/power-off-state-matrix.txt` is the certified companion: power off from
idle, from an active session, and from an idle state with a drain still pending,
each bounded. Measured on master, power off takes 10 ms from idle and 30 ms from
an active session, so the states where the inherited bound is currently harmless
are pinned before the state where it is not. Each leg reboots first, because
`m_PoweringOff` latches for the life of the process.

No fix is needed here and none is planned. PR #245 closes this as a side effect:
its `abortBlockingConnect()` terminates the link, the peer's stall ends on the
terminate, and `doDisconnect()` returns in milliseconds, so Off completes at
2550 ms against the 6000 ms bound on that branch. A separately bounded
power-off path was considered and dropped: it would be a second teardown policy
for a case #245 already handles. `power-off-state-matrix.txt` stays as the
regression net.

**(a) Connect did not work again after a manual Disconnect, until a restart.**
Not reproduced, and the negative result is worth as much as the scenario.

`bughunt/reconnect-after-disconnect-sweep.txt` walks the one variable the bench
could not control: connect to active, Disconnect, wait, Connect again, with the
gap swept 0, 100, 500, 1000, 2000, 5000, 10000, 20000 and 30000 ms. It passes on
master.

The hypothesis that fits "only a restart fixed it" is a NimBLE client leak: the
pool is `CONFIG_BT_NIMBLE_MAX_CONNECTIONS`, 9 on every furble board, and once
`NimBLEDevice::createClient()` starts returning nullptr every later connect
fails until a reboot. Testing it found two simulator gaps that would have hidden
exactly that, and both are closed here:

- The mock client pool was unlimited. `seed ble_max_clients` caps it at the
  board's number.
- NimBLE freeing a self-deleting client on its disconnect was not modelled at
  all, so every simulator session leaked its client into the mock pool. `seed
  ble_client_selfdelete` models it. Without it the pool grew by one per cycle
  and looked exactly like the leak being hunted, which is why the seed had to
  exist before the hypothesis could be tested at all.

With both modelled the pool is flat: one client while a session is up, zero
after every teardown, across nine connect/Disconnect cycles and across four
connect/cancel cycles. **So a client leak on the clean UI paths is refuted as
the cause of (a).** The sweeps carry `assert_max ble.live_clients 1` so the
result stays true.

The bench capture settled what the peer was missing, and it was not
advertising. After a central-initiated disconnect the X100VI advertises again at
once: a `connect 0` eleven seconds later logged `Scanning` and `Connecting to
78:44:F1:6D:52:CF` in the same second, and `Securing` then `Secured!` inside the
next one. What is slow is the confirmation. `Identifying as furble-38a70` landed
at +13 s, progress reached 85 at +25 s, and the registration notifications only
arrived around +30 to +45 s. A first connect of a session reaches active in
about 14 s; a reconnect on this body takes two to three times that, and
`Fujifilm::REGISTRATION_TIMEOUT_MS` is 25 s, so the first attempt can time out
legitimately and a later retry is what completes. That is the bench report
"Connect did not work again": it did, eventually, and slower than a user waits.

So the model is a camera that answers everything and confirms late, which
`action ble-withhold-registration` already builds; no new peer knob was needed.
`bughunt/reconnect-registration-delay.txt` holds the confirmation back across
the registration timeout so the reconnect loop has to survive a timed-out
attempt, requires the UI to stay honest for the whole 30 s, then releases it and
requires the session back. Certified, 5 of 5 clean runs on 135x240.

The peer knob for advertising silence after a disconnect is therefore not
added: the capture says the silence is zero.

One limit on the pool guard is written down rather than papered over: the mock
frees a self-deleting client of a *link-loss* disconnect only when
`reapDeferredClients()` is pumped from a quiescent point, and the simulator has
no such point. `cancel-sweep-reconnect-entry.txt` therefore carries no pool
guard, because it severs the link with `action drop` and the guard would report
a leak the firmware does not have. Closing it needs a quiescent pump in the
simulator, which is its own argument about where such a point exists.

### Cancel latency cannot be asserted in virtual time yet, and is not

A first revision bounded each windowed leg with one `assert_max clock.ms` over
the whole run, on the reasoning that an outwaited cancel adds a full connect
window and would push the clock past a tight bound. Review measured it and the
reasoning does not hold:

- `cancel-sweep-fauxny-ui` failed its own 24000 bound in 2 of 65 idle runs and
  in 4 to 6 of 8 at loadavg 80, once at 83330.
- `cancel-sweep-fuji-secure-ui` failed 1 of 15 at 31085 against 31000.
- A certified 80x160 bughunt pass failed at 55615.

The cause is issue **#279**. `Control::disconnect()` polls on the UI thread in
20 ms slices, and each slice advances the virtual clock, so the number of
slices a cancel burns is set by how long the *host* takes to let the connect
task run. Under plan 161's deadlock breaker that is host scheduling, leaking
straight into virtual time. A flaky bound is worse than none, so every one of
them is gone and no clock bound ships in the certified set until #279 closes.

Review also measured that the Secure bounds were meaningless as well as flaky:
with `waitForStallLocked()` instrumented, `m_StallLinkDown` is never true in
any Secure leg on master, so every armed stall runs to its 3500 ms deadline and
29555 to 29805 *is* the outwaited baseline. That is not a scenario defect. On
master nothing terminates a parked handshake at all, so no cancel there can
abort one; that is the bug, not the harness.

What replaces the bounds is provenance the breaker cannot inflate. The peer
already records which way its handshake ended, a link terminate or its own
deadline, and `ble.secure_stall_aborted` exposes it. The two reproductions
assert it, and it is exactly the assertion that separates the trees:

| Tree | `ble.secure_stall_aborted` after a cancel |
| --- | --- |
| master 8bdc52e4 | `no`, nothing issues a terminate |
| #272 a376c4e7 | `no`, its re-arm has no token to preserve here |
| #245 996c07c7 | `yes`, `abortBlockingConnect()` terminates the link |

### The plan 161 mutation, honestly

Plan 161 recorded that removing the `target->getCamera()->cancelConnect()` arm
from `Control::disconnect()` failed no certified scenario. **This branch does
not close that hole on master, and the earlier claim that it did was wrong.**
The only signal that separated a cancelled attempt from an outwaited one was
the clock bound, and the clock is not sound until #279 closes.

It is closed on the fix branch. Once #245 lands, both reproductions become
certified, and `ble.secure_stall_aborted` is exactly the assertion the mutation
breaks: with the arm removed there is no token to gate the terminate on, so the
stall runs to its deadline and the provenance reads `no`. Recorded here so the
promotion carries the mutation proof with it rather than dropping it.

The two Fujifilm Secure legs could never have caught that mutation in any case:
their handshake is `secureConnection()`, which ignores the cancel token whether
the arm is there or not. The mutation is about the polling contract, so the
polling topology is where it has to be caught, which is why FauxNY is in the
sweep at all.

## What is not covered, and what closing it needs

**The console connect entry.** `FURBLE_CONSOLE` is not compiled into the
simulator, so `UI::serviceRequests()` and `connectCamera()` do not exist there.
Its distinguishing property is the one the bench used: a connect request can be
queued from the console task while the UI task is blocked inside
`doDisconnect()`, so the next connect lands on a `m_Targets` that
`Control::disconnect()` has not drained yet, which is how the bench got
`already connecting, ignoring duplicate connect`. A scripted `action` cannot
express that, because the driver dispatches it onto the UI task and awaits its
`m_SimActionResult`. Closing it needs either the console request queue compiled
into the simulator, or a driver verb that enqueues a UI request without awaiting
it, which is a deliberate hole in the strict action contract and needs its own
argument.

**The plan 157 sync points.** They are not compiled into the simulator:
`sim/build.sh` does not define `FURBLE_TEST_SYNC`. Cancelling at one of them
means parking the arriving task at the point while the scenario issues the
cancel, and the simulator's UI task is the pseudo-task that drives virtual time,
so a barrier that blocks it stops the clock and a barrier that blocks the
control task runs straight into plan 161 residual gap 3. Closing it needs three
things: `FURBLE_TEST_SYNC` in both simulator build lists,
`TestSyncController` gaining a non-blocking `hasArrived()` so the driver can
poll for the park while it keeps ticking, and three DSL verbs to arm, await and
release. The four points are sub-50 ms windows in the control task, and
`control-interleave` in the host suite already forces the one that matters
(`connectall_returned`), so this stayed out of a PR whose job is the class the
host suite cannot reach: seconds-long vendor waits with a real UI on top.

**The #245 stale-bond rc sequence.** The same bench session captured a distinct
failure: `secureConnection` returning rc=13 after 30 s, then rc=520 after 5 s, a
self-initiated re-pair on the third link-up whose second `security_initiate`
returns rc=2, a terminate with rc=531, a `readValue` rc=271 and `registration
aborted after link loss`, looping. Replaying it needs `setSecureTimeouts()`,
`setRefuseWhileBonded()` and `setSecureConnectionDropsLink()`, which are exactly
the three peer members this branch drops from the cherry-pick because they
belong to PR #245's firmware commit. That reproduction belongs in #245's lane,
on a peer that already carries them, not duplicated here where it would collide
on merge.

**A modelled window for the non-Secure topologies.** `fuji`, `fuji-pair` and
`fuji-ricoh-flappy` complete their handshakes in under a millisecond, so their
sweep legs only cover the boundary. Giving them a window is the calibrated peer
work in plan 159, from captures, not from a guessed number.

**Runtime.** A cancel that lands inside a live handshake costs a few host
seconds, because the target task blocks on `Camera::m_Mutex`, which the
simulator scheduler cannot see, and plan 161's deadlock breaker has to time out
on its two second host bound before the run continues. That is plan 158 Phase 3,
not firmware behaviour, and it is why the two expensive legs walk 8 and 4
cancels rather than the full cross product. Measured on the 135x240 build:
51 s and 50 s; the other five legs are 1 to 5 s each.

## Verification

| Gate | Result |
| --- | --- |
| Host suite (`ctest`) | 94 of 94 |
| Python tooling tests | 144 of 144 |
| `tools/check_sim_scenarios.py` | manifest complete |
| `tools/check_ci_workflows.py`, `tools/check_portability_inventory.py --check` | pass |
| `sim/scripts/check-doc-tokens.sh` | pass |
| Certified e2e | 83 on 135x240, 8 on 80x160, 8 on 320x240 |
| Certified bughunt | pass on all three panels, 18 / 17 / 13 scenarios |
| `run-invalid.sh`, `run-watchdog.sh` | pass |
| `run-fuzz.sh`, 135x240 | 8 runs plus the determinism replay, exit 0, 0 forced completions |
| clang-format 21.1.5 | clean |
| em-dashes (bytes e2 80 94) | 0 |
| sdkconfig | untouched |
| `tools/coverage.py --check` | at or above every floor: host 64.02 (floor 62.55), sim union 54.42 (50.66), grand union 71.03 (69.26) |

### Against the fix branches

The reproduction is a gate for the branch that unblocks the call, not for the
branch that fixes the token, and it says so by passing on exactly one of them.
Both runs merged this branch into the fix branch in a scratch worktree and built
the simulator there.

| Tree | `cancel-secure-window-wedge` | `power-off-during-connect-hang` |
| --- | --- | --- |
| master 8bdc52e4 | FAIL, `clock.ms expected <= 8000 got 32330` | FAIL, `clock.ms expected <= 6000 got 32330` |
| PR #272 head a376c4e7 | FAIL, identical assertion and value | FAIL, identical assertion and value |
| PR #245 head 996c07c7 | PASS, cancels return at 2550, 7025 and 13255 ms | PASS, Off completes at 2550 ms |

Every certified scenario in this PR passes on all three trees; the sweeps and
the power-off matrix were rerun on both fix branches to confirm the fixes do not
regress them.

**#272 does not move this scenario at any offset, and that is correct.** #272
makes the vendor waits that poll honour the plan 148 contract (Canon, DJI,
Nikon) and stops `connectAll(bool)` clearing an in-flight attempt's token. The
wait that strands this scenario polls nothing:
`NimBLEClient::secureConnection()` in `FujifilmSecure::_connect()` takes no
cancel token at all, so there is no token for #272's change to preserve. PR
#272's own body names that as its one residual hole and hands it to the hardware
gate. Every offset that lands inside the handshake therefore still burns the
full `DISCONNECT_WAIT_MAX_MS`; offsets past it were never affected.

#245 fixes it because it terminates the link to unblock the call, and its peer
signals its stall on that terminate, so the attempt unwinds in one slice instead
of waiting out the modelled supervision timeout.

Every certified scenario here passes on master and on #272. One does not pass
on #245, and it is reported rather than adjusted:
`reconnect-registration-delay` reaches `control.state active` on master and on
#272 and does not on #245 (3a9a3c4), where the session stays `connecting` for
more than 100 virtual seconds after the registration confirmation is released,
with `control.reconnect_attempt` still 0, so it is stuck inside one attempt
rather than cycling. Nothing in this branch touches that path. It is a finding
for #245's lane, on the stale-bond recovery it rewrites, and it is exactly the
class of thing this scenario exists to catch.

### Merging with #245

The two branches both touch `lib/testing/peer/FujifilmVirtualCamera.*`,
`sim/scenario_action.*` and `tests/host/`. The verification merge resolved them
as follows, and the same resolution is the intended one:

- peer: the stall itself is the same commit on both sides. Keep #245's three
  extra fault members, and take `waitForStallLocked()` and the `PeerStall.h`
  include from here.
- `sim/scenario_action.cpp` must NOT be union-merged. Both sides add a parse
  block ending in `return accept(); }`, and taking both drops a brace and does
  not compile. Take #245's file and re-apply the two `SECURE_STALL` blocks, the
  enumerator and the parse case, on top.
- #245 flips both reproductions here to certified and widens them to all three
  boards, since both pass on that branch. That promotion is also where the plan
  161 mutation proof lands, above.
- #245's inline `m_StallSignal.wait_for(...)` initialiser of `terminated` must
  be deleted, not kept alongside `waitForStallLocked()`, or the merge fails to
  compile with a redefinition of `terminated`.
- `sim/scenario_action.*`: keep both, they each add one enumerator and one
  parse block (#245 adds `scan-row`, this adds `ble-secure-stall`).
- `tests/host/`: keep #245's, they own that test.

## Implementation state

Implemented as described. The reproduction ships non-certified because it fails
on master by design; the seven sweep scenarios ship certified and pass on
master.
