# 314 - Memory resilience

Status: in progress. PR 1 of 14 (sim config parity and abort semantics) is
implemented on `feat/314-sim-config-parity`. Sim-only: no hardware was flashed
or measured. Every number comes from datasheets, the pinned ESP-IDF source,
the sdkconfig files and the firmware's own build output.

## Problem

The device crashed on hardware from memory exhaustion (MQTT owner stack, Home
Assistant discovery OOM, the #407 event queue) while the simulator gate passed.
The simulator did not model memory the way the hardware does:

- `CONFIG_HEAP_ABORT_WHEN_ALLOCATION_FAILS=y` is set on every board. A failed
  `malloc`, `heap_caps_malloc`, `pvPortMalloc` or `xTaskCreate` calls the
  registered failed-alloc hook and then `esp_system_abort()`
  (`components/heap/heap_caps.c`). C++ exceptions are off, so failed
  `std::string`, `std::vector` and `new` growth aborts too. Every NULL and
  `!= pdPASS` fallback in the firmware is unreachable on hardware.
- The simulator returned NULL and continued, so four certified scenarios
  asserted that the UI survived an allocation failure. Those are exactly the
  shapes that boot-looped the device.
- No `CONFIG_*` symbol reached the simulator, so `#if defined(CONFIG_SPIRAM)`
  branches such as the MQTT PSRAM queue compiled out.
- The model charges only scripted services and four named task stacks.

## Plan

Phase 1, simulator fidelity: PR 1 config parity and abort semantics, PR 2
count every allocation, PR 3 per-board baselines from datasheets and code, PR 4
stacks and gate repair. Phase 2, fixes validated in the simulator: PR 5
failed-alloc diagnostics, PR 6 the memory pressure governor that sheds optional
services in tier order, PR 7 missing gates, PR 8 per-board config tuning, PR 9
remaining audit items. Phase 3, analysis gates: PR 10 firmware clang-tidy,
PR 11 size gate, PR 12 dynamic analysis, PR 13 debug hardening, PR 14 hooks.

Abort policy: keep the abort on. Turning it off gains nothing, because the
`std::` paths abort under `-fno-exceptions` and 213 unchecked `lv_*_create`
calls would become NULL dereferences with worse diagnostics. Survival has to
come from never reaching exhaustion.

## PR 1: config parity and abort semantics

Implemented:

- `tools/gen_sim_sdkconfig.py` writes the memory-relevant `CONFIG_*` subset of
  one board's sdkconfig into `sdkconfig_sim.h`. `sim/build.sh` and
  `sim/CMakeLists.txt` force-include it into firmware and simulator sources
  only. M5GFX and M5Unified key on SPIRAM symbols for on-device board
  detection and keep their host paths. `CONFIG_IDF_TARGET_*` stays out for the
  same reason. The panel class picks the board; `FURBLE_SIM_SDKCONFIG`
  overrides it.
- A modelled allocation failure now calls the hook registered through
  `heap_caps_register_failed_alloc_callback`, prints
  `abort() was called: Mem alloc fail. size 0x.. caps 0x..` and `SIM PANIC`,
  writes a panic crash record and exits with status 3. The abort runs after
  the heap mutex is released, because the record queries the heap. The board
  config decides: host unit targets linked without the generated header keep
  the returning behaviour.
- The failure line carries the real caps: 0x804 internal, 0x404 PSRAM, and the
  0x1800 plan 31 recorded for the icon decode.
- `xQueueCreateWithCaps` and `vQueueDeleteWithCaps` are shimmed, so the
  production MQTT PSRAM queue branch now compiles on the StickS3 model and
  charges the PSRAM pool.
- `memory-webui-pre-gate-alloc`, `memory-mqtt-task-alloc-fail` and
  `hw-memory-psram-icon-absent` moved to `sim/fixtures/hardware/panic-*.txt`.
  `sim/scripts/run-panic-fixtures.sh` requires status 3, the hardware line and
  a panic record that the next boot reports. CI runs it in `sim-e2e`.
- `hw-memory-webui-tg1wdt` no longer claims an allocation failure before the
  recorded TG1WDT; the production gate refuses the supervisor first.
- `tests/fuzz/memory_fuzz.py` no longer forces task failures, which always
  abort. A passing random pool now proves the admission gates refused every
  service before an allocation could fail. It treats `SIM PANIC` as a crash.

Deviations from the plan:

- `xTaskCreateWithCaps` and `uxTaskGetStackHighWaterMark` are not shimmed in
  PR 1. No simulator-compiled source calls them; PR 4 adds the stack work.
- `BtDebugJournal` keys its PSRAM path on `CONFIG_IDF_TARGET_ESP32S3`, which
  stays out of the header, and on `ESP_PLATFORM`. Its simulator path is
  unchanged.
- The scripted MQTT model in `sim/hardware_model.cpp` remains until PR 2 counts
  the production path. Its allocation failures now abort like every other.
- The panic record is written by a recorder that `sim/CrashLogSim.cpp`
  registers at static initialization, so the heap model has no link
  dependency on the crash log.

Validation (local, macOS):

- `sh sim/scripts/run-e2e.sh`: 205 of 205.
- `sh sim/scripts/run-memory.sh` (MQTT build): 3 of 3.
- `sh sim/scripts/check-hardware-quirks.sh`: pass.
- `sh sim/scripts/run-panic-fixtures.sh`: 3 of 3 abort with records.
- `tests/fuzz/memory_fuzz.py` seeds 1-40, default and MQTT builds: pass.
- Host suite: 183 of 183. CMake simulator build: pass.
- Fuzz corpus after PR 1: 10 targets, 135 seed runs, 89,145 iterations, 0 failures. The psram target lost the moved icon scenario.
