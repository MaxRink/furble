# 356 - Simulator memory certification closeout

Status (2026-11-25): implemented on `test/sim-memory-certify`, branched from
master `92618bff`. Simulator only: no hardware was flashed or measured, and no
number below is a device measurement.

## Problem

Plan 314 closed the heap-fidelity work, but three memory gaps stayed open. All
three are about which simulator runs *count* as evidence, not about new
firmware behavior.

1. Four scenarios were non-certified in `sim/scenarios/manifest.json`:
   `memory-boot-combination`, `memory-mqtt-unreachable`,
   `memory-mqtt-low-heap-retry-limit` and `memory-pressure-mqtt-shed`. They
   need the optional native transport (`FURBLE_SIM_MQTT=1`), and the manifest
   said so with a `reason` instead of a certification. Because every runner
   selects `certified` entries only, `tools/coverage.py` skipped all four and
   their lines were missing from every measured build.
2. `tools/coverage.py` measured three simulator builds and none of them passed
   `FURBLE_SIM_MQTT=1`. `src/FurbleMQTT.cpp` is an empty translation unit
   unless `FURBLE_MQTT` is defined, so the production MQTT owner, its
   pre-task admission gate and the PSRAM routing gate inside it were reached by
   no measured simulator run. They appeared only in the report's "firmware
   sources in neither instrumented build" list.
3. The simulator shim still lacks `heap_caps_get_info` for the console `heap`
   path. That is a different branch (`fix/sim-mem-shims`) and is deliberately
   not duplicated here.

## Plan

Make the transport a manifest fact instead of a comment, so one table decides
which binary runs which scenario:

- `sim/scenarios/manifest.json` gains an optional `transport` field with the
  values `default` (the ordinary build) and `mqtt` (the optional native
  transport). The four scenarios above are certified with `"transport": "mqtt"`
  and their `reason` is gone.
- `tools/check_sim_scenarios.py` validates the field and filters on it.
  `--list-certified` selects one transport at a time and defaults to `default`,
  so every existing CI sweep keeps running the ordinary build.
- `sim/scripts/run-memory.sh` reads its scenario list from the manifest instead
  of naming four files, so the runner and the certification cannot drift.
- `tools/coverage.py` gains a fourth measured stack, `sim-mqtt`: the same
  `sim/build.sh` with `FURBLE_SIM_MQTT=1` added to the environment it already
  passes, on the 135x240 panel, running the manifest's `mqtt`-transport
  selection. It reports as `sim m5stick-s3 mqtt (135x240)` and joins both the
  `sim union` and the grand union.
- `.github/workflows/coverage.yml` installs `libmosquitto-dev` and
  `libcjson-dev`, because `sim/build.sh` exits rather than skipping the stack
  when the transport is missing.

## What ran

Worktree: `git worktree add /tmp/furble-memcert -b test/sim-memory-certify
origin/master`, at `92618bff`.

Environment: macOS, 12 cores, Apple clang 21.0.0, no `clang-18` available.
libmosquitto 2.1.4 and cjson 1.7.19 from Homebrew, SDL2 2.32.74 (sdl2-compat),
M5GFX/M5Unified/TinyGPSPlus from `.pio/libdeps/m5stick-s3`, LVGL 9.4.0 from
`managed_components`. `LLVM_COV` and `LLVM_PROFDATA` are the Apple LLVM 21
tools in `/Library/Developer/CommandLineTools/usr/bin`, which match the
instrumenting compiler's raw profile version 10. Homebrew LLVM 23 refuses those
profiles ("raw profile version mismatch: Profile uses raw profile format
version = 10; expected version = 11"), so the Apple tools are the only usable
pair here.

Scenarios:

```sh
FURBLE_SIM_MQTT=1 FURBLE_SIM_BUILD_DIR=$PWD/sim/build-mqtt sh sim/build.sh
sh sim/scripts/run-memory.sh
```

All four certified scenarios pass, in the order the manifest lists them:

| Scenario | Result |
| --- | --- |
| `memory-boot-combination` | PASS |
| `memory-mqtt-low-heap-retry-limit` | PASS |
| `memory-mqtt-unreachable` | PASS |
| `memory-pressure-mqtt-shed` | PASS |

No assertion was weakened and no scenario was marked expected-pass. The runs
report what the scenario comments predict: the pre-task gate at 13,543 free /
12,788 largest never creates the 10 KiB owner and `mqtt.connect_failures` stays
0 (`memory-mqtt-unreachable` reaches the 10-failure limit and `mqtt.blocked`
yes, the pressure scenario walks 17,713 to 5,425 to 11,977 free and back to
17,713 while `alloc_failures` stays 0).

Coverage:

```sh
LLVM_COV=/Library/Developer/CommandLineTools/usr/bin/llvm-cov \
LLVM_PROFDATA=/Library/Developer/CommandLineTools/usr/bin/llvm-profdata \
python3 tools/coverage.py --stack sim-mqtt --build-dir /tmp/fcov \
  --dep-root "$PWD/.pio/libdeps/m5stick-s3" \
  --lvgl-dir "$PWD/managed_components/lvgl__lvgl" \
  --json /tmp/fcov/sim-mqtt.json --markdown /tmp/fcov/sim-mqtt.md --skip-build
```

The run selected four scenarios, wrote four profiles, printed no scenario
failure and exited 0.

Gates: `python3 tools/check_sim_scenarios.py`,
`python3 tools/check_sim_coverage.py`, `sh sim/scripts/check-doc-tokens.sh`,
`python3 -m unittest tests.test_coverage_tooling` (63 tests),
`python3 -m unittest tests.test_sim_ci_contract tests.test_check_pr_hygiene
tests.test_check_ci_workflows` (87 tests),
`python3 tools/pre_review.py --base origin/master`.

## Numbers

Measured on Apple clang 21 with the Apple LLVM 21 tools. These are line counts
from one instrumented build, not device memory figures.

| Measurement | Covered | Instrumented | Coverage |
| --- | ---: | ---: | ---: |
| stack `sim m5stick-s3 mqtt (135x240)` | 4,915 | 28,651 | 17.15% |
| `src/FurbleMQTT.cpp` | 491 | 1,727 | 28.43% |

The production MQTT path is now measured where it was previously absent from
every report. In `src/FurbleMQTT.cpp` the four scenarios reach:

| Region | Lines | Hits |
| --- | --- | ---: |
| Pre-task owner gate, internal free/largest | 286-287 | 4 |
| PSRAM routing gate, `queueInPsram` | 432-437 | 19 |
| `inPsram()` under `CONFIG_SPIRAM` | 740-742 | 4 |
| Owner task placement in PSRAM | 765-769 | 3 |
| Event-copy allocation `MALLOC_CAP_SPIRAM` | 792 | 2 |

So the gate itself is measured and its PSRAM arm is taken, not just compiled.

Floors this run justifies, not written yet:

- stack `sim m5stick-s3 mqtt (135x240)`: **16.15** (17.15 minus the one point
  ratchet margin).
- file `src/FurbleMQTT.cpp`: **27.43** (28.43 minus the margin), with
  `src/FurbleMQTT.cpp` added to `CRITICAL_FILES` in the same ratchet.

They are not written because a floor is only comparable within one pinned clang
major (docs/coverage.md) and no `clang-18` exists on this host; writing an
Apple-clang number into a clang-18 floor is how a floor stops meaning anything.
`tests/coverage_floor.json` is unchanged.

The stack percentage is not comparable to the other simulator stacks either:
they run the whole certified sweep, while this one runs four scenarios, so
17.15% describes the MQTT owner's reach under memory pressure, not the
firmware's overall coverage.

### Measured CI result, clang-18, all four stacks

The `Coverage floor` workflow ran on this head (run 45725944601, `CC=clang-18`,
`LLVM_COV=/usr/lib/llvm-18/bin/llvm-cov`). It selected the new stack, printed
`Running 4 m5stick-s3 scenarios with 4 workers.`, and reported:

| Stack | Covered | Instrumented | Coverage | Floor |
| --- | ---: | ---: | ---: | ---: |
| host | 19,153 | 25,321 | 75.64% | 62.55% |
| sim m5stick-s3 (135x240) | 17,157 | 26,778 | 64.07% | 50.13% |
| sim m5stick-c (80x160) | 14,486 | 26,669 | 54.32% | 35.90% |
| sim m5stack-core (320x240) | 14,176 | 26,700 | 53.09% | 35.12% |
| sim m5stick-s3 mqtt (135x240) | 4,915 | 28,651 | 17.15% | n/a |
| sim union | 17,978 | 28,761 | 62.51% | 50.66% |
| **grand union** | 31,181 | 39,023 | 79.90% | 69.26% |

Every row is at or above its floor and the job ended with `Coverage is at or
above every floor.` The new stack reproduces the local 4,915 of 28,651 and 491
of 1,727 exactly under clang-18, so its counted and covered lines do not depend
on the compiler.

## Remaining gaps

1. **The predicted union-floor failure did not happen, so no floor moved.**
   This plan expected the union floors to need `--lower`, because unioning 1,727
   instrumented lines at 491 covered should have pushed the grand union below
   its committed 69.26% and the sim union below 50.66%. The full clang-18 run
   falsified that: with all four stacks measured, `sim union` is 17,978 of
   28,761, 62.51%, and the grand union is 31,181 of 39,023, 79.90%, against
   those same unchanged floors. The job printed `Coverage is at or above every
   floor.` and exited 0, so no `--lower` ratchet ran and
   `tests/coverage_floor.json` is untouched. The prediction came from the
   deliberately single-stack local run, where the union row prints that one
   stack's 17.15% because the host and panel stacks are unmeasured. From the
   printed rows the new stack costs about 2.2 points on the sim union and about
   2.4 on the grand union, which both rows absorb. Those two floor values stay
   unwritten on purpose: the stack floor needs its own ratchet, and the file
   floor would fail every partial run, which item 2 covers.
2. **`src/FurbleMQTT.cpp` is still absent from `CRITICAL_FILES`.** Its lines
   exist only in the MQTT build, so a file floor would fail every partial run
   that does not measure that stack. Add the file and its 27.43 floor in the
   same clang-18 ratchet.
3. **The MQTT stack is not the measured build for the budget scenarios.**
   `hw-memory-post394-budget`, `hw-memory-mqtt-10k-success` and the other
   `hw-memory-mqtt-*` scenarios stay on the default transport, where the
   scripted model in `sim/hardware_model.cpp` stands in for the production
   client. They assert the same budgets, but only the four scenarios certified
   here drive the real `FURBLE_SIM_MQTT` path. Moving those rows onto the
   transport build, or certifying them for both transports, is the follow-up
   that would raise the 17.15%.
4. **`heap_caps_get_info` is still missing from the simulator shim**, so the
   console `heap` path cannot report per-capability totals here.
   `fix/sim-mem-shims` owns it and this branch does not duplicate it.
5. **No hardware.** Every number is a host line count or a simulator assertion.

## Deviations

- **Object cache reuse.** `sim/build.sh` compiles one file at a time and this
  host runs at load average 20 or more with twelve cores, so a from-scratch
  simulator build takes hours. The 556 LVGL, M5GFX, M5Unified and icon objects
  were reused from the main worktree's `sim/build` cache, which is at the same
  commit (`92618bff`; `git diff 4af87727 92618bff` under `src/`, `include/`,
  `lib/`, `sim/`, `components/` and `tools/` is `sim/CLAUDE.md` and nothing
  else, so the cached translation units are the same sources); the cache's
  build stamp and depfile targets were rewritten for this build directory. The 164 firmware and simulator objects, including
  `src/FurbleMQTT.cpp`, were compiled in this worktree from this worktree's
  sources with `FURBLE_MQTT=1 FURBLE_SIM_MQTT=1`, and the link ran here. The
  build script itself is unmodified.
- **The local run measured only the `sim-mqtt` stack**, by design: it is the
  stack this change adds, and the host stack and the three panel stacks were
  not re-measured here. The four-stack numbers come from the `Coverage floor`
  workflow on this head (run 45725944601), not from this host.
- **`sim/scripts/run-memory.sh` is now manifest-driven.** It selects
  `--list-certified --suite e2e --board m5stick-s3 --transport mqtt` instead of
  naming four files, and fails loudly when that selection is empty. The four
  scenarios and their order do not change.
- **`CommittedFloorTest.test_shape` was relaxed** from "the floor names exactly
  these stacks" to "the floor names no unknown stack and every always-measured
  stack". Without that, adding a measured stack would force a floor value into
  the same commit, which is the cross-compiler ratchet this branch refuses to
  guess.
