# Measuring firmware coverage

`tools/coverage.py` measures how many firmware lines the tests actually reach,
and `.github/workflows/coverage.yml` stops that number from sliding.

Firmware is `src/`, `lib/furble/`, `lib/preferences/`, `lib/blowfish/` and
`include/`. LVGL, M5GFX, M5Unified, the simulator shims and the test harnesses
are excluded.

## Why two stacks are unioned

The host harness (`tests/host`) and the simulator (`sim/`) compile the same
firmware sources but exercise different halves of them. The host harness owns
the BLE protocol, the Control state machine and the console. The simulator owns
the UI, the pages and the settings flows. Neither percentage alone describes the
firmware, so the tool exports both, unions the per-line hit data and reports a
grand union.

The simulator is measured on all three modeled panel classes, because some code
is reachable on one panel only. The M5StickC power button read and the Core
touch read are structurally excluded from the M5StickS3 build, so they only
appear as covered once the 80x160 and 320x240 binaries are in the union.

## Running it

Needs `clang`, `cmake`, SDL2 and the matching `llvm-profdata` and `llvm-cov`.
On Debian and Ubuntu that is `clang llvm libsdl2-dev`. The tool picks the LLVM
tools whose major version matches the clang in use, because the raw profile
format is tied to it. Override with `LLVM_COV` and `LLVM_PROFDATA`.

```sh
python3 tools/coverage.py \
  --build-dir /tmp/fcov \
  --dep-root "$PWD/sim/.pio/libdeps/sim" \
  --lvgl-dir "$PWD/sim/.pio/libdeps/sim/lvgl" \
  --json coverage.json --markdown coverage.md --html html
```

Keep `--build-dir` short. The gpx-writer host test truncates paths into a 64
byte buffer, so a deep scratch path makes it fail for unrelated reasons.

Useful narrowing while iterating:

- `--stack host` or `--stack sim` measures one stack.
- `--board m5stick-s3` measures one panel.
- `--skip-build` reuses the binaries already in `--build-dir`.
- `--summary-from coverage.json` re-renders a report without measuring.

Coverage instrumentation is opt-in in both build entry points and off by
default, so nothing else in CI changes:

- Host: `cmake -S tests/host -B <dir> -DFURBLE_COVERAGE=ON`, or export
  `FURBLE_COVERAGE=1`.
- Simulator: `FURBLE_SIM_COVERAGE=1 sh sim/build.sh`, with its own
  `FURBLE_SIM_BUILD_DIR` so the instrumented objects never overwrite the
  release-config build.

## The floor

`tests/coverage_floor.json` holds the minimum percentage for each stack, for the
grand union, and for a short list of critical files. `--check` fails when any
measurement is below its floor, and also when a floor names a stack or file that
the measurement no longer contains, so a build change cannot quietly drop a
tracked target.

A change that raises coverage can raise the floor in the same commit:

```sh
python3 tools/coverage.py --ratchet --build-dir /tmp/fcov ...
```

`--ratchet` writes each measurement minus one point. A few tests are timing
dependent, so two runs of the same tree differ by a fraction of a point, and a
floor pinned to the exact measurement would fail its own next run. Use
`--ratchet-margin 0` to pin the exact number anyway.

Lowering a floor is allowed but must be deliberate: the diff shows it, and the
pull request has to say why the coverage dropped.

## What CI reports

The workflow runs on pull requests and master pushes that touch firmware, the
simulator, the tests or the tool itself. It writes the markdown table to the job
summary and uploads `coverage.json`, `coverage.md` and the per-stack HTML as the
`firmware-coverage` artifact.

The report goes to the job summary rather than a pull request comment because
`tools/check_ci_workflows.py` forbids `pull-requests: write` in any workflow with
a `pull_request` trigger. The firmware size report in `main.yml` uses the job
summary for the same reason.

## Guarding the guard

`tests/test_coverage_tooling.py` exercises the lcov parsing, the union, the
summary arithmetic and the floor comparison on synthetic inputs, so it runs
without clang or llvm. It includes mutation checks: raising a floor above the
measurement, in a temporary copy of the committed file, must fail the check.
