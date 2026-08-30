# PR121: simulator usage and fault-injection reference

## Goal

Keep the host simulator discoverable and testable. The reference documents the
build entry points, panel variants, scenario DSL, query catalog, fault seams,
and seeded fuzzers. A source-text checker keeps documented tokens from drifting
away from the harness.

## Implementation state

- `docs/sim.md` is the canonical simulator reference.
- `sim/CLAUDE.md` points contributors to the reference and records the local
  simulator contract, including the PM1 watchdog `stall` verb.
- `sim/scripts/check-doc-tokens.sh` checks the documented action and query
  vocabulary and, with `--links`, verifies local Markdown links.
- The `xassert` verb was already present in the simulator before this docs
  change. Its expected-failure behavior is documented and covered by the
  existing simulator scenarios.
- The reference includes the current bulb, intervalometer, GPS, watchdog, and
  multiconnect seams that landed after the original plan draft.
- The simulator action boundary now uses the shared typed parser in
  `sim/scenario_action.{h,cpp}` for both pre-runtime validation and UI
  dispatch. Strict verb/action arity, numeric and finite-value checks, unknown
  options, duplicate seeds, and malformed GPS query indices fail orderly with
  status 2. UI action outcomes are classified as `APPLIED`,
  `VALID_NO_EFFECT`, `UNAVAILABLE`, or `INVALID`; `page main` and `page menu`
  remain explicit root aliases, and scroll values exclude `INT32_MIN` because
  runtime negates the signed LVGL delta.
- Fuzz CLI values are authoritative over matching `FURBLE_FUZZ_SEED` and
  `FURBLE_FUZZ_STEPS` fallbacks. `--fuzz-verbose` enables fuzzing on its own,
  and `sim/scripts/run-fuzz.sh` documents and exercises the explicit argument
  contract.

## Verification

The documentation gate runs without hardware or network access:

```sh
sh sim/scripts/check-doc-tokens.sh
sh sim/scripts/check-doc-tokens.sh --links
```

The implementation spans the typed action parser (`sim/scenario_action.{h,cpp}`),
strict scenario/CLI parsing in `sim/driver.cpp`, typed UI dispatch and outcome
reporting in `include/FurbleUI.h` and `src/FurbleUI.cpp`, focused host parser
coverage, malformed fixtures, and the fuzz validation wrappers. The simulator
CI workflows build the supported panel classes and run the full scenario suite;
this working slice was limited to static review and `git diff --check`, with no
build or test execution in the implementation handoff.

## Follow-ups

When new simulator actions, seeds, queries, or fault APIs land, update
`docs/sim.md`, `sim/CLAUDE.md` when the local contract changes, and this plan's
implementation state in the same PR.
