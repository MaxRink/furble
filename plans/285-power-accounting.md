# Issue #285 power accounting

## State

Reporting scenarios now opt into a frozen, synthetic virtual-work accounting
model before simulator events begin. Ordinary UI and fuzz runs retain the
legacy profiler path and do not load an external model. Timer and UI-poll work
is represented in integer microseconds, queued as scalar pending work, and
placed across existing virtual slices without host wall-clock timing.

The synthetic work inputs are not callback-duration measurements or hardware
current coefficients. Existing baseline reports remain unchanged unless a
scenario explicitly opts into the accounting model. Hardware calibration and
the broader release gates remain separate work.

The opt-in YAML requires exactly one well-formed accounting block with version
1, calibration status, poll cost and provenance, plus a timer-cost map with
per-entry cost and provenance. Well-formed unused timer names may remain in
that map; an observed timer without a cost fails the run. The report
fingerprint includes the model digest, all current coefficients, mode/version,
calibration status, and canonical work-cost provenance.
The digest is over the exact model bytes, so formatting or whitespace changes
intentionally require a new comparator identity.
The comparator continues to accept the complete pre-accounting input set
without identity metadata as legacy; partial or unknown input sets are invalid.

## Validation status

The runtime integration adds bounded per-frequency work counters, preserves
task-wake sleep exclusion, disables only the legacy timer-fired sleep
exclusion in opt-in mode, and fails on unresolved pending work or checked
arithmetic errors. The comparator rejects accounting mode and canonical
cost/provenance fingerprint mismatches. Root owns the build and test gates for
this candidate. Root reports the full host build and 119/119 host tests passed
at the validated candidate (`2776a266`); the focused six-case suite also
passed. CI/simulator packaging and hardware calibration remain pending, and
these synthetic estimates do not establish physical quantitative accuracy.
The opt-in report also exposes the exact microsecond window and component
residencies; host profiler harnesses must provide the fail-closed
`requestFailureExit()` stub when linking this runtime.
S3 runtime smoke validation passed for the positive fixture with valid
accounting, exact microsecond fields, and zero pending work; the corrected
negative diagnostics scenario fails closed after the `about` action and now
identifies the missing observed-timer cost. Hardware accuracy remains
unvalidated.
