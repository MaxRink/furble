# 153 - Spirit level entry on the main menu

Numbering note: 149, 150 and 151 are claimed by PRs #243, #244 and #245, and
152 is reserved by the parallel focus-outline PR, so this plan takes 153.

## Motivation

User request: "Tools like the bubble level ideally should be accessible even
when no camera is connected."

The spirit level needs no camera. It reads the IMU, draws a bubble, and never
touches the BLE link. Yet its only button lives on the Connected page, so the
tool is unreachable until a camera connects. The gate is purely structural,
not a state check: the simulator could already jump to the level page with no
connection (action nav level) and the page works fine there.

## Design

Add a second button for the already-built Level page to the main menu:

- addMainMenu() creates the button right after addConnectedMenu() returns,
  because that call builds the Level page the button loads. The button uses
  the same icon_adjust icon and m_LevelStr label, and shares the page through
  lv_menu_set_load_page_event, following the m_IRConnectedButton precedent of
  two buttons onto one page.
- On the M5COREX grid the button takes the free cell {1, 1}. The other boards
  use a flex column, where the entry lands between Settings and Power off.
- The new button is a static member m_LevelMainButton, not a m_Menu entry,
  because m_Menu keys pages and the Level page already has its owner.
- IMU gating: showIMUWidgets() now also hides and shows m_LevelMainButton
  (null-guarded), and the main menu open branch calls
  showIMUWidgets(imuEnabledForUI()), so boards without a usable IMU never
  show the entry. The button is also created hidden when the gate is off,
  matching addLevelMenu().
- No changes to the Level page itself. The page dispatch in addMainMenu()
  already re-enables Back on the Level page regardless of entry path, and the
  level timer resume and pause key off the page, not the entry path.
- Home row padding trim on the 135x240 boards: with the Level entry the home
  menu carries six visible rows and the CI page-matrix overflow guard caught
  a one pixel overflow at the default row padding of 6. The home rows trim
  to 5, following the per-page row padding prior art in addMenuItem(), so
  the page fits with headroom at Normal and Large text. The 80x160 and
  320x240 layouts fit unchanged. With the optional IR row enabled the home
  menu scrolls, the same as any over-tall list.

## Simulator surface

- New query ui.level_main_button_visible reports the entry's hidden flag.
- New action nav level_main clicks the main menu button directly, honouring
  the hidden gate, because the button has no m_Menu entry for the generic nav
  path.
- New scenario sim/scenarios/e2e/level-main-menu.txt: with a saved but never
  connected camera, the entry is visible on the home page, opens the level
  page without overflow, the live filter moves the bubble on injected IMU
  samples, and the header back button returns to the main menu.
- sim/scenarios/e2e/imu-gating.txt and imu-gating-sensor-off.txt additionally
  assert the main menu entry is hidden when the IMU is disabled or the sensor
  is absent, and that a nav onto the hidden button is a no-op.

## Verification

- Full end-to-end simulator suite headless: 73 of 73 scenarios pass on the
  default M5StickS3 panel after rebasing onto the current master.
- CI-equivalent panel matrix run locally on all three modeled panels
  (135x240, 80x160, 320x240): page-matrix, overflow-sweep,
  text-size-overflow large and small, level-overflow, imu-diagnostics,
  redraw-steady, the StickC text size gates, connstate-page-sweep and
  statusbar-stability all pass, 24 of 24 runs. Before the padding trim the
  page-matrix home assert failed on 135x240 with a one pixel overflow;
  after it the home page reports scroll_bottom 0 on every panel.
- Layout checked by screenshot on the 80x160 M5StickC panel (flex column, no
  icons), the 135x240 StickC Plus and StickS3 panel, and the 320x240 M5COREX
  panel. Every home menu fits without scrolling, and the main to level to
  back walk passes on every panel.
- Full host suite green: 82 of 82 ctest tests on the rebased master.
- m5stick-s3-debug and m5stick-c firmware builds pass and fit flash (sizes
  in the PR).

## Hardware boundary

Not yet hardware verified. The on-device look of the new home menu row on the
M5StickS3 is owed before merge.
