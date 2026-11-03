# 176 - Simulator task notifications

## Motivation

The simulator scheduler modeled queues and delays but not FreeRTOS task
notifications. The production MQTT owner uses notifications for callback and
timer wakeups, so a no-op notification shim could hide scheduling and shutdown
regressions.

## Change

Each simulator task owns a notification counter. `xTaskNotifyGive` increments
the target and wakes a notification waiter through the scheduler.
`ulTaskNotifyTake` blocks on the virtual clock, returns the pending count, and
either clears it or decrements it. Cooperative shutdown releases blocked
notification waiters using the same cancellation path as queue waiters.

## Verification

`sim_scheduler_test` covers blocked wakeup, virtual timeout, clear-on-exit,
decrement-on-exit, delivery at the deadline/resume boundary, and shutdown
unwind with real scheduler tasks. Build and test execution remain in the root
validation lane.
