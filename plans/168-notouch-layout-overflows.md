# 168 - fix the layout the physical-button boards actually ship

PR #264 certified the physical-button layout in the simulator and left the
product gaps it found recorded as `xassert` lines. This closes them.

Every gap is in the layout all three modeled boards render on hardware. None of
them is visible in the touch layout the simulator measured before #264, which is
the layout only the Core2 ships and `sim/build.sh` does not model. So each one
below is a defect a user sees on a real M5StickC, M5StickC Plus, M5StickS3 or
M5Stack Core Basic today.

## Numbering

165 is PR #264 and 166 is PR #270, both merged. 167 is unused. This plan is
numbered 168 to match the branch the work started on.

## The worklist

Fourteen `xassert` lines across the three board-scoped scenarios plan 165 added,
and twelve certified scenario runs that failed when the same certified set ran
with `FURBLE_SIM_NO_TOUCH=1`. The two lists describe the same defects from two
directions.

Plan 165 measured its twelve-run list on ab638874, before PR #261 merged. It was
re-derived on f425fd38 for this work, and one line had moved: on 135x240
`bughunt/overflow-sweep.txt` fails on `shutter`, not on `connected`. Its
connected page reads `overflow no` in the state it drives, because it never
turns the IR setting on. The rest of the list held.

| Page | Board | Gap | After |
| --- | --- | --- | --- |
| `display` | 135x240 | 2 widgets under an indicator | 0 |
| `main`, seven rows | 135x240 | 21 px overflow | 0 |
| `connected` | 135x240 | 25 px overflow | 0 |
| `connected` | 135x240 | 1 widget under an indicator | 0 |
| `shutter` | 135x240 | 64 px overflow | 0 |
| `bulb_duration` | 135x240 | 1 widget under an indicator | 0 |
| `display` | 80x160 | 1 widget under an indicator | 0 |
| `sensors` | 80x160 | 10 px overflow | 0 |
| `sensors` | 80x160 | 1 widget under an indicator | 0 |
| `timer` | 80x160 | 2 widgets under an indicator | 0 |
| `bulb` | 80x160 | 1 widget under an indicator | 0 |
| `bulb_duration` | 80x160 | 1 widget under an indicator | 0 |
| `timer_run` | 80x160 | 2 widgets under an indicator | 0 |
| `connected` | 320x240 | 13 px overflow | 0 |

All fourteen are promoted from `xassert` to hard `assert`. None became
`xassert board-varies`: each file covers one board, so a gap left as `xassert`
after being closed would record an XPASS and fail the run, which is the
mechanism splitting the files per board was for.

## The fixes

### 1. The Right indicator joins the band that was reserved for it

Nine of the fourteen gaps are one defect. The physical-button layout reserves a
navigation bar band, `ICON_HEADER_SIZE + 2` = 26 px, at the bottom of the window
content. On the Stick boards the band stays empty: the three indicators are
floating children of `m_Screen` instead. Left is at `LV_ALIGN_BOTTOM_LEFT` and
OK at `LV_ALIGN_BOTTOM_MID`, both inside the band. Right alone was at
`LV_ALIGN_RIGHT_MID` with a 65 px offset on the 135 px panel and no offset on
the 80 px panel, so it floated halfway down the right edge over page content,
and the band it could have occupied stayed empty.

The three indicators are a legend for the three physical buttons: previous,
select, next. Left and OK are already a bottom row that does not track where the
buttons physically sit. Right was the only one that did not join them, and the
cost was that it drew over content on every page tall enough to reach it. On the
80x160 panel it landed halfway down and covered the seconds value of both timer
rollers, which plan 165 called the one indefensible case.

Right moves to `LV_ALIGN_BOTTOM_RIGHT`. All three indicators now read as one
legend row inside the band the layout already reserves, the Stick layout matches
the Core (where the three live inside the band as flex children), no indicator
is ever drawn over content on any page, and `m_RightYOffset` and
`level_t::navRightYOffset` both go.

### 2. Home menu row padding on the 135x240 panel

`UI::addMenuItem` picks the per-row padding from the page. The home page used 3.
The comment above it did the arithmetic against a 215 px page, which is the
touch layout; the shipped page is 189 px. Seven rows at padding 3 are 210 px, so
they overflowed by 21.

The home page joins the Connected page at padding 0, which is the shape the
80x160 branch below it already uses. Seven rows are then 168 px against 189, and
the Large text size does not change it because the 24 px icon sets the row
height.

### 3. The Connected page on 135x240

Measured: the page is 167 px, not the 189 px the home page gets, because the
menu header reserves the back button's width on a sub page. It carried eight
rows of the 24 px icon plus that 22 px difference, 214 px, and overflowed by 25
with the IR setting on and by 1 with it off.

The first attempt dropped the Infrared entry, on the reasoning that it is the
only entry that is not about the connected camera and the home menu carries the
same shortcut. That was wrong, and the measurement said so twice. Seven rows
still needed 168 px of a 167 px page, one pixel short. And, more seriously,
`UI::addMainMenu`'s page dispatch hides and disables the header back button on
the Connected page: it is the root for the whole session, so anything dropped
from it is unreachable until the camera is disconnected. Dropping the Infrared
entry there would have taken IR away mid-session.

So every entry stays and the row icons go instead, which is what the 80x160
board already does on every page. Eight text rows are 144 px against 167. This
also removes eight compressed-icon decompresses from every draw of the page.

### 4. The Remote shutter page on 135x240

In the physical-button layout the page carried a floating shutter-lock icon and
a grey leader line drawn from three or four hardcoded points per board, under a
`@todo Clean up the plethora of hardcoded values here`. The points were derived
from `lv_obj_get_y(m_Right)`, which fix 1 invalidates outright. The line's
vertical run ended 103 px below its own origin, past the bottom of the page, and
that run was the 64 px overflow.

The line goes. The lock icon stays, aligned `LV_ALIGN_BOTTOM_MID` so it sits
directly above the OK indicator, which is the button whose long press toggles
it. The page then holds one widget and cannot overflow, and the hardcoded point
tables for all three boards go with the line.

### 5. The Sensors page on 80x160

The page held the IMU setting row, a "Restart to apply" notice row and a Restart
button, and overflowed by 10 px. The notice row goes on this panel, following
the precedent the GPS Data page already sets for the 80x160 board. The Restart
button below it says what the notice said, and every wider panel keeps both.

### 6. The Connected page on 320x240

The Core lays this page out as a grid. It declared three columns and two
`LV_GRID_CONTENT` rows, six cells, and put eight entries in them: Infrared,
Cameras and Level were all assigned cell (1,1). Three widgets stacked in one
cell is why the plan 165 screenshot shows Cameras drawn over its own icon, and
the content rows growing around the stack was the 13 px overflow.

The page moves to the four-column, two-`LV_GRID_FR(1)`-row descriptor the Core
home menu already uses, and the eight entries take eight distinct cells:

| | 0 | 1 | 2 | 3 |
| --- | --- | --- | --- | --- |
| row 0 | Remote | Bulb | Interval | Infrared |
| row 1 | GPS Data | Level | Cameras | Disconnect |

`LV_GRID_FR(1)` rows divide the page exactly, so it cannot overflow.

Four columns make a cell 80 px. Two things follow. The row container gives up
its horizontal padding, which only ever cost label width because the icon is
centred either way. And this page, and only this page, steps its labels down to
`lv_font_montserrat_14`: "Disconnect" is 87 px at the icon menu font and lost
its last glyphs to the cell edge.

### 7. The spirit level assertion

`e2e/level-spirit.txt` asserted `ui.level_surface_top 50`. That is not an
overflow and not a layout gap: the reserved band makes the page 26 px shorter,
so the bullseye settles 19 px lower and the query reads 69. The assertion's
stated intent, in its own comment, is that the circle sits below the header
rather than jammed under it, which is a minimum and not an equality. It is now
`assert_min ui.level_surface_top 50`, which holds in both layouts and keeps
measuring what it says it measures.

### 8. The timer settings page at the largest text size

`bughunt/text-size-overflow-large.txt` failed on `timer` on both Stick panels,
by 3 px at 135x240 and 23 px at 80x160.

The row is a `LV_FLEX_FLOW_ROW_WRAP` container holding the setting name and its
value. On a narrow panel the two do not fit side by side above the smallest
font, so the value wrapped onto a second line and the row doubled in height. The
80x160 capture showed exactly that: "Shutter" on one line and ") msec" on the
next, with the fourth row clipped away. The narrow panels now use
`LV_FLEX_FLOW_ROW`. The value label already carries
`LV_LABEL_LONG_SCROLL_CIRCULAR`, so a value too wide for the space left scrolls
rather than wrapping, and every row is one line at any font. That closed the
80x160 case outright, 23 px to 0.

The 135x240 case was 3 px of padding, so the spin rows there take the same trim
the 80x160 rows already had, 2 px rather than the theme default. 170 px to
138 px against a 167 px page.

## Evidence

Captured at native panel size with the scenario `capture` verb, before and after,
in `docs/img/notouch/`. The before set is the one plan 165 committed, plus
`135-shutter.png`, captured on f425fd38 before any of this landed.

| Before | After | What changed |
| --- | --- | --- |
| `135-main-seven-rows.png` | `after-135-main-seven-rows.png` | Infrared was below the fold, 21 px. All seven rows visible. |
| `135-connected.png` | `after-135-connected.png` | Disconnect was below the fold and the Disconnect row ran under the Right indicator. All eight rows visible, indicators in one row along the bottom. |
| `135-shutter.png` | `after-135-shutter.png` | The leader line ran off the bottom edge, 64 px. One lock icon above the select indicator. |
| `80-sensors.png` | `after-80-sensors.png` | The Restart button was clipped, 10 px. Setting row and button both visible. |
| `80-timer.png` | `after-80-timer.png` | The Right indicator covered the seconds value of the Delay and Shutter rows. Indicators along the bottom, rows clear. |
| `320-connected.png` | `after-320-connected.png` | Cameras drawn over its own icon, bottom row clipped, 13 px. Eight entries in eight cells, every label readable. |

## What CI runs now

`sim/scripts/run-notouch.sh` runs the certified bug-hunt and end-to-end sets for
one board with `FURBLE_SIM_NO_TOUCH=1`, bounded per scenario by the same
`FURBLE_SIM_SCENARIO_TIMEOUT` plan 166 added to the other runners.
`.github/workflows/sim-e2e.yml` calls it on all three board binaries. That is
follow-up 3 of plan 165 for the scenario suites.

## Still open

Two scenarios are skipped on the 80x160 board, named in the runner's header.
Both seed the maximum text size, and that board cannot render its own pages at
that size in the layout it ships:

- `bughunt/text-size-overflow-large.txt`: the Connected page has six visible
  rows in the state it drives, 108 px of text rows in an 87 px sub page.
- `e2e/home-seven-rows-large.txt`: seven home rows, 129 px in a 112 px page.

The rows are already at zero padding and every one of them is reachable only
from that page, so there is nothing left to remove and nothing left to trim.
The lever is the text size policy: `TextSizePolicy::MAX` is `NORMAL` on this
board, and the measurement says the shipped layout only holds `SMALL`, which is
also the board's default. Dropping `MAX` to `SMALL` would leave the board a
single text size and would hollow out three certified scenarios, so it is a
product decision rather than a layout fix and is left for gkoh. The touch-layout
runs of both scenarios are unchanged and still green.

The 320x240 intervalometer settings page overflow that plan 161 found through
fuzz seed 3, and that PR #270 unpinned because the scheduler change moved the
event stream past it, is still uncovered. This work does not touch that page on
the Core: fix 8 scopes its row flow change to the narrow panels, and the Core
keeps `LV_FLEX_FLOW_ROW_WRAP`. `EXACT_BOARDS` still restricts
`text-size-overflow-large.txt` to the two Stick boards. Closing it means
certifying that scenario for `m5stack-core` and fixing what it then reports.

The `FURBLE_SIM_NO_TOUCH=1` fuzz leg, follow-up 3's other half, is also still
open. The four `mustFit` pages the fuzzer checks are green in this layout now,
so it should be addable, but a fuzz leg needs its own seed calibration and does
not belong in a layout change.

## Hardware checklist

Owed on the M5StickS3 after review. Walkable in under five minutes.

| Page | How to reach it | What to look for |
| --- | --- | --- |
| Any page | Boot | Three indicators in one row across the bottom edge: previous, select, next. Nothing floating halfway down the right edge. |
| Home menu | Settings, Infrared, turn IR on, then back to the home menu | Seven rows all visible without scrolling: Connect, Scan, Delete, IR, Settings, Level, Off. |
| Connected | Connect to a camera | Eight text rows, no icons, all visible down to Disconnect without scrolling. |
| Remote shutter | Connected, Remote | The lock icon sits just above the select indicator. No grey line running off the bottom edge. Long press select still toggles the lock and the icon changes. |
| Display | Settings, Display | Every row clear of the indicator row along the bottom. |
| Bulb duration | Connected, Bulb, Duration | The spin value is readable and nothing covers it. |
| Timer | Settings, Intervalometer | Count, Delay, Shutter and Wait all visible, each on one line, at Normal and at Large. |
| Spirit level | Home, Level | The bullseye is below the header and centred. Tilt the device on its side: the panel rotates and all three indicators land on the rotated bottom edge, not the old corners. |

## Implementation state

Implemented as described. `src/FurbleUI.cpp` carries fixes 1 to 6 and 8,
`include/FurbleUI.h` loses `m_RightYOffset` and `level_t::navRightYOffset`, the
three board-scoped scenarios promote all fourteen lines,
`sim/scenarios/e2e/level-spirit.txt` takes fix 7, `sim/scripts/run-notouch.sh`
is new and `.github/workflows/sim-e2e.yml` calls it three times. No sdkconfig
changed and no firmware behaviour outside the UI layout changed.

## Deviations

The plan as first drafted proposed dropping the Infrared entry from the 135x240
Connected page as the row removal, with the row icons as a fallback only if that
was not enough. Both halves of that turned out to be wrong and the reasons are
in fix 3: the row removal was one pixel short of enough, and the Connected page
is the session root, so the entry would have been unreachable rather than merely
one press further away. Every entry stays and only the icons go.

The Core Connected page needed one more change than the plan predicted. The
four-column grid fixes the stacking and the overflow, but 80 px cells clip
"Disconnect" at the icon menu font, so that page steps its labels down one font.
That is the only font change in this work.

Fixes 5, 6 and 8 are unverified on hardware: only the M5StickS3 is available.
The 80x160 and 320x240 changes are simulator-verified and code-reviewed.
