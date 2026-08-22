# UI layout audit: walkthrough screenshot review

## Motivation

The UI walkthrough PR (#136) landed a real SDL-sim render of every page on the
M5StickS3 135x240 panel, in the Default and Dark themes. A vision review of all
29 screenshots against the layout code in `src/FurbleUI.cpp` was run to find
alignment and layout defects before an alpha release.

The headline finding is that the two most dramatic defects in the walkthrough,
the overflowing Remote page and the wrapped `NO BLE, NO` header on every page,
are both artifacts of how the sim captured the images, not firmware layout bugs
that appear on a real M5StickS3. The genuine device-visible defects are minor
and confined to a few narrow-panel pages.

This plan separates the two so the walkthrough can be corrected to show what the
device actually renders, and the small real defects can be fixed.

## Remote page verdict: sim artifact, not a firmware bug

The user flagged the Remote page (`docs/img/remote.png`, `docs/img/dark-remote.png`):
three oversized blue buttons overrun the panel, and the labels clip to `hutt`,
`ocu`, `hutt Lock`. Verdict: this is a sim rendering artifact. It does not appear
on a real M5StickS3.

Mechanism:

- The Remote page has two entirely separate layouts, chosen at build time by
  `M5.Touch.isEnabled()` in `addConnectedMenu`:
  - Touch branch, `src/FurbleUI.cpp:3172-3225`: a three-column grid
    (`remote_col_dsc` = `FR(1) FR(1) FR(1)`, line 3176) of three 64x64 buttons
    (`lv_obj_set_size(button, 64, 64)`, line 3211) with a label under each
    (`Shutter`, `Focus`, `Shutter\nLock`). Three 64px buttons plus grid gaps
    need roughly 192px of width. This layout targets the touch boards, the
    M5Stack Core2 at 320px, where it fits.
  - Non-touch branch, `src/FurbleUI.cpp:3226-3283`: no on-page buttons at all.
    The physical buttons drive the small floating `m_Left` / `m_OK` / `m_Right`
    indicators created at `src/FurbleUI.cpp:532-546` and sized to
    `ICON_HEADER_SIZE` (24px, `include/FurbleUI.h`), plus a small floating
    shutter-lock icon and a grey connector line. This is the layout a real
    M5StickS3 uses, because the StickS3 has no touch panel.
- The sim forces the touch branch. `sim/FurblePlatformSim.cpp:31-37`: "The SDL
  panel always attaches a mouse-driven touch device, so `M5.Touch.isEnabled()`
  is true regardless of the simulated board. A scenario can seed `no_touch true`
  to detach it." The walkthrough scenario did not seed `no_touch true`, so the
  sim built the 320px touch Remote layout and then rendered it into the 135px
  StickS3 frame. The 192px of buttons overflow the 135px panel and the labels
  clip.

So on real StickS3 hardware the Remote page shows the compact floating-indicator
layout, which does not overflow. The three-big-button layout that overflows is
only reachable on a touch board, and on its real target (Core2, 320px) it fits.

Fix direction: this is a walkthrough/scenario fix, not a firmware fix. The
StickS3 walkthrough should seed `no_touch true` before the Remote (and any other
touch-branched) capture so the screenshot matches the device. No product code
changes for the Remote page. A latent firmware note is recorded in section C in
case a narrow touch board is ever added.

## Header `NO BLE, NO` verdict: sim artifact

Every page shows a two-line, clipped title `NO BLE,` / `NO` at top left that
collides with the GPS and battery icons. This is also a sim artifact.

Mechanism: the window title text is `m_Title`, and in `FURBLE_RIG` (sim/rig)
builds it is the long string `RIG BUILD, NO BLE, NO ENCRYPTION`
(`include/FurbleUI.h:361-367`). On real hardware `m_Title` is `FURBLE_STR`
(the short product name) or `FURBLE_VERSION`. The header is a single short strip
whose height is pinned to `1.2f * lv_font_get_line_height(LV_FONT_DEFAULT)`
(`src/FurbleUI.cpp:483`), so any title too wide for the strip wraps to a second
line that spills below the strip and reads as an overlap. The real short title
fits on one line, so the overlap does not occur on device.

Fix direction: walkthrough fix. Build the walkthrough sim without the long
`FURBLE_RIG` title, or override `m_Title` to the product name for captures, so
the header shows the real title. No product code change required for correctness
on device. A minor latent-robustness note is in section C.

## A. Genuine firmware layout defects (ranked by device severity)

These are visible on a real M5StickS3 and warrant firmware fixes.

### A1. Bulb Duration units roller sits against the right edge (low-medium)

Evidence: `docs/img/bulb.png`. The right-hand units roller (`msec` / `sec` /
`min`) is pushed hard against the right panel edge and its text is clipped. The
Bulb Duration page is not touch-branched, so this reproduces on device.
Likely cause: the duration picker roller row has no right padding or margin
reserve for the units column at 135px. Fix: add a small right pad or reduce the
digit-roller column widths so the units roller clears the edge. Effort: small.
Needs per-board validation: yes, worse at 80x160, check 320x240 is unaffected.

### A2. Timer row label truncates ("Shutter count" to "Shutter c") (low-medium)

Evidence: `docs/img/settings-timer.png`, row reads `Shutter  c   30`. In
`addSpinItem` (`src/FurbleUI.cpp:3918-3949`) the row is `LV_FLEX_FLOW_ROW_WRAP`
with `spinner.m_Label` carrying no long-mode and, on non-CoreX boards, no
`flex_grow`, while `spinner.m_Value` has `flex_grow` 1 and circular scroll
(lines 3928-3938). The value expands and squeezes the plain label, which then
clips rather than scrolls. Fix options: shorten the label ("Shutter" instead of
"Shutter count"), or give the label the same circular-scroll long-mode as the
value, or let the label flex-grow and the value size to content. Effort: small.
Needs per-board validation: yes, tightest at 80x160.

### A3. Long setting labels rely on marquee scroll on the narrow panel (low)

Evidence: `docs/img/settings-gps.png` (`Baud 115200`),
`docs/img/settings-power.png` (`Sleep when...`),
`docs/img/settings-bluetooth.png` (`Active Connection`). These toggle rows are
built by `addSettingItem` (`src/FurbleUI.cpp:1473-1489`) with the label in
`LV_LABEL_LONG_SCROLL_CIRCULAR` (line 1486) and a switch beside it. At 135px the
labels are wider than their column, so they marquee-scroll continuously. This is
by-design and readable, but constant scrolling on several rows is distracting on
a small screen. Note: the walkthrough caught these mid-scroll, which exaggerates
the effect (see section B3). Optional fix: shorten the setting display names so
common ones fit statically at 135px. Effort: small, copy-only. Needs per-board
validation: yes, since 80x160 fits even less.

## B. Sim / walkthrough capture artifacts (not firmware defects)

These make the screenshots misrepresent the device. Fix the walkthrough
harness, not the firmware.

### B1. Remote page renders the touch layout (high visual impact)

`docs/img/remote.png`, `docs/img/dark-remote.png`. Cause and fix in the Remote
verdict above. Seed `no_touch true` for StickS3 captures of touch-branched
pages so the walkthrough shows the floating-indicator Remote layout the device
uses.

### B2. Header shows the long RIG title on every page (high visual impact)

All pages. Cause and fix in the header verdict above. Capture with the product
`m_Title`, not the `FURBLE_RIG` `RIG BUILD, NO BLE, NO ENCRYPTION` string.

### B3. Labels captured mid-marquee look truncated (medium visual impact)

`docs/img/settings-gps.png` (`ud 1152`), `docs/img/dark-settings-gps.png`,
`docs/img/settings-bluetooth.png` (`:ion po`), `docs/img/settings-power.png`
(`Sleep w`). These are `LV_LABEL_LONG_SCROLL_CIRCULAR` labels frozen partway
through their scroll animation, so they read as clipped on both ends. On device
they animate and reveal the full text. Fix: before each capture, let the
marquee settle to its start (or pause LVGL animations) so labels render from
their first character. The underlying "labels are long for 135px" point is the
real, low-severity A3 above.

### B4. Sim placeholder content (no impact, note only)

`docs/img/settings-about.png` (`Version: sim`, `ID: furble-sim`, `IDF: sim`),
`docs/img/diag-device-info.png` (`sim rev 0.0`). These are sim stub values, not
layout defects. Layout on these pages is clean. No action.

## C. Latent robustness notes (optional, not device bugs today)

- The touch Remote grid at `src/FurbleUI.cpp:3172-3225` assumes a wide panel.
  If a narrow touch board is ever added, the three 64px buttons will overflow.
  A guard (smaller buttons or a 3x1 to 2x2 reflow below a width threshold) would
  future-proof it. Not needed for current hardware.
- The window header height is pinned to one line (`src/FurbleUI.cpp:483`) and
  the title has no `LV_LABEL_LONG_DOT` ellipsis, so an over-long title wraps and
  spills. Setting the title label to ellipsis long-mode would degrade gracefully
  instead of overlapping. Cosmetic only, since the real title is short.

## Non-defects confirmed clean

`main`, `dark-main`, `connected`, `dark-connected`, `connecting`, `gps-data`,
`gps-nmea`, `settings`, `dark-settings`, `settings-display`, `settings-features`,
`dark-features`, `settings-theme`, `settings-text-size`, `settings-diagnostics`,
`diag-device-info`, `diag-power-state`, `diag-ble`, `battery`. Rows align, focus
outlines wrap the whole item (matching the agreed focus-outline preference),
label and value columns line up, icons sit centered with their text. The only
recurring blemish on these pages is the section B header artifact.

## Scope and sequencing

- Section B (walkthrough harness) is the highest value and lowest risk. It makes
  the walkthrough truthful. It touches sim/scenario files and docs, no product
  code.
- Section A (firmware) is small, copy-and-padding level changes, each requiring
  a sim screenshot check at 80x160, 135x240, and 320x240 before hardware.
- Section C is optional future-proofing.

This document is a proposal. No product or sim code is changed by it.
