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
3 KiB plain worker, a 9,392-byte MQTT event queue in PSRAM, a 5,120-byte camera
snapshot on connect, one 2,160-byte LVGL draw buffer and a 484-byte crash
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

## Per-board boot heap

Each board profile with a release env boots on its own pool from
`sim/coverage/board_heap.json`, written by `tools/derive_sim_heap.py` from the board's `firmware.elf`, its
sdkconfig and the pinned ESP-IDF heap region tables. The figure is the
internal 8-bit heap at app start minus the boot allocations the source sizes,
each term cited in the JSON. It is an upper bound: the closed WiFi driver,
NimBLE host state, lwIP and the LVGL object tree are not attributable from
code, and on the StickS3 they account for about 141 KiB against the measured
console figure. Those `profile-*` budget floors equal the derived pair.
`headless-s3` derives from the `esp32-s3-headless` env. `stickc-plus2` has no
firmware env to derive from; it is listed in `UNDERIVED_PROFILES` in
`sim/scripts/run-memory-profile.py` and keeps the simulator default pool. Any
other profile without a derived pool fails the run.

Regenerate after a change that moves static RAM or a boot allocation:

```sh
for env in m5stick-c m5stick-c-plus m5stick-s3 m5stack-core m5stack-core2 waveshare-s3-eth esp32-s3-headless; do
  FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e "$env"
done
python3 tools/derive_sim_heap.py
python3 -m unittest tests.test_sim_profile_sdkconfig
```

Update the `profile-*` floors in `sim/coverage/memory_budget.json` to the new
pair; the unit test fails until they match. Restore any `sdkconfig.<env>` the
builds rewrote unless all six release files change together.

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
- `task_create_fail <name|all>`: the named task's allocation fails, which
  aborts the run with status 3 under the board's heap abort policy

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
