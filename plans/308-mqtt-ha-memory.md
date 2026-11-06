# 307 - MQTT and Home Assistant discovery memory

## Finding

The general internal-RAM pass in #394 left the StickS3 with 27,351 free and
25,588 largest internal bytes with WiFi, BLE, the companion service and the
display active. Plain MQTT connected to a broker then dropped the pool to
1,739 free and 1,076 largest. Enabling `mqtt_ha` crashed and rebooted the
device while the Home Assistant discovery records were being built. The USB
CDC endpoint wedged, so no crash record was retrievable.

The MQTT owner needs 10 KiB. A 4 KiB and a 6 KiB owner stack both overflowed
during start and the first connected event. The measured owner high-water on
the working 10 KiB build was 3,084 bytes. The task is not reduced in this pass.

Measured MQTT task high-water marks from the StickS3:

```text
mqtt owner       10,240 stack, 3,084 used
mqtt_task         4,096 stack,   704 used
main              8,192 stack, 3,020 used
companion         4,096 stack, 1,884 used
control           8,192 stack, 7,468 used
```

## Changes

| Change | Saving | Safety | Evidence |
| :--- | ---: | :--- | :--- |
| Main stack 12 KiB to 8 KiB | 4,096 | 3,020 measured high-water. The WebUI identity still runs on its own 12 KiB internal task and the main task only waits. | `CONFIG_ESP_MAIN_TASK_STACK_SIZE`, `perf tasks` |
| One LVGL draw buffer on narrow panels | 2,160 on StickS3, 800 on StickC | The single-buffer flush path already waits for the SPI DMA before `lv_disp_flush_ready()`. Render and transfer are serialised per band. | `UIMemory::singleDrawBuffer`, `displayFlush()` |
| Companion task 4 KiB to 3 KiB | 1,024 | 1,884 measured. The task commits NVS, so the stack stays internal. | `src/FurbleCompanion.cpp` |
| Plain MQTT worker 4 KiB to 3 KiB | 1,024 | 704 measured. TLS keeps the 6 KiB worker for the handshake. | `MQTTMemory::PLAIN_WORKER_STACK_BYTES`, `config.task.stack_size` |
| MQTT network and outbox buffers 2,048 to 1,536 | 1,024 | The 3,005-byte hub payload fragments through the buffer and the outbox. Incoming commands are far below 1,536 bytes. | `config.buffer.size`, `config.buffer.out_size`, host payload test |
| Cache `MQTT_HA` in `reloadSetting()` | removes NVS reads and stack from the connected and HA status paths | The owner reads the cached atomic. A settings change takes effect on the next reload. | `m_HomeAssistantCached`, host load counter |
| Build Home Assistant discovery without a JSON round trip | about 2,000 to 4,000 peak, inferred | The component tree is attached to the root directly instead of being serialised and parsed again. The wire JSON is unchanged and the host test parses it. | `publishDiscoveryDevice(cJSON *)` |
| Start gate before `esp_mqtt_client_init()` | fail closed instead of allocating into a low pool | Requires 8,192 free and 4,096 largest internal bytes for a plain URI. A secure `mqtts://` URI requires 11,264 free and 7,168 largest because its worker is 6 KiB, not 3 KiB. Below the matching floor the owner counts a failed attempt and the existing retry limit stops a hopeless start. | `MQTTMemory::startGate(free, largest, secure)` |
| Discovery gate before the HA tree | fail closed instead of crashing | Requires 10,240 free and 4,096 largest internal bytes. Below that only the optional retained discovery records are skipped. The broker link stays up. | `MQTTMemory::discoveryGate` |

The steady internal saving with MQTT connected, against the measured build that
already has the 10 KiB owner stack, is:

```text
main stack            4,096
one LVGL buffer       2,160
companion stack       1,024
plain MQTT worker     1,024
MQTT buffers          1,024
                    -------
                      9,328 bytes
```

## Inferred result

Measured before, MQTT connected and HA off: 1,739 free / 1,076 largest.

```text
after: 1,739 + 9,328 = 11,067 free
largest: 1,076 plus the freed 4 KiB main block, 2,160-byte LVGL block,
         1 KiB companion block, 1 KiB worker block and 1 KiB buffer blocks
```

The largest block is the remaining unknown. The freed main stack is one 4 KiB
block, so the largest is expected to be at least 4,096 bytes. That is exactly
the discovery gate's largest floor. If the allocator does not coalesce the
freed regions, HA discovery is skipped and the MQTT command path stays alive.
The hardware run must read the actual pair.

With MQTT off the idle pool rises by 4,096 + 2,160 + 1,024 = 7,280 bytes, from
27,351 to an inferred 34,631 free.

The Home Assistant hub discovery payload is 3,005 bytes and a camera payload
is 1,874 bytes. The 1,536-byte network buffer does not cap them. esp-mqtt sends
the first fragment from the network buffer and copies the remainder to the
outbox, so the total payload still fits. The host test parses both retained
payloads and asserts their structure and retention.

The single draw buffer serialises rendering and SPI transfer for each band. On
the StickS3 a band is 2,160 bytes, about 0.216 ms at 80 MHz SPI, and a full
screen uses 30 bands. A full-screen page change pays about 6.5 ms more than the
double-buffered path. The UI does not animate full-screen content, so this is
accepted for the 2,160-byte internal saving.

## Gate thresholds

| Gate | Free floor | Largest floor | Why |
| :--- | ---: | ---: | :--- |
| MQTT start (plain) | 8,192 | 4,096 | 3 KiB worker, two 1.5 KiB buffers, esp-mqtt structures and lwIP pbufs |
| MQTT start (TLS) | 11,264 | 7,168 | 6 KiB TLS worker plus the same buffers and overhead |
| HA discovery | 10,240 | 4,096 | Hub and camera cJSON trees, the final payload string and the retained enqueue |

The measured pre-gate pool was 1,739 / 1,076. Starting esp-mqtt there is what
turned a low-memory condition into a panic. Both gates log the measured pair
and the floors, and neither takes the broker link down.

## Rejected cuts

| Candidate | Possible saving | Why not |
| :--- | ---: | :--- |
| MQTT owner below 10 KiB | 1,024 to 4,096 | 4 KiB and 6 KiB both overflowed on hardware. The working build uses 10 KiB. |
| TLS worker below 6 KiB | up to 3,072 | The TLS handshake needs the larger stack. Only the plain worker is reduced. |
| MQTT buffers below 1,536 | 512 to 1,024 | The inbound command path and the outgoing fragment split need headroom. 1,536 is the measured compromise. |
| Two LVGL buffers on narrow panels | 0 | The second buffer costs 2,160 internal bytes. The single-buffer path is already DMA-safe. |
| Move the companion task to PSRAM | 3,072 | It commits NVS, so the flash cache can be disabled while it runs. The stack stays internal. |
| Move the MQTT owner to PSRAM | 10,240 | It handles settings and discovery and is the largest stack. Not attempted without a hardware high-water on the PSRAM path. |
| Reduce the HA discovery payload | about 3,005 | The entity set is the feature. The gate handles the memory limit instead. |
| Keep `MQTT_HA` uncached | 0 | The owner would keep reading NVS on the connected and HA status paths, adding stack and flash work at the worst time. |

## Coordination with the MQTT stack PR

The open `fix/mqtt-task-stack` branch also changes the owner stack and the
plain worker stack. This pass agrees on the 10 KiB owner. The gate commit
introduces the worker and buffer configuration with the same 4 KiB worker and
2,048-byte buffers as the open branch. The follow-up commit
`perf(mqtt): size the plain worker and buffers for the StickS3` changes the
shared header constants and host assertions to 3 KiB and 1,536 bytes. Merge
order: land `fix/mqtt-task-stack` first, then rebase this branch. Expect a
small conflict on the configuration lines, resolved in favour of this branch's
3 KiB and 1,536-byte values.

## Validation done

- Host `mqtt-owner` test: stack and buffer constants, start gate, discovery
  gate, cached `MQTT_HA` load counter, retained discovery payloads.
- Host `ui-memory` test: narrow-panel single-buffer policy.
- Simulator memory scenarios with `FURBLE_SIM_MQTT=1`:
  `memory-mqtt-task-alloc-fail`, `memory-mqtt-unreachable`,
  `memory-mqtt-low-heap-retry-limit`, `memory-boot-combination`.
- Firmware builds: `m5stick-s3-debug`, `m5stick-s3`, `m5stack-core2`,
  `esp32-s3-headless`.
- Pre-review and static analysis before the PR.

## Still needs hardware

1. `perf heap` on the StickS3 with plain MQTT connected. Confirm the free and
   largest values and whether the discovery gate opens.
2. Enable `mqtt_ha` and confirm the hub and camera retained records appear in
   Home Assistant without a reset. Read `heap.internal.free` and
   `heap.internal.largest` immediately before and after discovery.
3. Confirm the 3 KiB companion stack with a companion settings write.
4. Confirm the 8 KiB main stack with the WebUI enabled and the identity task
   running.
5. Confirm UI smoothness on the StickS3 and StickC with the single buffer.
6. Re-run the MQTT OTA path to confirm throughput did not regress with the
   1,536-byte buffers.
