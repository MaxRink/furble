# 160 - simulator scenario ownership

The simulator scenario inventory is machine-checked in
`sim/scenarios/manifest.json`. The current inventory contains 133 scenarios:
every root, bug-hunt, end-to-end, and
invalid script has one entry with a suite, owner, supported board matrix,
capabilities, and exact expected process exit. The checker rejects unlisted,
duplicate, renamed, stale, or malformed entries.

The CI workflow explicitly executes the three useful orphan guards:
`back-nav-diagnostics`, `connect-fail-progress`, and `feedback-hidden-route`.
The older bug-hunt `interval-back-trap` remains only as a non-certified
historical duplicate of the end-to-end scenario, with that rationale recorded
in the manifest. CI path filters cover the simulator's compiled source,
headers, libraries, icons, host seams, corpus, configuration, tooling, and
ownership manifest on both pull requests and master pushes.
