# 295 - reclaim dead-PID simulator preference stores

## Motivation

Scripted simulator runs create one flash image per process:
`.pio/furble-sim-preferences-<scenario>-<pid>-XXXXXX.bin`. Orderly exits remove
it, but any abnormal exit leaves it behind with no bound. Issue #289 records
this as the per-PID preferences leak.

## Implementation state

`preparePreferences()` now scans the generated-preference directory before it
creates a new store. It only considers the exact simulator-owned name shape:

```text
furble-sim-preferences-<scenario>-<pid>-<six characters>.bin
```

For each match it parses the PID and calls `kill(pid, 0)`. A store is removed
only when the call fails with `ESRCH`, the sibling `.owner` marker says
`furble-sim-generated <same pid>`, and the marker matches the filename. The
matching `.tmp.<pid>` sidecar and the marker are removed at the same time. A
live PID is never touched, so two concurrent simulators cannot delete each
other's store.

Caller-selected `FURBLE_SIM_PREFS` paths do not carry the owner marker, even if
they use the same filename shape, and are never swept.

## Validation

`sim/scripts/check-preferences-lifecycle.sh` now:

- keeps the explicit-store and same-PID restart checks;
- crashes two runs and confirms two unique generated names;
- confirms a run in another directory does not sweep them;
- runs a normal simulator in the crash directory and confirms both dead-PID
  stores are reclaimed;
- confirms the caller-owned store still exists afterwards.

`sim-e2e.yml` already runs this script against the fresh M5StickS3 binary.

## Remaining limits

This is not a general temporary-file cleaner. The owner marker is what proves
the store was created by the simulator; a filename shape alone is not enough.
The sweep does not reclaim unmarked leftovers, and it does not attempt to
recover interrupted writes inside a store.
