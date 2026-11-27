# Plan 370: simulator memory-management coverage audit

State: implemented in this PR (test/sim-memory-100). Hardware vibration, soak
and physical-device gates are unchanged by this plan; nothing here changes
firmware behaviour.

## Motivation

The simulator is the release gate for firmware that changes memory
(`docs/sim-coverage.md`). That claim only means something if a scenario can
reach the paths the device reaches when an allocation fails. The heap model
carries internal and PSRAM pools, seeds, watermarks and an abort policy, but
the audit below found four places where a firmware defect could pass every
existing scenario and unit test:

1. The exact-size failure probe was wired to `heap_caps_*` and to the tagged
   allocation helper, but not to the plain device `new`/`malloc` path, so no
   test could fail a specific `new`.
2. A block freed twice reached the host allocator. `glibc` then aborts with its
   own message and a signal, or silently frees a live host block when the
   address has been reused: neither is the SIM PANIC, panic record and status 3
   the device produces.
3. A ready-but-full PSRAM pool was unreachable: `psram_mode octal` always seeds
   the whole 8 MiB module, so "external RAM exhausted while internal RAM is
   healthy" had no scenario.
4. Out-of-memory during a camera connect and during an MQTT reconnect had no
   scenario or fixture. The boot-time MQTT owner failure was covered; the retry
   that re-creates the client was not.

Two further gaps were model-level rather than wiring: the n-th (ordered)
failure form had no test, and fragmentation was only exercised through a
governor unit test, never end to end.

## Inventory

Call sites are counted over `src/`, `lib/` and `include/` at the base commit
(`git grep -w`), comments excluded.

| Firmware API | Sites | Modelled at | Failure injection |
| :--- | ---: | :--- | :--- |
| `heap_caps_malloc` | 5 | `sim/shim/esp_heap_caps.h` to `sim/heap_model.cpp` | pool exhaustion, `alloc_fail`, `task_create_fail` |
| `heap_caps_aligned_alloc` | 2 | same entry point | same |
| `heap_caps_calloc` | 3 | same shim, zeroing and overflow guard | pool exhaustion, overflow, `alloc_fail` |
| `heap_caps_realloc` | 0 | same shim, copy path | exhaustion and `alloc_fail` in unit tests |
| `heap_caps_free` | 5 | same shim to the device registry | double free |
| `heap_caps_get_info` | 1 | `furble_sim_heap_info()` | unit test against the per-caps getters |
| `heap_caps_get_free_size` | 14 | `furble_sim_heap_free()` | every memory scenario |
| `heap_caps_get_largest_free_block` | 17 | `furble_sim_heap_largest()` | scenarios, fragmentation unit test |
| `heap_caps_get_minimum_free_size` | 0 | `furble_sim_heap_minimum()` | unit test only, no firmware caller |
| `esp_get_free_heap_size` | 2 | `sim/shim/esp_system.h` | none: see Known limits |
| `esp_get_minimum_free_heap_size` | 2 | same | same |
| `heap_caps_register_failed_alloc_callback` | 1 | `sim/heap_model.cpp`, `sim/CrashLogSim.cpp` | every abort fixture |
| `esp_heap_caps_init` | 0 | not shimmed: a call would fail to link | not applicable |
| `malloc`/`calloc`/`realloc`/`free` | 0 | not called by firmware; `LVGL` and the firmware reach the allocator through C++ `new` and the shim | not applicable |
| `new`/`new[]` | 2 nothrow, 25 `std::make_unique` | replaced `operator new` family in `sim/heap_count.cpp` | `alloc_fail` after this plan |
| PSRAM allocator | 2 explicit `MALLOC_CAP_SPIRAM` sites plus default routing | `heapSetPsram()`, `takeExternalLocked()` | `psram_mode`, `psram_free`, `psram_largest` after this plan |

`esp_heap_caps_init()` and a bare `malloc()` family are absent by
construction: the shim declares neither, so a firmware call would fail to link
rather than silently succeed on the host heap.

## Coverage before and after

`scene` is a certified scenario, `fixture` an exit-3 panic fixture run by
`sim/scripts/run-panic-fixtures.sh`, and `unit` a host test.

| Path | Before | After | Evidence |
| :--- | :--- | :--- | :--- |
| `heap_caps_malloc` failure aborts | fixture | fixture | `panic-webui-pre-gate-alloc`, `panic-icon-no-psram` |
| `heap_caps_aligned_alloc` failure aborts | unit | unit | `sim-heap-caps-shim` `calloc-*` cases share the entry point |
| `heap_caps_calloc` failure aborts | unit | unit | `calloc-exhausted`, `calloc-injected` |
| `heap_caps_calloc` overflow aborts | none | unit | `calloc-overflow` |
| `heap_caps_malloc_prefer` all-sets failure aborts | none | unit | `prefer-exhausted` |
| `heap_caps_realloc` on a failing size aborts, old block untouched | unit | unit | `realloc-exhausted` |
| `new`/`malloc` failure by pool exhaustion aborts | unit (implicit) | unit | `sim-heap-caps` default-routing cases |
| `new`/`malloc` failure by exact size and owner | none | unit + seed | `malloc-injected`, `alloc_fail` |
| n-th (ordered) allocation failure | boundary only | unit + fixture | `sim-heap-caps` ordered case, `panic-mqtt-reconnect-alloc` |
| PSRAM exhausted while internal RAM is healthy | unit (realloc only) | unit + fixture + seed | `sim-heap-caps` PSRAM case, `panic-psram-exhausted-task-alloc` |
| Fragmentation (`largest` far below `free`) | unit (governor only) | unit + scene | `sim-heap-caps` fragmentation case, `memory-fragmented-largest-only` |
| `heap_caps_get_info` fills a real structure | unit | unit | `sim-heap-caps-shim` info cases |
| Double free | none: host allocator abort | unit | `double-free` |
| Realloc on a failing size, block preserved | unit | unit | `realloc-exhausted` |
| Out of memory during a WebUI TLS handshake | boundary | boundary | `boundary-tls` `tls.alloc_failure failed=1` |
| Out of memory during an MQTT reconnect | none | fixture + seed | `panic-mqtt-reconnect-alloc` |
| Out of memory during a camera connect | none | fixture | `panic-camera-connect-alloc` |
| Abort contract: SIM PANIC, panic record, status 3 | 4 fixtures | 7 fixtures | `sh sim/scripts/run-panic-fixtures.sh` |
| `psram_task` on a board without PSRAM aborts | fixture | fixture | `panic-psram-task-no-psram` |

The four unchanged items above were verified rather than assumed: the panic
fixtures drive `heapAllocTagged()`/`heapTaskCreate()` against a seeded pool or a
capability the board cannot serve, and `heap_caps_register_failed_alloc_callback`
is what fills the record's `alloc_size`, `alloc_caps` and `alloc_task` fields
that the runner checks. `tls.alloc_failure` fails the second allocation of the
start sequence, so it is a real mid-sequence failure and not a start refusal.

## Changes

Model, all under `sim/`:

- `sim/heap_count.cpp`: the exact-size probe now runs on the plain device
  `new`/`malloc` path too, and only for an allocation that would otherwise be
  charged (device thread, outside a host scope). A bounded set of recently
  released addresses makes a second free of the same block abort like
  `multi_heap_free` instead of reaching the host allocator.
- `sim/heap_model.cpp`, `sim/heap_model.h`: `fail_rule_t` carries a `skip`, so
  `heapSetAllocFailureAfter()` can fail the n-th matching request;
  `heapNoteDoubleFree()` shares one `panicExit()` with the allocation abort.
- `sim/SimMQTTClient.cpp`: the client task reservation consults the same probe,
  which is the only way to fail a reconnect (see Known limits).
- `sim/driver.cpp`: three seeds, `alloc_fail <owner|any>:<bytes|any>[:<nth>]`,
  `psram_free` and `psram_largest`, each rejected at scenario load when
  malformed.

Tests and evidence:

- `tests/host/sim_heap_caps_test.cpp`: exact size, owner, counted and ordered
  injection; PSRAM exhaustion with a healthy internal pool; fragmentation
  through both the model and `heap_caps_get_info`.
- `tests/host/sim_heap_caps_shim_test.cpp`: `calloc-overflow`,
  `prefer-exhausted`, `malloc-injected` and `double-free` abort cases in the
  existing forked-child harness.
- `sim/scenarios/e2e/memory-fragmented-largest-only.txt`: 60,000 free with a
  3,000-byte largest block closes the WebUI supervisor gate and enters RED.
- `sim/fixtures/hardware/panic-camera-connect-alloc.txt`,
  `panic-mqtt-reconnect-alloc.txt`, `panic-psram-exhausted-task-alloc.txt`.
- `sim/coverage/crashes.json` gains the three fixture entries; the scenario
  manifest gains one entry.
- `sim/scripts/check-doc-tokens.sh` allow-lists `parseAllocFailSeed()` as a seed
  value validator, because `alloc_fail` is the first seed whose value is not a
  single number or literal. The self-test still rejects an unknown validator.

## Evidence

Focused host tests (`cmake --build /tmp/furble-mem-host --target sim_heap_caps_test
sim_heap_caps_shim_test sim_webui_memory_model_test memory_pressure_test`):

```text
=== sim_heap_caps_test ===
[E] heap: Mem alloc fail. size 0x2000 caps 0x00000404
[E] heap: Mem alloc fail. size 0x1000 caps 0x00000804
sim heap caps: PASS
=== sim_heap_caps_shim_test ===
[E] heap: Heap block freed twice
SIM PANIC: heap block freed twice in task heap-caps-test aborts the device
sim-heap-caps-shim: PASS
=== sim_webui_memory_model_test ===
sim webui memory model: PASS
=== memory_pressure_test ===
memory pressure checks passed
```

Abort fixtures, `sh sim/scripts/run-panic-fixtures.sh`:

```text
=== expect panic: sim/fixtures/hardware/panic-webui-pre-gate-alloc.txt ===
PASS: sim/fixtures/hardware/panic-webui-pre-gate-alloc.txt aborted and its record survived the reboot
=== expect panic: sim/fixtures/hardware/panic-icon-no-psram.txt ===
PASS: sim/fixtures/hardware/panic-icon-no-psram.txt aborted and its record survived the reboot
=== expect panic: sim/fixtures/hardware/panic-psram-task-no-psram.txt ===
PASS: sim/fixtures/hardware/panic-psram-task-no-psram.txt aborted and its record survived the reboot
=== expect panic: sim/fixtures/hardware/panic-camera-connect-alloc.txt ===
PASS: sim/fixtures/hardware/panic-camera-connect-alloc.txt aborted and its record survived the reboot
=== expect panic: sim/fixtures/hardware/panic-psram-exhausted-task-alloc.txt ===
PASS: sim/fixtures/hardware/panic-psram-exhausted-task-alloc.txt aborted and its record survived the reboot
=== expect panic: sim/fixtures/hardware/panic-mqtt-task-alloc-fail.txt ===
PASS: sim/fixtures/hardware/panic-mqtt-task-alloc-fail.txt aborted and its record survived the reboot
=== expect panic: sim/fixtures/hardware/panic-mqtt-reconnect-alloc.txt ===
PASS: sim/fixtures/hardware/panic-mqtt-reconnect-alloc.txt aborted and its record survived the reboot
Allocation abort gate passed.
```

The three new fixtures print exactly this, before the record check:

```text
# panic-camera-connect-alloc.txt, exit 3
abort() was called: Mem alloc fail. size 0x1000 caps 0x00000804
SIM PANIC: allocation failure in task FauxNY-40 aborts the device

# panic-psram-exhausted-task-alloc.txt, exit 3
abort() was called: Mem alloc fail. size 0x2000 caps 0x00000404
SIM PANIC: allocation failure in task control aborts the device

# panic-mqtt-reconnect-alloc.txt, exit 3
[W] mqtt: MQTT attempt 1 failed, retrying in 10000 ms.
abort() was called: Mem alloc fail. size 0x0c00 caps 0x00000804
SIM PANIC: allocation failure in task mqtt_client aborts the device
```

MQTT memory subset, `sh sim/scripts/run-memory.sh`:

```text
PASS memory-boot-combination
PASS memory-mqtt-low-heap-retry-limit
PASS memory-mqtt-unreachable
PASS memory-pressure-mqtt-shed
All 4 MQTT memory scenarios passed.
```

New fragmentation scenario, `sim/build/furble-sim --script
sim/scenarios/e2e/memory-fragmented-largest-only.txt`, exit 0:

```text
assert ok: heap.free = 60000
assert ok: heap.largest = 3000
assert ok: webui.supervisor = blocked
assert ok: webui.server = off
assert ok: webui.gate = supervisor_memory
assert ok: webui.fail_closed = yes
state webui.gate_message = WebUI supervisor not created: internal free 60000, largest 3000 (need 5120/4608)
assert ok: heap.pressure = red
assert ok: heap.alloc_failures = 0
assert ok: ui.page = main
assert ok: platform.watchdog = armed
```

Regression sample on the same binary, no assertion failure and no panic in any
of them: `memory-counted-camera-connect`, `memory-webui-fail-closed`,
`memory-pressure-webui-shed`, `main-menu-gap-pages`, `display-settings-pages`,
`about-scroll-reachability` and `connected-gap-pages`. The double-free detector
did not fire in any legitimate run.

Gates:

```text
$ python3 tools/check_sim_scenarios.py
sim scenario manifest is complete
$ sh sim/scripts/check-doc-tokens.sh
sim documentation tokens are present
$ python3 tools/check_sim_coverage.py
hardware: {'total': 206, 'modeled': 206, 'approximate': 0, 'not-modeled': 0}
crashes:  {'total': 23, 'modeled': 23, 'approximate': 0, 'not-modeled': 0}
fuzz:     {'total': 11, 'modeled': 11, 'external-pending': 0}
perms:    {'declared': 6912, 'expected': 6912, 'results': 6912}
simulator coverage manifest is complete
$ python3 tools/check_doc_citations.py --root .
doc citations in the tree: 2820 line-checked (100 symbol-checked), 125 external, 207 unresolved basename, 0 errors of 3152 citations in 179 files
```

## Known limits

- `esp_get_free_heap_size()` and `esp_get_minimum_free_heap_size()` answer
  `MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT` in `sim/shim/esp_system.h`, while the
  pinned IDF answers `MALLOC_CAP_DEFAULT` (`esp_system_chip.c:65-78`), which on
  a PSRAM board includes the external pool. Closing it changes what the About
  and Device info rows render on the 135x240 panel, so it needs that page
  re-measured; plans/314-memory-resilience.md records the same deferral with
  the scenario that would move. Firmware behaviour is unchanged either way, so
  this plan documents it rather than weakening the assertion that would catch
  it.
- `heap_caps_get_info()` resolves one pool: a `MALLOC_CAP_DEFAULT` read reports
  internal RAM alone while the per-caps getters sum both, which plan 337
  records as a known divergence of the struct against the getters.
- `MALLOC_CAP_EXEC`, `MALLOC_CAP_IRAM_8BIT`, `MALLOC_CAP_RETENTION` and
  `MALLOC_CAP_RTCRAM` are answered from the merged internal pool. No firmware
  caller uses them (plans/314 records the over-report).
- The model carries one internal pool and one external pool with a single
  `largest` figure each: it does not model per-region fragmentation, and
  `free_blocks` is one for a non-empty pool.
- Host objects are 64-bit, so a charged `sizeof` is an upper bound on the
  32-bit device figure.
- The ThreadSanitizer build links the sanitizer's own `operator new`/`delete`,
  so C++ allocations are not charged there. Only the GPS concurrency scenarios
  run on that build and none asserts a heap figure.
- The double-free detector keeps the last 4,096 released addresses. A second
  free separated by more than that many allocations, with the address reused in
  between, is the same false negative the host allocator has. A double free is
  not a failed allocation, so the panic record's `alloc_size` fields stay zero
  and the record carries the modelled line instead.
- The MQTT client start gate floors (8,192 free, 4,096 largest) exceed the
  3,088-byte plain client block, so pool exhaustion can never reach that
  allocation. `alloc_fail` is what makes it reachable, and the fixture asserts
  the abort the device would take rather than the `ESP_ERR_NO_MEM` fallback the
  production code still carries for a host that cannot abort.
