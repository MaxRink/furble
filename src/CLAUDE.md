# src/ (app layer)

Application layer on top of lib/furble. Headers live in include/, sources here.

- `main.cpp` starts two contexts: a FreeRTOS control task
  (`Furble::Control::getInstance()`, priority 4) and the UI loop in the main
  task. Init order matters: Settings, Platform, Feedback, Device before
  Control. Settings must precede Platform because Platform reads FB_OUTPUT to
  set `cfg.internal_spk` before `M5.begin()`.
- `FurbleControl`: camera connection state machine. It owns the mutex from the
  root traps section. Keep critical sections short and delay-free. Restart
  entry points use `Platform::restart()` so camera disconnects, the bounded
  wait, and the S3 watchdog shutdown stay together.
  An empty connect target set returns idle, never active. A concurrent cancel
  still takes precedence and returns disconnecting.
  Every walk of `m_Targets` holds `m_Mutex`, including the control task's
  `STATE_ACTIVE` liveness check and command fan-out and the 1 Hz GPS service's
  `updateGPS()` fan-out. That service runs on the UI loop, and it releases the
  lock before posting the command that reaches the control queue. See
  include/CLAUDE.md for the ownership and lock-order contract (plan 327).
  The control task's queue wait is a 250 ms idle tick. Work that is not a
  command, such as reaping the drain after a cancel, must wake it: the
  interactive disconnect posts one no-op `CMD_ERROR`, or every cancel holds
  the connect gate for up to a tick (plan 317).
- The simulator UI loop calls its post-`lv_task_handler` hook only after the
  real production UI handler returns. Keep this hook observational: it may
  advance simulator settle bookkeeping, but must not move restart ownership or
  restore UI, Control, LVGL, or task state across a process reboot.
- Adaptive Bluetooth power sampling stays in the control task and uses the
  weakest connected camera because NimBLE connection power is global. NVS
  reads, RSSI reads and radio calls run with the Control mutex released,
  snapshot the targets first.
- `FurbleSettings`: type-safe NVS settings via `Settings::load<KEY>()` /
  `Settings::save<KEY>()`, backed by lib/preferences. New settings need the
  enum entry, a `storage_type` specialization, and a default.
- The remembered multi-connect set is keyed on the camera's displayed name, and
  a vendor client may compose that name. Store and compare it only through
  `Settings::multiselectAdd()` and `Settings::multiselectContains()`, which
  compare the whole stored string. A prefix comparison ticks the wrong body of
  the same model. `Settings::load<multiselect_t>()` widens a record written in
  the pre-32-byte layout, so changing `MULTISELECT_NAME_MAX` again means adding
  another legacy layout rather than dropping every saved selection.
- The IMU setting is a persisted bool at wire id 46, defaults off, and applies
  on reboot because it controls `M5.begin()` capability discovery. IMU pages
  must be gated on both that setting and `M5.Imu.isEnabled()`; the Level timer
  is started and stopped by page dispatch, not a widget-only callback. The
  motion activity callback is gated by the cached `IMU_WAKE` setting, so Wake
  Gesture Off never resets display inactivity. The MPU6886 hardware wake path
  remains uncertified until its clear-on-read race with M5Unified is measured.
  Every motion count and its documented mg, ms and g value lives in
  `FurbleIMUMotionScale.h`; the three backends here read it and never convert a
  count by hand. The MPU-6886 count size is this project's assumption until a
  measured tilt test settles it, see plan 332.
- `FurbleTimeKeeper`: owns one versioned CRC-protected wall-clock blob. Normal
  synchronization writes are limited to four per day and shutdown checkpoints
  without a backed RTC use a separate three-and-a-half-hour age budget. The
  strict rolling-window maximum is eight per day. Only a known battery-backed
  RTC may suppress those checkpoints, and the Plus2 counts as one because the
  board keeps its BM8563 alive across the HOLD power-off; its protection against
  a real outage is the BM8563 voltage-low flag (`0x02` bit 7), which is read on a
  backed RTC only. Keep every restart and power-off path on the
  shared flush helper and never add per-tick NVS writes.
  A copy of the same record in `RTC_NOINIT_ATTR` memory keeps the running
  system clock across soft resets on boards without an RTC chip. Rewrite it on
  every accepted sample, anchored where the system clock was set; never seed
  the system clock from NVS after a reset that kept it. The kept sample is
  demoted to `RTC` authority (NVS stays NVS) so the next GPS, NTP or companion
  sample can correct it; never restore it as GPS or NTP. Its uncertainty is an
  estimate of RC slow-clock drift, not a bound. The intervalometer does not
  trust an `RTC` sample when no RTC chip is present (plan 139).
- The StickS3 M5PM1 watchdog is armed for 45 seconds through the shared
  `Watchdog` constants. Keep `watchdogFeed()` at its one second cadence and
  preserve verified disarm and readback before flash or power-off paths.
- `FurbleGPS`: TinyGPSPlus over UART2. Mind the UART clock source trap. The
  parser is owned by GPS and guarded by its mutex; UI and console consumers use
  `getStatusSnapshot()` and must not retain parser references. GPX logging only
  builds a point and queues it via `SD::logPoint()`, never does file I/O.
  Burst power cycling uses a bounded degraded retry state. It releases
  `NO_LIGHT_SLEEP` during backoff, ignores late UART lock reacquisition, and
  reacquires only after moving to the time-bounded acquisition probe. Keep the
  host policy and simulator power-accounting tests aligned with these states.
  Under `FURBLE_SIM`, the fresh-fix parser counter is observability only; it lets
  GPS scenarios distinguish parser progression from a cached source without
  claiming a coherent delivered-geotag snapshot or timing-sensitive cycle state.
  Duty-cycle freshness is established at the per-byte TinyGPS++ location update
  boundary under `m_GPSMutex`, not by comparing `location.age()` values.
  The service mutex covers each task pass and settings reset. Before waiting
  for it, settings transitions must set the enable gate false and send the
  private front-of-queue wake event so an idle UART receive releases the mutex.
  Cycle status readers (`isDegraded()`, `getCycleStatusSnapshot()`,
  `getReceiverStatus()`) load the word `publishCycleStatus()` stores at the end
  of each task pass and after enable() or disable(); they never take
  `m_CycleMutex`. The service passes lock it blocking and never skip: a pass
  that skipped a consumed UART event lost the whole burst, and the cycle went
  degraded for at least the 10 s backoff.
- `FurbleIR`: RMT on `RMT_CLK_SRC_DEFAULT` (APB) is safe under DFS here, but not
  because the driver locks APB. The driver takes `ESP_PM_CPU_FREQ_MAX` for a
  channel without DMA and `ESP_PM_NO_LIGHT_SLEEP` for one with DMA
  (`components/esp_driver_rmt/src/rmt_common.c:230`), and `ESP_PM_APB_FREQ_MAX`
  appears nowhere in `esp_driver_rmt`. `createChannel()` never sets `with_dma`.
  furble raises the PM CPU maximum itself, 160 MHz by default
  (`include/FurblePlatform.h:25-28`, `src/FurblePlatform.cpp:84`), and APB is
  80 MHz at that maximum, so the 38 kHz carrier holds. The divisor is
  fixed at channel creation, before the lock is taken, so this only holds while
  APB is already 80. Setting `with_dma` on the S3 would swap the lock for
  `ESP_PM_NO_LIGHT_SLEEP` and let the carrier drift. Do NOT switch to
  `RMT_CLK_SRC_XTAL`, it does not exist on plain ESP32. Transmission runs on the
  dedicated ir task, never under the Control mutex. The emitter polarity is a
  profile fact (`irMarkLow`): the StickC and Plus drive the cathode from the
  pin, so their channel sets `invert_out` and the encoder stays mark-high. The
  carrier duty is flipped on those rows so the optical duty stays a third.
- `FurblePinShare`: the Plus2 drives the red LED and the IR emitter from G19, so
  `PinShare` owns that pin. Feedback records its level through
  `PinShare::requestLed()`; IR calls `beginIr()` before a frame and `endIr()`
  after `rmt_disable()`, and the hand-over restores the last recorded level and
  returns the pad to the GPIO path (`rmt_disable` leaves it muxed to the RMT
  signal). The mutex there is taken per call and never held across a transfer or
  an RMT wait. Boards whose LED and emitter pins differ never enter the path,
  and an unrecorded level is released as an input instead of being invented.
- `FurbleFeedback`: optional sound, LED and vibration event outputs. DFS-safe
  by construction: all sound paths go through I2S and the IDF i2s driver holds
  `ESP_PM_APB_FREQ_MAX` while the channel is enabled, so no extra pm lock is
  needed. The LED is plain GPIO, DFS-immune, and `PM_SLP_DISABLE_GPIO` is
  unset so pads hold through light sleep. Vibration is I2C, whose divider is
  recomputed per transaction. The output selection is frozen at boot (it
  decides `cfg.internal_spk`), only the event mask and volume reload live.
- The touch Remote shutter page keeps its 64 px controls intact. Measure the
  page content width through native flex row wrapping so the full-size
  content-sized controls stay in one row when they fit and wrap into scrolling
  rows when they do not.
  On the 320x240 Core touch model at Large, the root 2026-09-13 GDB trace
  measured a 316 px page, 136/114/136 px content-sized cells, 106/84/106 px
  labels and 162 px cells; the inherited horizontal wrapper padding made the
  third cell wrap and produced a 344 px row. Candidate `f76a374e` removes only
  that wrapper's left/right padding under `FURBLE_M5COREX`, preserving the
  64 px controls, selected font and vertical spacing. Runtime validation is
  pending; do not relax the Core fit assertion or shrink the controls/font.
- `FurbleGPS` demultiplexes NMEA and CASIC binary frames. It sends at most one
  acknowledged configuration command at a time and keeps the fallback path.
  Phase 2 adds `GPS_BAUD` Auto with the `Casic::Autobaud` ladder and a
  no-receiver state, tier 2 ephemeris poll and replay, the GSV/GSA satellite
  page parser, the `GPS_PLATFORM` dyModel write and a MON-HW poll. The
  parseable logic lives in `lib/furble/protocol/GpsCasic`.
- `FurbleGPS` identifies the receiver once per session before configuring it:
  a UBX-MON-VER poll, two attempts a second apart (`Ubx::Detect`). A u-blox
  receiver gets the UBX backend in `src/FurbleGPSUbx.cpp`; anything else keeps
  the NMEA backend, which is the CASIC/$PCAS path unchanged. Vendor-specific
  actions (configure, restart, standby, satellite capture, MON-HW, ephemeris)
  switch on `GPS::backend_t`; add a vendor as one more value and one more
  configure function. The `$PCAS` command set, its argument ranges and the
  MON-HW layout are recorded per protocol revision on
  `docs/components/at6668.md`: the 2020 specification and the 2025 one disagree
  about `$PCAS04`, `$PCAS15`, the binary classes and the `antStatus` encoding,
  and the receiver does not state which revision its ROM follows. UBX configuration goes through the same one-in-flight
  queue, via `enqueueUbxConfig()` with a codec-built frame, with a one second
  ACK timeout and writes RAM and BBR, because software
  standby clears RAM. The settle wait after a signal change is a
  `Ubx::Settle` gate, never a raw tick compared with 0: that form stalled every
  command in the second half of the tick range. A fresh NAV-PVT overlays the
  parser snapshot under `m_GPSMutex` unless the NMEA fix is more than two
  seconds newer; when it is stale the NMEA parser is the fallback. A valid but
  not fully resolved PVT time keeps the fix, and `status_t::time_resolved`
  gates the TimeKeeper sync. The ephemeris cache and AID-INI are CASIC only and
  never reach a UBX receiver. The stream split is the pure
  `GpsDemux::split()`; keep demultiplexing there, not in the driver.
- `FurbleEthernet.cpp` owns the W5500 RESETn pulse. The pinned ESP-IDF W5500
  PHY hardcodes 100 us in `w5500_reset_hw()`, so the transport drives a 500 us
  pulse before `esp_eth_driver_install()` (which runs the PHY hardware reset and
  then the MAC init over SPI) and passes `reset_gpio_num = -1`. Do not hand the
  pin back: a second 100 us assertion would be the last one and under the
  datasheet minimum.
- The duty cycle waits until the receiver is identified and the configuration
  queue is idle. The NMEA sentence prune is `Casic::sentenceCommand()`, never a
  hand-written `$PCAS03`: an empty field keeps the receiver's rate, so a short
  sentence cannot turn a sentence off. On UBX the driver follows
  `Ubx::PlanSummary`: when the plan enables BeiDou B1C it sends the
  `OPERATEMODE=FULL` write before the signal batch, because a power save mode
  an earlier session left behind makes the receiver reject B1C (ROM release
  notes 7.2), and it reports the ANA value the plan actually wrote, which is
  false with B1C (RN 5.1.5). Standby sends `$PCAS12` only on the NMEA backend and
  RXM-PMREQ (force flag) on UBX. A duty request seen in STANDBY or RAIL_OFF
  is stale (the UI pushed a fix the cycle already parked for) and is dropped;
  a resent PMREQ would wake a UART-wake receiver at once. Rail cycling on UBX without a backup supply
  first sends CFG-RST GNSS stop and UPD-SOS create and holds the cut in
  `m_RailOffPending` for up to one second; `disable()` does the same inline,
  so it can block for up to one second with the GPS task parked. Neither runs
  when `Platform::canCutGpsRail()` is false (the board profile's `gpsRail`
  column: Core, StickC Plus2, Waveshare, headless) or
  `hasGpsBackupSupply()` is true. A refused or unanswered save sets a latch
  (`m_UbxSosBackup` 2) that lasts until the next GPS enable. If the rail stays
  on after a cut that followed a GNSS stop, `railOffOrRestart()` sends GNSS
  start. `m_UbxStopped` records that stop; only `railOffOrRestart()` clears
  it, because `disable()` runs `resetUbx()` between the stop and the cut.
- `CompanionService::m_Mutex` serializes the status and camera notification
  caches and all
  trigger rate and held-command state, including timer and disconnect release.
  Keep transport-triggered callbacks on that ownership rule, but never hold it
  across a transport virtual call because production GATT takes its own mutex.
  A zero-duration timed trigger releases inline because `handleTrigger()`
  already owns the service mutex.
- The companion cameras characteristic never drives `Control` itself. Connect
  and disconnect go through `UI::sendRequest`, the same request queue the
  console uses, because `Control::disconnect()` waits for the teardown and the
  on-device screen has to follow the remote action. That queue is compiled into
  every build; only `PERF` and `AUDIT` stay behind `FURBLE_CONSOLE`. For the
  same reason the camera records read rssi from `Control::getTargetState()`,
  never `Camera::getRssi()`, which takes the camera connect mutex a cold
  connect holds for the whole connect timeout. Companion connect requests carry
  stable saved camera ids and are resolved and busy-checked again on the UI
  task; transient scan indexes must never cross that queue boundary.
  Console completion envelopes use the same intrusive `RequestState*` ownership
  in display and headless builds. Caller timeout does not release the queued
  owner's reference; completion or queue draining does.
- Companion credentials use the checked
  `Settings::loadPassword()` path: missing is unset, storage errors deny access.
  Never seed an empty password after an unsuccessful NVS existence check.
  Password console commands verify persistence and reload the live gate.
  The application gate rejects writes with a framed Auth result indication,
  not a custom ATT return from NimBLE's void write callback.
  `CompanionService::reloadPassword()` is the single live reload path, including
  a companion GATT password write; its password callback re-evaluates the WebUI
  credential gate so `m_PasswordReady` cannot stay true after a clear.
- `CompanionIdentity` owns the random-static LE address rotated by
  `companion reidentify`. Persist it under `companion_bleid` (15 characters,
  distinct from the `companion_addr` peer bond) and apply it in
  `CompanionGatt::init()` and on every disabled->enabled transition in
  `CompanionGatt::enable()`, before advertising. Rotation order is stop
  advertising, generate -> apply -> persist, then drop the link/bond; an apply
  failure leaves NVS untouched and a persist failure restores the previous
  controller identity. `Device::init()` intentionally restores the default RPA
  policy; applying the stored identity in the device library would couple
  protocol code to app NVS policy. Camera bonds are never deleted by the
  rotation.
- NimBLE may deliver `onAuthenticationComplete()` before `onConnect()` for a
  bonded central. `onConnect()` must inherit `connInfo.isEncrypted()` and
  `connInfo.isBonded()`; do not reset the flags to false, or the first auth
  write is ignored as unencrypted. Do not send a Service Changed indication
  during authentication. `CompanionGatt::reidentify()` is the explicit
  recovery for a stale host GATT cache: it drops only the companion bond,
  applies and persists the rotated LE address and reopens the pairing window.
  Camera bonds are untouched.
- `CompanionDebug` is the `FURBLE_BLE_DEBUG`-only, read-only BLE status service.
  It attaches during GATT construction without reading `CompanionGatt` state,
  and refreshes its snapshot only from the companion service task. Never call
  its update path while `CompanionGatt` holds `m_Mutex`, and keep it
  unencrypted so it still answers when companion auth or the host cache is
  broken. It is not part of the production companion contract.
- Compare `strcmp` results explicitly (`== 0`, `!= 0`). clang-tidy's
  `bugprone-suspicious-string-compare` runs on the firmware databases, and a
  bare `strcmp` used as a bool in `FurbleConsole` failed it (#463).
- Settings switch tables in `FurbleConsole`, `FurbleCompanionService` and
  `FurbleSD` must include every new `Settings::type_t` case. The `-debug` build
  enforces this with `-Werror=switch`, so build a debug env after adding a
  setting.
- `FurbleSD`: SD card service for the two Core boards. A dedicated writer task
  owns the card mount and all SD I/O. Every other task (LVGL, GPS, NimBLE)
  interacts only through `SD::request()` / `SD::logPoint()` and the atomic
  state accessors.   Never mount, unmount or touch files under `/sd` from
  another task. The mount point is `/sd` on device. `FURBLE_SD_MOUNT_POINT`
  exists only so the linked host conformance target can redirect the same
  production code to a writable sandbox; firmware leaves it undefined and
  device behavior is unchanged.
- `FurbleGPX`: GPX 1.1 track writer. Pure file writer with no SD or settings
  knowledge; every method runs on the SD writer task.
- `FurbleWiFi`: station lifecycle, remembered access point state and NTP.
  Never fall back to a WiFi scan while a camera is active. The event handler
  runs on the default event loop task (3,072-byte stack): it must not open
  NVS, read a setting or start SNTP. It updates memory and queues the
  access point save or erase and the NTP start for the 4 KiB worker
  (`serviceEventStorage()`); a newer save or erase replaces the older one.
  Every NVS write of the access point runs under `g_ApStorageMutex`. A clear
  bumps `g_ApGeneration` and cancels the queued work; a save captured under an
  older generation is dropped, so a clear is never undone.
  `IP_EVENT_STA_LOST_IP` clears `g_GotIp` and the cached address on the same
  task and notifies the worker, without stopping the station, the NTP client or
  the driver: esp-netif resets the address to 0 before it posts the event, and
  the DHCP client recovers on its own. A queued NTP start declines because
  `startNtp()` re-reads `g_GotIp`. `hasAssignedAddress()` answers readiness
  from that live station netif instead of the cache; the WebUI gate and its
  status page call it, and MQTT asks the same question of every netif.
- `FurbleMemoryPressure`: the runtime load-shedding governor. `tick()` runs
  right after `CrashLog::update()` in both UI loops and reads the internal heap
  once a second. AMBER pauses the WebUI server, RED also pauses the MQTT
  client; each service reads `MemoryPressure::current()` in its own run
  decision and returns through its own start gate. A pause never writes NVS,
  is not a connect failure and never touches BLE, Control, the UI, GPS, WiFi
  or the companion, so it never consumes an MQTT retry backoff step either.
  The MQTT owner reports the internal queue storage its
  next start still needs through `setMQTTStartExtra()`, from the same
  `MQTT::startExtra()` its start gate uses; RED adds it to its exit. Keep the
  two on that one helper. `app_main()` registers `MemoryPressure::admit()` as
  the Control admit hook, and `Control::addActive()` calls it under its own
  mutex after the duplicate check, with the Target, its queue block and the
  task: when the new target would take the heap into AMBER or RED, that level
  is entered at once instead of after the debounce. A repeated connect admits
  nothing, and connects from two tasks are admitted one after the other.
  Callers of `addActive()` never admit themselves. On a rise `admit()` calls
  the escalate hook outside the governor mutex; `WebUI::init()` registers one
  that wakes the supervisor, so the server stops shortly after the target
  task is created. Nothing orders that stop against the NimBLE client or the
  connected page's first draw. MQTT keeps reading the level on its own loop.
  `tick()` and `admit()` share one mutex, never held across a delay or another
  lock; include/CLAUDE.md records the hook lock order. Policy and thresholds live in the header and are
  host tested in `tests/host/memory_pressure_test.cpp`.
- `FurbleWebUI`: HTTPS-only browser and REST surface. Keep it fail closed on
  password or TLS identity errors. `networkReady()` is
  `WiFi::hasAssignedAddress()`, and `/api/status` reports the same answer, so a
  lost DHCP lease closes the listener instead of serving on a dead address; the
  cached `WiFi::status_t` keeps the last address until the next event. Camera
  actions stay on `UI::sendRequest`, settings stay on `Provision::apply`, and
  held releases remain owned until the Control queue accepts them or the camera
  session ends. Complete Control session replacement expires held ownership by
  generation, including when disconnect and reconnect both occur between WebUI
  supervisor polls.
  `HTTPD_MAX_SOCKETS` counts client sockets; ESP-IDF reserves three additional
  internal sockets. The production value is two clients (five total).
  `reloadSetting()` must not create the TLS identity task while `web_ui` is off.
  `initEarly()` prepares the TLS identity before the radios and display; the
  supervisor and HTTPS stacks stay in internal RAM at 4 KiB/8 KiB and must not
  move to PSRAM because the HTTPS task commits NVS. `reloadSetting()` caches
  the setting, password and credential state, so the supervisor loop never
  reads NVS. `startServer()` runs `WebUIMemory::serverGate()` before allocating
  and logs the measured free/largest pair when it refuses. An out-of-memory
  start keeps the stored identity; only a rejected certificate/key pair is
  replaced once. Every runtime identity task goes through
  `startIdentityTask()`, which runs `WebUIMemory::identityGate()` first; a
  refusal clears `m_IdentityTaskActive` and writes no NVS, so the next
  `reloadSetting()` retries. Only the boot path in `initEarly()` is ungated.
- `FurbleCompanion`: `CompanionGatt::enable()` runs
  `CompanionMemory::startGate()` (`include/FurbleCompanionMemory.h`) before the
  first enable creates the retained task and GATT database. A refusal leaves
  `m_Enabled` false and logs the measured pair. Later enables reuse both and
  are not gated; never delete the task or the services on disable.
- `FurbleUI*`: LVGL UI. Respect the changed-check rule for periodic setters.
  Both session teardowns (`doDisconnect()` and the connect timer's idle path)
  end with `dropSessionImages()`, which empties the LVGL image cache on boards
  that have one so the connected-page entries return their internal metadata
  and the WebUI can clear the AMBER exit (plan 304). It is safe only because
  LVGL runs with `LV_OS_NONE` and is called on the UI task outside a refresh;
  `lv_image_cache_drop(NULL)` frees an entry that is still referenced. Never
  call it from a draw callback or another task.
  LVGL 9.4 retains an encoder input device's `last_pressed` object when that
  object is deleted. Only when switching a persistent physical-button device
  from `LV_INDEV_TYPE_BUTTON` to `LV_INDEV_TYPE_ENCODER`, reset it through the
  public pointer input path, then restore the encoder type. This clears the retained object without using
  private LVGL fields. Keep the reset under the UI mutex and do not treat it as
  a replacement for releasing held inputs.
 Menu rows keep their icons and the selected font. Their labels wrap inside
  the row and the page scrolls vertically when the rows no longer fit. Do not
  use circular scrolling for an eager-built menu row: LVGL keeps its animation
  running while the page is hidden and invalidates it every frame. Camera names
  and spinner summaries also explicitly wrap. `ui.row_scrolling` and
  `ui.invalidate_count` measure this redraw trap. A wrapped row is taller and
  fills its width; it
  used to reach the indicators the Stick boards floated over the page. Where
  the legends sit is the `LEGEND` setting now: in the default Buttons placement
  the Right one is drawn over the page and `reserveLegendColumns()` gives every
  potentially scrolling row a stable right boundary when the page loads. In
  Bottom placement all three are in the navigation band and the reserve is
  zero. `LEGEND_OFF` uses the Buttons geometry and reservation but makes the
  indicator surfaces transparent; the LVGL button objects and physical input
  points remain live. Do not relayout rows from a scroll callback.
  The SCROLL_END settle cancels the async pass a SCROLL frame queued; a stale
  pass after the settle moves rows without re-pinning the bottom. Info pages
  use `addInfoPage()`: the reservation replaces its container's right
  padding while it is narrowed (plan 317).
  The width sample must follow the final 24x24 indicator sizing and a settled
  layout; the 2026-09-13 host-GDB trace found the pre-fix `m_LegendWidth` was
  26 px while the final Right box was 24 px (`x1=111,y1=173,x2=134,y2=196`).
  Commit `8aa65dfd90a108bb92e59977173d718024fc2128` records that narrow
  measurement-order correction. It is a two-pixel reserve correction, not a
  claim that every Large-font label becomes single-line.
  `ui.indicator_clearance` is still the check, and it has to hold in both
  placements.
  The simulator overlap metrics apply LVGL's native `lv_obj_area_is_visible`
  to each measured leaf before comparing it with the viewport or indicators.
  This is diagnostic clipping only: `simDrawnArea()` and the raw drawn-area
  path used by `ui.cut_labels` remain unchanged. The 2026-09-13 GDB trace
  measured a Bulb roller at x=50..74 while its horizontal scroll-row parent
  ended at x=51 and the legend began at x=56, proving the prior overlap was a
  clipped-child false positive. Keep the existing physical 80x160 clearance
  scenario as the runtime regression; do not add a second geometry framework.
  The Display settings page reserves an 8 px row gap for the themed 3 px
  focus outline plus 2 px outline padding; raw widget bounds do not include
  that decoration. The simulator's separate `ui.focus_overlaps` query measures
  conservative expanded bounds against unrelated content, not knob/shadow pixels.
  The Display settings page uses content-sized rows on all panel/layout
  variants; keep the three-panel touch matrix as the gate for any geometry
  claim.
  The Sensors Restart button likewise needs an explicit parent width before its
  percentage-sized label; otherwise LVGL can converge on a zero-width
  parent/child cycle. Keep the strict touch fit checks until runtime validation
  confirms the measured fix.
  The Bulb Start button follows the same rule: set its parent width before a
  percentage-sized Start label. Keep compact-fit and clipped-value assertions
  unchanged while validating all panel variants.
  `addSettingItem()` uses a full-width wrapping label in its row-wrap
  container. Do not restore natural/max-only width or flex-grow: a switch can
  squeeze a long setting name into a clipped line instead of letting the name
  wrap above the switch.
  Plus/S3 spin rows keep their vertical padding but spend no horizontal pixels:
  at Large text `999 mins` needs the full 103 px narrowed row width. Four pixels
  of horizontal padding make that complete value wrap and push the Bulb page
  past its viewport.
 Standalone buttons on pages with a reserved legend column must wrap their
 labels inside the narrowed button and keep the text centered; natural-width
 labels are clipped by the button's parent even when the label itself fits.
  Simulator page identity maps use inferred-size arrays, never a hand-counted
  element bound. Navigation action names may be aliases, so parity checks must
 allow the documented action-to-page mapping rather than compare table sizes.
 Page-level prose labels are not rows and must keep the page width; only
  layout children that can pass through the floating legend are narrowed.
  Scan advertisements are copied by `Scan` and drained on this task before
  `CameraList` or LVGL is touched; keep scan start unlocked around controller
  calls so the watchdog and callback handoff remain responsive.
  Aggregate UI context assignments must initialize every field explicitly. Keep
  the connect timer paused until its context and widgets are ready.
  `doDisconnect()` returns to the Main page from every teardown path, including
  Cancel and terminal connect failure, so it must restore the physical-button
  indicators hidden by the connect modal before returning.
  Under `FURBLE_SIM`, a driver exit request is observed inside the locked UI
  phase. Unlock and return from the task so simulator workers can be joined;
  never terminate the process from this production source.
  Simulator scenario actions enter through the typed parser in
  `sim/scenario_action.{h,cpp}` and are dispatched from that same canonical
  representation. `UI::simScenarioAction` reports `APPLIED`,
  `VALID_NO_EFFECT`, `UNAVAILABLE`, or `INVALID`; malformed direct calls must
  fail closed. Queue closures own their action and result state so a timed-out
  `simRunOnUi` request cannot retain stack references. Keep the explicit
  `page main` and `page menu` root routes, and reject scroll `INT32_MIN` because
  the signed value is negated for LVGL's int32 scroll delta.
  Camera pairing actions use the production modal and answer path, and their
  queries read the rendered code, modal kind, overflow, pending state, and
  timer state. The generic `ui.modal` query covers camera and companion
  dialogs, while `ble.pairing_answers` and `ble.pairing_answer` read the real
  MockNimBLE injection tally. A second pending camera keeps the timer armed
  until its prompt is shown; a declined or expired camera is not retried until
  an explicit fresh connect.
  `ControlMode::PRESET` remaps the three keys to minus, confirm and plus while
  the bulb Duration page uses the exposure preset picker.
  The header row is end aligned so the battery holds the right edge. Do not
  add a zero-width spacer or other empty header child: each one still costs a
  flex column gap, which pushed the battery off the 80 px header (plan 315).
  The Cameras page relies on `reserveLegendColumns()` alone; padding its
  labels as well reserves the legend column twice. A page container that can
  outgrow the 80x160 viewport is content sized so the page scrolls, never a
  full-height nested scroller.
  Fonts come from `fontForTextSize` and `fontForIconMenu` in FurbleUI.cpp:
  never hardcode a Montserrat font in a widget, and Large may only grow the
  icon menu font, never below its montserrat 16 default.
- `FurbleUIAudit`: layout audit walker (`FURBLE_SIM`/`FURBLE_CONSOLE` only),
  reached via `ui audit` on the console. Reports clipped labels and label
  overlaps as JSON Lines, see tools/ui-audit.md.
  The display off state machine lives here: `processInactivity` dims or sleeps
  the panel, sleep/wake pairs the APB lock with both 120 ms panel dwells, and a
  wake press is swallowed until every input source reports released. The dwell
  rule is per panel: ILI9342C needs 120 ms from SLPIN to the next SLPOUT as
  well as from SLPOUT to the next SLPIN, so `wakeDisplay()` keeps its wait
  (`src/FurbleUI.cpp:1887-1897`); ST7789V and ST7796S need only the SLPOUT to
  next SLPIN interval, which `sleepDisplay()` gates at `:1843-1845`.
  Only a pending low battery power-off countdown holds the panel awake
  through that state machine, the plain warning rides the normal dim/sleep
  path, and the LVGL idle clock is never touched. `wakeDisplay` does not count
  as activity, only a real input press triggers `lv_display_trigger_activity`.
  Intervalometer timed wake must retain its resume record when
  `Platform::powerOffUntil()` accepts the power-cycle request; clear it only
  after an explicit setup failure or normal completion.
  Modal boxes that steal focus must own a dedicated stacked input group, capture
  and restore the previous focus, and return physical navigation and shutter
  input to the page underneath when closed.
- `FurbleBtDebug`: console-only active BLE onboarding. Keep the raw explorer
  independent of `Camera`, NVS, and `CameraList`; pairing input is console
  passthrough and passive third-party sniffing is not supported by NimBLE.
  `Scan::start()` returns the physical NimBLE start result; leave the scan
  callback state stopped when the controller rejects the request.
- The GPS Data and Raw NMEA pages show TinyGPSPlus speed in km/h. Format
  user-visible coordinates to five decimal places without narrowing the double.
- Power policies: the one-second timer drives `processAutoOff` (disconnected
  idle power off, skipped while scanning or charging by default) and
  `processLowBattery` (warn or warn-then-off, consecutive-sample hysteresis on
  the smoothed level, power off gated on a real charging measurement).
  `AUTO_OFF_CHARGING` is an explicit opt-in for auto-off on external power;
  its saved bool uses wire id 43 and the `autooff_charge` NVS key. Both
  auto-off settings are cached in members, refreshed by the rollers and
  `Request::POWER_RELOAD`; no policy tick writes NVS. While the sampled battery
  state says charging, UI holds `NO_LIGHT_SLEEP`, allowing the panel to turn
  off without entering CPU/light sleep. All power off paths go through
  `UI::doPowerOff`: watchdog off, intervalometer quiesced with a proper shutter
  release, disconnect, then `Platform::powerOff`, which reports failure so the
  UI can resume.
- The companion status battery fields come only from
  `Platform::getBatteryStatus()`, in display and headless builds alike. Each
  field follows its own capability flag through `BatteryStatus::gate()`, so a
  board without a battery backend reports every field as unknown; VBUS
  follows `FURBLE_HAS_VBUS_SENSE`. Do not read `M5.Power` for status elsewhere.
- `GPS::setRailPower()` is the only code that switches the GPS Port A rail.
  The console `gps power` request goes through it too, so every switch gets
  the M5PM1 readback and single retry. It returns `Platform::setRail()`'s
  final read-back, which `railOffOrRestart()` uses to restart a receiver
  whose cut did not take. On the StickC and Plus the rail is already on before
  this runs: M5GFX writes AXP192 `0x12 = 0x4D` during `M5.begin()` and bit 6 is
  EXTEN, so `enable()` may not assume a power-on edge. Only the `RAIL_OFF` cycle
  state is a real off-to-on transition. The EXTEN-to-Grove-5V routing is
  UNVERIFIED; see plans/350.
- Under `FURBLE_SIM`, battery policy is exercised through the deterministic
  `Furble::Sim::battery_reading_t` seam. Keep `Platform::readBattery()` as the
  production boundary; scenario seeds/actions may replace its sample and
  `Platform::powerOff()` records the request so policy ordering can be asserted
  without terminating the host process.
- New source files must be added to `src/CMakeLists.txt` (alphabetical, before
  main.cpp). Component deps go in `idf_component_register` there.

Control cross-thread state contracts: `Control::m_State` is an acquire/release
atomic because the control task publishes it under `m_StateMutex` while UI and
other readers may sample it without that mutex. Keep compound transitions and
power-lock ordering under `m_StateMutex`; the atomic only removes the plain
read/write race. `Control::Target::m_Stopped` is also acquire/release atomic:
the target task publishes its terminal state before deleting itself, while
drain/reap predicates may observe it concurrently. The same explicit
acquire/release contract applies to `m_ConnectAbort`, `m_ConnectInProgress`,
and debug-only `m_SleepLockHeld`, whose readers cross task or snapshot
boundaries. The reconnect mode, backoff, attempt, and hint fields are also
independent acquire/release atomics for their cross-task reads and writes. They
do not provide group coherence, reset-wins, or a new request policy;
`m_ConnectFailCount` remains control-task-owned. These are narrow
synchronization changes, not a claim that the Control state surface is
race-free; raw TSAN and firmware/hardware evidence remain required.
