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
All simulator scenarios are owned in `sim/scenarios/manifest.json`; run
`python3 tools/check_sim_scenarios.py` after adding, removing, or renaming one.
Manual PlatformIO dispatches must run the complete firmware matrix. Do not
derive a comparison range from repository root commits.
Gate board-specific ESP-IDF component dependencies with a CMake profile
argument as well as a compiler define. Otherwise every board resolves those
components before conditional C++ compilation can remove them.

Development builds using `FURBLE_VERSION=dev` identify the checkout as
`dev+g<unambiguous-hash>` and append `.dirty` for tracked, staged, or
non-ignored untracked changes. Ignored-only changes stay clean. Explicit release
versions remain unchanged. See `CLAUDE.md` for build details.

The StickS3 flash helper is fail-closed. Use `--preflight-only` to validate the
PMIC handshake without uploading. After a successful prepare, it must issue
`flash cancel` and confirm watchdog restoration before returning zero. The
historical `--dry-run` spelling has the same semantics. A missing,
unexecutable, or unsuccessful PlatformIO command after a successful preflight
must also trigger an automatic `flash cancel` attempt and report the restoration
result. Download recovery remains available after cancel by design, preserving
manual rescue for wedged devices. Never infer PMIC state from a missing port or
dependency.
