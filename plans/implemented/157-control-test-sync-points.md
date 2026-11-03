# 157 - Deterministic interleaving sync points for the control task

Plan numbers through 156 are taken on master, 156 by the restart-seam PR
#251 that merged as 74247c17, and 158 through 160 landed on master while
this branch was open. This plan therefore takes 157.

## Motivation

The 2026-08-28 test gap analysis found the host harness has no deterministic
interleaving control over the control task. Sub-50 ms races are unforceable:
a test can only widen timing windows with sleeps and hope the scheduler
cooperates.

Concrete demonstration from the analysis: mutating away the
STATE_DISCONNECTING republish guard in `Control::task()` (after
`connectAll()` returns, the task refuses to `setState()` the returned value
when it is STATE_DISCONNECTING because `disconnect()` owns that state) could
not be made to fail any test. Forcing the wedge needs a precise preemption
between `connectAll()` returning and the `setState()` call. That guard is
exactly what prevented the 2026-08-28 hardware wedge class (DISCONNECTING is
terminal for the control task, so a late republish wedges the machine until
reboot), and it was not regression-testable.

## Change

### Test-only named sync points (lib/furble/FurbleTestSync.h)

A minimal test-point mechanism: `FURBLE_TEST_SYNC_POINT("name")` marks a
named preemption window in production code. Firmware builds never define
`FURBLE_TEST_SYNC`, so the macro expands to nothing: zero code, zero
symbols, zero overhead. A host test target that defines `FURBLE_TEST_SYNC`
compiles the call into `Furble::TestSync::point(name)` and links the host
controller.

Three points, the stable registry documented in the header:

- `connectall_returned`: control task, between `connectAll()` returning and
  the republish decision in `Control::task()`. The wedge window.
- `idle_connect_dequeued`: control task, between dequeuing CMD_CONNECT in
  STATE_IDLE and publishing STATE_CONNECT.
- `disconnect_abort_armed`: disconnecting caller, between arming
  `m_ConnectAbort` and publishing STATE_DISCONNECTING.

### Host controller (tests/host/testsync/)

`TestSyncController.{h,cpp}` implements `point()` plus the control surface:
`armBarrier(name, timeout_ms)` parks the next arriving thread,
`awaitArrival(name, timeout_ms)` waits for the park, `release(name)` resumes
it, `onPoint(name, fn)` runs a callback on the arriving thread, and
`anyTimedOut()` latches when a parked thread expired before release. Every
park carries its own timeout, so a forgotten release fails the test instead
of hanging the suite.

### Proof test (tests/host/control_interleave_test.cpp)

`control-interleave` forces the exact wedge interleaving:

1. A saved Fujifilm camera withholds registration (10 s host timeout), so
   the connect attempt parks in the registration wait.
2. A barrier is armed on `connectall_returned` and a user `disconnect()`
   lands mid wait. The cancel unwinds the attempt, `connectAll()` returns
   STATE_DISCONNECTING, and the control task parks holding that stale
   result.
3. `disconnect()` completes to STATE_IDLE while the control task is parked.
4. The barrier is released. The guard must drop the stale republish: the
   state stays IDLE and a follow-up connect reaches ACTIVE.

The two auxiliary points are observed via counting callbacks, so a renamed
or dropped point also fails the suite.

## Mutation proof

Removing the `next != STATE_DISCONNECTING` guard (the mutation the gap
analysis could not catch) and rebuilding only this test target makes
`control-interleave` fail deterministically. Re-run on 2026-09-02 against
the rebased tree:

```
$ ctest -R '^control-interleave$' --output-on-failure
1/1 Test #58: control-interleave ...............***Failed   10.67 sec
  FAIL: guard prevents the late DISCONNECTING republish, state stays idle
  FAIL: state is idle after the release
  FAIL: follow-up connect reaches active
  FAIL: follow-up connect is connected
  FAIL: idle_connect_dequeued fired for the follow-up connect
control-interleave: FAIL (5 checks)
```

Restoring the guard and rebuilding the same target returns the test to
green in 2.15 s. The follow-up checks go red because the machine is wedged
in DISCONNECTING, which is the hardware failure mode the guard prevents.

## Verification

Host suite, macOS, clang, 2026-09-02:

- `ctest` on this branch: 89 of 89 tests pass. Master at 74247c17 has 88,
  so `control-interleave` is the only added test. If PR #259 lands first it
  adds its own console tests on top of that count.
- `control-interleave` alone with `--repeat until-fail:5`: 5 of 5 pass, so
  the interleaving is forced rather than raced.
- Negative control on the premise assertion: inverting
  `check(getState() == STATE_DISCONNECTING)` at the park fails the test, so
  the assertion runs and really observes DISCONNECTING rather than being
  skipped.
- `tools/check_portability_inventory.py --check`, `tools/check_ci_workflows.py`,
  `tools/check_sim_scenarios.py`, `tests/test_ui_restart_path.py` and
  `python3 -m unittest discover -s tests` (80 tests) all pass.
- clang-format 21.1.2 `--dry-run --Werror --style=file` clean on every file
  this branch touches.

### Firmware size: the sync points compile out

Measured by building `m5stick-s3-debug` twice in one worktree, first at
master 1ead7803 and then at this branch, so the build path is constant. The
path matters: the same source built in two worktrees whose directory names
differ in length produced a 1128 byte `.text` delta on its own, so a
cross-worktree size comparison proves nothing.

| Build | firmware.bin | .text | .data | .bss |
| --- | --- | --- | --- | --- |
| master 1ead7803 | 1511776 | 1195318 | 316336 | 2679421 |
| this branch | 1511776 | 1195318 | 316336 | 2679421 |

`src/FurbleControl.cpp.o` reports the same 12552 byte `.text` in both
builds. Every allocated ELF section matches in size and file offset; only
the DWARF `.debug_line` section grows, by 33 bytes, which is the line-table
entry for the four added source lines and is never flashed.

`firmware.bin` differs in 89 of its 1511776 bytes, and every one of them is
accounted for:

- offsets 63 to 74, 25640 to 25661: the git describe version string,
  `v0.0.0-citest-449-g1ead7803` against `v0.0.0-citest-450-g54eab0ac`.
- offsets 176 to 207 and 465 to 472: the ELF SHA256 in the app descriptor.
- offsets 1511743 to 1511775: the appended image SHA256.

`nm -C firmware.elf | grep -i testsync` returns nothing and the three sync
point name strings do not appear in `firmware.bin`. The macro leaves no
code, no symbols, and no strings in a firmware build.

That measurement was taken against master 1ead7803, before #251 merged. It
has not been rerun on 74247c17 because a cheaper proof isolates the same
property exactly, and reruns in seconds rather than 20 minutes.

### Cheaper reproducible isolation proof

Compile `src/FurbleControl.cpp` alone, host toolchain, master version
against this branch's version, from an identical path so no path string can
differ:

```
c++ -std=c++17 -Os -g0 -w     -include tests/host/control_e2e/freertos/FreeRTOS.h     -DFURBLE_HOST_REGISTRATION_TIMEOUT_MS=10000     -I include -I lib/furble -I src     -I tests/host -I tests/host/testsync -I tests/host/nimble     -I tests/host/peer -I tests/host/control_e2e     -I tests/host/control_e2e/doubles     -c FurbleControl.cpp -o out.o
```

With `FURBLE_TEST_SYNC` undefined the object is byte-identical between
master 74247c17 and this branch:

| Optimisation | master 74247c17 | this branch | identical |
| --- | --- | --- | --- |
| `-Os` | deddd09e5b746a1d24f6ad5673ca5393 | deddd09e5b746a1d24f6ad5673ca5393 | yes |
| `-O2` | d6d5052e9af7e8e516c1ec6166f7c1db | d6d5052e9af7e8e516c1ec6166f7c1db | yes |

`-Os` is the level that matters: all five release sdkconfigs set
`CONFIG_COMPILER_OPTIMIZATION_SIZE=y`. Neither object contains any of the
three point names, while the same translation unit compiled with
`-DFURBLE_TEST_SYNC` contains all three.

At `-O0` the objects do differ, by 28 bytes of `__text` on arm64, because
the compiler does not fold the three empty `do {} while (0)` statements. No
furble environment builds `-O0`, so this does not reach any firmware.

## Overlap with in-flight work

The R1 branch `feat/sim-production-control` also edits
`src/FurbleControl.cpp`. Its hunks are the `ble_sim.h` include block at the
top of the file, a `Sim::noteCameraCommand()` observability call in
`Control::Target::task()`, and the `FURBLE_CONSOLE` guards around
`getState()` and `getDebugState()`. This branch adds its three points in
`Control::task()` at lines 298 and 318 and in `Control::disconnect()` at
line 507, none of which R1 touches. The only shared region is the include
block, where both branches append one line. The published
`feat/sim-real-control` branch touches no production source at all; its
overlap with this branch is confined to `tests/host/CMakeLists.txt`.

## Implementation state

Implemented as described. The three points and the controller are the whole
mechanism; no other production code changed beyond the three
`FURBLE_TEST_SYNC_POINT` lines and the header include in
`src/FurbleControl.cpp`.
