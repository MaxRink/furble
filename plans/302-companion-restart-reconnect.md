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
- Send the standard Service Changed indication once per boot after a bonded
  central encrypts. macOS caches the GATT handle map and can keep writing the
  old Auth CCCD handle after a firmware update, which looks like a lost auth
  challenge. The CLI also re-asserts the Auth notification subscription before
  writing the begin packet instead of trusting a cached `isNotifying`.

## Validation

- Host suite and pre-review on the branch.
- On the StickS3: `companion stop` -> `companion start` repeated ten times
  without a panic; `perf heap` recorded before and after each transition.
- Repeated BLE `status` cycles from the macOS CLI were partial: link encryption
  and several sessions authenticated, but the host GATT cache produced
  HMAC-auth timeouts (see the known limit below). USB `scan`, `status`,
  `cameras`, a 35-minute monitor, and the password-authenticated BLE flow all
  passed. With companion and autoconnect both enabled, the device booted into
  `state: connecting` with 27 KiB internal free and no panic (issue #353).
- Clean stop leaves the persisted setting false and `companion.enabled: false`.

## Known host-cache limit

macOS CoreBluetooth can keep a GATT handle map from an older firmware. When it
writes a CCCD to the cached handle, the device logs `subscribe event;
attr_handle=8, not found`, no characteristic records the subscription, and the
auth/status notifications are lost even though link encryption and the HMAC
password path succeed. The device now sends the standard Service Changed
indication after the subscription settles, but a host that subscribed to a
stale handle cannot receive it. Clearing the host pairing is the reliable
recovery. `blueutil --unpair` is experimental on macOS 26 and did not remove the
`nimble` entry here, so the remaining validation step is a manual
System Settings removal or a fresh host.

The address-rotation policy itself is unchanged: a different identity address
outside the pairing window is still rejected, because accepting an arbitrary new
bond would let any nearby central pair without the window.

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
