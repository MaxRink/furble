# 164 - receiver detail on the GPS status page

A user asked for it plainly:

> the GPS status page could give more details about the receiver state

The GPS Data page reported the navigation solution and nothing else: fix age,
satellite count, speed, latitude, longitude, altitude, and UTC date and time.
Everything about the receiver producing that solution was elsewhere or nowhere.
Whether the fix came from the wired receiver or from a companion phone was only
in the console. HDOP and the degraded retry state were only on the Raw NMEA
page. The power cycle state, the configured fix interval and the time since the
last sentence had no public accessor at all, so no page could have shown them.

That gap has a practical cost. A receiver sitting indoors with a stale fix and a
receiver whose UART has gone silent render identically on the old page: the same
coordinates, the same satellite count, an age that keeps counting up. The user
cannot tell "no sky" from "no receiver".

## Numbering

161 is `feat/sim-real-control-2`, in flight as PR #261. 162 and 163 are merged.
164 is the next free number. If 161 lands first the numbering is unchanged, only
the ordering of the documents on the plans branch differs.

## What the page shows now

Two rows are appended below the fix rows, in the smallest font so they read as
secondary:

| Row | Fields |
| :--- | :--- |
| `uart nmea 3s` | Fix source, and how long ago the receiver last sent a sentence |
| `waiting` | Power cycle state, plus the retry count while degraded |

`nmea n/a` replaces the age when no sentence has arrived yet. The source reads
`uart`, `comp` for a companion phone, or `none`. A degraded cycle appends its
retry count, so the row reads `degraded x1`.

## The row budget, which is the whole design

The interesting constraint on this page is not the one that looks obvious.

Vertically, the page filled its 135x240 panel almost exactly. Two rows of the
smallest font fit only after the date and time were merged onto one row, which
is what the 320x240 Core branch a few lines above already did. That merge is the
only change this makes to the existing rows.

Horizontally, the constraint is the navigation indicators. On the Stick boards
the three button indicators are `LV_OBJ_FLAG_FLOATING` children of the screen,
not part of the page layout, and the right hand one is a 24 pixel square aligned
`LV_ALIGN_RIGHT_MID` with a 65 pixel offset. On a 240 pixel panel that places it
over y 173 to 197, exactly where the last rows of a full page land. A centred
row wider than about 87 pixels runs underneath it. At `lv_font_montserrat_10`
that is fourteen characters.

Fourteen characters per row, two rows, is the entire budget. Everything the
scoping note asked for did not fit, so the fields were ranked by whether the
user can find them anywhere else:

- Fix source, sentence age and power cycle state are nowhere else in the UI.
  They are on the page.
- The configured rate is in `Settings` > `GPS` > `Rate`, where the user set it.
- HDOP, the sentence counters and the degraded retry text are on the Raw NMEA
  page. The retry count still appears here because it costs three characters on
  a row that is otherwise short.
- The per-field validity flags did not survive the cut. They are the most
  cryptic of the candidates and the least actionable.

Three earlier drafts of this page each rendered correctly in the simulator's
default touch layout and were wrong on hardware. The touch layout has no
navigation indicators, so the first draft's three wide rows fit there and ran
under the indicator on the shipped Stick layout. `FURBLE_SIM_NO_TOUCH=1` is what
exposed it, and no CI job sets it. That gap is noted below.

## Why the 80x160 panel is excluded

The M5StickC page already filled its panel exactly, with roughly one line of
slack. The detail rows wrap at 80 pixels, so two rows need four lines there.

Letting the page overflow was not an option. The GPS Data page carries no
focusable control, and furble's button navigation scrolls a page only by moving
LVGL group focus through the widgets on it. A page with no group members cannot
be scrolled by the buttons at all. On the M5StickC, overflowing rows would be
rendered below the fold with no way for the user to reach them.

So the rows are compiled out on `FURBLE_M5STICKC`, in the same spirit as the
`FURBLE_M5COREX` date and time split they sit below. The Raw NMEA page has a
focusable Hot restart button, so it does scroll, and it keeps carrying HDOP, the
receiver counters and the degraded retry count on that board.

This is a stated limitation, not an oversight. Making the M5StickC fit would
mean compressing the existing fix rows on that panel, which changes rendering on
a board this change cannot be hardware-tested against.

## The GPS layer API

The page needed state that had no accessor. `GPS::getReceiverStatus()` returns a
`receiver_status_t`:

| Field | Meaning |
| :--- | :--- |
| `cycle_state` | Power cycle state name, from the new `GPS::cycleStateName()` |
| `power_policy` | Applied receiver power policy |
| `duty_seconds` | Applied standby interval |
| `rate_ms` | Configured fix interval, 0 for the receiver default |
| `last_sentence_age_ms` | Milliseconds since the last received sentence |
| `have_sentence` | Whether any sentence has arrived since enable |
| `aid_mode` | Assisted start mode |
| `aid_cache_valid` | Whether a usable assisted start cache is loaded |

`cycle_state_t` moves from the private section to the public one so
`cycleStateName()` can take it, mirroring the existing `config_state_t` and
`configStateName()` pair. `GPS::sourceName()` is added alongside, and
`sim/driver.cpp` now calls it instead of keeping its own copy of the same
switch.

The page shows `cycle_state` and `last_sentence_age_ms`. The rest of the struct
is receiver state a caller may want, and omitting it would only mean another
accessor later; the row budget above is why none of it is rendered.

### Locking

`getReceiverStatus()` takes `m_CycleMutex` for the cycle fields, exactly as
`getCycleStatusSnapshot()` does, releases it, and only then takes `m_AidMutex`
for the assist fields. The two locks are never nested, so this adds no lock
order edge for the GPS task to trip over. `m_AidMode` is atomic and needs
neither.

### No NVS on the periodic path

`rate_ms` deliberately does not call `gpsRateInterval()`, which reads NVS. This
runs on a 1 Hz LVGL timer, and live timers in this codebase must not touch NVS.
`enable()` now caches the applied interval in `m_RateMs` beside the existing
`m_PowerPolicy` and `m_DutySeconds` caches, and the reader returns that.

`m_ExpectedInterval` was the obvious candidate and is the wrong one. It becomes
the *measured* burst period once the cycle has timed the receiver, so under a 5
second standby duty cycle it reads 5000 ms while the rate setting says 1000 ms.
Reporting that as "rate" was actively misleading; a draft did, and the simulator
caught it. The field is correct now even though the page no longer renders it.

## Tests

Two certified end-to-end scenarios, both registered in
`sim/scenarios/manifest.json`:

- `sim/scenarios/e2e/gps-receiver-detail.txt` seeds a healthy 1 Hz UART fix and
  asserts every rendered field, including that the fix rows above still render.
- `sim/scenarios/e2e/gps-receiver-degraded.txt` drives the production duty cycle
  wake timeout into the degraded retry state and asserts the cycle row reports
  the state and its retry count.

Both assert `ui.overflow no`. On this page that is not a cosmetic check, it is a
reachability check: an overflowing row cannot be scrolled to.

The scenarios read the labels through four new simulator queries, which parse
the rendered label text rather than recomputing the value. An absent row reads
back as `none`, so a dropped label fails the assertion instead of passing
silently. Mutation check: removing the source row update fails
`gps-receiver-detail` with `expected 'uart' got 'none'`.

### The gap these tests do not close

`sim/scripts/run-e2e.sh` does not set `FURBLE_SIM_NO_TOUCH`, so every end-to-end
scenario runs the touch layout, where the floating navigation indicators do not
exist and the page viewport is 24 pixels taller. `ui.overflow` is therefore
measured against a layout no Stick board ships. The overflow numbers here were
taken by hand with `FURBLE_SIM_NO_TOUCH=1` on all three panels.

A non-touch leg for the overflow sweep would close this for every page, not just
this one. It belongs in its own change: it will find pre-existing overflow, the
80x160 GPS Data page among it.

No host unit test was added. The GPS layer has no host harness for the real
`FurbleGPS.cpp`: `tests/host/gps_power_cycle_test.cpp` tests the pure
`GpsDegradedRetry` policy header, and the console suite links a stub
`tests/host/console/FurbleGPS.h`. Standing up a host build of the real GPS layer
means shimming the UART driver, LVGL, `Camera.h` and TinyGPSPlus, which is its
own piece of work. The simulator compiles and runs the real `FurbleGPS.cpp`, so
`getReceiverStatus()` and `cycleStateName()` are exercised as production code
there, across the `waiting`, `standby` and `degraded` states.

## Implementation state

Implemented and merged as one pull request.

- `include/FurbleGPS.h`, `src/FurbleGPS.cpp`: `cycle_state_t` made public,
  `receiver_status_t`, `getReceiverStatus()`, `cycleStateName()`,
  `sourceName()`, `m_RateMs` cache.
- `include/FurbleUI.h`, `src/FurbleUI.cpp`: `gps_data_t` label handles, the
  `addGPSDetailLabel()` row helper, the two rows on the page timer, the merged
  date and time row on 135x240, and the four simulator queries.
- `sim/driver.cpp`: `gps.source` reuses `GPS::sourceName()`.
- `sim/scenarios/`: two scenarios and their manifest entries.
- `docs/sim.md`, `docs/ui-walkthrough.md`, `docs/settings-and-controls.md`.

### Deviations from the original scope

- The scoping note asked for separate fix/source, HDOP, degraded and validity
  lines, and for the rate on the page. The row budget above allows two rows of
  fourteen characters, so HDOP, the validity flags, the explicit fix yes/no and
  the rate are not rendered. The first three are on the Raw NMEA page and the
  rate is in Settings.
- The power policy, duty seconds and assisted start fields are in the API only,
  for the same reason.
- The scoping note asked for a host unit test if the GPS layer has host tests.
  It does not, for the reason given above.
- The date and time merge on 135x240 is not in the scoping note. It is what
  makes the vertical room for the two rows.

### Owed

`docs/img/gps-data.png` is refreshed for this page. The rest of the committed
walkthrough gallery is stale repo-wide, it predates the focus outline change
among others, so this refreshes only the page this change touches. A gallery
refresh belongs in its own change.

An on-device look at the page on the M5StickS3. The rows are verified in the
simulator on all three modeled panels, in both the touch and the non-touch
layout, and the simulator shares the real UI code. The font 10 rows have still
not been read on the physical 135x240 panel, and the clearance from the floating
indicator is a few pixels, not a comfortable margin.
