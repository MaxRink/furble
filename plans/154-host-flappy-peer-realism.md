# Plan 154: Host test harness peer realism for the multi-target flappy-camera workflow

## Status

Implemented. Tests only, firmware untouched. All new tests pass on current
master and are mutation verified.

Numbering note: 151 is claimed by PR #245, 152 by PR #247, 153 by PR #246,
none merged yet. 154 is the next free number at branch time. If another
in-flight PR claims 154 first, renumber this document before merge.

## Motivation

A hardware incident on 2026-08-28 exposed a gap: no host test composed the
user's real workflow. Two connect targets where one camera is a flappy
standby GR IV (accepts the BLE connect, fails secureConnection with rc=520 on
some attempts, sometimes completes the handshake and then drops the link
about 20 seconds later after a CameraPower 0x00 notify) while the other
camera (X100VI) is healthy, plus a disconnect issued mid connect cycle. On
the build carrying PR #159 that workflow wedged the control state machine in
DISCONNECTING. A gap analysis reproduced the scenario with existing mock
hooks and proved that current master (with #159 reverted) survives it. This
plan lands that repro permanently and adds the peer realism the harness was
missing, so the scenarios become the reland gate for #159.

## Deliverables

### 1. Repro scenarios in the control end-to-end harness

`tests/host/control_e2e/control_e2e.cpp` gains three scenarios, each
registered individually in `tests/host/CMakeLists.txt`:

- `multi-flappy-disconnect`: two real FujifilmBasic cameras through the real
  Control. The healthy peer keeps its link; the flappy peer completes one
  handshake, drops via `mockDropLink(0x08, true)`, then fails every pairing
  write (`failWrite` on the pair characteristic). `setConnectDelayMs(800)`
  parks the reconnect inside a blocking connect so the disconnect lands with
  connect_in_progress true, the exact hardware wedge window. Asserts a
  bounded disconnect (under 3 s), a clean IDLE, no late state republish, and
  a working fresh connect afterwards.
- `flappy-cancel-stress`: 25 iterations sweeping the disconnect landing
  point across the reconnect cycle. Each iteration must land back in IDLE
  with no late republish.
- `flappy-peer-autonomous`: the same churn driven entirely by the peer's own
  FlappyPeer mode (no per-attempt scripting), proving the autonomous model
  composes with the real Control.

### 2. FlappyPeer mode on both virtual cameras

- `FujifilmVirtualCamera::setFlappy(fail_attempts, drop_after_ms)`: accept
  every connect, fail the pairing handshake write for N attempts, complete
  one handshake, then sever the link on the peer's own timer M milliseconds
  later (the Fujifilm protocol has no power notification, so the drop is
  silent). The failure budget re-arms after each drop so a reconnect loop
  churns autonomously. Cleared by `clearFaults()`.
- `RicohVirtualCamera::setFlappy(fail_attempts, drop_after_ms)`: the standby
  flap. secureConnection fails for N attempts the way a supervision timeout
  does (`mockMarkLinkDeadEventPending(520)`, the rc=520 class: the client
  keeps reporting connected with the disconnect event queued), then
  completes; after M milliseconds the peer emits CameraPower 0x00 and drops
  the link. OperationMode BLE_STARTUP semantics are untouched.
- RicohVirtualCamera previously discarded the subscribe callback, so it
  could never emit a notification. It now stores subscriptions and offers
  `emitNotification()` plus a `notifications()` log, mirroring
  FujifilmVirtualCamera.

The drop timer runs on its own thread guarded by a recursive mutex: the
timer may re-enter the peer's disconnect through `mockDropLink`, and any
other teardown path that races the timer blocks until the in-flight drop
finishes, so the timer never touches a freed client. A peer with the mode
enabled must be disabled (`setFlappy(0, 0)`) or destroyed before
`NimBLEDevice::resetMock()`.

### 3. SecureTimeoutPeer promotion and Ricoh-through-Control coverage

- The rc=520 decorator formerly private to
  `tests/host/ricoh_secure_timeout_uaf_test.cpp` is promoted to
  `tests/host/peer/SecureTimeoutPeer.h` as a generic wrapper around any
  `NimBLEMockPeer`; the UAF test now uses the shared class.
- `tests/host/ricoh_control_flap_test.cpp` is the first Ricoh-through-Control
  coverage in the repo: a RicohVirtualCamera in standby-flap mode next to a
  healthy FujifilmBasic, connected through the real Control. The connect
  cycle churns on rc=520 failures, the standby drop emits CameraPower 0x00
  first, a disconnect lands mid cycle, and the test asserts a clean bounded
  IDLE plus a working follow-up connect.

### 4. Absent-peer model

- New mock hook `NimBLEDevice::setScanAbsentAddress(address, absent)`:
  the scan never delivers advertisements from that address, even while an
  advertiser keeps emitting them (`nimbleMockScanDeliveryAllowed` filter in
  `NimBLEScan::emitResult`). This is distinct from `setConnectShouldFail`:
  the saved-reconnect SCAN path is starved and must time out or be
  cancelled; the connect call is never reached.
- `tests/host/absent_peer_scan_test.cpp`: a paired Fujifilm Secure camera is
  registered but absent, a healthy FujifilmBasic sits next to it, the
  connect cycle parks in the SAVED scan, a disconnect lands mid scan. The
  cancel poll inside the scan wait must abort the attempt so the disconnect
  is bounded, the machine lands in a clean IDLE with no late republish, and
  a manual connect to the healthy camera stays responsive.

## Verification

- Full host suite: 82/82 tests green (77 on master plus the five added
  here: control-e2e-multi-flappy-disconnect, control-e2e-flappy-cancel-stress,
  control-e2e-flappy-peer-autonomous, ricoh-control-flap, absent-peer-scan).
  Three back-to-back repetitions of the new tests showed no flakes.
- Mutation A (wedge class): changing the retry-path return in
  `Control::connectAll` to an unconditional `return STATE_CONNECT` (dropping
  the disconnect-state propagation) fails `flappy-cancel-stress` ("no late
  DISCONNECTING republish", iteration lands in state connecting) and
  `multi-flappy-disconnect` (late republish plus the fresh connect never
  reaches active). Mutation restored, tests pass again.
- Mutation B (plan 148 wedge class): removing `!connectCancelled()` from the
  FujifilmSecure saved-scan wait loop makes `absent-peer-scan` fail via its
  watchdog (the mid-scan disconnect can no longer abort the scan).
  Mutation restored, test passes again.
- Firmware is untouched by this PR; a `m5stick-s3-debug` build was run once
  to prove the tree still builds.

## Deviations

None from the gap-analysis reference: the two repro scenarios landed as
designed, with the autonomous scenario, the Ricoh standby-flap test, and the
absent-peer test added on top per the peer-realism scope.
