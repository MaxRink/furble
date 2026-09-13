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

| PR | Setting keys | Wire ids |
| --- | --- | --- |
| Master | shipped settings | 1-22, 24-35, 37-41, 43-44, 46-47, 51-55, 65-69, 72-74 |
| Master (conditional) | display, MQTT, S3 watchdog | 23 (`WATCHDOG` on `FURBLE_M5STICKS3`), 36 (`DISPLAY_MODE` without `FURBLE_NO_DISPLAY`), 56-61 (`FURBLE_MQTT`) |
| Historical claims | compatibility audit required | 42, 45 |
| #59 | `ivl_sleep`, `ivl_sleep_thr` | 75, 76 |
| #63 | no setting claim | none |
| #90 | Web UI settings | 62 |
| #265 | no setting claim | none |
| #273 | no setting claim | none |

Ids 48 through 64 are claimed by other open PRs. Take the next free id below
the reservations only after checking every open PR head.
IDs 42 and 45 have historical claims in older branches. Keep those claims
reserved and do not allocate or reuse either id without a compatibility audit.
Recheck every open feature PR head immediately before a rebase or merge. The
five open feature PRs listed above are the settings-relevant reservation rows;
documentation-only PRs do not change this table. No new id is free merely
because a branch does not currently touch settings.

Audit snapshot (2026-09-13): the exact MaxRink/furble master reviewed was
`16520a9f97b2db44449b057177965b1d94d33c19`. Remote PR #273 was observed at
`188a7e9a45d17c552897d020a296dda70f358aee`, an older-base view. The integrated
PR #273 candidate reviewed here is `b4ae1b299314039e865d6610177dbf4dceaca544`;
it carries the current settings rows and protocol schema, including
`AUTO_OFF_CHARGING` (43) and `LEGEND` (65). Use that integrated candidate for
the reservation audit rather than treating omissions in the remote diff as
missing source or schema.
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
- `FurbleUI.h` holds the physical-button layout's geometry contract. Where the
  three button legends sit is the `LEGEND` setting, read through
  `UI::legendPlacement()`: Buttons, the default, keeps the Right one at
  `m_RightYOffset` down the right edge, and Bottom puts all three in the
  reserved navbar band. `level_t::navRightYOffset` carries the same offset for
  the level page, which re-anchors all three on rotation, so a new anchor has to
  change `UI::begin` and `applyLevelRotation` together.
  `UI::floatingIndicatorReserve()` reports the room rows keep clear and
  `reserveLegendColumns()` applies it consistently to every row that can scroll
  through the floating legend. `UI::legendSelectable()` is the one board list
  both the setting page and the placement obey.
- Under `FURBLE_SIM`, `FurbleUI.h` exposes the typed `simScenarioAction` API
  using `Sim::scenario_action_t` from `sim/scenario_action.h`. Calls return
  `APPLIED`, `VALID_NO_EFFECT`, `UNAVAILABLE`, or `INVALID`; malformed direct
  actions fail closed. Keep this overload and result enum simulator-only so
  firmware builds retain the production header surface.
- FurbleWiFi.h exposes station provisioning and NTP status for the app layer.
