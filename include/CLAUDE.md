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
- `FurbleSettings.h` assigns the IMU enable switch wire id 46.

- `FurbleCompanionService.h` gates the settings and trigger writes on the
  companion password challenge in `FurbleCompanionAuth.h`. A new privileged
  characteristic must call `allowProtected()` before it acts, which is still
  owed by the reserved OTA control and data ids. `handleLocation` is
  deliberately outside that gate and needs only an encrypted link; see
  plans/116 for why.
  `Settings::loadPassword()` distinguishes unset credentials from NVS failures;
  pass its success flag to `CompanionAuth::setPassword()` so failures stay closed.
  Password mutations use `Settings::savePassword()` to check both NVS set and
  commit, including successful empty-string clears. Never acknowledge the
  generic void settings writer as proof that a credential was persisted.
  Camera pairing prompts use the same queued UI boundary: a missing handler is
  the headless NimBLE default, while a display queue failure must reject the
  unseen comparison. Camera pointers in queued requests are resolved against
  Control-owned shared pointers before use.

### Companion wire id reservations

The settings table in `src/FurbleSettings.cpp` is the source of truth for ids
already on master, which run from 0 through 46 plus 67, 68, 72 and 73. Ids that
master does not use are handed out here so open PRs cannot collide, because two
branches claiming one id produce an add/add conflict in
`tests/protocol/golden/settings/*-<id>.bin` and a silent protocol break for the
companion app. A PR claims its reserved ids at rebase time, regenerates its
golden corpus, and updates its row. See issue #280.

A merged id is frozen and never moves afterwards, because a shipped id is a
companion client contract: renumbering one and regenerating its fixtures
yields a self-consistent corpus that silently breaks every deployed client.
`tests/protocol/protocol_test.cpp` pins the ids it has been given so that
renumbering fails the build rather than passing quietly.

| PR | Setting keys | Wire ids |
| --- | --- | --- |
| #166 | `companion_pw` | 47 |
| #273 | `legend` | 65 |
| #65 | `gps_motion` | 66 |
| #139 | plan 32 phase 2 | 69, 70, 71 |
| #45 | `imu_wake`, `imu_trigger` | 72, 73 |
| #48 | `hw_motion` | 74 |

Ids 48 through 64 are claimed by other open PRs. Take the next free id below
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
- `FurbleUIGesture.h` is the accelerometer gesture state machine. `sample()` is
  the deterministic seam host tests and the simulator drive; `poll()` is the
  only method that touches hardware. Amplitude thresholds are scaled by a
  per-sensor gain and a console-settable calibration scale, because a real
  sensor in a real case never matches the paper numbers. Gesture settings
  written from the console or the companion must go through
  `UI::notifyGestureSettingsChanged()`; no other task may touch LVGL.
- Under `FURBLE_SIM`, `FurbleUI.h` exposes the typed `simScenarioAction` API
  using `Sim::scenario_action_t` from `sim/scenario_action.h`. Calls return
  `APPLIED`, `VALID_NO_EFFECT`, `UNAVAILABLE`, or `INVALID`; malformed direct
  actions fail closed. Keep this overload and result enum simulator-only so
  firmware builds retain the production header surface.
