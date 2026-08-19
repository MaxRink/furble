# PR70: UI text size setting and layout audit walker

## Motivation

Two complaints drive this change and they pull in opposite directions.

The primary user has good eyes and wants smaller text. The default fonts
spend scarce pixels on legibility margins this user does not need, and on the
135x240 sticks that costs visible rows in every roller and menu.

At the same time there are overlap bug reports: on some screens label text
already collides with neighboring widgets at the default size. Any text
scaling feature multiplies that risk, because every size that can be selected
is a layout that must not clip. So the setting ships together with a layout
audit walker that makes collisions and clipped labels measurable instead of
anecdotal.

## Design

A `TEXT_SIZE` setting with three values: Small, Normal, Large. Normal keeps
the current per-board default font, so defaults keep current behavior. The
theme is initialized with the mapped font at UI start.

### Per-board font mapping

`fontForTextSize` maps the setting to a Montserrat font per board class.
Normal is `LV_FONT_DEFAULT` from the board's sdkconfig.

| Board class | Small | Normal (default) | Large |
|---|---|---|---|
| M5StickC (80x160) | montserrat 10 | montserrat 12 | montserrat 14 |
| M5StickC Plus, M5StickS3 (135x240) | montserrat 12 | montserrat 16 | montserrat 22 |
| M5Stack Core, Core2 (320x240) | montserrat 16 | montserrat 22 | montserrat 28 |

`fontForIconMenu` styles the labels under the 64x64 icon grid buttons and the
Core menu item labels. It uses montserrat 16 and Large may only grow it,
never shrink it below that default: the Large font is used only where its
line height exceeds 16. On the M5StickC this clamps Large back to 16, because
the board's Large text font (14) is smaller than the icon menu default.

The three non-default sizes per board are enabled in all five release
sdkconfig files consistently.

### Restart semantics

The font is baked into the LVGL theme at UI construction, and re-theming a
live widget tree is not supported by the UI. The Text size page therefore
follows the Theme page precedent exactly, including its placement: it is a
top-level entry in the Settings menu next to Theme, not nested inside the
Display page. A roller picks the size, a Restart button saves the setting and
calls `esp_restart()`. On the M5StickS3 the
handler disarms the M5PM1 hardware watchdog first
(`Platform::getInstance().watchdogEnable(false)`), the same guard the Theme
restart uses, because a restart with the watchdog armed trips it during boot.

### Layout audit walker

`src/FurbleUIAudit.cpp`, compiled only for `FURBLE_SIM` or `FURBLE_CONSOLE`.
It walks the active screen and reports, as JSON Lines, labels whose rendered
text exceeds their content box and labels that intersect visible siblings.
Details and the output schema live in `tools/ui-audit.md`. The console
command `ui audit` runs it on hardware; the simulator can call
`Furble::UIAudit::dump` directly.

Walker rules, after review:

- Labels with long mode scroll, scroll circular, or dots overflow by design
  and are skipped by the clipped check. They previously produced 21
  guaranteed false positives per full-tree run.
- Wrap labels are measured as multi-line: text is wrapped at the label's
  content width and clipping is reported on height, not width.
- Width comparisons use `lv_obj_get_content_width`, so padding does not
  produce false positives, and coordinates use LVGL 9's `int32_t`.

## Dependencies

Fork PR #44 (screenshot CI) builds on this audit and navigation work: the
scripted screen walks feed both the screenshot comparison and the audit
reports.

## Deviations

- The Text size page gets the `icon_clear_all_24` icon (three text-like
  lines). It is the only registered icon that reads as text and it exists
  only in the 24 px variant; menu icons render with
  `LV_IMAGE_ALIGN_STRETCH`, so it scales on the Core boards.
- The Text size page has no title label. The Theme page precedent has none
  either, and the upstream preference is fewer UI elements.
- The roller width uses the same `#if !defined(FURBLE_M5COREX)` guard as the
  Theme menu roller.
- Text size is a top-level Settings entry, not a row on the Display page. An
  earlier revision nested it under Display; that added one row and pushed the
  Display page 36 px past the 135x240 viewport (worse on 80x160), which the
  `overflow-sweep` regression asserts must not happen. Placing it next to
  Theme restores the Display page fit and matches the Theme restart pattern.
  On the Core grid it takes the free cell `{2, 2}`.
- The worked examples in `tools/ui-audit.md` are illustrative, not captured
  from a run. The simulator (plans/28) is not part of this branch, so a
  cheap real-run regeneration was not available.

## Implementation state

Implemented on `feat/ui-text-scaling`.

- `TEXT_SIZE` has wire_id 40 (after `SD_GPX`, 39). `src/FurbleCompanion.cpp`
  covers it as SETTING_U8.
- Text size is a top-level Settings entry next to Theme. The Display page is
  unchanged from master and stays within the 80x160 and 135x240 viewports.
- `sim/scenarios/e2e/text-size-persist.txt` seeds a non-default size, boots,
  and asserts the stored setting, the roller selection and no page overflow.
  The `overflow-sweep` bughunt scenario covers the Display page fit across all
  three panel classes.
- Review fixes applied: S3 watchdog disarm before restart, the
  `fontForIconMenu` clamp, the audit walker long-mode and wrap rules,
  content-width comparisons, `int32_t` coordinates, the icon and title-label
  cleanup on the Text size page, and the roller width guard.

## Hardware pending

Verified on M5StickS3 hardware before merge:

- Text size Small, Normal, Large each render the Settings tree without
  clipping after restart.
- The Restart button restarts cleanly with the watchdog enabled in settings
  (no watchdog trip during boot).
- `ui audit` over the USB console reports zero issues on the main menu and
  Settings pages at all three sizes.

Not hardware verified (no device available): M5StickC, M5StickC Plus,
M5Stack Core, Core2. Those five-board font mappings are covered by code
review and the release builds only.
