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
only when the call fails with `ESRCH`, and the matching
`.tmp.<pid>` sidecar is removed at the same time. A live PID is never touched,
so two concurrent simulators cannot delete each other's store.

Caller-selected `FURBLE_SIM_PREFS` paths do not match this namespace and are
never swept.

## Validation

`sim/scripts/check-preferences-lifecycle.sh` now:

- keeps the explicit-store and same-PID restart checks;
- crashes two runs and confirms two unique generated names;
- confirms a run in another directory does not sweep them;
- runs a normal simulator in the crash directory and confirms both dead-PID
  stores are reclaimed;
- confirms the caller-owned store still exists afterwards.

`sim-e2e.yml` already runs this script against the fresh S5 binary.

## Remaining limits

This is not a general temporary-file cleaner. A dead encoded PID cannot prove
that another process did not intentionally choose the same generated name, so
the sweep is deliberately restricted to the simulator-owned prefix and name
shape. It does not reclaim arbitrary leftovers, and it does not attempt to
recover interrupted writes inside a store.
