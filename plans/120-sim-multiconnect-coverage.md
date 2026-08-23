# 120 - Sim multiconnect coverage

Status: design only. Extends the existing multi-connect sim scenarios and the
real-Control end-to-end harness so a full multi-camera flow is proven headless.

**Codex-implementable. Startable NOW.** Both vehicles exist on fork master: the
SDL sim scenarios (`sim/scenarios/e2e/reconnect-multiconnect*.txt`) and the
real-Control harness (`tests/host/control_e2e/`, scenario
`reconnect-shutter-drop`).

## Motivation

Multi-connect is the feature most likely to regress silently: a change that
fixes one camera's reconnect can blank the trigger for the survivors, or leak a
client, or fire the shutter only on the reconnected camera. The current coverage
is good but partial:

- `sim/scenarios/e2e/reconnect-multiconnect.txt` connects two FauxNY cameras,
  drops one, and asserts the connected page survives with `ui.reconnect_count
  1/2`, then both restore. It uses the fake Control.
- `tests/host/control_e2e` runs the real `FurbleControl.cpp` and has a
  `reconnect-shutter-drop` scenario plus `client-pool-exhaustion`.

What is not yet asserted as one flow: connect 2+ cameras, use the selection UI to
target a subset, disconnect exactly ONE and prove the survivor stays live AND
keeps firing the shutter, then per-device reconnect restores it, with the sleep
lock balanced and no client leak. This doc closes that on both panels (the SDL
UI panel and the real-Control panel).

## Scope

In scope:

- **SDL sim panel** (extends `reconnect-multiconnect.txt` and
  `reconnect-multiconnect-shutter.txt`):
  - New `sim/scenarios/e2e/multiconnect-survivor-shutter.txt`: connect two
    cameras (`action connect-two`), open the Cameras/selection page, fire the
    shutter and assert BOTH `control.shutter_presses` advance; `action drop 0`;
    assert `ui.page connected`, `ui.connect_box hidden`, `control.connected 1`,
    `ui.reconnect_count 1/2`; fire the shutter again and assert the SURVIVOR
    still receives it (`control.shutter_presses` advances while one link is
    down); wait for reconnect; assert `control.connected 2` and both fire again.
  - A three-camera variant if the fake Control supports it, to prove the
    `i/n` count with n>2.
  - Selection-UI assertions: navigate the multi-connect selection
    (`plans/25-multiconnect-ui.md`) and assert the per-camera rows and the
    connected-count title.
- **Real-Control panel** (extends `tests/host/control_e2e`):
  - New scenario `multiconnect-survivor-fires`: two real `Camera` targets against
    the shared mock peer; drop one; assert the control state goes `connecting`
    with `connected == 1`, a shutter command routes to the survivor's target
    task, the dropped target reconnects, `connected == 2`, the sleep lock count
    is balanced, and no client leaks (reuse the existing `Power` lock-count and
    `liveClientCount` seams).

Out of scope:

- New product behavior. This is coverage of existing multi-connect, not a
  feature change.
- Real BLE.

## Files to change

- New scenario `.txt` files under `sim/scenarios/e2e/`.
- If the fake Control lacks a per-camera drop-and-keep-firing seam, extend
  `sim/FurbleControlSim.cpp` / `sim/driver.cpp` (host-only, no firmware change)
  with a `drop <index>` that keeps other targets live (the `action drop N`
  already exists; confirm it leaves survivors firing).
- `tests/host/control_e2e/control_e2e.cpp`: register
  `multiconnect-survivor-fires` in the `foreach(scenario ...)` list so it gets an
  individual `add_test(NAME control-e2e-multiconnect-survivor-fires ...)`.

## Settings and defaults

None. Test-only. Uses the existing `reconnect` and `fauxny` scenario seeds.

## Dependencies

- `plans/25-multiconnect-ui.md`, the multi-connect selection UI: landed.
- The real-Control harness (`tests/host/control_e2e`): landed.
- `plans/119-sim-app-coverage.md`: independent; both extend the sim but touch
  different scenarios.
- **Startable NOW.** No network, no companion, no MQTT.

## Risks

- **Survivor-keeps-firing is the load-bearing assertion.** A regression where a
  single drop blanks the trigger for all cameras is exactly the rejected
  page-takeover bug that `reconnect-multiconnect.txt` guards against; assert the
  shutter count advances WHILE one link is down, not just that the page is
  visible.
- **Client leak and sleep-lock balance** are only observable on the real-Control
  panel; do not skip that panel in favour of the UI-only one.
- **Flake from timing.** Use the virtual clock `wait` steps already used by the
  existing scenarios; do not introduce wall-clock waits.

## Codex self-verification (headless)

SDL panel:

```
python3 tools/gen_lv_conf.py sdkconfig.m5stick-s3 sim/lv_conf.h
sh sim/build.sh
sh sim/scripts/run-e2e.sh
# or a single scenario:
SDL_VIDEODRIVER=dummy sim/build/furble-sim \
  --script sim/scenarios/e2e/multiconnect-survivor-shutter.txt
```

Real-Control panel:

```
cmake -S tests/camera -B build/camera-tests -DCMAKE_BUILD_TYPE=Release
cmake --build build/camera-tests --parallel 2
ctest --test-dir build/camera-tests -R control-e2e-multiconnect-survivor-fires \
  --output-on-failure
```

Exit 0 from both proves the full multi-camera flow (connect, select, drop one,
survivor stays live and fires, per-device reconnect, no leak) headless.

## Residual (Claude / hardware) verification

- Two real cameras are not available (only one Fujifilm on hardware). Multi-real
  is FauxNY plus the mock peer by design. A single-Fujifilm-plus-FauxNY smoke on
  hardware confirms the UI renders the count; note in the PR body that true
  two-real-camera behavior is covered by the mock peer, per the repo's
  one-Fujifilm constraint.
