# 150 - NimBLE task data release race

## Issue

The RICOH GR IV bench repro from plan 147 has a second, library level crash
that survives the app layer fix. With a saved GR IV session the camera powers
itself off (CameraPower notify 00), the link drops, furble auto reconnects,
and NimBLEClient::secureConnection() fails with rc=520 (0x200 | HCI 0x08
Connection Timeout). A moment later the device panics with LoadProhibited on
Core 0. Reproducible in about 3 minutes on hardware (2026-08-28).

Symbolized backtrace (debug ELF dev+g1443c910):

- xTaskGenericNotify (FreeRTOS tasks.c:5919)
- NimBLEUtils::taskRelease(NimBLETaskData const&, int) at NimBLEUtils.cpp:151
- NimBLEClient::handleGapEvent at NimBLEClient.cpp:1405
  (the BLE_GAP_EVENT_DISCONNECT release)
- ble_gap_conn_broken, ble_gap_rx_disconn_complete, HCI disconnect complete,
  all on the nimble_host task

EXCVADDR is 0x80388ac2, a code address shaped value read as data. That is the
signature of reading a pointer out of freed or reused stack memory.

## Root cause

This is a use after scope inside esp-nimble-cpp 2.5.0, the release pinned in
src/idf_component.yml. NimBLEClient blocking calls put a stack allocated
NimBLETaskData into the client member m_pTaskData and block on it:

- NimBLEClient::connect() sets m_pTaskData at NimBLEClient.cpp:294, waits
  with a finite timeout, and clears it at line 308 after the wait returns.
- NimBLEClient::secureConnection() sets it at line 351, waits forever, and
  clears it at line 359.

The release side runs on the nimble_host task. handleGapEvent() snapshots the
pointer at entry (NimBLEClient.cpp:1015, "save a copy in case client is
deleted"), the disconnect branch re-reads it at line 1060, and the shared
epilogue calls NimBLEUtils::taskRelease(*pTaskData, rc) at line 1405.
taskRelease (NimBLEUtils.cpp:147) writes taskData.m_flags and calls
xTaskNotify on taskData.m_pHandle. Nothing synchronizes any of this. The
component has no lock, no atomic, and no ownership handoff around m_pTaskData.

The observed interleaving:

1. secureConnection() blocks on the nimble_host task releasing it. A GAP
   event (encryption change or the first disconnect notification) releases
   the waiter with rc=520 at line 1405.
2. The camera link teardown queues a second GAP disconnect event. Its
   handleGapEvent() entry snapshot at line 1015 (or the re-read at 1060) still
   sees m_pTaskData, because the released waiter on the other core has not
   reached its clear at line 359 yet.
3. The disconnect branch then runs the furble onDisconnect callback, which
   takes real time. Meanwhile the waiter returns, secureConnection() logs
   "failed rc=520", the caller frame unwinds, and the NimBLETaskData dies.
4. The disconnect branch reaches line 1405 and calls taskRelease on the dead
   stack object. The m_pHandle load pulls reused stack bytes (the code
   address shaped EXCVADDR) and xTaskGenericNotify faults.

The same shape exists on the connect() timeout path: the waiter times out,
clears m_pTaskData at line 308 and returns, but an event handler that
snapshotted the pointer before the clear still releases it at line 1405.

## Upstream state

Checked h2zero/esp-nimble-cpp (read only) on 2026-08-28:

- 2.5.0 (2026-04-01) is the latest release. No release contains a fix.
- master commit 6b69cb16 ("Replace task notifications with semaphores to
  avoid conflicts", 2026-04-24) replaces the task notification transport with
  a pooled semaphore, to stop application task notifications from colliding
  with the block bit. It does not address the lifetime race: taskRelease
  still writes taskData.m_flags and reads taskData.m_pSem through a reference
  to the possibly dead stack object, and NimBLEClient master still has the
  identical unsynchronized m_pTaskData snapshot, re-read, and clear lines.
- No upstream issue matches this crash (searched taskRelease, m_pTaskData,
  LoadProhibited in esp-nimble-cpp and NimBLE-Arduino).

So there is no version to bump to. The fix is carried locally and is a
candidate for an upstream PR.

## Design

Make m_pTaskData an owned handoff instead of a shared pointer. A new private
helper does an atomic exchange under the NimBLE critical section:

    NimBLETaskData* NimBLEClient::extractTaskData() const {
        uint32_t        sr        = ble_npl_hw_enter_critical();
        NimBLETaskData* pTaskData = m_pTaskData;
        m_pTaskData               = nullptr;
        ble_npl_hw_exit_critical(sr);
        return pTaskData;
    }

Rules:

- Only the context that successfully extracts the pointer may release it.
  Every handleGapEvent branch that releases a waiter claims the pointer with
  extractTaskData() at its release point instead of using the stale entry
  snapshot. The disconnect and connect fail branches claim before the client
  can be deleted, so line 1405 never touches the client object again.
- completeConnectEstablished() claims with the same helper instead of the
  non atomic copy and clear pair.
- The GAP events are serviced sequentially on the nimble_host task, so once
  one event claims and releases, the next event's extract returns null and
  cannot double release.
- connect() timeout: the waiter also claims. If its extract succeeds no
  releaser holds the pointer and the frame may die; the cancel and
  BLE_HS_ETIMEOUT path runs as before. If its extract fails, a claimer is
  committed to releasing, so the waiter blocks again (forever, in practice
  microseconds) until that release lands, and only then lets the frame die.
- secureConnection() moves the arm into the retry loop (the claimer nulls
  m_pTaskData on release, so the PINKEY retry has to rearm it). If
  startSecurity fails without entering the wait, the waiter claims back; if
  that claim is lost to a concurrent GAP event (the camera can drop the link
  right before startSecurity fails with BLE_HS_ENOTCONN), the waiter blocks
  until the in-flight release lands, exactly like the connect() timeout path.
  This also consumes the release notification, so no stale TASK_BLOCK_BIT can
  leak into a later taskWait on the same task.
- The arm itself (armTaskData) runs under the same critical section as the
  claim, so arm and extract are serialized and the ownership protocol is
  uniform: arm, then exactly one context extracts, and only that context may
  release.

The waiters can never miss a wakeup: taskWait first polls the notification
state with a zero timeout, so a release that lands before the re-wait is
consumed immediately.

Liveness note for callback authors: a waiter that loses the claim blocks
without bound while the claimant runs the client callbacks (onConnect,
onDisconnect, onConnectFail) before releasing at the end of handleGapEvent.
If a furble callback on the nimble_host task ever blocks on a resource held
by the task inside connect() or secureConnection(), that is a deadlock the
unpatched component did not have. Callbacks must stay non blocking (post a
message and return), which the current Camera callbacks already honor.

NimBLEScan and NimBLEL2CAPChannel have the same pattern in principle, but
their waiters are not driven by the reconnect cycle that triggers this crash
and are left untouched to keep the patch minimal.

## Carry mechanism

managed_components/ is regenerated by the IDF component manager and never
committed, and the manager refuses hand edited managed components (checksum
validation). The patch therefore rides the official override mechanism:

- The pinned 2.5.0 release is vendored at components/esp-nimble-cpp from the
  IDF component registry artifact (not the git tag: the artifact carries no
  .github/ directory and adds registry metadata). docs/, examples/ and the
  registry's CHECKSUMS.json were stripped (872 KB). Provenance is the
  component's own idf_component.yml (version 2.5.0, upstream commit
  e26b5022).
- src/idf_component.yml keeps the 2.5.0 pin and adds
  override_path: "../components/esp-nimble-cpp", so the manager sources the
  dependency from the vendored copy on every clean build, for all envs, with
  no network fetch and no build time patching step.
- The delta against pristine 2.5.0 is confined to src/NimBLEClient.cpp and
  src/NimBLEClient.h and is visible with
  git log -- components/esp-nimble-cpp.

If upstream releases a version containing an equivalent fix, drop
components/esp-nimble-cpp and the override_path line and bump the pin.

## Testing

- A host level regression test is not possible: tests/host/nimble is a furble
  owned mock of the NimBLE API surface and contains no task data or wait and
  release machinery, so the component internal race cannot be modeled there.
  Verification rests on the symbolized backtrace, the code path analysis
  above, and the firmware build.
- Full host suite green (the mock layer proves the furble side still
  compiles and behaves against the unchanged API).
- m5stick-s3-debug firmware build SUCCESS with the vendored override.
- Hardware: bench re-soak pending. The 3 minute GR IV power off repro from
  this plan must survive with the fix before merge.

## State

Implemented on fix/nimble-taskrelease-race. Hardware soak pending.
