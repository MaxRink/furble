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

Three rows are appended below the fix rows, in the smallest font so they read as
secondary:

| Row | Fields |
| :--- | :--- |
| `fix yes uart PDTA` | Fix present, fix source, and the four validity flags |
| `hdop 0.9 nmea 3s` | Horizontal dilution of precision, and sentence age |
| `waiting @1000ms` | Power cycle state, and the configured fix interval |

The validity flags are position, date, time and altitude. An upper case letter
means that field is valid, lower case means it is not. `nmea never` replaces the
age when no sentence has arrived yet. `@default` replaces the interval when the
rate setting leaves the receiver on its own rate. A degraded cycle appends its
retry count, so the row reads `degraded @1000ms x1`.

Each row packs several fields because vertical space is the binding constraint,
not horizontal space. Three rows of one field each would not fit.

## Why the 80x160 panel is excluded

The M5StickC page already filled its panel exactly. There was roughly one line
of slack left, and the three detail rows need six lines there once they wrap at
80 pixels.

Letting the page overflow was not an option. The GPS Data page carries no
focusable control, and furble's button navigation scrolls a page only by moving
LVGL group focus through the widgets on it. A page with no group members cannot
be scrolled by the buttons at all. On the M5StickC, overflowing rows would be
rendered below the fold with no way for the user to reach them.

So the rows are compiled out on `FURBLE_M5STICKC`, in the same spirit as the
existing `FURBLE_M5COREX` date and time split a few lines above. The Raw NMEA
page has a focusable Hot restart button, so it does scroll, and it keeps
carrying HDOP, the receiver counters and the degraded retry count on that board.

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

The page shows `cycle_state`, `rate_ms` and `last_sentence_age_ms`. The power
policy, duty interval and assisted start fields are in the struct because they
are receiver state a caller may want, and because omitting them would mean
another accessor later. They are not on the page: the policy and interval are
already visible in `Settings` > `GPS` > `Power saving`, and rows are scarce.

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
Reporting that as "rate" was actively misleading; the first draft did, and the
simulator caught it.

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

The scenarios read the labels through eight new simulator queries, which parse
the rendered label text rather than recomputing the value. An absent row reads
back as `none`, so a dropped label fails the assertion instead of passing
silently. Mutation check: removing the fix row update fails
`gps-receiver-detail` with `expected 'yes' got 'none'`.

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
  `addGPSDetailLabel()` row helper, the three rows on the page timer, and the
  eight simulator queries.
- `sim/driver.cpp`: `gps.source` reuses `GPS::sourceName()`.
- `sim/scenarios/`: two scenarios and their manifest entries.
- `docs/sim.md`, `docs/ui-walkthrough.md`, `docs/settings-and-controls.md`.

### Deviations from the original scope

- The scoping note asked for separate fix/source, HDOP, degraded and validity
  lines. Four rows overflowed 135x240, so the fields are packed into three.
- The scoping note asked for the power policy and duty seconds on the page.
  They are in the API only, for the reason given above.
- The scoping note asked for a host unit test if the GPS layer has host tests.
  It does not, for the reason given above.

### Owed

An on-device look at the page on the M5StickS3. The rows are verified in the
simulator on all three modeled panels, which shares the real UI code, but the
font 10 rows have not been read on the physical 135x240 panel.
