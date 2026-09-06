# include/

Public headers for the app layer in src/, one header per module
(`Furble<Module>.h`). Everything lives in the `Furble` namespace.

- Add a header here only for src/ modules. lib/furble keeps its headers next
  to its sources.
- These headers may include lib/furble headers; the reverse is forbidden
  (see lib/furble/CLAUDE.md).
- Doxygen-style comments on public members, matching the existing files.
- `FurbleControl::setPower` is the user-selected maximum for Bluetooth transmit
  power. Adaptive runtime changes must stay at or below that cap.
- `FurbleGPS` exposes the CASIC binary test path and AID-INI injection as
  documented public methods. Keep their wire format comments next to the API.
- `FurbleSD.h` and `FurbleGPX.h`: all SD and GPX file I/O runs on the SD
  writer task. Other tasks use only the non-blocking `SD::request()` /
  `SD::logPoint()` API and the atomic state accessors, and never call GPX
  methods directly.
- `FurbleAutoOff.h` contains the pure disconnected-idle policy predicate.
  Charging blocks auto-off unless the explicit `AUTO_OFF_CHARGING` opt-in is
  enabled; callers must sample charging telemetry and keep policy ticks free of
  NVS writes.
- `FurbleSettings.h` assigns the IMU enable switch wire id 46 and the motion
  engine selector wire id 74. Wire id 45 is reserved for the companion-password
  contract and wire id 47 for the companion-password branches; neither may be
  reused. A PR claims the next free id at rebase time and regenerates its golden
  corpus. See issue #280 for the full reservation table.
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
  the IMU live page and the console probe also take.
- `FurbleUI.h` exposes IMU diagnostics and spirit-level state only when the
  persisted IMU capability is enabled; simulator seams must model the same
  `M5.Imu` read boundary rather than adding widget-only state.
- Under `FURBLE_SIM`, `FurbleUI.h` exposes the typed `simScenarioAction` API
  using `Sim::scenario_action_t` from `sim/scenario_action.h`. Calls return
  `APPLIED`, `VALID_NO_EFFECT`, `UNAVAILABLE`, or `INVALID`; malformed direct
  actions fail closed. Keep this overload and result enum simulator-only so
  firmware builds retain the production header surface.
