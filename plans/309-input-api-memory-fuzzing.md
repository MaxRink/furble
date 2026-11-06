# 309 - Input, API and memory fuzzing gate

Status: implemented on `fuzz/input-api-memory`, rebased on #399 and merged with
the #397 and #398 production branches for the combined-tree run. The full fuzz
corpus passes: 10 targets, 136 seed runs, 89,146 iterations, zero failures across all three panel binaries and both layouts. The
#393/#398 MQTT owner stack and HA discovery OOM are now fixed and carry
regressions. The flash-readiness gate remains blocked only on the full
permutation run and the memory profile dumps owned by #399; the full
permutation artifact is a separate evidence PR.

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

Both findings were recorded in `tests/fuzz/seeds.json` with their parallel
owner. They stayed in the gate until those PRs landed, were never marked
expected-pass, and now carry fixed regressions in
`sim/coverage/fuzz_results.json`.

## #397 and #398 cover targets

The combined-tree run exercises the production paths, not just constants:

- `fuzz.nvs` runs the real settings migration fuzzer, the NVS round-trip test,
  `hw-boot-settings-migration`, WiFi fast-connect fallback, the power-button
  state machine and the USB ROM/download recovery.
- `fuzz.psram` runs both PSRAM journal targets and the debug/release memtest
  scenarios plus PSRAM absent/failure and icon allocation scenarios.
- `fuzz.mqtt` runs the 10 KiB owner, 3 KiB plain worker and 6 KiB TLS worker,
  with separate plain and TLS start floors, start and
  discovery gates below/at/above their floors, the cached HA setting and the
  retained discovery records.
- `fuzz.memory.fragmentation` runs 1,536-byte discovery fragmentation,
  repeated HA online replay and the cJSON allocation-failure schedule.
- `fuzz.webui.http` runs the 12 KiB main stack with the separate 12 KiB WebUI
  identity task, the WebUI gate and the console workflow.
- `fuzz.ui.touch` and `fuzz.ui.physical-buttons` run the single-buffer display
  scenario and the event-count scenario on both layouts.

## Gate

`sim/scripts/run-fuzz-target.py --all` is the release-corpus entry point. It
runs the targets named by `sim/coverage/fuzz_targets.json`, asserts every
command and scenario, and writes `sim/coverage/fuzz_results.json` with seed
counts, iterations, failures and fixed regressions. `tools/check_fuzz.py
--smoke` / `--full` remains the local budget; `sim-e2e.yml` builds the host
targets and runs the smoke gate after the dedicated UI fuzz steps.

## Current findings

No fuzz seed is open. The fixed regressions are recorded in the result file:

1. `sim-preferences-deferred-commit`: #397 added
   `Preferences::setDeferredCommit()`/`commit()` to the shared settings seeder;
   the simulator adapter did not implement them, so the simulator link failed.
   Fixed in `sim/PreferencesSim.cpp` and guarded by the simulator boot
   migration scenario.
2. `mqtt-owner-stack`: the owner was 6144 bytes and hardware overflowed at
   4096 during start and 6144 on the first connected event. #398/#393 keep it
   at 10240 and the plain worker at 3072. Guarded by
   `hw-memory-mqtt-10k-success`.
3. `ha-discovery-oom`: discovery leaked cJSON nodes under allocation failure
   and tried the build with about 1.7 KiB free. #398 adds the 10240/4096 gate
   and null-safe builders. Guarded by
   `mqtt_fuzz --seed 1 --iterations 1 --discovery-oom`.

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
python3 sim/scripts/run-fuzz-target.py --all
python3 tools/check_sim_coverage.py
python3 tools/check_flash_readiness.py --ready
python3 tools/pre_review.py --base origin/master
python3 tools/check_sim_scenarios.py
sh sim/scripts/check-doc-tokens.sh
sh sim/scripts/run-memory.sh
sh tools/run_sanitized_host_tests.sh --clean
```

`--all` must pass before a hardware build. The readiness gate is still blocked
by the full 6,912 permutation run and the memory profile dumps. Those are #399
evidence gaps, not open fuzz findings; the full permutation artifact is a
separate evidence PR.
