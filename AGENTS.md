# AGENTS

This file is for coding agents. The full guide is `CLAUDE.md`; read it first.
Directory-specific rules live in the `CLAUDE.md` of each directory you touch
(`src/`, `include/`, `lib/furble/`, `sim/`, `web-installer/`, `components/icons/`,
`plans/`).

## Documentation mandate

Any PR that changes user-facing behavior, settings, console commands, supported
hardware, cameras, vendors, GPS units, boards, or the UI MUST update the matching
docs in the same PR. The doc surface to change-type mapping is the
"Documentation" section of `CLAUDE.md`. UI changes must also regenerate the
`docs/ui-walkthrough.md` screenshots with `sim/scripts/docs-capture.sh`.

A PR that changes behavior without its doc update is incomplete. Verify every doc
claim against the code, not against a plan doc.

CI trigger changes must keep validation workflows usable for stacked pull
requests. Keep pull request jobs path-filtered and read-only for fork safety,
and run `python3 tools/check_ci_workflows.py` after changing workflow triggers.
