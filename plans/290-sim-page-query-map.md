# 290 - simulator page query map sizing

## Motivation

`UI::simQueryState("page")` used a hand-counted `std::array` bound. Adding a
page without updating that count can fail compilation or hide a page identity
from the query path. The page map already has one source of truth: its
initializer list.

## Implementation state

Implemented the minimal fix in `src/FurbleUI.cpp`: the page map is now a
C++17 inferred-size array of `std::pair<const char *, const char *>`. The
existing range-based loop and all page mappings are unchanged. No fonts, UI
layout, navigation actions, or query behavior were modified.

## Semantic coverage and deviation

The existing scenarios provide representative route coverage, including
`sim/scenarios/e2e/level-main-menu.txt`,
`sim/scenarios/e2e/home-seven-rows.txt`, and
`sim/scenarios/bughunt/page-matrix.txt`. These exercise `nav level_main` and
then observe the canonical `ui.page` result `level` through the existing
simulator contract. `level_main` is intentionally an action alias for the
shared Level page, so a test must not compare navigation and query table
lengths or require identical names.

No new parity registry or length assertion was added. Such a registry would
duplicate the action and page tables and would incorrectly reject documented
aliases. Broader route coverage remains an existing simulator-test concern,
not a reason to expand this one-line compile-safety fix.

`tests/test_sim_ci_contract.py` now provides the smallest exhaustive guard
without a duplicate registry: it extracts the actual second strings from the
query-map initializer and compares them with `ui.page` assertions in every
certified scenario selected by `sim/scenarios/manifest.json`. The root page is
handled before the map, so `main` is intentionally outside this comparison.
The certified corpus currently covers all 55 mapped values. `level_main` is
still checked as an action alias whose canonical query result is `level`; the
guard compares query results, not action names or table lengths. A future
mapped page without a certified assertion now fails this host contract test.

## Validation boundary

Root owns builds and tests for this change. The isolated implementation lane
performed no compilation or test run.
