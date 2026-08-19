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
- `FurbleGPS`: TinyGPSPlus over UART2. Mind the UART clock source trap.
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
- `FurbleUI*`: LVGL UI. Respect the changed-check rule for periodic setters.
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
- `FurbleBtDebug`: console-only active BLE onboarding. Keep the raw explorer
  independent of `Camera`, NVS, and `CameraList`; pairing input is console
  passthrough and passive third-party sniffing is not supported by NimBLE.
- The GPS Data and Raw NMEA pages show TinyGPSPlus speed in km/h. Format
  user-visible coordinates to five decimal places without narrowing the double.
- New source files must be added to `src/CMakeLists.txt` (alphabetical, before
  main.cpp). Component deps go in `idf_component_register` there.
