# Focus outline dedup: single ring per selected item

## Motivation

Plan 76 made the keyboard focus ring a bold 3px outline so the selected item
reads in every theme. Hardware use then showed the ring over-drawing. A focused
menu row draws three rings: one around the whole row, one around its icon, and
one around its label. The reported fix is one ring around the whole item. On the
dark themes the primary-color fill already marks the selection, so the extra
rings are pure noise there too.

A second case has the same root: the Remote page shutter lock button draws its
own ring. It is the only focusable control on that page, so its focus is never
ambiguous and the ring is noise.

## Root cause

`addMenuItem` builds each row as an `lv_menu_cont` that holds an icon image and
a label, and sets `LV_OBJ_FLAG_STATE_TRICKLE` on the row. When the row is
focused, that flag propagates `LV_STATE_FOCUSED` down to the icon and label. The
theme apply callback in `UI::setTheme` attached the outline-bearing style to
every widget at `LV_STATE_FOCUSED`, so the icon and label each drew their own
ring on top of the row's ring.

The Remote page shutter lock (`m_ShutterLockIcon`) is an `lv_button`, so the same
apply callback gave it the outline at focus.

## Design

### Menu rows (firmware, all themes)

The theme apply callback no longer attaches the outline style to image and label
widgets. The ring stays on the focusable container (the menu row, plus buttons,
rollers, sliders, and switches), so a focused item shows exactly one ring. The
per-theme hue and the shared width, opacity, and pad from plan 76 are unchanged.
No setting and no wire protocol is touched.

### Remote page shutter lock (firmware)

After the shutter page is built, a local style zeroes the lock button's
`outline_width` for `LV_STATE_FOCUSED` and `LV_STATE_FOCUS_KEY`. A local style
overrides the theme's added style, so the lock shows no focus ring while every
other focusable control keeps its ring.

### Simulator instrumentation (sim only)

Two `FURBLE_SIM` seams make the change assertable without pixel diffing:

- `ui.focus_outline_count` reports how many widgets in the focused item's
  subtree currently render an outline. A focused icon menu row reports three
  before the fix and one after.
- `ui.lock_outline` reports the shutter lock's effective focus outline width,
  and the `focus-lock` scenario action focuses the lock so the query is
  meaningful. The sim always renders the touch remote layout, where key
  navigation does not land on this button, so the action focuses it directly.

`sim/scenarios/e2e/menu-focus-outline.txt` asserts `focus_outline_count` is one
on the home and settings menus and `lock_outline` is zero on the Remote page.

## Deviations and follow-ups

- The menu icons no longer lighten on focus. That lightening came from the same
  focus style the outline rode on. Removing the style from the icon drops both.
  The row ring and the primary-color fill still mark the selection, so this is
  an acceptable trade for a clean icon.
- The sim always reports touch enabled, so it renders the three-button touch
  Remote layout rather than the non-touch floating lock. The lock button is the
  same `m_ShutterLockIcon` in both layouts, so the fix and its assertion apply
  to both. On-device confirmation of the non-touch layout is still owed.

## Verification

- `sh sim/build.sh` green. Sim re-captured for the Default and Dark themes on
  the home menu, settings menu, and Remote page, before and after.
- The new `menu-focus-outline` scenario passes and fails as expected: it reports
  three outlines and a 3px lock ring against the pre-fix code, one and zero
  after. All 15 end-to-end scenarios pass.
- `FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e m5stick-s3-debug` green.
- The five release `sdkconfig.*` files are unchanged.
- On-device verification on the M5StickS3 is still owed before merge because the
  change is compiled into firmware.
