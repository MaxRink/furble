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

The walk is exact for direct calls. GCC cannot name the target of an indirect
call (a virtual call, a `std::function`, a C callback), so each indirect call
is listed, not followed, unless the manifest's `indirect` section resolves the
caller. The rules there resolve LVGL's dispatchers by callback signature: any
C++ function taking `(lv_event_t*)` may run under `lv_event_send`, and so on
for timers, display flush and input reads. That over-approximates, which is
the safe direction. A task with unresolved indirect sites therefore reports a
lower bound for paths through them, and the gate catches growth in the
resolved part only.

Recursion is cut at the first repeat and reported. Functions with dynamic
frames (`alloca` or VLAs) are reported; their static part is counted.

## Current results

Resolved worst case in bytes, port overhead included, on the 2026-11-07
master builds. Every task passes. The main row has two stacks: 12,288 on the
StickS3, 16,384 elsewhere. `mqtt_client` is checked against the plain 3 KiB
worker; TLS uses 6 KiB.

| Task | Stack | StickC | StickC Plus | Core | Core2 | StickS3 | Waveshare | Headless |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| main | 12,288/16,384 | 6,560 | 6,560 | 6,560 | 6,560 | 6,640 | 3,744 | 3,744 |
| nimble_host | 16,384 | 1,104 | 1,104 | 1,104 | 1,104 | 1,328 | 1,504 | 1,504 |
| control | 8,192 | 1,968 | 1,968 | 1,968 | 1,968 | 2,192 | 2,736 | 3,168 |
| camera_target | 4,096 | 1,536 | 1,536 | 1,536 | 1,536 | 1,888 | 2,224 | 2,832 |
| gps | 4,096 | 2,352 | 2,352 | 2,352 | 2,352 | 2,832 | 3,072 | 3,072 |
| wifi | 4,096 | 1,968 | 1,968 | 1,968 | 1,968 | 2,192 | 2,720 | 2,720 |
| companion | 3,072 | 2,208 | 2,208 | 2,208 | 2,208 | 2,272 | 2,800 | 2,800 |
| ir | 4,096 | 1,840 | 1,840 | | | 2,160 | | |
| sd | 6,144 | | | 3,456 | 3,456 | | | |
| mqtt | 10,240 | | | | 7,616 | 7,840 | 8,080 | 8,080 |
| mqtt_client | 3,072 | | | | 1,568 | 1,792 | 1,936 | 1,936 |
| webui | 3,072 | | | | 2,528 | 2,752 | 2,992 | 2,992 |
| webui_https | 8,192 | | | | 2,064 | 2,288 | 2,528 | 2,528 |
| webui_tls | 12,288 | | | | 6,960 | 7,488 | 7,744 | 7,744 |

Replaying the 6 KiB MQTT owner that overflowed on hardware fails without any
scripted constant: `--stack mqtt=6144` on the StickS3 reports 7,840 of 6,144.
Its deepest path runs through `MQTT::taskStep` and
`MQTT::processLifecycleEvents`, 2,400 bytes each, because both hold a
`queued_event_t` copy with the full topic and payload buffers.

The `boot_only` list keeps functions that run once on main before any task
exists out of every other task's walk. `M5Unified::begin` is the one entry:
without it the headless companion task reported 3,264 of 3,072 through a
first-call `Platform::getInstance()` that cannot happen after boot.

## Known limits

- `main` and the NimBLE host run most of their depth through unresolved
  indirect calls (LVGL widget code, M5GFX virtuals, NimBLE callbacks into
  vendor classes). Their numbers are not a proof of fit. The 8 KiB StickS3
  main stack overflowed on hardware with MQTT enabled while the resolved walk
  stays well below that, so the hardware high-water mark remains the evidence
  for those two tasks.
- The debug-only `console` and `bt-explore` tasks are not in the manifest.
- Each `.ci` frame is the function's `entry` allocation, which includes the
  register window save areas the windowed ABI reserves, so window overflow
  spills need no extra margin.
