# Simulator memory profiler

The simulator carries a per-board static memory budget and a dynamic heap model
so a scenario can assert memory behavior instead of only pixels.

## Static map

`sim/memory_profile.cpp` holds the per-profile budget table. It covers internal
RAM, PSRAM, flash, main stack, companion stack, MQTT owner and worker stacks,
IR stack, LVGL draw buffers, mbedTLS, the PSRAM internal reserve and the crash
record. Values come from `sdkconfig.*`, `platformio.ini`, the task creation
sites and `include/FurbleUIMemory.h`.

The table is a release target, not a hardware measurement. The S3 row encodes
the sim-first target: 12 KiB main stack, 3 KiB companion, 10 KiB MQTT owner,
3 KiB plain worker, a 9,216-byte MQTT event queue in PSRAM, a 5,120-byte camera
snapshot on connect, one 2,160-byte LVGL draw buffer and a 436-byte crash
record (the `sizeof(record_t)` from `include/FurbleCrashRecord.h`).

## Dynamic heap model

`sim/heap_model.cpp` models internal and PSRAM pools with:

- free, largest, minimum free and minimum largest;
- allocation count, failure count and last failed owner;
- owner-tagged allocations and scenario-end leak detection;
- exact-size and owner-specific failure injection;
- PSRAM mode, init failure and task stack placement;
- a flash-cache safety check that fails closed when a PSRAM task stack is live
  during a flash write;
- a sampled timeline in virtual milliseconds.

`heap.*` queries expose the pool. `mem.*` queries expose the static map, owner
totals, leaks, timeline count and watermarks.

## Dumps and budget

`action memory-dump PATH` writes one JSON document:

```text
static budget -> components and sources
heap          -> free, largest, minimum, fragmentation, failures, leaks
owners        -> owner:bytes:region:lifetime
tasks         -> name:requested[:psram]
timeline      -> sampled virtual time and pools
```

Compare dumps against `sim/coverage/memory_budget.json`:

```sh
# build first
FURBLE_DEP_ROOT=... FURBLE_LVGL_DIR=... sh sim/build.sh
python3 sim/scripts/run-memory-profile.py
```

The runner profiles all supported board identities, then the WebUI and MQTT
budget scenarios, and fails when free, largest, fragmentation, leaks or a
component size misses the golden budget.

## Failure injection

Seeds:

- `heap_internal_free`, `heap_internal_largest`
- `psram_mode absent|quad|octal|init_fail`
- `task_stack_region internal|psram`
- `mqtt_owner_stack`, `mqtt_worker_stack`, `mqtt_buffer`
- `mqtt_tls`, `mqtt_ha`, `mqtt_expect_overflow`
- `mqtt_discovery_payload`
- `boot_defaults_marker`, `boot_profile`, `psram_memtest`
- `wifi_fast_connect`, `wifi_auth`
- `task_create_fail <name|all>`

Actions:

- `heap-leak-probe`, `heap-leak-free`
- `alloc-icon`
- `psram-task-start`
- `flash-write`

The MQTT owner stack gate fails closed below 10,240 bytes unless a scenario
explicitly expects the historical overflow. MQTT buffers above 2,048 bytes fail
closed. A PSRAM task stack during a flash write fails closed.

## Known limits

The SDL build does not link the production mbedTLS/socket stack, the IR RMT
driver, the SD writer, the speaker path, the W5500 Ethernet driver or
`CompanionGatt`. Their allocation sizes and state transitions are modeled in the
SDL scenarios. The linked host-boundary suite compiles those production paths
unchanged against peripheral doubles, so the manifest marks those entries
modeled. Physical wiring, analog audio, the SD controller and a real mbedTLS
handshake remain hardware validation items.
