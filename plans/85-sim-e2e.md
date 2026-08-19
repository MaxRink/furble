# 85 - simulator end-to-end scenarios

## Goal

Run scripted end-to-end scenarios against the SDL simulator in CI, headlessly,
and assert furble behaviour by app state rather than by pixels. This drives the
real `FurbleUI` and `Control` against the fake camera through a full boot,
connect, shutter, disconnect and settings flow, and fails the build when an
asserted outcome regresses.

## Motivation

The sim already renders every UI page for the ui-screenshots job, but that job
only captures images for human review. It asserts nothing, and its pixel
readback (`M5.Display.readRectRGB`) hits a nondeterministic software-GL crash on
hosted runners, so it cannot gate merges.

This job asserts control-flow outcomes instead: the Control state machine, the
current UI page, the connect progress box, the connected flag, the shutter
command count and persisted settings. None of that needs a pixel, so the job
avoids the GL readback path entirely and stays reliable. It complements the
plan 83 host camera tests, which unit-test the real `Camera` lifecycle against
MockNimBLE. This job is the layer above: the real UI and Control task wired to
the sim's fake camera, exercised end to end.

## Input injection

Scenarios are text scripts (`sim/scenarios/e2e/*.txt`) read by the existing sim
driver (`sim/driver.cpp`). The driver advances a controllable virtual clock, so
runs are deterministic with no wall-clock or randomness: `wait N` accumulates
virtual milliseconds that only pass as the UI task calls `vTaskDelay`.

Inputs are injected through the driver's `action` step, which calls
`UI::simScenarioAction`. That method runs the same UI and Control code paths a
button press or console command triggers, on the UI task, under LVGL's single
thread. New actions added for these scenarios:

- `connect`: marks the FauxNY camera active and calls the real `doConnect`, the
  same entry the Scan and Connect buttons use. The connect timer then advances
  the state machine and reveals the connected page.
- `disconnect`: calls the real `doDisconnect`, mirroring the disconnect button.
- `shutter`: sends `LV_EVENT_PRESSED` and `LV_EVENT_RELEASED` to the shutter
  button so the real `handleShutter` fires the shutter command.
- `toggle <setting>`: flips a setting through its real switch widget and sends
  `LV_EVENT_VALUE_CHANGED`, so the switch's own callback persists the value.

The driver's raw `key` step injects GPIO button presses, but that path needs a
display-backed SDL event pump (the same server the flaky screenshots job runs
under Xvfb). The `action` layer drives the identical UI and Control code without
a display, so the job runs under the SDL dummy video driver with no Xvfb and is
verifiable locally on macOS.

Assertions use a new `assert <key> <expected>` step. Keys resolve to live state:
`ui.page`, `ui.connected`, `ui.connect_box` (from `UI::simQueryState`, read on
the UI task), `control.state`, `control.connected`, `control.targets`,
`camera.shutter_presses`, `camera.shutter_releases`, and `setting.<name>`. A
mismatch prints the expected and actual values and exits non-zero. A `print`
step reports a value without asserting, for debugging.

## Scenarios

`sim/scripts/run-e2e.sh` runs every scenario and fails if any assertion fails.

1. `connect-flow`: boot, drive the connect, assert Control passes through
   `connecting` with the progress box visible, then reaches `active` with one
   connected target and the UI showing connected.
2. `false-connected-guard`: the e2e mirror of the stale-connected hardware bug
   (BUG C) at the UI layer. A `connect_fail` camera never establishes a link.
   Assert the UI never shows connected and Control never reports active; the
   flow ends idle with zero connected targets.
3. `shutter-command`: from connected, fire the shutter and assert the shutter
   press and release reached the fake camera.
4. `settings-persist`: open Settings, navigate to Features, toggle a setting
   through its real switch, and assert the value persisted and navigation
   returned to the main menu.

## Sim hooks

- `Camera::connect` honours `Sim::connectShouldFail()` (seeded by
  `connect_fail`) and returns failure without connecting, so `FurbleControlSim`
  transitions to `STATE_CONNECT_FAILED` instead of `STATE_ACTIVE`. This models a
  camera that never links.
- `CameraSim` counts shutter presses and releases, exposed through
  `Sim::cameraShutterPresses` / `cameraShutterReleases`.
- `UI::simQueryState` reports the current page, the connect progress box
  visibility and the composite connected signal (connected page shown, progress
  box gone, Control active).

## CI

A new `sim-e2e` job (`.github/workflows/sim-e2e.yml`) builds the sim the same
way ui-screenshots does (pinned shallow dependency clones, `sim/build.sh`) and
runs `sim/scripts/run-e2e.sh` under the SDL dummy driver. It is separate from
the flaky ui-screenshots job and needs no Xvfb, because it asserts state and
never reads back pixels.

No new source files were added, so the `sim/build.sh` and `sim/CMakeLists.txt`
source lists are unchanged. The scenarios and the shim changes live in files the
existing `sim/*.cpp` glob already compiles.

## Teeth

Injecting the BUG C regression into `FurbleControlSim` (always set
`STATE_ACTIVE`, ignoring the connect result) fails `false-connected-guard`
(`control.state expected 'idle' got 'active'`) while `connect-flow` still
passes, confirming the guard catches the stale-connected regression without
coupling to the connect path. All four scenarios pass on the correct build and
are deterministic across repeated runs.
