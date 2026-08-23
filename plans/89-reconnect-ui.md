# 89 - Reconnect UI indicator (task #54)

## Motivation

Hardware testing surfaced a gap: when a camera drops mid-session and furble is
silently auto-reconnecting, the screen gives no sign of it. The connected page
keeps showing live shutter controls while the link is actually down and
retrying (task #54, and sim bug-hunt findings F3 and F4 in plan 86).

The first attempt at a fix (the initial fix/connui commits) drove the connect
progress box back onto the screen on a mid-session drop. That took over the
whole display: it hid the main menu and blanked the shutter trigger while
control retried. In a multi-connect session one camera dropping would blank the
trigger for every camera, including the ones still connected. That approach was
rejected.

## Design

Keep the connected view and its shutter trigger fully live during a mid-session
reconnect. Surface the retry with a small non-blocking indicator instead of a
page takeover.

- A `reconnectingIcon` image lives in the top status row (`m_Header`), next to
  the existing reconnect, GPS, and battery icons. It is created hidden. It is an
  `lv_image`, not a focusable widget: it is not in any encoder group and never
  takes focus, so it draws no focus ring.
- The connect timer keeps polling after the link goes active (it no longer
  parks on `STATE_ACTIVE`), so it observes a drop. A `sessionEstablished` flag
  on the connect context records that a link has gone fully active at least
  once.
  - Initial connect (`sessionEstablished` false): the connect progress box owns
    the screen as before, until the first link goes active. Nothing else is on
    screen yet, so there is nothing to take over.
  - Mid-session drop (`sessionEstablished` true): the connecting branch does not
    touch the menu or the progress box. It only shows the `reconnectingIcon`.
    The connected page and its trigger stay up and usable.
- The indicator clears the moment control returns to `STATE_ACTIVE`, and on a
  full teardown (idle, disconnecting, or an interactive Disconnect).
- Every periodic show or hide of the icon goes through `showStatusIcon`, which
  toggles `LV_OBJ_FLAG_HIDDEN` only when the visibility actually changes. This
  keeps the icon within the LVGL redraw discipline: no unconditional
  invalidation on the liveness poll (project rule).

### Status text, not only an icon

The icon alone was not enough: on hardware the reconnecting icon appeared while
the connected page title still read "Connected", which contradicts itself. The
title is now bound to the same state as the icon.

- The connected page is an `lv_menu` page whose header title normally reads
  "Connected". `addMainMenu` caches that header title label
  (`m_Status.menuTitle`) once, found by type among the main header's children so
  it does not depend on a brittle child index.
- `UI::updateReconnectTitle(bool reconnecting)` rewrites it in place:
  "Reconnecting" for a single camera, or "Reconnecting (i/n)" for a
  multi-connect session, and back to "Connected" on recovery. It is a no-op
  unless the connected page currently owns the header, so a sub page (Shutter,
  Intervalometer, GPS Data) keeps its own title while the status-row icon still
  signals the retry globally.
- The count semantics are **i = number of cameras currently down/reconnecting,
  n = total cameras in the session**. So "Reconnecting (1/2)" means one of two
  cameras dropped and is reconnecting while the other stays live. A single camera
  session shows the bare word "Reconnecting" with no count.
- `connectTimerHandler` drives it alongside the icon: the reconnecting branch
  calls `updateReconnectTitle(true)`, the active and idle/disconnecting branches
  call `updateReconnectTitle(false)`. Updates go through the guarded
  `setLabelTextIfChanged` / `setLabelTextFmtIfChanged` helpers so the label is
  only re-set when the text actually changes (same redraw discipline as the
  icon).

### Reconnect indicator on the Remote shutter page

The status-row icon and the connected-page title cover the connected menu, but
hardware testing of #149 surfaced a further gap: the full-screen Remote shutter
page, where shots are actually taken, showed no sign of a mid-session drop.
`updateReconnectTitle` deliberately leaves a sub page's own header title alone,
and the tight shutter layout does not read the small mono status-row icon.

A dedicated non-blocking banner now lives on the shutter page itself
(`m_RemoteReconnect`, built in `addConnectedMenu` on `menuShutter.page`):

- A red-recolored Bluetooth glyph (the shared `icon_bluetooth`, recolored with
  `image_recolor` rather than shipping a new compressed asset) stacked over a red
  "Reconnecting" / "Reconnecting (i/n)" label, in a translucent dark chip anchored
  top-left. It is `LV_OBJ_FLAG_FLOATING`, so it never joins the page layout or its
  scroll extent and never obscures the shutter, focus, or lock controls.
- The badge stacks the icon over the text (column layout) at the board's Small
  font so "Reconnecting (i/n)" stays inside even the 135 px StickS3 panel instead
  of clipping off the right edge. The 80 px StickC panel is too narrow for any
  wording, so it shows the red icon alone (the connected page title still carries
  the words there).
- `UI::updateRemoteReconnect(bool)` drives it from the same three
  `connectTimerHandler` branches and the same per-target counts as
  `updateReconnectTitle`, reusing the count semantics (i down of n total). Unlike
  the title helper it runs regardless of the current page: the banner is a child
  of the shutter page, so it only renders while that page is on screen, which is
  exactly where the title is not rewritten. Show/hide goes through
  `showStatusIcon` and the label through the changed-check helpers, so a liveness
  poll never re-invalidates.

### Reconnect UI polish (hardware bench follow-up)

A bench session on the M5StickS3 asked for three refinements on top of the
menu-title and shutter-page indicators. All three reuse the existing per-target
reconnect state (`getConnectedTargetCount` < `getTargetCount`); none add a second
connection-state tracker.

- **Red status-row Bluetooth symbol.** The status row already shows a Bluetooth
  glyph (`reconnectingIcon`) while a mid-session reconnect runs. It is now
  recolored red with `image_recolor`, the same red as the shutter-page banner
  glyph, so the drop is unmistakable in the persistent status row too. The
  recolor is a one-time local style set at creation: the icon is only ever
  visible while reconnecting (hidden otherwise), so there is nothing to restore
  on recovery and no per-tick style work. The existing `showStatusIcon` guard
  still toggles visibility only on the state transition.
- **Name the dropped camera.** When exactly one camera is down, the shutter-page
  banner names it: "Reconnecting X-T5" for a single-camera session, or
  "Reconnecting (i/n): X-T5" in a multi-connect session. With more than one down
  the name is ambiguous, so it falls back to the bare count. The name comes from
  `Control::getDisconnectedName()`, which returns the first target whose link is
  down under the control mutex. The name only shows where there is width for it:
  the 135 px StickS3/StickC Plus and the 320 px Core carry it, the 80 px StickC
  keeps the icon and count only (`RECONNECT_NAME_MIN_WIDTH`). The banner label is
  `LV_LABEL_LONG_WRAP` with a width capped to the panel, so a long name wraps onto
  more lines inside the floating chip instead of overflowing the right edge. The
  connected-page header title stays count-only: the header is height constrained,
  while the floating banner has room to wrap. The name never widens the layout or
  covers the shutter, focus or lock controls.
- **Pin the battery to the top-right.** The header battery block used to pack
  inline with the other status icons, so it shifted as the GPS icon and the
  reconnecting Bluetooth symbol showed and hid, and it drifted to the middle when
  the title was hidden (the sticks drop the title for header width). A zero-width
  flex-grow spacer between the title and the status icons now pins the whole
  icon-and-battery block to the header's right edge. The battery is the last
  child, so its right edge stays fixed no matter which sibling icons come and go.
  The spacer grows alongside the title when the title is shown, which is inert
  (the title text is left aligned and the icons stay right), so the visible layout
  is unchanged on every board while the battery no longer reflows.

## Dropping shutter and focus issued while a target is down

Fixing the BLE dead-link detection (the supervision-timeout cap, #143) made a
second bug visible on hardware: shutter presses made while the camera was off
fired when it reconnected. NimBLE writes correctly failed at the transport while
the link was down, but furble's own control command queue buffered the
`CMD_SHUTTER_PRESS` / `CMD_SHUTTER_RELEASE` and replayed them on reconnect,
because the reconnect runs synchronously on the control task and anything left on
the control queue during the drop is dispatched the instant the link is back.

The fix routes camera-action commands (shutter and focus, press and release)
straight to the per-target queues in `Control::sendCommand`, gated on each
target's live link, instead of parking them on the shared control queue:

- A press issued while a target is down is dropped for that target with a
  `LOGD`, never queued, so it cannot replay when the link returns.
- Delivery is per-target, not global. A camera still connected in a
  multi-connect session keeps firing even while another target is mid-reconnect
  and the control task is busy reconnecting it: the survivor's own task drains
  its queue independently of the control queue.
- `CMD_CONNECT` / `CMD_DISCONNECT` / `CMD_GPS_UPDATE` still flow through the
  control queue and the control task as before, so the connect, disconnect and
  geotag paths are unchanged. The normal (all-active) shutter path and the
  blind-remote path are unchanged: they call the same `sendCommand`, which now
  simply delivers to the connected targets directly.

## Multi-camera disconnection behavior (first class)

A multi-connect session (task #54 and task #55) must degrade per device, never
as a whole. Concretely, when camera A drops while camera B stays connected:

- **Drop detection is per device.** The control task leaves `STATE_ACTIVE` when
  `allConnected()` is false and re-enters the reconnect loop for A only; B keeps
  its live link the entire time. `simDropActiveLink(index)` models exactly this
  (`index >= 0` drops one target).
- **The indicator is non-blocking and per device.** No page takeover. The
  connected/trigger view stays up. The status-row reconnecting icon shows, and
  the title reads "Reconnecting (1/2)" so the user sees that one of two cameras
  is reconnecting while the other is still usable.
- **Shutter routing is per target.** A shutter pressed while A is down fires on
  B and is dropped for A. When A reconnects, the presses made while A was down do
  not replay on A. In aggregate terms (the sim's global shutter counter): two
  connected cameras add two per press, one-down adds one (the survivor only), and
  a reconnect adds nothing back.
- **Independent reconnect.** A reconnects on its own without disturbing B, and
  the session returns to "Connected" once both links are live again. An
  interactive or lost disconnect of one target does not tear down the others
  (relates to task #55, per-device disconnect).

### Multi-connect behavior

One camera dropping must not hide the trigger for cameras still connected. The
non-blocking design already keeps the trigger live for the whole session. The
reconnect flow only re-enters connecting for the dropped target while the
survivors keep their links, so the aggregate connected view stays up. The single
status indicator reflects that a reconnect is in progress without implying the
whole session is down.

### F4: one Cancel, one disconnect

The Cancel button handler is registered once when the connect message box is
built, not per connect attempt. Registering it in `doConnect` stacked one
callback per attempt, so after N attempts a single Cancel click fired N
disconnects. The existing F4 = Cancel (disconnect one) behavior is unchanged
otherwise.

## Test

Sim regression scenarios under `sim/scenarios/e2e/`:

- `reconnect-indicator.txt`: connect one FauxNY camera with reconnect on, drop
  the link, assert the connected page stays, the progress box stays hidden, and
  the reconnecting indicator appears then clears once the link is back.
- `reconnect-multiconnect.txt`: connect two FauxNY cameras, drop only one, assert
  the connected page and trigger stay up, one link stays live
  (`control.connected 1`), the progress box never takes over, and both links
  restore.
- `cancel-single-disconnect.txt`: prove one Cancel click fires exactly one
  disconnect regardless of prior connect attempts (F4).
- `drop-idle-self-heal.txt`: a drop with reconnect off parks the liveness timer
  instead of spinning on a torn-down link.

- `reconnect-indicator.txt` also asserts the status **text**: `ui.status_text`
  reads "Connected" while active, "Reconnecting" during the single-camera drop,
  and clears back to "Connected" on recovery.
- `reconnect-multiconnect.txt` also asserts the per-device count text:
  `ui.reconnect_count` reads `0/2` while both are connected and `1/2` while one
  is reconnecting (the rendered title is "Reconnecting (1/2)", printed for the
  record), then clears to `0/2`.
- `reconnect-multiconnect-shutter.txt`: two cameras connected, drop one, and
  assert the per-target shutter routing through the aggregate shutter counter:
  a press with both connected adds two, a press with one down adds one (the
  survivor fires, the dropped one is suppressed), the down-time press does not
  replay on reconnect, and a press once both are live again adds two.
- `remote-reconnect-indicator.txt`: connect one FauxNY camera, navigate to the
  full-screen Remote shutter page, drop the link, and assert the shutter-page
  banner shows (`ui.remote_reconnecting yes`, `ui.remote_status Reconnecting`)
  without leaving the page, then clears on recovery.
- `remote-reconnect-multiconnect.txt`: two cameras, open the shutter page, drop
  one, and assert the shutter-page banner shows with the per-device count
  (`ui.remote_reconnecting yes`, `ui.reconnect_count 1/2`), then clears once both
  are live again.

Host test additions:

- `control-e2e-reconnect-shutter-drop` (real `Furble::Control`, `tests/host/
  control_e2e/control_e2e.cpp`): connect a real Fujifilm virtual camera with
  infinite reconnect, force the link down and hold the reconnect open
  (`setConnectShouldFail`), fire a shutter while the target is down and assert no
  write reaches the peer, then let it reconnect and assert the down-time press
  never replays (zero shutter writes), while a shutter on the recovered link
  still fires. This is the authoritative regression for the buffered-replay bug
  because it runs the production control queue and per-target tasks.

Sim harness additions (all `FURBLE_SIM` gated, release firmware byte-unaffected):

- `Control::simDropActiveLink(int index)`: `index < 0` drops every active link;
  `index >= 0` drops only that target, so a multi-connect session can lose one
  camera and keep the rest live.
- `ui.reconnecting` query: whether the non-blocking indicator is showing.
- `ui.status_text` query: the current menu header title text ("Connected" /
  "Reconnecting" / "Reconnecting (i/n)").
- `ui.reconnect_count` query: the title's "i/n" count as a single token, tied to
  the same target counts the title formats from.
- `ui.remote_status` query: the Remote shutter page banner label text, or
  "hidden" when the banner is not showing.
- `ui.remote_reconnecting` query: token-safe "yes"/"no" visibility of the shutter
  page banner, so a multi-connect scenario can assert it without matching the
  spaced "Reconnecting (i/n)" text (pair with `ui.reconnect_count`).
- `ui.connect_box`, `ui.page`, `control.connected`, `control.targets`,
  `camera.shutter_presses` / `camera.shutter_releases` queries used above.

Reconnect UI polish additions:

- `Control::getDisconnectedName()`: name of the first target whose link is down,
  or "" when all are connected. Drives the banner's dropped-camera name.
- `ui.bt_color` query: "hidden" when the status-row Bluetooth symbol is not
  showing, "red" when the visible glyph is recolored red, "other" otherwise. The
  `reconnect-indicator` scenario asserts it is hidden while connected, red during
  the drop, and hidden again on recovery.
- `ui.remote_named` query: "yes" when the shutter banner text matches the
  dropped-camera name policy for the panel (name shown when one camera is down on
  a wide enough board, omitted on the narrow StickC or when several are down). It
  compares the actual banner text to the expected policy, so the same assertion
  holds on every board width and flips to "no" if the name is shown or dropped
  incorrectly. The `remote-reconnect-indicator` and `remote-reconnect-multiconnect`
  scenarios assert it.
- `ui.battery_pinned` query: "yes" when the header battery block sits flush at
  the status row's right edge. The new `battery-anchor.txt` scenario enables GPS,
  connects, drops (showing the reconnecting Bluetooth symbol) and recovers,
  asserting the battery stays pinned the whole time so it never reflows. Board
  independent, so it holds on both panels.

## Implementation state

Implemented on fix/connui. Non-blocking top-status indicator, `sessionEstablished`
gating, `showStatusIcon` changed-check guard, multi-connect one-link drop, and
the F4 single-Cancel fix are all in place. clang-format clean. Sim e2e scenarios
pass. Firmware builds for m5stick-s3.

Follow-up on fix/reconnect-ux-and-shutter-drop (stacks on the unmerged
supervision-timeout cap, #143): after hardware confirmed the icon appeared but
the title still read "Connected" and that shutter presses made during an outage
replayed on reconnect, two fixes were added.

1. Status text bound to the reconnect state (`updateReconnectTitle`): the
   connected page title reads "Reconnecting" / "Reconnecting (i/n)" during a
   drop and clears to "Connected" on recovery, guarded by the changed-check
   helpers. No new setting; always-on status.
2. Per-target routing of shutter and focus in `Control::sendCommand`: a press
   issued while a target is down is dropped for that target, never buffered on
   the control queue and never replayed on reconnect, while a still-connected
   camera keeps firing. The connect/disconnect/GPS paths and the blind-remote
   path are unchanged.

clang-format clean. All five release envs plus m5stick-s3-debug build. Host
ctests pass, including the new `control-e2e-reconnect-shutter-drop`. Sim e2e
scenarios pass, including the new multi-camera text and shutter-routing coverage.

PENDING HARDWARE RETEST on the M5StickS3, single and multi camera:
- Drop a live Fujifilm link mid-session: the trigger stays responsive, the
  reconnecting icon shows, and the title reads "Reconnecting" (or
  "Reconnecting (i/n)" with a second camera), clearing to "Connected" on
  reconnect.
- Press the shutter while the camera is off: it must NOT fire when the camera
  reconnects (no replay).
- Multi-connect: with two cameras, drop one and confirm the surviving camera's
  shutter still fires while the other reconnects, the dropped camera's press is
  suppressed and never replays, and the survivor is never interrupted.

Follow-up on feat/remote-reconnect-indicator (stacks on #149): the same
mid-session reconnect state now also drives a non-blocking banner on the Remote
shutter page (`updateRemoteReconnect`), a red Bluetooth icon plus
"Reconnecting (i/n)" text top-left, so the full-screen shutter view is no longer
blank when the link drops. clang-format clean. All five release envs plus
m5stick-s3-debug build. Host ctests pass. Sim e2e scenarios pass on both the
135x240 and 80x160 panels, including the two new `remote-reconnect-*` scenarios.

PENDING HARDWARE VISUAL CONFIRM on the M5StickS3: on the Remote shutter page,
drop a live Fujifilm link and confirm the red Bluetooth icon plus "Reconnecting"
text appears top-left without interrupting the shutter, and clears on reconnect.

Follow-up on feat/reconnect-ui-polish (stacks on #154): the three bench-session
refinements above. The status-row Bluetooth symbol is recolored red during a
drop; the shutter banner names the dropped camera where the panel is wide enough
(wrapped, never overflowing); and the header battery is pinned to the top-right
so it no longer reflows when sibling status icons come and go. New sim queries
`ui.bt_color`, `ui.remote_named` and `ui.battery_pinned`, plus the new
`battery-anchor.txt` scenario and extended `reconnect-indicator`,
`remote-reconnect-indicator` and `remote-reconnect-multiconnect` scenarios.
clang-format clean, no em-dashes. All five release envs plus m5stick-s3-debug
build. Host ctests pass. All sim e2e scenarios pass on both the 135x240 and
80x160 panels.

PENDING HARDWARE VISUAL CONFIRM on the M5StickS3: (1) the status-row Bluetooth
symbol turns red on a mid-session drop and clears on recovery; (2) the shutter
banner names the dropped camera on the 135 px panel without overflow; (3) the
battery indicator stays fixed in the top-right corner across connected,
reconnecting and GPS on/off states.

Follow-up on feat/bulb-reconnect-indicator (stacks on #154 and #156): the
full-screen Bulb page is the other operational page that hides the header status
row, and it was the last one still blank on a mid-session drop (flagged by the
per-page connection-state sim sweep as `page_banner` WILL_FAIL on Bulb). The same
reconnect state now drives an identical banner there. `updateRemoteReconnect` was
factored into a shared `updatePageReconnectBanner(banner, label, reconnecting)`
helper that it calls for both the shutter banner (`m_RemoteReconnect`) and the
new Bulb banner (`m_BulbReconnect`), so the two always agree and there is no
second copy of the per-target connection-state read. Because the helper is
shared, the Bulb banner inherits #156's dropped-camera name and width policy for
free (name shown when one camera is down and the panel is wide enough, wrapped in
a capped-width label). The banner is built on the Bulb page in `addBulbMenu`,
floating top-left, red Bluetooth icon over "Reconnecting" / "Reconnecting (i/n)"
text, icon-only under 110 px, and never covers the duration picker or Start
button. New sim queries `bulb_status` and `bulb_reconnecting` mirror
`remote_status` / `remote_reconnecting`, and a `nav bulb` action reaches the page
through its real menu button.

clang-format clean, no em-dashes. All five release envs plus m5stick-s3-debug
build. Host ctests pass (34/34). Sim e2e scenarios pass on both the 135x240 and
80x160 panels, including the new `bulb-reconnect-indicator` scenario; the
overflow sweep stays clean on the 80x160 panel.

PENDING HARDWARE VISUAL CONFIRM on the M5StickS3: on the Bulb page, drop a live
Fujifilm link and confirm the red Bluetooth icon plus "Reconnecting" text appears
top-left without covering the bulb controls, and clears on reconnect.
