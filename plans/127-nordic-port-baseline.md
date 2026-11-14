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
entries, and normalized-content copies under the declared Nordic roots
`ports/nrf52840`, `ports/nrf5340`, `ports/nrf54*`, `ports/nordic`, and
`platform/nordic`. It reports direct coupling in the current camera and app
layers, including public headers, so a future extraction can shrink the
inventory rather than silently growing a second implementation. This is a
machine guard against copied source content, including whitespace-only copies;
semantic rewrites with different bytes still require review and shared-scenario
tests.

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

### Coupling ratchet (port stack step 1)

- `tools/check_coupling_ratchet.py` counts, per file, four kinds of coupling:
  preprocessor conditionals naming a board identity macro, `M5.` uses,
  `M5.getBoard()` runtime switches, and ESP-IDF or NimBLE type names in the
  public headers under `include/` and `lib/furble/`. Comments and string
  literals are ignored.
- The committed manifest is `tools/coupling_ratchet.json`. A count above its
  entry fails. A count below it also fails until the manifest is regenerated
  with `--update`, so each reduction is locked in.
- `tools/pre_review.py` runs it, and `tests/test_check_coupling_ratchet.py`
  is in the scanner unit test set of the gate and of `pre-review.yml`.
- Board identity macros are `FURBLE_M5STICKC`, `FURBLE_M5STICKC_PLUS`,
  `FURBLE_M5STICKS3`, `FURBLE_M5COREX`, `FURBLE_M5STACK_CORE`,
  `FURBLE_GROVE_CORE` and `FURBLE_WAVESHARE_S3_ETH`. `FURBLE_NO_DISPLAY` and
  `FURBLE_ETHERNET` are not counted: they are already profile capability
  flags, set as both a compiler define and a CMake argument, and a ratchet on
  them would have no replacement to point to.
- Baseline on master `e66e8807`: 96 board identity guards in 16 files, 204
  `M5.` uses in 15 files, 31 `M5.getBoard()` switches in 7 files, and 351
  ESP/NimBLE header type names in 28 headers. Including the two profile flags
  there are 165 guard lines in 19 files; the review estimate of 174 also
  counted a header include guard and commented-out lines.

### Capability macros (port stack step 2)

- `include/FurbleBoardCaps.h` derives 0/1 capability macros from the board
  identity macros: `FURBLE_HAS_M5PM1`, `FURBLE_HAS_GPS_BURST_LOCK`,
  `FURBLE_HAS_USB_SERIAL_JTAG_CONSOLE`, `FURBLE_HAS_SD`,
  `FURBLE_HAS_VBUS_SENSE`, and the panel classes `FURBLE_PANEL_80X160`,
  `FURBLE_PANEL_135X240`, `FURBLE_PANEL_320X240` and `FURBLE_PANEL_NARROW`.
- 70 board guards became capability guards. The ratchet went from 96 board
  identity guards in 16 files to 26 in 12 files.
- The ratchet also fails a file that tests a capability macro with `#ifdef`
  or `defined()`, or uses one without including the header directly. Both
  mistakes would silently read the macro as 0.
- Deliberately unchanged: the `WATCHDOG` setting enum entry, its default and
  every switch case that names it stay on `FURBLE_M5STICKS3`, because the
  settings contract scanner checks each board-conditional setting against the
  host test target that defines that board macro. Code that reads the
  setting stays with it. `FurbleTextSize.h` and `FurbleSettingsDefaults.h` are
  settings contract files and keep their board macros for the same reason.
- Deliberately unchanged identity checks: the hand-placed legend arrows for
  the StickC and the Core Basic buttons, the StickS3 side button offset, the
  Core Basic timer wake clear before power off, and the GPS UART clock guard,
  which already falls back to `CONFIG_IDF_TARGET_ESP32S3`.
- `FURBLE_NO_DISPLAY` and `FURBLE_ETHERNET` were already capability flags and
  are unchanged. No `FURBLE_HAS_DISPLAY` alias was added.

### Battery and GPS rail routing (port stack step 3)

- `Platform::getBatteryStatus()` is the one source of the companion status
  battery fields. It replaces `UI::getBattery*()`, which had a display
  implementation in `FurbleUI.cpp` and a headless one in `main.cpp`.
- Behavior is unchanged on every shipped image. Display boards always have a
  battery backend, so they still report the raw M5Unified readings. The
  headless images resolve to an unknown board, whose battery capabilities are
  all false, so they still report every field as unknown. The rule is now
  stated once: no battery backend means unknown. VBUS follows
  `FURBLE_HAS_VBUS_SENSE`, which is 0 only on the Waveshare board, as before.
- `GPS::setRailPower()` is the only code that switches the GPS rail. The UI
  and headless `GPS_POWER` request handlers called `M5.Power.setExtOutput()`
  directly. Deviation: that console path now also gets the readback and one
  retry that every other rail switch already had, which is the documented
  M5PM1 first-access trap.
- The simulator `Platform::getBatteryStatus()` has the same body, gated by the
  modelled gauge capabilities. The companion host doubles moved from
  `UI::getBattery*()` to `Platform::getBatteryStatus()`.
- The ratchet no longer counts `src/FurblePlatform.cpp`: it is the M5Unified
  adapter, and moving a call into it must not fail the gate. Outside
  Platform, `M5.` uses fell from 160 to 148 and `main.cpp` has none left.

### Camera library public types (port stack step 4)

- `lib/furble/Device.h` defines `Furble::TxPower` (`P3`, `P6`, `P9`).
  `Device::init()`, `Device::powerLevelToDbm()` and `Camera::connect()` take
  it instead of `esp_power_level_t`, and `Device.cpp` maps it to dBm.
- `Camera::SecurityMode` no longer takes its values from `BLE_HS_IO_*`.
  `Camera.cpp` maps each mode to the NimBLE IO capability in `ioCap()`.
- The app layer still uses `esp_power_level_t` for the setting and the
  adaptive power policy. `Control::txPower()` is the one mapping point, used
  by the connect path, `Control::applyPower()`, `main.cpp` and the simulator.
  Its declaration adds one ESP type name to `include/FurbleControl.h`; the
  public header total still falls from 351 to 346 and the library headers
  `Camera.h` and `Device.h` lose all four `esp_power_level_t` and `BLE_HS_IO_*`
  uses.
- Behavior is unchanged. The camera connect log still prints the ESP-IDF
  level value: `Camera.cpp` maps `TxPower` back to it for that one log line.
  Unknown ESP levels still map to +3 dBm, as `powerLevelToDbm()` did.
- 43 host tests that passed `ESP_PWR_LVL_P3` to `Device::init()` or
  `Camera::connect()` now pass `Furble::TxPower::P3`. The headless doubles
  gained `TxPower` and `Control::txPower()`.

Verification:

- Camera vector tests pass. `sh tools/run_sanitized_host_tests.sh --clean`
  passes 183 of 183. The linked boundary suite passes 18 of 18.
- Fixed-version builds (`FURBLE_VERSION=1.0.0-ci FURBLE_TEST=0`) against the
  step 3 base `9dd2dd61`. Static RAM is unchanged; the mapping switches add a
  few bytes of text.

| Env | Base bin | Step 4 bin | Delta | DRAM |
| --- | ---: | ---: | ---: | ---: |
| m5stick-s3 | 1,848,960 | 1,849,024 | +64 | 47,208, unchanged |
| m5stack-core | 1,614,304 | 1,614,368 | +64 | 54,884, unchanged |

- The symbol diff on the StickS3 is confined to the mapping points:
  `Control::txPower()` (22 bytes), `Camera::onConnect()` (+12, the log
  mapping), `Camera::connect()`, `Device::init()` and their callers.
- `check_size_budget.py` and `stack_budget.py` pass for both envs, and the
  coupling ratchet is clean at 346 header type names.
