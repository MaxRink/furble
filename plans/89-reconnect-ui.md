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
- `ui.connect_box`, `ui.page`, `control.connected`, `control.targets`,
  `camera.shutter_presses` / `camera.shutter_releases` queries used above.

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
