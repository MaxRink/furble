# 306: simulator memory-pressure reproduction and crash logging

## Motivation

Two StickS3 failures were found on hardware and fixed or mitigated in the
firmware, but never reproduced on the host:

- Enabling the WebUI with WiFi, BLE and the companion service up left the
  device at 13,543 free / 12,788 largest internal bytes. The 8 KiB supervisor
  and the 12 KiB HTTPS task could not be allocated, so the device logged
  `Mem alloc fail. size 0x00002000 caps 0x00000804` and boot-looped until
  #392 replaced the attempt with a fail-closed gate.
- Enabling MQTT on the pre-#392 build allocated a second 6 KiB esp-mqtt task
  and panicked in `esp_mqtt_client_start -> xTaskCreate` at
  `Mem alloc fail. size 0x00001800 caps 0x00000804`, followed by
  `RTC_SW_CPU_RST`.

The host simulator runs on a large host heap and never sees either allocation
fail. It also had no way to assert a gate decision or a service retry bound, and
no retained crash record that survives the simulator's `restart` path.

The seeded pair is the pre-#394 measurement. PR #394 changed the steady
allocation map, so the same scenarios can be rerun with the new measured pair
without changing their shape. Keeping the old pair as a fixture preserves the
pre-optimization failure contract.

## Design

### Heap model

`sim/heap_model.cpp` carries one internal pool. `heap_internal_free` and
`heap_internal_largest` seed it with the measured post-boot figures. Tasks
created before the admission window are part of that measurement. After
`heapActivate()`, only the optional services are charged: `webui`,
`webui_tls`, `mqtt` and `mqtt_client`. The model reports the hardware-shaped
`Mem alloc fail. size 0x%04x caps 0x00000804` line and records the failed task.

The capability bit values in `sim/shim/esp_heap_caps.h` now match ESP-IDF
(`MALLOC_CAP_8BIT` 0x004, `MALLOC_CAP_INTERNAL` 0x800) so that line is
comparable to a device log.

### WebUI admission model

`sim/webui_memory_model.cpp` runs the production
`WebUIMemory::supervisorGate()` and `serverGate()` functions against the modelled
pool. The log formats are shared constants in `include/FurbleWebUIMemory.h`, so
the simulator prints the production sentence. `webui_gate off` skips the
supervisor gate and asks for the pre-#392 8 KiB stack, which reproduces the
`0x2000` failure without claiming the old image is built.

This is an admission model. It does not serve HTTPS, generate a TLS identity or
claim TLS parity. Those boundaries are recorded in `sim/CLAUDE.md`.

### MQTT transport

The optional native MQTT build now models the esp-mqtt client task's 6 KiB
allocation at the transport boundary. When it cannot fit, `startClient()`
returns an error and the production `MAX_CONNECT_FAILURES` path runs. A refused
socket now emits both the error and the disconnect event, and a wall-clock
worker retries at the component's one-second cadence, so ten attempts finish in
a bounded scenario. `FURBLE_SIM_MQTT_FAST_RECONNECT` only shortens that host
cadence.

### Crash record

`include/FurbleCrashRecord.h` is the fixed-size record and its CRC. Production
keeps it in `RTC_NOINIT_ATTR` memory and refreshes it about once per second from
the UI task with reset reason, last logging task, uptime, internal free/largest
and four recent log lines. The log ring is filled through
`esp_log_set_vprintf`. No panic handler writes the record and no flash path is
used, so a recursive panic cannot deadlock on a lock or an NVS transaction. A
shutdown handler marks an orderly reset clean. On the next boot the record is
printed once and then invalidated.

The task and log tail are the last normal snapshot, not a guaranteed panic
task. That limit is documented instead of overstated.

`sim/CrashLogSim.cpp` stores the same record in a sidecar next to
`FURBLE_SIM_PREFS`, so the simulator's `restart` path proves one-shot reporting.
`crash-inject-panic` and `crash-inject-task-wdt` write an unclean record.

## Scenarios

| Scenario | Shape |
|---|---|
| `e2e/memory-webui-fail-closed.txt` | 13,543/12,788, WebUI on: supervisor created, server blocked, production gate log, 10 s retry, fed watchdog. |
| `e2e/memory-webui-pre-gate-alloc.txt` | Gate disabled, 6,800/2,048: 8 KiB supervisor allocation fails with the 0x2000 line, UI continues. |
| `e2e/memory-mqtt-task-alloc-fail.txt` | `task_create_fail mqtt`: owner task creation fails, firmware logs and continues. |
| `e2e/memory-mqtt-unreachable.txt` | Unreachable broker, no pressure: production retry accounting stops at 10. |
| `e2e/memory-mqtt-low-heap-retry-limit.txt` | 13,543/12,788: esp-mqtt client task fails at 0x1800 on each bounded attempt. |
| `e2e/memory-boot-combination.txt` | WebUI + MQTT + companion baseline: fail-closed WebUI, MQTT retry limit, no panic. |
| `e2e/crash-record-survives-restart.txt` | Injected panic survives `restart`, prints once, and a clean reboot does not print it. |

The MQTT scenarios require `FURBLE_SIM_MQTT=1` and an unreachable
`FURBLE_SIM_MQTT_URI`; the others run in the default build.

## Implementation state

Implemented on the branch:

- heap seeds and queries, the WebUI admission model and its scenarios;
- MQTT client-task accounting, disconnect-on-refused-connect, and the host
  reconnect worker;
- the shared crash record, the RTC writer, the simulator sidecar, the console
  `crash` command and host encoding coverage;
- documentation in `docs/sim.md`, `docs/crash-diagnostics.md`,
  `docs/console-commands.md`, the wiki copy and `sim/CLAUDE.md`;
- a pre-existing `saved_cameras` predicate in `sim/scripts/check-doc-tokens.sh`
  that the allowlist did not accept, which made the gate fail on master. The
  exact shape is now listed with its reason.

## Validation

Host and simulator:

```sh
python3 tools/pre_review.py --base origin/master
python3 tools/check_sim_scenarios.py
sh sim/scripts/check-doc-tokens.sh
ctest --test-dir .pio/host-sanitized --output-on-failure -R '^(crash-record|crash-log)$'
sh sim/scripts/run-e2e.sh
sh sim/scripts/run-memory.sh
```

Hardware is not used in this plan. The StickS3 USB-JTAG endpoint is unstable, so
the WebUI and MQTT rows remain measured-input simulator reproductions, not new
device measurements.

## Deviations

- The WebUI HTTPS server is not linked into the host simulator. The plan
  reproduces the production gate, log sentence and allocation failure at the
  heap boundary. It does not reproduce an HTTP session.
- The crash record's task field is the last logging task because ESP-IDF has no
  public panic hook. A true panic task name still requires the ROM panic output
  or a core dump.
- The simulator persists the record in a file because the host has no RTC
  memory. Production never writes flash from the record path.
