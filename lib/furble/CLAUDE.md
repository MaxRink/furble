# lib/furble/ (BLE camera protocol library)

NimBLE-based camera protocol implementations. This is the future portable
protocol core.

- Must stay free of app-layer includes: no FurbleSettings.h, FurbleUI.h, or
  anything else from include/ or src/. Allowed deps: NimBLE, ESP-IDF,
  lib/blowfish, lib/preferences.
- `Camera` is the abstract base (also NimBLEClientCallbacks). Each vendor mode
  is one subclass pair (.h/.cpp): Fujifilm Basic/Secure, CanonEOS
  Smart/Remote, Nikon Smart/Remote, Sony, the Ricoh Imaging family (RICOH and
  PENTAX K), Panasonic Lumix, DJI Osmo, and FauxNY (software test camera).
- `Ricoh` owns the shared Ricoh Imaging GATT family. PENTAX K bodies reuse its
  persisted `Camera::Type::RICOH`, security flow, UUIDs, and command bytes until
  hardware testing proves a model-specific delta.
- New camera types get a new `Camera::Type` enum value. Values are persisted
  in NVS: never renumber or reuse existing ones (MOBILE_DEVICE is deprecated
  but its value stays reserved).
- `CameraList` handles persistence of paired cameras, `Scan` handles
  advertisement matching and discovery.
- The saved index blob is versioned by an explicit four byte header. A v1 blob
  has no header and no camera ids, so it still decodes and `load()` assigns and
  persists ids once. Camera ids are the companion wire identity: 1 to 254, zero
  means unassigned, `0xff` means all cameras, and the allocator walks forward
  from a persisted counter so a delete does not hand an id straight back.
- `CameraList::m_Mutex` guards the connect list and the id map. Off-UI-task
  callers take `snapshot()` rather than iterating `size()` and `get()`, which
  race a concurrent `load()`.
- `Scan` owns one stable NimBLE callback proxy. In the pinned
  esp-nimble-cpp 2.5.0 source (`NimBLEScan.cpp`, `stop()`), cancellation calls
  `ble_gap_disc_cancel()` and does not synthesize `onScanEnd`; Apache NimBLE's
  `ble_gap.h` contract says a successful cancel has fully aborted discovery
  and permits a new one immediately. The host lock serializes cancellation
  with GAP dispatch, so no proxy callback remains in flight after `stop()`.
  Keep the logical generation fence and bounded copied-event queue around that
  contract. Never run `CameraList` or UI work from the NimBLE host callback.
- `Camera` exposes connection RSSI and applies the power cap it was given on
  connect. The runtime adaptive level lives in the app layer Control. NimBLE
  transmit power calls take dBm, so map the supported P3, P6 and P9 enum
  levels through `Device::powerLevelToDbm`.
- Connection profiles: with the experimental conn saver on, `Camera` switches
  the live link between a fast profile (30-50 ms) and an idle profile
  (250-300 ms, latency 0) via `setConnProfile()`. `maybeSetIdle()` and
  `updateConnStats()` are driven from the per-target task tick; the UI only
  reads the cached snapshot through `getConnParams()`. Never request the idle
  profile while a connect is in progress: discovery and subscription round
  trips at the idle interval stretch a two second connect into minutes. The
  `m_ConnectInProgress` gate in `Camera::connect()` enforces this, keep it.
  `setConnProfile()` also mirrors the profile into the NimBLE client so peer
  renegotiation counter-proposals carry the current values, not the
  pre-connect ones.
- Fujifilm Secure's registration identifier write is the only permitted
  exception to the supervision-timeout cap. The Secure handshake immediately
  keeps that exception active through required subscriptions and shutter
  discovery. It then requests the bounded FAST profile and waits up to one
  second for NimBLE's asynchronous controller update. Requesting FAST before
  discovery is complete can make a real X100VI stop responding. Verification
  requires the exact FAST latency and supervision timeout, not merely any
  timeout below the idle cap. A confirmed update clears the peer override so
  the normal inactivity path can later enter IDLE. Every failure path must
  close the exception. Do not broaden it to Basic or steady-state operation.
- Secure registration must check link state after every read, write, and
  subscription boundary. A peer disconnect aborts the handshake immediately;
  never continue discovery or GATT traffic against a disconnected client.
- Fujifilm registration confirmation is a notification on the dedicated
  `CHR_NOT1` characteristic. The captured X100VI payload is `01 00`; accept
  legacy `02 00` only on `CHR_NOT1`. On `GEOTAG_UPDATE` the payload `01 00` is
  a geotag request, and a valid request also confirms registration: a saved
  X100VI reconnect skips the `CHR_NOT1` confirmation and goes straight to
  periodic geotag requests, which the camera only sends to an accepted client
  (hardware trace 2026-08-28). Clear the confirmation
  flag and advance a per-connect callback generation before every Basic or
  Secure attempt, and keep active promotion behind a bounded wait that aborts
  on link loss or Control cancellation.
- A Fujifilm Secure body advertises only the bare model in the BLE local name
  ("X100VI"); the longer label on the camera's own Bluetooth screen is never on
  the air. The displayed name is therefore the advertised model plus the
  advertised five byte serial, composed by
  `FujifilmProtocol::deviceName()`. Both constructors compose it, so a saved
  entry gains the serial on load without any change to the stored record: the
  serial was always in `nvs_t`. Keep the composition idempotent. Do not do this
  for Basic, whose manufacturer data carries a rotating pairing token rather
  than a stable serial. Name derivation must never change matcher acceptance.
- Ricoh fresh pairing treats a bonded-address security failure as a stale local
  bond: delete that bond and return failure so the next bounded control retry
  can perform numeric comparison. Saved reconnect failures preserve the bond;
  do not reconnect inline from the security callback or alter callback/client
  lifetime ownership.
- Ricoh `focusPress()` and `focusRelease()` are intentional no-ops. The
  documented Focus Mode characteristic configures a focus mode, while
  Operation Request starts capture. Do not map focus to a timer or capture
  operation without a verified autofocus command. Source:
  https://github.com/dm-zharov/ricoh-gr-bluetooth-api
- Vendor GATT traffic goes through the protected `Camera::gattWrite`,
  `gattRead`, and `gattSubscribe` wrappers. The console-only journal hooks
  live at that seam, so companion traffic and raw explorer traffic stay out
  of the vendor journal. Keep new vendor operations on these wrappers. The
  journal stores compact binary records, not `BtDebugEvent` objects. Non-S3
  storage is 32 records and at most 8 KiB. ESP32-S3 builds configured with
  `CONFIG_SPIRAM` request 128 records from PSRAM and fall back to 32 internal
  records when PSRAM is unavailable.
  Payloads and text are bounded and the dropped counter is diagnostic data.
- Vendor protocol files are per-camera. Any change here needs the
  hardware-tested-vendors statement in the PR: only Fujifilm is testable on
  real hardware, all other vendors must be declared untested. Every camera or
  protocol PR must also cite its data source as a real URL, not a name mention:
  a capture log, vendor doc, open-source implementation, or datasheet.
- The Fujifilm X100VI Secure golden GATT handshake (STATUS values, identity
  write, registration-accept notifications, shutter sequence) is recorded in
  `plans/95-engineering-lessons.md`. Cite it instead of re-capturing.
- `protocol/` is the portable core: pure wire-format code with no BLE, NVS,
  app-layer, RTOS or UI dependency. `tools/check_portability_inventory.py`
  enforces that by keyword, comments included, and
  `tools/portable_core_manifest.txt` must list every file here. Adding a file
  means adding it to that manifest and keeping the platform names out of its
  prose.
- `GpsCasic` (`Furble::Casic`) lives here even though it is not a camera codec:
  binary checksum and framing, the autobaud ladder, the GSV/GSA satellite
  parser, ephemeris cache framing and freshness, and the MON-HW decode. GPS is
  a furble feature rather than an ESP one, so a Nordic port carries it, and the
  file already satisfies the portable contract unchanged. It is used by
  `src/FurbleGPS`, by no camera, and is covered by the `gps-casic` host test.
