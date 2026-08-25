# 127 - Nordic silicon port baseline

Status: baseline only. This change adds no Nordic board, firmware target, or
hardware claim. It defines the contracts that a future Nordic implementation
must satisfy while continuing to use the existing simulator and host harness.

## Motivation

furble is an ESP32 remote today. A Nordic build is not a board-profile change:
the camera library uses NimBLE C++ types, settings use ESP-IDF NVS, GPS uses
the ESP-IDF UART event driver, and the app uses FreeRTOS, M5Unified, LVGL,
`esp_pm`, ESP timers, and ESP networking. Copying those implementations into a
second firmware would create two behavior trees and make protocol regressions
likely.

The first useful slice is therefore a portability boundary and a simulator
gate. It lets the protocol and state-machine work move ahead without selecting
a Nordic board or pretending that a Zephyr build already exists.

## Non-goals and fixed boundaries

- Do not add a speculative PlatformIO or Zephyr board environment here.
- Do not select a production SoC until the BLE concurrency, memory, radio
  security, and board power budget are measured on a development kit.
- Keep Waveshare ESP32-S3-ETH, W5500, and its optional PoE HAT in the ESP-only
  HAL. Nordic has no reason to inherit that board code or its power model.
- Do not duplicate vendor protocol bytes in a Nordic tree. The same production
  protocol sources and scenario corpus must be used by ESP firmware, the host
  simulator, and the future Nordic target.
- Hardware bring-up is a later gate. This baseline is verified only by static
  checks and host tests.

## Candidate silicon matrix

The matrix is a decision aid, not a board recommendation. Exact flash, RAM,
radio, and errata values must be pinned to the chosen part's current product
specification in the bring-up PR.

| Candidate | Useful strengths | Port risks | Role in the plan |
|---|---|---|---|
| nRF52840 | Mature BLE central and peripheral ecosystem, USB, large single-core memory budget, many existing DKs and modules | Single core, older generation, no Wi-Fi or Ethernet, board peripherals are not standardized | First protocol/HAL proof target if a simple remote or sidecar is needed |
| nRF5340 | More memory and a dual-core application/network split, supported by nRF Connect SDK and Zephyr sysbuild | Multi-domain image, IPC and power ownership, more complex CI and debugging | Second target when the workload needs the extra headroom or isolation |
| nRF54L15 family | Newer Nordic generation and a forward-looking low-power path | New silicon and board support must be pinned to the exact part and SDK release; avoid assumptions about peripherals or radio feature availability | Later optimization target after the contract is stable |

All candidates require a BLE central implementation for cameras. The companion
GATT service additionally requires a peripheral role. The future configuration
must explicitly enable the roles required by a product mode and test concurrent
scan, camera links, and companion advertising on the selected controller.
Zephyr exposes these as separate build-time roles (`CONFIG_BT_CENTRAL`,
`CONFIG_BT_PERIPHERAL`, `CONFIG_BT_OBSERVER`, and `CONFIG_BT_BROADCASTER`).

## Recommended software baseline

Use Zephyr through the Nordic nRF Connect SDK for the Nordic target. Keep the
SDK and toolchain in a pinned west manifest and a checked-in lock/metadata file.
Use sysbuild when the selected SoC has multiple domains or a network-core image.
Do not use the deprecated Nordic nRF5 SDK for new work.

The current Nordic documentation describes sysbuild as the CMake/west system
for combining images and domains, and says it is enabled by default in nRF
Connect SDK. It also documents SDK/toolchain validation per build
configuration. Primary references:

- [nRF Connect SDK build configuration and sysbuild](https://docs.nordicsemi.com/r/bundle/nrf-connect-vscode/page/reference/ui_sidebar_applications.html/build-configuration-page?contentId=GJjPmz6hV5v6zdzvse2CIQ)
- [nRF Connect SDK sysbuild overview](https://docs.nordicsemi.com/r/bundle/nrf-connect-vscode/page/guides/build_overview.html?contentId=TlMcHFmLwDT2xkKInvhemA)
- [Nordic SDK and toolchain management](https://docs.nordicsemi.com/r/bundle/nrfutil/page/nrfutil-sdk-manager/nrfutil-sdk-manager.html)
- [Zephyr Bluetooth LE host roles](https://docs.zephyrproject.org/latest/connectivity/bluetooth/bluetooth-le-host.html)

The first Nordic PR should add only a freestanding contract library and a
native host test target. A board application comes after that PR is green.

## Portability inventory

The current boundary is intentionally narrow:

| Area | Current location | Current state | Required extraction |
|---|---|---|---|
| Byte codecs, advertisement parsing, provisioning TLV | `lib/furble/protocol/` | Portable C++ and host-tested | Keep shared unchanged; add platform-free fuzz and corpus gates |
| Camera vendor state and command policy | `lib/furble/*.cpp` | Coupled to NimBLE C++ and some ESP randomness/timing | Introduce a small async BLE client, GATT, UUID, address, clock, random, and log interfaces; retain one vendor implementation |
| Camera list and discovery | `CameraList.*`, `Scan.*` | NimBLE and Preferences coupled | Put persistence and scan/connection adapters behind interfaces; keep matcher and dedup policy shared |
| Settings schema and wire IDs | `src/FurbleSettings.*`, `lib/preferences/` | ESP NVS wrapper | Extract a typed key/value contract with atomic commit, migration, corruption, and generation semantics; map ESP NVS and Zephyr settings/NVS separately |
| Control and reconnect policy | `src/FurbleControl.*` | FreeRTOS queues/tasks, ESP power enum, tick APIs | Extract event-loop/state-machine logic from task and queue adapters; use a monotonic 64-bit clock interface and keep wrap tests |
| Companion GATT protocol and HMAC | `src/FurbleCompanion*`, protocol helpers | NimBLE server, ESP timer, settings | Keep framing, authorization, replay/session rules shared; put GATT callbacks, timers, and crypto primitives behind a HAL |
| GPS parser and policy | `src/FurbleGPS.*`, TinyGPSPlus | TinyGPSPlus is portable; UART/event driver and power control are ESP-only | Keep parser/policy shared; implement Zephyr UART async or polling adapter and explicit rail/standby adapter |
| Power, sleep, watchdog, reset | `src/FurblePower.*`, `FurblePlatform.*`, watchdog headers | M5Unified, ESP PM, PMIC, ESP reset APIs | Define capabilities and measured transitions, not ESP enum aliases; map deep sleep, watchdog, GPIO wake, and reset reason per board |
| UI and display | `src/FurbleUI*`, LVGL, M5GFX | ESP/M5 display and touch | Headless Nordic first; later use LVGL with a separate display/input HAL and no camera-policy fork |
| SD/GPX and filesystem | `src/FurbleSD.*`, `FurbleGPX.*` | ESP SDMMC/SDSPI and FATFS | Keep GPX serialization shared; storage mount/write queue is a HAL |
| Wi-Fi, MQTT, Ethernet, PoE | `src/FurbleMQTT.*`, `FurbleEthernet.*` | ESP-IDF network stack; W5500/PoE is Waveshare-specific | No Nordic requirement; keep Waveshare and optional PoE model ESP-only, and test transport-neutral MQTT separately |
| OTA and boot state | `src/FurbleOTA.*`, partition plans | ESP partitions and ESP OTA APIs | Use MCUboot/Zephyr image metadata and settings on Nordic; preserve the same update state machine, validation, rollback, and interruption scenarios |
| Simulator | `sim/`, `tests/host/`, protocol tests | Existing host simulator uses production sources plus HAL doubles | Make this the acceptance gate for every extracted contract and every Nordic HAL double |

`tools/check_portability_inventory.py --check` protects the declared portable
subtree and `tools/portable_core_manifest.txt` is the shared production-source
manifest. The checker fails closed for missing or empty roots, stale manifest
entries, and a copied protocol source under a Nordic port tree. It also reports
direct coupling in the current camera and app layers, including public headers,
so a future extraction can shrink the inventory rather than silently growing a
second implementation.

## Shared contract shape

The extraction should proceed in independent slices. Names are descriptive and
can be adjusted during implementation, but the ownership rule is fixed:

1. `furble-core`: protocol codecs, camera policy/state, settings schema,
   reconnect/backoff, companion framing/auth, GPS parsing, OTA lifecycle, and
   deterministic policy clocks. No RTOS, BLE stack, filesystem, display, or
   SoC headers.
2. `furble-port`: small interfaces for `BleCentral`, `BlePeripheral`,
   `SettingsStore`, `Clock`, `Random`, `Crypto`, `Uart`, `Power`, `Watchdog`,
   `Storage`, `Network`, and `OtaWriter`. Interfaces return explicit errors and
   own buffer lifetimes. No interface exposes NimBLE, ESP-IDF, or Zephyr types.
3. `furble-esp`: adapters retaining current ESP32 behavior. The existing
   M5Unified, NimBLE, NVS, ESP timer, PMIC, Wi-Fi, W5500, and OTA code stays
   here. The Waveshare optional PoE HAT remains in this adapter and its power
   model remains board-specific.
4. `furble-nordic`: Zephyr/nRF Connect SDK adapters. This starts headless and
   uses a development kit. A board overlay describes pins and rails; it must
   not change core behavior.
5. `furble-sim`: deterministic doubles for every port interface. The current
   SDL UI remains available, but headless contract tests must not depend on
   SDL or M5GFX.

The core must be compiled once per target from the same source list. A CI check
should compare the core source manifest used by ESP, sim, and Nordic builds and
fail if either target adds a vendor-protocol copy. A test-only compile can use
the Nordic adapter doubles before a Nordic SDK is installed.

## Simulator-first acceptance gates

Every slice has two gates: a pure host contract test and the existing simulator
scenario gate. The simulator is required for the Nordic port, not optional
developer tooling.

### Stage A: preserve today's gates

- Run all host CTest targets, protocol tests, vendor conformance, overflow
  sweeps, malformed advertisement corpus, settings round trips, HMAC/replay,
  OTA state-machine tests, and fuzz seeds under ASan/UBSan.
- Keep the existing SDL simulator scenarios and add a headless mode that
  replaces only the display/input HAL. No camera or settings behavior is
  duplicated for the headless path.
- Add deterministic virtual time, random seed, BLE peer, UART, storage, power,
  watchdog, and OTA doubles. The current wrap-safe clock tests are part of the
  common contract.

### Stage B: Nordic adapter doubles on the host

- Compile the future Zephyr adapter interfaces against host doubles with
  `-Wall -Wextra -Werror`, without a Nordic SDK or board.
- Run camera central plus companion peripheral concurrency scenarios, including
  scan while connected, two camera links, connection loss, security failure,
  HMAC replay, and callback ordering.
- Run settings power-loss and migration scenarios, GPS partial-frame and rail
  transitions, watchdog expiry, deep-sleep wake reason, and OTA interruption /
  rollback scenarios.
- Run the same protocol corpus and fuzz seeds through the production core. A
  Nordic-specific test may add adapter faults, but may not replace a shared
  scenario.

### Stage C: target builds and hardware gates

- Build a pinned nRF Connect SDK application for the chosen DK with a headless
  configuration. CI must build pristine and incremental configurations and
  archive map, size, image metadata, and toolchain identity.
- Exercise the same host scenarios against the target's adapter configuration.
- Only then test real camera pairing, companion auth, sleep current, GPS,
  watchdog recovery, and OTA. Hardware results belong in the target board PR.

## Capability contract

The Nordic adapter cannot claim a capability just because an API exists. Each
capability has a result and a testable negative path:

| Capability | Required contract | Nordic implementation checkpoint |
|---|---|---|
| BLE central | scan, filter, connect, discover, read/write/notify, RSSI, connection params, cancel | Zephyr host/controller configuration and concurrent-link limits measured |
| BLE peripheral | companion service, notifications, bonding, whitelist/session close | Security and callback ordering tested with a host GATT double |
| Security | authenticated pairing where required, HMAC, constant-time compare, replay/session generation | Use PSA/mbedTLS or the SDK crypto API behind `Crypto`; never copy ESP calls into core |
| Settings | typed values, atomic commit, migration, corruption detection, generation | Zephyr settings/NVS or a small append-only store, with power-loss simulation |
| Time/random | monotonic time, wall-clock optional, unbiased session randomness | Inject both into core; test wrap and deterministic replay |
| Power | named locks/constraints, sleep entry/exit, wake reason, battery/charger optional | Map to Zephyr PM and board GPIO/PMIC; absent battery is a valid capability result |
| Watchdog | start/feed/stop policy and reset reason | Map to Nordic watchdog; host double must model expiry |
| GPS/UART | byte stream, partial reads, write completion, rail/standby | Zephyr UART API plus board-specific power GPIO; TinyGPSPlus remains shared |
| Storage | queued writes, fsync/close result, mount loss | LittleFS/FATFS choice is board-specific; GPX bytes remain shared |
| OTA | verified image, interrupted write, pending/confirm/rollback | MCUboot and sysbuild image contract; test metadata and state machine before DK |
| Network | optional interface and explicit unavailable result | Nordic remote may omit Wi-Fi/Ethernet; MQTT feature is not a hidden requirement |
| Display/input | optional headless operation, event queue | First Nordic target is headless; LVGL is a later adapter, not a policy fork |

## Reproducibility and CI

The Nordic lane must pin the west manifest revision, nRF Connect SDK revision,
toolchain bundle, host generator, Python dependencies, and board revision. CI
should build in a clean container with `SOURCE_DATE_EPOCH`, stable path maps,
and a recorded manifest/toolchain digest. Repeat the build and compare the
application, bootloader, and metadata artifacts. A negative version or source
change must change the expected artifacts.

The shared simulator and host tests run before the SDK job. The SDK job is
allowed to be unavailable while this baseline is being developed, but the
workflow must fail closed once a Nordic target is declared supported. A
generated source-manifest check should prove that the three builds consume the
same `furble-core` files.

## Follow-up slices

1. Extract `Clock`, `Random`, and platform-free errors. Port the existing wrap
   and fuzz tests first.
2. Extract BLE UUID/address/GATT value interfaces and adapt the host NimBLE
   double. Keep one Fujifilm implementation and run the full camera corpus.
3. Extract `SettingsStore`; add atomic and power-loss tests before changing the
   ESP NVS adapter.
4. Extract companion framing/auth and OTA lifecycle from their transports.
5. Extract UART/GPS, watchdog, power, and storage interfaces with deterministic
   host doubles.
6. Add a headless `furble-nordic` application skeleton and a pinned west/NCS
   build for one DK. Do not add a product board until its pinout and power
   measurements are available.
7. Add the first Nordic DK to CI, then select nRF52840, nRF5340, or nRF54L15
   using measured BLE concurrency, flash/RAM, sleep, and OTA results.

## Implementation state

This baseline is documentation and an inventory guard only. No Nordic binary,
board environment, SDK dependency, or hardware test is claimed.
