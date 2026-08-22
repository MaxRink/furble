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

Sim harness additions (all `FURBLE_SIM` gated, release firmware byte-unaffected):

- `Control::simDropActiveLink(int index)`: `index < 0` drops every active link;
  `index >= 0` drops only that target, so a multi-connect session can lose one
  camera and keep the rest live.
- `ui.reconnecting` query: whether the non-blocking indicator is showing.
- `ui.connect_box`, `ui.page`, `control.connected`, `control.targets` queries
  used by the scenarios above.

## Implementation state

Implemented on fix/connui. Non-blocking top-status indicator, `sessionEstablished`
gating, `showStatusIcon` changed-check guard, multi-connect one-link drop, and
the F4 single-Cancel fix are all in place. clang-format clean. Sim e2e scenarios
pass. Firmware builds for m5stick-s3.

No on-device text hint was added. The indicator is a small Bluetooth icon in the
status row next to the other status icons, which reads as self-explanatory. No
new setting was added: the indicator is always-on status, not a configurable
feature.

PENDING HARDWARE RETEST: the non-blocking indicator must be confirmed on the
M5StickS3. Drop a live Fujifilm link mid-session and confirm the trigger stays
responsive, the indicator shows during the retry, and it clears on reconnect.
For multi-connect, confirm dropping one camera leaves the trigger live for the
others. The first (page-takeover) approach was hardware-observed; this rework
changes the on-screen behavior and needs a fresh on-device pass.
