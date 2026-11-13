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

## USB monitor recovery (#381, 2026-11-13)

State: host side implemented, hardware retest pending. The project is in
sim-only mode, so nothing below ran against a board.

Changes:

- `FurbleSerialConsole` reads through a `FurbleSerialTransport`. The POSIX
  transport is the old termios code. Tests replay a scripted port.
- A poll that reports hang-up without data is a lost port. Before, a
  re-enumerated node spun until the command timeout and looked like a stall.
- A command that ends without a prompt checks the bytes it received for the
  ESP32 ROM reset line (`rst:0x15 (USB_UART_CHIP_RESET),boot:0x0
  (DOWNLOAD(USB/UART0))`) and reports `romBoot` with the cause and the boot
  mode instead of a bare timeout.
- `CLIUSBRecovery` decides the next step. A lost port reopens. A ROM banner in
  flash boot reopens. ROM download mode, or three stalls in a row, need a
  reset. The monitor sends one only with `--usb-hard-reset`; otherwise it
  writes one `hard-reset-needed` marker and keeps the existing backoff.
- The hard reset is esptool's `--after hard_reset` sequence for the USB
  Serial/JTAG port: RTS with DTR low for 200 ms, then both low, one
  `TIOCMSET` per step so the lines move together. It never selects download
  mode and never touches the PMIC watchdog.
- The first sample and every uptime regression print the console `reset`
  field and the previous run's `crash` summary. A regression also writes a
  `device-reset:<reason>` marker. ROM banners write `rom-reset:<reason>`.
- The prompt resync before each command waits at most the command timeout.
  Production timeouts are 1 s or more, so its 1 s wait is unchanged.

Firmware side: no change was needed. `status` already prints `reset:` from
`esp_reset_reason()`, `CrashLog::init()` prints an unclean previous-boot record
right after `Settings::init()`, and `crash` re-prints it for a console that
opened later. The monitor now reads both, so the next hardware run records why
the app restarted even when the boot log scrolled past with the port closed.

Evidence: `swift test` in `companion/apple` passes 44 Core and 49 CLIKit tests.
`USBRecoveryTests` covers banner parsing, a silent port timing out as a stall,
a ROM banner replacing the timeout, the RTS/DTR pulse order, the escalation
policy with and without the flag, the uptime regression, the option
validation, and a miniature monitor loop that recovers from download mode
through one hard reset.

Deviations:

- The reset is a native RTS pulse, not an `esptool.py chip_id` subprocess.
  The subprocess path enters download mode first and needs a located esptool.
  The pulse is the same `--after hard_reset` step without that detour. The
  advisory message still names the esptool command for a manual recovery.
- The port open and close line handling is unchanged. The issue saw ROM after
  "the port closed", so macOS dropping DTR and RTS on close (`HUPCL`) is a
  candidate trigger. Changing it without a board would be a guess; it is a
  hardware experiment below.
- No simulator scenario. `hw-usb-jtag-rom-stuck-recovery.txt` models ROM entry
  and the power-button recovery but not an RTS release, and adding one would
  encode an unmeasured line model. The Swift fake transport is the host test.

Still needs hardware (StickS3, debug image, companion off, `inactivity=0`,
`display_off=0`):

1. UART0 panic capture. The S3 sdkconfig keeps UART0 as the primary console
   at 115200 baud and USB Serial/JTAG as the secondary, so a panic prints on
   UART0 even when the USB endpoint is gone. Attach a 3.3 V USB-UART adapter
   to the ESP32-S3 default UART0 pins (GPIO43 TX, GPIO44 RX, GND; confirm on
   the StickS3 schematic that they are reachable) and log it with
   `pio device monitor -p <uart-port> -b 115200 | tee uart0.log`. Run the USB
   monitor in parallel:
   `furble-companion-cli monitor --transport usb --port /dev/cu.usbmodem1101
   --interval 5 --duration 3600 --csv > usb.csv 2> usb.err`.
   Leave `--usb-hard-reset` off for this run so a ROM stop stays visible.
2. Pass: 720 samples and no marker rows. Fail: record the first marker, the
   `rom-reset` reason in `usb.err`, and the panic or `rst:` line in
   `uart0.log`. A `USB_UART_CHIP_RESET` with `DOWNLOAD(USB/UART0)` and no
   panic points at host line toggling; a panic or watchdog reset points at
   firmware.
3. Recovery: repeat with `--usb-hard-reset`. Pass when every
   `hard-reset-needed` stop is replaced by `hard-reset`, followed by
   `reconnected` and `device-reset:<reason>`, and the run reaches its
   duration.
4. If `--usb-hard-reset` does not leave the ROM, fall back to the modeled
   recovery: one power-button single click, then record that the RTS pulse
   is not enough on this board.
5. If step 2 shows the line-toggle signature, test clearing `HUPCL` in
   `FurblePOSIXSerialTransport.open()` so a close leaves DTR and RTS alone,
   then repeat step 2.
