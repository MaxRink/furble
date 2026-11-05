# 302 - make companion stop/start and repeat connections survive

## Problem

Two hardware failures showed up while validating the macOS companion against a
StickS3 on internal-heap-tight master:

1. `companion stop` followed by `companion start` aborted with
   `Mem alloc fail. size 0x00001000 caps 0x00000804`. The decoded backtrace is
   `xTaskCreatePinnedToCore` from `CompanionGatt::enable()`. Disabling deleted
   the companion task, freeing its 4 KiB internal-heap stack; re-enabling could
   not find a contiguous 4 KiB block (`perf heap` showed 6.7 KiB free but a
   3.5 KiB largest block) and `heap_caps_alloc_failed()` panicked.
2. After a link was rejected because macOS presented an address that did not
   match the stored bond, the controller stopped advertising. `onDisconnect()`
   returned early for a non-companion link, so the device stayed silent and the
   next scan found nothing. Only a companion stop/start or reboot recovered it.

## Changes

- Keep the companion task alive for the life of the service. `disable()` no
  longer deletes it; the task sleeps in 100 ms slices while disabled and
  resumes when the setting is enabled again. This removes the restart-time
  4 KiB allocation that could not be satisfied.
- Keep the GATT services and the started server across disable/enable. Removing
  and re-creating them freed little memory but the re-registration path
  (`ble_gatts_start`) still aborted with 0x3e8/0x258 internal allocations.
- Move NimBLE host allocations to PSRAM with
  `CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL`; the StickS3 has 8 MB PSRAM and
  the BLE host does not need internal DMA buffers. This also gives the camera
  task path (#353) the internal headroom it was missing.
- Rename the bond NVS key from `companion_addr_type` (19 characters) to
  `companion_ty`. NVS keys are limited to 15 characters, so the address type
  was never persisted and `loadBond()` reloaded type 0, which broke the
  `isBonded()` check and therefore reconnect advertising.
- If the controller rejects a whitelist update while a link is still tearing
  down, fall back to unfiltered reconnect advertising instead of advertising
  with an empty filter list. `onConnect()` still checks the bonded identity.
- Re-evaluate advertising on every disconnect, including a link the companion
  rejected. The controller stops advertising as soon as any connection is
  established, so a rejected link must also restore the pairing window or the
  bonded reconnect advertisement.
- Log the peer address, identity address, stored bond, pairing-window state,
  accept decision and bond count on connect. This makes macOS address rotation
  visible in the console instead of a silent timeout.
- Add a service-ready flag so the retained task never touches the service
  between `m_Service.deinit()` and the next `m_Service.init()`.
- Match the CCCD handle in `NimBLEService::getCharacteristicByHandle()`. The
  GAP subscribe event reports the CCCD handle (value handle + 1), but the
  server-level lookup accepted only the value handle. Every subscription
  therefore logged `attr_handle=8, not found`, the characteristic never
  recorded the subscription, and the HMAC auth indications failed with
  `BLE_HS_ENOENT` on the connections that happened to hit this path.
- Re-assert the Auth notification subscription in the macOS CLI before writing
  the begin packet instead of trusting a cached `isNotifying`.
- Do not send a Service Changed indication during authentication. CoreBluetooth
  responds by rediscovering services while the auth exchange is in flight,
  which intermittently dropped the auth write or the first status notification.
  A host with a genuinely stale handle map uses `companion reidentify` instead.
- Preserve the link encryption state in `onConnect()`. NimBLE can deliver the
  encryption-change callback before the connect callback for a bonded central;
  the old hardcoded `false` assignments then made the first auth write look
  unencrypted and it was silently ignored. The flags now come from `connInfo`,
  and `onAuthenticationComplete()` remains the normal path when encryption
  finishes after connect.

## Validation

- Host suite and pre-review on the branch.
- On the StickS3: `companion stop` -> `companion start` repeated ten times
  without a panic; `perf heap` recorded before and after each transition.
- Repeated BLE `status` cycles from the macOS CLI now pass 10/10 after the
  `onConnect()` encryption-state fix, with the auth subscription re-asserted
  and a 0.3 s settle window before the begin packet. USB `scan`, `status`,
  `cameras`, a 35-minute monitor, and wrong-password rejection all passed. With
  companion and autoconnect both enabled, the device booted into
  `state: connecting` with 27 KiB internal free and no panic (issue #353).
- Clean stop leaves the persisted setting false and `companion.enabled: false`.

## Host-cache recovery

The observed `Companion auth write len=2` followed by no challenge was not a
lost GATT cache. The device accepted the encrypted bond, but `onConnect()`
re-cleared `m_CompanionEncrypted`/`m_CompanionAuthenticated` after NimBLE had
already delivered `onAuthenticationComplete()`. `CompanionService::handleAuth()`
therefore ignored the begin packet as unencrypted. The flags now inherit
`connInfo.isEncrypted()`/`isBonded()` in `onConnect()`; the normal
encryption-after-connect path still sets them in `onAuthenticationComplete()`.

The remaining `subscribe event; attr_handle=8, not found` line is the standard
GAP Service Changed CCCD, which is not part of the application service vector.
It is unrelated to companion auth.

For a host that genuinely has a stale handle map from an older firmware layout,
`companion reidentify` rotates the LE address and drops the companion bond
without touching camera bonds. Clearing the host pairing is then no longer
required, and the host builds a fresh GATT cache.

## Power-monitor note

The USB monitor now disables stdout buffering, so a redirected CSV shows each
sample immediately. Earlier long runs ended with the console unresponsive after
4-35 minutes; the likely contributors were the enable deadlock fixed in #379,
the manual-flash recovery state, and a 2 s console command timeout that made the
monitor close and reopen the CDC endpoint during a display flush. The monitor
now uses 5 s for `status` and `gps` (#382). After the deadlock fix the StickS3
stayed up for 4.9 hours before a fresh 1 h monitor run, with companion disabled
and inactivity/display off set to Never. With a live BLE companion link the USB
console parser can still time out under load and enter its reconnect backoff.
The BLE monitor is only meaningful while status notifications are actually
arriving; a constant `uptime_s` in consecutive rows means the host is reading a
cached status and the run should be discarded.

## Bond/window locking follow-up (2026-11-05)

Bond state (`m_BondValid`, `m_BondAddress`, `m_BondAddressType`) is owned by
`m_BondMutex`; every read decision takes a snapshot, and the lock order is
`m_Mutex` then `m_BondMutex`. Pairing-window state and connection state are
owned by `m_Mutex`, including the service task's advertising decision and the
disconnect restart helper. The console double preserves a saved bond across
`companion stop` to match production, and models bond/window/advertising
transitions independently.

The shared NimBLE server also serves Sony camera name discovery, so an unknown
inbound link is left alone rather than disconnected; `onDisconnect` restores
advertising when it leaves. This is the deliberate trade-off against breaking a
camera client.

The enable path computes the reconnect decision under `m_Mutex` but calls
`startReconnectAdvertising()` only after releasing it, because that function
reads the public getters and would otherwise deadlock. The disconnect restart
snapshots the window flag and deadline atomically, so an expired window is not
silently extended.

The complete advertising operation is now serialized by
`m_AdvertisingMutex`: pairing start, reconnect start, stop, and the service
task's decision enter through it, and the state checks inside each operation
prevent a stale reconnect from overriding an explicit pairing window. If task
creation fails, `enable()` keeps a direct reconnect fallback because there is no
service task to own it. Only one context mutates the shared NimBLE advertising
object at a time.

The USB monitor documentation surfaces were synchronized in the follow-up to
#382: `docs/companion.md`, `companion/apple/README.md`, the root README and
`docs/wiki/Home.md` all state the 5 s console command timeout.
