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
