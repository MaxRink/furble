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

Two classes of promoted assertion are structurally satisfied by the fix rather
than by the page happening to fit, and they are kept as regression pins with
that stated plainly:

- Every `ui.indicator_clearance` line. `countIndicatorOverlaps` clamps each
  measured area to the page viewport, and the viewport ends above the reserved
  band, so once all three indicators are in that band no fitted page can report
  an overlap. The line still fails the moment an indicator is anchored over the
  content area again, which is exactly the regression it exists to catch, and it
  is the only assertion that catches it.
- The `shutter` page `ui.overflow` line. That page now holds one floating
  widget, and a floating child does not join its parent's scroll extent, so the
  page cannot overflow while it stays that way. The line fails if a laid-out
  widget is added to the page, which is how the 64 px arrived in the first
  place.

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

Dropping the icons made the page height text-size sensitive, where the 24 px
icon had fixed it. At the Large face, 28 px a line, eight rows are 224 px and
the page overflowed by 31, unchanged from master's number but now for a
different reason. That matters more here than on other pages, because this page
hides and disables the header back button: it is the session root, so a
Disconnect below the fold is only reachable by scrolling the encoder through
seven rows. The page therefore caps its own face at Normal, through
`fontForConnectedMenu`, the same page-scoped step the Core takes. Small still
shrinks it; Large no longer grows it. Eight rows are 144 px at every text size.

### 4. The Remote shutter page on 135x240

In the physical-button layout the page carried a floating shutter-lock icon and
a grey leader line drawn from three or four hardcoded points per board, under a
`@todo Clean up the plethora of hardcoded values here`. The points were derived
from `lv_obj_get_y(m_Right)`, which fix 1 invalidates outright. The line's
vertical run ended 103 px below its own origin, past the bottom of the page, and
that run was the 64 px overflow.

The line goes. The lock icon stays, aligned `LV_ALIGN_BOTTOM_MID`. The page then
holds one widget and cannot overflow, and the hardcoded point tables for all
three boards go with the line.

The gesture is worth stating exactly, because the comment this replaced had it
wrong and so did the first draft of this plan. `handleShutterLock`, the long
press toggle, is bound only on the touch branch of `addConnectedMenu`. Nothing
binds it in the physical-button layout, so the icon there is an indicator and
not a control. The lock is set in `handleShutter` on `LV_EVENT_PRESSED` while
`m_FocusPressed` is true, which is hold next then press select, and it is
cleared in `handleFocus` on the next press of next alone.

`LV_ALIGN_BOTTOM_MID` puts the icon above the select indicator, and that is
still the right place: both buttons take part, but next is held as a modifier
while select is the one whose press event latches the lock. The next indicator,
which clears it, is one position along the same legend row.

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
`lv_font_montserrat_14` through `fontForConnectedMenu`: "Disconnect" is 87 px at
the icon menu font and lost its last glyphs to the cell edge.

### 6b. The same three changes reach the Core2, and fix it too

None of those three is layout-scoped. They are `FURBLE_M5COREX` changes, so they
compile into every Core class build, and the M5Stack Core2 is a 320x240 board
that does ship a touch panel and therefore renders the touch layout. That layout
had the same page with the same six cells and the same three entries assigned to
(1,1). On master it drew Infrared, Cameras and Level through each other, and at
the maximum text size the page overflowed by 23 px. Both read zero here.

`sim/build.sh` does not model the Core2 separately, but its layout is what an
unseeded run on the Core binary renders, so that is where it is asserted:
`bughunt/core-connected-grid.txt` and `bughunt/core-connected-grid-large.txt`,
both certified for `m5stack-core`, walk the page in the touch layout at the
default and the maximum text size. Each opens with `assert ui.nav_layout touch`,
the mirror of the `buttons` guard the three no-touch files carry, so a stray
seed cannot make either file measure the other's subject.

Asserting the collision needed a new query. Entries sharing a cell is invisible
to every fit and scroll query, because the page still fits; what a user sees is
the labels drawn through each other. `ui.label_overlaps` counts the pairs of
visible labels on the current page whose drawn text intersects, using the same
drawn-text-extent and viewport-clamp rules `ui.indicator_clearance` already
used, now shared between the two. The walk is page-scoped, so a widget on the
top layer, a message box or any other modal, is not in the subtree and a page
showing one still reads 0.

Master's reading depends on how many of the three entries are visible, which is
worth stating exactly because both numbers are true:

| IR setting | Entries in cell (1,1) | Master `label_overlaps` | Master `overflow` | This branch |
| --- | --- | ---: | --- | ---: |
| on | Infrared, Cameras, Level | 3 | 13 px, bottom row clipped | 0 |
| off | Cameras, Level | 1 | fits | 0 |

Both new files turn the IR setting on, so all eight entries are present and the
reading they assert against is 3. Run against master's layout code they fail on
exactly that line at the default size, and on the 23 px overflow at the maximum
size.

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
`LV_FLEX_FLOW_ROW`, so every row is one line at any face. That closed the 80x160
case outright, 23 px to 0.

The 135x240 case was 3 px of padding, so the spin rows there take the same trim
the 80x160 rows already had, 2 px rather than the theme default. 170 px to
138 px against a 167 px page.

One line means the two labels share the row width, and that has a redraw cost
worth naming. The value label carried `LV_LABEL_LONG_SCROLL_CIRCULAR`, which is
harmless while the label owns a whole row and animates the moment its box is
narrower than its text. Measured on the 80x160 timer page over a one second
probe with the page held still: master 170 invalidations, the first version of
this change 338. That is the LVGL redraw trap CLAUDE.md warns about, reached by
a layout change rather than by a setter. Both labels are `LV_LABEL_LONG_CLIP` on
the narrow panels now and the same probe reads 3.

Clipping is only acceptable if nothing important is clipped, and the first
version of this got that wrong: with the value still the grown label, clipping
ate its digits. It drew "Shutter 250 m" at 135x240 and "Shutter 25" for 250 ms
at 80x160, and "999 m" at the maxima. A value that loses a digit or its unit
reads as a different setting, and no user can tell 25 from 250.

So the row now has a rule, in this order:

1. The value never clips. It takes its natural width, so every digit and its
   unit are always drawn.
2. The name gives up the room, because it is recoverable from its position in a
   fixed ordered list, but only down to four characters. `spinRowNameFloor`
   measures the first four characters of the name in the row's own face and sets
   that as the label's minimum width, which keeps Coun, Dela, Shut and Wait
   distinct. Moving `flex_grow` to the name without that floor collapses it to a
   single letter.
3. If those two still do not fit, the unit text shortens rather than the value.
   The narrow panels use `SpinValue::getShortUnitString`, "ms", "s" and "min"
   instead of "msec", "secs" and "mins", through the same board split the rest
   of this row uses. The 320x240 grid has the width and keeps the spelled out
   unit.

The unit was chosen over the alternative of folding it into the row name
("Shutter ms" with a bare "250") because that alternative lengthens the name on
exactly the panels where the name is already the thing being squeezed, and it
puts the unit somewhere the value roller page does not repeat it. Shortening the
unit costs three characters and reads the same.

Those rows also cap their face through `fontForSpinRow`: the board default on
the 80x160 panel and Normal on the 135x240 one.

Measured on both narrow panels at all three text sizes, at ordinary values
(250 ms, 30 s, 60 min) and at the maxima (999 of each unit, 999 count):
`ui.clipped_values` is 0 in all twelve combinations and `ui.min_name_chars` is 4
in all twelve, which is "Wait" shown whole rather than any name cut to four. The
longest name, Shutter, shows six characters at 135x240 and four at 80x160.

`e2e/redraw-steady.txt` gained a timer page step, on all three boards, so the
animation cannot come back. That step runs before the connect rather than after
it. Reaching the timer page means leaving the Connected page, and leaving the
Connected page ends the session, so appending the step at the end of that
scenario cut its connected tail and cost `lib/furble/Camera.cpp` thirteen
covered lines, which put that file under its own floor. A scenario that measures
anything outside the session has to do it before the session starts.

`bughunt/spin-row-widths-135.txt` and `bughunt/spin-row-widths-80.txt` drive the
widest value of every unit at the maximum text size and assert both queries.
Restoring the old layout, the value grown with `LV_LABEL_LONG_CLIP`, fails them
at 1 clipped value on 135x240 and 3 on 80x160.

## Evidence

Captured at native panel size with the scenario `capture` verb, before and
after,
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

Three captures have no before pair because they are the states this change
introduced rather than repaired:

| Capture | What it shows |
| --- | --- |
| `after-135-connected-large.png` | The Connected page at the maximum text size, all eight rows on screen. This is the page-scoped font cap, without which it overflows by 31 px with the back button hidden. |
| `after-135-timer-large.png` | The timer rows at the maximum text size on 135x240, each on one line with both name and value readable. |
| `after-80-timer-large.png` | The same on 80x160, where the face caps at the board default. |

All of these are palette PNGs, quantised and recompressed like the before set;
the raw simulator capture is an uncompressed RGB PNG and the nine files together
would otherwise weigh about 700 KB rather than 40.

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

The same board's timer page is capped rather than clipped: `fontForSpinRow`
holds those rows at the board default, so choosing Normal there does not enlarge
the Count, Delay, Shutter and Wait rows. That is a deliberate consequence of the
same limit, not a separate one.

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
the Core: fix 8 scopes its row flow change and its font cap to the narrow
panels, and the Core keeps `LV_FLEX_FLOW_ROW_WRAP` and the full text size.
`EXACT_BOARDS` still restricts
`text-size-overflow-large.txt` to the two Stick boards. Closing it means
certifying that scenario for `m5stack-core` and fixing what it then reports.

The Display page reports `ui.label_overlaps` 1 on the 135x240 and 320x240
panels, in both layouts. It is pre-existing: master's build reports the same 1
on the same page, and nothing here touches that page. It is recorded rather than
fixed, because a Display page layout change has no measurement in this work and
belongs with whoever is changing that page.

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
| Connected, Normal | Connect to a camera | Eight text rows, no icons, all visible down to Disconnect without scrolling. |
| Connected, Large | Settings, Text size, Large, then connect | Still eight rows and still no scrolling. The rows deliberately do not grow with the setting: this page is the session root with the back button hidden, so nothing on it may fall off. |
| Remote shutter | Connected, Remote | The lock icon sits just above the select indicator. No grey line running off the bottom edge. Hold next, then press select: the icon closes and the shutter holds. Press next alone: it opens again. A long press of select does nothing, which is correct here; the long press binding is the touch layout's. In one-button mode there is no lock gesture at all and the icon stays open. |
| Display | Settings, Display | Every row clear of the indicator row along the bottom. |
| Bulb duration | Connected, Bulb, Duration | The spin value is readable and nothing covers it. |
| Timer, Normal and Large | Settings, Intervalometer, at both text sizes | Count, Delay, Shutter and Wait all visible, each on one line. Every value complete, digits and unit: the units read ms, s and min here, not msec, secs and mins. Names may be cut, never below four characters. Watch the page for a full minute: no text should slide or flicker. A scrolling value is the redraw regression this change removed. |
| Spirit level | Home, Level | The bullseye is below the header and centred. Tilt the device on its side: the panel rotates and all three indicators land on the rotated bottom edge, not the old corners. |
| Indicator legend, all themes | Settings, Theme, each of Default, Dark and Mono Furble | The three glyphs stay legible against the band in every theme. They moved into the band in this change, so their contrast against it is new on the Sticks. |

## Implementation state

Implemented as described. `src/FurbleUI.cpp` carries fixes 1 to 6b and 8 and
gains two font policy helpers, `fontForConnectedMenu` and `fontForSpinRow`,
beside the existing `fontForTextSize` and `fontForIconMenu`, so no widget
hardcodes a face. `include/FurbleUI.h` loses `m_RightYOffset` and
`level_t::navRightYOffset`. The three board-scoped scenarios promote all
fourteen lines, `bughunt/core-connected-grid.txt` and its `-large` companion are
new for the Core2 touch layout, `e2e/redraw-steady.txt` gains a timer page step,
`e2e/level-spirit.txt` takes fix 7,
`bughunt/stick-notouch-connected-large.txt` is the pin for the Connected page
font cap, `bughunt/spin-row-widths-135.txt` and `-80.txt` are the pins for the
spin row rule, `sim/scripts/run-notouch.sh` is new and
`.github/workflows/sim-e2e.yml` calls it three times. That runner skips a
scenario whose own guard is `assert ui.nav_layout touch`, derived from the guard
rather than from a name list, so the two Core2 files are not forced into the
layout they exist to contrast with. `src/CLAUDE.md`, `sim/CLAUDE.md` and
`docs/sim.md` are updated for the indicator band, the font helpers,
`ui.label_overlaps`, `ui.clipped_values`, `ui.min_name_chars`, the interval seed
unit suffix and `assert_min`. `include/CLAUDE.md` records the layout geometry
contract `FurbleUI.h` now carries. No sdkconfig changed and no firmware
behaviour outside
the UI layout changed.

## Owed at the next rebase

PR #266 merges before this one. When this branch rebases onto it, its
`floatingIndicatorReserve()` becomes dead: it reserves content-area room for an
indicator that floats over the page, and after this change none does. Delete the
function and both call sites, the `include/CLAUDE.md` bullet that documents it,
and the paragraph in plan 167 that introduces it. Keep the rest of #266:
`camera-name-rows.txt` and the `LV_LABEL_LONG_WRAP` change stand on their own
and
are not affected by the indicator move.

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

A first version of fix 8 used a scrolling value label to keep the timer rows on
one line. That is a per-tick repaint, and it raised the 80x160 timer page from
170 invalidations over a one second probe to 338. The rows clip and cap their
face instead, and the probe now reads 3. The lesson is that the LVGL redraw trap
is reachable from a layout change and not only from an unguarded setter, so a
layout change that alters a label's usable width has to be measured for redraw
cost as well as for fit.

The first draft of fix 4 described the shutter lock as a long press of select.
That is the touch layout's binding. In the physical-button layout nothing binds
`handleShutterLock` at all; the real gesture is in fix 4 above. The comments,
the plan and the device checklist all carried the wrong gesture and are
corrected.

The first version of fix 8 clipped the value rather than the name, which cost
digits at ordinary values and not only at the maxima. Fix 8 now states the rule
it should have started from: the value never clips, the name clips to a floor of
four characters, and if neither fits the unit text shortens. That took a query
to hold, because nothing already in the simulator could see a lost digit.

Fixes 5, 6, 6b and 8 are unverified on hardware: only the M5StickS3 is
available. The 80x160 and 320x240 changes, including the Core2 touch-layout
reach in 6b, are simulator-verified and code-reviewed.
