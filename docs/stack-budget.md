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

A call back into a function already on the walked path closes a cycle. The
walk cuts it there, so it follows the deepest path that visits each function
once. A task whose path holds a cycle fails unless a member matches an entry
in the manifest's `recursion_bounds`. An entry's `depth` is the number of
passes the cycle can make, cited from source; the walk adds `depth - 1` more
passes of the whole cycle's frames once, at the member where the walk entered
the cycle, so every path through the cycle pays exactly `depth` passes.
Cycles that share an entry member add their passes, because one can nest
inside the other. An entry without `depth` records a documented known
limit and adds nothing; `tasks` scopes an entry. `--verbose` lists every
cycle and its bound.

GCC marks a frame with an `alloca` or a VLA as `dynamic` and reports only its
static part. A task that reaches one fails unless `dynamic_bounds` adds a
cited maximum. A `dynamic,bounded` frame already carries GCC's maximum and
needs no entry.

The bounds reached today:

| Cycle or frame | Bound | Why |
| :--- | ---: | :--- |
| `esp_log` and `malloc` | 1 pass | malloc reaches `esp_log` only through a null check that its caller never fails |
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

Resolved worst case in bytes, port overhead included, on the 2026-11-07
builds of `fix/stack-budget-review`. Every task passes. The main row has two
stacks: 12,288 on the StickS3, 16,384 elsewhere. `mqtt_client` is checked
against the plain 3 KiB worker; TLS uses 6 KiB.

| Task | Stack | StickC | StickC Plus | Core | Core2 | StickS3 | Waveshare | Headless |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| main | 12,288/16,384 | 6,816 | 6,816 | 6,816 | 6,816 | 6,928 | 3,744 | 3,744 |
| nimble_host | 16,384 | 1,104 | 1,104 | 1,104 | 1,104 | 1,328 | 1,504 | 1,504 |
| sys_evt | 2,304 | 1,632 | 1,632 | 1,632 | 1,632 | 1,856 | 2,096 | 2,096 |
| esp_timer | 3,584 | 1,696 | 1,696 | 1,696 | 1,696 | 1,920 | 2,144 | 2,144 |
| control | 8,192 | 1,968 | 1,968 | 1,968 | 1,968 | 2,192 | 2,720 | 3,168 |
| camera_target | 4,096 | 1,872 | 1,872 | 1,872 | 1,872 | 2,096 | 2,320 | 3,168 |
| gps | 4,096 | 2,352 | 2,352 | 2,352 | 2,352 | 2,832 | 3,056 | 3,056 |
| wifi | 4,096 | 2,208 | 2,208 | 2,208 | 2,208 | 2,432 | 2,944 | 2,944 |
| companion | 3,072 | 2,208 | 2,208 | 2,208 | 2,208 | 2,272 | 2,784 | 2,784 |
| ir | 4,096 | 1,840 | 1,840 | | | 2,160 | | |
| sd | 6,144 | | | 3,456 | 3,456 | | | |
| mqtt | 10,240 | | | | 7,680 | 7,904 | 8,128 | 8,128 |
| mqtt_client | 3,072 | | | | 1,776 | 2,000 | 2,256 | 2,256 |
| webui | 3,072 | | | | 2,528 | 2,752 | 2,992 | 2,992 |
| webui_https | 8,192 | | | | 2,192 | 2,480 | 2,720 | 2,720 |
| webui_tls | 12,288 | | | | 6,960 | 7,488 | 7,744 | 7,744 |

Against the #414 results, `webui_https` grows by 128 bytes on every board
that has it: the `netconn_drain` and `netconn_free` cycle carries its bounded
second pass, charged once. `mqtt` grows by 48 to 64 bytes through the nine bounded
`parse_value` passes. The Core `gps` row reads 2,352 where #414 reported
2,560; the #414 tool reports the same 2,352 on this build, so the change came
from master between the two. The `esp_timer` row is new.

Replaying the 6 KiB MQTT owner that overflowed on hardware fails without any
scripted constant: `--stack mqtt=6144` on the StickS3 reports 7,840 of 6,144.
Its deepest path runs through `MQTT::taskStep` and
`MQTT::processLifecycleEvents`, 2,400 bytes each, because both hold a
`queued_event_t` copy with the full topic and payload buffers.

The `sys_evt` row is the ESP-IDF default event loop task
(`CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE`, 2,304 bytes), which runs the WiFi
event handler. Before #424 that handler saved the access point to NVS and
started SNTP there, and the walk reached 2,864 of 2,304 bytes on Waveshare and
headless. The handler now queues that work for the 4 KiB WiFi worker. A
`tasks`-scoped indirect rule keeps each event loop to the handlers registered
on it: the esp-mqtt loop dispatches only `MQTT::eventHandler`.

The `boot_only` list keeps functions that run once on main before any task
exists out of every other task's walk. `M5Unified::begin` is the one entry:
without it the headless companion task reported 3,264 of 3,072 through a
first-call `Platform::getInstance()` that cannot happen after boot.

## Known limits

- The walk memoizes each function once. A function first reached inside an
  open cycle has that cycle's back edge cut, and its value is reused on other
  paths, so depth behind the cut can be missed. An exact re-walk is
  exponential on `main`'s LVGL cycles; with `main` left on the fast walk it
  runs in seconds but raises most tasks by roughly 100 to 1,000 bytes, and on
  the StickS3 it puts `sys_evt` (2,432 of 2,304) and `webui` (3,296 of 3,072)
  over budget through paths that end in `abort` and `__assert_func`. A
  follow-up PR settles how panic paths count and replaces the walk.
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
