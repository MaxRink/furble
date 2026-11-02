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

## Gates

- `sim/scenarios/bughunt/core-notouch-layout.txt`
- `sim/scenarios/bughunt/stick-notouch-layout-135.txt`
- `sim/scenarios/bughunt/stick-notouch-layout-80.txt`
- `sim/scenarios/bughunt/page-matrix.txt` and the certified overflow sweeps
- `sim/scripts/docs-capture.sh`
- `python3 tools/check_sim_scenarios.py`

No camera support or protocol claim is involved. Hardware validation is not a
gate for a layout change, but a physical-button board capture is required before
the page is called shipped.

## Issue

- [#281](https://github.com/MaxRink/furble/issues/281) stays open until all
  three page decisions land. Close it with the last page PR, not before.
