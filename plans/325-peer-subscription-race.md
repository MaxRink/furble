# 325 - Thread-safe virtual camera subscriptions (#451)

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
  behind its own leaf mutex. Every access takes the lock: `set`, `find`,
  `clear`, and `takeAndClear`. `find` returns a copy, so a caller invokes the
  subscriber's callback after the lock is released. No callback runs under the
  lock and nothing else is taken under it, so a callback may re-enter the
  camera.
- `takeAndClear` lets `FujifilmVirtualCamera::disconnect()` save the stale
  registration entry and clear the table in one step.
- The siblings were checked for the same shape. Fujifilm, Sony, Lumix, DJI,
  Canon Smart, Nikon Smart and Nikon Remote all used an unguarded
  `std::map<std::string, Subscription>` that a notification walked while
  `disconnect()` or `reset()` cleared it. All seven now use the table.
- Ricoh already guarded its map with `m_FlappyMutex` and is unchanged. Canon
  Remote and the secure-timeout peer hold no subscription map.

## Implementation state

Done on `fix/451-fuji-double-subscriptions`.

| Gate | Result |
| --- | --- |
| Sanitized host ctest (`tools/run_sanitized_host_tests.sh --clean`) | 183 of 183 |
| `control-e2e-multi-connect-fujifilm` under ASan, `--repeat until-fail:50` | 50 of 50 |
| Every `control-e2e` test under ASan, 5 repeats at `-j4` | 18 of 18 tests, every repeat passed |
| Plain simulator build on macOS (`sim/build.sh`, build only, not run) | built; no warning from the doubles or the table |

## Deviations and open items

- The `m_Notifications` journals these doubles append to while notifying
  are still unguarded. Their accessors return a reference, so a lock around
  the append alone would not make a concurrent reader safe. No failure has been
  seen there. Treat any ASan report on them as the same class and give the
  journal the same treatment.
- The simulator compiles these doubles as well. The table is a plain
  `std::mutex` that is never held across a scheduler boundary, so it does not
  add a wait the simulator scheduler cannot see.
