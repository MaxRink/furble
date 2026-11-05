# 307: sim-first hardware and memory release gate

## Acceptance order

1. Work on the simulator.
2. Reproduce every known crash in the simulator.
3. Get memory management fully working.
4. Optimize with the simulator.
5. Test all configuration permutations and features in the simulator.
6. Only then flash a new build.

Hardware flashing is blocked until `tools/check_flash_readiness.py --ready`
reports `READY TO FLASH`. A passing subset is not completion.

## What the gate enforces

- `sim/coverage/hardware_quirks.json` is the denominator for every supported
  board/profile and every hardware or memory quirk. Every modeled or
  approximate entry has a model, a proving scenario or runner, and an assertion
  that the scenario contains.
- `sim/coverage/crashes.json` enumerates every known crash and boot loop. The
  release gate requires every entry to be `modeled`, every scenario to exist,
  and every known failing seed to be fixed with a regression.
- `sim/coverage/fuzz_targets.json` consumes the parallel `fuzz-suite` results.
  The gate fails when a target is pending, a known failing seed is unfixed, or
  `sim/coverage/fuzz_results.json` is missing or not passing.
- `sim/coverage/permutations.json` expands 8 memory boards against `web_ui`,
  `mqtt`, `mqtt_ha`, `companion`, `wifi`, `camera`, `psram` and `mqtt_tls`.
  The current expected count is 6,912. The gate requires one passing result per
  combination.
- `sim/coverage/memory_budget.json` is compared against the JSON dumps from
  `sim/scripts/run-memory-profile.py`. It checks free, largest, fragmentation,
  leaks, allocation failures and component sizes.
- `sim/scripts/check-hardware-quirks.sh` runs the fail-closed fixtures and the
  positive power, USB, WebUI, MQTT and memory scenarios.

## Simulator additions

- `sim/hardware_model.cpp` carries board profiles, PMIC variants, power-button
  state, USB Serial/JTAG ROM download, reset reasons, radios, companion state,
  peripheral faults, OTA slots and the MQTT stack regression model.
- `sim/memory_profile.cpp` carries the per-board static map and JSON dumps.
- `sim/heap_model.cpp` carries the tagged internal/DMA/PSRAM pools, exact-size
  failure injection, watermarks, timeline and leak detection.
- New scenarios live under `sim/scenarios/e2e/hw-*` and `memory-*`.

## Current blockers

The first readiness report is expected to be blocked. It names:

- the approximate boundaries where the SDL build does not link the production
  driver: display-less UI, W5500 Ethernet, IR RMT, SD writer, speaker,
  `CompanionGatt` and mbedTLS/socket allocations;
- the parallel fuzz targets that have no results yet;
- the full permutation run, which is intentionally not a pull-request smoke
  run.

Each blocker names its missing evidence in the manifest. Do not delete a
blocker to make the gate green.

## Commands

```sh
python3 tools/check_sim_scenarios.py
python3 tools/check_sim_coverage.py --require-ready
sh sim/scripts/check-doc-tokens.sh
sh sim/scripts/check-hardware-quirks.sh
python3 sim/scripts/run-memory-profile.py
python3 sim/scripts/run-permutations.py \
  --results sim/coverage/permutation_results.json
sh sim/scripts/run-fuzz.sh
python3 tools/check_flash_readiness.py --ready \
  --write-report plans/evidence/flash-readiness.md
python3 tools/pre_review.py --base origin/master
```

## Flash candidate report

The report must name the exact commit and firmware artifact, the outputs of
every gate above, and the hardware test plan: StickS3 USB boot, WebUI gate log,
MQTT owner/worker/HA, USB ROM recovery, power-button single/double/long, BLE
auth/reidentify, crash record and a memory soak. No hardware action is taken
until the report says `READY TO FLASH`.
