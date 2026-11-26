# 338 - End-to-end coverage for the untested UI pages

Status: scenarios and manifest entries only. No firmware change. The files were
executed against a simulator built from `4af87727` before they were added, and
again from this branch after review.

Related: [28](28-emulator.md) (the simulator), [165](165-sim-no-touch-layout.md)
(physical-button layouts).

## The gap

The page-name contract in `src/FurbleUI.cpp:5805-5862` lists 57 pages. The
certified `sim/scenarios/e2e` suite asserted 34 of them. The other 23 appeared
only in `sim/scenarios/bughunt`, which is not part of the certified e2e run: its
page-matrix walks those pages and asserts their identity and both scroll ends,
but a regression there cannot fail an e2e gate.

Measured by counting `assert ui.page <name>` across `sim/scenarios/e2e/*.txt`
and `sim/scenarios/bughunt/*.txt`.

## The scenarios

Eight files, grouped by shared setup, covering all 23 pages:

| File | Pages |
| :--- | :--- |
| `main-menu-gap-pages.txt` | delete, power_off, ir |
| `connected-gap-pages.txt` | remote_disconnect |
| `display-settings-pages.txt` | legend, theme |
| `bluetooth-pages.txt` | bluetooth, tx_power |
| `power-settings-page.txt` | power |
| `feedback-pages.txt` | feedback_events, feedback_volume |
| `gps-settings-pages.txt` | gps_rate, gps_sentences, gps_constellation, gps_power, gps_assist, gps_hold, gps_baud, gps_platform |
| `interval-spinner-pages.txt` | interval_count, interval_delay, interval_shutter, interval_wait |

Two pages cannot be reached through `action nav`: `power_off` because its entry
handler performs the power-off itself (`src/FurbleUI.cpp:3410-3418`), and `ir`
because it has no navigation entry (`src/FurbleUI.cpp:4415-4463`). Both use the
page route.

## Findings recorded, not fixed

1. The Power page cuts one label on the 135x240 panel. `ui.cut_labels` reads 1.
   The scenario carries it as `xassert`, which flips to an unexpected pass when
   the layout is fixed.
2. Leaving a GPS roller sub-page with `back` strands the encoder focus off the
   page: `ui.focus` reads ok while `ui.focus_on_page` reads no, so a following
   `select` changes nothing.
3. Focus is stable on every page entered straight from `gps`: all eight read
   `ui.focus_on_page` yes that way, while a sibling entry can strand it.
   `gps_baud` reads no entered from `gps_rate`, from `gps_assist` or from
   `gps_hold`, and a following `select` reloads the page the focus was left on
   rather than acting on the new one. The file uses `home` once, before the
   walk, and then visits the eight pages consecutively, so it asserts focus on
   `gps_rate` alone, and `gps_baud` and `gps_hold` are the two that read no on
   that walk.

## Verification

- All eight files ran against a simulator built from `4af87727` on the
  `m5stick-s3` board: 8 of 8 pass, four rounds. A rebuild from this branch, whose
  firmware sources are unchanged, passes the same eight in the default and the
  `FURBLE_SIM_NO_TOUCH` layouts, with and without the optional capability
  variables set.
- `feedback-pages.txt` requires `FURBLE_SIM_FEEDBACK`. The other seven run clean
  with every capability variable unset, because `addIRMenu` and the IR settings
  switch are unconditional and neither the `action page` route nor the `toggle`
  action consults the IR capability. Their manifest entries therefore declare no
  capabilities.
- `python3 tools/check_sim_scenarios.py` reports the manifest complete with the
  eight new entries.
- Screenshots are not part of this plan. The capture matrix is
  `sim/scripts/docs-capture.sh`, and it already covers the pages a walkthrough
  shows.

## Remaining

The focus-strand findings need their own PR because they are UI behaviour, not
coverage.

The board matrix is not closed: every entry lists `m5stick-s3`, so neither
`m5stick-c` nor `m5stack-core` is covered. For `m5stick-c` the reason is the
authoring environment, which could not build a variant. `m5stack-core` is a
different case. There `display-settings-pages.txt` cannot run at all, because
`legendSelectable()` is false off the Stick boards
(`src/FurbleUI.cpp:11340-11357`): for the Core the legend is a flex child of the
navigation bar, the Legend child page is never built, and `action nav legend`
has no route. The other seven are untested on the Core; nothing in this plan
excludes them there.
