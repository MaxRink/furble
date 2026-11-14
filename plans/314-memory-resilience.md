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

## PR 2: count every device allocation

Implemented on `feat/314-sim-count-allocations`, stacked on PR 1:

- `sim/heap_count.cpp` replaces the global `operator new`/`delete` family and
  provides `furble_sim_fw_malloc`, `realloc` and `free`. LVGL's
  `lv_mem_core_clib.c`, the device allocator path for `LV_STDLIB_CLIB`, is
  compiled against them in both simulator builds.
- Counting starts one UI timer period (1 s of virtual time) after the
  production `BootTimeline::UI_FIRST_FRAME` mark, at a driver tick with no
  running LVGL animation. The seeded pairs were read from the console of an
  idle device, after the boot focus transitions released their style copies
  and after the one-second UI timers had sized their labels and layouts.
  Starting at the first frame instead charged those 2,016 settle bytes on the
  StickS3 and pushed the post-#407 budget to a false 2,391 / 1,044.
- From UI construction until counting starts, device allocations are recorded
  as baseline blocks without a charge. Freeing one after counting starts
  returns its block cost to the pool, as the device would.
- Each charge is the ESP-IDF 5.5.3 block cost: payload plus the 12-byte
  comprehensive-poisoning head and tail, rounded to 4 bytes, minimum 12, plus
  the 4-byte TLSF header.
- Default `malloc` follows `heap_caps_malloc_default`: up to
  `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL` internal first, above it PSRAM first,
  then either pool. Failure caps are 0x1000 with PSRAM malloc and 0x1800
  without, matching the plan 31 record.
- Every device task stack and TCB is charged once counting runs, not only the
  four optional services. Host tasks (`ble-radio`, `psram_probe`) are not.
- Every queue is charged at `StaticQueue_t` (84 bytes on the 32-bit port with
  queue sets) plus storage, internal through `pvPortMalloc`, or the named
  pool through `xQueueCreateWithCaps`.
- An allocation counts only on a device task thread and outside a
  `HostAllocScope`. The whole scenario driver tick is a host scope; only the
  steps that call straight into a firmware handler (actions, button presses,
  home, back and fuzz events) re-enter device counting through
  `DeviceAllocScope`. The heap model and power profiler locks, queries,
  screenshot capture, memory dumps, crash-log sidecar writes, fuzz
  bookkeeping, the WebUI model tick, the rig, scheduler bookkeeping and every
  MockNimBLE call into a virtual peer are host scopes too. Idle heap is flat
  for 70 s after the first frame.
- The StickC and Core simulator runs found the gap that made the driver a
  host scope: a `capture` step allocated a full RGB frame (230,400 bytes on the
  320x240 panel) on the UI thread and aborted against the 200 KiB pool.
- `crash-record-survives-restart` now bounds the recorded heap to 1 KiB below
  the default pool instead of asserting the untouched pool exactly, because
  each panel builds a different first page before the panic.
- `heap.counting`, `heap.counted_internal` and `heap.counted_external` are new
  observables; they include counted task stacks, and `heap.counted_tasks`
  isolates the live task charges. On a clean build a camera connect costs
  5,220 bytes internal (the 4,464-byte target task plus the connected page),
  and disconnect returns the task. Four connect cycles leave about 230 bytes per cycle in
  LVGL image cache bookkeeping, and the decompressed icons (about 4.9 KiB each)
  land in PSRAM as `heap_caps_malloc_default` routes them. That is bounded
  cache growth, not a leak. Decoded icons of 4 KiB or less stay internal on
  the S3, as on the device: evidence for the LVGL internal-working-set finding
  (audit M8).
- `memory.device_allocation_counting` joins the hardware coverage manifest
  (189 entries).

Deviations:

- The first-fit fragmentation model moves to its own PR (2b) so counting can
  be reviewed alone. `heap.largest` still uses the one-pool approximation.
- The scripted MQTT model in `sim/hardware_model.cpp` stays. The default
  build has no production MQTT, so its seeded budget scenarios still need it;
  the `FURBLE_SIM_MQTT=1` build now counts the production MQTT path as well.
- `SPIRAM_MALLOC_RESERVE_INTERNAL` is not yet modelled; PR 3 accounts for it
  in the derived baseline.
- Host objects are 64-bit. Pointer-heavy structures such as LVGL objects are
  counted at up to twice their 32-bit size, so the count is an upper bound and
  the simulator fails before the device would.
- Calls from a virtual peer back into firmware callbacks run inside the peer
  host scope and are not counted.
- The first local figures for this PR came from a build directory whose LVGL
  allocator object predated the counter defines; the cache stamp did not
  cover per-file flags. The stamp now carries the `sim/build.sh` checksum and
  every figure above is from clean builds.
- `sim/lv_conf.h` is generated from the StickS3 sdkconfig and shared by
  every panel build. Under counting, UI fuzz seed 3 aborted the Core sim on an
  8,963-byte icon decode: the shared 256 KiB image cache kept decoded icons in
  the Core's internal pool, while the Core's sdkconfig sets no cache. Each
  build now writes its own `lv_conf.h` with the board's `LV_CACHE_DEF_SIZE`;
  seed 3 then runs clean. Font and DPI parity is still open (PR 2c): StickC
  and Core sims render with 130 DPI and the 16 px default font, while their
  sdkconfigs set 64 DPI and 12 px. Changing that moves their layouts and needs
  scenario re-certification.

## PR 3: per-board boot heap from datasheets and code

Implemented on `feat/314-sim-board-baselines`, stacked on PR 2. No hardware.

- `tools/derive_sim_heap.py` reads each release env's `firmware.elf` and
  `sdkconfig.<env>`. It takes the internal 8-bit regions the pinned ESP-IDF
  registers (`components/heap/port/<chip>/memory_layout.c`) and subtracts the
  ranges it reserves: `.data`/`.bss` (`_data_start`..`_heap_start`), S3 IRAM
  code on shared DIRAM, the ESP32 ROM data, and the ESP32 BT controller
  ranges that stay reserved after NimBLE releases the classic-BT window
  (`bt.c`, `esp_bt.h`). It then subtracts the boot allocations the source
  sizes: kernel and IDF task stacks with a TCB read from the ELF's DWARF, the
  BT controller task, NimBLE pools, static WiFi RX data and management
  buffers, the GPS UART driver's allocations, the furble boot tasks (with the
  board-conditional IR and SD tasks, queues and semaphore, and the headless
  request queue) and LVGL draw buffers, each with its citation. Kernel and
  driver struct sizes come from each ELF's DWARF. Output is
  `sim/coverage/board_heap.json`.
- Each ELF's static DRAM matches PlatformIO's "RAM used" figure to the byte.
- `CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL` is not subtracted: `esp_psram.c`
  re-registers that pool with INTERNAL | DMA | 8BIT caps, so it stays in
  `heap_caps_get_free_size(INTERNAL | 8BIT)`. Plain `malloc` cannot use it.
- WiFi starts only with the WiFi setting on and an SSID stored
  (`WiFi::init`). The WiFi buffers and furble's WiFi task carry that
  condition, and the companion task and timers carry the COMPANION setting.
  The JSON reports the ceiling with each optional service off. The profile
  seeds use the all-on figure, the state of the measured seeds (WiFi, BLE,
  companion and display active) and of every MQTT and WebUI path.
- The profile runner fails a board that has neither a derived pool nor an
  entry in its explicit underived list (`stickc-plus2`, `headless-s3`).
  `esp_netif_init()` and the default event loop run on every boot, so the
  tcpip and event tasks, the loop object, its queue and its mutex are always
  charged.
- `sim/scripts/run-memory-profile.py` seeds each board profile with its
  derived free and largest upper bounds instead of the shared 200 KiB / 160
  KiB default, which an ESP32 Core can never have. The `profile-*` budget
  floors equal the derived pair, and `tests/test_sim_profile_sdkconfig.py`
  keeps them equal.
- The profile table in `sim/memory_profile.cpp` listed an 8 KiB main stack
  for `headless-s3` and `waveshare-s3-eth`; both sdkconfigs set 16 KiB. Fixed,
  and the same test now checks every row's main stack and PSRAM presence.

| Env | Static DRAM | Heap at app start | Largest span | Attributable boot | Derived free, all on | WiFi off | WiFi and companion off |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| m5stick-c | 54,928 | 255,132 | 129,984 | 120,388 | 134,744 | 157,948 | 161,484 |
| m5stick-c-plus | 54,904 | 255,156 | 129,984 | 121,748 | 133,408 | 156,612 | 160,148 |
| m5stick-s3 | 47,704 | 261,800 | 261,800 | 87,160 | 174,640 | 191,380 | 194,916 |
| m5stack-core | 54,952 | 255,108 | 129,984 | 142,612 | 112,496 | 135,700 | 139,236 |
| m5stack-core2 | 56,384 | 253,676 | 129,984 | 142,612 | 111,064 | 134,268 | 137,804 |
| waveshare-s3-eth | 52,136 | 258,136 | 258,136 | 115,228 | 142,908 | 166,124 | 169,672 |

Limit of a code-only derivation: the StickS3 console measurement already in
the repo (30,007 free, MQTT off, debug image with the 8 KiB main stack) sits
144,633 bytes below the derived bound. That gap is boot cost the source does
not size: the closed WiFi driver's task and allocations, NimBLE host state,
lwIP control blocks and the LVGL object tree, which stays internal because
its objects are below the 4 KiB PSRAM threshold. The derived figures are
therefore proven ceilings, not realistic post-boot seeds. A realistic seed per
board needs one console `heap` measurement per board, which belongs to the
deferred hardware pass. The measured StickS3 seeds remain the inputs of the
pressure scenarios.

Deviations:

- Lifecycle charges for WiFi up, BLE connect and the TLS handshake are not in
  this PR. The closed WiFi driver and mbedTLS session sizes cannot be derived
  from code alone; PR 2 already charges the open parts (task stacks, NimBLE
  peer state through the counter, LVGL).
- Findings for PR 8 from this work: the Core2 build reports 4.5 MB of RAM
  because the board has 8 MB PSRAM, yet its sdkconfig leaves `CONFIG_SPIRAM`
  off (audit H1). Regenerating the StickS3 sdkconfig drops a stale
  `CONFIG_NIMBLE_MEM_ALLOC_MODE_INTERNAL=y` alias that contradicts the file's
  `EXTERNAL` choice. Both need all six release sdkconfigs regenerated
  together.

## PR 5: failed-allocation diagnostics

State: implemented. The `esp_timer_create` return checks in
`src/FurbleMQTT.cpp` landed separately after the MQTT stack fix. The audit
found the ignored results already safe: a failed create leaves the handle
null, the hold and interval paths refuse a null timer, and the owner loop
waits for the hold and interval deadlines itself, so a failed
`esp_timer_start_once` loses only a wake. The failures are now logged and the
start result is discarded explicitly.

- The crash record is version 2 (484 bytes, was 436) and carries the failed
  allocation: size, capabilities, task and allocating function.
- `CrashLog::init()` registers `heap_caps_register_failed_alloc_callback`
  after it has consumed the previous record and written this boot's snapshot.
  ESP-IDF calls the callback on the failing task just before the abort. It
  edits the RTC record in place under the record lock: no logging, no
  allocation, and no second record on what may be a 3 KiB stack.
- The panic handler prints "Mem alloc fail" directly, never through the log
  hook, so before this change a record named the last logging task and nothing
  about the allocation.
- The console `crash` output, the summary line (`alloc_fail=... caps=... in=...
  fn=...`, now sized by `SUMMARY_BYTES`), and the simulator `crash.alloc_*`
  queries expose it. The simulator twin registers the same callback and the
  panic fixtures assert that the recorded size, caps and task match the abort
  line.
- A version 1 record from before the update fails validation once and is not
  reported. That is the documented behavior for a record format change.

## PR 6: memory pressure governor

State: implemented. `include/FurbleMemoryPressure.h` holds the policy and
`src/FurbleMemoryPressure.cpp` the one-second tick beside `CrashLog::update()`
in both UI loops. Three low readings enter a level; five clear readings leave
it, one level at a time:

| Level | Enter | Pauses | Leave |
| :--- | :--- | :--- | :--- |
| AMBER | free < 12,288 or largest < 8,192 | WebUI server | free >= 20,480, largest >= 12,288 |
| RED | free < 8,192 or largest < 6,144 | WebUI server, MQTT client | free >= 19,456 plus any internal MQTT queue storage still missing (11,996), largest >= 10,240 |

The WebUI supervisor and the MQTT owner add the level to their existing run
decision, so a pause stops the server or client exactly like a disabled
setting, without an NVS write, and the service returns through its own start
gate. The simulator's WebUI model mirrors the supervisor decision.
`action heap-squeeze` and `heap-release` move the modelled heap at runtime and
`heap.pressure` reports the level. The console `status` prints
`memory_pressure`.

Evidence: `tests/host/memory_pressure_test.cpp` (classification, debounce,
one-level recovery, the post-#394 StickS3 WebUI pair staying normal),
`memory-pressure-webui-shed.txt` (pause, no thrash while the restored server
would land below AMBER, return after release) and
`memory-pressure-mqtt-shed.txt` (pause frees the 3 KiB worker, no retries
while paused, return at AMBER).

Deviations:

- The plan's seven tiers become two. HA discovery is already gated per
  publish by `MQTTMemory::discoveryGate`. GPX logging frees no heap because
  the SD writer task and its queue stay. The BT debug journal is console-only
  and off by default. The companion keeps its task and GATT server across a
  disable (re-creating them aborted on the StickS3), and WiFi's driver pools do
  not grow at runtime, so neither would free RAM worth the outage. WiFi is
  also not production code in the simulator.
- The plan's AMBER 16 KiB threshold would pause a healthy post-#394 StickS3
  WebUI session (16,087 free), and a flat 4 KiB hysteresis would thrash. The
  thresholds keep room for a camera connect instead, and each exit level
  includes the cost of the service it re-admits.
- `ensure(bytes)` before `Control::addTarget` is not added. The RED floor
  keeps the target task's block free whenever an optional service is running;
  a demand path that sheds synchronously from the UI task can follow if a
  measured session shows a connect still failing.
- No UI status row: that is a UI change with a screenshot regeneration, and
  the console line plus the log cover diagnosis. It can follow with PR 7.
- The thresholds are derived. A hardware reading of a busy WebUI and MQTT
  session is pending with the rest of hardware validation.

## PR 7: missing gates

State: implemented. Every board aborts on a failed allocation, so each
runtime path that creates a task or a retained structure needs an admission
gate that measures internal free and largest bytes first and refuses with a
log. The audit covered five sites.

1. Runtime WebUI TLS identity task. Gated.
   `WebUIMemory::identityGate()` admits the 12 KiB `webui_tls` task only at
   20,864 free / 12,672 largest. Both runtime sites, the enable path in
   `reloadSetting()` and the replacement after a non-memory
   `httpd_ssl_start` failure, now go through `WebUI::startIdentityTask()`.
   A refusal does not create the task, clears `m_IdentityTaskActive`, writes no
   NVS and logs `TLS identity not generated: internal free N, largest N (need
   20864/12672). HTTPS stays closed; reboot or change a setting once more
   internal RAM is free to retry.` The next
   `reloadSetting()` with the WebUI on retries, as the old "reboot to retry"
   path did. The boot path in `initEarly()` stays ungated: it runs before
   WiFi, the display and the companion take their share.
2. Companion first enable. Gated. `CompanionMemory::startGate()` in the new
   `include/FurbleCompanionMemory.h` runs in `CompanionGatt::enable()` when
   `m_Task` is null, before the identity, GATT, server start, timers and task
   allocations. The floor is 7,552 free / 3,456 largest. A refusal leaves
   `m_Enabled` false, keeps the saved setting and logs `Companion not
   started: internal free N, largest N (need 7552/3456). It stays off; enable
   it again or reboot to retry.` Later enables reuse the retained task and
   services and are not gated. The task stack constant moved into the header,
   and `tools/stack_budget.json` reads it there.
3. OTA start. No gate, no code. No production path starts an OTA update.
   `OTA::PartitionSink`, `EspIdfPartitionTarget` and the MQTT OTA engine are
   compiled (`src/CMakeLists.txt`) but nothing in `src/`, `lib/` or
   `include/` outside the OTA files constructs them: no console command,
   companion characteristic (`createGatt()` says "OTA is intentionally
   reserved only"), WebUI route or MQTT handler. None of the seven release
   images links `esp_ota_begin` or a `PartitionSink` symbol (`nm
   firmware.elf`; `esp_ota_get_running_partition` is present as a
   control). When a
   transport is wired, its start allocates `esp_ota_begin`'s 52-byte
   `ota_ops_entry_t` (m5stack-core2 DWARF) plus the transport's receive
   buffer and task; that PR must add the gate with the transport.
4. `Control::Target` queue and per-camera task. No change. The
   `xQueueCreate` result in `Target::Target()` and the `xTaskCreate` `!=
   pdPASS` branch in `Control::addTarget()` are dead under abort semantics,
   and a camera connect is the core path, never refused. PR 6 did not add
   `ensure()`; instead the governor's RED floor keeps the 4 KiB target task's
   block free while any optional service runs. The NULL checks stay as
   harmless host-build fallbacks.
5. Scan start probe thread. No change. `Scan::runStartProbe()` and its
   `std::thread` are inside `#if defined(FURBLE_SIM)` and run only when a
   scenario installs a probe with `seed scan_start_probe true`. The firmware
   does not compile them.

Derived floors:

| Gate | Free floor | Largest floor | Derivation |
| :--- | ---: | ---: | :--- |
| WebUI identity | 20,864 | 12,672 | 12,288 stack + 384 task overhead + 8,192 mbedTLS heap; largest is stack + overhead |
| Companion start | 7,552 | 3,456 | 3,072 stack + 384 task overhead + 4,096 GATT database; largest is stack + overhead |

- Task overhead, 384: the largest `StaticTask_t` in
  `sim/coverage/board_heap.json` is 348 bytes (Waveshare; 336 elsewhere), and
  the stack and TCB are two blocks with a 16-byte head each (4-byte TLSF
  header, 12 bytes of comprehensive poisoning). 380, rounded up.
- mbedTLS heap, 8,192: the peak of `createIdentity()` on the pinned mbedTLS
  3.6.5 is `mbedtls_pk_write_key_pem()`'s DER buffer. `esp_config.h` always
  defines `MBEDTLS_RSA_C`, so `PRV_DER_MAX_BYTES` is the RSA bound, 47 +
  3 x 1,024 + 5 x 512 = 5,679 bytes. The key pair (128-byte
  `mbedtls_ecp_keypair` plus limbs), the subject and issuer name nodes and the
  subject string are live with it, about 6.6 KiB. Key generation and ECDSA
  signing peak near 4.6 KiB: an 8-point comb table (window 4, fixed-point
  optimization off) and the HMAC-DRBG and signing temporaries. Rounded up to
  8 KiB. The S3 boards set `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC`, so there the
  floor is conservative; the Core2 is internal.
- GATT database, 4,096: from the m5stack-core2 DWARF, 2 `NimBLEService` (68
  bytes) and 9 `NimBLECharacteristic` (92 bytes) objects, 9 initial values of
  21 bytes, the `ble_gatt_svc_def` and `ble_gatt_chr_def` arrays, the vectors,
  the NimBLE attribute and client-configuration pools that `ble_gatts_start()`
  rebuilds and the two `esp_timer`s: about 3.1 KiB with block heads, rounded
  up. The S3 boards keep the NimBLE pools in PSRAM.

Both floors hold for the measured StickS3 pairs: the post-#394 27,351 / 25,588
less the 3 KiB supervisor admits the identity task, and the pre-#394
13,543 / 12,788 admits the companion. Host checks pin both.

Simulator: the WebUI model gains `seed webui_identity_absent`,
`action webui-enable` and `webui.identity`, and runs `identityGate()` on the
runtime enable. The hardware model's runtime `action companion-enable` runs
`CompanionMemory::startGate()` and charges the retained task and GATT
database once; a seeded boot enable (`companion-boot`) is part of the seeded
heap. `memory-webui-identity-gate.txt` and `memory-companion-start-gate.txt`
squeeze the heap until the gate refuses, assert no allocation failure, release
and assert recovery.

Deviations:

- The identity floor is one constant for all boards. A per-board floor keyed on
  `CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC` would admit the S3 boards about 8 KiB
  earlier, but the header is shared with the host tests and simulator, and the
  StickS3 runtime enable already passes.
- The simulator charges only the identity stack, because the StickS3 keeps
  the mbedTLS heap in PSRAM. The gate still requires the full floor.
- The `!= pdPASS` fallbacks after each gate stay. They are dead on hardware and
  keep the host doubles meaningful.
- The companion gate tells the user to enable it again; nothing re-enables it
  automatically, because a retry loop would spend the radio on a heap that has
  not recovered.

Validation (local, macOS, sim-only, no hardware):

- Host suite: 185 of 185, including `companion-memory`, the identity checks in
  `webui-security`, the production `reloadSetting()` refusal and retry in
  `webui-handler`, and the linked `boundary-companion-gatt`.
- `sh tools/run_sanitized_host_tests.sh --clean`: 10 of 10.
- `sh sim/scripts/run-e2e.sh`: StickS3 210 of 210, StickC 65 of 65, Core 69
  of 69.
- `sh sim/scripts/run-memory.sh` (MQTT build): 4 of 4.
- `sh sim/scripts/run-panic-fixtures.sh`: pass.
- Firmware: all seven envs build; `tools/stack_budget.py` passes every task
  with zero FAIL. The supervisor and companion figures are unchanged
  (Waveshare and headless `webui` 2,992 of 3,072, `companion` 2,784 of 3,072).
- `python3 tools/pre_review.py --base origin/master`: 8 gates pass.
  `sh sim/scripts/check-doc-tokens.sh`: pass.

## PR 8: per-board config tuning

State: implemented. Every change is in the committed release sdkconfigs; all
seven release files were rebuilt together.

Changes:

- Core2: `CONFIG_SPIRAM` on, with the StickS3 PSRAM profile:
  `SPIRAM_USE_MALLOC`, `SPIRAM_MALLOC_ALWAYSINTERNAL=4096`,
  `SPIRAM_MALLOC_RESERVE_INTERNAL=32768`, `FREERTOS_TASK_CREATE_ALLOW_EXT_MEM`,
  mbedTLS and the NimBLE host in PSRAM, and the release memtest off. The Core2
  datasheet lists 8 MB quad PSRAM on GPIO16/17, which furble does not use
  there (GPS is on Port A, GPIO32/33). M5GFX and M5Unified do not key ESP32
  board detection on PSRAM. The display draw buffers already request
  `MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL`, and every PSRAM path in the firmware
  is behind `CONFIG_SPIRAM`, the same code the StickS3 runs.
- Core2: `CONFIG_ESP32_REV_MIN_3`. With PSRAM, the default revision 0 floor
  enables the rev 1 PSRAM cache workaround, which places libc in IRAM and
  overflowed `iram0_0_seg` by 1,692 bytes. Core2 and Tough ship the
  ESP32-D0WDQ6-V3 (revision 3), where the bug is fixed, so the workaround is
  not needed. The bootloader refuses an older chip instead of running it.
- Core2 debug merges `sdkconfig.spiram-memtest` like the S3 debug envs.
- Waveshare and headless: `BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL`, as on the
  StickS3. `docs/webui.md` already claimed this for the headless build.
- All boards: WiFi dynamic RX and TX buffer caps 32 to 16. Plan 305 rejected
  this because the caps are not preallocated and save nothing steady. Under
  `HEAP_ABORT_WHEN_ALLOCATION_FAILS` the cap matters: a receive burst that
  cannot allocate its buffer reboots the device, and the cap bounds that
  transient at 16 frames of about 1.6 KiB. 16 still exceeds the 6-frame
  AMPDU BA window.

Derived boot heap (`tools/derive_sim_heap.py`, upper bound): Core2 111,064 to
130,292 free and 129,984 largest, Waveshare 142,908 to 162,288. The sim models Core2
and Tough with 8 MB quad PSRAM. Their memory profile rows had stale figures (a
6 KiB MQTT owner and worker and 20 KiB internal mbedTLS) and now match the
firmware constants: 10,240 owner, 3,072 plain worker, mbedTLS external, the
32 KiB PSRAM internal reserve.

Not changed, with the reason:

- NimBLE connections 9 to 5 and bonds 15 to 8. Fewer connections caps
  Multi-Connect, a user-visible limit. Fewer bonds drops existing bonds on
  upgrade and contradicts `CompanionGatt::MAX_BONDS` (15). The saving is a few
  hundred bytes per connection.
- `SPIRAM_TRY_ALLOCATE_WIFI_LWIP`. It forces static WiFi TX buffers; plan 305
  defers it to an OTA throughput soak, which needs hardware.
- `MBEDTLS_DYNAMIC_BUFFER` on boards without PSRAM. With Core2 on PSRAM, no
  board without PSRAM runs TLS (the 4 MB boards build neither MQTT nor WebUI).
- `HEAP_POISONING_LIGHT` in release. Light and comprehensive add the same
  per-block header and canary, so it saves CPU, not memory.
- A uniform assertion level. It is a diagnostics choice, so PR 13 decides it.

Preexisting: the `board_heap.json` committed with #430 was derived from an
older ELF. Master's own build already ends static DRAM 176 bytes later on the
ESP32 boards and 184 on the StickS3 (`_heap_start` 0x3ffcb2a0 on the StickC
in both builds), so the regenerated derivation and the golden floors in
`sim/coverage/memory_budget.json` move down by that amount there. The Core2
and Waveshare floors move up to their new derived values.

Validation (local, macOS, sim-only, no hardware):

- Firmware: all seven release envs plus `m5stack-core2-debug` and
  `m5stick-s3-debug` build; `tools/stack_budget.py` passes every env. Core2
  uses 1,966,069 bytes of its 6,291,456-byte slot.
- `sh sim/scripts/run-e2e.sh`: 210 of 210. `sh sim/scripts/run-memory.sh`
  (MQTT build): pass. `python3 sim/scripts/run-memory-profile.py`: 13 of 13
  dumps within budget.
- Host suite: 185 of 185.
- `python3 tools/pre_review.py --base origin/master`: pass.
  `sh sim/scripts/check-doc-tokens.sh` and `tools/check_sim_coverage.py`: pass.

## PR 9: GPS buffer audit

The plan asked to confirm or add caps on the GPS line buffers. Every buffer
the UART bytes can grow already had a cap. This PR deletes `m_SatPartial`, a
`std::string` member no code ever read or wrote, and tightens the GSV bound:
`feedGsv` consumed every quad on a line, so a checksum-valid 96-byte line
could add more satellites than the four the format allows. It now
reads at most four, which bounds a set at 32, the `MAX_SATELLITES` publish
cap.

| Buffer | Bound | Where |
|---|---|---|
| `m_Partial` (raw NMEA line) | `SENTENCE_LEN` (96) bytes; longer lines are cut | `GPS::captureSentences` |
| `m_Sentences` ring | `SENTENCES` (8) lines of at most 96 bytes | `include/FurbleGPS.h` |
| `m_EphNmeaPartial` | 96 bytes; `$`, CR or LF resets it | `GPS::noteEphemerisDate` |
| `m_RxBuffer` | one pending frame: `MAX_BINARY_PAYLOAD` (2,048) plus the 10-byte overhead plus one UART read; NMEA bytes are drained at once | `GPS::processSerial` |
| `m_ConfigQueue` and `m_ConfigStatus` | `MAX_CONFIG_COMMANDS` (16); a full queue logs and drops | `GPS::enqueueConfig` |
| `splitNmea` fields | at most one field per byte of a 96-byte line | `lib/furble/protocol/GpsCasic.cpp` |
| GSV `TalkerSet` list | one per constellation, six values | `NmeaSatellites::setFor` |
| GSV `building` | `total <= 8` sentences of at most four satellites (enforced since this PR), repeated sentence numbers ignored: 32 | `NmeaSatellites::feedGsv` |
| GSA used list | `MAX_USED_SATELLITES` (64) | `NmeaSatellites::feedGsa` |

Not done: the optional ESP32-S3 LVGL allocator that prefers PSRAM. The plan
adopts it only if the simulator shows no redraw regression, and the simulator
does not model PSRAM access latency, so it cannot show that. It stays
deferred until a hardware timing pass.

### Fix: stack budget parsed a destructor alias from the ELF

Removing the member moved code, and `tools/stack_budget.py` then failed the
StickS3 control task: 5,216 bytes with 164 unbounded recursions, against
2,192 bytes on the parent commit. objdump labelled the shared address of
`std::_Vector_base<NimBLERemoteService*>::~_Vector_base()` by its D1 alias.
The D2 body has a `.ci` node, but the tool only skipped ELF functions whose
exact name was compiled, so it parsed the alias and read past its `retw.n`
into the literal pool. Those bytes decoded as a call to `GPS::update`, which
reaches the UI and LVGL event recursion. The tool now skips a C1/D1 alias of
a compiled body and resolves calls to it through the body's `file:symbol`
node. `body_of()` finds the C1/D1 by walking the `_ZN...E` nested name: its
length-prefixed source names, substitutions and template arguments. So
`A::ABC1()` is left alone and the destructor of a class `Foo2` still resolves.
The alias is skipped only when `objdump -t` puts it at its body's address; a
distinct C1 or D1, as with virtual bases, is parsed.
The control task is back at 2,192 bytes, and the StickS3 main task lists 23
fewer callees without a frame. `tests/test_stack_budget.py` covers the alias.
All nine envs built for PR 8 still pass with the fixed tool.

Validation: m5stick-s3 and esp32-s3-headless build and pass the stack budget;
the default and MQTT simulator builds; e2e 210 of 210; `run-memory.sh`; the
memory profile 13 of 13; host 185 of 185; `pre_review.py`, the doc token check
and `check_sim_coverage.py`.

## PR 10: static analysis on the firmware and raw ownership

The gate ran clang-tidy only with the host and simulator compile databases,
skipped any changed source missing from both, and had no ownership check.

- `tools/firmware_compile_db.py` rewrites the `pio run -t compiledb` database
  for clang. Released LLVM has no Xtensa target, so it uses an i686 stand-in:
  32-bit, little-endian, and its `a` constraint accepts the `=a` outputs of
  the Xtensa FreeRTOS port. riscv32 was tried first and rejects them. It
  defines `__XTENSA__`, drops GCC-only flags, adds the toolchain's libstdc++
  and newlib as system headers, and passes `-w`, because GCC owns compiler
  warnings on firmware. Every furble source parses with zero errors. The
  m5stick-s3 database covers 75 sources and the m5stick-c database 73.
- With both firmware databases, every tracked production source is in a
  database except `sim/env_mutation_guard.c`, a Linux `LD_PRELOAD` helper. It is
  named in `NO_COMPILE_DB`; any other source without a compile command now
  fails the gate instead of being skipped.
- `cppcoreguidelines-owning-memory` is on, with its legacy-resource lists
  empty: `FILE` handles and the C allocators are left to the path-sensitive
  `clang-analyzer-unix.Stream` and `unix.Malloc` checks, which need no
  annotations on C APIs. `bugprone-unchecked-optional-access` was already on
  through `bugprone-*` and has no findings. The memory families are
  `WarningsAsErrors`. A promoted check inside a vendored header, such as the
  M5GFX `LGFXBase.hpp` deletes, is not reported as a compile failure; any other
  clang-tidy error still is.
- `bugprone-invalid-enum-default-initialization` is off: ESP-IDF config
  structs are zero-initialized by contract and several of their C enums have
  no zero enumerator.
- The UI and headless request queues used raw `new` and `delete`. They now
  build a `std::unique_ptr`, `release()` it into the queue and `reset()` it on a
  failed send, and every drain wraps the received pointer. The remaining
  `new` sites are placement new into static or preallocated storage and the
  simulator's FreeRTOS and IDF handle shims; each carries a NOLINT with that
  reason.

Running the gate over all 104 sources with the four databases found these
preexisting issues, fixed here:

| Site | Finding | Fix |
|---|---|---|
| `lib/furble/Scan.cpp` | use-after-move of the probe worker (path-insensitive; the move and join were guarded by the same flag) | the blocked branch moves and returns, so `join()` is unconditional after it |
| `include/FurbleMemoryPressure.h` | `clears()` indexed `ENTER` with an unchecked level | a level past RED returns false |
| `src/FurbleSD.cpp` | `getPin()` returns `int8_t`, so `cs == 255` could never match | dropped; the gpio cast goes through `uint8_t` after the `>= 0` check |
| `src/FurbleWiFi.cpp` | `g_ScanShowHidden` written, never read | deleted |
| `src/main.cpp` | unused headless `count`; implicit `T**` to `void*` at the queue calls | deleted; explicit casts |
| `src/FurbleCalibrate.cpp` | corner initializers missed the union braces | `{{x, y}}` |
| `src/FurbleUIIntervalometer.cpp` | unit switch had no default | `default: break` |
| `lib/furble/NikonBase.cpp` | C-style casts (cppcheck) | `reinterpret_cast` |
| `lib/furble/DJIOsmo.cpp`, `Lumix.cpp`, `sim/heap_model.cpp` | values only logged, or used on PSRAM boards only | `[[maybe_unused]]`, a narrower scope, or a NOLINT naming the board condition |
| `sim/PreferencesSim.cpp` | `std::move` of a `const` copy | non-const |
| `src/FurbleIR.cpp`, `src/FurbleEthernet.cpp` | GPIO 46 cast out of range on the ESP32 database; destructor may throw | NOLINT: only the S3 image runs on a StickS3; firmware has no exceptions |

`static-analysis.yml` installs PlatformIO, generates both firmware databases
and passes them to the gate, then runs the tool tests. Fork Actions are
disabled, so this was validated locally only.

## Follow-up: the Core OTA debug profile is a mandatory CI lane

State: implemented.

After the flash slimming `m5stack-core-debug` fits its 1,740,800-byte slot
again: 1,723,200 bytes at #438, with the stack protector left out of that
profile only. It was still a `workflow_dispatch` opt-in, so nothing stopped it
from outgrowing the slot again unnoticed. `main.yml` now builds it on every
firmware change next to `m5stack-core-usb-debug`, with the same strict stack
and size budget steps, and the `core_ota_debug` input is gone.
`tools/check_ci_workflows.py` requires both profiles in the discover env and
rejects an opt-in input or an optional Core debug entry. Tests run the real discover
step and assert its matrix holds both profiles. Release and web
installer matrices are unchanged.

## PR 14: push hooks

State: implemented.

`.githooks/pre-push` runs `tools/pre_review.py --base origin/master --head
<sha>` for every pushed ref and skips deletions. Git runs it at push time, so
it reviews the commit actually sent, including one made earlier in the same
command line. Each SHA is checked out in a temporary detached worktree, which
is removed afterwards, because `--head` steers only the diff scanners; the
workflow, scenario and coverage checks and the unit tests read the checkout. Enable it once per clone with
`git config core.hooksPath .githooks`; `git push --no-verify` skips one push.

`.claude/settings.json` is now tracked, and `.gitignore` still ignores the
rest of `.claude/`. Its `env` sets `GIT_CONFIG_COUNT=1`,
`GIT_CONFIG_KEY_0=core.hooksPath` and `GIT_CONFIG_VALUE_0=.githooks` for
every Claude Code Bash call, so an agent's push runs the same hook with no
per-clone setting. The first cut used a `PreToolUse` gate script instead.
Review showed that a gate running before the command cannot see the refs a
push sends or scope `--no-verify` to one push, so Git now does both.

Git exports `GIT_DIR` and related variables to a hook run from a linked
worktree. The first push through the hook from such a worktree ran the
scanner unit tests with them set, and the tests that build scratch
repositories committed into the real one, set `core.bare=true` and wrote a
test `[user]` into its shared config. The hook blocked that push, so nothing
reached GitHub, and the repository was restored. The hook now clears them with
`unset $(git rev-parse --local-env-vars)` before the gate runs, and a test
pushes from a linked worktree with a reviewer that fails if `GIT_DIR` reaches
it; it fails without the fix.

Setting `core.hooksPath` replaces `.git/hooks`. furble stores no Git LFS
objects, so the LFS hooks a global `git lfs install` writes there are unused.

`tests/test_push_hooks.py` pushes through real git to a local bare remote with
the settings env and a stub reviewer. It covers a clean push, a blocked push,
a push of a ref other than `HEAD`, a push with an uncommitted change,
`--no-verify` on one push only, a push from a linked worktree, and a deletion.
The stub fails unless it runs in a checkout of exactly the pushed SHA.

## PR 13: debug hardening

State: implemented.

The debug images gain four checks. None reaches a release image.

- `sdkconfig.debug` adds the FreeRTOS end-of-stack watchpoint and standalone
  heap tracing.
- `sdkconfig.stack-protector` adds `COMPILER_STACK_CHECK_MODE_NORM`. Every
  debug env merges it except the `m5stack-core-debug` OTA profile. On
  Core it costs 24,284 bytes; the image measured 1,746,997 bytes against the
  1,740,800-byte slot with it and 1,722,697 without. Heap tracing costs only
  2,376 bytes there and stays. `m5stack-core-usb-debug`, the mandatory Core
  debug build, keeps the protector.
- `FURBLE_TASK_WDT` subscribes the MQTT owner, the WebUI supervisor and the
  companion service loop to the task watchdog through
  `include/FurbleTaskWatchdog.h`, and each loop feeds it once per pass. The
  timeout logs a backtrace of every CPU. `CONFIG_ESP_TASK_WDT_PANIC` stays off
  by decision until a hardware soak shows no false trigger. Release,
  simulator and host builds compile both calls to nothing.
- The console `heap trace start [records] | stop | dump` command drives the
  tracer. `start` takes 1 to 1,000 records, default 100, and admits the
  record buffer only if the largest internal block is at least twice its
  size, so it cannot be the allocation that aborts. Other builds report that
  tracing is not enabled. The host console suite compiles the enabled path
  against an `esp_heap_trace` double and covers each of these cases.

The 8 and 16 MB partition tables end with a 256 KiB `coredump` partition right
after `ota_1`, which takes space that was unallocated. The ELF dump holds each
task's TCB and used stack; with every stack full the manifest tasks (about
106 KB on Core2), the ESP-IDF service tasks, transient tasks and headers
come to about 180 KB, and ESP-IDF writes nothing when a dump does not fit.
The first cut was 64 KiB; review showed that was too small. The debug envs for
those boards merge `sdkconfig.coredump-flash`, so a panic writes a core dump
there. `tools/check_partition_tables.py` requires that partition, its size
and its position on boards larger than 4 MB and rejects it on 4 MB tables.
Waveshare was missing from the checker and is now covered, with its release
image as the reference app size;
`tests/test_check_partition_tables.py` covers each rule.

`m5stack-core-usb-debug` used to repeat four `sdkconfig.debug` lines in its
own fragment. It now layers `sdkconfig.debug` and the protector under a
fragment that holds only its partition choice.

The debug RAM budgets were re-recorded with `check_size_budget.py --write`.
The largest budget moves are S3 DRAM +3 KiB and Core2 DRAM +4 KiB, where the
flash core dump is also on. The flash slots are unchanged and every image
fits. Every task still passes `stack_budget.py`
with the protector's frame cost.

## PR 12: dynamic analysis on every host test

State: implemented.

`tools/run_sanitized_host_tests.sh` ran three test groups by default (GPS
CASIC, GPS power cycle, preferences). It now runs every host test under ASan
and UBSan except the two ThreadSanitizer probes, which it excludes with
`ctest -E` instead of refusing the selection. `-R` still narrows a run. The
`host-sanitized.yml` job inherits the new default.

The ThreadSanitizer item needed no change: `control-connect-camera-race` and
`control-connecting-snapshot` are ctest entries that run through
`tests/host/run_tsan_race.sh`, and the `main.yml` host job runs the full
ctest suite on every firmware change.

The fuzz targets used to drop ASan and UBSan silently when the compiler
lacked them. That is now a configure error unless
`-DFURBLE_FUZZ_ALLOW_UNSANITIZED=ON`, and `tools/check_fuzz.py` reports a
finding for any fuzz binary that does not contain both the `__asan_init` and
`__ubsan_handle_` runtime symbols. It checks the binary it is about to run,
not the CMake cache, so a stale build from before this change cannot pass.
The flags used to reach only `control_fuzz` and `companion_fuzz`; `mqtt_fuzz`,
`webui_fuzz`, `settings_migration_fuzz` and `wifi_policy_fuzz` ran without
them in the normal host build. The probe now sits before the first fuzz
target, one loop applies the compile and link flags to every entry in
`FUZZ_TARGETS`, and a test requires every `*_fuzz` executable to be listed.
The normal host suite passes 185 of 185 with all six sanitized, on macOS and
on native arm64 Linux with LeakSanitizer.

The wider run found five preexisting faults, all in host doubles and tests;
no firmware source changed:

| Test | Sanitizer | Fault | Fix |
|---|---|---|---|
| `webui-fuzz` | UBSan | the host cJSON double cast an out-of-range number (7.8e145) to `int` | saturate to `INT_MAX`/`INT_MIN` like upstream cJSON; firmware uses ESP-IDF cJSON, which already does |
| `real-wifi-lifecycle` | UBSan | the WiFi stub `Control::state_t` had two values, and the test feeds all six | the stub mirrors the six production states |
| `sim-watchdog` | ASan (macOS) | the test disabled its thread's alternate signal stack on exit, discarding the one ASan installed, so ASan failed to unmap it | restore the stack the thread started with |
| `console-commands` | LeakSanitizer (Linux) | the console shim joined its task threads but never freed their three records | free them after the join |
| `boundary-sd-mount-write` | timing (emulated Linux) | the test read the GPX file once it was non-empty, before the point and closers were written | wait for a `<trkpt` and `</gpx>`; the full-card case measured the same partial file |

Results: macOS ASan+UBSan 183 of 183 (LeakSanitizer is unsupported there),
native arm64 Linux with GCC and LeakSanitizer 183 of 183, and the normal
host suite 185 of 185. Under amd64 emulation `sim-watchdog` hit its 5 s
fatal-child deadline from emulation overhead alone; native Linux passes.

## PR 11: size budget gate

State: implemented.

`tools/check_size_budget.py --env <env>` fails a build that outgrows its
flash slot or its static RAM budget. `tools/size_budget.json` records, per
env:

- `flash_slot`: the app partition size from the CSV it names. `firmware.bin`
  must fit, or the image cannot be flashed or installed over the air. A test
  checks every slot against `check_partition_tables.parse_csv`, so a
  partition change cannot leave a stale slot behind.
- `dram_bytes`: static DRAM, `.dram0.data + .noinit + .dram0.bss`. Every byte
  comes out of the boot heap that `tools/derive_sim_heap.py` derives.
- `iram_bytes`: every `.iram0.*` section.

The sections come from the ELF section headers through a small reader in the
tool, so it needs no toolchain. A RAM budget is the measured size plus at
least 512 bytes, rounded up to a KiB. Plain KiB rounding left the Core IRAM
budget 44 bytes above the image, where any unrelated change would have
failed. A change that crosses a budget raises it with `--write` and states
its reason in the PR. A test requires an entry for every `[env:*]` in
`platformio.ini`, which added the `m5stick-s3-debug-ble` bench image. A
missing build reports `FAIL <env>: not built` instead of a traceback. Firmware
CI runs the check after the stack budget on every matrix env. The matrix has
no headless build, so `reproducible.yml` runs it on the kept
`esp32-s3-headless` build through `--build-dir`, as it does the stack budget.
The slot test takes the smallest app partition, and without the ESP-IDF
package it compares the stock table against the OTA size that
`check_partition_tables.py` pins, so it never skips.

`-Wframe-larger-than=2048` from the original plan was dropped.
`tools/stack_budget.py` already checks every task's full path against its
real stack, which is stricter than a per-frame limit. On the current builds
the only frame above 2,048 bytes is `WebUI::createIdentity` (4,048), which
runs on the 12 KiB TLS identity task and passes its budget.

| Env | Flash / slot | DRAM / budget | IRAM / budget |
|---|---:|---:|---:|
| m5stick-c | 1,490,112 / 1,740,800 | 54,780 / 55,296 | 114,024 / 114,688 |
| m5stick-c-plus | 1,540,112 / 1,740,800 | 54,756 / 55,296 | 114,024 / 114,688 |
| m5stack-core | 1,614,288 / 1,740,800 | 54,884 / 56,320 | 115,668 / 116,736 |
| m5stack-core2 | 1,898,592 / 6,291,456 | 56,496 / 57,344 | 121,036 / 121,856 |
| m5stick-s3 | 1,848,896 / 3,145,728 | 47,208 / 48,128 | 132,864 / 134,144 |
| waveshare-s3-eth | 1,696,464 / 6,291,456 | 51,332 / 52,224 | 132,352 / 133,120 |
| esp32-s3-headless | 1,759,328 / 3,145,728 | 51,952 / 53,248 | 140,032 / 141,312 |
| m5stick-c-debug | 1,588,016 / 1,740,800 | 55,772 / 56,320 | 116,336 / 117,760 |
| m5stick-c-plus-debug | 1,637,488 / 1,740,800 | 55,748 / 56,320 | 116,336 / 117,760 |
| m5stack-core-usb-debug | 1,719,456 / 4,063,232 | 55,876 / 57,344 | 118,056 / 118,784 |
| m5stack-core-debug | 1,719,952 / 1,740,800 | 55,892 / 57,344 | 118,056 / 118,784 |
| m5stack-core2-debug | 2,008,368 / 6,291,456 | 57,536 / 58,368 | 123,280 / 123,904 |
| m5stick-s3-debug | 2,200,224 / 3,145,728 | 48,932 / 50,176 | 151,296 / 152,576 |
| m5stick-s3-debug-ble | 2,201,376 / 3,145,728 | 48,940 / 50,176 | 151,296 / 152,576 |
| waveshare-s3-eth-debug | 1,987,184 / 6,291,456 | 52,272 / 53,248 | 146,688 / 147,456 |

Flash is the `firmware.bin` file size, which runs about 500 bytes above
PlatformIO's "Flash used" figure because it includes the image header and
padding.

## Fix: flash slimming, every debug image back in its OTA slot

State: implemented. Size-only change: no feature, log line or console command
is removed, and IPv6 stays enabled on every board.

Master's `m5stick-c-debug` (1,760,585 bytes) and `m5stick-c-plus-debug`
(1,761,261) had outgrown the 1,740,800-byte OTA slot, and the opt-in
`m5stack-core-debug` was 1,810,109. #434 proposed a single factory slot for
the StickC debug images; this change makes it unnecessary.

| Change | Boards | Saving |
|---|---|---|
| `SD_HARDWARE` constant compiles out FATFS, SDMMC and SDSPI | StickC, StickC Plus (no SD slot) | 54 to 65 KB |
| Pairing code uses only Montserrat 16 on the 80 px panel | StickC | about 25 KB |
| `CONFIG_LOG_VERSION_2` (ESP-IDF Log V2) | all | about 31 KB of format strings plus smaller call sites; S3 release 80 KB with the two below |
| `CONFIG_LWIP_DHCPS=n`: only SoftAP used it, and SoftAP is compiled out | all | 2,916 bytes |
| `CONFIG_MBEDTLS_GCM_SUPPORT_NON_AES_CIPHER=n`: only AES-GCM is used | all | 1,804 bytes |
| `M5GFX_BOARD` pins the board in `M5GFX::autodetect` | `m5stick-c`, `m5stack-core`, `m5stick-s3` | about 22 KB on ESP32 |

`patches/m5gfx_board.patch` adds a three-line `#if defined(M5GFX_BOARD)` in
`autodetect`, so the compiler drops every other board's probe, panel and
touch driver. PlatformIO installs `lib_deps` after `pre:` scripts run, so
`post:patches/m5gfx_board.py` applies it through the existing locked,
marker-checked `apply_patch`. A fresh libdeps directory was verified.
StickC Plus/Plus2 and Core2/Tough share an image and keep autodetect.

Log V2 writes one record as three vprintf calls: prefix, message, newline.
`FurbleCrashLog::logVprintf` now joins fragments up to the newline under its
lock before keeping the last line. ESP-IDF holds the stdout lock across the
three calls, so records from different tasks do not interleave. The host
double sends the V2 shape, and `crash_log_test` fails on the old hook.

Log V2 also deepens every logging path by 100 to 700 bytes. The WebUI
supervisor reached 3,136 of 3,072 bytes on Waveshare and headless, so
`SUPERVISOR_STACK_BYTES` is now 3,584 and its admission floors 4,608 / 4,096.
The WebUI e2e scenarios and the two memory budget floors move by exactly
those 512 bytes; the pressure and gate decisions they test are unchanged.

Rejected: IPv6 off (required), `BT_NIMBLE_PRINT_ERR_NAME=n` (a debug
feature), non-P-256 curves (WPA3), newlib nano printf (`%f`, `%llx` and
`PRId64` are used), scanf and `strtod` removal (pulled in by `tzset` and
`strtof`), font symbol stripping and compressed fonts (regenerated assets or
render cost), and M5Unified IMU, PMIC and touch drivers (runtime probed, no
macro). The NimBLE services, error string, LVGL widget and compiler options
already cost nothing.

`tools/stack_budget.py` fix found here: an `entry` after alignment padding
can be misdecoded, and the 0x1e-byte `config_get_wifi_task_stack_size` took
the calls of the unnamed blob function behind it. The headless WiFi task then
reported three unbounded association cycles. The walk now stops at the
`objdump -t` symbol size until the next decoded `entry`, with a regression
test.

Before is master, except the two StickC release rows, which already include
the SD and font changes. Debug rows are master.

| Image | Before | After | Slot |
|---|---:|---:|---:|
| m5stick-c | 1,562,925 | 1,490,037 | 1,740,800 |
| m5stick-c-plus | 1,589,793 | 1,540,097 | 1,740,800 |
| m5stack-core | 1,691,621 | 1,613,277 | 1,740,800 |
| m5stick-s3 | 1,984,333 | 1,847,137 | 3,145,728 |
| m5stack-core2 | 1,964,637 | 1,898,081 | 6,291,456 |
| waveshare-s3-eth | 1,790,793 | 1,696,065 | 6,291,456 |
| esp32-s3-headless | 1,846,417 | 1,758,649 | 3,145,728 |
| m5stick-c-debug | 1,760,585 | 1,588,353 | 1,740,800 |
| m5stick-c-plus-debug | 1,761,261 | 1,637,797 | 1,740,800 |
| m5stack-core-debug | 1,810,109 | 1,719,453 | 1,740,800 |

`m5stack-core-debug` fits again with 21,347 bytes free. It stays a
workflow-dispatch opt-in here; making it the mandatory Core debug profile
again is a separate CI change. Serial output and the crash-log tail under
Log V2 are not yet verified on hardware (sim-only mode).

## Baseline: regenerated release sdkconfigs

State: implemented, as the clean baseline for PR 8 config tuning.

Every firmware build rewrote the committed sdkconfig files, which left the tree
dirty (the flash readiness gate refuses a dirty tree) and the seven files
inconsistent with each other. This commits what the pinned ESP-IDF 5.5.3
confgen writes for m5stick-c, m5stick-c-plus, m5stack-core, m5stack-core2,
m5stick-s3, waveshare-s3-eth and esp32-s3-headless:

- deprecated aliases follow their real options (`REDUCE_PHY_TX_POWER`, the
  StickS3 `NIMBLE_MEM_ALLOC_MODE`, `MAIN_TASK_STACK_SIZE`);
- options hidden by disabled dependencies are no longer written;
- the `# default:` annotations that only four files carried are gone;
- the StickS3 static RX comment is dropped; plan 304 item 5 keeps its reason.

The effective configuration is unchanged, and a second build regenerates the
files byte for byte. Deviation: none. PR 8 makes its tuning changes against
this baseline, and CLAUDE.md now says to keep setting rationale in plan docs,
because regeneration strips comments.

## Fix: no deque allocation on an idle UI tick

State: implemented, found while building the PR 4b sanitizer job.

`Scan::processPendingCallbacks()` and the simulator request queue run on every
UI tick and built a `std::deque` before checking for work. libstdc++, which the
device uses, allocates the deque's map and a 512-byte node in that constructor.
macOS libc++ does not, so only the Linux simulator showed it: the counted heap
read 1,088 bytes at idle and `memory-counted-camera-connect` failed. gdb on the
Linux build attributed every idle counted allocation to these two sites. Both
now check for pending work under their lock first. The Linux ASan build reads 0
counted bytes at idle and passes the scenario; the macOS e2e suite is unchanged.

## Fix: the esp-mqtt worker stack overflow

State: implemented, found by the PR 4a stack walk before that PR merged.
`MQTT::eventHandler` runs on the esp-mqtt client task, which has a 3,072-byte
stack on plain MQTT since #407. The handler held a 2,348-byte
`queued_event_t` on that stack (a 2,384-byte frame), and the walk through
`esp_event_loop_run` reached 4,352 bytes. Release builds have no stack
watchpoint, so an overflow would corrupt memory silently.

The handler now fills `m_Incoming`, a copy the owner allocates with the event
queue, in PSRAM when the queue is there. It is never freed, because a stopped
client may still deliver a late callback, and a mutex covers overlapping
clients. Without PSRAM the start gate reserves it with the queue
(`EVENT_STORAGE_BYTES`), while the largest-block floor stays the queue alone,
since the two are separate blocks. The walk now reports 2,560 of 3,072 on the
StickS3. No hardware retest yet.

## PR 4b: simulator sanitizers and the permutation assertion

State: implemented.

- The `sim-sanitizers` CI job builds the simulator with
  `FURBLE_SIM_SANITIZE=address,undefined` on Linux and runs the certified e2e
  suite with LeakSanitizer on. The full suite takes about six minutes.
- On the first full run, every UBSan report came from LVGL or M5GFX:
  function-type casts, null plus zero, packed misaligned members, a negative
  shift and an infinite float cast. LeakSanitizer reported two leaks: M5GFX's
  SDL frame buffers, the only one in linked code, and the sanitizer runtime's
  own symbolizer (`__sanitizer::Symbolizer::PlatformDemangle`), which keeps
  its demangled names. `sim/ubsan.supp` and `sim/lsan.supp` name each site and
  its reason.
  No furble frame is suppressed.
- Three virtual camera peers used `std::recursive_mutex` without
  `<mutex>`. That compiled only through libc++ transitive includes, so the
  simulator did not build on Linux at all.
- Permutations asserted `heap.free >= 0` on an unsigned value. They now assert
  `heap.alloc_failures 0`; exhaustion already aborts the run with status 3.
  The permutation manifest and its `config-matrix.txt` template name the same
  assertion, so the coverage gate describes the executable check.

Deviations:

- Sim pthread stack sizing from a host factor is dropped. Host frame sizes
  change with the build flags (ASan inflates them severalfold), so a
  factor-sized stack would report false overflows. `tools/stack_budget.py`
  (PR 4a) is the stack evidence.
- A real permutation floor waits for measured per-board seeds. The derived
  seeds are ceilings, about 144 KB above the measured StickS3 figure, so any
  floor checked against them proves little.
- The Linux run found that libstdc++ allocates in the `std::deque`
  constructor, as the device does, while macOS libc++ does not. Two per-tick
  deques followed: production `Scan::processPendingCallbacks` and the
  simulator request queue. #422 fixed both.

## PR 4a: task stack budget from the compiler call graph

State: implemented. Every firmware object is compiled with
`-fcallgraph-info=su`. `tools/stack_budget.py` merges the `.ci` call graphs
and walks each task entry in `tools/stack_budget.json`, adding the Xtensa
port overhead (interrupt frame plus coprocessor save area, 288 bytes on ESP32
and 512 on ESP32-S3). Firmware CI fails a task whose resolved worst case
exceeds its stack. `docs/stack-budget.md` has the per-board table; every task
passes on all seven envs.

The flag does not change the image: a StickS3 build with and without it
differs only in `app_elf_sha256` and the trailing image hash, both of which
cover the ELF's debug info.

Phase 1 exit, MQTT owner: replaying the 6 KiB owner fails (7,840 of 6,144 on
the StickS3) with no scripted constant.

Deviations:

- The plan asked for `-fstack-usage` plus objdump. `-fcallgraph-info=su`
  gives frames and call edges for everything compiled here. Prebuilt ESP-IDF
  libraries have no `.ci`, so an ELF pass reads their `entry a1, N` frames and
  `call8` or `l32r`/`callx8` edges from the disassembly, and skips every
  symbol that a `.ci` already describes.
- The sim pthread stack sizing, the real permutation floor, the component
  check against derived charges and the sim ASan/UBSan/LSan job move to PR 4b
  to keep this review small.
- Indirect calls are followed only where a manifest rule resolves them: LVGL's
  dispatchers by callback signature, and the `esp_event` loop by the handlers
  registered on it. A rule may be scoped to tasks; it then replaces the
  unscoped rule for that caller on those tasks. `main` and `nimble_host` keep most of their depth behind
  unresolved virtuals and callbacks, so their numbers are lower bounds. The
  8 KiB StickS3 main overflow is not reproduced by the walk; its evidence stays
  the hardware high-water mark.
- Post-merge follow-up (`fix/stack-budget-review`): recursion and unbounded
  `alloca`/VLA frames now fail a task unless the manifest cites a bound
  (`recursion_bounds`, `dynamic_bounds`). `main` records its LVGL cycles as a
  known limit. The ELF pass drops a literal once its register is rewritten,
  which removed 299 wrong edges on the StickS3. The `esp_timer` task is
  checked. The gate found cJSON's 1,000-level default nesting on the MQTT and
  HTTPS parsers; `CJSON_NESTING_LIMIT=8` bounds it. The
  `Preferences::get<std::string>()` VLA is gone.

Findings for Phase 2:

- `MQTT::taskStep` and `MQTT::processLifecycleEvents` each hold a stack copy
  of `queued_event_t` (2,400-byte frames). One owner-task buffer would remove
  about 4.6 KB of depth, enough to shrink the 10 KiB internal owner stack.
- The WebUI supervisor has 80 bytes of resolved headroom on the Waveshare and
  headless builds (2,992 of 3,072) and 320 on the StickS3.
- The companion task has 288 bytes on the Waveshare and headless builds.
- Fixed in #424: the WiFi event handler ran NVS and SNTP on the 2,304-byte
  default event loop task (`sys_evt`, 2,864 bytes on Waveshare and headless).
  After the fix it is 2,096 there and 1,632 to 1,856 elsewhere.

## Fix: no NVS on the default event task

State: implemented, found by the PR 4a stack walk once it covered the default
event loop task (`sys_evt`, `CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE` 2,304
bytes on every board).

The WiFi event handler read the NTP setting and started SNTP on `GOT_IP`,
persisted the remembered access point there, and erased it after a failed
direct join, all on that task. The NTP setting read alone, through
`Preferences::begin()` and the NVS namespace lookup, walked to 2,864 bytes on
the Waveshare and headless builds and 2,288 on the StickS3.

The handler now updates the in-memory access point state and queues the save,
the erase and the NTP start under `g_StateMutex`. The WiFi worker, with 4 KiB,
runs them at the top of its loop and before each retry. A newer save or erase
cancels the older one, so only the latest outcome is persisted. A host test
proves `GOT_IP` opens no NVS and starts no SNTP, and that the worker then does
both exactly once.

## Fix: no SDL3 dialog from the macOS simulator

Six simulator runs on macOS stopped on a blocking "Fatal error! Cannot
continue! Failed loading SDL3 library." dialog. Homebrew's `sdl2` 2.32 is
sdl2-compat. Its library initializer dlopens SDL3 through its own rpath and
shows that dialog, then aborts, when the load fails. No environment variable
suppresses it, and a headless gate run then hangs on a GUI window until its
timeout. The failing binaries were already deleted, so the load failure could
not be reproduced; SDL3 resolves in every normal run.

The fix does not depend on the cause. `sim/build.sh` and `sim/CMakeLists.txt`
link `libSDL3.dylib` directly with `-needed_library` on macOS. dyld maps SDL3
at launch, before the sdl2-compat initializer runs, and its dlopen returns the
loaded image. `DYLD_PRINT_LIBRARIES` shows SDL3 mapped once. If SDL3 cannot be
loaded, dyld stops the launch with an error on stderr, which the gates report.
Linux and CI link real SDL2 and are unchanged. A macOS build without SDL3
keeps the plain SDL2 link only for a real SDL2. When `libSDL2.dylib`
resolves into sdl2-compat and no SDL3 is installed, `build.sh` exits and the
CMake configure fails, each naming `brew install sdl3`, because that binary
could only fail at launch through the dialog.

## Fix: clang-format drift on master

The PlatformIO CI clang-format job failed on master `e66e8807` with 44
violations in 12 files: `sim/ui_buttons.cpp` and 11 host tests and shims.
pre_review had no formatting gate, and the CI job is the only check, so
merged PRs carried the drift. This fix reformats those files with clang-format
21. Most hunks are line wrapping. Three boundary tests also get their includes
sorted, so the linked-boundary suite and the affected host tests were rebuilt
and run.

`tools/check_format.py` now runs clang-format 21 on the C and C++ files a
branch changes, under the same directories and suffixes as the CI step.
`pre_review.py` runs it, so the pre-push hook and `pre-review.yml` catch a
formatting failure before merge. A missing or non-21 clang-format fails the
gate with the install command. A test pins its directory and suffix list to
the CI step.

## Fix: the coverage job lost its compiler-rt runtimes

`Coverage floor` failed on every master run from `e8ce6184`. The host configure
first stopped at the fuzz sanitizer probe from PR 12 (#437). With that probe
bypassed, the link showed the real cause: `libclang_rt.profile` and the ASan
runtimes are missing from `/usr/lib/llvm-18`. The workflow installs
`clang-18` with `--no-install-recommends`, and current Ubuntu packaging moves
compiler-rt into `libclang-rt-18-dev`, which `clang-18` only recommends. The
probe failure was a symptom, not a reason to build unsanitized.

`coverage.yml` now installs `libclang-rt-18-dev` and checks that the profile
runtime exists before building. The pinned clang major and the floor are
unchanged. `docs/coverage.md` lists the package for local runs.

With the runtimes back, the run reached the profile check and found 17 host
tests that wrote no raw profile, all `boundary-*`. `tests/host/CMakeLists.txt`
gives each test its own `LLVM_PROFILE_FILE` in a loop over the tests already
registered, and the boundary tests were added below that loop. The job had
not run on master since September, so nothing noticed. The loop now sits last
in the file and appends to each test's environment, so
`boundary-companion-gatt` keeps its `FURBLE_SIM_PREFS`. A test fails if any
`add_test` follows the loop.

The next run left one test, `boundary-headless-service`, with an empty raw
profile. It runs the production `app_main` and must leave from inside the
never-returning service loop, so it calls `std::_Exit`, which skips the exit
handler that writes the profile. Under coverage, CMake now defines
`FURBLE_HOST_COVERAGE`, and the test calls `__llvm_profile_write_file()`
before `std::_Exit`. A local coverage build writes a 41 KB profile for it.

The run after that merged no profiles: `boundary-sd-full` wrote a truncated
one. The test simulates a full card with `RLIMIT_FSIZE` and set both the soft
and the hard limit. An unprivileged process cannot raise a hard limit again,
so its `(void)` restore failed silently and the profile written at exit was
cut off. The test now lowers only the soft limit, restores the original pair
and checks that the restore succeeds.

With every profile merging, the floor check ran for the first time since
September and failed one file: `src/FurbleConsole.cpp` at 88.64% against its
89.08% floor. `crash last` with a retained record had never been tested, so
the lines that print it, including the failed-allocation fields from PR 5,
were uncovered. The console stub now lets a test install a record, and the
console test checks every printed field with and without a failed
allocation. The console reaches 89.26% locally. The floor is not lowered.

## Fix: CI re-enable, 2026-11-13

State: implemented on `ci/master-green`. GitHub Actions was disabled on the
fork from 2026-10-21, so the plan 314 PRs of that window merged with no CI.
Three of their gates failed on the first run. See `plans/95-engineering-lessons.md`
for the full incident.

- PR 12, ASan and UBSan host tests: `gpx-writer` failed only there. GPX::open()
  formats its path into 64 bytes, and the test sandbox was the absolute
  build-tree path. The plain tree truncated silently and the test truncated its
  expectation the same way; the sanitized tree is deeper, so the path was cut
  inside the directory name. `GPX::open()` now sizes the buffer from
  `FURBLE_GPX_DIRECTORY` plus the widest name the point fields can print, and
  fails the open on a truncated path. On the device that is 37 bytes instead
  of 64. The test keeps its long absolute sandbox as the regression check, and
  the coverage workflow no longer needs a short build directory.
- PR 6, `memory-pressure-mqtt-shed`: the paused figure of 8,497 was measured on
  macOS. Linux read 8,457 on arm64 and 8,441 on x86_64. The host MQTT
  transport allocated its adapter object, strings and reconnect thread on the
  owner task, and the counter charged them as device memory, so their host
  sizes leaked into the model. Every libmosquitto call and the adapter's own
  objects now run inside a `HostAllocScope`. In their place
  `esp_mqtt_client_init()` charges what the pinned esp-mqtt allocates
  (`components/mqtt/esp-mqtt/mqtt_client.c`): the 248-byte client and 160-byte
  config storage (sizes from the StickS3 firmware DWARF) and the in and out
  buffers FurbleMQTT configures, 1,536 bytes each, so 3,480 bytes. Destroy
  releases them. The sim had never charged them. The firmware stops and
  destroys the client on a memory pause, so the paused reading rises from
  8,497 to 11,977 (worker plus init allocations) and still sits between the
  RED floor and the RED exit, which keeps the check that such a reading holds
  RED. The scenario is reseeded 4,152 bytes higher so boot and the squeezed
  level keep their old figures. Smaller esp-mqtt allocations (URI strings,
  outbox, transport list) are not modelled.
- PR 4b, sim-sanitizers: every scenario failed with a signed overflow in
  M5GFX `Panel_sdl::_update_scaling()`. Under the dummy video driver
  `SDL_CreateRenderer` returns NULL, `SDL_GetRendererOutputSize` never writes
  its outputs, and M5GFX divides by uninitialized stack ints. clang 18 made
  that an infinite float, which the old `float-cast-overflow` entry
  suppressed; clang 21 on ubuntu-26.04 made it a signed overflow. The
  simulator now sets `SDL_HINT_RENDER_DRIVER=software` before
  `Panel_sdl::setup()` when the video driver is dummy or offscreen, so the
  renderer exists and its size is real. The `float-cast-overflow` suppression
  is removed. An explicit `SDL_RENDER_DRIVER` in the environment still wins.
  A live renderer exposed one more M5GFX lifetime gap: `Panel_sdl::close()`
  never destroys it, and LeakSanitizer reported its 768 bytes in every
  scenario. The SDL lifecycle shim (`sim/shim/FurbleSDLWrap.h`) now routes
  `SDL_CreateRenderer` and `SDL_DestroyRenderer` through
  `sim/sdl_lifecycle.cpp`, which destroys any renderer still live before
  `SDL_Quit`. No leak suppression was added.
- PR 4b, the same job: with the M5GFX report gone, every scenario then stopped
  on an LVGL `function` report that `sim/ubsan.supp` names. The LVGL entries
  were written against `managed_components/lvgl__lvgl/src/...`, but CI clones
  LVGL into `sim/.pio/libdeps/sim/lvgl/src/...`, so they never matched there.
  A pattern matches anywhere in the path, so the entries now start at
  `lvgl/src/`, which covers both layouts. The suppressed sites are unchanged.

## Fix: deep-sleep scenarios that assumed a trusted clock

The intervalometer deep-sleep gate (`sim/scripts/run-deep-sleep.sh`) landed on
2026-11-01, during the Actions outage, and never ran green in CI. Its `stale`,
`late` and `stable-id` scenarios assumed a trusted wall clock at boot. The
deep-sleep review fix the same evening made `trustedWallClock()` reject an
NVS-restored clock, because that sample does not advance across power-off.
The StickS3 has no RTC chip, so the M5PM1 timer is the authority: a timed-wake
resume waits `RESUME_WAKE_MARGIN_S` (15 s) from boot and then shoots, whatever
the record's wake time says. This is the documented firmware policy, and a
probe showed the interval timer behaving exactly so; the scenarios were wrong.

`stale` and `late` now assert that wait and the following shot, and
`stable-id` keeps its camera-selection check with a finish bound that covers
the margin. Rejecting a stale record, or firing a late one at once, needs a
battery-backed RTC sample at boot, which the simulator does not model; that
coverage gap is issue #455. The gate passes 3 of 3 runs in an
Ubuntu 24.04 container.
