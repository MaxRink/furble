# 301 - simulator on-screen buttons

Status: implemented on branch `feat/sim-ui-buttons` (2026-11-04). Do not merge
from this worktree.

## Goal

Let a person drive the SDL simulator's modeled physical buttons by clicking a
small control window, so an interactive session does not require remembering
the `a`/`b`/`c`/`p` keyboard mapping. The control surface must use the same
input seam the keyboard already uses, must not exist in scripted or headless
runs, and must not change any production firmware source.

## Board mapping and top-view layout

The button set follows the same board macros as `sim/driver.cpp` and
`sim/FurblePlatformSim.cpp`:

| Modeled board | Window title | A | B | Third button | GPIOs | Keyboard |
| --- | --- | --- | --- | --- | --- | --- |
| M5StickC / M5StickS3 | `... - Stick: B top, A front, PWR left` | front/bottom edge | top edge | PWR on the left side | 39, 38, 36 | `a`, `b`, `p` |
| M5Stack Core (`FURBLE_M5COREX`) | `... - Core front: A B C` | front edge | front edge | C on the front edge | 39, 38, 37 | `a`, `b`, `c` |

The pins are the ones M5Unified's PC build samples in `M5Unified.cpp` and the
ones the existing `Panel_sdl` keyboard mapping drives. A click therefore writes
the same `lgfx::gpio_lo`/`gpio_hi` state as the matching keyboard letter; it is
not a second firmware input path and does not call `UI::simPressButton`.

The control window draws a simple top view of the device body and screen, with
`TOP` and `FRONT` captions. Sticks are a tall outline with B above the top
edge, A below the front edge and PWR beside the left edge. Cores are a wide
outline with A/B/C in a row under the front edge. The outline makes the
physical positions obvious rather than presenting three anonymous buttons in a
row.

## Design

`sim/ui_buttons.{h,cpp}` owns one small SDL window (320x340 logical points).
The window is created from `sim/main.cpp` on the SDL main thread on the first
panel-loop tick, after the panel window exists. No M5GFX file is edited.

- Mouse down inside a button calls `lgfx::gpio_lo(pin)` and captures the mouse;
  mouse up calls `lgfx::gpio_hi(pin)`. A held button keeps its GPIO low and
  keeps the amber pressed rendering, so hold/release is modeled rather than a
  click-only shortcut.
- One click is one press/release transition. `press()` is idempotent for the
  held button, a per-button 120 ms debounce ignores duplicate down events, and
  `releasePress()` is idempotent. A left-button up releases even when its
  `windowID` is not the control window, so a drag that ends over the panel
  cannot leave the GPIO latched. Drag out of the window, focus loss, hidden
  window, close, and the periodic latch guard (`SDL_GetMouseState` no longer
  sees the left button, or mouse focus left the window) also release.
- A process-level `SDL_SetEventFilter` consumes events for the control window
  only. `Panel_sdl` never sees them, so its own polling loop is untouched. The
  previous filter, if one exists, is restored on close.
- Closing the control window releases any held button, removes the filter and
  destroys only that window. Closing the panel window still exits the
  simulator through the existing path.
- Hit testing uses SDL window points. The renderer uses
  `SDL_RenderSetLogicalSize` with the same logical size, so a Retina 2x
  drawable does not move the hit boxes away from the drawn buttons.
- The control window is destroyed in `uiButtonsShutdown()` before
  `Panel_sdl::close()` runs.

## Placement

The control window is placed beside the panel first (right, then left), then
below, then above, always with a 16 px gap. Placement reads the panel's frame
including the title bar (`SDL_GetWindowBordersSize`, with a macOS title-bar
fallback) and the display's usable bounds, then clamps into those bounds. It
therefore does not cover the panel or the panel's own button-position overlays.
When even the fallback cannot clear the panel on a very small display, the
window goes to the bottom-right corner rather than over the panel's top
overlays.

## Opt-out and headless behavior

The surface is created only when all of these hold:

- `Sim::interactiveRun()` is true: no `--script` file and no fuzzer.
- `FURBLE_SIM_BUTTONS` is unset, empty, or not one of `0`, `false`, `no`, `off`.
- The active SDL video driver is not `dummy` or `offscreen`.
- `SDL_CreateWindow`, the renderer and the event filter all succeed.

A headless run, a scripted run, a fuzz run, or a run with
`FURBLE_SIM_BUTTONS=0` behaves exactly as before and creates no second window.
If a visible run cannot create the window, the simulator prints one diagnostic
and continues without the control surface.

## Interactive display wake

Interactive runs advance the virtual clock far faster than wall time, so the
firmware's 60 s inactivity timeout can expire between two human clicks and
swallow every press as a display wake. While the control window exists,
`sim/driver.cpp` triggers LVGL display activity from the UI task. Script,
headless and fuzz runs never create the window and still exercise the real
inactivity, dim and sleep paths, including the first-press wake behavior. The
keepalive is simulator-only and does not change production display policy.

## Files

- `sim/ui_buttons.cpp`, `sim/ui_buttons.h` - the control window, top-view
  layout, event filter, pressed-state rendering and GPIO injection.
- `sim/main.cpp` - creates and services the window on the SDL main thread.
- `sim/driver.cpp`, `sim/driver.h` - `interactiveRun()` and the activity
  keepalive.
- `sim/CMakeLists.txt` - documents that the `sim/*.cpp` glob picks up the new
  translation unit; `sim/build.sh` already globs the same directory.
- `docs/sim.md`, `sim/CLAUDE.md` - user and directory-local contracts.

No production `src/` or `lib/furble/` file changes, and no simulator self-test
hook in `src/`.

## Validation

- Build the three modeled panel classes with `sim/build.sh` (M5StickS3,
  M5StickC, M5Stack Core).
- Run the existing headless e2e suite with `SDL_VIDEODRIVER=dummy`; the
  control window must not be created and the suite result must be unchanged.
- Visible smoke on the development Mac: capture the control window to confirm
  the Stick top view (B top, A front, PWR left), the device outline and the
  `TOP`/`FRONT` captions; capture the panel to confirm the control window is
  beside it and the panel overlays remain visible. A synthetic left-button
  press moved the panel focus exactly as the scenario `btn` verb does, which
  proves the event filter, hit box, GPIO and firmware path. The pressed-state
  fill is drawn by the same code path and is covered by the normal render, but
  the final top-view pressed frame was not captured separately.
- OS-level mouse automation on the shared desktop was not available without an
  accessibility grant, so the final manual gate is a real human click on the
  control window on a Mac with a display.

## Remaining manual gate

A human should launch `sim/build/furble-sim`, click A/B and the board's third
button, hold one to confirm the pressed state and release, drag out of the
window while holding to confirm the GPIO releases, and close the control
window without disturbing the panel. This is interactive visual verification
only; the headless suite does not and cannot cover it.
