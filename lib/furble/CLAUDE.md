# lib/furble/ (BLE camera protocol library)

NimBLE-based camera protocol implementations. This is the future portable
protocol core.

- Must stay free of app-layer includes: no FurbleSettings.h, FurbleUI.h, or
  anything else from include/ or src/. Allowed deps: NimBLE, ESP-IDF,
  lib/blowfish, lib/preferences.
- Public API types are furble-owned. `TxPower` (Device.h) and
  `Camera::SecurityMode` are mapped to ESP-IDF and NimBLE values only in
  Device.cpp and Camera.cpp. Do not add an `esp_*_t` or `BLE_HS_*` value to a
  public signature or enum here; the coupling ratchet counts them.
- `Camera` is the abstract base (also NimBLEClientCallbacks). Each vendor mode
  is one subclass pair (.h/.cpp): Fujifilm Basic/Secure, CanonEOS
  Smart/Remote, Nikon Smart/Remote, Sony, the Ricoh Imaging family (RICOH and
  PENTAX K), Panasonic Lumix, DJI Osmo, and FauxNY (software test camera).
- `CanonEOS::writePrefix()` is the identity exchange for both Canon roles and
  always waits for the ATT response. Nothing else acknowledges that the camera
  accepted this client, so a write without response promotes a body that refused
  the identity step as paired (plans/96 A4). Do not relax it to save a round
  trip on the connect path.
- `Ricoh` owns the shared Ricoh Imaging GATT family. PENTAX K bodies reuse its
  persisted `Camera::Type::RICOH`, security flow, UUIDs, and command bytes until
  hardware testing proves a model-specific delta.
- `Camera` owns the ATT MTU helpers because the MTU is a link property and the
  local preference is process-wide. `requestAttMtu(floor)` only raises, so a
  vendor that asks for more keeps its request (DJI Osmo pins 517), and
  `verifyAttMtu(floor)` polls the negotiated value in 10 ms slices up to 200 ms
  because the exchange completes on the host task. `attMtu()` is read again at
  each use and never cached, since a late completion raises it mid-session. A
  wait on the connect path holds `Camera::m_Mutex`, so it must poll
  `connectCancelled()` in the same loop body (plan 148).
- A vendor behaviour that depends on a discovered characteristic, rather than on
  an advertisement, is decided from the GATT profile. Sony's location gate is the
  example: DD30 plus DD31 present means `Strict`, both absent means `BestEffort`,
  and a profile with only one of the two keeps `Strict` because half a gate is
  not an absent one. The advertised generation only breaks the tie
  (`Sony::geoGate()`). A saved record carries no advertisement, so it is
  `Generation::Unknown` and the profile decides; there is no per-body model
  table, because Sony's model codes are not documented and would need
  maintaining per firmware release.
- New camera types get a new `Camera::Type` enum value. Values are persisted
  in NVS: never renumber or reuse existing ones (MOBILE_DEVICE is deprecated
  but its value stays reserved).
- `CameraList` handles persistence of paired cameras, `Scan` handles
  advertisement matching and discovery. `CameraList::savedSnapshot()` is the
  lazy persisted catalog; `snapshot()` is transient scan/connect state. A load
  copies saved shared pointers into the transient list instead of rebuilding
  active objects. `CameraList::save()` marks the exact shared camera `SAVED`
  only after its record is persisted, so vendor reconnect handshakes retain
  their saved-camera behavior without losing Control ownership identity.
- The saved index blob is versioned by an explicit four byte header. A v1 blob
  has no header and no camera ids, so it still decodes and `load()` assigns and
  persists ids once. Camera ids are the companion wire identity: 1 to 254, zero
  means unassigned, `0xff` means all cameras, and the allocator walks forward
  from a persisted counter so a delete does not hand an id straight back.
- `CameraList::m_Mutex` guards the connect list and the id map. Off-UI-task
  callers take `snapshot()` rather than iterating `size()` and `get()`, which
  race a concurrent `load()`.
  advertisement matching and discovery.
- `protocol/ProvisionTLV` mirrors the frozen settings wire ledger. Wire id 46
  is the IMU enable switch and wire id 47 is the write-only companion
  password. A bundle that names the password twice, once as a settings record
  and once through the dedicated field tag, is rejected before anything is
  written. The password is never echoed back on any surface.
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
- `Device::init()` keeps the default resolvable-private address policy. The
  app layer owns the durable companion identity: `CompanionIdentity` loads and
  persists the random-static address and calls
  `Device::applyRandomStaticIdentity()` before companion advertising. Keep the
  controller-address helpers free of NVS so the protocol library stays free of
  app storage policy.
  Both live RSSI accessors use `connect_mutex_t`, preserving scheduler-visible
  waits in simulator builds. Nonblocking status consumers use `getConnParams()`.
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
- Bond store lookups take the identity address, never the advertised one. A
  Secure body advertises a resolvable private address that rotates, so
  `NimBLEDevice::isBonded()` and `deleteBond()` keyed on it miss the bond every
  time: the stale keys survive and the recovery loops. Read
  `m_Client->getConnInfo().getIdAddress()` off the live link, which is the
  address NimBLE resolved through the IRK and the address the bond is filed
  under. That means the snapshot has to happen after `connect()`, not before.
- `NimBLEDevice::deleteBond()` is `ble_gap_unpair()`, which calls
  `ble_gap_terminate_with_conn()` for every connection to that peer before it
  drops the keys. Unpairing therefore ends the live link. Nothing that needs the
  link may run after it: Fujifilm Secure's in-link fresh pair is kept only
  because it costs one immediate failure and produces the "Fresh pair failed"
  verdict that raises the re-pair prompt, never because it can recover here.
- Ricoh fresh pairing treats a bonded-address security failure as a stale local
  bond: delete that bond and return failure so the next bounded control retry
  can perform numeric comparison. Saved reconnect failures preserve the bond;
  do not reconnect inline from the security callback or alter callback/client
  lifetime ownership.
- Camera security callbacks publish a pairing request only after validating its
  code and connection. A headless build with no handler keeps NimBLE's default,
  but a display handler that cannot queue the prompt rejects it. Explicit user
  rejection and expiry are not stale-bond evidence; they terminally suppress
  reconnect for only that camera until an explicit fresh connect re-arms it.
- Ricoh `focusPress()` and `focusRelease()` are intentional no-ops. The
  documented Focus Mode characteristic configures a focus mode, while
  Operation Request starts capture. Do not map focus to a timer or capture
  operation without a verified autofocus command. Source:
  https://github.com/dm-zharov/ricoh-gr-bluetooth-api
- Ricoh's standby wake runs only from the shutter path and only on a fresh
  `OperationMode` read of BLE_STARTUP. It is a link sequence, not a
  characteristic write: `Camera::reconnectInPlace()` drops the link, keeps it
  down for the settle interval, reconnects and re-runs the vendor `_connect()`,
  so every characteristic pointer is re-derived after the rediscovery. Never
  wake the body by writing CameraPower; no source verifies that, and the
  Ricoh Imaging handler treats the write as a power-state request. A
  notification, including an OperationMode or CameraPower transition, is a
  diagnostic and never authorizes a capture: a held or re-established link can
  repeat a stale value. The budgets in `Ricoh.h` are design bounds pending
  bench measurement. See `plans/149-ricoh-sleep-shutter-gate.md`.
- A vendor command path may call `Camera::reconnectInPlace()` when a body needs
  a fresh link before a command is safe. It owns `Camera::m_Mutex` for the whole
  sequence, publishes `m_CancelClient` so a teardown can abort it, restores
  client self-delete on success and returns the client to the pool on failure.
  It deliberately leaves `m_Active`, `m_Paired` and the target ownership alone:
  a wake is not a connect cycle. Do not reimplement it in a vendor class, and do
  not call it while holding another Camera lock. Its connect timeout bounds the
  GAP connect only: the vendor `_connect()` also runs a security handshake that
  waits on the peer, so never describe the call as bounded by that timeout. On
  the failure path it runs the vendor `_disconnect()` while `m_Connected` still
  describes the link, so a vendor teardown may keep gating on `isConnected()`.
  A caller must check `connectCancelled()` before it drops a working link: a
  press that arrives during a teardown must not relink the camera.
- New waits on the connect path go through `Camera::pollDelay()`, never a bare
  `vTaskDelay` or `sleep_for` and never a vendor class's own copy. It slices the
  wait and polls the cancel token in its own body, which is the plan 170 rule
  `tests/test_connect_cancel_contract.py` enforces, and it owns the
  scheduler-clock-versus-wall-clock choice. Do not include `<freertos/*>` from a
  vendor class to get `vTaskDelay`: the host and simulator shims declare
  conflicting queue types and clang-tidy fails the translation unit. Seven
  waits predate the seam and stay as they are, each polling
  `connectCancelled()` in its own body and so already satisfying the gate:
  the ATT MTU poll (`Camera.cpp:1245-1249`),
  `Fujifilm::waitForRegistration()` (`Fujifilm.cpp:155-159`),
  `FujifilmSecure::_connect()` (`FujifilmSecure.cpp:493`),
  `FujifilmBasic::_connect()` (`FujifilmBasic.cpp:141`),
  `CanonEOSSmart::_connect()` (`CanonEOSSmart.cpp:131`),
  `DJIOsmo::finishProtocolConnection()` (`DJIOsmo.cpp:326`) and
  `Lumix::startSession()` (`Lumix.cpp:301`). Convert one only while you are
  already changing that function's body.
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
  The CASIC facts behind it, with the section and page of both the 2020 and the
  2025 protocol revision, are on `docs/components/at6668.md`. `antStatus` 0 is
  "initialising" in V4.2.0.3 and "open" in R6, so do not read it as a fault
  without knowing the revision.
  `Casic::sentenceCommand()` owns the `$PCAS03` bodies. A field that is left
  empty keeps the receiver's rate, so every body writes all fourteen rate
  fields the V6.3.2 section 2.7.4 and V4.2.0.3 section 1.6.4 formats share;
  never go back to a short sentence, and never write one by hand in the
  driver.
- `GpsText` (`Furble::GpsText`) is the vendor text command seam behind
  `GpsQuectel` ($PAIR and $PQTM) and `GpsUnicore` ($CFG): the `$` body `*`
  checksum CR LF frame, the XOR checksum, reply parsing into a name and fields,
  acknowledgement matching, and the one-command-in-flight `Session` with its one
  second timeout and three attempts. The vendor half is a `Dialect` value and a
  command table, not an interface: a vtable and two objects cost DRAM the 4 MB
  images do not have, and the two dialects differ in a name set and an
  acknowledgement shape. That shape is why the seam exists and why it is worth
  reading: a Quectel ack names the packet id it acknowledges, while a Unicore
  `$OK` and `$FAIL` name nothing (UC 3.2 pages 30 and 31), so only one command
  may be in flight there. `match()` returns a verdict, never a bare bool:
  unsupported, parameter error, busy and a bad checksum are different outcomes
  for the user. Every command in `GpsQuectel.h` and `GpsUnicore.h` cites its
  document, section and page, and both planners turn the GPS settings into a
  command queue the way `Ubx::plan()` does. `gps-text` and `gps-text-sim` cover
  them; the documents and the UNVERIFIED list are in plans/388.
- `GpsUbx` (`Furble::Ubx`) is the u-blox codec: framing, Fletcher checksum,
  `scan()`, the resynchronising `Stream`, every message furble uses, `Detect`,
  `CapsProbe`, the tick-wrap-safe `Settle` gate and `plan()`, which turns the
  GPS settings into configuration keys under the receiver's rules. Every
  message and key cites an interface description section and page; two are in
  play, "IFD" for the M10 SPG 5.10 firmware and "F10-IFD" for the F10 SPG 6.00
  one, and an added message or key must say which. The 4.9.20 table of the F10
  document is the only place the four L5 signal keys are written down.
- A band is a receiver capability, never a setting: the F10 firmware cannot run
  one band of a constellation (NEO-F10N integration manual 2.1.3 page 10), so
  `Caps` carries `BAND_*`, `plan()` writes the L5 rows only for a receiver that
  advertised them, and an L1 receiver gets no L5 key written or deleted. One
  unknown key refuses the whole VALSET it travels in, so a wrong guess costs
  the batch, not one key (M10 IFD 3.10.5). `CAPS_UNKNOWN` grants nothing new and
  takes nothing away, which keeps an unnamed but working M10-generation receiver
  configuring as it always did.
- A dual-band receiver has no GLONASS and no CFG-PM key at all (F10-IFD 4.9.20
  pages 158 and 159; the configuration reference has no CFG-PM group), so
  `gnssValid(mask, caps)` refuses GLONASS before it is written and `plan()`
  leaves the power key list empty rather than sending an empty VALSET.
- `GpsDemux` splits the shared UART stream into NMEA, CASIC, UBX and vendor
  text pieces and is the only demultiplexer; the driver dispatches its pieces. A
  vendor piece is one complete line whose name starts a Quectel `$PAIR` or
  `$PQTM` message or a Unicore `$CFG`, `$OK`, `$FAIL` or `$PDTINFO` one, which
  no standard NMEA talker collides with, so the CASIC `$PCAS` text path is
  untouched. A line that never terminates is dropped a byte at a time past
  `GpsText::MAX_LINE`, so it cannot grow the receive buffer. `lineEnd()` returns
  a bool plus an out parameter for a reason: a complete line can end exactly at
  the end of the buffer, and a sentinel return value reads that as "not
  finished".
  Both are covered by `gps-ubx` and fuzzed by `gps_ubx_fuzz`. `plan()` builds
  the signal keys from one table, and the driver keeps only its
  `PlanSummary`; the 4 MB Core debug image has little flash to spare. `plan()`
  also decides what the receiver will refuse: BeiDou B1C is what a BeiDou plus
  GLONASS request gets, because B1I cannot run beside GLONASS L1OF (M10Q data
  sheet table 4 note 13, ROM release notes 4.3.6), and B1C refuses power save
  (RN 7.2) and AssistNow Autonomous (RN 5.1.5). `PlanSummary::ana` reports the
  value actually written, and `power_before_signals` says the
  `OPERATEMODE=FULL` write has to precede the signal write, because a power
  save mode an earlier session left behind would reject the B1C batch (RN 7.2,
  IFD 4.6).
- The `GpsUbx` receiver encoders (`encodeNavPvt`, `encodeNavSat`,
  `encodeMonVer`) refuse a payload above `MAX_PAYLOAD`, so every frame the
  simulator receiver emits passes `scan()`. `frame()` keeps the 65,535-byte
  transmit bound; `MAX_PAYLOAD` is a receive limit.
