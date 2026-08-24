# ESP-IDF optimization program

## Motivation

furble has six release targets and five debug variants on its pinned ESP-IDF
5.4 toolchain, with BLE camera control, displays, GPS, and planned Wi-Fi,
MQTT, OTA, and Ethernet paths. The
firmware has power-management code, but it lacks a common measurement contract
for current, wake latency, radio reliability, heap, stack, flash, and UI work.
Several expensive diagnostic settings are also present in configurations used
as release inputs. An unmeasured Kconfig change can easily save RAM while
breaking DFS-sensitive UART, LEDC, SPI, BLE timing, or sleep recovery.

This is an umbrella roadmap. It does not authorize a broad rewrite. Each slice
below is a separate, independently mergeable PR with a simulator seam, an
all-board build, a named measurement, and a rollback. Defaults keep current
behavior until the measured result and the hardware gate justify a change.

## Evidence

Official Espressif documentation is normative for API and Kconfig behavior:

- [Performance and speed](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/performance/speed.html)
- [RAM usage](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/performance/ram-usage.html)
- [Binary size](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/performance/size.html)
- [Heap debugging](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/heap_debug.html)
- [Power management](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/power_management.html)
- [Sleep modes](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/sleep_modes.html)
- [Wi-Fi power save](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/wifi-driver/wifi-performance-and-power-save.html)
- [NimBLE Kconfig](https://github.com/espressif/esp-idf/blob/master/components/bt/host/nimble/Kconfig.in)
- [NimBLE host API](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/bluetooth/nimble/index.html)
- [External RAM](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/external-ram.html)
- [SPI and DMA](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/peripherals/spi_master.html)
- [FreeRTOS](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/freertos_idf.html)
- [Watchdogs](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/wdts.html)
- [NVS](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/storage/nvs_flash.html)
- [MQTT](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/protocols/mqtt.html)
- [Security](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/security/security.html)
- [Version policy](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/versions.html)
- [ESP-IDF release notes and migration warnings](https://github.com/espressif/esp-idf/releases)

These official articles and community reports are leads for experiments, not
acceptance criteria:

- [Espressif ULP low-power article](https://developer.espressif.com/blog/2025/04/ulp-lp-core-get-started/)
- [Espressif deep-sleep wake-stub article](https://developer.espressif.com/blog/2025/09/deep-sleep-wake-stub-get-started/)
- [Espressif simple boot article](https://developer.espressif.com/blog/2025/06/simple-boot-explained/)
- [Espressif RAM optimization article](https://developer.espressif.com/blog/2025/11/esp32c2-ram-optimization/)
- [NimBLE S3 power-save issue and measurements](https://github.com/espressif/esp-idf/issues/13073)
- [LVGL community forum](https://forum.lvgl.io/)

The August 2026 follow-up also reviewed the current ESP32-S3 guidance and
upstream implementation:

- [ESP-IDF 6.0.2 Wi-Fi performance and power save](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/wifi-driver/wifi-performance-and-power-save.html)
- [ESP-IDF 6.0.2 Mbed TLS memory tuning](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/protocols/mbedtls.html)
- [ESP HTTPS OTA resumption and partial download](https://github.com/espressif/esp-idf/blob/master/docs/en/api-reference/system/esp_https_ota.rst)
- [Advanced HTTPS OTA example](https://github.com/espressif/esp-idf/blob/master/examples/system/ota/advanced_https_ota/main/advanced_https_ota_example.c)
- [Espressif OTA reliability and security guidance](https://developer.espressif.com/blog/ota-updates-framework/)
- [ESP32-S3 Wi-Fi/BLE Android-hotspot coexistence report](https://github.com/espressif/esp-idf/issues/17871)

The checked-in release configurations already enable DFS, tickless idle, and
disconnected-station power management. Bootloader application rollback is
already enabled on all six release targets, and the OTA partition work already
emits the initial OTA metadata image. Its runtime health check is not implemented.
HTTPS OTA partial download and resumption are runtime adapter options that are
not implemented yet, rather than checked-in Kconfig settings. The release
configurations leave coexistence power management, Mbed TLS dynamic buffers,
and variable TLS record lengths disabled. They retain peer certificates after
verification and use the default 10 static Wi-Fi RX buffers and 32 dynamic RX
and TX buffers. These are candidates, not defects: each setting trades memory,
throughput, latency, or radio reliability and must be measured on the combined
BLE, Wi-Fi, MQTT, and HTTPS workload.

Prioritize the new candidates as separate slices:

1. Implement and validate the rollback health contract already specified by
   plan 34. Mark an image valid only after NVS, platform and PMIC, BLE, control
   task, UI or headless loop, heap floor, and reset-reason checks pass. Camera,
   Wi-Fi, MQTT, and user interaction remain outside this offline-safe boot
   contract. Test explicit validation, diagnostic rollback, crash rollback,
   and power loss before using OTA for the wider hardware matrix.
2. Add HTTPS range-resume behind a capability flag. Persist a versioned URL,
   immutable manifest identity, cryptographic image digest, expected length,
   target partition and slot, and committed byte count transactionally in NVS.
   Advance the checkpoint only after the corresponding flash write is durable.
   Require HTTP `206` and an exact `Content-Range` continuation. Reject a
   changed image, length or slot, a server that ignores ranges, and corrupt
   resume state. Host tests use a deterministic HTTP range server, then
   hardware tests cut the connection and power at multiple erase, write, and
   checkpoint boundaries.
3. Give OTA a scoped throughput policy. Temporarily select the measured Wi-Fi
   power mode for download, then restore the previous mode on success, failure,
   cancellation, and reboot. Do not globally disable modem sleep.
4. Measure Mbed TLS dynamic buffers, variable record lengths, CA release, and
   peer-certificate retention one option at a time. Record minimum internal
   heap, largest free block, download throughput, certificate failures, and
   reconnect behavior before selecting a release profile.
5. Measure coexistence power management and Wi-Fi buffer counts only after the
   real AP, MQTT, BLE scan, connected camera, and HTTPS OTA workload exists.
   Include home routers and Android 13 or newer hotspots because the upstream
   S3 coexistence report shows that a normal home-router pass is insufficient.

The current inventory also records the hard-won board constraints: GPS UART
must use an XTAL-stable clock under DFS, LEDC backlight needs an APB lock while
active, M5PM1's first transaction after idle needs a retry, and DMA display
buffers must remain internal and DMA-capable.

## Baseline and measurement contract

The first slice adds a `diag snapshot` console command and the same bounded
schema to the host simulator. It reports build/profile/toolchain identity,
reset reason, boot phase timestamps, CPU and APB frequency, PM-lock ownership,
free heap and minimum heap, largest internal and PSRAM blocks, task stack
high-water marks, BLE scan/connect/shutter counters, connection parameters,
Wi-Fi/MQTT state and queue depth, GPS state, LVGL frames and invalidations, and
NVS free entries and writes. Tests consume structured TSV or JSON, never human
log text.

Record a baseline for all six release targets and the M5StickS3 debug variant:

| Domain | Required measurements |
| --- | --- |
| Build | firmware, bootloader, partition, text, rodata, IRAM, DRAM, RTC, PSRAM, free flash |
| Boot | reset to `app_main`, first frame, and console ready |
| Memory | free, minimum, largest blocks by capability, and task high-water marks |
| Power | active, display idle, BLE scan, BLE connected idle, GPS active, Wi-Fi, MQTT, interval sleep and wake average |
| Radio | discovery, connect, reconnect, shutter round trip, missed notifications, RSSI, and TX setting |
| UI | frame period, invalidations, SPI bytes per frame, and wake-to-first-frame |
| Storage | NVS writes per setting interaction, free entries, and repeated-write boot time |
| Reliability | 24-hour idle, 1000 scan cycles, 1000 connect cycles, 100 interval wakes, watchdogs and reset reason |

Power results state the measurement point, USB or battery state, supply, cable,
display, and attached peripherals. USB current is not a battery-life claim.

## Independently mergeable slices

### 125.1 Instrumentation and simulator seams

Add the snapshot schema, fake clock, deterministic platform providers, build
metadata, map extraction, and CI baseline artifact. Add fakes for PM locks,
BLE transport, network and broker, display, GPS UART, and NVS. Host tests cover
serialization, malformed inputs, time advancement, lock leaks, and teardown.

Benefit: all later optimization claims become reproducible. Risk: counters can
perturb timing or consume RAM. Gate the feature by profile and prove release
overhead is bounded. Acceptance: host tests, all six release builds, and one
complete StickS3 snapshot pass.

### 125.2 Reproducible builds and cache hygiene

The byte-reproducible six-target firmware gate and immutable CI action lock are
already on `master`. Preserve those gates while completing the remaining
dependency-reporting and cache work below.

Pin PlatformIO, ESP-IDF, Python packages, component versions, sdkconfig inputs,
and generated metadata. Add CI ccache keys containing host, compiler, IDF,
board, sdkconfig hash, and source hash. Upload dependency licenses and a
version report. Split the simulator's stable LVGL, M5GFX, and M5Unified objects
from furble application objects so isolated PR worktrees can reuse a
content-addressed dependency cache. Include compiler, target board, LVGL config,
dependency revision, and compile flags in that cache key. The complete key
covers each dependency's content identity, transitive and generated headers,
forced simulator shim headers, both board macros, host ABI, exact compiler
version, C and C++ flags, sanitizer flags, link flags, and a cache-schema
version. Add scheduled clean builds and cache-bypass jobs. Coordinate the
implementation with plan 124.

Benefit: upgrades and tuning can be bisected. Risk: stale caches mask missing
inputs. Acceptance: clean and cached builds pass, with identical hashes where
supported or documented timestamp differences. The same simulator scenario
must pass from a cold build and a dependency-cache hit, and CI must retain an
uncached gate. Mutation tests change each key input and prove a cache miss.
Rollback is cache disablement.

### 125.3 Release, debug, power-lab, and size-lab profiles

Separate six release and five debug sdkconfigs. Keep watchdog, brownout, and
abort-on-corruption protections. Move comprehensive heap poisoning, heap task
tracking, verbose logs, and expensive PSRAM memtest to debug or diagnostics.
Add power-lab current markers and size-lab map reports.

Benefit: lower release boot time and RAM. Risk: hiding memory bugs. Keep
sanitizer, poisoning, and diagnostic CI jobs. Acceptance: all release and
debug builds, protocol tests, and a StickS3 idle soak pass. Rollback is config
only.

### 125.4 Compiler and linker matrix

Measure `-Os`, `-O2`, section garbage collection, and LTO one variable at a
time. Record flash, RAM, boot, shutter latency, BLE stability, watchdogs, and
build time. LTO must not be enabled by assumption because it can expose
undefined behavior or alter timing.

Candidate acceptance requires a measured win without more than 5% regression in
boot, shutter round trip, or minimum internal heap, and no reliability failure.
Keep the matrix even if `-Os` remains the default. Revert the selected flag.

### 125.5 PSRAM, flash mode, and cache matrix

Measure internal versus PSRAM placement for NimBLE, network, UI, and immutable
data. Keep flash, NVS, OTA, stacks, and DMA display buffers in valid internal
capabilities. Evaluate S3 cache line settings and QIO versus DIO only after
checking module wiring and documenting recovery.

Risk: cache misses, DMA faults, failed boot, or sleep wake errors. Acceptance
requires cold boot, display stress, OTA, NVS writes, BLE scan, light sleep,
deep sleep, and all-board builds. A flash-mode experiment never becomes a
default without a reflash procedure.

### 125.6 PM-lock ownership and event-driven idle

Audit CPU, APB, and no-light-sleep locks. Scope them to backlight, SPI, GPS,
camera transactions, USB, Wi-Fi, and MQTT. Replace safe polling with task
notifications or event groups and bounded timers. Never hold a control mutex or
PM lock across a delay or external callback.

Host tests cover acquire/release, timeout, cancellation, deletion, and DFS.
StickS3 gates GPS UART, LEDC, USB, BLE connected idle, and reconnect. Zero lock
leaks after 1000 lifecycle cycles is required. This slice coordinates plans 06,
07, 12, 15, 19, and 26.

### 125.7 PM configuration sweep and wake latency

Evaluate CPU minimum frequency, automatic light sleep, tickless idle, modem
sleep, XTAL versus APB peripheral clocks, flash leakage workaround, PSRAM
half-sleep or leakage workaround, and RTC clocks. Model changed clocks and
rejected peripheral operations in sim.

StickS3 with GPS attached must pass current, wake latency, display transitions,
BLE idle, and 100 interval wakes with no watchdog, reset, lost UART bytes, or
USB regression. Keep board-specific defaults. Flash power-down requires special
review because capacitance and wake source timing affect safety.

### 125.8 BLE profile and radio telemetry

Measure and then right-size NimBLE connections, bonds, MTU, host/controller
stacks, privacy, repeat-pairing, and unused roles. Report interval, supervision
timeout, PHY, RSSI, TX power, and scan window. Use separate idle, shutter, and
pairing profiles.

Host and FauxNY tests cover profiles and invalid values. Attached Fujifilm must
pass discovery, pairing, reconnect, 1000 shutters, and notifications. Other
vendors remain untested unless hardware arrives. Coordinate plans 08 to 11 and
25.

### 125.9 Passive scan and bounded advertisement parsing

Make active or passive scan, duplicate filtering, interval/window, timeout,
and manufacturer-data retention explicit per use case. Parse into bounded value
objects before camera construction and avoid per-advertisement heap churn.

Corpus tests cover each vendor, truncation, malformed lengths, duplicates, and
address types. Measure discovery latency and current in both modes. Fujifilm
hardware is required before changing its default; other vendors use review and
FauxNY.

### 125.10 Wi-Fi, MQTT, and Ethernet lifecycle

Add explicit network states and a bounded MQTT outbox. Measure Wi-Fi modem
sleep, listen interval, buffers, power save, reconnect backoff, TLS allocation,
and event-driven polling. Release Wi-Fi, MQTT, and W5500 resources when off.
Build and link Ethernet source explicitly for the Waveshare environment.

Host broker tests cover offline, reconnect, full outbox, duplicate delivery,
retained discovery, malformed payload, and TLS errors. Build all boards plus
Waveshare. Hardware-test a real AP and broker and the Waveshare node when
attached. Document message-loss semantics. Coordinate plans 33 and 34.

### 125.11 NVS wear and settings transactions

Guard unchanged writes, add delayed commit or explicit Apply for UI edits,
grouped transactions, schema versions, migrations, and NVS telemetry. Pairing
and deep-sleep safety state must have a commit barrier before success.

Host tests cover round trips, power loss, migration, full NVS, and 1000
unchanged writes. StickS3 validates reset durability. Revert to immediate
commit if power-loss durability fails.

### 125.12 LVGL and SPI display pipeline

Measure invalidations, frames, SPI bytes, and wake-to-first-frame. Keep DMA
buffers internal. Evaluate partial buffer count, SPI batching, dirty-area
limits, cache-aware icon decompression, changed-value guards, and event-driven
LVGL rendering.

Host tests assert unchanged subjects do not invalidate. Screenshot or checksum
tests cover all pages and overflow cases. StickS3 checks tearing, flicker,
backlight, 30-minute animation, and wake frame. Preserve APB lock lifecycle.

### 125.13 GPS and IMU duty cycle

Gate UART, `$PCAS` sentence rate, receiver standby, and GPS rail through the
state machine. Keep XTAL-stable UART clocks and account for the GPS v1.1's lack
of backup supply and approximately 108 second cold-start cost. Use IMU
interrupts or bounded sampling when supported.

Host tests cover rates, standby, no-fix, stale fix, malformed sentences, and
wake timing. Attached GPS hardware must validate cold and warm starts, standby,
rail cycles, and interval wakes before defaults change. Coordinate plans 14 to
21.

### 125.14 Interval deep sleep and wake stub

Compare full reboot with RTC wake. Investigate a minimal S3 wake stub for
versioned, checksummed state validation and early abort only. Keep camera, GPS,
display, USB, and PMIC sequencing out of the stub until separately gated.

Sim covers every wake reason, corrupt record, version mismatch, missed shot,
and abort. StickS3 runs 100 wake cycles and must remain USB-reflashable. Do not
enable a wake stub in release without recovery documentation. Coordinate plans
19 and 26.

### 125.15 Security and production hardening

Decide the alpha posture for signed OTA, rollback, secure boot, flash
encryption, key provisioning, anti-rollback, debug/JTAG policy, and companion
authentication. Measure RAM, boot, and flash cost. Use sacrificial eFuse
hardware, never the only recoverable StickS3.

Host tests cover signature rejection, rollback, version checks, credential
redaction, and malformed metadata. CI keeps signed and unsigned artifacts
distinct. Hardware-test OTA recovery on an erasable board. Production security
defaults require an explicit release decision.

### 125.16 SDK and dependency upgrade ladder

Upgrade one dependency at a time. Record current PlatformIO and ESP-IDF first,
then evaluate the latest supported 5.4 patch, ESP-IDF 5.5, and ESP-IDF 6.x in a
compatibility branch. Upgrade M5PM1, M5GFX, M5Unified, LVGL, NimBLE C++, and
TinyGPSPlus separately with lockfile, API, and map reports.

ESP-IDF 6 has major migration risk: legacy peripheral drivers are removed,
provisioning moves to an external component, Mbed TLS and PSA APIs change, and
language defaults change. A 5.5 candidate must pass the alpha gate before 6.x
becomes a release candidate. Every step needs all-board builds, host/sim,
protocol, and hardware smoke tests. Rollback is the previous lockfile and
sdkconfig set. Coordinate with plan 124 and release CI.

## Required simulator seams

All optimization PRs must be host-testable unless physically irreducible:

- `FakeClock` for PM, GPS, reconnect, and interval timing;
- `FakePower` for lock ownership, DFS, sleep rejection, wake reason, and rails;
- `FakeBleTransport` for adverts, connection parameters, pairing callbacks,
  cancellation, disconnect races, RSSI, and notifications;
- `FakeNetworkTransport` and broker for DHCP, Wi-Fi states, MQTT QoS, outbox
  exhaustion, duplicates, TLS failure, and W5500 link state;
- `FakeDisplay` for LVGL invalidation, frame timing, DMA failures, and buffers;
- `FakeGps` for byte timing, `$PCAS`, cold or warm start, rail cut, and bad data;
- `FakeNvs` for commit failure, power loss, wear, full namespace, migrations,
  and corruption.

Use sanitizers, fuzzers, overflow sweeps, and long deterministic scenarios.
Assert state transitions and external events, not implementation details.

## Hardware and release gates

The M5StickS3 is the primary gate when it is available. Tests state
USB-connected versus battery conditions. Safe automated tests include cold
boot, snapshots, PM and DFS transitions, display off/on, BLE scan and idle,
paired Fujifilm reconnect and shutter cycles, GPS parser and standby, settings
durability, OTA validation, and deep-sleep recovery.

Human or unavailable-equipment blockers remain explicit: Wi-Fi credentials,
real MQTT authorization, companion password entry, numeric-comparison pairing,
Waveshare hardware, power-analyzer current, and irreversible eFuse operations.
Only Fujifilm cameras are available. Other vendor claims are simulator and
review coverage, marked untested.

An optimization PR is mergeable only after independent review, host tests and
sanitizers, deterministic sim, all six release and five debug builds, before and
after artifacts, and applicable StickS3 gates. Initial budgets are no unexplained
failure, no more than 5% regression in boot, shutter round trip, minimum
internal heap, or wake-to-frame, no missed-notification or reconnect increase,
and no extra unchanged NVS writes. A power win must name its scenario.

On failure, revert the smallest candidate change, preserve the artifact, and
add a regression test. Do not stack another optimization on a failing one.

## Dependency graph

```
125.1 measurement -> 125.2 tooling -> 125.3 profiles
                                  -> 125.4 compiler matrix
                                  -> 125.5 memory/cache matrix
125.1 + 125.3 -> 125.6 PM locks -> 125.7 PM sweep
                                  -> 125.8 BLE -> 125.9 scan
                                  -> 125.10 network -> 125.11 NVS
                                  -> 125.12 LVGL -> 125.13 GPS
                                                    -> 125.14 deep sleep
125.2 + OTA/auth -> 125.15 security
125.1 + 125.2 -> 125.16 SDK ladder
```

This plan is documentation only. Existing feature plans remain authoritative
for behavior. The first implementation PR is 125.1.
