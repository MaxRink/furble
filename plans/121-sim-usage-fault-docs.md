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

## Verification

The documentation gate runs without hardware or network access:

```sh
sh sim/scripts/check-doc-tokens.sh
sh sim/scripts/check-doc-tokens.sh --links
```

The simulator CI workflows build the supported panel classes and run the full
scenario suite. This PR changes documentation and a checker only, so no
firmware or hardware behavior changes.

## Follow-ups

When new simulator actions, seeds, queries, or fault APIs land, update
`docs/sim.md`, `sim/CLAUDE.md` when the local contract changes, and this plan's
implementation state in the same PR.
