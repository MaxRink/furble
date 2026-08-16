# PR03: reach Settings while connected

## Goal

Add a Settings entry to the Connected page so settings are reachable without
disconnecting the camera. Pure navigation, no new settings and no behaviour
change anywhere else.

## Scope

In scope:

- A Settings item on the Connected page that loads the existing Settings page.
- Back button handling so back returns to the Connected page while a camera is
  connected.
- A warning on the settings pages that are unsafe to use during a connection.
- Grid placement for Core and Core2, which lay the Connected page out as a grid.

Out of scope:

- Any restructuring of the Settings tree.
- Disabling individual settings. A warning is enough for this PR.

## Files to change

- `src/FurbleUI.cpp:53-76`, `UI::m_Menu`. Add an entry for the new Connected
  page item. The Connected page grid currently holds `m_RemoteShutter {0,0}`
  (line 72), `m_RemoteInterval {1,0}` (line 73) and `m_RemoteDisconnect {2,0}`
  (line 74).
- `src/FurbleUI.cpp:1265-1279`, `UI::addConnectedMenu`. Lines 1268-1275 set a
  three column by one row grid for `FURBLE_M5COREX`. Lines 1277-1279 create the
  three existing items. Add the Settings item here.
- `src/FurbleUI.cpp:1402-1413`, the shared page pattern used for the
  intervalometer. `lv_menu_set_load_page_event(menuIntervalometer.main,
  menuInterval.button, menuIntervalometer.page)` at lines 1404-1405 attaches an
  already created page to a second button. Copy it for Settings.
- `src/FurbleUI.cpp:950`, the empty `else if (page ==
  m_Menu.at(m_SettingsStr).page) {}` branch in the `LV_EVENT_VALUE_CHANGED`
  handler. This is where the back button must be re-enabled and un-hidden.
- `src/FurbleUI.cpp:955-961`, the Connected page branch. It stops scans, then
  disables and hides the back button. This stays, and it is what re-hides back
  when the user comes back from Settings.
- `src/FurbleUI.cpp:980-998`, the `LV_EVENT_CLICKED` handler. Its Connected
  branch at lines 990-996 moves focus off the back button and releases the
  shutter lock. Check the same is right when arriving from Settings.
- `src/FurbleUI.cpp:1251-1263`, `UI::doDisconnect`. It clears the menu history
  and jumps to the main page. Confirm it behaves correctly when the user is deep
  in Settings at the moment of a disconnect.
- `src/FurbleUI.cpp:869-873`, `addMainMenu` calls `addSettingsMenu()` at line 872
  before `addConnectedMenu()` at line 873. So the Settings page object already
  exists when the Connected menu is built. This is the same ordering the
  intervalometer relies on.

## New settings

None.

## Menu placement

```
Connected
+- Remote
+- Interval
+- Settings   (new, shares the existing Settings page)
+- Disconnect
```

On Core and Core2 the Connected page is a grid, created at
`src/FurbleUI.cpp:1269-1274` with three columns and one row. A fourth item does
not fit. Either widen the column descriptor to four columns, or go to two rows of
two and move Disconnect to `{1,1}`. Two rows of two reads better on a 320x240
screen and matches the four slot look of the main menu. Whichever is chosen, the
`{col,row}` values in `UI::m_Menu` must be updated for `m_RemoteDisconnect` as
well as the new entry. On the Stick boards the Connected page is a scrolling
list, so placement is just insertion order.

Put Settings before Disconnect so a mis-click does not drop the connection.

## Implementation notes

### Loading the shared page

Copy the intervalometer pattern exactly:

```
menu_t &menuSettings = m_Menu.at(m_SettingsStr);
lv_menu_set_load_page_event(menuSettings.main, settingsButton, menuSettings.page);
```

`lv_menu_set_load_page_event` pushes the current page onto the menu history, so
the built in back button already returns to the Connected page. No custom back
handling is needed for the happy path.

### Back button state

The back button is a single shared object,
`lv_menu_get_main_header_back_button(m_MainMenu.main)`. Its disabled and hidden
state is set per page by the `LV_EVENT_VALUE_CHANGED` handler starting at
`src/FurbleUI.cpp:887`. The Connected branch at lines 959-961 sets both
`LV_STATE_DISABLED` and `LV_OBJ_FLAG_HIDDEN`. Nothing clears them on the Settings
page today, because the Settings branch at line 950 is empty. So entering
Settings from Connected would leave the user with no back button.

The fix is two lines in that branch:

```
lv_obj_remove_state(back, LV_STATE_DISABLED);
lv_obj_clear_flag(back, LV_OBJ_FLAG_HIDDEN);
```

The main menu branch already does the enable half at lines 913-916, so this
matches existing style. When the user goes back, the Connected branch re-hides
the button, which restores today's behaviour of not being able to walk out of a
connection by accident.

Note that entering Settings from the main menu already leaves back enabled,
because the main page branch enables it. Adding the explicit clear is harmless
there and makes the page self contained.

### Disconnect while inside Settings

`doDisconnect` at `src/FurbleUI.cpp:1251-1263` calls `lv_menu_clear_history` then
`lv_menu_set_page(m_MainMenu.main, m_MainMenu.page)` and focuses the Connect
button. That already yanks the user out of any page, so an unexpected disconnect
from inside Settings lands on the main menu. Verify it does not leave a stale
back button state, since the main page branch re-enables back on load. Also
verify the connection message box at `src/FurbleUI.cpp:1256` is hidden correctly
when the user was not on the Connected page.

### Connection unsafe pages

Four settings pages do something that disturbs an active connection:

- Theme, `src/FurbleUI.cpp:1980-1992`, has a Restart button that calls
  `esp_restart()`.
- Touch calibration, `src/FurbleUI.cpp:1935-1947`, takes over the screen.
- GPS enable and GPS baud, `src/FurbleUI.cpp:1517` and `:1531-1546`, call
  `reloadSetting()`, which cycles the Grove 5 V rail through
  `M5.Power.setExtOutput` at `src/FurbleGPS.cpp:118` and `:128`.
- TX Power, `src/FurbleUI.cpp:2010-2037`, calls `control.setPower()` on release,
  which changes the radio while connected.

Show a message box when one of these pages is opened during an active
connection. Use the existing `lv_msgbox_create` pattern from
`src/FurbleUI.cpp:191-198`. Query connection state with
`Control::getInstance().getState()` against `STATE_ACTIVE`, declared at
`include/FurbleControl.h:24-37` and `:124`. Warn only. Do not block, and do not
change what the controls do.

## Dependencies

None. Independent of the other Phase 0 PRs. PR01 and PR05 add pages under
Settings, and those pages inherit this navigation for free.

## Risks

- Back button state is global. A missed branch leaves the user stuck with no way
  out except the power button. Walk every page reachable from Connected during
  verification.
- The Core grid change moves Disconnect. Muscle memory breaks for existing users.
  Call this out in the PR body.
- Changing settings during a connection can drop the link. That is the user's
  choice, and the warning covers it.
- The shutter lock state at `src/FurbleUI.cpp:506-521` is released when the
  Connected page is clicked into (`src/FurbleUI.cpp:995`). Confirm a lock is not
  left engaged when the user wanders into Settings and back.

## Verification

Build matrix:

```
export FURBLE_VERSION=dev FURBLE_TEST=0
pio run -e m5stick-c -e m5stick-c-plus -e m5stack-core -e m5stack-core2 -e m5stick-s3
```

On device, M5StickS3 over USB, connected to the Fujifilm camera:

1. Connect. Confirm the Connected page shows Remote, Interval, Settings,
   Disconnect.
2. Open Settings. Confirm the back button is visible and works.
3. Walk into Display, Features, GPS, Timer, Theme, TX Power, About. Back out of
   each. Confirm back always returns one level and finally to Connected.
4. From Connected, confirm back is hidden again and cannot leave the page.
5. Change brightness while connected. Confirm the camera stays connected.
6. Open TX Power and Theme. Confirm the warning message box appears.
7. Fire the shutter after returning from Settings. Confirm it still works.
8. Engage the shutter lock, go to Settings, come back, confirm the lock state is
   sane and the camera is not left with the shutter held.
9. Power off the camera while the user is inside Settings. Confirm the UI lands
   on the main menu with a working back button and no stale message box.
10. Repeat 1 to 4 with Multi-Connect enabled and two targets if a second camera
    is available. Otherwise enable FauxNY (`src/FurbleUI.cpp:933-936`) as the
    second target.

On Core2, verify the Connected page grid: four items, no overlap, focus order
left to right, and touch targets not clipped. Repeat steps 1 to 5.

Battery drain: not applicable. No new timers, no new polling. Do not measure.

Camera coverage: Fujifilm on hardware. FauxNY covers the multi target path.
Sony, Nikon, Canon and Ricoh code paths are untouched. State this in the PR body.

## References

- LVGL 9.4, Menu widget, `lv_menu_page_create`, `lv_menu_set_load_page_event`,
  `lv_menu_get_main_header_back_button`:
  https://lvgl.io/docs/open/9.4/details/widgets/menu
- M5Unified Power_Class, `setExtOutput`, used by the GPS enable path:
  https://docs.m5stack.com/en/arduino/m5unified/power_class
- M5Stack StickS3 product page:
  https://docs.m5stack.com/en/core/StickS3
- PlatformIO, device monitor, for on-device logs during navigation testing:
  https://docs.platformio.org/en/latest/core/userguide/device/cmd_monitor.html
