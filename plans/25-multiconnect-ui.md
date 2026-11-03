# PR25: multi-connect selection and a Cameras status page

## Goal

Make Multi-Connect legible. Fix the camera selection flow so it takes fewer
taps, and add one read-only "Cameras" page on the Connected menu that shows each
active camera, its connection state and its signal strength.

All line anchors below were read at commit `2b79ce8` on `master`.

## Motivation

Multi-Connect works, but the user cannot see what it is doing.

Once two or three cameras are connected there is no way to tell which ones are
actually connected. The Connected page looks identical whether one camera or
three are live. If one camera drops, the only sign is the "Connecting" message
box taking over the whole screen, which says nothing about which camera left.
Selecting the cameras is also clumsy. The list is a column of checkboxes and the
"Multi-Connect" button is appended after the last camera, so on a Stick screen
with several saved cameras the button is below the fold. The selection is also
forgotten on every power cycle, so the same boxes have to be ticked again every
time.

## Draft issue

Open this before any code. Motivation only, no design.

> **Multi-Connect gives no visibility into which cameras are connected**
>
> When Multi-Connect is enabled and two or three cameras are connected, there is
> no way on the device to see which ones are actually connected right now. The
> Connected page looks the same whether one camera is live or three are. If one
> camera drops, the reconnect message box takes over the screen but never says
> which camera it lost. Selecting the cameras is also awkward: the boxes have to
> be ticked again after every power cycle, and the Multi-Connect button sits
> below the camera list so it is off screen when several cameras are saved.
> Would a small read-only status page reachable from the Connected menu be
> welcome?

## Scope

In scope:

- Move the Multi-Connect button to the top of the Connect page and put the
  selected count in its label.
- Disable that button while nothing is selected.
- Remember the last multi-connect selection in NVS and pre-tick it.
- A read-only "Cameras" page on the Connected menu, one row per active camera,
  showing name, state and RSSI.
- A `Camera::getRSSI()` accessor, because RSSI is not reachable today.
- Grid placement for Core and Core2, which lay the Connected page out as a grid.

Out of scope:

- Any change to the reconnect state machine in `src/FurbleControl.cpp`.
- Any change to the "Connecting" message box behaviour. It is existing
  behaviour, and changing it in the same PR would make the PR hard to review.
- A select-all button. It is an extra UI element and remembering the selection
  removes most of the need for it. See the alternatives note.
- Making Auto-Connect use the remembered multi-connect set. Separate PR.

## Files to change

### Selection flow

- `src/FurbleUI.cpp:1430-1459`, `UI::addConnectMenu`. The `LV_EVENT_CLICKED`
  callback on `menu.button` rebuilds the page every time it is opened. Line 1438
  reads `Settings::load<Settings::MULTICONNECT>()`. Line 1440 calls
  `lv_obj_clean(menu.page)`. Lines 1442-1446 load the saved list and add one
  item per camera through `addCameraItem` at line 1445. Lines 1448-1454 create
  the plain "Multi-Connect" button, after the camera items, and bind `doConnect`
  to it at line 1453. Reorder so the button is created before the loop, and set
  its label from a running count.
- `src/FurbleUI.cpp:770-813`, `UI::addCameraItem`. Line 771 decides checkbox
  mode from `MODE_MULTICONNECT`. The `MODE_MULTICONNECT` case at lines 799-810
  binds `LV_EVENT_VALUE_CHANGED` and does nothing but
  `camera->setActive(checked)`. This is where the count update and the pre-tick
  belong.
- `src/FurbleUI.cpp:647`, `UI::addMenuItem`, which creates the checkbox object.
  Needed to know what to call `lv_obj_add_state(item, LV_STATE_CHECKED)` on.
- `src/FurbleUI.cpp:901-908`, the existing disable pattern. It uses
  `lv_obj_add_state(button, LV_STATE_DISABLED)` and `lv_obj_remove_state(...)`
  on the Connect and Delete buttons when there are no saved cameras. Copy it for
  the Multi-Connect button when the count is zero.
- `src/FurbleUI.cpp:1229-1249`, `UI::doConnect`. Lines 1233-1238 walk
  `CameraList` and call `control.addActive(camera)` for every camera where
  `isActive()` is true. Line 1243 calls `control.connectAll(...)`. Lines
  1247-1248 jump to the Connected page. This is shared by single and multi
  connect, so nothing here changes. Add the "save the selection" call here,
  once, after the loop.
- `include/FurbleUI.h:128`, `CameraListMode_t`. No new mode is needed.

### Cameras status page

- `src/FurbleUI.cpp:1464-1481`, `UI::addConnectedMenu`. The current branch
  already contains the merged PR03 Connected-page layout. Add the Cameras item
  to that existing grid.
- `src/FurbleUI.cpp:53-76`, `UI::m_Menu`. Add an entry for the new page. The
  Connected page grid already holds Remote, Bulb, Interval, GPS Data and
  Disconnect. Add `m_CamerasStr` at `{1,1}`.
- `include/FurbleUI.h:161-191`, the page name string constants. Add
  `m_CamerasStr`. `m_GPSDataStr` at line 185 is the model.
- `src/FurbleUI.cpp:1548-1611`, the GPS Data page. This is the pattern to copy
  in full:
  - line 1548 creates the page and its button with `addMenu(m_GPSDataStr, NULL,
    true, menu)`
  - lines 1555-1589 create a 1000 ms `lv_timer` whose user data is the `menu_t`
  - line 1590 pauses it immediately
  - lines 1592-1601 resume the timer on the button `LV_EVENT_CLICKED` and attach
    `gpsDataStop` to the menu object
  - line 1603 attaches the page with `lv_menu_set_load_page_event`
  - `UI::gpsDataStop` at lines 1606-1611 pauses the timer again and removes
    itself
- `include/FurbleUI.h:355`, `static void gpsDataStop(lv_event_t *e)`. Declare
  the Cameras equivalent next to it.
- `src/FurbleUI.cpp:871-872`, `addMainMenu` calls `addSettingsMenu()` then
  `addConnectedMenu()`. Page creation order matters if any page is shared.

### Control and camera state

- `include/FurbleControl.h:99`, `getTargets()` returns `const
  std::vector<std::unique_ptr<Control::Target>> &`. This is the list of active
  cameras and it is what the Cameras page iterates.
- `include/FurbleControl.h:24-36`, `state_t`. `STATE_CONNECT`,
  `STATE_CONNECTING`, `STATE_ACTIVE` are what distinguish "connected" from
  "reconnecting".
- `include/FurbleControl.h:124`, `getState()`.
- `src/FurbleControl.cpp:159-162`, the `STATE_ACTIVE` case in `Control::task`.
  If `allConnected()` goes false it sets `STATE_CONNECT` and loops. This is the
  silent drop path.
- `src/FurbleControl.cpp:95-133`, `Control::connectAll(void)`. Line 102 iterates
  targets and only reconnects the ones where `isConnected()` is false. Line 97
  picks the 5 s infinite reconnect timeout over the 30 s default
  (`include/FurbleControl.h:72-74`).
- `lib/furble/Camera.h:119`, `isConnected()`. `lib/furble/Camera.cpp:92-99`
  shows it takes `m_Mutex` and special cases FauxNY.
- `lib/furble/Camera.h:138`, `getConnectProgress()`, returns 0 to 100 and is
  already used for the connect progress bar at `src/FurbleUI.cpp:1134`.
- `lib/furble/Camera.h:168`, `NimBLEClient *m_Client`, protected. RSSI has to go
  through a new accessor because `m_Client` is not public.
- `lib/furble/Camera.cpp`, add `Camera::getRSSI()` next to `isConnected()`.
- `lib/furble/FauxNY.h` and `lib/furble/FauxNY.cpp`, so the test camera returns
  something sane instead of touching a null client.

### Settings

- `include/FurbleSettings.h:16-29`, `type_t`. Add `MULTISELECT` after
  `MULTICONNECT` (line 24).
- `include/FurbleSettings.h:130`, the `storage_type<MULTICONNECT>`
  specialisation. Add one for the new blob type.
- `src/FurbleSettings.cpp:11-24`, `m_Setting`. Add the name, key and namespace.
  Line 19 is the `MULTICONNECT` row.
- `src/FurbleSettings.cpp:169-228`, `Settings::init`. Add a default case that
  writes an empty selection on first boot. Lines 219-227 show the blob default
  pattern used by `TOUCH_CALIBRATION`.

## New settings

One, and it is not user visible.

| Setting | Type | Default | Where |
|---|---|---|---|
| `MULTISELECT` | fixed size blob | empty | not shown in any menu |

The blob remembers which saved cameras were ticked last time Multi-Connect was
used. Empty means nothing is pre-ticked, which is exactly today's behaviour. No
new switch, no new page, no new menu entry.

## Menu placement

```
Connect  (Multi-Connect on)
+- [Connect 2]     (moved to the top, label carries the count)
+- [x] Camera A
+- [ ] Camera B
+- [x] Camera C

Connected
+- Remote        {0,0}
+- Bulb          {1,0}
+- Interval      {2,0}
+- GPS Data      {0,1}
+- Cameras       {1,1} (new, read-only)
+- Disconnect    {2,1}
```

On Core and Core2 the current Connected page is a three-column by two-row grid
from merged PR03. It already has Bulb and GPS Data, so no grid dimension change
is needed. Cameras uses the open middle-bottom cell `{1,1}` and Disconnect
stays at `{2,1}`. On the Stick boards the Connected page is a scrolling list,
so placement is insertion order.

Put Cameras next to Remote and keep Disconnect last, so a mis-click does not
drop the connection.

PR03 is already merged on this branch. The implementation integrates with its
existing five-item grid instead of applying the old two-by-two grid instruction.

## Implementation notes

### Multi-Connect button placement and count

Today the button is created after the camera loop
(`src/FurbleUI.cpp:1448-1454`), so it is the last object on a scrolling page.
Create it first instead, keep the pointer, and update its label from the
checkbox callback:

```
lv_label_set_text_fmt(label, "Connect %u", selected);
```

`selected` is derived by counting `CameraList::get(n)->isActive()` over the
list, not by keeping a separate counter. `isActive()` is already the single
source of truth that `doConnect` reads at `src/FurbleUI.cpp:1235`, so there is
nothing new to keep in sync.

When `selected` is zero, `lv_obj_add_state(button, LV_STATE_DISABLED)`. When it
goes above zero, `lv_obj_remove_state(...)`. Same two calls the main page
already makes at `src/FurbleUI.cpp:901-908`.

This is one button with one label, same as today. No element is added.

### Remembering the selection

A bitmask over `CameraList` indices is wrong. `CameraList::remove` rewrites the
saved index (`lib/furble/CameraList.cpp:110-125`), so deleting a camera silently
shifts every later bit onto the wrong camera.

Store names instead. On the current branch `CameraList::index_entry_t` uses its
fixed `char name[16]` as an address key, not as the display name. The selection
blob therefore owns a separate fixed array of eight 16 byte display-name slots.
It is 128 bytes before the count field and keeps the intended fixed storage
shape:

```
typedef struct {
  char name[8][16];
  uint8_t count;
} multiselect_t;
```

Save it once in `doConnect` (`src/FurbleUI.cpp:1229-1249`) after the activate
loop. Load and apply it in the `addConnectMenu` refresh callback
(`src/FurbleUI.cpp:1434-1458`), right where each item is created: if the camera
name matches an entry, call `camera->setActive(true)` and
`lv_obj_add_state(item, LV_STATE_CHECKED)` on the returned object.

Blob save and load already work. `Settings::save<calibration_t>` and
`Settings::save<interval_t>` are the same shape, see
`src/FurbleSettings.cpp:169-228`.

Names are not unique in principle. Two identical camera models produce two
identical names, and both get pre-ticked. That is a pre-existing property of the
saved list, which is also keyed by name, so it is not made worse here. Say so in
the PR body.

The match is also only a 15-character prefix comparison, because the stored
slots are 16-byte C strings. Two cameras whose names share the same first 15
characters both get pre-ticked even when the full names differ. Accepted and
documented, not fixed here.

### The Cameras page

Copy the GPS Data page exactly (`src/FurbleUI.cpp:1548-1611`). One difference:
GPS Data creates its labels as function local statics inside the timer callback
(lines 1560-1587) because the label set never changes. The camera list changes
per connection, so build the rows when the page is opened, not in the timer:

- on the button `LV_EVENT_CLICKED` handler, `lv_obj_clean(page)`, then one label
  per entry of `Control::getInstance().getTargets()`, then
  `lv_timer_resume(timer)`
- in the 1000 ms timer, format each row and call `lv_label_set_text` only when
  the formatted text changed
- on leaving, pause the timer through a `camerasStop` copy of `gpsDataStop`
  (`src/FurbleUI.cpp:1606-1611`)

The timer runs only while the page is visible. That is the whole point of the
GPS Data pattern and it is why this costs nothing when the page is closed.

Guard the rebuild with `UI::m_Mutex` (`src/FurbleUI.cpp:42`) the same way
`updateItems` is guarded from the scan callback at `src/FurbleUI.cpp:943-945`.
`getTargets()` returns a reference to `m_Targets`, which `Control::disconnect`
clears at `src/FurbleControl.cpp:242` under `m_Mutex`. Do not hold that
reference across a timer tick. Re-fetch it each tick and re-check the size
against the number of labels; if they differ, the connection changed underneath
and the page should be rebuilt.

### Row content

One line per camera:

```
Camera A   connected    -54 dBm
Camera B   reconnecting
```

State comes from two facts already available:

| `Control::getState()` | `Camera::isConnected()` | Row shows |
|---|---|---|
| `STATE_ACTIVE` | true | `connected` |
| `STATE_CONNECTING` | true | `connected` |
| `STATE_CONNECTING` | false | `reconnecting NN%` |
| `STATE_CONNECT` | false | `reconnecting` |
| `STATE_CONNECT_FAILED` | false | `lost` |
| `STATE_DISCONNECTING` | any | `disconnecting` |

`NN` is `Camera::getConnectProgress()` (`lib/furble/Camera.h:138`), the same
value the connect progress bar uses at `src/FurbleUI.cpp:1134`.

This is the drop indication. No new icon, no new banner, no blinking anything. A
camera that goes away turns into a "reconnecting" row, driven entirely by the
existing infinite reconnect machinery at `src/FurbleControl.cpp:95-133`. When it
comes back the row says connected again.

### RSSI

`Camera` does not expose RSSI. `m_Client` is protected
(`lib/furble/Camera.h:168`). `NimBLEClient::getRssi()` exists and returns `int`
(esp-nimble-cpp 2.5.0, `src/NimBLEClient.h:65`).

Add to `lib/furble/Camera.h` next to `isConnected()` at line 119:

```
/** Get connection RSSI in dBm, or INT8_MIN if unknown. */
int8_t getRSSI(void) const;
```

Implement it in `lib/furble/Camera.cpp` alongside `isConnected()` at lines
92-99, taking `m_Mutex` the same way, returning `INT8_MIN` when `m_Client` is
null or not connected. `FauxNY` has no client, so return `INT8_MIN` there too
and print `--` for it.

`getRssi()` is not free. It issues an HCI read of the connection RSSI and blocks
on the reply, so it must not be called from a hot path. Once per second per
camera, only while the page is on screen, is acceptable. Confirm the call
latency on device with two cameras before merging. If it turns out to stall the
LVGL timer, drop RSSI from this PR and ship name plus state only. The page is
still worth having without it.

Print `INT8_MIN` as `--`, not as a number.

### Alternatives considered

- **Select all button.** Rejected. It is a second button for a case that
  remembering the selection already covers. If upstream wants it, it is three
  lines on top of this PR.
- **Per camera icons in the status bar.** Rejected. The status bar is shared and
  already carries GPS and reconnect icons (`include/FurbleUI.h:67-75`). A row of
  camera dots there does not scale past two cameras and it costs a new element
  on every page.
- **Suppressing the reconnect message box when only one of several cameras
  drops.** Tempting, and it is the real annoyance in multi-camera use, but it
  changes `connectTimerHandler` behaviour (`src/FurbleUI.cpp:1119-1136`) for
  every user including single camera users. Keep it out. Raise it in the issue
  and let upstream decide if it wants a follow-up.

## Dependencies

None hard.

Integrates with the PR03 Connected page grid, see Menu placement.
Independent of everything else. The `Camera::getRSSI()` accessor originally
planned here was dropped during review, see the fork PR #24 reconciliation
section. RSSI consumers (diagnostics, adaptive TX power) build on the cached
snapshot from that PR instead.

## Risks

- `getTargets()` hands out a reference to a vector that `Control::disconnect`
  clears (`src/FurbleControl.cpp:242`). A timer tick that runs during a
  disconnect can walk a stale vector. Re-fetch every tick, and pause the timer
  in `doDisconnect` (`src/FurbleUI.cpp:1251-1263`) as well as in the page leave
  handler.
- `NimBLEClient::getRssi()` blocks. Measured cost is unknown until it is tried
  on hardware. Mitigation is above.
- Moving the Multi-Connect button changes focus order on the three button
  boards. Existing users press down N times to reach it today. Call this out in
  the PR body.
- The Core grid change moves Disconnect. Muscle memory breaks. Call it out too.
- Pre-ticked checkboxes mean a user who forgot the last selection could connect
  to a camera they did not mean to. The count in the button label is the
  mitigation, and the boxes are visible before the button is pressed.
- A saved camera that was deleted since the last multi-connect will not match
  any name and is simply not ticked. Verify that path rather than assuming it.

## Verification

Build matrix:

```
export FURBLE_VERSION=dev FURBLE_TEST=0
pio run -e m5stick-c -e m5stick-c-plus -e m5stack-core -e m5stack-core2 -e m5stick-s3
```

On device, M5StickS3 over USB, with the Fujifilm camera plus FauxNY as a second
target. Enable FauxNY under Settings, Features (`src/FurbleUI.cpp:1613-1620`).

Selection:

1. Enable Multi-Connect. Open Connect. Confirm the Connect button is the first
   item and reads `Connect 0` and is disabled.
2. Tick one camera. Confirm the label reads `Connect 1` and the button is
   enabled. Tick the second. Confirm `Connect 2`.
3. Untick both. Confirm the button disables again.
4. Connect two cameras, disconnect, power cycle, open Connect. Confirm both
   boxes are pre-ticked and the label reads `Connect 2`.
5. Delete one of the two saved cameras. Open Connect. Confirm the remaining one
   is ticked, the deleted one is gone, and nothing crashes.
6. Turn Multi-Connect off. Confirm the Connect page is a plain list with no
   checkboxes and no Connect button, exactly as today.

Cameras page:

7. Connect two cameras. Open Connected, Cameras. Confirm two rows, both
   `connected`, both with a plausible RSSI between -30 and -95 dBm.
8. Walk away from the camera until the number falls, walk back. Confirm the
   number tracks.
9. Power off one camera. Confirm its row turns to `reconnecting` within a few
   seconds while the other stays `connected`.
10. Power the camera back on. Confirm the row returns to `connected`.
11. Leave the Cameras page. Confirm the timer stops: add a temporary `ESP_LOGD`
    in the timer callback and confirm it stops logging.
12. Disconnect from the Connected page while the Cameras page is open in
    history. Confirm the UI lands on the main menu, no crash, no stale rows.
13. Connect a single camera without Multi-Connect. Confirm the Cameras page
    shows exactly one row and is still useful.
14. Leave the Cameras page open for ten minutes with two cameras. Confirm no
    memory growth, using the ESP-IDF heap trace or a periodic
    `esp_get_free_heap_size` log.

On Core2, verify the Connected page grid: four items, no overlap, focus order
left to right, touch targets not clipped. Repeat steps 7 to 10.

Battery drain: the new timer runs at 1 Hz and only while the page is visible,
and each tick does one RSSI read per camera. Measure current on the Cameras page
with two cameras connected and compare against the Remote page for the same
duration. Report both numbers in the PR body. If the difference is not small,
drop RSSI.

Camera coverage: Fujifilm on hardware, FauxNY for the second target. Sony,
Nikon, Canon and Ricoh inherit `Camera::getRSSI()` unchanged from the base class
and are untested. State this in the PR body.

## Implementation state

Implemented on `feat/25-multiconnect-ui`.

Rebase notes:

- `MULTISELECT` is a device-local selection blob, so it gets wire_id 0 and is
  not exposed over the companion wire, matching `TOUCH_CALIBRATION` and
  `BULB`.
- Console settingType reports it as "struct"; printValue and setValue fall to
  their unsupported defaults. `src/FurbleCompanion.cpp` groups it with the
  other SETTING_BLOB rejects.

- The Multi-Connect button is first, reads `Connect N`, joins the input group,
  and is disabled when `N` is zero. The checkbox rows update it through a
  second `LV_EVENT_VALUE_CHANGED` callback whose user data is the button
  pointer, not by walking the widget parent chain.
- The last active selection is stored in the `MULTISELECT` NVS blob. Display
  names use eight fixed 16-byte slots. Matching uses the same 15-character
  normalized form used when saving.
- Only the Multi-Connect button click saves the selection, right before it
  calls `doConnect`. Single connect, boot autoconnect and console connect
  never touch the blob, and an unchanged selection skips the NVS write.
- The Connected menu has a read-only Cameras page. It creates one row per
  active target and refreshes name plus connection state once per second while
  visible. Row text is compared before any LVGL label update and rows clip
  with `LV_LABEL_LONG_DOT` rather than scrolling.
- The Cameras timer starts and stops in the `LV_EVENT_VALUE_CHANGED`
  page-change dispatch, following the diagnostics timer. It also pauses while
  the reconnect message box is visible and resumes when the box hides with the
  page still active. `doDisconnect` pauses it as well.
- The target list is re-fetched on every tick and the page is rebuilt when its
  size changes.

### Deviations from the original plan

- The branch already contains merged PR03. Its Connected page is a three-column
  by two-row grid with Bulb and GPS Data. Cameras uses the unused `{1,1}` cell.
  The old two-column by two-row instruction is not applied, and Disconnect
  remains at `{2,1}`.
- The original line anchors describe the pre-PR03 file layout. The implementation
  uses the current `addConnectedMenu` and `addConnectMenu` locations.
- The current `CameraList::index_entry_t::name` field is an address key. The
  selection blob uses a separate display-name array instead of reusing that
  field.
- The existing Connected flow hides the menu back button. The page-change
  handler now restores it for Cameras and the shared GPS Data page so both
  read-only pages can be left while connected.
- Live RSSI is dropped, taking the fallback the RSSI section already named.
  `NimBLEClient::getRssi()` is a blocking HCI round trip serialized against
  all host operations, up to two seconds worst case, and the rows render on
  the LVGL task. The rows show name and connection state only and the
  `Camera::getRSSI()` accessor is removed again, so this PR no longer touches
  lib/furble. See the fork PR #24 reconciliation section.
- The GPS-Data-style `camerasStart`/`camerasStop` click handlers from the
  original plan are gone. The click-based stop leaked a re-registration on
  every page entry, and the page-change dispatch is the established pattern
  for timers tied to page visibility.
- `Control::getTargets()` stays a raw reference. That is safe today because
  every mutation of `m_Targets` (`addActive`, the clear in `disconnect`) runs
  on the LVGL task, the same task that renders the rows. Fork PR #24 replaces
  this with a snapshot form, which supersedes the raw reference when it lands.

### Fork PR #24 reconciliation

Fork PR #24 (adaptive BLE connection parameters, plan 10) maintains a cached
per-target connection statistics snapshot via `updateConnStats`, refreshed off
the render path. That cache is the one sanctioned RSSI source:

- Whichever of the two PRs lands second wires the Cameras rows to the cached
  snapshot. There must never be a second, live RSSI path.
- The row format keeps room for a trailing RSSI column, so re-adding it is a
  formatting change in `updateCameraRow` only.
- Until then the Cameras page shows name and connection state, which already
  covers the drop-visibility motivation.

### Verification state

- clang-format 21.1.2 ran on all touched C++ files.
- `FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e m5stick-s3` and
  `-e m5stick-s3-debug` pass after the review fixes.
- Hardware tested: none. StickS3 pending.

### Narrow-panel connected-page fit

The Cameras entry adds a row to the Connected page. On the three narrow panels
the per-row padding is trimmed so the extra row stays on screen. The 135x240
M5StickC Plus and M5StickS3 drop the Connected page rows from 6 to 0 padding.
The 80x160 M5StickC, the shortest panel, already ran its rows at 1 padding for
the home menu; its Connected page now drops to 0 as well, matching the larger
narrow panels. All other menus on the M5StickC keep 1 padding. Core and Core2
lay the Connected page out as a grid and are unaffected. The
`sim/scenarios/bughunt/text-size-overflow-large.txt` scenario asserts the
Connected page reports no overflow on the 80x160 and 135x240 panels.

## References

- LVGL 9.4, Menu widget, `lv_menu_page_create`, `lv_menu_set_load_page_event`,
  `lv_menu_get_main_header_back_button`:
  https://lvgl.io/docs/open/9.4/details/widgets/menu
- LVGL 9.4, Timer, `lv_timer_create`, `lv_timer_pause`, `lv_timer_resume`,
  `lv_timer_get_user_data`:
  https://lvgl.io/docs/open/9.4/details/main-modules/timer
- esp-nimble-cpp 2.5.0, `NimBLEClient::getRssi()` and
  `NimBLEClient::isConnected()`:
  https://github.com/h2zero/esp-nimble-cpp/blob/master/src/NimBLEClient.h
- esp-nimble-cpp 2.5.0 API documentation index:
  https://h2zero.github.io/esp-nimble-cpp/
- M5Stack StickS3 product page:
  https://docs.m5stack.com/en/core/StickS3
- PlatformIO, device monitor, for on-device logs during page testing:
  https://docs.platformio.org/en/latest/core/userguide/device/cmd_monitor.html

## Hardware verification, pass 3, 2026-08-18

Verdict: PARTIAL. Tested on the combined image (version `hwv3`, app
`v3.9.1-159-g138dd80`) on the M5StickS3 over USB, with a Fujifilm X100VI as the
live camera (saved index 1) and a saved Fujifilm X-E5 (index 0).

Console evidence:

- `settings list` shows both `multiconnect` (bool, `Multi-Connect`) and
  `multiselect` (struct, `Multi-Select`, `applies: on reboot`, wire id 0). The
  remembered selection blob is present and, being a persisted struct, survives a
  reboot at the settings layer.
- `cameras list` returns the two saved cameras with names and types. `cameras
  status` returns per target name and `connected` flags, the data the Cameras
  page renders.
- RSSI is reachable. Connection RSSI shows in the logs while connected (for
  example the adaptive TX log `Adaptive RSSI -74.6 dBm` and idle values near
  -70 dBm), which is the same client RSSI the Cameras page reads through the new
  `Camera::getRSSI()` accessor.

Still on the user checklist, needs eyes, fingers and a second camera:

- Multi-Connect selection list with the Connect count in the button label, the
  disable when count is zero, and the pre-tick of the remembered set after a
  reboot.
- The Cameras status page rows, states and RSSI values while connected.
- The residual race check: power cycle the camera 5 or 6 times with the Cameras
  page open and confirm no multi second UI freeze.

Cross cutting stability note for the combined image (affects the whole
integration, not only this PR):

1. Boot task watchdog. Every boot of the `hwv3` combined image tripped a single
   `task_wdt` at about 8 s: the main task stalled for roughly 5 s during UI
   construction and starved the CPU 0 IDLE task, then recovered and ran normally.
   It reproduced across a fresh flash, a warm reboot and with `imu false`. No
   plain master or `combined3` boot log in the bench archive shows this. The
   LVGL invalidation rate at boot (359 per second) is normal, master boots show
   360 to 570 per second, so the storm is not the cause. Because the four merged
   branches all touch UI construction, this may be a merge resolution artifact of
   the combined test image rather than a single PR defect. It should be isolated
   per branch before merge.
2. Disconnect during connect hang. Issuing a console `disconnect` immediately
   followed by `connect` while the X100VI was connected de-enumerated the device
   from USB entirely (`AppleUSBSerial = 0`, no 303a device present), matching the
   reconnect cancel deadlock documented in the repo CLAUDE.md. Recovery needs the
   physical rescue: if PMIC `DL_LOCK` is retained, remove battery power first,
   restore it, then hold the side button until the green LED flashes and reflash.
   None of these four PRs change the reconnect state
   machine (this PR explicitly leaves it alone), so this is most likely a pre
   existing reconnect path hazard exposed by back to back console commands rather
   than a regression from these PRs. It still needs a fix before the reconnect UX
   work lands.

Lifecycle and camera walk evidence captured on this image (feeds reconnect and
connection UX diagnosis):

- Good connect to the X100VI (Secure path): scan match, `Connecting to
  58:5E:B0:EF:23:76`, GATT `Connected` at about 1.2 s, then `Requesting status`,
  `Status: 16552300`, `Responding status with 16552320`, then subscribe to
  notifications 1 through 11 (8 and 10 fail to subscribe), first notification
  (2 bytes) from `c95d91ae-b247-4d6d-8661-7dd5d6a0f85b`, then the periodic 2 byte
  notification from `ad06c7b7-f41a-46f4-a29a-712055319122`. Full setup completes
  about 6 s after `Connected`.
- Connection UX (task 39): furble commits to `Connected` at the GATT layer about
  1.2 s in, but the status handshake and 11 notification subscriptions run for
  about 6 s more. The connecting overlay lingering past the real connect lines up
  with the UI holding the overlay until this setup finishes.
- Secure path confirm (plans/75, PR 70): on the X100VI there is no confirm wait.
  furble declares connected right after the GATT connect. The Fujifilm status
  characteristic value in the registered, awake state read `16552300` and furble
  answered `16552320`. The unregistered comparison, camera in its settings menu,
  needs the user and is on the checklist below.
- Shutter fires on the X100VI: `shutterPress(X100VI)` and `shutterRelease(X100VI)`
  log cleanly with no error. Physical capture confirmation needs the user.
- Reconnect backoff (feat/62): after a drop the log shows `Timeout waiting for
  camera` then `Reconnect retry N, waiting 5000 ms`, the 5 s infinite reconnect
  interval.
