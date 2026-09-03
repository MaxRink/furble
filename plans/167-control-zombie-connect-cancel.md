# 167 - uncancellable connect attempts and the draining camera

## Motivation

Issue 271. Two firmware gaps in the reconnect-cancel deadlock family recorded
in `CLAUDE.md`, found while root-causing the simulator teardown timeout in plan
166. The simulator defect there was host starvation and is fixed; these are
separate, and they are reachable on hardware.

The entry point is a vendor wait that does not honour the plan 148 cancel
contract. `Camera::connect()` holds `Camera::m_Mutex` for the whole attempt, so
the target task's `Camera::disconnect()` blocks behind it, and the only way to
release it early is for the wait to poll `connectCancelled()`.
`CanonEOSSmart::_connect()` waited 60 s for the user to confirm pairing without
polling. That is twice `Control::DISCONNECT_WAIT_MAX_MS`, so a user disconnect
during Canon pairing could not abort the attempt and was guaranteed to burn the
whole 30 s cap. `DJIOsmo::_connect()` waited the full 30 s on its protocol
handshake the same way.

What happened next was invisible and then harmful:

1. The interactive teardown broke out of its cap with no log line at all, so a
   30 s freeze looked like a slow disconnect rather than a defect.
2. It drained the target into `m_ZombieTargets` with the attempt still running.
3. `Control::addActive()` deduplicates against `m_Targets` only, so tapping
   connect again handed that same camera a fresh target, and
   `Control::connectAll(bool)` then cleared the very cancel token the teardown
   had set. The attempt became uncancellable for the rest of its vendor
   timeout. `resetConnectionState()` also ran on a camera mid-attempt.
4. The drained target's task stays blocked on `Camera::m_Mutex`, so it never
   publishes `m_Stopped`, the zombie is never reaped, and `teardownDraining()`
   holds the connect gate shut for up to a minute.

The user-visible result on a Canon: disconnect during pairing, tap connect
again, and the device refuses to connect for the best part of a minute with
nothing in the log explaining it.

## What was corrected from the issue text

The issue described gap 1 as a drained target that "can never be cancelled by a
later `disconnect()`". Read literally that is not reachable: the cancel loop
runs before the drain in the same call, and `clearConnectCancel()` walks only
`m_Targets`, so a zombie always carries a set token. The reachable route is
step 3 above, where a later connect cycle *clears* a token that was correctly
set. The fix closes both the route and the underlying assumption.

## Change

### Vendor waits honour the cancel contract

`CanonEOSSmart` and `DJIOsmo` poll `connectCancelled()` inside their waits and
abort. Only the poll is added. The waits themselves, their durations, their
poll intervals and every protocol step around them are untouched; the vendor
behaviour they implement comes from the sources already cited in
[61-camera-compatibility.md](61-camera-compatibility.md), and this change adds
no protocol knowledge. Both vendors are declared untested on hardware: only
Fujifilm is available here.

`Nikon`, `NikonBase` and `NikonSmart` were found by the gate below once it was
widened, and carry the same one-line change. Their protocol sources are the ones
already recorded in [61-camera-compatibility.md](61-camera-compatibility.md):
skyblond.info, hurui200320/nsg, the ML-L7 manual, and furble's own classes as
the working reference. The 60 s scan loop, the 10 s per-stage wait and
`FINAL_OK_WAIT_MS` are pre-existing constants preserved exactly by the slicing,
which is exact division in both cases, so no protocol knowledge is added here
either. `Nikon::_connect()` polls a queue in
one-second slices for up to a minute and its comment already claimed
cancellation stopped it; the condition never checked. `NikonBase` waits 10 s per
handshake stage and `NikonSmart` waits for a final OK, both on a single blocking
`xQueueReceive` that cannot be interrupted, so those receives are sliced at
250 ms with the total timeout and the protocol unchanged. Nikon is untested on
hardware for the same reason as Canon and DJI.

`tests/test_connect_cancel_contract.py` holds the rule, scoped to the function
that waits: the poll must appear in the same braced body as the wait. Every
wider scope was escapable, and review escaped each one in turn:

- It matched the bare word anywhere in the file, so deleting a poll and leaving
  `// TODO: should poll connectCancelled()` kept it green. Comments and string
  literals are now stripped and the poll must appear as a call.
- It only scanned files defining `::_connect(`, so a shared base carrying the
  wait for its subclasses, such as `Fujifilm.cpp`, was never checked. Every
  camera source is scanned now.
- It only recognised `vTaskDelay` and `std::this_thread::sleep_for`. A timed
  `xQueueReceive`, `xSemaphoreTake`, task notification or event group wait
  blocks identically, and widening it is what found the three Nikon waits.

- File scope let one poll immunise every wait in the file, so a second unpolled
  wait was invisible. Worse, this PR's own accessor
  `bool NikonBase::connectCancelled(void) const` satisfied a file-scope search
  for both of the sliced waits in that file, so the gate was disarmed for a file
  this PR fixes. Per-function scope closes that, the second-wait hole and the
  header hole together.
- Only sources were scanned, so an inline helper with a wait in a header was
  invisible. Headers are scanned now.
- `#if 0 ... #else <live code> #endif` dropped the live branch with the dead
  one. The `#else` branch survives now.

Each of those is a self-test in the gate file, so the gate's own teeth are
checked rather than assumed.

A NimBLE call with its own internal timeout still cannot be spotted by name, and
that residual hole is written down rather than claimed closed. It is the shape
behind the Fujifilm Secure stale-bond window, and the hardware gate below is
what covers it.

Code inside `#if 0` is not code, which is why `FujifilmBasic.cpp` is correctly
exempt.

### The cap break-out is logged

`Control::disconnect()` records when the interactive wait expires, naming the
bound and how many targets it is draining with an attempt still in flight.

### The interactive path honours timeout_ms

That path ignored its `timeout_ms` parameter and always waited
`DISCONNECT_WAIT_MAX_MS`, which is the parameter's default, so every existing
caller is unchanged and a caller asking for a shorter cap now gets one. This is
also what lets the regression test drive the cap in milliseconds instead of
waiting 30 s.

### The cancel token is re-armed where no attempt can be in flight

This is the root fix, and it replaces a design that review rejected. Recording
both, because the rejected one is instructive.

`Control::connectAll(bool)` runs on the UI task and cleared the cancel token for
every target camera. A camera can have an attempt still in flight at that
moment: a teardown that reached its cap drained the target while the attempt
held `Camera::m_Mutex`, and `Control::connectAll()` works from a snapshot taken
before the drain. So the clear landed on a running attempt.

The re-arm is now requested by `connectAll(bool)` and performed by
`connectAll()` on the control task, at the top of the cycle.

What makes that safe is **where it is consumed, not who requested it**.
`Camera::connect()` has exactly one caller in the tree, `connectAll()` on the
control task, and the consume sits at the top of that same function on that same
task. So a clear cannot land inside a live attempt whatever set the request.
That argument holds even for a stale request, which matters because a request
can outlive its cycle: a `CMD_CONNECT` dropped in `STATE_DISCONNECTING`, or in
`STATE_ACTIVE` where it hits the invalid-command default, leaves the flag set.
`disconnect()` therefore clears the request too, so it cannot go stale across a
teardown, and restricting the request to user cycles is a refinement on top: it
keeps an automatic reconnect from discarding a cancel that landed mid-reconnect.
It is not what carries the safety argument.

Both halves are covered. Phase 5 of the regression proves the clear happens at
all, which is the load-bearing half nothing else sees: every other phase asserts
the token survives long enough. Deleting the consume block leaves the rest of
the suite green while on hardware the device would never reconnect after its
first disconnect, because `disconnect()` sets the token on every target,
`Camera::connect()` deliberately never clears it, `resetConnectionState()` does
not touch it, and the consume block is the only clearing writer in the tree.
Phase 6 proves an automatic reconnect does not consume a re-arm it never
requested.

Nothing is refused and no target is dropped.

#### The rejected design, and why

The first revision instead had `addActive()` refuse a camera whose drained
target had not stopped, keyed on `Target::m_Stopped`. Review found two defects
and both are real.

The first was user-visible. Both connect entry points call `addActive()` per
camera and then `connectAll()` unconditionally. With the only selected camera
refused, `m_Targets` was empty, `allConnected()` was vacuously true on an empty
vector, and Control published `STATE_ACTIVE` for a session containing nothing.
The UI signalled `CONNECTED`, revealed the main menu and set
`sessionEstablished`, while `sendCommand()` iterated an empty `m_Targets` so
shutter and focus did nothing and reported nothing. Recovery was disconnect and
connect again. The PR had argued the silence was consistent with the existing
duplicate-connect refusal; it is not, because that one drops a redundant request
for a camera the session still contains, while this dropped the only request.

The second was that `m_Stopped` does not mean "attempt in flight". A drained
target can be in five states:

| Drained target | Refusal fired | Re-arm useful |
| --- | --- | --- |
| `m_Stopped` true, link still up | no | ordinary teardown |
| `m_Stopped` true, link down, about to reap | no | ordinary teardown |
| `m_Stopped` false, attempt in flight holding `Camera::m_Mutex` | yes | yes, the intended case |
| `m_Stopped` false, task inside `Camera::disconnect()`, no attempt | yes | no, `cancelConnect()` is a no-op with no wait polling it, and `reapZombieTargets()` never frees a target with `m_Stopped` false, so the refusal persisted for the whole stall |
| `m_Stopped` false, target task not yet scheduled | yes | no, transient |

Two of the five refused with no benefit. `Target::m_Stopped` is also a
`volatile bool` written without `m_Mutex`, so the check could race; the flip is
one-way so only a spurious refusal was possible, never a missed one, which was
memory-safe but widened the first defect.

The current design has neither problem because it refuses nothing.

### An empty session is never active

`allConnected()` returns false with no targets, and a connect cycle with no
cameras logs and returns `STATE_CONNECT_FAILED` instead of going active. This is
defence in depth rather than a consequence of the withdrawn refusal: both entry
points call `connectAll()` unconditionally, so a failed `xTaskCreate` inside
`addActive()` reaches the same vacuous truth.

Only one of the two halves is covered, and that is deliberate. The regression
reaches `STATE_CONNECT_FAILED` through the early return in `connectAll()`,
before `allConnected()` is ever consulted, so reverting the `allConnected()`
guard alone leaves the suite green. The guard protects the `STATE_ACTIVE`
liveness branch, and reaching it with no targets needs `m_Targets` emptied while
the machine is already active. Only `disconnect()` empties it and it publishes
`STATE_DISCONNECTING` first, leaving a window a few instructions wide. Worse,
the window is not observable from outside: on the guarded side the branch
publishes `STATE_CONNECT` over the `STATE_IDLE` that `disconnect()` just set,
and on the unguarded side the state is already `STATE_IDLE`, so neither outcome
presents as a phantom active session to a probe. Covering it would need a new
production sync point in that branch and a barrier test, which is not worth
adding for a guard whose whole purpose is to be unreachable. It is recorded here
and in a comment at the guard instead.

### The teardown no longer depends on an earlier call

`Control::disconnect()` cancels the drain set and the attempt in flight as well
as the live targets, so a teardown is self-sufficient rather than relying on a
token some earlier call happened to set.

### m_ConnectCamera is guarded

It was published unlocked from `connectAll()` and read unlocked in
`getConnectingCamera()`, while `disconnect()` and `getDebugState()` read it
under `m_Mutex`. That is a data race on a `shared_ptr`: a torn read hands out a
control block that is being replaced. Publication now goes through
`setConnectCamera()` and every access takes the mutex, at all eight sites.

The claim that this was only inspectable was wrong, and review disproved it with
a deterministic reproduction. `tests/host/control_connect_camera_race_test.cpp`
runs a connect cycle while a second thread polls `getConnectingCamera()`, the
reader the connect progress timer uses, under `-fsanitize=thread`. Guarded, no
report names the accessor; with the guard reverted, exactly one does, a race on
the `shared_ptr` control block under `_M_add_ref_copy`.

The wrapper asserts that specific claim rather than "zero races", because the
races this PR does not fix have template top frames and any suppression broad
enough to silence them would also hide the one being proved. The pre-existing
count is printed for visibility, not asserted. Those remaining races, including
`m_ConnectAbort` and `m_State`, are known and out of scope here.

`connectAll()` also used to leave `m_ConnectCamera` set after a failed attempt,
so the field no longer named an attempt in flight and handed a stale pointer to
`disconnect()` and to the getter. It is cleared on both outcomes now.

## Verification

Host regression `tests/host/control_zombie_cancel_test.cpp` builds the state
deliberately, in milliseconds. Its camera models the pre-fix Canon shape: a
blind window inside `_connect()` that ignores the cancel token, then a polling
window that honours it. Three phases isolate three fixes, and each has a
mutation:

| Mutation | Failing check |
| --- | --- |
| M1 interactive path ignores `timeout_ms` again | `interactive disconnect honours its 50 ms cap, not the blind wait` |
| M3 `disconnect()` stops cancelling the drain set | `a teardown cancels a drained attempt whose token was cleared` |
| M4 Canon poll removed | contract gate names `lib/furble/CanonEOSSmart.cpp` |
| M5 DJI poll removed | contract gate names `lib/furble/DJIOsmo.cpp` |
| M7 Canon poll replaced by a comment naming `connectCancelled()` | contract gate names `lib/furble/CanonEOSSmart.cpp` |
| M8 `m_ConnectCamera` guard reverted, under TSAN | `a data race names Control::m_ConnectCamera`, 1 of 20 reports |
| M9 re-arm moved back to the UI task | `the in-flight attempt still observes the cancel after a new connect cycle` |
| M10 `allConnected()` vacuously true on an empty session | 3 checks including `control never published active with no targets, whole run` |
| M11 Nikon 60 s loop cancel removed | contract gate names `lib/furble/Nikon.cpp` |
| M12 re-arm consume block deleted entirely | `the camera connects again after a teardown set its cancel token` |
| M13 re-arm consumed unconditionally, reconnect included | `a reconnect attempt observes a cancel the user cycle never re-armed` |
| Ma poll removed from `NikonBase::handshake4Stage()` | gate names `lib/furble/NikonBase.cpp:120` |
| Mb unpolled wait in `NikonBase.h` | gate names `lib/furble/NikonBase.h:110` |
| Mc second unpolled wait in `CanonEOSSmart.cpp` | gate names `lib/furble/CanonEOSSmart.cpp:41` |
| Me NikonSmart final-OK poll removed | gate names `lib/furble/NikonSmart.cpp` |
| M10a only the `allConnected()` empty guard reverted | **survives, deliberately.** See the empty-session section: the branch it guards is not observable from outside |

M2 and M6 from the review are retired with the design they tested: there is no
refusal to remove the drain check from, and no re-arm inside a refusal to
delete.

The regression's bounds are wall clock rather than virtual, and deliberately
around a factor of two from the behaviour they separate, so a loaded host does
not flip them.

No mutation is left in the tree.

## Not covered, and why

There is no simulator scenario for the zombie route, and that is a real gap
rather than an oversight.

Reaching it needs the interactive cap to expire, which needs a camera whose
connect ignores the cancel token. After this change every camera the simulator
can drive honours the contract, which is the point of the change. The two ways
to build the state in the simulator are to compile the plan 157 sync points
into it and park the control task at `disconnect_abort_armed` from a scenario
verb, or to add a virtual peer for a vendor with a long confirmation wait.
Neither is small, the first is simulator scheduling work that belongs with plan
158, and the second needs the capture-backed peer evidence plan 159 requires.
Adding a deliberately non-polling camera purely to make a scenario reachable
would be simulator-only behaviour with no production counterpart, which
`sim/CLAUDE.md` forbids.

Plan 166 already recorded the other half of this: before that change the fuzzer
walked into the drained-with-attempt-in-flight state by accident on every seed,
and a settled teardown no longer reaches it. So the host regression is the only
coverage of this route today, and it is deliberately exhaustive about the three
paths rather than incidental.

## Vendor edit safety

Canon, DJI and Nikon all take the same shape: an early `return false` on a
cancel, inside a wait, on a path that already returns false and lets
`Camera::connect()` run its normal failure teardown. No half-finished pairing is
persisted and no NVS write of an unconfirmed pairing happens.
`CanonEOSSmart::_connect()` returns before its `m_PairResult != PAIR_ACCEPT`
branch, so a cancelled attempt leaves the camera-side bond in place rather than
deleting it, which is the same state a timed-out wait leaves today.

One is more than a poll and is called out: in `DJIOsmo`, the cancel check sits
before `if (requestReceived) break;`, so an iteration where the handshake
completed *and* a cancel is pending now returns false instead of succeeding.
That precedence is deliberate, since the user asked to stop, but it is a
behaviour change rather than a pure addition.

## Hardware gate, owed

Not run. Only Fujifilm is available here; the Canon, DJI and Nikon edits are
declared untested.

The obvious gate is not sufficient. On Fujifilm every wait already polls, so
connect, disconnect during a registration wait and reconnect settles inside the
cap, prints no new line and touches no changed branch. It would pass without
testing anything.

Reaching the change needs a teardown that does not settle, which means a long
block inside `Camera::connect()` that is not a polling wait. The Fujifilm Secure
stale-bond `secureConnection()` window is that block. Debug build,
`FURBLE_CONSOLE`, M5StickS3:

```
settings set fauxNY 0
settings set reconnect 0
cameras list                  # note the X100VI index N
```

`scan start`, `scan stop` and `scan list` exist if the camera is not saved yet;
plain `scan` prints usage.

1. Camera in pairing mode. `connect N`. Expect `debug control` to reach
   `control.state: active`, `control.targets: 1`, `control.connected: 1`.
   `shutter press` then `shutter release` trips the camera.
2. `disconnect`, then immediately `connect N`. Baseline: no
   `Camera teardown did not settle` line, `control.zombies: 0`, active again
   within about two seconds, `control.targets: 1`, shutter works.
3. The case that reaches the change. On the camera only, delete the stick's
   pairing (Connection Setting, Bluetooth, Pairing registration) and leave the
   camera out of pairing mode. On the stick `connect N`. When the log enters the
   security handshake, run `disconnect`. Expect exactly one new line, once. The
   firmware tag is `LOG_TAG = FURBLE_STR`, so on the device it reads:
   `W (...) furble: Camera teardown did not settle within 30000 ms; draining 1 target(s), still running: <name>.`
   Then `debug control` shows `control.state: idle` and `control.zombies: 1`.
4. Immediately `connect N`. **Pass condition: `debug control` shows
   `control.state: active` AND `control.targets: 1` AND `control.connected: 1`,
   and `shutter press` trips the camera, with no second `did not settle` line.**
   Asserting the target count and firing the shutter is the point: the withdrawn
   design would have shown `control.state: active` with `control.targets: 0` and
   a dead shutter, and a gate that only timed the reconnect would have called
   that a pass.

   No timing bound here, deliberately. Nothing in this change can unwind the
   stale-bond block, because it does not poll, so the drain gate stays shut
   until `secureConnection()` returns on its own. Bounding this step by the
   clock would be asserting PR 245's job. The vendor timeout is the only bound
   that holds today.

   This step is also the only place the re-arm is exercised end to end, because
   Fujifilm polls the token in both its registration and Secure paths, so a
   broken re-arm shows up here as an instant connect failure. That is the
   hardware counterpart of the phase 5 regression.
5. Repeat 3 and 4 five times. `control.zombies` must return to 0 each cycle and
   free heap in `status` must not trend down, since a target that is never
   reaped is the leak shape here.
6. Re-run 1 and 2 afterwards to confirm the ordinary path is unchanged.

Caveat: step 3 is the same window PR 245 closes with `abortBlockingConnect()`.
Once that lands, step 3 stops reaching the cap, so the cap log, the drain-set
cancel and the empty-session guard all go unexercised on hardware.

**After PR 245 the gate becomes the re-arm regression**, which is on the hot
path of every connect and is executable today as well: connect, `disconnect`
during the Fujifilm registration wait, reconnect immediately, and require
`control.targets: 1` plus a working shutter within a couple of seconds. Five
cycles, with `control.zombies` back to 0 each time and free heap in `status`
flat. That is the version that would have caught a broken re-arm.

## Coordination

### PR 245 lands first, then this rebases

It carries hardware evidence and the harder radio-call constraint. Five actions
on the rebase:

1. `connectAll()` connect loop. 245 keeps `m_ConnectCamera = camera;` and the
   `else` clear while adding a `needsRepair()` branch; this replaces the shape
   with `setConnectCamera()` on both outcomes. Keep this shape and put 245's
   `repairReason` assignment inside the `if (!connected)` branch.
2. `connectAll(bool)`. 245 adds `m_ConnectFailReason.clear();` inside the lock
   block. That block is restored here and already holds the re-arm request, so
   both live in it and the conflict and the unsynchronised-flag finding resolve
   in the same edit.
3. `disconnect()`. 245 collects `cancelling` from `m_Targets` and calls
   `abortBlockingConnect()` on each after releasing `m_Mutex`. It must be
   extended to the two sets added here. That is **three push sites, not one**:
   the `m_ZombieTargets` loop, the `m_ConnectCamera` case and the existing
   `m_Targets` loop are separate statements in one block, so the clean form is a
   single `cancelling` vector filled by all three and drained once after the
   unlock. Getting this right is the point of landing 245 first: the attempt
   class this plan cannot reach, blocked inside NimBLE and polling nothing, is
   exactly the one `abortBlockingConnect()` wakes.
4. Adjacent-line only: `getConnectFailReason()` and `getTargetCount()` sit where
   `setConnectCamera()` and the locked getter now go.
5. After 245, give the empty-cycle `STATE_CONNECT_FAILED` a reason string. An
   empty cycle is the natural first user of `m_ConnectFailReason`.

### PR 63 is a deadlock hazard against this change

Record before merging either. 63 adds `Control::setConnectCameraLocked()`, whose
contract is "caller already holds `m_Mutex`", and routes every write through it,
including the four raw assignments here. This PR adds `setConnectCamera()`,
whose contract is the opposite: it takes `m_Mutex` itself. The names differ by
one suffix, and after this PR `connectAll()` contains both conventions six lines
apart, a self-locking `setConnectCamera(nullptr)` and a raw
`m_ConnectCamera = nullptr;`. Anyone merging 63 on top who swaps one for the
other self-deadlocks on a non-recursive `std::mutex`, on the control task, while
holding the lock the UI task needs. That is the `CLAUDE.md` reconnect-cancel
brick, one rename away. The four raw sites carry a `// caller holds m_Mutex`
comment so the distinction is visible at the point of edit.

63 also removes the getter's lock from the 20 Hz connect timer path using a
generation counter, which is the natural resolution of the contention this
change introduces there.
