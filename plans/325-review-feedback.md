# 325 - Review feedback for the virtual camera subscription race

## Scope

This is the review-feedback record for #453
(`fix/451-fuji-double-subscriptions`). Every finding is recorded as fixed,
already fixed or a false positive, with evidence.

## PR #453 (aa5b2089)

Source: Copilot review.

| # | Finding | Resolution | Evidence |
| ---: | :--- | :--- | :--- |
| 1 | `lib/testing/peer/SubscriptionTable.h:37`. `find()` copies the raw `remote` pointer, which does not keep the `NimBLERemoteCharacteristic` alive. A concurrent `disconnect()` can clear the table and self-delete the client, freeing the characteristic before the callback runs with `found->remote`. | Fixed. The race is real in the double. `NimBLEClient::disconnect()`, `mockDropLinkSelfDelete()` and `mockCompleteStalledTerminate()` call the peer's `disconnect()` and then free a self-deleting client, whose `NimBLERemoteService` owns each characteristic through a `unique_ptr`. Test-thread emitters such as `requestGeotag()` and `requestTimeSync()` can run while the control task does that. `Camera::gattSubscribe()` dereferences the pointer under `FURBLE_CONSOLE` and `Fujifilm::notify()` calls `getUUID()` on it. The table lock is now recursive and every emitter holds it across the lookup and the callback through `deliveryLock()`, so the clear in the peer's `disconnect()` waits for the callback and a later emit finds nothing. The Fujifilm disconnect also holds it across the stale-registration copy. | `lib/testing/peer/SubscriptionTable.h`, the seven `emitNotification()` or `emitRawNotification()` bodies, `FujifilmVirtualCamera::emitStaleRegistration()` and `disconnect()`, `lib/testing/nimble/MockNimBLE.cpp` (`eraseClient()` after the peer `disconnect()`), plan 325 Design. |

Validation: `sh tools/run_sanitized_host_tests.sh --clean --jobs 4` passes 183
of 183 under ASan and UBSan. The 18 `control-e2e` tests then pass 5 repeats
each at `-j4`. The plain StickS3 simulator builds with no warning from the
doubles, and `sh sim/scripts/run-e2e.sh` passes all 210 scenarios.
`python3 tools/pre_review.py --base origin/master` passes 9 gates and
`sh sim/scripts/check-doc-tokens.sh` passes.
