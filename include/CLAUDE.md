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
- `FurbleSettings.h` assigns the IMU enable switch wire id 46 and the legend
  placement wire id 65. A wire id is frozen the moment a branch generates its
  golden fixtures, so pick the next free one by surveying every open PR's
  `tests/protocol/golden/settings`, not by taking the next number after master.
  Claimed at the time of writing: 1 to 41 and 43, 44, 46 on master; 42 by the
  time policy work; 45 and 47 by the companion password; 48 by GPS motion;
  49 and 50 by dead reckoning; 51 to 61 by provisioning, MQTT and the web UI;
  62 by the cameras characteristic; 63 and 64 by IMU gestures. 65 is the legend
  and 66 is the next free.
- `FurbleSettings.h` widened `MULTISELECT_NAME_MAX` from 16 to 32, which changed
  the stored record size. `Settings::load<multiselect_t>()` and the SD settings
  importer both read the old layout through `multiselect_legacy_t` and widen it.
  Changing that constant again means adding another legacy layout, not dropping
  every saved selection.
- `FurbleUI.h` exposes IMU diagnostics and spirit-level state only when the
  persisted IMU capability is enabled; simulator seams must model the same
  `M5.Imu` read boundary rather than adding widget-only state.
- `FurbleUI.h` holds the physical-button layout's geometry contract. Where the
  three button legends sit is the `LEGEND` setting, read through
  `UI::legendPlacement()`: Buttons, the default, keeps the Right one at
  `m_RightYOffset` down the right edge, and Bottom puts all three in the
  reserved navbar band. `level_t::navRightYOffset` carries the same offset for
  the level page, which re-anchors all three on rotation, so a new anchor has to
  change `UI::begin` and `applyLevelRotation` together. `UI::legendReserve()` is
  the room a page keeps clear when a legend is drawn over it, and
  `UI::legendSelectable()` is the one board list both the setting page and the
  placement obey.
- Under `FURBLE_SIM`, `FurbleUI.h` exposes the typed `simScenarioAction` API
  using `Sim::scenario_action_t` from `sim/scenario_action.h`. Calls return
  `APPLIED`, `VALID_NO_EFFECT`, `UNAVAILABLE`, or `INVALID`; malformed direct
  actions fail closed. Keep this overload and result enum simulator-only so
  firmware builds retain the production header surface.
