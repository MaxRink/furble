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
- `FurbleSettings.h` assigns the IMU enable switch wire id 46. Wire id 45 is
  reserved for the companion-password contract and must not be reused.

### Companion wire id reservations

The settings table in `src/FurbleSettings.cpp` is the source of truth for ids
already on master, which run from 0 through 46. Ids above that are handed out
here so open PRs cannot collide, because two branches claiming one id produce an
add/add conflict in `tests/protocol/golden/settings/*-<id>.bin` and a silent
protocol break for the companion app. A PR claims its reserved ids at rebase
time, regenerates its golden corpus, and updates its row. See issue #280.

| PR | Setting keys | Wire ids |
| --- | --- | --- |
| #273 | `legend` | 65 |
| #65 | `gps_motion` | 66 |
| #47 | `gps_hold`, `gps_extrap` | 67, 68 |
| #139 | plan 32 phase 2 | 69, 70, 71 |
| #45 | `imu_wake`, `imu_trigger` | 72, 73 |
| #48 | `hw_motion` | 74 |

Ids 47 through 64 are claimed by other open PRs. Take the next free id below
the reservations only after checking every open PR head.
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
- `FurbleUI.h` exposes IMU diagnostics and spirit-level state only when the
  persisted IMU capability is enabled; simulator seams must model the same
  `M5.Imu` read boundary rather than adding widget-only state.
- Under `FURBLE_SIM`, `FurbleUI.h` exposes the typed `simScenarioAction` API
  using `Sim::scenario_action_t` from `sim/scenario_action.h`. Calls return
  `APPLIED`, `VALID_NO_EFFECT`, `UNAVAILABLE`, or `INVALID`; malformed direct
  actions fail closed. Keep this overload and result enum simulator-only so
  firmware builds retain the production header surface.
