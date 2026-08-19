# 87 - per-model physical buttons in the SDL sim

## Goal

Model each board's real physical buttons in the SDL simulator and let a
headless scenario press them by name, so the sim exercises the same per-board
button to navigation path furble runs on hardware. The touch-only sim was blind
to physical buttons, which produced the F6-class false positive: a Remote page
flagged as a navigation trap when the physical Back button already returns.

## Motivation

The sim attaches a mouse-driven touch device at init, so every scenario drove
furble as if it were a touchscreen. Non-touch boards (M5StickC, M5StickC-Plus,
M5StickS3) navigate back through a long press of the side BtnPWR, and the Cores
use front BtnA/BtnB/BtnC. None of that was reachable from a scenario, so the sim
could report a page as a dead end even though hardware escapes it fine. Finding
F6 flagged the Remote shutter page this way; hardware verification showed no real
trap and PR #113 was closed. Modelling the buttons stops the sim reporting these
false positives and lets future scenarios assert the real hardware nav path.

## Per-model button sets

The physical button set is board-gated so a scenario that presses an absent
button fails at parse time rather than passing silently:

- Sticks (M5StickC / Plus / Plus2 / S3): front `a`, top `b`, side `pwr`.
- Cores (M5Stack / Core2 / Tough): front `a`, `b`, `c`, no dedicated power
  button.

`sim/driver.cpp` selects the set with `#if defined(FURBLE_M5COREX)` and rejects
an unknown or unavailable button name when parsing a scenario. `UI::simPressButton`
resolves the silk-screen button to its LVGL input device using the same
per-board wiring as `initInputDevices`, and returns false when the board lacks
that button so the driver fails loudly.

## Input injection

Two injection paths are added:

- Interactive: `sim/FurblePlatformSim.cpp` maps keyboard letters a/b/c/p to the
  same emulated button GPIOs M5Unified's PC build reads (BtnA=39, BtnB=38,
  BtnC=37, BtnPWR=36) via `lgfx::Panel_sdl::addKeyCodeMapping`. This drives the
  physical buttons in a visible SDL window and needs the SDL event pump, so it
  only works interactively.
- Headless: the driver's new `btn <name> [hold|long]` step. Headless runs cannot
  reach the UI through the SDL panel's emulated GPIOs (that path needs a
  display-backed event pump), so the press is injected on the UI task through
  `UI::simPressButton`. A short tap sends the encoder key the read callback
  reports (left/right scroll the focus group, OK activates the focus); the
  `hold`/`long` modifier on the left button runs `navigateBack`, the universal
  back escape furble reaches through `handleLeftLongPress`, which force-shows the
  header back arrow even on pages that hide it.

## Scenario

`sim/scenarios/e2e/remote-back-button.txt` is the F6 regression. On the
M5StickS3 (a non-touch board whose Back is the side BtnPWR) it connects the
FauxNY camera, opens the Remote shutter page, holds the physical Back button and
asserts the UI returns to the Connected page. The scenario lives in
`sim/scenarios/e2e/`, which `sim/scripts/run-e2e.sh` already globs, so CI runs it
with no workflow change.

## Release impact

All added symbols are gated behind `#if defined(FURBLE_SIM)`: the
`UI::simPressButton` declaration in `include/FurbleUI.h` and its definition in
`src/FurbleUI.cpp`. `sim/driver.cpp` and `sim/FurblePlatformSim.cpp` are sim-only
files compiled solely by `sim/build.sh`. Release firmware is byte-unaffected; the
`m5stick-s3` firmware build was confirmed to still link and fit.

## Teeth

`sim/scripts/run-e2e.sh` runs all scenarios; the new `remote-back-button` passes
and is deterministic across repeated runs. Pressing a button the board lacks
(BtnC on a Stick, BtnPWR on a Core) is rejected at parse time, and a press before
the UI is ready exits non-zero, so the button model cannot silently no-op.
