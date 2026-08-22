# UI polish: focus highlight and sim theme/board capture

## Motivation

A full themes x boards x pages screenshot sweep of the LVGL UI found one clear
cross-theme defect. The keyboard focus ring inherits the LVGL default theme
width and opacity because `UI::setTheme` only sets the ring color, never its
width or opacity. On the two dark themes the thin ring still reads because it
sits on black. In the light Default theme the ring is orange on a white
background and a focused switch, roller, or slider is hard to see.

Producing that sweep also exposed a tooling gap. The simulator was hardwired to
the M5StickS3 135x240 board and to whatever theme NVS happened to hold, so it
could not render the M5StickC 80x160 or M5Stack 320x240 classes, or the Dark
and Mono Furble themes, without editing source by hand. A themes x boards
screenshot matrix is a project requirement for UI review, so the simulator has
to be able to render every combination.

## Design

### Explicit focus ring (firmware, all themes)

`UI::setTheme` now sets `outline_width`, `outline_opa`, and `outline_pad` on the
shared focus style before the per-theme `outline_color`. The ring is a bold 3px
fully opaque outline in every theme. The per-theme hue is unchanged: chartreuse
on Dark, white on Mono Furble, orange on Default. This is the only firmware
change. It touches no setting and no wire protocol. Menu buttons keep their
existing primary-color fill on focus; the ring change is what rescues the
switch, roller, and slider rows in the light theme.

### Simulator theme selection

`sim/main.cpp` reads `FURBLE_SIM_THEME`. When set, it seeds `Settings::THEME`
before the UI is constructed, so the theme applies on the single boot the same
way the roller Restart button would. Unset keeps the previous behavior. This is
sim only.

### Simulator board selection

`sim/build.sh` reads `FURBLE_SIM_FURBLE_BOARD` and `FURBLE_SIM_M5GFX_BOARD`.
They default to the M5StickS3 values, so CI and a plain `sh sim/build.sh` are
byte-for-byte unchanged. Override both to build the 80x160 M5StickC
(`FURBLE_M5STICKC` / `board_M5StickC`) or the 320x240 M5Stack
(`FURBLE_M5COREX` / `board_M5Stack`) panel class. The board is a compile-time
choice in M5GFX, so each class needs its own build.

## Deviations and follow-ups

- The reported Level page icon and collision are not in scope here. That page
  does not exist on the base branch (see 71-ui-bug-batch.md).
- The `ui-screenshots.txt` route is tuned to the S3 menu geometry. On the Core
  320x240 board the main menu is a horizontal icon grid, so the S3 key counts
  mis-navigate past the menu pages and the run exits early through a control
  that calls `esp_restart` (the sim shims that to exit). Core capture is
  reliable for the main menu only. A Core-specific route is a follow-up.
- Several leaf menu entries have no icon (GPS Power, Feedback events, several
  diagnostics rows). Adding icons is a manual pipeline (Material Symbols SVG,
  inkscape to 64x64 PNG, then LVGLImage.py) with no source assets committed, so
  it is deferred rather than half-done. Wiring the already-generated but unused
  icons (for example `icon_my_location`) to matching entries is a cheap
  follow-up.
- Setting-row labels use `LV_LABEL_LONG_SCROLL_CIRCULAR`, so a narrow-screen
  label that looks clipped in a still is a marquee caught mid-scroll, not a
  layout bug.

## Verification

- `sh sim/build.sh` green. Sim rebuilt and re-captured for all three boards and
  all three themes, before and after.
- `FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e m5stick-s3` green.
- The five release `sdkconfig.*` files are unchanged.
- On-device verification on the M5StickS3 is still owed before merge because the
  change is compiled into firmware.

## Follow-up: retained focus outline on the shutter button indicators

### Motivation

Hardware use on the M5StickS3 showed the same chartreuse focus ring leaking onto
the on-screen physical-button indicators (`m_Left`, `m_OK`, `m_Right`). In
SHUTTER mode the indicators are pressed, so the green outline appears on press
and then stays after the button is released. The indicators are pure hints and
must never carry a focus ring, so the retained highlight reads as a regression.

### Root cause

The indicators are `lv_button` widgets. Every base object is created with
`LV_OBJ_FLAG_CLICK_FOCUSABLE`. In SHUTTER mode the physical buttons drive the
indicators through a `LV_INDEV_TYPE_BUTTON` device mapped to each widget centre.
On press LVGL runs `indev_click_focus`, which for a click-focusable object with
no group sends it `LV_EVENT_FOCUSED` and only clears that focus on the next
press of a different object. The indicators were already removed from the
encoder group, but they kept the click-focus flag, so a press latched
`LV_STATE_FOCUSED`, the `style_button` chartreuse outline drew, and it lingered
past release.

### Fix

Drop `LV_OBJ_FLAG_CLICK_FOCUSABLE` from the three indicators right where they are
already removed from the encoder group. Click focus then skips them entirely, so
no `LV_EVENT_FOCUSED` is ever sent and no outline latches. No focusable widget or
focus ring is added; the indicators keep their position and their press, release
and click handlers unchanged. This is the only firmware change.

### Verification

- New host regression `sim/scenarios/e2e/shutter-indicator-focus.txt` enters
  SHUTTER mode and replays LVGL's exact click-focus flag gate through the
  `indicator-click-focus` sim action, then asserts `ui.indicators_focused` is
  `no`. The scenario fails on the pre-fix code (flag still set, state latches)
  and passes with the flag dropped. The action and the `indicators_focused`
  observer are `FURBLE_SIM`-gated, so firmware builds are unchanged.
- `sh sim/build.sh` green, `run-e2e.sh` green.
- `FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e m5stick-s3-debug` green.
- PENDING HARDWARE RETEST: visually confirm on the M5StickS3 that the green
  outline no longer appears on the shutter indicators, or clears immediately on
  release.
