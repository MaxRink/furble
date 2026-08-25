# 109 sim coverage expansion

## Motivation

The UI walkthrough (docs/ui-walkthrough.md) documented several pages in words
only because the SDL simulator did not model them: the boot splash, the Scan,
Connect and Delete list pages, and the Infrared, Feedback and Storage submenus.
The screenshots also covered only the M5StickS3 in the default and dark themes,
and captured the touch layout by default, so the always-on physical button
indicators the non-touch Stick boards show were absent. This plan makes those
pages renderable and captures the full boards x themes matrix.

## Scope

Sim tooling and docs only. No shipping firmware behavior changes. Every new seam
is guarded so a firmware build compiles identical code, and every capability the
sim reports is gated behind a sim-only environment variable that does not touch
on-device hardware detection.

## Implementation

- Capability presence, sim only, env gated (`sim/shim/FurbleSimCaps.h`):
  - `FURBLE_SIM_IR` makes `IR::isSupported()` report an IR LED, so the Infrared
    submenu renders.
  - `FURBLE_SIM_FEEDBACK` makes `Feedback` report the full output set, so the
    Feedback submenu renders.
  - `FURBLE_SIM_SD` makes `SD` report a mounted card, so the Storage submenu
    renders.
  - Off by default, so the existing key-count menu routes keep their positions.
- Boot splash capture: `sim/main.cpp` snapshots the panel mid progress bar when
  `FURBLE_SIM_CAPTURE_SPLASH` names an output PNG, before the LVGL UI starts.
- List pages: a `saved_camera` seed adds a saved but inactive camera so the
  Connect and Delete entries enable and their lists render; the fake scan
  surfaces the FauxNY camera on the Scan page. New `nav` targets reach `scan`,
  `connect`, `delete`, `infrared`, `feedback` and `storage`.
- Companion pairing: the non-rig `rigInjectPendingPairing` stub now holds the
  injected pending pairing, so the pairing dialog renders without a rig peer.
- Non-touch layout per board: `FURBLE_SIM_NO_TOUCH` selects the physical-button
  layout so narrow boards render the floating indicators, matching the device.
- Non-rig header: `FURBLE_SIM_RIG=0` in `sim/build.sh` drops the rig build so the
  shipped one-line title renders instead of the rig placeholder.
- Capture harness `sim/scripts/docs-capture.sh` rebuilds the sim once per panel
  class and drives the scenarios for each of the three themes (Default, Dark,
  Mono Furble) with every optional feature enabled. Output layout:
  `docs/img/<board>/<theme>/<page>.png` plus the flat default and dark sets.
- e2e coverage: `list-pages.txt` and `capability-submenus.txt`; `run-e2e.sh`
  reports the optional capabilities present for the suite.
- Page/layout matrix: `bughunt/page-matrix.txt` now reaches every modeled page,
  including optional capability pages and connected-session run states. It
  asserts stable `ui.page` identities, keeps compact pages must-fit, and
  verifies the bottom and top scroll extents for every intentional-scroll
  route. The same scenario is run on all three panel classes in CI.
- Overflow audit: `bughunt/overflow-sweep.txt` now visits those same reachable
  page classes for layout diagnostics. Compact pages assert `ui.overflow no`,
  while intentional-scroll pages print their observed state. CI runs the audit
  on the 80x160 M5StickC, 135x240 M5StickS3, and 320x240 M5Stack Core with
  optional capability pages enabled.

## Deviations

- The physical button indicators were not a firmware gap. The old screenshots
  were captured in touch mode, which has no physical-button nav bar. The
  non-touch captures restore all three indicators (up, select on the bottom bar,
  focus on the right edge at the BtnB position).
- The IMU spirit level previews come from the unmerged PR #28 branch, not this
  branch. They are labelled as pending and land when #28 merges. The rotated
  landscape level view has a bench-only DMA issue and is not captured.

## Verification

- Sim builds clean for all three panel classes, rig and non-rig.
- `sim/scripts/run-e2e.sh` passes, including the two new scenarios.
- clang-format clean, no em-dashes, 2-space indent.
- The page matrix passes on the 80x160 M5StickC, 135x240 M5StickS3 and 320x240
  M5Stack Core simulator builds with all optional capabilities enabled.
- The expanded overflow audit passes on all three panel classes. It covers 48
  page observations, including 22 compact-page fit assertions and 26
  intentional-scroll diagnostics, without adding a firmware seam.
