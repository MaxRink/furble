# 92 - Better suited menu icons

Status: implemented. Stacks on 91 (feat/ui-polish, PR #91) for the sim theme
and board screenshot tooling used to capture before/after.

## Motivation

Several settings pages reused one generic glyph. `icon_settings_remote` (a
handset remote) stood in for Bluetooth, Transmit power and Feedback, and `About`
and `Diagnostics` shared `icon_info`. On the StickS3 and M5Stack menus, where
icons render, this made unrelated pages look identical. This change gives those
entries a glyph that matches what the page does.

## What changed

Four settings entries get a dedicated icon:

| Settings entry | Before | After |
|---|---|---|
| Bluetooth | `icon_settings_remote` | `icon_bluetooth` |
| Transmit power | `icon_settings_remote` | `icon_cell_tower` |
| Feedback | `icon_settings_remote` | `icon_notifications_active` |
| Diagnostics | `icon_info` (shared with About) | `icon_troubleshoot` |

Infrared keeps `icon_settings_remote`; a handset remote is the right glyph for
an IR remote. About keeps `icon_info`.

## Cheap wins investigated

The only nominally unused generated icons on this base are `icon_clear_all_24`,
`icon_remote_gen_24`, `icon_restart_alt_24` and `icon_save_24`. All four are
24px button-glyph variants with no fitting menu slot, so none was wired.
`icon_my_location` is already used for the GPS-fix header state, so there was no
wire-an-unused-icon win to take.

## Pipeline

Icons come from Material Symbols (Apache-2.0), weight 300, grade 0, no fill.
Source SVGs live in `components/icons/svg/`. Each is rendered to a 48x48 PNG
(menu size) and a 24x24 PNG (`_24`, StickC size), then converted with
`LVGLImage.py --ofmt C --cf RGB565A8 --compress LZ4`, matching the existing
icons byte format (RGB565A8, LZ4 compressed). Arrays are clang-formatted.

Each new base name is aliased to its `_24` variant inside the StickC-family
guard in `FurbleUI.cpp`, matching every other menu icon, so the small screens
draw the 24px art.

## Verification

- `m5stick-s3` firmware build: SUCCESS.
- sim before/after screenshots captured for the StickS3 across all three
  themes (Dark, Default, Mono Furble). StickC suppresses menu icons by design
  (screen too small), so it shows no icon change.
- On-device M5StickS3 check still owed before merge.
