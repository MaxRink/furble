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
seven rows. The first design capped the page's own face at Normal to hold the
fit. The device walk rejected that: a page honours the size the user chose and
scrolls when the rows stop fitting. The cap is gone, the page scrolls at Large,
and what is asserted instead is that nothing is drawn over anything else and
that the scroll is bounded. See "Rework after the device walk" below.

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

The first design used four columns, which make a cell 80 px, and stepped this
page's labels down to `lv_font_montserrat_14` because "Disconnect" is 87 px at
the icon menu font and lost its last glyphs to the cell edge. The device walk
rejected the font step. The page runs three columns instead, which leave 106 px
and fit "Disconnect" as it is, and the names under the icons wrap rather than
scroll or clip. See "Rework after the device walk" below.

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

| IR setting | Entries in cell (1,1) | Master `label_overlaps` | This branch |
| --- | --- | ---: | ---: |
| on | Infrared, Cameras, Level | 3 | 0 |
| off | Cameras, Level | 1 | 0 |

The 13 px overflow this plan opened with is the physical-button layout's, in
both IR states, and it is what `core-notouch-layout.txt` records. The touch
layout fits at the default text size in both states and overflows by 23 px at
the maximum, which is what `core-connected-grid-large.txt` records. The two
numbers belong to two layouts and neither depends on the IR setting; only the
overlap count does.

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

So the row has a rule, in this order, and the rework settled it at the user's
own font rather than by shrinking anything:

1. Neither label is cut. The row wraps, so the value takes a line of its own
   when the two do not fit side by side, and each label wraps within its line.
   A name cut mid glyph reads as a different setting exactly as a lost digit
   does: "Duratic" and "999 mi" are the same defect.
2. A 6 px column gap keeps the name and its value from reading as one word.
   The gap was set and then overwritten with 2 px by a later board block, which
   is how 135x240 Large drew "Count10".
3. Capping the name's width is what cut it, not what saved it: the cap made
   LVGL wrap the name and the row then clipped the wrapped second line, so
   80x160 Normal drew "Coun". The name keeps its natural width instead. On the
   80x160 panel, where the reserved legend column leaves 52 px and "Shutter" is
   63 px at the default face, the name takes the whole line and wraps inside it
   with the value on the next line.
4. If the two still do not fit, the unit text shortens rather than the value.
   The narrow panels use `SpinValue::getShortUnitString`, "ms", "s" and "min"
   instead of "msec", "secs" and "mins". The 320x240 grid has the width and
   keeps the spelled out unit, and the unit roller follows the same split so a
   board reads the same in both places.

Folding the unit into the row name ("Shutter ms" with a bare "250") was
rejected: it lengthens the name on exactly the panels where the name is the
thing being squeezed, and it puts the unit somewhere the value page does not
repeat.

`ui.cut_names` is the assertion that holds it, not `ui.min_name_chars`. A
minimum of four can mean "Wait shown whole" or "Count cut to Coun", and only the
count tells them apart; the earlier claim that the 4 was Wait was wrong.
Measured on both narrow panels, both legend placements, all three text sizes:
`ui.cut_names` and `ui.clipped_values` are 0 in all twelve combinations.

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

Every touched page, on every modeled panel, in both layouts, at all three text
sizes, before and after, is captured in `plans/168-evidence/`: 720 palette PNGs,
about 3 MB. The before set is the PR base, commit `8bdc52e4`; the after set is
this branch. The directory is laid out as
`<panel>/<before|after>/<layout>-ts<size>[-lg<legend>]--<page>.png`, so
`320x240/before/buttons-ts1--settings.png` and
`320x240/after/buttons-ts1-lg0--settings.png` are the pair that shows the
Settings icon grid collision and its fix.

`lg0` is the default Buttons legend placement, which is what master had. `lg1`
is the new Bottom option, which has no before pair because it did not exist.

The captures are generated by driving the `capture` verb through one scenario
per layout, text size and legend placement, then quantised with `pngquant` and
recompressed with `optipng`. The raw simulator capture is an uncompressed RGB
PNG and the set would otherwise weigh about ten times as much.

The first design's hand-picked before and after pairs under `docs/img/notouch/`
are gone. The after half of that set showed the layout the device walk rejected,
so it contradicted the head; plan 165's before captures stay where plan 165
references them.

## What CI runs now

`sim/scripts/run-notouch.sh` runs the certified bug-hunt and end-to-end sets for
one board with `FURBLE_SIM_NO_TOUCH=1`, bounded per scenario by the same
`FURBLE_SIM_SCENARIO_TIMEOUT` plan 166 added to the other runners.
`.github/workflows/sim-e2e.yml` calls it on all three board binaries. That is
follow-up 3 of plan 165 for the scenario suites.

The runner takes its list from the manifest and skips only what its own header
names, so a scenario added by another PR joins this layout without editing the
script. The twelve cancel-sweep, power-off and reconnect scenarios plan 172
added on `6245a301` are picked up that way and pass in both layouts.

Rebased onto `6245a301` after PR #278 merged. That PR changed no file under
`src/`, `include/`, `components/` or `lib/furble/`, so no capture in the
evidence set moved: a regenerated after set on the rebased binaries is
byte-identical except for six 320x240 frames whose header shows the GPS icon in
its searching state rather than its fixed state, which is fix timing, not
layout. Those six are refreshed anyway.

## Still open

Two scenarios are skipped on the 80x160 board, named in the runner's header.
Both seed the maximum text size, and that board cannot render its own pages at
that size in the layout it ships:

- `bughunt/text-size-overflow-large.txt`: the Connected page has six visible
  rows in the state it drives, 108 px of text rows in an 87 px sub page.
- `e2e/home-seven-rows-large.txt`: seven home rows, 129 px in a 112 px page.

The first design capped the same board's timer page with `fontForSpinRow`, so
choosing Normal there did not enlarge the Count, Delay, Shutter and Wait rows.
The device walk rejected that cap along with the others. The rows are drawn at
the chosen face and the page scrolls.

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

`tests/host`'s `console-commands` segfaults intermittently, roughly one run in
two, but only when built with coverage instrumentation. The uninstrumented
suite passes 93 of 93. The fault is on a background thread in
`Furble::Control::reapZombieTargets`, which this change does not touch, and
neither `src/FurbleControl.cpp` nor `src/FurbleConsole.cpp` nor anything under
`lib/furble` or `tests/host` differs from master on this branch. It is a
control-teardown race that the slower instrumented build exposes, in the same
family as the three plan 169 made deterministic, and it belongs with that work
rather than with a layout change.

`sim/scripts/check-doc-tokens.sh` is not run by any workflow. It caught a real
gap here, the new `intervalSeedIsValid` predicate its `validateSeed` reader did
not recognise, and it caught it only because it was run by hand. Wiring it into
`.github/workflows/sim-e2e.yml`, or into the python tests beside
`tests/test_build_inventory.py`, is a small follow-up and belongs to whoever
owns that script rather than to a layout change.

The `FURBLE_SIM_NO_TOUCH=1` fuzz leg, follow-up 3's other half, is also still
open. The four `mustFit` pages the fuzzer checks are green in this layout now,
so it should be addable, but a fuzz leg needs its own seed calibration and does
not belong in a layout change.

## Hardware checklist

Owed on the M5StickS3 after review. Walkable in under ten minutes.

| Page | How to reach it | What to look for |
| --- | --- | --- |
| Any page | Boot | The legends sit where they always did: previous and select along the bottom edge, next partway down the right edge. No page content is drawn underneath the next legend, on any page, at any text size. |
| Legend setting | Settings, Display, Legend | The page opens and offers Buttons and Bottom. Buttons is selected on a fresh device. Choose Bottom, restart: all three legends now sit in one row along the bottom edge and the right hand column is given back to the content. Choose Buttons, restart: back to the shipped placement. The choice survives a power cycle either way. |
| Home menu | Settings, Infrared, turn IR on, then back to the home menu | Seven rows: Connect, Scan, Delete, IR, Settings, Level, Off. The page scrolls to reach the last of them, which is the trade for keeping the row spacing. Nothing is drawn on top of anything else while scrolling. |
| Connected, Normal | Connect to a camera | Eight rows, each with its icon, all readable. Nothing runs under the next legend. |
| Connected, Large | Settings, Text size, Large, then connect | Rows are drawn at the Large face and the page scrolls. Every row still readable, nothing stacked. Disconnect is reachable by scrolling. |
| Remote shutter | Connected, Remote | The lock icon sits just above the select legend. No grey line running off the bottom edge. Hold next, then press select: the icon closes and the shutter holds. Press next alone: it opens again. A long press of select does nothing, which is correct here; the long press binding is the touch layout's. In one-button mode there is no lock gesture at all and the icon stays open. |
| Display | Settings, Display | Every row clear of the next legend. The page scrolls; the rows do not overlap while it does. |
| Bulb duration | Connected, Bulb, Duration | The spin value is readable and nothing covers it. |
| Timer, all three sizes | Settings, Intervalometer, at Small, Normal and Large | Count, Delay, Shutter and Wait all present. Every value complete, digits and unit: the units read ms, s and min here, not msec, secs and mins, and the unit roller inside a value page says the same. Every name whole, wrapped onto a second line where the row is too narrow, never cut. At Large the page scrolls rather than shrinking the face. Watch the page for a full minute: no text should slide or flicker. A scrolling value is the redraw regression this change removed. |
| Settings pages with a roller | Settings, Text size and Settings, Theme | The roller never covers the label that names it, at any text size. |
| Spirit level | Home, Level | The bullseye is below the header and centred. Tilt the device on its side: the panel rotates and the legends follow the rotated edges. |
| Legend contrast, all themes | Settings, Theme, each of Default, Dark and Mono Furble | The three glyphs stay legible in every theme, in both legend placements. |

## Implementation state

Implemented as described, then reworked after the device walk. Read this
section together with "Rework after the device walk" below, which supersedes it
where the two differ. `src/FurbleUI.cpp` carries fixes 1 to 6b and 8. The two
page-scoped font helpers the first design added, `fontForConnectedMenu` and
`fontForSpinRow`, are gone again; `fontForTextSize` and `fontForIconMenu` are
the whole font policy. `include/FurbleUI.h` keeps `m_RightYOffset` and
`level_t::navRightYOffset`, which the legend placement setting needs back. The three board-scoped scenarios promote all
fourteen lines, `bughunt/core-connected-grid.txt` and its `-large` companion are
new for the Core2 touch layout, `e2e/redraw-steady.txt` gains a timer page step,
`e2e/level-spirit.txt` takes fix 7,
`bughunt/stick-notouch-connected-large.txt` is the pin for the Connected page
font cap, `bughunt/spin-row-widths-135.txt` and `-80.txt` are the pins for the
spin row rule, `invalid/interval-unit-unknown.txt` and
`invalid/interval-unit-only.txt` pin the seed suffix grammar,
`sim/scripts/run-notouch.sh` is new and
`.github/workflows/sim-e2e.yml` calls it three times. That runner skips a
scenario whose own guard is `assert ui.nav_layout touch`, derived from the guard
rather than from a name list, so the two Core2 files are not forced into the
layout they exist to contrast with. `src/CLAUDE.md`, `sim/CLAUDE.md` and
`docs/sim.md` are updated for the indicator band, the font helpers,
`ui.label_overlaps`, `ui.clipped_values`, `ui.cut_names`,
`ui.min_name_chars`, the interval seed
unit suffix and `assert_min`. `include/CLAUDE.md` records the layout geometry
contract `FurbleUI.h` now carries. No sdkconfig changed and no firmware
behaviour outside
the UI layout changed.

## The device walk sent it back

The M5StickS3 walk rejected three things, and the rework below is what the user
asked for rather than what the simulator found convenient.

### The legend placement is a setting, not a decision

Moving the Right legend into the navigation band was not wanted. It is now the
`LEGEND` setting, wire id 65, on its own page under Settings, Display:

- **Buttons**, the default and what these boards shipped, leaves each legend
  beside the button it names. Left and OK along the bottom edge, Right partway
  down the right edge.
- **Bottom** puts all three in the reserved navigation band, which is what this
  plan originally did to everything.

`UI::legendPlacement()` reads it and both `UI::begin` and `applyLevelRotation`
anchor from it. It is a restart setting, like the theme and the text size,
because the legends are anchored once and the room they need is reserved once.

Buttons placement means the Right legend is drawn over the page again, so the
content has to keep that column clear. `UI::legendReserve()` returns the legend
width in that placement and zero in the other, and `m_Content` reserves it once
for every page rather than each page discovering it separately. That is the
whole fix for the nine page-and-board combinations that used to overlap:
`ui.indicator_overlaps` is 0 on every page in both placements.
`bughunt/legend-bottom-135.txt` and `-80.txt` walk the same pages as the
per-board layout files with the other setting, so both placements are pinned,
and `bughunt/legend-setting-135.txt` and `-80.txt` open the Legend page itself,
assert both values render, and assert the choice survives a restart.

### The icons and the row spacing come back

The Connected page keeps its row icons and the home menu keeps the row padding
it shipped with. Seven home rows then need 210 px of a 189 px page and the page
scrolls, which is the trade the walk asked for: the icons were fine and the
shrink was not.

### No page shrinks the size the user chose

Every per-page font cap is gone: `fontForConnectedMenu`, `fontForSpinRow`, and
the `montserrat_14` the Core Connected grid used. A page renders at the chosen
size and scrolls when the rows stop fitting. The Core Connected page went back
to three columns for the same reason: four columns made a cell 80 px, which only
worked with a smaller face on that page.

The spin row rule survives at full size by wrapping rather than shrinking. The
name keeps its natural width and wraps if it is wider than the row; the value
takes a line of its own when the two do not fit side by side and wraps if it is
still too wide, so it never gives up a digit or its unit; and a 6 px column gap
keeps the two from reading as one word. Neither label animates.
`ui.clipped_values` is 0 on both narrow panels at all three sizes in both legend
placements.

### The rotary pickers were drawn over their labels

The Display page reported ten overlapping pairs at Normal on 135x240, and the
Settings pages generally were the "collisions of the rotary pickers and text"
the walk saw. The cause was one pattern: a flex container pinned to the full
page height with `LV_FLEX_ALIGN_SPACE_EVENLY`. Once the rows stop fitting there
is no free space to distribute, so LVGL stacks them. Six containers now size to
their content with a minimum of the page height, so they keep the even spread
while the rows fit and grow into a scroll when they do not.

`ui.label_overlaps` grew to cover the pickers, not just labels: rollers,
sliders, switches, checkboxes and bars. Every Settings page reads 0 on all three
boards, both layouts, at Normal and Large.

A settings row name also stopped scrolling. A circular scroll animates the row
for as long as the page is open, and the 135x240 Feedback page measured 117
invalidations over a one second probe against master's 55. Those names wrap
instead: the same probe reads 60, and the 80x160 page went from 59 to 3.

### The Settings icon grid on 320x240, a defect that predates this PR

Widening `ui.label_overlaps` to the whole page turned up a collision that is
not this PR's. On the **M5Stack Core class boards (320x240), the Settings
page**, the icon grid declares four columns and three rows, twelve cells, and
the page carries fourteen entries. On the PR base, commit `8bdc52e4`, two pairs
share a cell: Sensors and Intervalometer are both at `{3, 0}`, and IR settings
and Feedback are both at `{1, 2}`. Master has it. It is reachable on hardware
and it is why one of each pair could not be opened.

It hid behind three things at once, none of which a fit or scroll query can
see. The page still fits, so `ui.overflow` reads `no`. The rows were a fixed
fraction of the page, so the name under an icon was clipped away below it and
the collision drew label-on-label with no label visible. And the names scrolled,
`LV_LABEL_LONG_SCROLL_CIRCULAR`, so at any instant a name showed only part of
itself: "Features" read "atures" and "Sensors" read "nsors".

Three changes close it, all `FURBLE_M5COREX`:

- A fourth row. Fourteen entries need sixteen cells, so Intervalometer moves to
  `{0, 3}` and Feedback to `{1, 3}` and every entry has a cell of its own.
- The rows size to their content, `LV_GRID_CONTENT`, and the cell no longer
  stretches its container down the row. A stretched container reports the whole
  page as its height, which made one row as tall as the viewport and pushed the
  rest off the bottom.
- The name under an icon wraps instead of scrolling. That also retires a
  permanent animation on every visible cell, the redraw trap the project guide
  names.

`bughunt/core-icon-grid.txt` and `core-icon-grid-large.txt` are the teeth, on
the home menu as well as the Settings page, and they assert the new
`ui.cut_labels` query at 0: no label may lose characters at its box edge. Both
pages scroll at Large and neither cuts a name.

### What now scrolls, in pixels

The trade, stated so it is not buried: these pages render at the size the user
chose and scroll instead of shrinking. Numbers are `ui.scroll_bottom`, the
content below the fold, in the default Buttons legend placement and in the touch
layout. A blank cell fits.

| Panel | Page | Small | Normal | Large |
| --- | --- | ---: | ---: | ---: |
| 135x240 buttons | Display | 224 | 314 | 428 |
| 135x240 buttons | Timer | | | 19 |
| 135x240 buttons | Connected | | 1 | 7 |
| 135x240 touch | Display | 156 | 204 | 318 |
| 80x160 buttons | Display | 247 | 374 | 374 |
| 80x160 buttons | Text size | 104 | 217 | 217 |
| 80x160 buttons | Timer | 38 | 137 | 137 |
| 80x160 buttons | Theme | 19 | 47 | 47 |
| 80x160 buttons | Connected | | 39 | 39 |
| 80x160 buttons | Intervalometer running | 8 | 15 | 15 |
| 80x160 touch | Display | 221 | 316 | 316 |
| 80x160 touch | Text size | 45 | 137 | 137 |
| 80x160 touch | Timer | 12 | 75 | 75 |
| 320x240 buttons | Display | 164 | 164 | 250 |
| 320x240 buttons | Connected | 103 | 103 | 151 |
| 320x240 buttons | Theme, Text size, Timer | | | 11, 11, 29 |
| 320x240 buttons | Settings | 275 | 275 | 493 |
| 320x240 buttons | Home menu | | | 73 |
| 320x240 touch, the Core2 | Display | 180 | 180 | 278 |
| 320x240 touch, the Core2 | Connected | 77 | 77 | 125 |
| 320x240 touch, the Core2 | Settings | 249 | 249 | 467 |
| 320x240 touch, the Core2 | Home menu | | | 47 |

The Display page is the biggest number on every Stick because it is the longest
settings page and it gained the Legend row. On the 320x240 boards the Settings
page is the biggest, because it is an icon grid of fourteen entries whose rows
now size to their content instead of being clipped to a fixed fraction of the
page. The home menu is the one page there that still has to fit without
scrolling, since it is the session root, and it does at Small and Normal in both
layouts; at Large it scrolls. On the 80x160 panel the Large column
equals the Normal one because that board clamps Large to Normal.

### What that cost the fit assertions

A page that scrolls cannot assert `ui.overflow no`. The assertions that measured
a fit on a page which now scrolls were replaced by `ui.label_overlaps 0`, which
is the property that actually matters and the one those pages were quietly
failing: before this change several of them reported a fit only because the
container absorbed the excess by stacking widgets on top of each other, which no
fit query can see.

## What PR #266 no longer needs

#266 merged first and this branch is rebased onto it. Its
`UI::floatingIndicatorReserve()` reserved the right indicator's width on any
full width menu row, because a wrapped camera row is tall enough to reach an
indicator that floats over the page. The reservation is still needed in the
default Buttons placement, where the Right legend is drawn over the page, but it
does not belong on the row: `m_Content` reserves `UI::legendReserve()` once for
every page instead, which is zero in the Bottom placement and on a touch panel.
The function, its declaration, both call sites in `addMenuItem` and
`rebuildCamerasPage`, its `include/CLAUDE.md` bullet and the paragraph in plan
167 that introduced it are deleted here, and the `src/CLAUDE.md` rule that told
a full width row to keep the column clear now points at the page level
reservation.

The camera rows still wrap on `LV_LABEL_LONG_WRAP` rather than scrolling, for
the redraw reason #266 gives, and `e2e/camera-name-rows.txt` still asserts
`ui.indicator_clearance clear` on the saved list, the scan list and the Cameras
page. That scenario is what makes the deletion provable rather than plausible.
Restoring the float without the reservation, at the offset master used on each
board, fails it on 80x160 with `overlap`; with the indicator in the band it
passes with no reservation anywhere. Both runs are in the tally.

### One thing #266 needed narrowing

The no-touch leg went red on `bughunt/feedback-hidden-route.txt` after the
rebase, on 135x240, `ui.overflow yes` by 13 px. #266 wrapped every icon-less
menu row, reasoning in its own comment that "a camera row is the only icon-less
menu item". That is not so: a menu entry built with a null icon is icon-less
too. "Feedback Events" is one, it is wide enough to wrap at 135 px, and the
second line it gained pushed the Feedback page past the shipped layout's 167 px.

`addMenuItem` now takes `wrapText`, and `addCameraItem` is its only caller that
sets it. A camera name is user data composed by the vendor client and can be
wider than any row; a fixed menu label is chosen to fit and keeps the scroll it
always had. That is the rule `src/CLAUDE.md` now states. The overflow is gone
and `camera-name-rows` is unaffected, because every row it measures is a camera
row.

This is the leg doing its job: the defect reached master 40 minutes before this
rebase, in the touch layout it is 26 px of slack away from mattering, and
nothing but a no-touch run would have seen it.


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

The first version of `ui.clipped_values` compared a label's own width against
its own content width, which for a content-sized label is a tautology: it read 0
however far the label hung out of the row that draws it, and it certified an
80x160 layout that was losing digits. A query that measures a widget against
itself measures nothing. It now measures the value's box against the row's
content box, which is where the clipping actually happens, and flags
`LV_LABEL_LONG_DOT` as a lost character whatever the geometry says. The same
pass scoped the spin row shape to `lv_menu_cont_class`, because the spirit
level's readout row is a plain object with two labels and was reporting into
these numbers.

The first version of fix 8 clipped the value rather than the name, which cost
digits at ordinary values and not only at the maxima. Fix 8 now states the rule
it should have started from: the value never clips, the name clips to a floor of
four characters, and if neither fits the unit text shortens. That took a query
to hold, because nothing already in the simulator could see a lost digit.

Fixes 5, 6, 6b and 8 are unverified on hardware: only the M5StickS3 is
available. The 80x160 and 320x240 changes, including the Core2 touch-layout
reach in 6b, are simulator-verified and code-reviewed.
