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
- Adaptive Bluetooth power sampling stays in the control task and uses the
  weakest connected camera because NimBLE connection power is global. NVS
  reads, RSSI reads and radio calls run with the Control mutex released,
  snapshot the targets first.
- `FurbleSettings`: type-safe NVS settings via `Settings::load<KEY>()` /
  `Settings::save<KEY>()`, backed by lib/preferences. New settings need the
  enum entry, a `storage_type` specialization, and a default.
- The IMU setting is a persisted bool at wire id 46, defaults off, and applies
  on reboot because it controls `M5.begin()` capability discovery. IMU pages
  must be gated on both that setting and `M5.Imu.isEnabled()`; the Level timer
  is started and stopped by page dispatch, not a widget-only callback.
- `FurbleTimeKeeper`: owns one versioned CRC-protected wall-clock blob. Normal
  synchronization writes are limited to four per day and shutdown checkpoints
  without a backed RTC use a separate three-and-a-half-hour age budget. The
  strict rolling-window maximum is eight per day. Only a known battery-backed
  RTC may suppress those checkpoints. Keep every restart and power-off path on the
  shared flush helper and never add per-tick NVS writes.
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
  The service mutex covers each task pass and settings reset. Before waiting
  for it, settings transitions must set the enable gate false and send the
  private front-of-queue wake event so an idle UART receive releases the mutex.
- `FurbleIR`: RMT on `RMT_CLK_SRC_DEFAULT` (APB) is SAFE under DFS because the
  IDF rmt driver holds `ESP_PM_APB_FREQ_MAX` between `rmt_enable` and
  `rmt_disable`. Do NOT switch to `RMT_CLK_SRC_XTAL`, it does not exist on
  plain ESP32. Transmission runs on the dedicated ir task, never under the
  Control mutex.
- `FurbleFeedback`: optional sound, LED and vibration event outputs. DFS-safe
  by construction: all sound paths go through I2S and the IDF i2s driver holds
  `ESP_PM_APB_FREQ_MAX` while the channel is enabled, so no extra pm lock is
  needed. The LED is plain GPIO, DFS-immune, and `PM_SLP_DISABLE_GPIO` is
  unset so pads hold through light sleep. Vibration is I2C, whose divider is
  recomputed per transaction. The output selection is frozen at boot (it
  decides `cfg.internal_spk`), only the event mask and volume reload live.
- `FurbleGPS` demultiplexes NMEA and CASIC binary frames. It sends at most one
  acknowledged configuration command at a time and keeps the fallback path.
- Settings switch tables in `FurbleConsole` and `FurbleCompanion` must include
  every new `Settings::type_t` case. The host
  `settings_nvs_roundtrip_test` keeps an exhaustive storage-kind mirror so a
  new enum value fails the host build until its table and switch handling are
  updated.
- `CompanionService::m_Mutex` serializes the status notification cache and all
  trigger rate and held-command state, including timer and disconnect release.
  Keep transport-triggered callbacks on that ownership rule, but never hold it
  across a transport virtual call because production GATT takes its own mutex.
  A zero-duration timed trigger releases inline because `handleTrigger()`
  already owns the service mutex.
- `FurbleSD`: SD card service for the two Core boards. A dedicated writer task
  owns the card mount and all SD I/O. Every other task (LVGL, GPS, NimBLE)
  interacts only through `SD::request()` / `SD::logPoint()` and the atomic
  state accessors. Never mount, unmount or touch files under `/sd` from
  another task.
- `FurbleGPX`: GPX 1.1 track writer. Pure file writer with no SD or settings
  knowledge; every method runs on the SD writer task.
- `FurbleUI*`: LVGL UI. Respect the changed-check rule for periodic setters.
  Scan advertisements are copied by `Scan` and drained on this task before
  `CameraList` or LVGL is touched; keep scan start unlocked around controller
  calls so the watchdog and callback handoff remain responsive.
  Aggregate UI context assignments must initialize every field explicitly. Keep
  the connect timer paused until its context and widgets are ready.
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
  `ControlMode::PRESET` remaps the three keys to minus, confirm and plus while
  the bulb Duration page uses the exposure preset picker.
  Fonts come from `fontForTextSize` and `fontForIconMenu` in FurbleUI.cpp:
  never hardcode a Montserrat font in a widget, and Large may only grow the
  icon menu font, never below its montserrat 16 default.
- `FurbleUIAudit`: layout audit walker (`FURBLE_SIM`/`FURBLE_CONSOLE` only),
  reached via `ui audit` on the console. Reports clipped labels and label
  overlaps as JSON Lines, see tools/ui-audit.md.
  The display off state machine lives here: `processInactivity` dims or sleeps
  the panel, sleep/wake pairs the APB lock with a 120 ms SLPIN/SLPOUT dwell,
  and a wake press is swallowed until every input source reports released.
  Only a pending low battery power-off countdown holds the panel awake
  through that state machine, the plain warning rides the normal dim/sleep
  path, and the LVGL idle clock is never touched. `wakeDisplay` does not count
  as activity, only a real input press triggers `lv_display_trigger_activity`.
  Modal boxes that steal focus must capture and restore the previous focus,
  the group is flat.
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
- Under `FURBLE_SIM`, battery policy is exercised through the deterministic
  `Furble::Sim::battery_reading_t` seam. Keep `Platform::readBattery()` as the
  production boundary; scenario seeds/actions may replace its sample and
  `Platform::powerOff()` records the request so policy ordering can be asserted
  without terminating the host process.
- New source files must be added to `src/CMakeLists.txt` (alphabetical, before
  main.cpp). Component deps go in `idf_component_register` there.
