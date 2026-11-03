# 291 - upstream layout parity for About, GPS, and Connected

Plan 291 is the next free number after 290. This is the plan for open issue
[#281](https://github.com/MaxRink/furble/issues/281), the remaining page-parity
work from the three-way UI audit.

## State

Plans 107, 165, and 168 already changed the problem. Plan 168 closed the
measured Connected-page overflow on 320x240, and the Connected page now uses a
4-by-2 grid with deliberate scrolling at Large text. The issue's "eight entries
in a 3x2 grid" description is historical; today the grid has four columns and
two rows, and the page scrolls rather than clipping.

What remains is content parity, not overlap:

- **About** has seven info rows: Version, ID, Build, IDF, Uptime, Heap, and
  Reset. Upstream gkoh master has two. The extra rows exist on every modeled
  panel, in both the touch and physical-button layouts, at every text size.
  Resolved by decision: keep all seven rows and certify the scroll. No layout
  change was made. See "About decision and measurements" below.
- **GPS** has seven top-level rows: the enable switch, baud, rate, sentences,
  constellation, power, and assist. The upstream page is smaller. The extra
  receiver-configuration rows overflow on 135x240 and 320x240.
- **Connected** no longer reports the 13 px overflow plan 168 measured, but its
  eight entries are still more content than upstream. Whether to keep the
  scrolling grid or reduce the entry set is a product decision, not a layout
  bug.

The fork also fixed three upstream defects (main on 80x160 in both layouts, the
touch timer on 80x160, and the shutter overflow). Those stay fixed; parity work
must not reintroduce them.

## Approach

One small PR per page, in this order: About, GPS, Connected.

Each PR must:

1. Start from the current master geometry, not from the upstream file wholesale.
2. Keep whole words and the selected text size. No smaller fonts and no
   ellipsis-only labels.
3. Record the before and after `ui.overflow`, `ui.label_overlaps`,
   `ui.cut_labels`, and `ui.indicator_clearance` values on all three modeled
   panels (135x240, 80x160, 320x240), in both layouts where the page differs.
4. Include a screenshot pair from `sim/scripts/docs-capture.sh` for each board
   and size changed, so the decision is reviewable.
5. State explicitly whether the page is reduced to upstream's row count or
   keeps its rows and scrolls. A scroll is acceptable only when every row stays
   reachable and no label overlaps or is cut.
6. Re-run the upstream graft comparison that produced issue #281 and attach the
   result. If the graft branch is gone, reproduce it from the merge-base and
   record the merge-base commit.

## About decision and measurements

The About page keeps all seven info rows on every panel. No row is removed and
no font is shrunk. The page scrolls, which the audit already recorded as a
deliberate fork divergence from upstream's two rows (Version and ID). A scroll
is acceptable here because both ends are reachable and no label overlaps or is
cut. Every size each board offers was measured, and none showed an overlap or a
cut label.

No layout change was needed, so the before and after geometry is identical.
The PR adds the certification instead of changing the page.

Physical-button layout, default text size, measured on master `8cd6be97`
(the branch base) and unchanged after this PR. The SDL panel attaches a
touch device by default, so these runs seed `no_touch true`; this is the layout
every modeled board ships.

| panel | `ui.overflow` | `ui.label_overlaps` | `ui.cut_labels` | `ui.indicator_clearance` | `ui.scroll_bottom` at top | `ui.scroll_top` at bottom | Reset reachable |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 80x160 | yes | 0 | 0 | clear | 238 | 238 | yes |
| 135x240 | yes | 0 | 0 | clear | 213 | 213 | yes |
| 320x240 | yes | 0 | 0 | clear | 159 | 159 | yes |

Touch layout, default text size, same commit. The Core ships buttons, so its
touch row is the supplemental layout the SDL harness renders by default.

| panel | `ui.overflow` | `ui.label_overlaps` | `ui.cut_labels` | `ui.indicator_clearance` | `ui.scroll_bottom` at top | `ui.scroll_top` at bottom | Reset reachable |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 80x160 | yes | 0 | 0 | n/a | 135 | 135 | yes |
| 135x240 | yes | 0 | 0 | n/a | 151 | 151 | yes |
| 320x240 | yes | 0 | 0 | n/a | 133 | 133 | yes |

Supplementary size evidence, every size the board actually offers. The 80x160
board renders Small and Normal (its maximum) only; its Large value clamps to
Normal in the firmware. Every run at every size kept `ui.overflow yes`,
`ui.label_overlaps 0` and `ui.cut_labels 0`.

| panel | size | button `ui.scroll_bottom` | touch `ui.scroll_bottom` |
| --- | --- | --- | --- |
| 80x160 | Small (default) | 238 | 135 |
| 80x160 | Normal (maximum) | 611 | 369 |
| 135x240 | Small | 132 | 106 |
| 135x240 | Normal (default) | 213 | 151 |
| 135x240 | Large | 387 | 289 |
| 320x240 | Small | 159 | 133 |
| 320x240 | Normal (default) | 159 | 133 |
| 320x240 | Large | 359 | 333 |

`Reset` reachability was checked two ways. At `action scroll bottom` the
scenario reads the rendered Reset label through `ui.diag.about.reset`, and the
page reports `ui.scroll_bottom 0`, so the last row is at the bottom end. A
physical-button walk of the seven rows also landed on `Reset:_Power_on` with
`ui.focus_on_page yes` on all three panels. The walk is not in the gate because
the next-button name differs by board (b on the Sticks, c on the Core) and one
scenario file cannot switch names.

Gates added or extended:

- `sim/scenarios/bughunt/about-scroll-reachability.txt` bounds the default
  button-layout scroll extents (159 to 238 px), asserts both ends, no overlaps,
  no cuts, clear indicators, and the rendered Reset row.
- `sim/scenarios/bughunt/about-scroll-small.txt` and
  `about-scroll-large.txt` repeat the button-layout contract at `text_size 0`
  and `text_size 2`; their bounds are 132 to 238 px and 359 to 611 px across
  the three panels. `about-scroll-small-touch.txt` and
  `about-scroll-large-touch.txt` cover the same sizes in the touch layout,
  bounded 106 to 135 px and 289 to 369 px. Each seeds and asserts
  `setting.text_size` so a lost seed cannot pass. The 80x160 board clamps Large
  to its Normal maximum, which stays inside the Large range.
- `sim/scenarios/bughunt/page-matrix.txt` now requires About to overflow, no
  overlaps or cuts at both ends, and `ui.diag.about.reset_visible yes` at the
  bottom. Its default pass measures touch and `run-notouch.sh` repeats the
  shorter button layout.
- `ui.diag.about.reset_visible` is viewport-aware: it intersects the rendered
  Reset label with the current menu page, unlike the recursive
  `ui.diag.about.reset` lookup that only proves the label exists.

Captures: no layout, font, or row set changed, so the walkthrough screenshots
in `docs/ui-walkthrough.md` remain valid and no regeneration is owed. The
existing `settings-about.png` plus `scroll/default/about-2.png` and
`about-3.png` already picture the scrolling page. Decision evidence captures
(`about-top`, `about-bottom`, `about-reset-focused`) were read from all three
panel binaries as scratch artifacts and are not committed.

Upstream graft: the multi-tree graft lives only in the uncommitted audit
worktree, not on a branch, and is not reproducible from this checkout without
that overlay. The recorded result stands at merge-base `2b79ce8d` (audit tree
`a978add5`): upstream About has two rows, the fork's seven-row page scrolls
everywhere. This PR does not claim upstream row parity; it records the
divergence as the product decision.

## Gates

- `sim/scenarios/bughunt/about-scroll-reachability.txt`
- `sim/scenarios/bughunt/about-scroll-small.txt`
- `sim/scenarios/bughunt/about-scroll-large.txt`
- `sim/scenarios/bughunt/about-scroll-small-touch.txt`
- `sim/scenarios/bughunt/about-scroll-large-touch.txt`
- `sim/scenarios/bughunt/core-notouch-layout.txt`
- `sim/scenarios/bughunt/stick-notouch-layout-135.txt`
- `sim/scenarios/bughunt/stick-notouch-layout-80.txt`
- `sim/scenarios/bughunt/page-matrix.txt` and the certified overflow sweeps
- `sim/scripts/docs-capture.sh`
- `python3 tools/check_sim_scenarios.py`

Gate note: `stick-notouch-layout-80.txt` was already red on master `8cd6be97`,
and it does not visit About. Its Sensors block asserts `ui.overflow no` while
the page reports 51 px of scroll, which contradicts plan 168's recorded decision
that Sensors scrolls on that panel by design. The stale assert is unrelated to
the About change and is left to the Sensors work. The About contract on that
panel is covered by the new scenario, which passes.

No camera support or protocol claim is involved. Hardware validation is not a
gate for a layout change, but a physical-button board capture is required before
the page is called shipped.

## Issue

- [#281](https://github.com/MaxRink/furble/issues/281) stays open until all
  three page decisions land. Close it with the last page PR, not before.
