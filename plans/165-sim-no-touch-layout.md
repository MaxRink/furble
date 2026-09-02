# 165 - the simulator measured a layout no Stick board ships

Every certified overflow assertion in the simulator measured the wrong layout.

`UI::begin()` picks the navigation layout at runtime from
`M5.Touch.isEnabled()`. A board with a touch panel gets the touch grid. A board
without one gets the physical-button layout: a navigation bar band at the bottom
of the window content, `ICON_HEADER_SIZE + 2` = 26 px tall, plus three floating
button indicators aligned to the screen edges. On the Stick boards that band
stays empty, because the switch on `M5.getBoard()` creates the three buttons on
`m_Screen` as floating children instead of inside the bar. The band is the
reserved clearance for the Left and OK indicators, which sit at the bottom edge.

The M5StickC, the M5StickC Plus and the M5StickS3 have no touch panel, so this
is the only layout they ever render. The M5GFX SDL panel, however, always
attaches a mouse-driven touch device, so `M5.Touch.isEnabled()` is true for
every modeled board in the simulator. The non-touch layout was reachable only
through `FURBLE_SIM_NO_TOUCH=1` or a `no_touch` scenario seed, and no CI job set
either. `sim/scripts/docs-capture.sh` did set it per board for the screenshots,
which is how the shipped layout still appeared in the documentation while no
assertion ever measured it.

So `bughunt/page-matrix.txt`, `bughunt/overflow-sweep.txt`,
`bughunt/text-size-overflow-large.txt`, `bughunt/text-size-overflow-small.txt`,
`e2e/level-overflow.txt`, `e2e/home-seven-rows.txt` and every other
`assert ui.overflow no` measured a viewport 26 px taller than the one a Stick
board has, against a page with no indicators drawn over it.

## Numbering

161 is `feat/sim-real-control-2` and 164 is the layout work of PR #263, both in
flight. 165 is the next free number. This document does not depend on either.

## Why the board default was not changed

The obvious fix is to derive the default from the compiled board, the way the
firmware derives it from the panel. That was measured first, on 153c38b7. It
turns eleven certified scenarios red, because the shipped Stick layout really
does overflow where the touch layout does not:

| Scenario | Board | Page |
| --- | --- | --- |
| `bughunt/overflow-sweep.txt` | 135x240 | `connected` |
| `bughunt/page-matrix.txt` | 135x240 | `connected` |
| `bughunt/text-size-overflow-large.txt` | 135x240 | `timer` at Large |
| `bughunt/text-size-overflow-small.txt` | 135x240 | `shutter` at Small |
| `e2e/home-seven-rows.txt` | 135x240 | `main`, seven rows |
| `e2e/home-seven-rows-large.txt` | 135x240 | `main`, seven rows at Large |
| `e2e/level-spirit.txt` | 135x240 | `level_surface_top` 69, not 50 |
| `bughunt/overflow-sweep.txt` | 80x160 | `sensors` |
| `bughunt/page-matrix.txt` | 80x160 | `sensors` |
| `bughunt/text-size-overflow-large.txt` | 80x160 | `timer` at Large |
| `e2e/home-seven-rows-large.txt` | 80x160 | `main`, seven rows at Large |

Turning those eleven into expected failures would weaken the same assertions for
the M5Stack Core, which shares every one of those scenarios and is not affected.
Fixing the layouts is a hardware-verified change to `src/FurbleUI.cpp` and is not
this change.

This plan therefore closes the measurement gap and records the product gaps. The
board-derived default is the follow-up, once the layouts below are fixed.

## What this adds

`sim/scenarios/bughunt/stick-notouch-layout.txt` seeds `no_touch true` and is
certified for `m5stick-s3` and `m5stick-c` only. The manifest already selects
certified scenarios per board, so the existing certified bug-hunt steps run it on
both Stick binaries with no new workflow job. `tools/check_sim_scenarios.py`
pins its board matrix in `EXACT_BOARDS`, so it can never silently start running
on the Core, and its workflow capabilities in `WORKFLOW_CAPABILITIES`.

Its first line is `assert ui.nav_layout buttons`. Without that guard a run that
lost the seed would fall back to the touch layout and pass for the wrong reason,
which is the exact failure this plan exists to remove.

Three simulator-only queries were added to `UI::simQueryState`:

- `ui.nav_layout`: `touch` or `buttons`.
- `ui.indicator_clearance`: `clear`, `overlap` or `n/a`.
- `ui.indicator_overlaps`: the count, for diagnosis.

The clearance query walks the current page for visible labels and icons and
intersects them with the indicator rectangles. Two corrections make it
meaningful rather than noisy. A label object is stretched by its flex row while
the glyphs occupy only part of it, so the box is shrunk to the drawn text extent
and the text alignment is honoured. A scrolled page keeps coordinates for rows
that are clipped away, so every area is clamped to the page viewport first.
Without the first correction sixteen pages report an overlap on the 135x240
panel; with it, the reported overlaps are the ones a screenshot confirms.

## Measured gaps, recorded as xassert

Every line below is an `xassert` in the new scenario with the measured count in
a comment. An XPASS is the signal to promote it to a hard assert.

Overflow, 135x240 (M5StickS3 and M5StickC Plus):

| Page | Overflow | Touch layout |
| --- | --- | --- |
| `main`, seven rows | 21 px | fits |
| `connected` | 25 px | fits |
| `shutter` | 64 px | fits |

Overflow, 80x160 (M5StickC):

| Page | Overflow | Touch layout |
| --- | --- | --- |
| `sensors` | 10 px | fits |

Content under an indicator. The Left and OK indicators sit inside the reserved
band and never collide. The Right indicator floats over the content area, at
`LV_ALIGN_RIGHT_MID` with a 65 px offset on the 135x240 panel, so a 24 px square
at y 173 to 197, and with no offset on the 80x160 panel, so a 24 px square at
y 68 to 92.

| Page | Board | Widgets under an indicator |
| --- | --- | --- |
| `connected` | 135x240 | 1, the centred Disconnect row |
| `timer` | 80x160 | 2, the Delay and Shutter value rows |
| `bulb` | 80x160 | 1 |
| `cameras` | 80x160 | 1 |
| `timer_run` | 80x160 | 2 |

The 80x160 timer page is the clearest one: the indicator covers the second half
of the Delay and Shutter rows, so the seconds value is unreadable.

None of these were fixed here. Each is a change to the shipped layout on real
hardware and needs a hardware pass, and the Right indicator overlapping content
may be deliberate on the 135x240 panel, where the indicator marks a physical
button that has nowhere else to go. The 80x160 timer case is not defensible and
is the first one to fix.

## Implementation state

Implemented as described. `src/FurbleUI.cpp` gains only the three queries and
their helper, all inside the existing `FURBLE_SIM` guard, plus two standard
library includes. No layout code changed, so no firmware behaviour changed.

## Follow-ups

1. Fix the four overflow cases, then promote the four `xassert` lines.
2. Fix the 80x160 timer, bulb, cameras and timer_run indicator collisions,
   probably by reserving right padding on the Stick content area, then promote
   the remaining `xassert` lines.
3. Once both are done, derive the simulator default from the compiled board and
   delete the `no_touch` seed from this scenario. Every existing overflow
   scenario then measures the shipped layout automatically.
