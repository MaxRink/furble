# 308 - Input, API and memory fuzzing gate

Status: implemented on `fuzz/input-api-memory`. The gate is intentionally red
for two hardware crashes owned by parallel PRs: the MQTT owner stack at 4/6 KiB
and the Home Assistant discovery OOM. No hardware image is built while those
findings are present.

## Goal

Find input, API and memory bugs now, keep a deterministic replay for every one,
and block the next hardware build until the known crashes are fixed. The work
does not rewrite the simulator memory model or the production RAM/MQTT branches
owned by the parallel worktrees.

## Input fuzzing

`sim/fuzz.cpp` keeps the seeded Apply/Settle/Check/Escape machine and extends
the event set to 27 classes:

- physical taps and timed holds through `simPressButton` and `simHoldButton`,
  including the 900/1000/1050/1250/1600/2000 ms points around the one-second
  repeat threshold;
- every menu page, scan rows, camera pairing request/accept/reject/expire,
  companion pairing request/answer, connect/disconnect, BLE link kill, blind
  remote, shutter and focus, bulb start/stop, intervalometer start/stop,
  presets, button mode, and the boolean setting switches.

The fit rule now follows the shipped layout contract instead of assuming every
compact page fits: physical-button Home/Connected scroll by design, Display and
Timer scroll, narrow Stick touch Remote scrolls, and the Core touch Home and
Remote keep strict fit. This removes the false overflow findings that the
previous rule raised after plans 165 and 168 changed the layouts.

`sim/ui_buttons.cpp` pins B on the Stick top edge with a compile-time
`static_assert`, so a layout change cannot turn it into a side button.

## API fuzzing

New host fuzzers reuse the existing production sources and doubles:

- `tests/host/fuzz/webui_fuzz.cpp`: malformed/partial/oversized JSON, wrong
  types and ids, missing/wrong auth, method and path mismatches, duplicate
  keys, cross-origin and content-type failures, concurrency, and post-fuzz
  recovery.
- `tests/host/fuzz/companion_fuzz.cpp`: malformed auth, settings, camera,
  location and trigger writes, fragmented and oversized packets, auth gating,
  reconnects, and a clean post-fuzz authentication and settings list.
- `tests/host/fuzz/mqtt_fuzz.cpp`: malformed topics, oversized and fragmented
  payloads, retained and wildcard traffic, bursts, duplicate/rapid operations,
  and a post-fuzz valid command.
- `tests/host/ota_mqtt_protocol_test.cpp` gains a deterministic randomized
  session sequence over begin/chunk/commit/abort with the existing 200k
  malformed-payload loop.

The MQTT cJSON host double now tracks live allocations and can fail them from a
seeded schedule. This is how the discovery OOM is reproduced without hardware:
the production tree builder leaks nodes when `cJSON_Add...` returns null.

## Memory fuzzing

`tests/fuzz/memory_fuzz.py` generates deterministic heap-pressure boots and
forces MQTT task allocation failures where the simulator seam supports them. It
asserts no crash, main-page survival, a fed watchdog, valid WebUI states and
fail-closed behavior when a gate blocks. The fixed hardware memory model and
expanded allocation injector remain owned by `sim-mem-crash`; this driver only
adds randomized schedules on the shared seams.

The known MQTT findings are checked directly:

- `tools/check_fuzz.py` resolves the production MQTT owner stack expression and
  fails below 10240 bytes.
- `mqtt_fuzz --discovery-oom` fails on any leaked cJSON allocation while Home
  Assistant discovery is rebuilt under allocation failure.

Both findings are recorded in `tests/fuzz/seeds.json` with their parallel owner.
They stay in the gate until those PRs land; they are not marked expected-pass.

## Gate

`tools/check_fuzz.py` is the single entry point. `--smoke` is the pull-request
budget and `--full` is the pre-hardware budget. It runs the host fuzzers, OTA
randomized session test, UI fuzz on all three panels in both layouts, the fixed
MQTT memory scenarios, and the randomized memory boots. A finding prints its
seed, the reproduction command and the owner when known. `sim-e2e.yml` builds
the host targets and runs the smoke gate after the dedicated UI fuzz steps.

## Current findings

1. `mqtt-owner-stack`: `src/FurbleMQTT.cpp` creates the owner at 6144 bytes.
   Hardware overflowed at 4096 during start and 6144 on the first connected
   event. Owner: `fix/mqtt-task-stack`, which uses 10240.
2. `ha-discovery-oom`: Home Assistant discovery leaks cJSON nodes when an
   allocation fails, and the pre-fix build tries the build with about 1.7 KiB
   free. Owner: `perf/mqtt-ha-memory`, which adds the 10240/4096 discovery gate
   and null-safe builders.

## Remaining gaps

- The simulator scheduler does not measure per-task stack high-water marks.
  The MQTT owner stack is guarded as a source budget, and the hardware
  high-water numbers remain the authority until the memory worktree adds stack
  accounting.
- The WebUI host double starts and stops the TLS server but does not perform a
  real handshake. Start failure and credential/NVS failure are covered; wire
  TLS parity remains a hardware and mbedTLS test.
- The companion transport is packet-addressable, so a stale GATT handle is not
  represented. Malformed and fragmented writes, reconnect, auth drop and
  oversized payloads are covered.
- The expanded allocation-failure injector at every production allocation site
  is owned by `sim-mem-crash`. This gate consumes the shared heap seams and
  adds randomized schedules without editing that model.

## Validation

```sh
python3 tools/check_fuzz.py --smoke --host-build host-build \
  --mqtt-binary sim/build-mqtt/furble-sim
python3 tools/check_fuzz.py --full --host-build host-build \
  --mqtt-binary sim/build-mqtt/furble-sim
python3 tools/pre_review.py --base origin/master
python3 tools/check_sim_scenarios.py
sh sim/scripts/check-doc-tokens.sh
sh sim/scripts/run-memory.sh
sh tools/run_sanitized_host_tests.sh --clean
```

The smoke and full gates must both pass before a hardware build. The current
expected output is a `FUZZ GATE FAIL` naming the two owners above.
