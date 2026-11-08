# Contributing

Contributions to the `furble` project are welcome. This fork tracks
[gkoh/furble](https://github.com/gkoh/furble) and offers its changes upstream.
`CLAUDE.md` and `AGENTS.md` carry the detailed engineering guide; this file is
the short contributor workflow.

## Use of LLMs

`furble` is carefully and purposefully crafted and will remain so for the
forseeable future.
All tools must be wielded responsibly and precisely, large language models are
no exception.

To ensure provenance and traceability, please ensure:
- you review all changes
- you understand the origin of the changes
- you take full responsibility for the changes
All contributions will continue to pass through human review before acceptance.

## Building

furble is an ESP-IDF 5.x project built with PlatformIO. It is not Arduino.

Install the pinned PlatformIO and serial-console tooling before using the
PMIC-safe uploader:

```sh
python3 -m pip install -r requirements.txt
```

The uploader also looks for `pyserial` in PlatformIO's virtual environment, so
it can be run with the system Python when PlatformIO was installed by `pio`
there. It never installs dependencies or contacts the device when the import
check fails.

Use `python3 tools/flash_prepare.py --port <device> --env <environment>` for a
PMIC-safe upload. Use `--preflight-only` to validate the handshake without
starting PlatformIO. The older `--dry-run` option remains an alias. Both forms
cancel a successful prepare and return zero only after confirming watchdog
restoration. Download recovery remains available after cancel by design. If
PlatformIO cannot be started or the upload exits unsuccessfully
after the watchdog is disabled, the helper also tries `flash cancel` and reports
whether restoration succeeded.

There are six release board environments: `m5stick-c`, `m5stick-c-plus`,
`m5stick-s3`, `m5stack-core`, `m5stack-core2`, and `waveshare-s3-eth`. Each has
a matching `-debug` environment that adds verbose logging and the USB serial
console. CI builds the six release environments and their mandatory debug
profiles. Releases publish the six release images and five OTA-compatible debug
images. The Core single-factory USB debug profile remains developer-only.

Normal CI uses the USB-only Core debug profile. The legacy dual-OTA
`m5stack-core-debug` profile is available only through **Run workflow** with
the `core_ota_debug` boolean enabled. If selected, its failure is strict.

Firmware prereleases use a `v*` tag followed by a published GitHub prerelease.
The tag starts the Pages installer build; publishing the release starts the
firmware artifact workflow. The artifact publishing job alone requires scoped
`contents: write` permission. Verify its manifest, `sha256sum.txt`, and source
provenance before flashing hardware.

Every build needs the `FURBLE_VERSION` and `FURBLE_TEST` variables:

```sh
FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e m5stick-s3
FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e m5stick-s3-debug -t upload
FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e waveshare-s3-eth
```

If the 4 MB Core debug image exceeds its dual-OTA slot, the developer-only
`m5stack-core-usb-debug` environment provides one large factory app partition:

```sh
FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e m5stack-core-usb-debug -t upload
```

This is a USB-UART recovery profile, not a release or OTA-compatible image.
Keep the existing NVS partition by avoiding a full-chip erase. Returning to a
normal OTA image requires USB reflashing its dual-OTA partition table and
`ota_data_initial.bin`.

Validate its partition and upload contract without a build:

```sh
python3 tools/check_single_factory_profile.py
```

All board OTA application partitions begin at `0x20000`. The repository sets
this explicitly for PlatformIO uploads, including `-t nobuild -t upload`, so
an existing firmware artifact is not sent to PlatformIO's historical `0x10000`
default. If flashing an artifact manually, use the app offset from its
`flash_args` file, currently `0x20000`, and do not erase NVS unless a separate
migration requires it.

Development builds report `dev+g<unambiguous-hash>`. A tracked, staged, or
non-ignored untracked change appends `.dirty`; ignored-only changes do not.
Explicit release versions remain unchanged. See `CLAUDE.md` for the complete
build identity guidance.

A settings configuration change must be applied consistently across all six
committed release `sdkconfig.<env>` files, never for just one env. See
`CLAUDE.md` for the build traps.

The S3 and Core2 release sdkconfigs disable the boot-time PSRAM memory test.
Their debug environments merge `sdkconfig.spiram-memtest` back in, and the debug sdkconfig
script accepts comma-separated fragments for any custom diagnostic profile.

Board-specific ESP-IDF component dependencies need both a compiler define and
a CMake profile argument. CMake resolves components before conditional C++
compilation. See the Waveshare Ethernet environment for the established form.

## Pre-review checks

Run the fast scanner set before opening or updating a pull request:

```sh
python3 tools/pre_review.py --base origin/master
```

It runs the diff hygiene scanner, the settings contract scanner, the C++ safety
scanner, the CI workflow checker, the simulator manifest checker, and the
scanner unit tests. It fails closed on review-blocking findings.

Run the sanitized host subset when touching C++ state, containers, camera
drivers, or simulator fixtures:

```sh
sh tools/run_sanitized_host_tests.sh --clean
```

Review threads are a merge gate, not a suggestion. Before merging any pull
request, every review finding must be fixed in the branch, or proven false or
outdated with evidence in the thread and in the remediation plan. Run the
online check against the pull request:

```sh
python3 tools/check_review_threads.py --repo owner/name --pr <number>
```

It queries the GitHub GraphQL API through `gh api graphql` and fails closed on
API errors, so a missing token or network is never treated as a clean result.
Do not wire this into offline pre-review or fork CI jobs. The remediation pull
request itself must pass the same check before it merges. Record the audit in
its `plans/NN-review-feedback.md` table.

`pre-review.yml` runs the scanners on every pull request; `host-sanitized.yml`
runs the ASan/UBSan subset when C++ paths change. The `sim-sanitizers` job in
`sim-e2e.yml` runs the certified e2e suite under ASan, UBSan and LeakSanitizer
on Linux. LeakSanitizer does not run on macOS, so reproduce a leak report in a
Linux container with `FURBLE_SIM_SANITIZE=address,undefined`, the job's
`ASAN_OPTIONS`, and `sim/lsan.supp` and `sim/ubsan.supp`. Those files list only
third-party sites, each with its reason. The scanners cover absolute
workstation paths, whitespace and generated artifacts, debug leftovers,
behavior/doc/test coupling, settings defaults and board branches, wire-ID
ledger drift, container bounds, unsafe filesystem removal, low-byte sentinel
assertions, and terminal-state lock acquisition.

Run the static analyzers when touching C/C++:

```sh
cmake -S tests/host -B .pio/static-build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake -S sim -B .pio/static-sim   -DCMAKE_EXPORT_COMPILE_COMMANDS=ON   -DFURBLE_DEP_ROOT="$PWD/.pio/libdeps/m5stick-s3"   -DFURBLE_LVGL_DIR="$PWD/managed_components/lvgl__lvgl"
python3 tools/check_static_analysis.py --base origin/master   --build-dir .pio/static-build --build-dir .pio/static-sim
```

The gate runs cppcheck and clang-tidy on changed files under `src/`,
`lib/furble/`, `include/`, and `sim/`. `.clang-tidy` records the check set;
`tools/check_static_analysis.py` records the cppcheck suppressions and their
reasons. `static-analysis.yml` runs the same command on pull requests that
touch C/C++ paths. A changed file with no compile command is reported as
skipped; do not treat a skip as analysis.

### Sim-first release gates

Hardware flashing is gated on the simulator. Run the coverage manifest, the
hardware-quirk gate, the memory-profile dump, the full input/API/memory fuzz
corpus, the full configuration permutations and the readiness report:

```sh
python3 tools/check_sim_scenarios.py
python3 tools/check_sim_coverage.py --require-ready
sh sim/scripts/check-doc-tokens.sh
sh sim/scripts/check-hardware-quirks.sh
sh sim/scripts/run-panic-fixtures.sh
python3 sim/scripts/run-memory-profile.py
python3 sim/scripts/run-fuzz-target.py --all
python3 sim/scripts/run-permutations.py --compact
python3 tools/check_flash_readiness.py --ready
```

`run-memory-profile.py` seeds each board from `sim/coverage/board_heap.json`.
After a change that moves static RAM or a boot allocation, rebuild the six
release envs and run `python3 tools/derive_sim_heap.py`; see
`docs/sim-memory-profiler.md`.

After a firmware build, `python3 tools/stack_budget.py --env <env>` checks
each task's worst-case stack from the `-fcallgraph-info=su` call graph against
`tools/stack_budget.json`. Firmware CI runs it after every build. Add a task
there when you create one. A recursion or an `alloca`/VLA frame on a task's
path fails the check until `recursion_bounds` or `dynamic_bounds` in the
manifest cites a bound from source. See `docs/stack-budget.md` for the
indirect call rules, the bounds and the limits.

`run-panic-fixtures.sh` needs the default binary and the `FURBLE_SIM_MQTT=1`
binary. Every sdkconfig aborts on a failed allocation, so the simulator does
too: each fixture must exit 3 with `SIM PANIC` and leave a panic record for the
next boot. A scenario cannot pass by surviving an allocation failure.

`tools/check_flash_readiness.py` validates per-target fuzz results, unique and
complete permutation results in manifest order, and the exact memory-profile
scenario set. A top-level `status: pass` alone is not evidence. Coverage
entries must name executable scenario `assert` lines; a matching token in a
comment or log string does not count.

## Testing

Validation workflows use path filters and run for pull requests stacked on any
base branch. To validate a branch manually, open the Actions tab, select the
workflow, choose **Run workflow**, and select the branch. Android dispatches
run unit tests and an APK build by default; enable `run_emulator` when the
slower emulator smoke test is needed. CI checks this trigger policy with
`python3 tools/check_ci_workflows.py`.
Node.js is also required for the installer event-flow regression in
`tests/test_check_ci_workflows.py`; the standard GitHub-hosted Ubuntu runner
provides it.
All simulator scenarios are catalogued in
`sim/scenarios/manifest.json`. Check ownership and certification metadata with
`python3 tools/check_sim_scenarios.py` after changing those scenarios, and
`sh sim/scripts/check-doc-tokens.sh` after changing scenario seeds. The
`sim-e2e` workflow runs both checks.
CI obtains each suite's certified paths from that manifest; do not introduce a
directory glob or hand-maintained suite list as an alternate source of truth.
The power-gate comparator applies its `--threshold` on both increases and
decreases. The default 10% value is compatibility policy, not calibrated
hardware noise. Run `python3 -m unittest tests/test_power_compare.py` for its
focused regression coverage.
Manual PlatformIO dispatches run the complete firmware matrix. They do not
attempt to infer changed paths without a pull request or push comparison base.

- Host tests compile the production camera code with plain clang or g++, no
  radio or ESP-IDF needed:

  ```sh
  cmake -S tests/host -B /tmp/furble-host-build
  cmake --build /tmp/furble-host-build
  ctest --test-dir /tmp/furble-host-build --output-on-failure
  ```

- The `tests/host/run_tsan_race.sh` gate fails on any ThreadSanitizer warning
  or non-zero child status and requires the exact completion line
  `control-connect-camera-race: PASS`. Check this shell contract without a
  compiler with `sh tests/host/run_tsan_race_contract.sh`.

- Run the fuzz gate before building a hardware image:

  ```sh
  python3 tools/check_fuzz.py --full \
    --host-build /tmp/furble-fuzz-host \
    --mqtt-binary sim/build-mqtt/furble-sim
  ```

  `--smoke` is the pull-request budget. The fixed seeds are in
  `tests/fuzz/seeds.json`; `tests/fuzz/README.md` lists the replay commands.
  The gate covers seeded input on all three panels in both layouts, the WebUI,
  companion, MQTT and OTA protocol fuzzers, the fixed memory-pressure
  scenarios and randomized heap-pressure boots. The full corpus currently
  passes: 10 targets, 135 seed runs, 89,145 iterations and zero failures. The
  earlier MQTT owner stack overflow and Home Assistant discovery OOM are fixed
  and carry regressions; no fuzz seed is open.

- Before a hardware flash, run the simulator evidence gate:

  ```sh
  python3 tools/check_sim_scenarios.py
  python3 tools/check_sim_coverage.py --require-ready
  sh sim/scripts/check-doc-tokens.sh
  sh sim/scripts/run-boundary-conformance.sh
  sh sim/scripts/check-hardware-quirks.sh
  sh sim/scripts/run-panic-fixtures.sh
  python3 sim/scripts/run-memory-profile.py
  python3 sim/scripts/run-permutations.py --compact
  python3 sim/scripts/run-fuzz-target.py --all
  python3 tools/check_flash_readiness.py --ready
  ```

  `check_flash_readiness.py --ready` is the only authority for a flash
  candidate. It refuses approximate or unmodeled hardware entries, pending
  fuzz results, unfixed failing seeds, incomplete or failed permutations,
  memory-budget misses, missing memory profile dumps and a dirty worktree, so
  commit or stash before the final run. The full permutation run is not a
  pull-request smoke test; a `--limit` run is smoke evidence only. See
  [docs/sim-coverage.md](docs/sim-coverage.md) and
  [docs/sim-memory-profiler.md](docs/sim-memory-profiler.md).

- The protocol and camera vector tests live under `tests/protocol` and
  `tests/camera`. CI runs them through `protocol-tests.yml` and
  `camera-tests.yml`.
- The host SDL simulator under `sim/` runs the real UI on a desktop. Build it
  with `sim/build.sh`. It drives scripted UI runs, the seeded UI fuzzer, and the
  documentation screenshot capture. Incremental builds use compiler depfiles;
  the cache stamp also identifies the absolute firmware, dependency, and LVGL
  roots so shared build directories cannot mix checkout objects. Verify the
  cache with `sh sim/scripts/test-build-deps.sh` after changing the build
  script. It never changes firmware behavior.
- UI screenshot CI uses `FURBLE_SIM_NO_TOUCH=1` for the StickS3 and asserts
  each page before capture. Its setup routes are not physical-input validation.
- UI fuzz CI runs all three panel binaries in touch and physical-button
  (`FURBLE_SIM_NO_TOUCH=1`) modes. Use the same `run-fuzz.sh` defaults in both;
  a passing touch run does not validate physical input-device lifetimes.
- After building the simulator, run `FURBLE_SIM_BIN=sim/build/furble-sim sh
  sim/scripts/check-preferences-lifecycle.sh` to check explicit and generated
  preference ownership; `sim-e2e.yml` runs the same gate in CI.
- The USB serial console in debug builds is the automation surface for settings,
  GPS, shutter, and status. See `docs/console-commands.md`.
- Hardware verification happens before a PR. Only Fujifilm cameras are available
  for hardware tests. Other vendors are covered by code review and the FauxNY
  test camera, and are declared untested in the PR.
- Coverage of the firmware sources is measured across both stacks with
  `python3 tools/coverage.py`, and CI holds it to the per-stack, union and
  per-file minimums in `tests/coverage_floor.json`. A change that raises
  coverage should raise the floor in the same commit with
  `python3 tools/coverage.py --ratchet`. See `docs/coverage.md`.
- The Apple workflow runs unsigned iOS simulator and macOS tests. After the
  macOS tests pass, it uploads `furble-companion-macos-debug-unsigned`, which
  contains the unsigned Debug app, a SHA-256 checksum, and source/Xcode
  provenance. This is a testing artifact, not a signed release or installer.
- Release tags beginning with `companion-test-` are reserved for companion
  testing and are excluded from the firmware release workflow. Other release
  tags retain the existing firmware build and publication path.

## Style

- Short declarative sentences. Plain English. No em-dashes anywhere: code
  comments, commits, PRs, and docs.
- 2-space indent. Match existing naming. clang-format 21 is enforced in CI; see
  `.clang-format`.

## Camera protocol changes

A new camera vendor or model needs its protocol data source cited in the PR:
a BLE HCI snoop log, a public protocol document, or an existing reference
implementation. State the hardware validation status explicitly.

## Plans

Each change is tracked by a numbered document under `plans/`. Update the
matching `plans/NN` doc in the same PR with the implementation state and any
deviation from the plan and its reason.

## Documentation

Docs must stay in sync with the code. Any PR that changes user-facing behavior,
settings, console commands, supported hardware, cameras, vendors, GPS units,
boards, or the UI must update the matching docs in the same PR. The doc surface
to change-type mapping is the "Documentation" section of `CLAUDE.md`. The wiki
is in-repo under `docs/wiki/`.

## Pull request checklist

A PR must have zero unresolved review threads before merge. Record every
Copilot or human finding as fixed, already fixed, or a false positive with
evidence. Resolving a thread without a fix is not acceptable. This rule also
applies to a remediation PR that closes review findings. The final gate is
`python3 tools/check_review_threads.py --repo owner/name --pr <number>`, after
`python3 tools/pre_review.py --base origin/master` and
`sh sim/scripts/check-doc-tokens.sh` pass.

- [ ] Every review thread is resolved with a fix or recorded evidence, not just marked resolved.
- [ ] Builds clean for the affected board envs (`FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e <env>`).
- [ ] Host tests pass (`ctest --test-dir /tmp/furble-host-build`) where they apply.
- [ ] clang-format 21 clean. No em-dashes. Plain English.
- [ ] Static analysis clean for changed C/C++ files.
- [ ] The matching `plans/NN` doc is updated with implementation state and deviations.
- [ ] Docs updated for this change (README / docs / wiki / walkthrough+screenshots as applicable).
- [ ] New camera or vendor work cites its protocol data source and states the hardware validation status.
- [ ] Hardware verified, or the untested surface is declared in the PR.

Reviewers confirm the mapped docs were updated for any user-facing, behavior, or
UI change before approving.

## Dependency updates

Dependabot checks GitHub Actions, Python tooling, Android Gradle dependencies, and the Apple Swift package weekly. Routine minor and patch changes are grouped for review. Firmware pins in PlatformIO and ESP-IDF component manifests remain manual; Dependabot does not support those formats. Updates are proposed through pull requests and do not deploy or flash hardware.

Pin Git-sourced PlatformIO dependencies to the full 40-character commit SHA. The VSCode PlatformIO extension fetches the pin as a remote ref, so a short SHA fails with `couldn't find remote ref` (upstream issue 309).
