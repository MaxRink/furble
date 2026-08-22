<!--
Keep it plain English. No em-dashes. Short declarative sentences.
See CONTRIBUTING.md and CLAUDE.md.
-->

## Summary

Motivation first, then what changed.

## Plan

Which `plans/NN` doc does this PR implement or update?

## Checklist

- [ ] Builds clean for the affected board envs (`FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e <env>`).
- [ ] Host tests pass (`ctest --test-dir /tmp/furble-host-build`) where they apply.
- [ ] clang-format 21 clean. No em-dashes. Plain English.
- [ ] The matching `plans/NN` doc is updated with implementation state and deviations.
- [ ] Docs updated for this change (README / docs / wiki / walkthrough+screenshots as applicable).
- [ ] New camera or vendor work cites its protocol data source and states the hardware validation status.
- [ ] Hardware verified, or the untested surface is declared below.

## Documentation

Which docs did this change touch? User-facing behavior, settings, console
commands, supported hardware, cameras, vendors, GPS units, boards, or UI changes
must update the mapped docs in this same PR. UI changes must regenerate the
`docs/ui-walkthrough.md` screenshots with `sim/scripts/docs-capture.sh`. See the
"Documentation" section of `CLAUDE.md` for the full mapping.

## Testing

How was this verified? Note any untested vendor or hardware surface.
