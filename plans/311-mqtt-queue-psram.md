# 311 - MQTT event queue, owner gate and main stack

## Finding

Hardware validation of the merged #398 candidate found a second MQTT memory
bug and a main-stack regression on the StickS3.

With WiFi, the companion service and the display active and `mqtt=false`, the
debug image measured 30,007 free and 28,660 largest internal bytes. Enabling
`mqtt=true` dropped the pool to 5,199 free and 4,596 largest, and after about
11 seconds the device panicked:

```text
***ERROR*** A stack overflow in task main
```

The start gate logged the measured pair and refused the client, but the damage
was already done before the gate:

- `MQTT::queued_event_t` is about 2.3 KiB. `xQueueCreate(4, sizeof(...))` in
  every `taskStep()` allocated about 9.2 KiB of internal RAM before the client
  could start.
- `MQTT::init()` called `CameraList::savedSnapshot()` early, loading and
  caching the saved camera list (about 5 KiB) even when MQTT could not start.
- `MQTT::reloadSetting()` created the 10 KiB owner task before the start gate.
- #398 reduced the main stack to 8 KiB. The earlier 3,476-byte high-water did
  not include the later MQTT/UI path, which overflowed the 8 KiB stack.

## Changes

| Change | Effect | Safety |
| :--- | :--- | :--- |
| Move the 9,216-byte event queue to PSRAM | removes 9.2 KiB from the internal pool | The queue holds copied topic/payload bytes and client handles, never flash or NVS pointers. The owner only touches it with the cache enabled. Internal heap fallback when PSRAM allocation is unavailable. |
| Create the queue in `startClient()` after the start gate | a refused start no longer pays 9.2 KiB | The queue is not created in every `taskStep()`. `enqueueEvent()` drops events when the queue is absent. |
| Remove the early `CameraList::savedSnapshot()` | removes about 5 KiB from the MQTT-init path | `publishCameras()` and `publishDiscovery()` already load the saved list on demand. Camera behavior is unchanged. |
| Revert the main stack to 12 KiB | restores the safe main stack | The 8 KiB experiment overflowed on hardware. The WebUI TLS identity still runs on its own 12 KiB internal task. |
| Gate owner creation at 18,432 free / 12,288 largest | a low pool no longer creates a 10 KiB owner that cannot start | The owner gate is the start floor plus the owner stack. Below it the service logs the pair and stays off. TLS uses 21,504 / 12,288. |
| Keep the start gate at 8,192 / 4,096 | the queue is no longer part of the internal start budget | The gate now covers the worker, buffers and esp-mqtt overhead only. |
| Model the queue, camera snapshot, owner gate and 12 KiB main stack in the simulator | the hardware crash has a regression | The model asserts the queue is in PSRAM, the camera snapshot loads on connect, the owner gate closes at 16,000 bytes and the 8 KiB main stack reports `main_overflow`. |

## Expected result

Measured with the 8 KiB main stack:

```text
MQTT off:           30,007 free / 28,660 largest
MQTT on, pre-fix:    5,199 free /  4,596 largest
```

Inferred after reverting to 12 KiB and moving the queue to PSRAM:

```text
MQTT off:           30,007 - 4,096 = 25,911 free
MQTT on, before camera snapshot:
                     25,911 - 10,752 owner - 3,584 worker - 3,072 buffers
                     = 8,503 free
MQTT on, after camera snapshot:
                     8,503 - 5,120 = 3,383 free
```

The start gate passes at 8,503 free. That is the 8 to 10 KiB target. The camera
snapshot then loads, and the discovery gate requires 10,240 free, so it closes.
Home Assistant discovery is skipped on this measured pool. The broker link and
command path stay up, and the main stack no longer overflows.

The simulator budget scenario reports 8,503 free / 7,156 largest after the
camera snapshot, with no allocation failures and no leaks. The exact largest
block on hardware remains to be measured.

## Gate thresholds

| Gate | Free floor | Largest floor | Why |
| :--- | ---: | ---: | :--- |
| Owner creation (plain) | 18,432 | 12,288 | 8,192 client start floor plus the 10,240 owner stack |
| Owner creation (TLS) | 21,504 | 12,288 | 11,264 TLS client floor plus the owner stack |
| Client start (plain) | 8,192 | 4,096 | 3 KiB worker, two 1.5 KiB buffers, esp-mqtt overhead |
| Client start (TLS) | 11,264 | 7,168 | 6 KiB TLS worker plus the same buffers |
| HA discovery | 10,240 | 4,096 | camera list plus the discovery cJSON tree and payload |

The 5,199-byte hardware case is below the owner gate, so the 10 KiB owner is
never created. That is the intended fail-closed behavior.

## Rejected cuts

| Candidate | Why not |
| :--- | :--- |
| Keep the queue in internal RAM | It is the 9.2 KiB allocation that caused the 5,199-byte pool. |
| Create the queue in `taskStep()` | It pays the queue before the start gate and on every owner instance. |
| Keep the early camera snapshot | It spends about 5 KiB when MQTT cannot start and pushes the main task further. |
| Keep the 8 KiB main stack | It overflowed on hardware after MQTT was enabled. |
| Lower the discovery gate to 8,192 | The discovery peak is 8,192 bytes, so that leaves no margin. HA stays skipped until the hardware pool grows. |
| Move the owner to PSRAM | No hardware high-water on the PSRAM stack path, and it handles settings and discovery. |
| Reduce the owner below 10 KiB | 4 KiB and 6 KiB both overflowed on hardware. |

## Validation done

- Host `mqtt-owner` test: owner gate runtime and pure-floor cases, start gate,
  queue constants, cached `MQTT_HA`, discovery gate and retained payloads.
- Simulator memory profile: 13 dumps pass, including the 30,007-byte budget,
  the 16,000-byte owner-gate failure and the 40,000-byte HA success.
- Simulator memory scenarios with `FURBLE_SIM_MQTT=1`:
  `memory-mqtt-task-alloc-fail`, `memory-mqtt-unreachable`,
  `memory-mqtt-low-heap-retry-limit`, `memory-boot-combination`.
- Simulator start, discovery and main-stack scenarios.
- Firmware builds: `m5stick-s3-debug`, `m5stick-s3`.
- Pre-review, static analysis and the flash-readiness gate before the PR.

## Still needs hardware

1. `perf heap` with `mqtt=true` on the StickS3. Confirm the free/largest pair
   after the owner gate, the queue move and the camera snapshot.
2. Confirm the main task stays up for at least an hour with MQTT enabled and
   the display active. Read the main high-water mark.
3. Confirm `mqtt_ha=true` either publishes the retained records or logs the
   discovery gate and skips them. No reset either way.
4. Confirm the companion 3 KiB and main 12 KiB stacks with a settings write and
   the WebUI enabled.
5. MQTT OTA throughput with the 1,536-byte buffers.
