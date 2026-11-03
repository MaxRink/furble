# 160 - simulator scenario ownership

The simulator scenario inventory is machine-checked in
`sim/scenarios/manifest.json`. The current inventory contains 127 scenarios:
every root, bug-hunt, end-to-end, and
invalid script has one entry with a suite, owner, supported board matrix,
capabilities, and exact expected process exit. The checker rejects unlisted,
duplicate, renamed, stale, or malformed entries.

Every suite runner obtains its certified scenario list from the manifest. That
includes recursive end-to-end and invalid fixtures, every certified bug-hunt
scenario on each declared board, and root power-gate scenarios. A certified
entry therefore cannot pass ownership validation without being selected for
execution, while non-certified historical scenarios are never picked up by an
incidental directory glob. Explicit targeted stress/sanitizer repetitions remain
separate from suite ownership. The older bug-hunt `interval-back-trap` remains only
as a non-certified historical duplicate of the end-to-end scenario, with that
rationale recorded in the manifest. CI path filters cover the simulator's compiled source,
headers, libraries, icons, host seams, corpus, configuration, tooling, and
ownership manifest on both pull requests and master pushes.
