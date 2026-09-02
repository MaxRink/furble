# 163 - measured coverage on both stacks, with a ratcheting floor

Plan 162 quoted a number nothing in the repository could reproduce: a coverage
audit run by hand on 792815cd, 65.95 percent union firmware line coverage. The
audit was a one-off. Nothing measured coverage in CI, nothing published the
number, and nothing stopped the next pull request from lowering it. Every
coverage claim in a plan document or a pull request body was therefore an
assertion, not a measurement.

This plan makes the number reproducible, published on every pull request, and
monotonic.

## Numbering

161 is `feat/sim-real-control-2`, in flight. 162 is the merged console coverage
plan. 163 is the next free number. If 161 lands first the numbering is
unchanged; only the ordering of the two documents on the plans branch differs.

## What is measured

Firmware is `src/`, `lib/furble/`, `lib/preferences/`, `lib/blowfish/` and
`include/`. LVGL, M5GFX, M5Unified, TinyGPSPlus, the simulator harness and the
test harnesses are excluded, because coverage of a vendored dependency says
nothing about this project's tests.

The two test stacks compile the same firmware sources and exercise different
halves of them. The host harness owns the BLE protocol, the Control state
machine, the settings store and the console. The simulator owns the UI, the
pages, the navigation and the power policy. Neither percentage describes the
firmware on its own, so `tools/coverage.py` exports lcov from both and unions
the per-line hit data.

The simulator is measured on all three modeled panel classes, not just the
135x240 default, because panel-specific code is structurally excluded from the
other builds. `UI::buttonPEKRead` compiles into every build but is only reachable
on `board_M5StickC`, and `UI::buttonCRead` only on `board_M5Stack`. On the
135x240 binary both read as dead code. In the three-panel union they are covered,
which is the concrete demonstration that the union is the honest number.

## How it is measured

Both build entry points gained an opt-in flag. Neither default changes, so every
existing CI job builds exactly what it built before.

- Host: `cmake -S tests/host -B <dir> -DFURBLE_COVERAGE=ON`, which adds
  `-fprofile-instr-generate -fcoverage-mapping` to every target.
- Simulator: `FURBLE_SIM_COVERAGE=1 sh sim/build.sh`, which adds the same flags
  to firmware translation units only, and the profile runtime to the link. LVGL
  and M5GFX are deliberately left uninstrumented. They contribute nothing to the
  report, and instrumenting the render path slows every scenario.

`tools/coverage.py` then runs the suites the simulator workflows run, selected
through the same `tools/check_sim_scenarios.py --list-certified` manifest query
CI uses, with the same reported capabilities (`FURBLE_SIM_IR`,
`FURBLE_SIM_FEEDBACK`, `FURBLE_SIM_SD`) so the gated pages stay reachable. It
merges the raw profiles per stack, exports lcov per binary, and unions.

Each host test binary is exported on its own rather than in one `llvm-cov`
invocation. The host harness compiles the same source with different
definitions per target on purpose, and a shared invocation rejects the
mismatched function records.

Scenarios run in parallel, each with its own simulated NVS file through
`FURBLE_SIM_PREFS` and its own raw profile. Most scenario wall time is spent
waiting on the simulated clock rather than on the CPU, so this is the difference
between a 25 minute job and a 7 minute one. The coverage run never gates on a
scenario outcome. That belongs to `sim-e2e.yml` and `power-gate.yml`, and a
scenario that fails here still contributes the lines it reached. Unexpected exit
statuses are printed, because a scenario that stops passing shows up as a
coverage drop and the reason should be in the same log.

## The floor

`tests/coverage_floor.json` holds a minimum percentage per stack, for the grand
union, and for five critical files: `src/FurbleControl.cpp`,
`lib/furble/Camera.cpp`, `lib/furble/Scan.cpp`, `src/FurbleUI.cpp` and
`src/FurbleConsole.cpp`. `--check` fails when any measurement is below its
floor, and also when a floor names a stack or a file the measurement no longer
contains, so a build change cannot quietly drop a tracked target and pass.

`--ratchet` rewrites the floor from the current measurement, so a pull request
that raises coverage raises the floor in the same commit. It subtracts one point
by default. Plan 162 measured the console suite at 90.07 to 90.42 percent across
repeat runs, the spread coming from a timing dependent power log tick, so a
floor pinned to the exact measurement would fail its own next run. The committed
floor is literally `--ratchet` output on this branch, which is why every value
sits one point below the measurement below.

Lowering a floor stays possible, and stays visible: it is a diff, and the pull
request has to justify it.

## CI shape

`.github/workflows/coverage.yml` runs on pull requests and master pushes that
touch firmware, the simulator, the tests or the tool, with the path filter,
`workflow_dispatch` trigger and read-only permissions
`tools/check_ci_workflows.py` requires.

The report goes to the job summary, not to a pull request comment.
`tools/check_ci_workflows.py` rejects `pull-requests: write` in any workflow with
a `pull_request` trigger, which is a deliberate stacked-PR safety rule, and the
firmware size report in `main.yml` publishes to the job summary for the same
reason. The markdown carries a `<!-- furble-coverage-report -->` marker so a
future workflow that is allowed to comment can adopt it as a sticky body without
changing the tool.

`tests/test_check_ci_workflows.py` asserts the count of pull-request workflows,
so it moves from 8 to 9.

## Measured on this branch

Method: clang 14 `-fprofile-instr-generate -fcoverage-mapping`,
`llvm-profdata merge -sparse`, `llvm-cov export --format=lcov`, union by line.

| Stack | Covered | Instrumented | Coverage |
|---|---:|---:|---:|
| host | 7737 | 12194 | 63.45% |
| sim m5stick-s3 (135x240) | 6817 | 11722 | 58.16% |
| sim m5stick-c (80x160) | 5165 | 11663 | 44.29% |
| sim m5stack-core (320x240) | 5034 | 11653 | 43.20% |
| sim union | 6936 | 11784 | 58.86% |
| grand union | 14092 | 20414 | 69.03% |

The audit on 792815cd measured host 57.44 percent, sim 135x240 58.09 percent and
union 65.95 percent. The 135x240 number reproduces within 0.07 points, which is
the cross-check that this tool measures the same thing the audit did. The host
number moved because PR #259 added `src/FurbleConsole.cpp` to the host suite, and
the union moved with it.

Panel-specific input handlers, region hit counts from `llvm-cov export`:

| Handler | 135x240 | 80x160 | 320x240 |
|---|---:|---:|---:|
| `UI::buttonPEKRead` | 0 | 1698 | 0 |
| `UI::buttonCRead` | 0 | 0 | 1488 |
| `UI::touchRead` | 0 | 0 | 0 |

`buttonPEKRead` and `buttonCRead` are covered by the alternate panels exactly as
expected. `touchRead` is not, and this is the first time that is a measured fact
rather than an assumption: it is registered only for `board_M5StackCore2` and
`board_M5Tough`, and the widest modeled simulator panel is `board_M5Stack`, which
has no touchscreen. A Core2 simulator panel is the follow-up that closes it.

Firmware files under 30 percent in the grand union:

| File | Covered | Instrumented | Coverage |
|---|---:|---:|---:|
| `lib/blowfish/Blowfish.cpp` | 0 | 68 | 0.00% |
| `lib/furble/CanonEOS.cpp` | 0 | 38 | 0.00% |
| `lib/furble/CanonEOSRemote.h` | 0 | 2 | 0.00% |
| `lib/furble/CanonEOSSmart.h` | 0 | 2 | 0.00% |
| `lib/furble/DJIOsmo.cpp` | 0 | 403 | 0.00% |
| `lib/furble/FauxNY.cpp` | 0 | 61 | 0.00% |
| `lib/furble/NikonBase.cpp` | 0 | 91 | 0.00% |
| `lib/furble/NikonBase.h` | 0 | 2 | 0.00% |
| `lib/furble/NikonRemote.cpp` | 0 | 76 | 0.00% |
| `lib/furble/NikonSmart.cpp` | 0 | 190 | 0.00% |
| `src/FurbleCalibrate.cpp` | 1 | 120 | 0.83% |
| `lib/furble/CanonEOSSmart.cpp` | 6 | 166 | 3.61% |
| `lib/furble/Sony.cpp` | 7 | 150 | 4.67% |
| `lib/furble/Lumix.cpp` | 15 | 185 | 8.11% |
| `lib/furble/CanonEOSRemote.cpp` | 6 | 64 | 9.38% |
| `lib/furble/CameraList.cpp` | 30 | 215 | 13.95% |
| `lib/furble/Nikon.cpp` | 24 | 130 | 18.46% |
| `include/FurbleGPS.h` | 2 | 7 | 28.57% |

That list is the roadmap. Every vendor except Fujifilm and Ricoh is close to
unmeasured, which is the same gap plan 159 addresses through virtual camera
peers, and `lib/blowfish/Blowfish.cpp` at zero is notable because it is pure,
deterministic, hardware-free code that a host test could cover completely.

Nine firmware sources are compiled by neither stack and so contribute no lines
at all: `src/FurbleBtDebug.cpp`, `src/FurbleCompanion.cpp`,
`src/FurbleFeedback.cpp`, `src/FurbleIR.cpp`,
`src/FurbleOTAPartitionSinkArduino.cpp`, `src/FurbleOTAPartitionSinkESP.cpp`,
`src/FurblePlatform.cpp`, `src/FurbleUIAudit.cpp` and `src/main.cpp`. The report
lists them on every run so the total cannot be flattered by removing a file from
a build list. Plan 162's inventory gate already stops new firmware sources from
joining that list.

## Guarding the guard

`tests/test_coverage_tooling.py` exercises the lcov parser, the union, the
summary arithmetic and the floor comparison on synthetic inputs, so it needs no
clang, no llvm and no built binaries. It includes mutation checks: a stack floor,
a union floor and a file floor each raised above the measurement must each
produce exactly one failure, a floor naming a target the measurement no longer
contains must fail rather than pass silently, and raising the committed union
floor to 100 percent in a temporary copy must fail the check while the committed
file stays untouched.

## Deliberate non-goals

- No branch or function coverage. Line coverage is what the audit measured and
  what the floor gates on. Adding more axes now would make the floor noisier
  without making it more useful.
- No coverage requirement on new code specifically. The union floor already
  makes a large untested addition fail, and a per-diff gate needs a base
  measurement this job does not yet carry.
- No PR comment, for the workflow policy reason above.

## Follow-ups

- Cover `lib/blowfish/Blowfish.cpp` with a host test. It is the cheapest zero in
  the list.
- Add a Core2 simulator panel so `UI::touchRead` is reachable.
- Raise the floor as plan 159's camera peers land, which is what the ratchet is
  for.
- Plan 161 (`feat/sim-real-control-2`) puts the real Control into the simulator.
  It will move the simulator numbers, most likely upward. Ratchet the floor in
  that pull request rather than here.
