# AGENTS

This file is for coding agents. The full guide is `CLAUDE.md`; read it first.
Directory-specific rules live in the `CLAUDE.md` of each directory you touch
(`src/`, `include/`, `lib/furble/`, `sim/`, `web-installer/`, `components/icons/`,
`plans/`).

## Review and merge gate

Do not merge a PR with an unresolved review thread. Every Copilot or human
finding must be fixed, or proven false or outdated with evidence, before merge.
Record it as fixed, already fixed, or a false positive in
`plans/NN-review-feedback.md`. Resolving a thread without a fix is not
acceptable. The online gate is
`python3 tools/check_review_threads.py --repo owner/name --pr N`; it fails
closed on API errors and is intentionally not part of the offline pre-review or
fork CI jobs. The remediation PR must pass it before merge.

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
The pre-review gate is `python3 tools/pre_review.py --base origin/master`; run
it before pushing. `pre-review.yml` runs the same scanners in CI, and
`host-sanitized.yml` runs the ASan/UBSan host subset when C++ paths change.
The `sim-sanitizers` job runs the e2e suite under ASan, UBSan and LeakSanitizer
on Linux; `sim/ubsan.supp` and `sim/lsan.supp` hold only third-party sites.
The pre-hardware fuzz budget is `python3 tools/check_fuzz.py --full
--host-build <dir> --mqtt-binary sim/build-mqtt/furble-sim`; the full corpus
currently passes 10 targets, 135 seed runs and 89,145 iterations with zero
failures. Its fixed seeds live in `tests/fuzz/seeds.json`; the earlier MQTT
owner stack overflow and Home Assistant discovery OOM are fixed and carry
regressions. The flash-readiness gate below remains the authority.
Static analysis is `python3 tools/check_static_analysis.py --base origin/master`
with the host and simulator compile databases; `static-analysis.yml` runs it in
CI.
The simulator power gate uses the two-sided `tools/power-model/compare.py`
threshold; its default 10% value is compatibility policy, not calibrated
hardware noise. Focused coverage is `python3 -m unittest tests/test_power_compare.py`.
The Apple workflow's macOS app artifact is unsigned and test-only; it is
uploaded only after the macOS tests pass and includes checksum/provenance files.
Release tags beginning with `companion-test-` skip firmware publication and
are reserved for companion testing; other release tags retain normal behavior.
All simulator scenarios are owned in `sim/scenarios/manifest.json`; run
`python3 tools/check_sim_scenarios.py` after adding, removing, or renaming one.
Run `sh sim/scripts/check-doc-tokens.sh` after changing scenario seeds; the
`sim-e2e` workflow runs it with the scenario check.
The built simulator preference ownership gate is
`sim/scripts/check-preferences-lifecycle.sh`; `sim-e2e.yml` runs it against the
fresh M5StickS3 binary.
The host TSAN wrapper is fail-closed; see `CONTRIBUTING.md` for its exact
completion marker and compiler-free contract command.
UI fuzz validation covers both touch and physical-button layouts on all three
panel binaries, using the same seeds and strict replay comparison.
UI screenshot CI selects `FURBLE_SIM_NO_TOUCH=1` and checks page identity before
every capture; its direct navigation routes do not certify physical input.
Memory-pressure scenarios seed `heap_internal_free` and
`heap_internal_largest` and assert the production WebUI and MQTT gates plus the
MQTT retry bounds. `sh sim/scripts/run-memory.sh` runs the MQTT subset against the optional
`FURBLE_SIM_MQTT=1` build with a closed local port, so no broker is required.
Crash-record scenarios use `crash-inject-panic` or `crash-inject-task-wdt`
followed by `restart`; the record is printed once on the next boot.
Every sdkconfig aborts on a failed allocation and the simulator does the same:
`SIM PANIC`, a panic record and exit status 3. `sh sim/scripts/run-panic-fixtures.sh`
certifies the abort fixtures with the default and `FURBLE_SIM_MQTT=1` binaries.
The simulator build force-includes the board's memory sdkconfig subset from
`tools/gen_sim_sdkconfig.py`. `tools/derive_sim_heap.py` derives each board's
boot heap ceiling into `sim/coverage/board_heap.json`, which the memory
profile seeds per board.
`tools/stack_budget.py` checks each firmware task's worst-case stack from the
`-fcallgraph-info=su` call graph against `tools/stack_budget.json`; firmware CI
runs it after every build. A recursion or an `alloca`/VLA frame on a task's
path fails until the manifest cites a bound from source. `platformio.ini` sets
`CJSON_NESTING_LIMIT=8`, which the cJSON bounds depend on. See
`docs/stack-budget.md`.
Hardware flashing is gated on the simulator. `sim/coverage/` holds the hardware,
crash, fuzz and permutation manifests; `tools/check_sim_coverage.py` validates
them and `tools/check_flash_readiness.py --ready` is the only authority for a
flash candidate. Run `python3 tools/check_sim_coverage.py --require-ready`,
`sim/scripts/check-hardware-quirks.sh`, `sim/scripts/run-memory-profile.py` and
the full permutation run before hardware.
`sh sim/scripts/run-boundary-conformance.sh` compiles the production headless
service loop and the IR, SD, feedback, W5500 Ethernet, CompanionGatt and WebUI
TLS/socket paths unchanged against peripheral doubles; those linked paths back
the boundary manifest entries. The fuzz harness publishes
`sim/coverage/fuzz_results.json` with
`python3 sim/scripts/run-fuzz-target.py --all`; every target must pass before
the readiness gate can pass. On master at `2d2be868` the gate passes: 188 of
188 hardware entries, 20 of 20 crash entries, 10 of 10 fuzz targets, 6,912 of
6,912 permutations, 13 of 13 memory dumps, 18 of 18 linked boundaries and 206 of 206 end-to-end scenarios. The
memory dumps are ignored generated evidence, so run
`python3 sim/scripts/run-memory-profile.py` before the final check. Hardware
validation is deferred in sim-only mode and #407 has not been hardware-retested;
see `plans/313-sim-only-status.md`.
Firmware line coverage is measured by `tools/coverage.py` and gated against
`tests/coverage_floor.json`. Coverage instrumentation is opt-in in both build
entry points (`-DFURBLE_COVERAGE=ON` for the host harness,
`FURBLE_SIM_COVERAGE=1` for `sim/build.sh`); never make it a default. Lowering a
floor needs a stated reason in the PR.
Manual PlatformIO dispatches must run the complete firmware matrix. Do not
derive a comparison range from repository root commits.
Gate board-specific ESP-IDF component dependencies with a CMake profile
argument as well as a compiler define. Otherwise every board resolves those
components before conditional C++ compilation can remove them.

Development builds using `FURBLE_VERSION=dev` identify the checkout as
`dev+g<unambiguous-hash>` and append `.dirty` for tracked, staged, or
non-ignored untracked changes. Ignored-only changes stay clean. Explicit release
versions remain unchanged. See `CLAUDE.md` for build details.

S3 release images disable `CONFIG_SPIRAM_MEMTEST`; the S3 debug environments
merge `sdkconfig.spiram-memtest` back in. The `boot timing` console command
prints the retained boot timeline. See `docs/boot-timing.md`.

The developer-only `m5stack-core-usb-debug` profile is USB-UART only and uses a
single factory app partition. Normal CI builds it as the mandatory Core debug
profile. Keep it out of release and web-installer matrices; the legacy
`m5stack-core-debug` OTA profile is an explicit workflow-dispatch opt-in, and
returning to dual OTA requires a USB partition-table reflash.

The StickS3 flash helper is fail-closed. Use `--preflight-only` to validate the
PMIC handshake without uploading. After a successful prepare, it must issue
`flash cancel` and confirm watchdog restoration before returning zero. The
historical `--dry-run` spelling has the same semantics. A missing,
unexecutable, or unsuccessful PlatformIO command after a successful preflight
must also trigger an automatic `flash cancel` attempt and report the restoration
result. Download recovery remains available after cancel by design, preserving
manual rescue for wedged devices. Never infer PMIC state from a missing port or
dependency.

## Dependency updates

Dependabot checks GitHub Actions, Python tooling, Android Gradle dependencies, and the Apple Swift package weekly. Routine minor and patch changes are grouped for review. Firmware pins in PlatformIO and ESP-IDF component manifests remain manual; Dependabot does not support those formats. Updates are proposed through pull requests and do not deploy or flash hardware. Pin Git-sourced PlatformIO dependencies to the full 40-character commit SHA so the VSCode extension can fetch them (upstream issue 309).
