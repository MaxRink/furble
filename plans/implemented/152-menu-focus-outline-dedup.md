# 152 - Drop the focus ring on menu rows that already carry the accent fill

## Issue

User report: "We dont need the green border highlighting for stuff we already
highlight in the accent colour when selecting."

Focused menu rows show two highlights at once. The LVGL default theme fills a
focused `lv_menu_cont` with the primary accent colour (see
`lv_theme_default.c`, the `LV_STATE_FOCUS_KEY` style added to
`lv_menu_cont_class` uses `bg_color_primary`). On top of that fill, furble's
theme apply callback attaches `style_button`, which carries a bold 3 px
outline (green in the Dark theme, white in Mono Furble, orange in Default), to
every non-image non-label object on `LV_STATE_FOCUSED`. Menu rows match that
condition, so every selected row draws both the accent fill and the ring.

## Fix

Exclude `lv_menu_cont_class` from the `outlineTarget` condition in the theme
apply callback in `src/FurbleUI.cpp`. The accent fill remains the single focus
cue on menu rows.

The ring stays everywhere it is the only cue. Switches, rollers, sliders and
plain buttons get no focus fill from the LVGL theme, so they keep
`style_button` on `LV_STATE_FOCUS_KEY` (and on `LV_STATE_FOCUSED` through the
`outlineTarget` branch, which still matches them).

The change applies to all three themes, since the outline style is shared and
only its colour differs per theme.

## Verification

- Sim scenario `sim/scenarios/e2e/menu-focus-outline.txt` updated: the
  `ui.focus_outline_count` asserts on focused home and settings menu rows now
  expect 0 (previously 1). The `ui.lock_outline 0` assert is unchanged and
  still passes.
- Full headless sim scenario suite run, including
  `shutter-indicator-focus.txt`.
- Sim screenshots on the m5stick-s3 and m5stick-c panels: Dark and Default
  theme main menu with a focused row (accent fill, no ring) and a settings
  control page (ring still present on switches).
- Full host test suite green.
- On-device visual check on the M5StickS3 is owed before merge.

## Implementation state

Implemented as planned. No deviations.
