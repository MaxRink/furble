# 305 - General internal RAM pass

> Plan 308 continues this work after the hardware measurement came in at
> 27,351 free / 25,588 largest, not the 28,103 inferred here. It reduces the
> main stack to 8 KiB, uses one draw buffer on narrow panels and reduces the
> companion stack. The measured and inferred map below remains the #394
> baseline.

## Finding

PR #392 fixed the WebUI path on the StickS3: panel-sized LVGL draw buffers,
six WiFi static RX buffers, a 3 KiB supervisor and an 8 KiB HTTPS task. It did
not audit the other steady internal-RAM consumers. This pass does that audit and
implements the safe reductions that keep every feature.

The measured baseline is the StickS3 debug image with WiFi connected, the
companion service enabled, the display active, `web_ui=false` and `mqtt=false`:

```text
heap.internal.free: 13543
heap.internal.largest_block: 12788
```

With the 3 KiB WebUI supervisor running the same image reports 10,607 free and
9,716 largest. The HTTPS server gate refuses to start below 20,480 / 12,288.
Before #392 the same image reported 7,731 / 7,156.

The build reports 48,600 bytes of static DRAM:

```text
.dram0.data   19,688
.dram0.bss    28,912
RAM: 14.8% (used 48600 bytes from 327680 bytes)
```

`CONFIG_ESP_MAIN_TASK_STACK_SIZE` is 16,384 on every board in this tree. The
StickS3 main task measured a 7,020-byte high-water mark with `web_ui=false`.
The stack was sized for the TLS identity generation that runs from `initEarly()`
when the WebUI is enabled, not for the UI loop.

## Internal RAM map

Measured means read from this firmware or from the hardware console. Inferred
means configuration or allocation arithmetic. The heap free value already
includes the 32 KiB `CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL` pool, so the pool is
not another 32 KiB of free internal RAM.

### Task stacks

All task stacks are allocated from internal RAM by FreeRTOS
(`components/freertos/heap_idf.c`, `portFREERTOS_HEAP_CAPS`), except the IR
task, which this pass moves to PSRAM. Sizes below are the configured stack
bytes, excluding the task control block.

| Task | Configured stack | High-water | Evidence |
| :--- | ---: | ---: | :--- |
| `main` | 16,384, now 12,288 | 7,020 | `CONFIG_ESP_MAIN_TASK_STACK_SIZE`; `perf tasks` |
| `control` | 8,192 | 7,372 | `src/main.cpp:505`; `perf tasks` |
| `nimble_host` | 16,384 | 14,200 | `CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE`; `perf tasks` |
| `wifi` | 4,096 | 3,516 | `src/FurbleWiFi.cpp:61`; `perf tasks` |
| `esp_timer` | 3,584 | 3,068 | `CONFIG_ESP_TIMER_TASK_STACK_SIZE`; `perf tasks` |
| `ir` | 4,096 | 3,420 | `src/FurbleIR.cpp`; `perf tasks` |
| `companion` | 4,096 | 2,012 | `src/FurbleCompanion.cpp:207`; `perf tasks` |
| `console` | 8,192, now 6,144 | 5,504 | `src/FurbleConsole.cpp`; `perf tasks` |
| `btController` | about 2,048 | 2,028 | controller library, inferred |
| `IDLE` (two cores) | 1,536 each | 788 | `CONFIG_FREERTOS_IDLE_TASK_STACKSIZE`; `perf tasks` |
| `tcpip` | 3,072 | not measured | `CONFIG_LWIP_TCPIP_TASK_STACK_SIZE` |
| `sys_evt` | 2,304 | not measured | `CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE` |
| per-camera target | 4,096 each | not measured | `src/FurbleControl.cpp:1240` |
| MQTT owner | 6,144, 4,096 in #393 | under 1,024 in #393 | `src/FurbleMQTT.cpp:229` |
| esp-mqtt worker | 6,144, 4,096 plain in #393 | not measured | esp-mqtt, #393 |
| WebUI supervisor | 3,072 | 512 | `include/FurbleWebUIMemory.h`; plan 304 |
| HTTPS worker | 8,192 | not measured | `include/FurbleWebUIMemory.h` |
| TLS identity | 12,288, transient | not measured | `include/FurbleWebUI.h` |
| GPS worker | 4,096 | not measured | `src/FurbleGPS.cpp:185` |
| SD writer | 6,144 | not measured | `src/FurbleSD.cpp:40`, Core boards only |

The GPS worker writes the fix cache and ephemeris to NVS, so it must stay on an
internal stack. The companion worker calls `Settings::save` for companion
writes, so it also stays internal. The NimBLE host worker writes bonds through
NVS. Main, console, control, WiFi and `esp_timer` all execute code that can
commit NVS or otherwise disable the flash cache.

### WiFi and lwIP

| Region | Size | Evidence |
| :--- | ---: | :--- |
| WiFi static RX pool | 6 x about 1,600 = 9,600 | `CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=6`; IDF Kconfig says about 1.6 KiB each |
| WiFi static RX management pool | 5 x about 500 = 2,500 | `CONFIG_ESP_WIFI_RX_MGMT_BUF_NUM_DEF=5`; `CONFIG_ESP_WIFI_STATIC_RX_MGMT_BUFFER=y` |
| WiFi dynamic RX pool | up to 32 frames, on demand | `CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM=32` |
| WiFi dynamic TX pool | up to 32 frames, on demand | `CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER_NUM=32` |
| WiFi management short buffers | up to 32, dynamically allocated | `CONFIG_ESP_WIFI_MGMT_SBUF_NUM=32` |
| AMPDU RX/TX BA window | 6 each, per link state | `CONFIG_ESP_WIFI_RX_BA_WIN=6`, `CONFIG_ESP_WIFI_TX_BA_WIN=6` |
| TCP MSS | 1,440 | `CONFIG_LWIP_TCP_MSS=1440` |
| TCP receive window | 5,760 per connection, pbufs on demand | `CONFIG_LWIP_TCP_WND_DEFAULT=5760` |
| TCP send buffer | 5,760 per connection, pbufs on demand | `CONFIG_LWIP_TCP_SND_BUF_DEFAULT=5760` |
| TCP receive mailbox | 6 entries | `CONFIG_LWIP_TCP_RECVMBOX_SIZE=6` |
| TCP accept mailbox | 6 entries | `CONFIG_LWIP_TCP_ACCEPTMBOX_SIZE=6` |
| TCPIP receive mailbox | 32 entries | `CONFIG_LWIP_TCPIP_RECVMBOX_SIZE=32` |
| IP reassembly | up to 10 pbufs, on demand | `CONFIG_LWIP_IP_REASS_MAX_PBUFS=10` |

The static RX pools are the only WiFi allocations that are always resident.
The dynamic pools are caps, not preallocations. The TCP windows are only filled
while a socket has data in flight, so a quiet MQTT link does not hold 5,760
bytes in each direction. Reducing the TCP windows would directly reduce
MQTT/OTA throughput, so this pass leaves them alone.

### NimBLE

`CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL=y` routes NimBLE host allocations to
PSRAM on the StickS3. The checked-in file also contains stale
`CONFIG_NIMBLE_MEM_ALLOC_MODE_INTERNAL=y` symbols from the old component name.
`components/bt/sdkconfig.rename` maps the old names to the `BT_NIMBLE` names, so
the `BT_NIMBLE` values are authoritative.

| Region | Size | Evidence |
| :--- | ---: | :--- |
| Host task stack | 16,384 internal | `CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE=16384` |
| MSYS pool 1 | 12 x 256 = 3,072 PSRAM | `CONFIG_BT_NIMBLE_MSYS_1_BLOCK_COUNT`, `_SIZE` |
| MSYS pool 2 | 24 x 320 = 7,680 PSRAM | `CONFIG_BT_NIMBLE_MSYS_2_BLOCK_COUNT`, `_SIZE` |
| Transport ACL buffers | 24 x 255 = 6,120 PSRAM | `CONFIG_BT_NIMBLE_TRANSPORT_ACL_FROM_LL_COUNT`, `_SIZE` |
| Transport event buffers | 30 x 70 = 2,100 PSRAM | `CONFIG_BT_NIMBLE_TRANSPORT_EVT_COUNT`, `_SIZE` |
| Controller | static internal, not exposed | controller library |
| Max connections / bonds | 9 / 15 | `CONFIG_BT_NIMBLE_MAX_CONNECTIONS`, `_MAX_BONDS` |

The host stack is 87 percent used at 14,200 bytes. It cannot shrink. The host
heap is already external, so the remaining internal NimBLE cost is the host
stack, the controller, and the driver structures.

### LVGL

| Region | Size | Evidence |
| :--- | ---: | :--- |
| Draw buffers before | 2 x 4,320 = 8,640 internal DMA | `include/FurbleUIMemory.h`, `src/FurbleUI.cpp` |
| Draw buffers after | 2 x 2,160 = 4,320 internal DMA | same |
| Image cache | 262,144 PSRAM, lazy | `CONFIG_LV_CACHE_DEF_SIZE=262144` |
| Layer buffer | 24,576 PSRAM, on demand | `CONFIG_LV_DRAW_LAYER_SIMPLE_BUF_SIZE=24576` |
| Small objects and styles | below the 4,096 threshold, internal, unquantified | `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096` |
| Colour depth | 16-bit RGB565 | `CONFIG_LV_COLOR_DEPTH=16` |

The draw buffers must stay in `MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL` because
M5GFX hands them to the SPI DMA engine. PSRAM is not a safe home for them.
The large LVGL buffers are already in PSRAM because they exceed the 4,096-byte
`malloc` threshold. The many small LVGL objects stay internal by design. That
is the inferred reason the remaining internal heap is fragmented and hard to
reclaim.

### mbedTLS, HTTP and JSON

On the StickS3 `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y` and the TLS content
buffers are in PSRAM (`CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN=16384`,
`CONFIG_MBEDTLS_SSL_OUT_CONTENT_LEN=4096`). The Core2 has no PSRAM enabled, so
its TLS buffers are internal. The headless and Waveshare S3 images had TLS
allocations in internal RAM until this pass.

The WebUI per-connection budget from plan 304 is still the reference:
`httpd_data` 776, two client socket slots 384, `httpd_config` 96, seven
response headers 56, one request scratch buffer 1,024, one `esp_tls_t` 2,184,
the streamed settings JSON about 2,000, and lwIP pbufs about 3,000. The server
gate requires 20,480 free and a 12,288 largest block before it allocates any of
it.

The WebUI settings response is streamed one setting at a time. The MQTT
discovery tree still uses cJSON nodes, which are small internal allocations,
and the final text is one buffer that goes to PSRAM when it exceeds 4,096
bytes. This was not changed because MQTT is owned by the open stack PR.

### Companion, console, NVS and logging

The companion authentication state is fixed size: a 64-byte password buffer, a
16-byte nonce and a 32-byte digest. The service uses small `std::vector`
payloads and has no large fixed buffers. The companion worker is 2,012 bytes
used of 4,096.

The console task carries a 256-byte line buffer and `esp_console` command
arguments. Its deepest command is `perf tasks`, which used two 24-entry
`TaskStatus_t` snapshots on the stack before this pass. The USB Serial/JTAG
driver and the 115200 UART console use small fixed buffers. The console is
debug only.

NVS uses 4 KiB page buffers while a commit is in progress. Those are transient
and are one reason a task that writes NVS must keep its stack in internal RAM.
The log path has no large ring buffer in the release image.

## Changes

| Change | Before | After | Saving | Safety | Evidence | Implemented |
| :--- | ---: | ---: | ---: | :--- | :--- | :--- |
| StickS3 main stack | 16,384 | 12,288 | 4,096 | Identity generation moves to the existing 12 KiB internal task. NVS still runs on an internal stack. | 7,020 measured high-water; 12 KiB identity task | Yes |
| LVGL narrow-panel rows | 4,320 per buffer | 2,160 per buffer | 4,320 total on StickS3 | Two buffers stay, both internal DMA. Only the band height changes. | `UIMemory::drawBufferBytes`, measured panel size | Yes |
| IR task stack | 4,096 internal | 4,096 PSRAM | 4,096 internal | IR only drives RMT and logs. It never writes NVS or disables the flash cache. Falls back to internal RAM. | `src/FurbleIR.cpp`, `CONFIG_SPIRAM`, `CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM` | Yes |
| Console stack | 8,192 | 6,144 | 2,048 debug only | The `perf tasks` snapshots move to PSRAM with an internal fallback. The command is diagnostic. | 5,504 measured high-water included about 1.9 KiB of snapshots | Yes |
| mbedTLS on headless and Waveshare | internal | PSRAM | about 20,000 internal, inferred | Same S3 PSRAM path the StickS3 already uses. No DMA. | `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC` | Yes |
| Single-buffer flush fallback | immediate `flush_ready` | waits for SPI DMA | 0 | Prevents LVGL from overwriting a buffer that is still on the wire if the second allocation ever fails. | `src/FurbleUI.cpp` | Yes |

The StickS3 idle saving is 4,096 + 4,320 + 4,096 = 12,512 bytes in a release
image and 14,560 bytes in a debug image. The debug figure includes the 2,048-byte
console saving.

## Rejected or deferred

| Candidate | Current | Possible saving | Why not |
| :--- | ---: | ---: | :--- |
| Single LVGL draw buffer | 2 x 2,160 | 2,160 on StickS3 | `displayFlush()` calls `lv_disp_flush_ready()` as soon as M5GFX queues the SPI DMA. With one buffer LVGL can render the next band over pixels still being shifted out. The two-buffer ping-pong is what makes the early ready safe. |
| Console stack below 6 KiB | 6,144 | 1,024 | The 5,504-byte high-water included the snapshots. The remaining margin is about 2.5 KiB and `esp_console_run` depth is input dependent. |
| Companion stack 4,096 to 3,072 | 4,096 | 1,024 | 2,012 bytes measured, but the worker performs NVS writes and the remaining margin is only 1 KiB. Needs a hardware high-water with a companion write first. |
| Camera target task in PSRAM | 4,096 per camera | 4,096 per active camera | The task never writes NVS, so it is safe in principle, but there is no high-water mark and shutter latency has not been measured. Deferred. |
| MQTT owner task in PSRAM | 4,096 in #393 | 4,096 | The task does not write NVS, but MQTT is owned by the open stack PR. Deferred to a follow-up. |
| WiFi static RX below 6 | 6 x 1.6 KiB | 1.6 KiB per buffer | IDF recommends at least the AMPDU RX BA window of 6. Below that, throughput and compatibility regress. |
| WiFi dynamic RX/TX below 32 | caps | 0 steady | They are caps, not preallocations. |
| Smaller TCP windows | 5,760 | up to 2,880 per socket | Direct MQTT/OTA throughput regression. |
| Smaller NimBLE host stack | 16,384 | 2,184 | 14,200 measured, 87 percent used. No safe reduction. |
| Lower `SPIRAM_MALLOC_RESERVE_INTERNAL` | 32,768 | 0 net | The reported internal free already includes the reserve. Moving bytes between the general heap and the reserve does not create RAM. |
| Lower `SPIRAM_MALLOC_ALWAYSINTERNAL` | 4,096 | unquantified | Driver and LVGL small allocations would start landing in PSRAM. Needs a soak and a latency measurement. |
| `SPIRAM_TRY_ALLOCATE_WIFI_LWIP` | off | about 12 KiB | It forces static WiFi TX buffers and can reduce throughput. Not a safe default without a radio soak. |
| Reduce LVGL image cache | 262,144 PSRAM | 0 internal | It is already in PSRAM. |
| Reduce `CONFIG_LV_DRAW_LAYER_SIMPLE_BUF_SIZE` | 24,576 PSRAM | 0 internal | It is already in PSRAM. |

## MQTT after #392 and #393

The pre-#392 failure was `Mem alloc fail. size 0x00001800 caps 0x00000804` in
`esp_mqtt_client_start` after the 6 KiB Furble owner task had already been
created. #392 leaves 13,543 free and 12,788 largest with MQTT off. #393 reduces
the owner to 4,096 and the plain `mqtt://` esp-mqtt worker to 4,096.

Inferred result with #392 and #393 only:

```text
before MQTT start:            13,543 free, 12,788 largest
owner task 4,096:            -4,096
plain esp-mqtt task 4,096:   -4,096
remaining:                    ~5,351 free, largest at least 4,096
```

That fits, but the remaining internal headroom is small. The cJSON discovery
tree and the per-publish text copies are transient internal allocations on top
of that. This pass adds 12,512 bytes of idle internal RAM on the StickS3, so the
plain MQTT path should have about 17,800 bytes free after both tasks start.
TLS MQTT keeps the 6,144-byte esp-mqtt worker and needs about 2 KiB more, which
also fits in inference. Hardware must still soak discovery, publishes and an
OTA update.

## Expected result

Measured baseline plus the implemented savings, in bytes:

| Build | Measured before | Implemented saving | Inferred after |
| :--- | ---: | ---: | ---: |
| StickS3 debug, WebUI off | 13,543 | 14,560 | 28,103 free |
| StickS3 debug, WebUI supervisor on | 10,607 | 14,560 | 25,167 free |
| StickS3 release, WebUI off | not measured | 12,512 | not measured |
| StickC debug | not measured | 3,648 | not measured |
| StickC Plus debug | not measured | 6,368 | not measured |
| Core, Core2 | not measured | 2,048 debug only | unchanged release |
| Headless, Waveshare | not measured | about 20,000 inferred plus 2,048 debug console | not measured |

The StickS3 release image has no console task, so its free value is at least
8,192 bytes above the debug value before the pass. The release saving is 12,512
bytes. A release after value is not inferred here because the debug image also
carries the USB Serial/JTAG console driver and its buffers.

The largest free block is the part that cannot be inferred from a total. The
StickS3 baseline was 12,788. The main stack and IR task are separate 4 KiB
blocks, and the draw buffers are two 2,160-byte blocks. The largest block should
not shrink, but only `perf heap` on hardware can prove it grew enough for the
HTTPS task.

The StickS3 WebUI result is therefore **inferred, not measured**. With 25,167
free and a largest block above 12,288 the server gate would now pass, and the
server needs about 18.7 KiB including the HTTPS task. That leaves roughly 6 KiB
of internal heap while HTTPS is running, which is thin. Do not call the StickS3
a supported WebUI target until a hardware boot with `web_ui=on`, a TLS
handshake, a settings write and a soak all pass. If it is unstable, the existing
gate still fails closed instead of resetting.

## Validation done

- Firmware build: `FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e m5stick-s3-debug`.
- Host suite: `ctest --test-dir .pio/host-build --output-on-failure`. The
  `ui-memory` case asserts 2,160 bytes for 135x240, 800 for 80x160 and unchanged
  10,240 for 320x240 and 240x135.
- Host console test compiles the heap-backed `perf tasks` snapshots against the
  console shim.
- Pre-review and static analysis are run before the PR.

## Still needs hardware

1. `perf heap` and `perf tasks` on the StickS3 debug image with WebUI off. Confirm
   the free/largest values and the main, IR and console high-water marks.
2. `settings set web_ui on`, reboot, and check whether the server gate passes. If
   it does, load `/api/settings`, save one setting, and soak for at least an
   hour with WiFi and BLE active.
3. Fire the IR trigger after the task has moved to PSRAM. Confirm the RMT
   waveform and that no watchdog or cache assertion appears.
4. MQTT with #393 merged: plain `mqtt://` and `mqtts://`, discovery, a shutter
   command and an OTA update. Watch `heap.internal.free` during discovery.
5. UI smoothness with eight-line bands on the StickS3. A full-screen redraw now
   uses 30 bands instead of 15.
6. Core2, headless and Waveshare: `perf heap` and a WebUI page load. The
   headless and Waveshare TLS buffers are now in PSRAM by configuration.

## Follow-ups

- Camera target task in PSRAM: 4,096 bytes per connected camera. Needs a
  high-water mark and a shutter-latency comparison.
- MQTT owner task in PSRAM: the hardware build needs 10,240 bytes, so this is
  the largest remaining stack candidate.
- Revisit `SPIRAM_TRY_ALLOCATE_WIFI_LWIP` only with an OTA throughput soak.
- Measure LVGL object memory on the StickS3 with `heap_caps_get_info` around
  UI construction. That is the remaining unquantified internal consumer.

## Follow-up: plan 308

[308-mqtt-ha-memory.md](308-mqtt-ha-memory.md) continues from the measured
27,351 / 25,588 pair. It reduces the main stack from 12 KiB to 8 KiB, uses one
LVGL draw buffer on narrow panels, reduces the companion stack to 3 KiB, and
adds the MQTT start and Home Assistant discovery gates. The companion stack cut
listed above is implemented there, not here.
