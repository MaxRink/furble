# Seeded fuzz corpus and gate

This directory owns the fixed seed corpus and the host memory-pressure driver.
`tools/check_fuzz.py` is the fail-closed entry point. A hardware image is not
built while it reports a finding.

## Files

- `seeds.json`: smoke and full seed budgets for UI input, WebUI, companion,
  MQTT, OTA and memory pressure. Change a seed here only with the replay
  evidence that justifies it.
- `memory_fuzz.py`: generates deterministic heap-pressure boot scripts and
  checks fail-closed behavior, watchdog liveness and crash-free shutdown. A
  failed allocation aborts the simulator like the device, so a passing seed
  proves the admission gates refused every service in time.
- `failures/`: created only when a finding reproduces. Each file is the exact
  log or generated script needed to replay that seed. The directory is ignored
  so a local finding is not committed by accident.

## Commands

Run the complete corpus and write the flash-readiness result file:

```sh
python3 sim/scripts/run-fuzz-target.py --all
python3 tools/check_flash_readiness.py --ready
```

Build the host fuzzers and run the pull-request budget:

```sh
cmake -S tests/host -B /tmp/furble-fuzz-host
cmake --build /tmp/furble-fuzz-host -j"$(sysctl -n hw.ncpu 2>/dev/null || nproc)" \
  --target webui_fuzz companion_fuzz mqtt_fuzz ota_mqtt_protocol_test
python3 tools/check_fuzz.py --smoke --host-build /tmp/furble-fuzz-host \
  --mqtt-binary sim/build-mqtt/furble-sim
```

Run the pre-hardware budget. This is the gate that must pass before a firmware
image is built:

```sh
python3 tools/check_fuzz.py --full --host-build /tmp/furble-fuzz-host \
  --mqtt-binary sim/build-mqtt/furble-sim
```

The simulator UI step needs all three panel binaries:

```sh
sh sim/build.sh
FURBLE_SIM_BUILD_DIR=$PWD/sim/build-stickc \
  FURBLE_SIM_FURBLE_BOARD=FURBLE_M5STICKC \
  FURBLE_SIM_M5GFX_BOARD=board_M5StickC sh sim/build.sh
FURBLE_SIM_BUILD_DIR=$PWD/sim/build-core \
  FURBLE_SIM_FURBLE_BOARD=FURBLE_M5COREX \
  FURBLE_SIM_M5GFX_BOARD=board_M5Stack sh sim/build.sh
```

## Replaying one finding

Every engine prints the seed that produced the finding. The exact commands are:

```sh
# UI input stream on one panel and layout
FURBLE_SIM_BIN=$PWD/sim/build/furble-sim \
  FURBLE_FUZZ_SEEDS=42 FURBLE_FUZZ_STEPS=1200 \
  sh sim/scripts/run-fuzz.sh

# WebUI, companion, MQTT and OTA
./host-build/webui_fuzz --seed 7 --iterations 1500
./host-build/companion_fuzz --seed 7 --iterations 1000
./host-build/mqtt_fuzz --seed 7 --iterations 1000
./host-build/mqtt_fuzz --seed 1 --iterations 1 --discovery-oom
./host-build/ota_mqtt_protocol_test

# One generated memory-pressure boot
python3 tests/fuzz/memory_fuzz.py --sim sim/build-mqtt/furble-sim --mqtt --seeds 7
```

When a product bug is fixed, keep the failing seed in `seeds.json` so the gate
continues to replay it. If a fix belongs to a parallel PR, leave the regression
check in place and record the owner in `known_findings` until that PR lands
instead of silently removing coverage. A landed fix moves to
`fixed_regressions` in `sim/coverage/fuzz_results.json` and keeps its replay
command.
