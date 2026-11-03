# 148 - Connect cancellation token for the teardown wedge

## Issue

Hardware trace 2026-08-28 (firmware dev+gf445ed70, X100VI plus a Ricoh saved
pair). After repeated Fujifilm registration-timeout connect failures with user
disconnect and cancel attempts interleaved, Control wedged in
STATE_DISCONNECTING for 89 minutes until reboot:

- control.state disconnecting, control.targets 0, control.zombies 4
- connect_in_progress stuck true, connect_abort true, infinite_reconnect true
- the per-target task (task.X100VI) still existed, blocked at 0 percent CPU
- ble.target0 still held a live NimBLE client for the X100 address
- the cameras console command blocked on the Control mutex, UI locked
- the reboot teardown printed "Fujifilm registration aborted before
  confirmation" then "Camera connect failed", so a connect attempt was still
  inside waitForRegistration when the reboot finally propagated

The camera was in the X100 stale-session state: happy at the BLE level, link
up, registration withheld.

## Root cause

Three defects compose into the wedge. The first is the driver; all three are
present on master before this plan.

1. The Fujifilm registration wait is uncancellable for attempts that begin
   inactive. waitForRegistration() aborts only on m_Connected clearing or, when
   cancelOnInactive is set, on isActive() clearing. cancelOnInactive is
   captured at _connect() entry and is false for any attempt that starts while
   the camera is not active. A stale-session camera keeps the link up, so
   m_Connected never clears and a user disconnect cannot abort the wait: it
   runs the full 25 s while Camera::connect() holds Camera::m_Mutex for the
   whole attempt. The FujifilmSecure saved-camera scan (up to 60 s) sits under
   the same mutex with no cancel path at all.

2. The teardown chain stacks behind that mutex. The target task's
   CMD_DISCONNECT handler calls Camera::disconnect(), which blocks on
   Camera::m_Mutex, so the task cannot stop. Control::disconnect() waits on
   targetTasksStopped(), which needs every task stopped and connect_in_progress
   clear, so the UI task freezes for the remaining wait, up to the 30 s
   DISCONNECT_WAIT_MAX_MS cap. A Secure attempt (scan plus registration) can
   outlive the cap, and then disconnect() hands a still-running target to the
   zombie drain. reapZombieTargets() never frees a zombie whose task has not
   stopped, even past the reclaim deadline, so teardownDraining() gates
   STATE_CONNECT until the blocked task finally stops. Zombies accumulate
   across interleaved cycles, which is the observed zombies 4 with a blocked
   task.X100VI.

3. The control task can republish DISCONNECTING after disconnect() has already
   moved the machine to IDLE. connectAll()'s abort exits return m_State, and
   the control task fed that value straight back into setState(). disconnect()
   runs on the UI task and performs its zombie handoff plus setState(IDLE) the
   moment targetTasksStopped() settles, racing the control task between its
   read of m_State and the setState() store. When the control task loses that
   race it stamps the machine back to STATE_DISCONNECTING, a state with no
   exit on the control task, matching the observed 89 minute terminal
   disconnecting state.

## Design

A per-camera connect cancellation token, owned by Control:

- Camera::cancelConnect() sets an atomic flag, lock-free, callable while
  connect() holds m_Mutex. Camera::clearConnectCancel() clears it and the
  protected Camera::connectCancelled() reads it.
- Control::disconnect() sets the token on every target before queueing
  CMD_DISCONNECT. The in-flight attempt unwinds within one poll, releasing
  Camera::m_Mutex, so the target task's Camera::disconnect() never blocks
  behind the attempt and the zombie drain settles normally.
- Control::connectAll(bool), the user connect entry, clears the token for
  every target before queueing CMD_CONNECT. The token is deliberately not
  cleared inside Camera::connect(): a cancel that lands between the
  connectAll() abort check and the attempt entering connect() must survive
  into that attempt.
- Fujifilm::waitForRegistration() aborts on the token in addition to its
  existing conditions. FujifilmSecure checks it in the registrationAlive
  helper (covering the registration sequence and the FAST profile confirm
  loop) and in the saved-camera scan wait.
- The control task no longer republishes STATE_DISCONNECTING returned by
  connectAll(). That value means disconnect() owns the machine; disconnect()
  performs the DISCONNECTING to IDLE transition itself.

The failed-connect teardown paths are untouched: the plan 147 reclaim
ordering, the m_Connected sentinel semantics, and the PR #229
queued-event-frees-through-callback rule all apply unchanged after the wait
aborts.

Other vendors keep their existing behavior. The token plumbing lives in the
Camera base, so their long waits can adopt the same check later.

## Verification

- New host regression tests/host/control_teardown_wedge_test.cpp (ctest
  control-teardown-wedge). It compiles the production FurbleControl.cpp and
  the real Fujifilm camera against MockNimBLE with a 10 s registration
  timeout, an order of magnitude above the asserted bounds. Scenario: saved
  camera, registration withheld with the link up, terminate stalled (the
  observed live-client state), user disconnect() lands mid registration wait.
  Asserts disconnect() returns bounded, Control reaches IDLE bounded, and a
  follow-up connect reaches ACTIVE, proving the zombie drain opened the
  connect gate again.
- Mutation check: with the fix reverted the test fails on "disconnect returns
  bounded, not after the registration timeout" and the binary runs 10.5 s
  instead of 0.8 s, the wedge signature. With the fix restored it passes.
- Full host suite green: 74 of 74 ctest tests.
- m5stick-s3-debug firmware build passes.

## Hardware boundary

Not yet hardware verified. The user scenario (X100 stale session, repeated
registration timeouts, interleaved disconnect and connect presses) needs to be
reproduced on the M5StickS3 to confirm the device stays responsive and the
drain settles. The historical 89 minute trace ran firmware just before plans
146 and 147 landed, so parts of that wedge were already narrowed; this plan
closes the uncancellable wait that remained.
