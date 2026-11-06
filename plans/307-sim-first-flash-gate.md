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
  approximate entry has a model, a proving scenario or executable runner, and a
  real `assert` step. `xassert`, comments and log strings are not evidence.
  The readiness gate executes every declared runner and fails on its exit code.
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

## Combined tree

The gate is evaluated on the combined tree, not each PR alone. The merge order
is recorded in `sim/coverage/merge_order.json`: `#394`, `#395`, `#396`, `#397`,
`#398`, `#393`, `#407`. `#397` covers settings migration, failed default seed
retries, WiFi fast-connect fallback and the PSRAM memtest split. `#398` covers
the single LVGL buffer, 3 KiB companion, 3 KiB plain MQTT worker, 1,536-byte
buffers, cached MQTT_HA, 8,192/4,096 MQTT start gate, 10,240/4,096 discovery
gate and fragmented discovery payload. `#407` moves the 9,392-byte MQTT event
queue to PSRAM, removes the early camera snapshot, gates owner creation at
18,432/12,288 (TLS 21,504/12,288) and restores the StickS3 12 KiB main stack.

## Gate status on master

The readiness report passes on master at `0883a31e`:

- hardware entries: 188 of 188 modeled, 0 approximate, 0 not modeled;
- crash entries: 20 of 20 modeled;
- fuzz targets: 10 of 10 modeled, with 136 seed runs, 89,146 iterations and
  zero failures in `sim/coverage/fuzz_results.json`, which records commit
  `0883a31e`;
- permutations: 6,912 of 6,912 passed in
  `sim/coverage/permutation_results.json`;
- memory profile: 13 of 13 dumps pass after
  `python3 sim/scripts/run-memory-profile.py` generates the ignored dumps in
  `.pio/sim-memory`;
- linked boundary conformance: 18 of 18.

The memory dumps are generated evidence and are not committed. A clean checkout
must run the profiler before the final readiness command. Do not delete a
manifest entry or edit a result file to make the report green.

The approximate SDL boundaries were closed by linked host conformance targets
that compile the production driver unchanged and replace only the lowest
hardware API. The runner is `sim/scripts/run-boundary-conformance.sh`; the
manifest entries name its scenario files and production sources. The host
targets cover display-less `src/main.cpp`, W5500 Ethernet, IR RMT, SD/GPX,
feedback, `CompanionGatt`, the companion task-stack restart and the WebUI
TLS/socket allocation path. Physical wiring, analog audio, the SD controller
and a real mbedTLS handshake remain hardware validation items, not simulator
claims.

Every manifest row names its scenario, runner or assertion. Keep the manifest
and the evidence aligned when a driver or gate changes.

## Commands

```sh
python3 tools/check_sim_scenarios.py
python3 tools/check_sim_coverage.py --require-ready
sh sim/scripts/check-doc-tokens.sh
sh sim/scripts/check-hardware-quirks.sh
python3 sim/scripts/run-memory-profile.py
python3 sim/scripts/run-permutations.py --compact
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

The last hardware flash, of the candidate before #407, reproduced the MQTT main
stack overflow. #407 is the fix and has not been flashed or retested. A green
simulator report does not close that hardware gate. Sim-only mode and the
resume criteria are recorded in
[313-sim-only-status.md](313-sim-only-status.md).
