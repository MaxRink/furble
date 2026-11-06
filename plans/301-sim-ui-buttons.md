# 301 - simulator on-screen buttons

Status: merged on master with PR #359 (2026-11-04). The review follow-up
applied left-first flush placement, the event-filter action queue for
cross-thread safety, the explicit `scriptProvided` sentinel, sentinel
initialization for hover/pressed/`lastChange`, the `btn-hold` repeat-timing
scenario, and the root README plus wiki docs.

## Goal

Let a person drive the SDL simulator's modeled physical buttons by clicking a
small control window, so an interactive session does not require remembering
the `a`/`b`/`c`/`p` keyboard mapping. The control surface must use the same
input seam the keyboard already uses, must not exist in scripted or headless
runs, and must not change production behavior. The only firmware source touch
is a narrow `FURBLE_SIM` guard for deterministic encoder timing.

## Board mapping and top-view layout

The button set follows the same board macros as `sim/driver.cpp` and
`sim/FurblePlatformSim.cpp`:

| Modeled board | Window title | A | B | Third button | GPIOs | Keyboard |
| --- | --- | --- | --- | --- | --- | --- |
| M5StickC / M5StickS3 | `... - Stick: B top, A front, PWR left` | front/bottom edge | top edge | PWR on the left side | 39, 38, 36 | `a`, `b`, `p` |
| M5Stack Core (`FURBLE_M5COREX`) | `... - Core front: A B C` | front edge | front edge | C on the front edge | 39, 38, 37 | `a`, `b`, `c` |

The pins are the ones M5Unified's PC build samples in `M5Unified.cpp` and the
ones the existing `Panel_sdl` keyboard mapping drives. A click and the matching
keyboard letter therefore write the same `lgfx::gpio_lo`/`gpio_hi` state. The
scenario `btn` verb is a different simulator seam: it injects through
`UI::simPressButton` on the UI task and does not drive these GPIOs.

The control window draws a simple top view of the device body and screen, with
`TOP VIEW` and `FRONT` captions. Sticks are a tall outline with B above the top
edge, A below the front edge and PWR beside the left edge. Cores are a wide
outline with A/B/C in a row under the front edge. The window is 300 points wide
and its height matches the modeled panel's content height (240 for StickS3 and
Core, 160 for StickC), so the two content areas share a top and bottom edge when
placed side by side. B is always centered on the body's top edge, never on the
right edge, and the `TOP VIEW` caption sits directly above it so a strip placed
to the right of the panel still reads as a top view. The panel keeps its own
title bar because M5GFX owns that window.

## Design

`sim/ui_buttons.{h,cpp}` owns one small SDL window (300 points wide, with the
height matched to the panel). The window is created from `sim/main.cpp` on the
SDL main thread on the first panel-loop tick, after the panel window exists. No
M5GFX file is edited.

- Mouse down inside a button calls `lgfx::gpio_lo(pin)` and captures the mouse;
  mouse up calls `lgfx::gpio_hi(pin)`. A held button keeps its GPIO low and
  keeps the amber pressed rendering, so hold/release is modeled rather than a
  click-only shortcut.
- One click is one press/release transition. `press()` is idempotent for the
  held button, a per-button 120 ms debounce ignores duplicate down events, a
  down/up pair drained in one UI tick is held for at least 30 ms so the
  firmware's 10 ms pin debounce still sees one press, and `releasePress()` is
  idempotent. `lastChange` starts at a `UINT32_MAX`
  sentinel and `hover`/`pressed` start at -1, so the first click after SDL init
  is not rejected by the debounce. A left-button up releases even when its
  `windowID` is not the control window, so a drag that ends over the panel
  cannot leave the GPIO latched. Drag out of the window, focus loss, hidden
  window, close, and the periodic latch guard (`SDL_GetMouseState` no longer
  sees the left button, or mouse focus left the window) also release.
- SDL2 can invoke the process event filter on the thread that pushes an event,
  not only the thread that polls, and `SDL_PushEvent` calls it synchronously.
  The filter therefore never touches the draw state: it records a small action
  in a queue, and the SDL main thread applies the actions in `uiButtonsTick()`.
  The queue lock is held only for the copy in and out, never across SDL calls,
  so a push from another thread or the main thread itself cannot race `draw()`
  or self-deadlock.
- Under `FURBLE_SIM`, `UI::initInputDevices` sets the three encoder indevs to a
  1000 ms long-press time and a 250 ms repeat interval. A normal click is one
  press/release; repeats begin only after holding for about one second. This is
  a simulator-only input-timing seam; firmware timing is unchanged.
- `UI::simHoldButton` and the `btn-hold NAME MS` scenario verb sample the same
  LVGL input device every 20 ms of virtual time. The `button-hold-repeat`
  scenario proves a tap and a 600 ms or 900 ms hold each move one row, while a
  1600 ms hold crosses the one-second threshold and both 250 ms repeat ticks.
- A visible interactive run with the control window detaches the touch device
  and uses the non-touch layout so the legends are visible.
  `FURBLE_SIM_NO_TOUCH=0` is the explicit escape that keeps touch for a
  deliberate mixed-input session.
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

The control window is borderless and is placed left of the panel first, then
below, then above, and only then to the right side, flush with no gap. Its
content height matches the panel's content height, plus a top inset equal to
the panel title bar, so side placement shares a top and bottom outer edge and
the button strip plus panel read as one unit. Placement reads the panel's
content frame and title-bar border
(`SDL_GetWindowBordersSize`, with a macOS title-bar fallback) and the display's
usable bounds, then clamps into those bounds. It therefore does not cover the
panel or the panel's own button-position overlays and leaves the rightmost
screen space for the user. When even the fallback cannot clear the panel on a
very small display, the window goes to the top-left of the usable area rather
than the right edge. On each tick the strip re-resolves the panel window by its
SDL window ID and follows it when the user moves or resizes the panel.

## Single-window feasibility

A true single SDL window is not feasible without changing M5GFX. `Panel_sdl`
owns its `SDL_Window`, `SDL_Renderer` and texture in the protected `monitor`
member, creates them inside `sdl_create()` from its own `SDL_CreateWindow`
call, and exposes no caller-provided window/renderer or child-window API.
SDL2 (including Homebrew's sdl2-compat) has no public window-parenting call;
`SDL_CreateWindowFrom` creates a new top-level window from a native handle, and
M5GFX does not accept one. A wrapper window could read the panel's pixels and
re-present them with the buttons, but that adds a second renderer/readback path,
duplicates the event pump, and breaks the simulator's pixel-capture and timing
semantics.

The closest safe option is therefore the implemented height-matched control
strip: it is a separate borderless SDL window whose outer rectangle shares the
panel's top and bottom edge, and it is placed flush beside the panel (left
first), with a drawn device outline, `TOP VIEW`/`FRONT` captions, and the same
background as the panel. Its top inset leaves a blank band at the panel title
bar height, so the combined silhouette is one rectangle. The panel title bar
itself remains M5GFX-owned, which is the documented constraint rather than a
simulator choice.
Vendoring, patching M5GFX, or reparenting its native window is the blocker for
a genuinely single window.

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

Interactive runs pace `vTaskDelay` with wall time so a human click spans the
same virtual duration as it does on hardware. While the control window exists,
`sim/driver.cpp` triggers LVGL display activity from the UI task so an idle
interactive session does not dim or sleep between clicks. Script, headless and
fuzz runs never create the window and still exercise the real inactivity, dim
and sleep paths, including the first-press wake behavior. The keepalive is
simulator-only and does not change production display policy.

## Files

- `sim/ui_buttons.cpp`, `sim/ui_buttons.h` - the control window, top-view
  layout, event filter, pressed-state rendering and GPIO injection.
- `sim/main.cpp` - creates and services the window on the SDL main thread.
- `sim/driver.cpp`, `sim/driver.h` - the explicit `scriptProvided` interactive
  sentinel, the `btn-hold` timed-hold step, and the activity keepalive.
- `sim/FurblePlatformSim.cpp` - forces the non-touch layout when the on-screen
  buttons are requested, with `FURBLE_SIM_NO_TOUCH=0` as the escape.
- `src/FurbleUI.cpp` - the `FURBLE_SIM`-only 1000 ms long-press and 250 ms
  repeat timing on the three encoder input devices, plus the timed-hold test
  seam.
- `sim/CMakeLists.txt` - documents that the `sim/*.cpp` glob picks up the new
  translation unit; `sim/build.sh` already globs the same directory.
- `docs/sim.md`, `sim/CLAUDE.md` - simulator reference and directory-local
  contract.
- `README.md`, `docs/wiki/Controls.md`, `docs/wiki/Settings-Reference.md` and
  `docs/settings-and-controls.md` - user-facing button map, automated-input
  distinction and opt-out.
- `sim/scenarios/e2e/button-hold-repeat.txt` and
  `sim/scenarios/manifest.json` - the repeat-threshold regression.

The only production-file change is the `FURBLE_SIM`-guarded long-press/repeat
timing in `UI::initInputDevices`; firmware builds compile the unchanged
production path. There is no simulator self-test hook in `src/`.

## Validation

- Built all three modeled panel classes with `sim/build.sh` (M5StickS3,
  M5StickC, M5Stack Core). All three linked.
- `button-hold-repeat` passes on StickS3 and StickC. A tap, a 600 ms hold and
  a 900 ms hold each move one focus row, while a 1600 ms hold crosses the
  one-second threshold and both 250 ms repeat ticks.
- Targeted headless runs pass: `button-mode-dispatch`, `modal-input-overlap`,
  `modal-input-ownership`, `remote-back-button`, `stick-notouch-layout-135`,
  `core-notouch-layout` and `overflow-sweep` on their modeled panels.
- `stick-notouch-layout-80` still reports the documented sensors-page overflow
  at commit eb55e761. A clean `git archive eb55e761` build reproduces the same
  `ui.overflow expected no got yes`, so it is pre-existing and not caused by
  this branch.
- `python3 tools/pre_review.py --base origin/master` passes 7/7.
- `python3 tools/check_static_analysis.py --base origin/master` passes with
  the host and simulator compile databases.
- Visible smoke on the development Mac, using native mouse events: 1 ms, 10 ms,
  30 ms and 150 ms clicks each produced exactly one focus move, and a 1.5 s
  hold produced repeats. Captured the StickS3 control strip, the flush combined
  frame, the pressed amber B state, and the Core front layout. The capture
  files are attached to PR #359.
- `--script interactive.txt` creates no control window, and
  `FURBLE_SIM_BUTTONS=0` leaves only the panel window.
- `FURBLE_SIM_NO_TOUCH=0` keeps the mouse-driven touch layout, while the
  default interactive run shows the physical-button legends.

## Remaining manual gate

A human should still drag out of the control window while holding a button to
confirm the GPIO releases, and close the control window without disturbing the
panel. The headless suite does not cover those pointer-capture paths. The
native mouse smoke above covered one-click, repeat, and pressed rendering.
