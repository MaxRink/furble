# Plan 104: seeded BLE lifecycle fuzzing

## Motivation

The connect/disconnect/reconnect lifecycle is where furble's hardest bugs live:
the NimBLE client-pool leak that broke connecting until reboot, the ~30 s
interactive-disconnect freeze, the false-connected guard, and the reclaim
use-after-free. Each was found by hand. Plan 103's real-Control end-to-end
harness (tests/host/control_e2e, PR #131) pinned the known ones as fixed
scenarios. This
plan turns that harness into an offensive tool: a seeded fuzzer that drives the
real `Furble::Control` state machine through randomised sequences of lifecycle
operations with injected BLE faults, asserts a battery of invariants after every
step, and runs under AddressSanitizer + UndefinedBehaviorSanitizer so the
memory-safety classes surface automatically.

This PR is test infrastructure and findings only. No product code changes. The
two FujifilmBasic missing-shutter findings it reproduces were fixed separately in
PR #126 (already merged), per the fault-hunting discipline (tooling + regression
tests in one PR, product fixes in another). Because that fix has landed on
master, the two reproductions now run as normal passing regression guards rather
than inverted WILL_FAIL placeholders.

## Implementation state

Rebased onto current fork/master, which already carries PR #131 (the real-Control
end-to-end harness this plan builds on), PR #128 (the disconnect-freeze / reclaim
UAF fix and its deferred-delete MockNimBLE model) and PR #126 (the FujifilmBasic
missing-shutter guard). The fuzzer's MockNimBLE additions were reconciled with the
model those PRs already provide: the thread-safe pool, the setSelfDelete-honouring
deferred free, the async reap queue and the `setDeferredClientDelete` toggle are
added on top of master's stuck-terminate deferral (`mockRequestDelete` /
`mockCompleteStalledTerminate`) without redefining it. The full `tests/host` suite
is 30/30 green locally, including the five fuzz seeds and both former-WILL_FAIL
reproductions now passing as regression guards.

## What landed

- `tests/host/fuzz/control_fuzz.cpp`: the seeded fuzzer. Compiles the production
  `src/FurbleControl.cpp` and the real `lib/furble` Camera against the shared
  MockNimBLE seam and the Fujifilm virtual camera, on the real-thread FreeRTOS
  shim. Built with `-fsanitize=address,undefined` (guarded by a compiler-support
  check so the job stays portable).
- Additive MockNimBLE fault hooks (test-only, off by default so the existing
  suites are untouched):
  - `setDeferredClientDelete(bool)` plus `reapDeferredClients()` /
    `pendingReapCount()`. This is the deferred client-delete model: a
    self-deleting client is freed after its onDisconnect (inline on a clean
    Camera-driven teardown, queued for an explicit reap on a link-loss drop),
    and `deleteClient()` on a still-connected client defers instead of freeing.
    This makes the live-client count a sound leak probe across reconnect cycles
    and arms ASan to catch a dereference of a client after its self-delete (the
    reclaim use-after-free class). The client pool is now mutex-guarded because
    the fuzzer exercises it from several threads at once.
- Additive FujifilmVirtualCamera fault hooks: `dropLinkOnWrite()` (a
  supervision-timeout link loss mid-handshake) and `clearFaults()` (reset all
  injected faults on the persistent peer between operations).
- CI wiring in `tests/host/CMakeLists.txt`: five seed-pinned fuzz tests plus two
  seed-pinned regression guards for the (now fixed) missing-shutter findings. The
  existing `host_camera` job runs `ctest`, so these are picked up with no workflow
  YAML change.

## Design

Each fuzz iteration reuses one persistent Camera and peer (the physical camera on
the saved list) and runs a random-length (4-9) sequence of self-contained
operation templates drawn from a seeded `std::mt19937`. Each template starts and
ends at STATE_IDLE, so a leak or a stuck-state bug accumulates across the
sequence. The seed comes from argv or `FUZZ_SEED`; the same seed always replays
the same sequence. Nothing uses unseeded `rand`.

The operation vocabulary (over addActive / connectAll interactive+infinite /
disconnect interactive+restart / drop / reconnect / camera-off / camera-back):

- `connect-clean`: connect, assert active + handshake + shutter, disconnect.
- `transient-connect-fail`: 1-3 connect-fail-N, reach a terminal state without
  wedging, recover on a user retry (the interactive path gives up after two
  failures by design).
- `pool-exhaustion`: pool capped at 9 with every connect failing, must land in
  CONNECT_FAILED with no leak, then recover.
- `stale-session-reconnect`: withheld ATT write on the acknowledged CCCD subscribe.
- `write-fail-abort`: rejected handshake write, must abort and reclaim.
- `mid-handshake-drop`: supervision-timeout link loss mid-handshake.
- `drop-auto-reconnect`: silent power-off then supervision timeout; the control
  task must auto-reconnect without leaking the old client.
- `dead-camera-disconnect`: powered-off camera with a stale connected flag; the
  interactive disconnect must return promptly (the ~30 s freeze class).

Invariants asserted after each operation settles:

- no crash / no ASan-or-UBSan error (implicit; the process dies otherwise);
- state consistency: never ACTIVE with an unconnected target, never a held sleep
  lock once idle, no targets once idle;
- no client-pool leak: after reaping the async-freed clients, the live pool never
  exceeds one client for the single fuzzed camera (one retained client per
  connected camera is by design; growth across cycles is the leak);
- no wedge: every operation, and especially the interactive disconnect,
  completes inside a generous time bound;
- sleep-lock balance returns to zero once idle;
- a connect that reports ACTIVE actually ran the handshake (peer.connected() and
  peer.tokenAccepted()) and its shutter reaches the peer.

## Fault variants

missing shutter service, missing shutter characteristic, withheld ATT write
response (stale-session and failWrite), connect-fail-N, client-pool exhaustion
(max 9), supervision timeout, mid-handshake drop, silent power-off drop, and the
deferred client-delete model. The two missing-shutter faults are held out of the
random fuzz pool and driven only by the seed-pinned reproductions, so the fuzz
seeds stay focused on the healthy lifecycle while the reproductions guard the
specific fixed findings.

## Findings (ranked)

### F1 (HIGH, crash/UB, KNOWN) FujifilmBasic missing shutter service null-deref

`FujifilmBasic::_connect()` logs but does not return when
`getService(SVC_SHUTTER_UUID)` returns null, then dereferences it on the next
line (`FujifilmBasic.cpp:159`). A camera that pairs and configures but exposes no
shutter service crashes the connect. The fuzzer reproduces this independently
through the real Control lifecycle: UBSan traps `member call on null pointer of
type 'NimBLERemoteService'` at FujifilmBasic.cpp:159 on the control task.

- Bug class: crash (null dereference, undefined behavior).
- Repro: `control_fuzz --repro missing-shutter-service 1` (deterministic).
- Regression test: `control-fuzz-repro-missing-shutter-service`. Runs the connect
  in a forked child so a regression that reintroduces the crash is caught as the
  child's signal death; the fixed code exits the child cleanly and the guard
  passes.
- Status: FIXED in PR #126 (merged). `FujifilmBasic::_connect` now returns false
  when the shutter service is missing. The fuzzer confirmed the crash independently
  and now guards the fix through the real Control lifecycle. Also covered by the
  `fujifilm-missing-shutter-service` unit test.

### F2 (MEDIUM, silent failure, KNOWN) FujifilmBasic missing shutter characteristic

When the shutter service is present but the characteristic is absent,
`FujifilmBasic::_connect()` logs and falls through to `return true` with a null
`m_Shutter`. The connect reports ACTIVE, but every shutter and focus command is
silently dropped in `sendShutterCommand()`. The fuzzer's shutter-effectiveness
invariant catches it: a connect that reports active whose shutter never reaches
the peer.

- Bug class: silent failure (dead remote, no error surfaced).
- Repro: `control_fuzz --repro missing-shutter-char 1` (deterministic).
- Regression test: `control-fuzz-repro-missing-shutter-char`. Asserts the fixed
  contract: the connect must never report a connected camera whose shutter is
  silently dead. It either fails to reach ACTIVE (the fixed behavior) or, if it
  did, its shutter must reach the peer.
- Status: FIXED in PR #126 (merged). `FujifilmBasic::_connect` now returns false
  when the shutter characteristic is missing, so the machine settles in a terminal
  non-active state instead of a live-but-dead connect. The guard passes.

### No new product bugs

Across five fixed seeds (1, 2, 3, 7, 42) at 10-12 iterations each, repeated for
determinism, every invariant held on the current branch. The reclaim
use-after-free class did not reproduce: the fixed code never calls
`deleteClient()` on a still-connected client, so the deferred-delete model finds
no live-reclaim UAF, and every clean and drop teardown frees its client under
ASan with no post-free dereference. The connect/disconnect/reconnect lifecycle,
the interactive-disconnect bound, the false-connected guard, the pool-exhaustion
path, and the sleep-lock balance are all clean.

### Harness teeth (mutation-verified)

Disabling the client reclaim in `Camera::connect()` (the exact "connecting broken
until reboot" regression) makes the fuzzer report `client pool leak (2 live)` on
the `pool-exhaustion` and `transient-connect-fail` operations within the first
iteration of several seeds. Restoring the reclaim returns to green. The leak
invariant has teeth.

## CI wiring

Added to `tests/host/CMakeLists.txt` inside the existing `BUILD_TESTING` block,
so the `host_camera` job (`ctest` over `tests/host`) runs them with no workflow
change:

- `control-fuzz-seed-{1,2,3,7,42}`: green regression guard + new-bug hunter.
- `control-fuzz-repro-missing-shutter-service`: guards the F1 crash fix (#126).
- `control-fuzz-repro-missing-shutter-char`: guards the F2 silent-failure fix (#126).

The whole `tests/host` suite is 30/30 green locally, including the five fuzz seeds
and the two reproductions now passing as regression guards. Total fuzz time is
~70 s.

## Coverage gaps (honest)

- Single camera, single peer only. MockNimBLE has one global peer, so multi-target
  connect/disconnect (one camera dropping while another stays active, partial
  multi-connect) is not fuzzed. This is the biggest gap.
- Not coverage-guided. This is a seeded randomised-sequence generator over a
  fixed operation and fault vocabulary, not libFuzzer/AFL. It explores orderings
  and parameters, not instruction coverage.
- No data-race detection. The build uses ASan+UBSan, not ThreadSanitizer. The
  mock serialises some paths that hardware would race (a user disconnect arriving
  concurrently with a supervision drop on a live link).
- The reclaim use-after-free is reachable by the deferred-delete model but not
  currently driven, because the fixed product code no longer reclaims a connected
  client. The mutation test covers the leak variant of that class, not a live
  post-free dereference sequence.
- Adaptive power (TX_ADAPTIVE) and the connection saver (CONN_SAVER) are held off,
  so `sampleAdaptivePower()` and the idle/fast profile churn in `setConnProfile()`
  are not fuzzed. Reconnect backoff and infinite-reconnect timing are only lightly
  exercised.
- Fujifilm Basic only. Other vendors and the FauxNY test camera are not fuzzed.
- GPS updates, the intervalometer, and the UI command surface are out of scope
  (this fuzzer is the BLE control state machine only).
