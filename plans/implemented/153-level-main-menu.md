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
  menu carries six visible rows, seven once the IR setting is on. A home row
  is the 24 px icon plus its top and bottom padding, and the text size
  setting does not change it, so the fit is fixed arithmetic against the
  215 px page. At the default row padding of 6 the sixth row overflowed by
  one pixel (the first CI page-matrix failure). At 5 six rows fit but seven
  overflow by 23 px, which is what the on-device audit of the M5StickS3 with
  its IR capability showed and what the independent review measured. The
  home rows now use padding 3: seven rows take 210 px and fit with 5 px to
  spare at Normal, Large and Small text. Only the home page is trimmed; the
  Connected page keeps its zero padding and every other page keeps 6.
- The 80x160 panel has no icons, so its home row is one text line plus the
  padding and the text size grows it. This board defaults to Small and
  clamps Large to Normal. Seven rows fit at Small at the existing padding of
  1, but at Normal they overflow by 5 px. The home page joins the Connected
  page at zero padding, where seven Normal rows fit. Other pages keep 1.
- The 320x240 grid fits seven rows at every text size unchanged. The IR row
  is the last row a user can add, so seven is the ceiling these guard.

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
  is absent, and that a nav onto the hidden button reports unavailable.
  imu-gating.txt and level-main-menu.txt also disable the IMU at runtime and
  return home, proving the main menu open branch refreshes the gate.
- New scenarios sim/scenarios/e2e/home-seven-rows.txt and
  home-seven-rows-large.txt: IR is not a seed, it only flips through the
  switch on the Infrared page, so no earlier scenario ever rendered the full
  seven-row home menu. These toggle IR the way a user does, return home and
  assert no scrollable extent at Normal and Large text, then open Level from
  the fuller menu and come back. Certified on all three panels in
  sim/scenarios/manifest.json.
- bughunt/page-matrix.txt walks nav level_main and back on all three panels.
  bughunt/text-size-overflow-small.txt seeds the IMU so its home fit assert
  includes the Level row. text-size-overflow-large.txt deliberately does
  not: seeding the IMU also adds the Level row to the Connected page, and on
  80x160 at Normal (which Large clamps to) the Connected page then scrolls
  by 13 px. That is a pre-existing gap from the Connected page Level entry,
  not from this plan, and the scenario's Connected page assert would fail
  for it. The Level row's home fit at Large is guarded on every panel by
  home-seven-rows-large.txt instead. Follow-up: fit or intentionally scroll
  the 80x160 Connected page with the IMU on at Normal.

## Verification

- Padding arithmetic measured in the 135x240 simulator with the IR setting
  toggled on (seven home rows): padding 5 leaves 23 px below the viewport
  (scroll_bottom 23, overflow yes) at Normal, Large and Small text; padding 3
  leaves 0 px (overflow no) at all three sizes. On 80x160 seven rows at
  padding 1 report overflow no at the Small default and scroll_bottom 5 at
  Normal (the Large seed clamps to it); at zero padding both report
  overflow no. The 320x240 grid reports overflow no
  at every size before and after.
- Full end-to-end simulator suite headless on the default 135x240 panel:
  76 of 76 scenarios pass, including home-seven-rows, home-seven-rows-large,
  level-main-menu and the extended imu-gating pair.
- Certified alternate-board sets on 80x160 and 320x240 (e2e and bughunt
  suites from the manifest), plus page-matrix and overflow-sweep on all three
  panels with FURBLE_SIM_IR=1 FURBLE_SIM_FEEDBACK=1 FURBLE_SIM_SD=1 exported
  as CI does: all pass.
- Full host suite green: 85 of 85 ctest tests.
- tools/check_sim_scenarios.py reports the manifest complete.
- CI on the previous head 1c3c1d0e (padding 5): 30 of 30 jobs green. That
  head was reviewed and approved with the seven-row finding above; this
  revision closes it.
- On-device audit of the M5StickS3 (135x240, IR capability present) at
  dev+g1c3c1d0e: visible_objects 34, labels 7, issues 0, overlaps 0,
  clipped 0. The seven labels are the seven home rows, which confirmed the
  fuller menu on the primary device and drove the padding 3 trim.

## Hardware boundary

The M5StickS3 on-device audit above covered the home menu with the Level and
IR rows present at padding 5. A look at the padding 3 home menu on the device
is owed before merge; the layout is fixed arithmetic the simulator reproduces,
so no behavioural difference is expected.
