# include/

Public headers for the app layer in src/, one header per module
(`Furble<Module>.h`). Everything lives in the `Furble` namespace.

- Add a header here only for src/ modules. lib/furble keeps its headers next
  to its sources.
- These headers may include lib/furble headers; the reverse is forbidden
  (see lib/furble/CLAUDE.md).
- Doxygen-style comments on public members, matching the existing files.
- Any change to a public header must update this guidance when it changes a
  synchronization or ownership contract, and must be reviewed as a public API
  change.
- `FurbleControl.h` atomic state changes must document the acquire/release
  contract in this file and preserve the existing mutex-protected transitions.
- `FurbleControl` reconnect mode, backoff, attempt, and hint fields are
  independent acquire/release atomics because UI, disconnect, control, and
  debug-snapshot accesses cross task boundaries. They are not one coherent
  request or reset transaction. `m_ConnectFailCount` remains control-task-owned.
- `FurbleControl::setPower` is the user-selected maximum for Bluetooth transmit
  power. Adaptive runtime changes must stay at or below that cap.
- `FurbleControl::sendCameraCommand()` returns the complete target-set session
  generation captured with delivery. Expected-session releases must fail when
  the whole session has been replaced.
- `Control::Target::sendCommand()` bounds every command by the per-target
  queue, one of 8, except a shutter or focus release, which takes the oldest
  waiting command's slot rather than being dropped, and only when that command is
  a request. A queued release is never displaced, and the one command a full
  queue still refuses is a release that arrives with another release at the head
  of the queue (plans 374 and 546). The press a release ends has already been
  delivered and no vendor driver times that hold out, so the loss leaves the
  shutter held, and on Lumix, Sony, Canon EOS Smart, Nikon Remote, Ricoh and DJI
  a focus release does not let go of the shutter, so one release kind never
  stands in for the other. Both sends stay non-blocking and the queue never
  grows, and an evicted request is answered by the release behind it with an up
  the camera ignores. Every producer of that queue holds `Control::m_Mutex`, and
  that lock, not the queue, is what keeps the check-and-send atomic; the
  `sendCommand` comment states it. `CMD_DISCONNECT` sets
  `Target::m_DisconnectPending` before it drains and every later command is
  refused, so the eviction can never take the one command the queue must not
  lose and `disconnect()` never waits out its cap on a lost teardown. That flag
  is only ever set, which is safe because both teardown paths drop the `Target`
  from `m_Targets` in the same critical section.
- `Control::m_Targets` is owned by `m_Mutex`, and every reader takes it. The
  1 Hz GPS service is a fourth owner of that vector. It runs on the UI loop,
  never on `GPS::task()`: the LVGL `gps_service_timer` in display builds and
  `vUITask()` in headless ones, both from `app_main`'s task, while `GPS::task()`
  only publishes the fix. `updateGPS()` walks `m_Targets`, so it walks
  and stores under the same lock, and it releases that lock before posting
  `CMD_GPS_UPDATE` on the control queue, so that queue's own lock is never taken
  under `m_Mutex`.
  `Target::updateGPS()` is therefore a pure member copy with no radio
  call, allocation or delay, and it must stay one; a snapshot copied out of
  `m_Targets` to shorten the section would hand that walk ownership of a
  `Target`, because the vector holds the only reference. `allConnected()` keeps a
  second, `m_Mutex`-taking entry point for callers that hold nothing, and
  `allConnectedLocked()` for `connectAll()` and the control task's
  `STATE_ACTIVE` branch, which also fans commands out under that lock. The lock
  order inside Control is unchanged: `m_Mutex` stays outermost, then the
  governor mutex (released), then a hook's own locks. That is Control's order
  only; the companion order, advertising then `m_Mutex` then `m_BondMutex`, is
  documented below. The GPS fan-out takes `m_Mutex` under `UI::m_Mutex` on the
  UI task, the same direction the UI rebuild path already uses; Control never
  calls into the UI, so that edge stays one-way.
- `Control::setAdmitHook()` is registered once at boot, before the control
  task starts, with `MemoryPressure::admit()`; host Control harnesses
  register none. `addActive()` calls it under `Control::m_Mutex`, after the
  duplicate check and before the Target, its queue and its task are
  allocated, with all of their internal bytes. That is what makes admission
  atomic with target creation across the UI, console and companion connect
  paths; never admit a connect from a caller of `addActive()`.
- `MemoryPressure::setEscalateHook()` is registered once by `WebUI::init()`
  and never cleared on device; the hook pointer is an acquire/release atomic.
  `admit()` calls the hook after releasing the governor mutex but possibly
  under `Control::m_Mutex`, so the lock order is `Control::m_Mutex`, then the
  governor mutex (released), then the hook's own locks. A hook must not block
  or call Control, and may only take a lock that is never held while calling
  Control; `WebUI::m_Mutex` qualifies because WebUI calls Control only outside
  it. `tick()` and `admit()` share the governor mutex, never held across a
  delay or another lock.
- `FurbleCompanion.h` bond state (`m_BondValid`, `m_BondAddress`,
  `m_BondAddressType`) is owned by `m_BondMutex`; read a snapshot through
  `hasBond()` or a scoped `m_BondMutex` lock. Pairing-window state
  (`m_PairingWindow`, `m_PairingDeadlineMs`), reconnect cadence state
  (`m_ReconnectCadence`, `m_ReconnectInterval`) and connection state are owned
  by `m_Mutex`; use the public getters outside a lock. A new advertising caller
  that reads or moves the cadence takes `m_Mutex` for it. The lock order is `m_Mutex`
  then `m_BondMutex`.
- `FurbleCompanionIdentity.h` owns the durable random-static LE address used
  by `companion reidentify`. Apply it before companion advertising; the
  controller helpers in `lib/furble/Device.h` only touch NimBLE and never NVS.
  Camera bonds stay in the shared NimBLE store and are not deleted by rotation.
- `FurbleCompanion.h` serializes the complete advertising operation with
  `m_AdvertisingMutex` (recursive): pairing start, reconnect start, stop, and
  the service task's decision/action. `m_PairingGeneration` invalidates queued
  advertising-complete callbacks before a new window is installed. Any new
  advertising caller must take the same mutex; the lock order is advertising,
  then `m_Mutex`, then `m_BondMutex`.
- `FurbleCompanion::disableNow()` is the synchronous stop for callers outside a
  NimBLE callback (console stop, tests). `reloadSetting(false)` deliberately
  keeps the one-second grace timer so a companion settings write can return
  before the GATT services are removed; never use `disableNow()` from a
  characteristic callback.
- `FurbleCompanion::onAuthenticationComplete()` and `onConnect()` may arrive in
  either order for a bonded central. `onConnect()` must inherit the encrypted
  and authenticated state from `connInfo`; resetting those flags drops the
  first auth write. Authentication must not send a Service Changed indication.
  `reidentify()` is the explicit stale-host-cache recovery and may drop only
  the companion bond. Keep camera bonds intact.
- `FurbleCompanionDebug.h` exists only under `FURBLE_BLE_DEBUG`. Its service is
  read-only and unencrypted so it still answers when companion auth or the host
  GATT cache is broken. `attach()` runs during GATT construction and must not
  read `CompanionGatt` state; `update()` runs from the companion service task
  and never under `CompanionGatt::m_Mutex`.
- `FurbleGPS` exposes the CASIC binary test path and AID-INI injection as
  documented public methods. Keep their wire format comments next to the API.
  `GPS::getBackend()` and `getUbxStatus()` are the UBX receiver surface.
  `m_Backend` is written only by the GPS task and by `enable()`/`disable()`
  with the task parked; the UBX identity, SOS and verify results sit under
  `m_UbxMutex`, and the NAV-PVT under `m_GPSMutex` with the parser it
  overlays. Never nest `m_UbxMutex` inside `m_GPSMutex`.
  Its cycle status getters are lock free: they read `m_CycleStatus`, which the
  GPS task publishes after each pass. Do not make a getter take `m_CycleMutex`
  again; the task's passes block on it, and a reader holding it stalls them.
  `m_UartInstallFailed` is set once by `getInstance()` before any task exists and
  makes `getReceiverState()` report `absent` for the session, so a failed UART
  install cannot be hidden by a later `enable()` or `disable()` write.

  `GPS::setRailPower()` is the receiver's hold on `Rail::EXT_5V`
  (`RailHolder::GPS`), not an exclusive switch: its enable and its cut can both
  be refused, and it returns the read-back. `disable()`, `enterRailOff()` and
  `onProbeFailed()` release the hold and must keep `railOffOrRestart()`'s
  promise for the refused case, because the rail can stay up while the receiver
  is stopped. Never call `Platform::setRail()` from the GPS path. The receiver
  never assumes it owns the rail, because the infrared circuit and a Grove
  device draw from the same switch; see plans/364-ext5v-rail.md.
- `FurbleSD.h` and `FurbleGPX.h`: all SD and GPX file I/O runs on the SD
  writer task. Other tasks use only the non-blocking `SD::request()` /
  `SD::logPoint()` API and the atomic state accessors, and never call GPX
  methods directly. `SD::powerOff()` is the one blocking call: it returns
  `false` when the writer did not acknowledge inside its timeout plus one
  extension, or did not take the request at all, and every caller must treat
  that as "the card may still be writing" and keep the rails up.
- `FurbleAutoOff.h` contains the pure disconnected-idle policy predicate.
  Charging blocks auto-off unless the explicit `AUTO_OFF_CHARGING` opt-in is
  enabled; callers must sample charging telemetry and keep policy ticks free of
  NVS writes. It also carries the refusal backoff: `POWER_OFF_RETRY_MS`,
  `retryAfter()` and the wrap-safe `retryBlocked()` that holds the automatic
  shutdown paths off after a refused power off.
- `FurbleSettings.h` assigns the IMU enable switch wire id 46.
- `FurbleSettingsDefaults.h` holds the fresh-device defaults and the
  board-conditional `displayOff()` policy. Its Core Basic comment carries the
  IP5306 light-load hold: 64 s below 45 mA, because M5Unified writes
  `SYS_CTL2` bits 3:2 = `11`, not the 32 s power-on default. Do not restate the
  32 s datasheet default as the shipped behavior.

- `FurbleCompanionService.h` gates the settings and trigger writes on the
  companion password challenge in `FurbleCompanionAuth.h`. A new privileged
  characteristic must call `allowProtected()` before it acts, which is still
  owed by the reserved OTA control and data ids. `handleLocation` is
  deliberately outside that gate and needs only an encrypted link; see
  plans/implemented/116-companion-password.md for why.
  `Settings::loadPassword()` distinguishes unset credentials from NVS failures;
  pass its success flag to `CompanionAuth::setPassword()` so failures stay closed.
  Password mutations use `Settings::savePassword()` to check both NVS set and
  commit, including successful empty-string clears. Never acknowledge the
  generic void settings writer as proof that a credential was persisted.
  Camera pairing prompts use the same queued UI boundary: a missing handler is
  the headless NimBLE default, while a display queue failure must reject the
  unseen comparison. Camera pointers in queued requests are resolved against
  Control-owned shared pointers before use.
  `CompanionService::notifyCameras()` re-publishes every camera record on
  `CAMERA_KEEPALIVE_MS` (30 s) as well as on a change. The transport reports no
  delivery result, so the keepalive is what bounds how long a notification the
  controller refused can stay missing from the phone's list. Keep it inside the
  `CAMERA_NOTIFY_INTERVAL_MS` rate gate, and mirror `notifyStatus()`, which has
  used the same 30 s cadence since the status packet existed. The stamp it
  measures from is the last notification handed to the transport, not the last
  call: the companion loop calls this every second, so stamping an empty pass
  makes the keepalive unreachable.

### Companion wire id reservations

The settings table in `src/FurbleSettings.cpp` is the source of truth for ids
already on master. Every nonzero id must also have a row in
`SETTING_SCHEMAS` in `lib/furble/protocol/ProvisionTLV.cpp`; the host provisioning
test enforces that invariant. Ids that master does not use are handed out here
so open PRs cannot collide, because two branches claiming one id produce an
add/add conflict in `tests/protocol/golden/settings/*-<id>.bin` and a silent
protocol break for the companion app. A PR claims its reserved ids at rebase
time, regenerates its golden corpus, and updates its row. See issue #280.

A merged id is frozen and never moves afterwards, because a shipped id is a
companion client contract: renumbering one and regenerating its fixtures
yields a self-consistent corpus that silently breaks every deployed client.
`tests/protocol/protocol_test.cpp` pins the ids it has been given so that
renumbering fails the build rather than passing quietly.
`tests/host/settings_table_test.cpp` parses this table, expands its ranges,
requires every documented owner row, rejects duplicate owners, and checks the
Master rows exactly against all source-exposed settings ids, including
conditional rows. It cannot inspect GitHub; audit every open head again at
rebase time.

| Owner | Setting keys | Wire ids |
| --- | --- | --- |
| Master | shipped settings | 1-22, 24-35, 37-44, 46-55, 65-69, 72-76 |
| Master (conditional) | display, MQTT, WebUI, S3 watchdog | 23 (`WATCHDOG` on `FURBLE_M5STICKS3`), 36 (`DISPLAY_MODE` without `FURBLE_NO_DISPLAY`), 56-61 (`FURBLE_MQTT`), 62 (`WEB_UI` on `FURBLE_WEBUI`) |
| Historical claims | compatibility audit required | 45 |
| Free | available after rechecking open heads | 63-64, 70-71 |

Wire ids 48 (`GPS_GNSS`), 49 (`GPS_PSM`) and 50 (`GPS_ANA`) are claimed by the
u-blox M10 driver, plan 321.

Take the next free id only after checking every open PR head. Id 45 has a
historical claim in older branches; keep it reserved and do not allocate or
reuse it without a compatibility audit. Id 42 was reserved for a timezone
setting and is now merged as `TIMEZONE`. Recheck every open feature PR head
immediately before a rebase or merge. Documentation-only PRs do not change this
table. No new id is free merely because a branch does not currently touch
settings.

Audit snapshot (2026-11-04): the MaxRink/furble master reviewed was `dd31f47e`
(the PR #350 merge). The shipped settings table and `SETTING_SCHEMAS` were
checked together. Wire IDs 42 (`TIMEZONE`), 43 (`AUTO_OFF_CHARGING`), 46 (`IMU`),
75/76 (`IVL_SLEEP`, `IVL_SLEEP_THR`), and 62 (`WEB_UI`) are merged. PRs #317,
#318 and #319 merged on 2026-11-02 without a setting claim. No pull request
claimed a setting id at the 2026-11-06 recheck. Refresh this snapshot at every
settings-touching rebase.
- `FurbleSettings.h` widened `MULTISELECT_NAME_MAX` from 16 to 32, which changed
  the stored record size. `Settings::load<multiselect_t>()` and the SD settings
  importer both read the old layout through `multiselect_legacy_t` and widen it.
  Changing that constant again means adding another legacy layout, not dropping
  every saved selection.
- `FurbleUI.h` declares `UI::floatingIndicatorReserve()`. Its board list is the
  set of boards whose navigation indicators float over the page instead of
  sitting in a navbar, and it must track the indicator construction in
  `UI::UI()`. It returns zero elsewhere, and callers must not write that zero
  over a theme padding.
- `FurbleIMU.h` is the shared motion API. `IMU::MotionSource` is a singleton
  with one interface and three backends: software, BMI270 any-motion and
  no-motion, MPU6886 wake on motion. Every consumer uses `arm()`, `poll()`,
  `addCallback()` and `state()`, and gets `MOVING` or `STATIONARY` on the same
  contract from all three: a slope threshold plus a 60 s quiet window. Keep that
  surface small. PR65's motion-adaptive GPS consumes this source rather than
  running a second detector, so there is exactly one IMU poller and one
  definition of stationary. `setScale()` is the runtime calibration knob for the
  software backend's 0.20 g threshold, clamped to 0.25 to 4.0; the hardware
  engines threshold in the chip and ignore it. The source is polled from the UI
  housekeeping timer, never from its own, so the simulator power model sees it.
  A motion setting change must never route through `GPS::reloadSetting()` or
  `GPS::enable()`: those reset the receiver. Callbacks are a bounded registry,
  not a single slot, and run on the task that calls `poll()`; add and remove
  from that task and never block in one. Reader-facing state is atomic because
  the diagnostics timer reads it while `poll()` writes. Every engine register
  sequence holds `g_IMUMutex`, declared in this header, which the spirit level,
  the IMU live page and the console probe also take. `FurbleIMUMotionScale.h`
  is the one definition of every motion count and its documented mg, ms and g
  value, asserted against each other at compile time: the backends, the
  simulator double and `tests/host/imu_motion_scale_test.cpp` read it, and no
  file may hand convert a count to mg. The BMI270 scales are documented in
  datasheet revision 1.6 section 5.2.40; the MPU-6886 count size is this
  project's assumption because revision 1.1 gives none, and it is marked as one.
  See plan 332 before changing a count or a scale.
- `FurbleUI.h` exposes IMU diagnostics and spirit-level state only when the
  persisted IMU capability is enabled; simulator seams must model the same
  `M5.Imu` read boundary rather than adding widget-only state.
- `FurblePressEdge.h` is the UI's companion boost press edge. A press counts
  only while no input is held, so one touch-board tap that drives the touch
  indev and a synthesized button indev boosts once. `press-edge` covers it.
- `FurbleUIGesture.h` is the accelerometer gesture state machine. `sample()` is
  the deterministic seam host tests and the simulator drive; `poll()` is the
  only method that touches hardware. Amplitude thresholds are scaled by a
  per-sensor gain and a console-settable calibration scale, because a real
  sensor in a real case never matches the paper numbers. Gesture settings
  written from the console or the companion must go through
  `UI::notifyGestureSettingsChanged()`; no other task may touch LVGL.
- `FurbleUI.h` holds the physical-button layout's geometry contract. Where the
  three button legends sit is the `LEGEND` setting, read through
  `UI::legendPlacement()`: Buttons, the default, keeps the Right one at
  `m_RightYOffset` down the right edge, and Bottom puts all three in the
  reserved navbar band. `level_t::navRightYOffset` carries the same offset for
  the level page, which re-anchors all three on rotation, so a new anchor has to
  change the `UI::UI` constructor and `applyLevelRotation` together.
  `UI::floatingIndicatorReserve()` reports the room rows keep clear and
  `reserveLegendColumns()` applies it consistently to every row that can scroll
  through the floating legend. `UI::legendSelectable()` is the one board list
  both the setting page and the placement obey.
  `LEGEND_OFF` retains those anchors and reservations for physical input while
  suppressing only the rendered indicator surfaces; it must never remove or
  move an LVGL button hit target.
- Under `FURBLE_SIM`, `FurbleUI.h` exposes the typed `simScenarioAction` API
  using `Sim::scenario_action_t` from `sim/scenario_action.h`. Calls return
  `APPLIED`, `VALID_NO_EFFECT`, `UNAVAILABLE`, or `INVALID`; malformed direct
  actions fail closed. Keep this overload and result enum simulator-only so
  firmware builds retain the production header surface.
- Under `FURBLE_SIM`, `FurblePlatform.h` declares and befriends the simulator
  readers for the retained PMIC model: `watchdogState`, `motionWakeState`,
  `pmicIrqState`, `pmicIrqStatus` and the two injected PMIC events. They exist
  only so a scenario can observe the aggregated IRQ line and the watchdog
  cycle, and firmware has no callers for any of them.
- FurbleWiFi.h exposes station provisioning and NTP status for the app layer.
  `hasAssignedAddress()` is the live station readiness query: it asks esp-netif
  for an up netif with a non-zero IPv4 address instead of reading the cached
  status, so the WebUI gate cannot outlive a lost DHCP lease.
- `FurbleWebUI.h` owns the HTTPS supervisor and release-retry state. The pure
  ingress checks in `FurbleWebUIProtocol.h` are shared with host coverage. Held
  ownership never crosses a complete Control session replacement.
  `HTTPD_MAX_SOCKETS` counts client sockets; ESP-IDF reserves three additional
  internal sockets. The production value is two clients (five total).
  `initEarly()` must run before WiFi/display allocation. Both WebUI task stacks
  stay in internal RAM; `FurbleWebUIMemory.h` holds the 4 KiB/8 KiB sizes and
  the 20,480/12,288 server gate. The gate log text lives in the
  `FURBLE_WEBUI_*_GATE_LOG` macros because `ESP_LOG_ATTR_STR()` needs a
  literal; the matching constants are for the simulator. It also holds the
  12 KiB identity stack and the 20,864/12,672 runtime identity gate; keep its
  derivation comment current when the identity code or mbedTLS pin changes.
- `FurbleCompanionMemory.h` holds the 3.75 KiB companion task stack (plus a
  512-byte margin under `FURBLE_DEBUG_BUILD`) and the
  8,320/4,224 first-enable gate. `tools/stack_budget.json` reads the stack
  value from it. The simulator logs through `FURBLE_COMPANION_START_GATE_LOG`.
- `FurbleTaskMemory.h` holds the derived 384-byte TCB and allocator overhead
  of one task. Every task admission gate adds it; do not copy the value.
- `FurbleMQTTMemory.h` has separate plain and TLS start floors. A secure
  `mqtts://` URI must pass the 6 KiB TLS-worker floor, not the plain 3 KiB
  worker floor. `wss://` is not part of the contract because no firmware build
  enables the WebSocket transport.
- `FurbleMQTT.h` `m_Incoming` is the event copy the esp-mqtt client task
  fills in `eventHandler`; that task's plain stack is 3 KiB, so a
  `queued_event_t` must never live on it. `m_Received` is the copy the owner
  receives each queued event into, so the owner stack never holds one either.
  Both live in one block that the owner allocates with the event queue (PSRAM
  when the queue is there) and never frees, because a stopped client may still
  deliver a late callback. `m_IncomingMutex` guards `m_Incoming` for
  overlapping clients; hold it only across filling and queueing the copy.
  `m_Received` belongs to the owner task alone. A lifecycle event carries the
  client, the event id and the esp-mqtt error type and CONNACK code, copied
  before the callback returns because the owner classifies the failure later.
  The retry backoff (`m_RetryDelayMs`, `m_NextAttemptMs`) is under `m_Mutex`.
  Without PSRAM the start gate reserves `EVENT_STORAGE_BYTES`, the queue plus
  both copies. A failed allocation aborts on every board, so the queue and the
  copies pick PSRAM from a fresh largest-block reading before allocating, never
  by trying and falling back, and an internal choice re-runs the start gate
  first.
- `FurbleUIMemory.h` owns the panel-sized LVGL draw-buffer contract. Narrow
  panels use one buffer and wait for the SPI DMA before `lv_disp_flush_ready()`;
  wide panels keep two buffers. Both stay DMA-capable internal RAM. Update
  `tests/host/ui_memory_test.cpp` whenever a size or the single-buffer rule
  changes.
- The simulator-only UI geometry surface keeps content overlap, label clipping,
  and focused-control expanded bounds as separate diagnostics. Focus clearance
  excludes the focused object's own descendants and ancestors.
- `FurbleBoardCaps.h` turns the board identity macros into capability macros,
  each defined to 0 or 1. Test one with `#if` only, never `#ifdef` or
  `defined()`, and include the header directly in every file that uses one;
  a missing include evaluates silently to 0. The coupling ratchet enforces
  both rules, scans `sim/` and `tests/host/` too, and rejects a name the
  header does not define. An include inside a comment does not count.
- `BatteryStatus::gate()` in `FurbleBatteryStatus.h` gates each battery status
  field by its own `battery_caps_t` flag. Never gate on the union of the
  flags: partial boards such as the StickS3 and M5Stack Core would then
  report unsupported readings.
- `FurbleControl.h` keeps `esp_power_level_t` for the setting and the
  adaptive policy, but no public member maps it. The ESP to `TxPower` mapping
  is file-local in `FurbleControl.cpp`; boot code calls
  `Control::savedTxPower()` before `Device::init()`.
- `FurbleBoard.h` holds one `Board::Profile` row per board model, chosen from
  the detected board (an image serves several boards). Non-UI modules read
  board facts from it instead of switching on `M5.getBoard()`; UI geometry
  stays in the UI and compile-time facts stay in `FurbleBoardCaps.h`. A new
  row needs a matching case in `tests/host/board_profile_test.cpp`. The row
  also carries the driven-stage polarity (`ledActiveLow`, `irMarkLow`) and the
  timed-wake kind, because both follow the wiring and not the pin number.
  `canPowerOff` and `powerOffPath` are separate columns on purpose.
  `canPowerOff` is the UI policy gate for the Auto off and Low battery rollers
  and is already false on the Core, which keeps its IP5306 path. `powerOffPath`
  says whether `Platform::powerOff()` can remove power at all, and only the
  Waveshare row is false. See plans/349-waveshare-board-row.md. The Tough row
  carries the hardware's SD slot, NS4168 speaker, BT1-backed BM8563 and RTC
  alarm wake; the Core2 row keeps `TimedWake::NONE` because the 2023.2 revision
  removed its RTC backup cell (plans/354).
  `rtcOnBus` means an RTC chip the driver can reach answers on the internal
  I2C bus, which is only the Plus2's BM8563; it is not the same fact as
  `rtcBatteryBacked` or as the `TimedWake` kind, so gate RTC writes on it.
- `Board::gpsPins()` is the only GPS UART pin source. `BOARD_SUPPORT` reads
  M5Unified's Port A table in `src/FurbleBoard.cpp`; a board M5Unified cannot
  describe sets `GpsPinSource::PROFILE` and carries `gpsRxPin`/`gpsTxPin` in its
  row; `GpsPinSource::NONE` means the board has no GPS UART and claims no pin.
  `NONE` is for a board whose Port A belongs to another peripheral, not a
  fallback: the Core (Port A is the internal I2C bus with the IP5306 and
  MPU6886) and the Waveshare ESP32-S3-ETH (GPIO13 is the W5500 SPI clock) both
  hit a live defect before it existed. `FurbleGPS` installs nothing on `NONE`
  and reports the receiver absent, so never add a pin fallback there, and never
  give `board_M5StampS3` `BOARD_SUPPORT` because M5Unified reports the id. -1
  means no pin and is what `uart_set_pin` receives on the other sources. See
  plans/336-gps-pin-seam.md.
- `FurblePinShare.h` is the ownership contract for one pin shared by the
  feedback LED and the IR emitter (Plus2 G19). Feedback calls
  `PinShare::requestLed()`; IR brackets a frame with `beginIr()` and
  `endIr()`. The glue takes one short mutex per call and never holds it across
  an RMT transfer, so no caller may add work inside those two calls. Only
  `ledSharesIr()` selects the path; a board with separate pins keeps driving its
  LED directly.
- `FurblePmicIrq.h` owns the M5PM1 IRQ status facts and the clear sequence
  `Platform` uses. IRQ Status 1 (0x40) and Status 2 (0x41) are latches. The chip
  manual (V1.9) states the hold rule: a GPIO configured as IRQ is held low until
  the IRQ Status is cleared. The zero write is the vendor driver's own clear
  (`irqClearGpioAll()` and `irqClearSysAll()` both write 0x00), not a manual
  claim. `clearLatched()` writes Status 1 then Status 2 through the caller's
  retried access, so a late GPIO4 edge survives the sequence for the motion
  poll. `tests/host/platform-wake` compiles `src/FurblePlatform.cpp` against the
  latching shim and drives the four call sites; `pmic_irq_test` covers the
  header alone. Do not replace the clear with a mask write and do not clear only
  Status 1; see plans/363.
- `FurbleFeedbackPolicy.h` holds the speaker volume ceiling
  (`MAX_BATTERY_VOLUME` = 191, the StickS3 notice's 75 percent of 0 to 255) and
  `onBatteryPower()`, which is true when the board can tell its power source
  apart and has no positive VBUS reading. The charge flag is deliberately not an
  input: a failed PMIC read reports "charging" on the StickS3, so only a VBUS
  measurement proves mains power. Callers cap what reaches the amplifier and
  never rewrite the stored setting.
- `FurblePowerBackend.h` is the contract a board's power hardware meets: the
  shared battery and rail types plus the member list `meetsPowerBackend()`
  checks, asserted after `Platform` in `FurblePlatform.h`. It is a
  link-time contract with no virtual functions. `Platform` meets it for the M5
  boards and the simulator; a new board family supplies its own `Platform`
  file on the drivers in `lib/hal`. Never make it virtual: a vtable defeats
  `--gc-sections` and pushed the headless Waveshare image over its IRAM budget.
  See plans/implemented/322-hardware-abstraction.md.
- `Rail::EXT_5V` is named by what it feeds, not by one consumer. On the
  StickS3 it is M5PM1 `PWR_CFG` 0x06 bit 3 (`BOOST_EN`, the EXT_5V port), which
  powers the Grove 5 V output, the HAT EXT_5V pin and the infrared circuit as
  well as the receiver; on the AXPs it is EXTEN or LDOIO0. Never add a rail
  named after one client.
- `FurbleRail.h` owns the rail ownership contract: one owner (`Platform`) and a
  reference count of named holders (`RailHolder::GPS`, `IR`, `GROVE`). The first
  holder switches the rail on, the last holder switches it off, and a cut is
  refused while another holder remains. `RailAccount` is counted per holder, not
  per call, so a repeated acquire from one task cannot inflate it and a stale
  release cannot strand the rail on. Every firmware client holds the rail
  through `Platform::railAcquire()`/`railRelease()`; `Platform::setRail()` is the
  owner's own switch, refuses a switch-on with no counted holder, and must never
  be a client's entry point. The IR task takes the IR hold when it dequeues a
  request and releases it after the burst, which is the deferred-cut rule that
  stops a GPS duty cycle cutting the carrier without letting a queued request cut
  one already in flight.
  See plans/364-ext5v-rail.md.
- Rail returns are read-back, never intent. A cut refused for another holder, a
  refused output-mode enable and a PMIC that did not take the write all return
  false, and `GPS::railOffOrRestart()` restarts a receiver it stopped for a cut
  that did not take. Do not "fix" a false into a retry loop: the holders change
  on their own tasks.
- `FurblePlatformPolicy.h` holds the EXT_5V output-mode interlock
  (`railEnableVerdict()`). Output mode is refused while an external 5 V source
  is on the bidirectional port or the supply cannot be established, because the
  vendor documents short-circuit and device-damage risk otherwise. The failure
  direction is closed: a refusal writes nothing, so the port keeps the vendor
  default of input mode. A cut is always allowed. See plans/364-ext5v-rail.md.
- `PWR_CFG` 0x06 has one writer, `Platform::updatePowerConfig()`, because the
  rail bit and the status LED bit live in the same byte and both callers run on
  different tasks. Its `m_PowerConfigMutex` is a leaf lock: never hold it across
  a delay, an event wait or a call into Control, and never take `m_RailMutex`
  while holding it. Lock order is rail, then power config.
