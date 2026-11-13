# Task stack budget

`tools/stack_budget.py` computes each firmware task's worst-case stack depth
from the compiler's own call graph and fails a task that does not fit its
configured stack. It is the authoritative stack check: the simulator runs host
frames, which say nothing about Xtensa frame sizes.

## How it works

Every object is compiled with `-fcallgraph-info=su` (`[furble] build_flags` in
`platformio.ini`). GCC writes a `.ci` file next to each object with one node
per function, carrying its frame size, and one edge per call. The flag does
not change code or data: a StickS3 image built with and without it differs
only in the app descriptor's `app_elf_sha256` and the trailing image hash,
both of which cover the ELF's debug info.

Prebuilt code has no `.ci` file: newlib, libstdc++ template instances, and the
WiFi and Bluetooth blobs. For every function the `.ci` graph does not define,
the tool reads `firmware.elf` with the toolchain's objdump. The frame is the
windowed ABI `entry a1, N` allocation; a call is a `call8` to a symbol or a
longcall `l32r` literal followed by `callx8`. That is how `snprintf` (192
bytes), `_svfprintf_r` (800) and `__ssvfscanf_r` (896) are counted.

objdump prints a literal load in two forms. `l32r a8, <slot> (<value>)` names
the literal's value, which is the call target. `l32r a8, <slot>` appears when
objdump cannot read the literal (203 times in the StickS3 image) and names
only the slot, so it never becomes a call edge. A literal names its target
only until its register is written again or a call rotates the window over
it. A `callx8` through any other register is an indirect call. Before this
rule a stale literal attributed 299 calls in the StickS3 image to the wrong
target, most of them to data such as `npl_funcs` or `g_phyFuns`.

A blob's static functions have no symbol. objdump lists one under the
previous symbol and calls it as `<symbol+0xoffset>`. Every `entry` after the
first in a symbol therefore starts a function of that name. Without the split
the StickC Plus WiFi blob showed six recursions that were only neighbouring
functions merged into one, such as `cnx_remove_rc`.

An `entry` that follows alignment padding can be misdecoded, so the split
alone misses that function. The tool therefore stops reading a symbol at its
`objdump -t` size until the next decoded `entry`, which always starts its own
`symbol+offset` function. In the plan 314 slimming the
0x1e-byte `config_get_wifi_task_stack_size` otherwise took the calls of the
unnamed function behind it, and the headless WiFi task walked into three
association cycles in the blob.

A C1 or D1 constructor or destructor is usually an alias of its C2 or D2
body, at the same address, and objdump may label that address by the alias.
The tool skips such an alias when the body is compiled here and the symbol
table (`objdump -t`) puts both at one address, and resolves calls to it
through the body's `.ci` node, which a comdat body titles `file:symbol`. A
C1 or D1 at its own address, as with virtual bases, is distinct code and is
parsed like any other prebuilt function. Parsing the
alias instead read past its `retw` into the literal pool that follows. In plan
314 PR 9 that turned a `std::vector` destructor in `NimBLEClient` into a call
to `GPS::update` and the UI, so the StickS3 control task reported 164
unbounded recursions.

The tool merges every `.ci` file in `.pio/build/<env>`, then walks the graph
from each task entry in `tools/stack_budget.json`. It sums the deepest path
and adds the per-task overhead that the pinned ESP-IDF Xtensa port carves from
every stack: the 192-byte interrupt frame (`XT_STK_FRMSZ`) and the
coprocessor save area (`XT_CP_SIZE`, 96 bytes on ESP32 and 316 rounded to 320
on ESP32-S3). Release builds have no TLS section and no task wrapper.

```sh
FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e m5stick-s3
python3 tools/stack_budget.py --env m5stick-s3            # gate
python3 tools/stack_budget.py --env m5stick-s3 --verbose  # paths and indirect sites
python3 tools/stack_budget.py --env m5stick-s3 --stack mqtt=6144  # replay a size
```

Firmware CI runs the gate after every build in the matrix. Debug envs use their
release env's tasks and sdkconfig.

## What the number means

The walk is exact for direct calls into code in the ELF. Callees outside it,
the mask ROM functions such as `strlen` or `esp_rom_printf`, count as zero and
are reported as "callees without a frame"; `--verbose` lists them. GCC cannot name the target of an indirect
call (a virtual call, a `std::function`, a C callback), so each indirect call
is listed, not followed, unless the manifest's `indirect` section resolves the
caller. The rules there resolve LVGL's dispatchers by callback signature: any
C++ function taking `(lv_event_t*)` may run under `lv_event_send`, and so on
for timers, display flush and input reads. `esp_event_loop_run` resolves to
every `esp_event_handler_t`, which is how the esp-mqtt client task reaches
`MQTT::eventHandler`. That over-approximates, which is the safe direction. A task with unresolved indirect sites therefore reports a
lower bound for paths through them, and the gate catches growth in the
resolved part only.

The walk collapses the task's reachable call graph into strongly connected
components (Tarjan) and takes the longest path over the resulting DAG. A
function's depth no longer depends on the order the walk reached it. Outside a
cycle the result is exact.

A component with a cycle is recursion. A task whose path holds one fails
unless a member matches an entry in the manifest's `recursion_bounds`. An
entry's `depth` is the number of passes the recursion can make, cited from
source. A component matching several entries pays `1 + sum(depth - 1)` passes
over the distinct entries, so nested cycles add their passes.

- One pass (`depth` 1, or a single matching entry of depth 1) means the
  cycle never closes, and the stack holds each member at most once. The walk
  enumerates every simple path through the component and leaves it through
  the last member's deepest callee. This is exact. Every bounded component on
  the seven envs stays under the 200,000-path budget; past it, the walk
  charges the sum of every member's frame, which bounds any simple path.
- More passes charge `passes` times the sum of every member's frame, plus the
  deepest callee that leaves the component. One pass puts each member on the
  stack at most once, so the sum bounds every path through it.
- An entry without `depth` records a documented known limit. It is enumerated
  exactly when it fits the budget. `main`'s 410-member LVGL component does
  not, so it keeps the greedy walk the tool used before: each member is
  visited once and a member first reached inside an open cycle keeps the
  value it had there. That is a lower bound, as `main` already is.

`tasks` scopes an entry. `--verbose` lists every component and its bound.

The memoized walk before plan 314 PR 4c reused a function's depth from the
first time it was reached. A function first reached inside an open cycle had
that cycle's back edge cut, and the depth behind the cut was lost on every
other path. `tests/test_stack_budget.py` keeps that graph: the old walk
reported 440 bytes of frames where the real path is 756.

`infeasible_calls` removes one call edge whose guard cannot pass in this
image, cited by file and line. An edge without a citable guard stays. The
entries today:

| Edge | Guard |
| :--- | :--- |
| `esp_cache_get_alignment` to `esp_log` | logs only for a null output pointer; every linked caller passes an address |
| copy-on-write `std::string` `_S_create` to `__throw_length_error` | the limit is about 1 GiB; the chips address under 32 MiB |
| copy-on-write `std::string` `_S_construct` to `__throw_logic_error` | only for a null message; libstdc++ passes literals |
| `cache_hal_get_cache_line_size` to `abort` | `HAL_ASSERT` on the cache level; every caller passes a valid level |
| `rtc_clk_xtal_freq_get` to `esp_log` | the bootloader stores the crystal frequency before the app runs |

The first three replace depth 1 bounds the walk used to apply by cutting
whichever back edge it met first.

GCC marks a frame with an `alloca` or a VLA as `dynamic` and reports only its
static part. A task that reaches one fails unless `dynamic_bounds` adds a
cited maximum. A `dynamic,bounded` frame already carries GCC's maximum and
needs no entry.

The bounds reached today:

| Cycle or frame | Bound | Why |
| :--- | ---: | :--- |
| `esp_log` and `malloc` | 1 pass | malloc reaches `esp_log` only through a null check that its caller never fails; the `infeasible_calls` edge removes the cycle where it is linked |
| libstdc++ exception constructors, `__cxa_begin_catch` | 1 pass | exceptions are off; a literal message never throws again |
| `std::map<std::string, uint8_t>::_M_erase` | 21 passes | the camera id map; a 24 KiB NVS index holds fewer than 1,171 entries |
| GPS `std::sort` introsort | 1 pass | five samples, below the 16-element threshold |
| `netconn_drain`, `netconn_free` | 2 passes | only a listening netconn has an accept mailbox |
| `lwip_send`, `lwip_sendto` | 1 pass | the socket type picks one forward |
| `mbedtls_mpi_mul_mpi` on `webui_tls` | 1 pass | P-256 products never exceed the 4,096-bit hardware limit |
| `esp_aes_process_dma` | 2 passes | the copy it makes is DMA capable |
| cJSON `parse_value`, `cJSON_Delete` | 9 passes | `CJSON_NESTING_LIMIT=8` in `platformio.ini` |
| cJSON `print_value` | 6 passes | the deepest JSON furble builds, the Home Assistant discovery |
| every cycle on `main` | known limit | the LVGL rules join every UI callback into cycles; see below |
| `s_i2c_start_end_command` | 32 bytes | two VLAs of at most two address bytes |
| `m5::Log_Class::output` | 256 bytes | M5Unified's own fixed log lines |

The cJSON limit is a firmware change that this gate found. The ESP-IDF
default of 1,000 levels let a 2 KiB MQTT location payload of nested arrays
recurse 64 bytes per level, about 64 KB, on the 10 KiB MQTT owner stack,
and a 1 KiB WebUI body the same on the 8 KiB HTTPS task.
Every furble request body and MQTT payload is a flat object, so the limit of
8 rejects nothing furble accepts. PlatformIO passes the define to the ESP-IDF
json component too: the Core2 `parse_value` compares the depth with
`bltui a9, 8`. The host simulator links its own cJSON and
keeps the library default.

## Current results

Resolved worst case in bytes, port overhead included, on the 2026-11-13
StickS3 and Waveshare builds of this branch, before and after plan 314 PR 4c.
"Before" is the memoized walk without the panic handler; "after" is the
component walk with the panic handler and the `infeasible_calls` edges. The
StickC, StickC Plus, Core, Core2 and headless rows come from the firmware CI
run on the PR, which runs the gate after every build; see plan 314 PR 4c.

| Task | Stack | StickS3 before | StickS3 after | Waveshare before | Waveshare after |
| :--- | ---: | ---: | ---: | ---: | ---: |
| panic handler | | | 512 | | 752 |
| main | 12,288/16,384 | 6,928 | 7,408 | 3,920 | 4,640 |
| nimble_host | 16,384 | 1,664 | 2,144 | 1,856 | 2,608 |
| sys_evt | 3,072 (was 2,304) | 1,952 | 2,432 | 2,192 | 2,944 |
| esp_timer | 3,584 | 1,920 | 2,704 | 2,128 | 3,504 |
| control | 8,192 | 2,896 | 2,816 | 3,120 | 3,648 |
| camera_target | 4,096 | 2,560 | 2,384 | 2,784 | 3,248 |
| gps | 4,096 | 2,992 | 3,392 | 3,232 | 3,984 |
| wifi | 4,096 | 2,592 | 3,072 | 3,120 | 3,872 |
| companion | 3,840 (was 3,072) | 2,560 | 2,912 | 2,960 | 3,712 |
| ir | 4,096 | 2,352 | 2,800 | | |
| mqtt | 10,240 | 8,080 | 8,256 | 8,304 | 9,056 |
| mqtt_client | 3,072 | 1,936 | 2,352 | 2,208 | 2,896 |
| webui | 4,096 (was 3,584) | 2,912 | 3,152 | 3,136 | 3,952 |
| webui_https | 8,192 | 2,640 | 2,848 | 2,864 | 3,312 |
| webui_tls | 12,288 | 7,664 | 8,144 | 7,920 | 8,672 |

The main row has two stacks: 12,288 on the StickS3, 16,384 elsewhere.
`mqtt_client` is checked against the plain 3 KiB worker; TLS uses 6 KiB.
Most rows grow because every path to `abort` or an assert now pays the panic
handler (512 bytes on the StickS3, 752 on Waveshare, whose `__assert_func`
keeps the file and line). The common tail is the first log call creating its
mutex, which allocates, and FreeRTOS's `configASSERT` in `vPortExitCritical`
on the way out of the heap lock. `control` and `camera_target` shrink on the
StickS3: the old walk followed the copy-on-write string's length throw, which
`infeasible_calls` now removes with its citation.

Three stacks grew to their measured maximum across envs, rounded up to
256 bytes: the default event loop task (`sys_evt`) from 2,304 to 3,072 in all
seven release sdkconfigs, the companion task from 3,072 to 3,840, and the
WebUI supervisor from 3,584 to 4,096. Each is one task per board, so the
internal RAM cost is 768 bytes on every board for `sys_evt`, 768 more where
the companion is enabled, and 512 more on the four MQTT and WebUI boards
(Core2, StickS3, Waveshare, headless) while the WebUI runs.

Replaying the 6 KiB MQTT owner that overflowed on hardware fails without any
scripted constant: `--stack mqtt=6144` on the StickS3 reports 7,840 of 6,144.
Its deepest path runs through `MQTT::taskStep` and
`MQTT::processLifecycleEvents`, 2,400 bytes each, because both hold a
`queued_event_t` copy with the full topic and payload buffers.

The `sys_evt` row is the ESP-IDF default event loop task
(`CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE`, 3,072 bytes), which runs the WiFi
event handler. Before #424 that handler saved the access point to NVS and
started SNTP there, and the walk reached 2,864 of 2,304 bytes on Waveshare and
headless. The handler now queues that work for the 4 KiB WiFi worker. A
`tasks`-scoped indirect rule keeps each event loop to the handlers registered
on it: the esp-mqtt loop dispatches only `MQTT::eventHandler`.

The `boot_only` list keeps functions that run once on main before any task
exists out of every other task's walk. `M5Unified::begin` is the one entry:
without it the headless companion task reported 3,264 of 3,072 through a
first-call `Platform::getInstance()` that cannot happen after boot.

## Panic paths

A panic path counts. `abort()`, `__assert_func` and `esp_system_abort()` end
in `panic_abort` (`esp_system/panic.c`), which executes `ill`. The illegal
instruction exception enters `_xt_user_exc` in `xtensa/xtensa_vectors.S`,
which says "we'll stay on the user stack for exception handling". It pushes
the `XT_STK_FRMSZ` exception frame on the faulting task's stack and calls the
`_xt_exception_table` entry, which `xtensa/xtensa_intr.c` sets to
`xt_unhandled_exception` for every exception without a registered handler.
That calls `panic_handler` and `esp_panic_handler` on the same stack. The
release builds print and reboot (`CONFIG_ESP_SYSTEM_PANIC_PRINT_REBOOT`) with
no core dump. The Core2, StickS3 and Waveshare debug builds write a core
dump to flash. Those boards allow task stacks in PSRAM
(`CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM`), so
`CONFIG_ESP_COREDUMP_USE_STACK_SIZE` gives the dump its own 1,792-byte stack
(`espcoredump/Kconfig`), and only the handler before it runs on the task.

An overflow during the panic corrupts whatever lies below the task stack and
loses the panic output, so the handler's depth belongs in every task's
budget. The manifest's `panic` entry names `panic_abort` and the handler. The
walk measures the handler once, from `xt_unhandled_exception`, and adds it as
one node that `panic_abort` calls. A nested panic inside the handler is not
followed: `esp_panic_handler_increment_entry_count` restarts after
`PANIC_ENTRY_COUNT_MAX` entries, and a handler that faults has already lost
the first panic's output. The exception frame needs no extra charge: the
port overhead already reserves `XT_STK_FRMSZ`, and the handler runs with
interrupts masked below the debug level, so no interrupt frame stacks on
top.

## Known limits

- `main`'s 410-member LVGL component is too large to enumerate, so it keeps
  the greedy walk and its row is a lower bound. Every other task is exact
  outside its recursion bounds.
- The walk is path-insensitive. It follows every call in a function whether
  or not one call's condition excludes another, so most rows now end in an
  error tail (a lazy log mutex, an assert on the way out of a lock) that
  needs two unlikely conditions at once. That over-approximates, which is the
  safe direction; `infeasible_calls` removes only an edge whose guard is
  cited.
- Only a panic through `abort` is modelled. A fault in an ISR or a double
  exception enters `panicHandler` from the vector code at an arbitrary depth;
  the port overhead's interrupt frame is all the walk reserves for it.
- `main` and the NimBLE host run most of their depth through unresolved
  indirect calls (LVGL widget code, M5GFX virtuals, NimBLE callbacks into
  vendor classes). Their numbers are not a proof of fit. The 8 KiB StickS3
  main stack overflowed on hardware with MQTT enabled while the resolved walk
  stays well below that, so the hardware high-water mark remains the evidence
  for those two tasks.
- The debug-only `console` and `bt-explore` tasks are not in the manifest.
- `main` records every cycle as a known limit. The signature rules let any
  `(lv_event_t*)` callback run under `lv_event_send`, so the walk joins the UI
  callbacks, `lv_obj_invalidate` and the refresh path into cycles the firmware
  cannot take as written. The real recursion is LVGL's widget tree walk,
  bounded by the page depth furble builds. A new recursion on any other task
  still fails.
- The `esp_timer` task resolves its callbacks to the six furble timers and the
  linked ESP-IDF ones (NimBLE callouts, Ethernet link check, PHY PLL tracker,
  Bluetooth alarms). The WiFi and coexistence blobs arm `ets_timer` callbacks
  that run on the same task, and the image does not name them, so they are
  not counted.
- Each `.ci` frame is the function's `entry` allocation, which includes the
  register window save areas the windowed ABI reserves, so window overflow
  spills need no extra margin.
