# 86 - SDL simulator bug hunt

Status: findings report. This PR is docs plus reusable sim tooling. It fixes no
product bugs. Each product fix is a separate follow up PR.

## Goal

Drive the furble UI through the SDL simulator to find UI layout and navigation
bugs and logic/state bugs, reproduce the known hardware issues where the sim can
reach them, and produce a triaged report. Based on `feat/sim-e2e` (PR #111),
which carries the richest sim driver (assert/action/query steps,
`UI::simScenarioAction`, `UI::simQueryState`, `connectShouldFail`).

## How to reproduce

Build the three panel classes, then run the scenarios headlessly:

```
pio run -e m5stick-s3                 # populate .pio/libdeps once
sh sim/build.sh                       # 135x240 (M5StickS3), sim/build/furble-sim
FURBLE_SIM_BUILD_DIR="$PWD/sim/build-c" \
  FURBLE_SIM_FURBLE_BOARD=FURBLE_M5STICKC \
  FURBLE_SIM_M5GFX_BOARD=board_M5StickC sh sim/build.sh      # 80x160
FURBLE_SIM_BUILD_DIR="$PWD/sim/build-corex" \
  FURBLE_SIM_FURBLE_BOARD=FURBLE_M5COREX \
  FURBLE_SIM_M5GFX_BOARD=board_M5Stack sh sim/build.sh       # 320x240

SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  ./sim/build/furble-sim --script sim/scenarios/bughunt/<name>.txt
```

Screenshots render headlessly with the default driver (no dummy):
`./sim/build-c/furble-sim --script <script> --out <dir>`.

## Findings, most severe first

Severity ladder: crash > broken-navigation > state-desync > visual-overflow >
minor.

### F1 - Intervalometer keeps firing after you navigate away (NEW)

- Severity: broken-navigation / state-desync.
- Repro (sim, confirmed): `sim/scenarios/bughunt/interval-back-trap.txt`. Connect
  the FauxNY camera, start an 8 frame intervalometer, navigate back to the main
  menu. `camera.shutter_presses` climbs from 1 to 8 while the UI sits on the main
  menu, with no run page visible and no stop control reachable.
- Affects: all resolutions (logic bug, board independent).
- Suspected cause: only the Stop button pauses `m_IntervalTimer` /
  `m_IntervalPageRefresh` (`src/FurbleUI.cpp` ~3180). The run page's back branch
  in the main menu `LV_EVENT_VALUE_CHANGED` handler contradicts its own comment:
  `src/FurbleUI.cpp:1598-1600` says "disable 'Back' when intervalometer is
  running" but calls `lv_obj_remove_state(back, LV_STATE_DISABLED)`, which
  enables back. Navigating away never stops the run.
- Known/new: NEW.

### F2 - Companion pairing modal buttons are not in the encoder group (maps to #32)

- Severity: broken-navigation (input trap).
- Repro (sim): partial. The `modal` / `focus` queries and `companion-accept` /
  `companion-reject` actions added here can drive and observe the modal, but the
  modal only appears when a companion peer raises a pending pairing over the rig
  transport, which needs a TCP peer the headless harness does not yet start. See
  the harness gap note below.
- Static evidence (strong): `src/FurbleUI.cpp:551-571`. `Accept` and `Reject` are
  created with `lv_msgbox_add_footer_button` but never `addToInputGroup(m_Group,
  ...)` the way every other interactive widget is. `lv_group_focus_obj(accept)`
  at line 571 is then a no-op because `accept` has no group, so on an encoder
  only device (StickC/S3) the buttons cannot be focused or clicked, and the
  background menu keeps the group focus under the modal. On close only
  `m_CompanionPairingPrevFocus` is restored (lines 515-520); if that object was
  rebuilt meanwhile the group is left with no focus. A duplicate-dialog window
  also exists: the pointer is cleared at line 512 while `lv_msgbox_close_async`
  only schedules deletion, and the guard at 540-542 checks the already-nulled
  pointer.
- Affects: StickC / StickS3 (encoder only). Touch boards differ.
- Known/new: reproduces the mechanism behind known task #32.

### F3 - Connect timer parks on STATE_ACTIVE, so a mid-session link drop is never observed (maps to #39 and #40)

- Severity: state-desync.
- Repro (sim): not reachable. The sim can force a connect that never links
  (`connect_fail`), but has no hook to drop an already-active link, so the
  post-active path cannot be driven yet. See harness gap note.
- Static evidence (strong): `m_ConnectTimer` is paused on reaching
  `STATE_ACTIVE` (`src/FurbleUI.cpp` ~1973) and only ever resumed by a fresh
  `doConnect` (~2263). After the first connect the handler stops running, so if
  Control leaves `STATE_ACTIVE` (silent drop / reconnect), the UI never re-shows
  the progress box (#39) and the connected page with live shutter controls
  lingers with no connection indicator in the header (#40). The header has no
  connection icon (only reconnect/gps/battery, lines 303-306).
- Harness note: the existing `ui.connected` query gates on
  `getState()==STATE_ACTIVE` (`src/FurbleUI.cpp:1804-1813`), which is stricter
  than the human-visible UI, so a lingering connected page would NOT be caught by
  that query. A laxer "page shows connected controls regardless of Control state"
  query is needed to test #40 once the drop hook exists.
- Known/new: root cause behind known tasks #39 and #40.

### F4 - `doConnect` stacks a new Cancel callback on every attempt (NEW)

- Severity: state-desync.
- Repro (sim): not directly asserted; static plus flow reasoning.
- Static evidence: the Cancel button is created once (`src/FurbleUI.cpp` ~2520)
  but `doConnect` adds another `LV_EVENT_CLICKED` -> `doDisconnect` callback on it
  every call (~2258), and `doConnect` runs on button, autoconnect (~1550) and
  console (~2139). After N connect cycles one Cancel click runs `doDisconnect` N
  times, each re-running teardown and re-navigating.
- Known/new: NEW.

### F5 - Intervalometer Stop does not reset its state (NEW)

- Severity: state-desync (telemetry / console).
- Static evidence: the Stop handler (`src/FurbleUI.cpp` ~3173-3190) pauses the
  timers and sends a shutter release but never resets `interval->m_State` or the
  `m_IntervalometerState` atomic, so `getIntervalometerState()` keeps reporting
  the last running phase after a stop until the next Start.
- Known/new: NEW.

### F6 - Remote shutter page (non-touch) leaves Back hidden and disabled (NEW, low)

- Severity: minor / potential trap.
- Static evidence: `src/FurbleUI.cpp:1582-1589`. Arriving from the connected page
  (which disables+hides Back), the non-touch branch only adds HIDDEN and never
  clears DISABLED, so escape depends entirely on the hardware left long-press
  (`navigateBack`). Touch path re-enables correctly.
- Known/new: NEW (adjacent to #34 dead-end class).

### F7 - Narrow-panel layout overflow, worst at 80x160 (visual)

- Severity: visual-overflow / minor.
- Repro (sim, confirmed): `sim/scenarios/bughunt/overflow-sweep.txt` with the new
  `ui.overflow` query. At 80x160 the main menu itself overflows (the home screen
  needs scrolling) along with settings, features, gps, timer, bluetooth, about,
  and every diagnostics sub-page. At 135x240 and 320x240 fewer pages overflow
  (features, gps, bluetooth, about, device_info, power_state still do). Timer
  setting rows clip their values at the right edge on 80x160 (Count / Delay /
  Shutter values run off screen). Screenshots reproducible via the capture path
  above.
- Affects: 80x160 worst; some pages on all three.
- Known/new: NEW (layout matrix observation). Scrolling itself is by design; the
  home-menu overflow and value clipping on 80x160 are the notable parts.

## Reproduced-known vs new

- Reproduced in the sim (runnable assertion): F1 (new). F7 layout overflow
  (new, measured across all three panels).
- Known bugs matched by strong static evidence, sim repro blocked by a harness
  gap: F2 (#32), F3 (#39, #40).
- New static findings: F4, F5, F6.
- Count: 1 known-class fully reproduced pathway is not applicable; 2 known tasks
  (#32, #39/#40) confirmed by static analysis with tooling staged to drive them;
  4 NEW findings (F1, F4, F5, F6) plus the F7 layout matrix.

## Known bugs NOT reproduced (triage)

- #34 (BLE settings and IMU-live diagnostics have no back): NOT reproduced.
  `sim/scenarios/bughunt/back-nav-diagnostics.txt` navigates into BLE, Power
  state and Device info via real button clicks and every one returns a `visible`
  header back button that pops to Diagnostics (all asserts pass). The BLE page
  therefore has a working back on this branch. The IMU-live diagnostics page and
  a Level / spirit-level page do not exist on `feat/sim-e2e`, so that half of #34
  is out of scope here.
- #39 connect-failure path: the progress box is correctly hidden after a failed
  connect (`sim/scenarios/bughunt/connect-fail-progress.txt` passes,
  `ui.connect_box hidden`). #39 is only plausible via a mid-session drop (F3),
  which the sim cannot drive yet.
- Header title overflow seen in screenshots is a SIM ARTIFACT, not a product bug:
  the sim defines `FURBLE_RIG`, so `m_Title` is the long warning "RIG BUILD, NO
  BLE, NO ENCRYPTION" (`include/FurbleUI.h:302`). Release firmware uses a short
  title (`FURBLE_VERSION` / `furble`), so the header does not overflow on device.

## Harness additions (reusable test tooling in this PR)

All additions are `FURBLE_SIM` gated (or board gated) so release firmware is
byte-unaffected. `pio run -e m5stick-s3` and all three sim boards build; e2e
stays green (4/4).

- Multi-board sim build unblocked. `Settings::WATCHDOG` is `FURBLE_M5STICKS3`
  only, but the sim referenced it unconditionally in `sim/driver.cpp` and in the
  `FURBLE_SIM` toggle map in `src/FurbleUI.cpp`, so the sim only built for
  StickS3. Both references are now board gated, so the 80x160 and 320x240 panels
  build. This is what enables the F7 layout matrix.
- New `UI::simQueryState` observers (all `ui.` prefixed, forwarded by the driver
  automatically): `back` (header back button hidden/disabled/visible),
  `modal` (companion dialog open/closed), `focus` (encoder group has a valid
  focused object none/stale/ok), `focus_on_page`, `overflow` (page content
  exceeds viewport via scroll extent). The `page` query now recognizes the deep
  pages (bluetooth, about, diagnostics, device_info, power_state, ble, bulb,
  timer, nmea, gps_data, ...).
- New `UI::simScenarioAction` drivers: `nav <page>` clicks the real menu button so
  LVGL records history and the header back button pops correctly (reaches BLE /
  Power state / Device info); `companion-accept` / `companion-reject` click the
  real modal footer buttons.
- New scenarios under `sim/scenarios/bughunt/`: `back-nav-diagnostics.txt`,
  `connect-fail-progress.txt`, `interval-back-trap.txt`, `overflow-sweep.txt`.
  These are kept out of `sim/scenarios/e2e/` so `run-e2e.sh` stays green; several
  are written to demonstrate current behavior and become regression tests once
  the product fixes land.

## Harness gaps found (candidates for follow up tooling)

- No hook to drop an already-active link. Needed to drive F3 (#39, #40). Suggest a
  `connect-then-drop` mode on the FauxNY camera plus a laxer connected-display
  query.
- The companion pairing modal needs a rig TCP peer to raise a pending pairing.
  The `modal` / `focus` / `companion-accept` tooling is staged; a headless peer
  that calls `beginPairing` would let F2 (#32) run as an assertion.

If the coordinator wants, the multi-board build unblock (the WATCHDOG gate) is
self-contained and could be split into its own small PR ahead of the rest.
