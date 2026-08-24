# 123 - Host control task lifecycle

## Motivation

The real-Control host harness runs firmware tasks as host threads. The control
task is intentionally immortal on-device, but its old host thread was detached.
That allowed C++ to destroy singleton queues and mocks while the control task
was still polling them at process exit. GitHub Actions exposed the race as an
intermittent segmentation fault after the dead-camera scenario had printed all
of its passing assertions.

This is a host-harness lifetime defect, not a firmware control-state failure.
The exact scenario passed 200 consecutive Linux runs and 100 sanitizer runs
before process teardown raced once in CI.

## Design

1. Keep every FreeRTOS-shim task as a joinable `std::thread`.
2. Add one host-only main scope that marks firmware-lifetime tasks for exit and
   joins all of them on every normal and early return path.
3. Reject task creation after shutdown starts, register host queues, wake every
   queue waiter, and interrupt tasks at the existing suspension points.
4. Install that scope in every executable sharing the shim, after each scenario
   has reset Control and joined its finite supervision and observer helpers.

No production source or firmware behavior changes.

## Verification

- Run all host tests, including every real-Control scenario and fuzz seed.
- Repeat `dead-camera-disconnect-no-freeze` at least 200 times.
- Run the focused scenario in a Linux container to match GitHub Actions.
- Require clang-format and `git diff --check`.

## Implementation state

Implemented on `fix/host-control-task-lifecycle`. The shim owns and joins its
task threads before host static destruction; all firmware and simulator code is
unchanged.
