# 325 - Thread-safe virtual camera subscriptions (#451)

Status: implemented on master (archived 2026-11-25).

## Motivation

`control-e2e-multi-connect-fujifilm` aborted once under ASan with a
heap-use-after-free. It happened while #289 was being verified, on a branch
that did not touch the test doubles. The race was:

1. The control task called `FujifilmVirtualCamera::subscribe()`, which
   inserted the subscription and then called `emitNotification()`.
2. `emitNotification()` took an iterator into `m_Subscriptions` and invoked
   the subscriber's callback through it.
3. Meanwhile, on the test's main thread, `disconnect()` cleared the map and
   freed the node that iterator still pointed at.

The production firmware is not involved. The defect is in the host test
double. It was intermittent: the test passed 183 of 183 on the parent commit
and 20 of 20 when rerun alone.

## Design

- `lib/testing/peer/SubscriptionTable.h` holds a camera's subscriptions
  behind its own recursive mutex. Every access takes the lock: `set`, `find`,
  `clear`, and `takeAndClear`.
- A notification holds the same lock across the lookup and the callback,
  through `deliveryLock()`. The callback receives the subscriber's raw
  `NimBLERemoteCharacteristic`. The mock client owns it and frees it when it
  self-deletes, which happens only after the peer's `disconnect()` has cleared
  the table. So the clear waits for an in-flight callback, and a later
  notification finds nothing. The real NimBLE host gives the same order,
  because it runs notifications and the disconnect event on one task. The
  first version released the lock before the callback; a copy of the entry
  kept the map node safe but not the characteristic (PR #453 review).
- The lock is recursive, so a callback may re-enter the camera on its own
  thread. A callback must not wait on another thread that may clear the
  table. Production notification callbacks only record state and never write
  GATT, so none does. Ricoh already ran its callback under `m_FlappyMutex`
  the same way.
- `takeAndClear` lets `FujifilmVirtualCamera::disconnect()` save the stale
  registration entry and clear the table in one step. The disconnect holds
  `deliveryLock()` across it and the stale copy, which
  `emitStaleRegistration()` reads under the same lock.
- The siblings were checked for the same shape. Fujifilm, Sony, Lumix, DJI,
  Canon Smart, Nikon Smart and Nikon Remote all used an unguarded
  `std::map<std::string, Subscription>` that a notification walked while
  `disconnect()` or `reset()` cleared it. All seven now use the table.
- Ricoh already guarded its map with `m_FlappyMutex` and is unchanged. Canon
  Remote and the secure-timeout peer hold no subscription map.

## Implementation state

Merged as #453 (`139d884a`). The merge commit carries `Fixes #451`, so the
issue closes with it.

| Gate | Result |
| --- | --- |
| Sanitized host ctest (`tools/run_sanitized_host_tests.sh --clean`) | 183 of 183 |
| `control-e2e-multi-connect-fujifilm` under ASan, `--repeat until-fail:50` | 50 of 50 |
| Every `control-e2e` test under ASan, 5 repeats at `-j4` | 18 of 18 tests, every repeat passed |
| Plain simulator build on macOS (`sim/build.sh`, build only, not run) | built; no warning from the doubles or the table |

## Deviations and open items

- The `m_Notifications` journals these doubles append to while notifying
  are still unguarded for readers. Appends now happen under the table lock. Their accessors return a reference, so a lock around
  the append alone would not make a concurrent reader safe. No failure has been
  seen there. Treat any ASan report on them as the same class and give the
  journal the same treatment.
- Other Fujifilm double state is also read and written across threads
  without a lock: the plain flags `m_Configured`, `m_GeotagRequested`, `m_Connected` and
  `m_Client`. They predate this fix and no failure has been seen. Treat an
  ASan or TSan report on them the same way.
- The simulator compiles these doubles as well. The table lock is a plain
  `std::recursive_mutex`. It is now held across a notification callback, and
  a Nikon callback calls `xQueueSend(..., 0)`, which is a simulator scheduler
  boundary. No simulator source calls an emitter directly; every
  notification there is emitted inline by the peer on the task that wrote or
  subscribed. A wait the scheduler cannot see therefore needs a teardown of
  the same peer on another task while that callback is preempted. If a
  simulator run shows that stall, make the table lock a scheduler-visible
  mutex the way `connect_mutex_t` is.
- `NimBLEClient::dropServiceCache()` frees the characteristics without
  clearing a peer's table. Only `gatt_journal_uaf_test` calls it, as a fault
  inside an operation, and no notification follows. It predates this fix.
