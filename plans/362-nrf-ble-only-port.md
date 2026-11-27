# 362 - nRF port for a BLE-only furble variant

Status: research and decision. This plan adds no firmware environment, board
profile, driver, directory under `ports/`, or support claim. No nRF part was
bought and none was measured. Every external claim carries a source ID that
resolves in the Sources table with its retrieval date, 2026-11-26. Facts about
this checkout were read at master `81139818`.

Tags on every claim:

- `[tree]` read in this checkout at `81139818`.
- `[measured: esp32s3]` a size measured from the committed `m5stick-s3` build
  outputs on 2026-11-26, not from a fresh build.
- `[vendor]` a vendor product page, README or manifest.
- `[inferred]` my conclusion, stated by no source.
- `UNVERIFIED` no readable page settles it; listed again in section 13.

Related: [127](127-nordic-port-baseline.md) (the port contract this plan
inherits), [287](287-mcu-portability-and-bundle-hardware.md) (the MCU survey),
[318](318-next-board-battery-gps.md) (the board decision this plan must not
contradict), [322](implemented/322-hardware-abstraction.md) (the HAL seam the display work
would extend).

## Motivation

Furble targets ESP32 boards. The nRF question is narrow and worth asking once:
could the camera remote core run on nRF52840 or nRF5340 for better BLE central
behaviour, lower power and a mature NimBLE, as a BLE-only variant with no
Wi-Fi, no MQTT-over-Wi-Fi, no WebUI server and optionally no display?

Two things make the question non-obvious in this tree. First, the camera library
is already split: 18 files under `lib/furble/protocol` are machine-checked
platform-free, and the rest is NimBLE plus five small ESP facilities. Second, the
NimBLE C++ API furble calls is the same API `NimBLE-Arduino` exposes on nRF, so
a port might cost far less than a rewrite. The plan tests both claims against
the code and against the flash on the target silicon.

The answer has to be numeric, because the interesting failure mode is not "the
port is hard" but "the port is easy and the resulting device is worse than the
ESP32 board plan 318 already picked".

## Recommendation

**Do not add an nRF board target now.** The port is cheap enough to be
tempting and the device it produces is not worth it. The arithmetic, all in
bytes from section 2 and section 7:

- **A display variant is 498,583 to 598,583 bytes before any BLE stack.** That
  is the measured camera and protocol library (118,582) plus the UI object
  (91,058) plus the icon arrays (48,943) plus LVGL's own 200 to 300 KB figure
  plus about 40,000 of glue.
- **The nRF52840 and the nRF5340 application core both have 1 MB of flash**, so
  neither accepts that image plus a second OTA slot of the same size. One image
  fits with 426 to 526 KB left over; a 598 KB second slot does not. More RAM on
  the nRF5340 does not buy flash.
- **The headless variant fits comfortably and ships none of what differentiates
  furble.** It lands around 370 KB of 1,024 KB even with a pessimistic BLE stack,
  and it has no UI, no intervalometer, no GPS geotagging, no companion GATT and
  no MQTT. The UI alone is 91,058 bytes of `src/FurbleUI.cpp`.
- **The parts that would fit force Zephyr and a second CI system.** nRF54L15 at
  1.5 MB NVM and nRF54LM20A at 2 MB NVM `[vendor S21][vendor S22]` are the only
  candidates with room, neither is supported by the `n-able` core
  `[vendor S10]`, and both mean nRF Connect SDK, `west` and a build that leaves
  `platformio.ini` behind.

Two conditions flip this, and each is a measurement rather than an argument:

1. **A measured headless nRF52840 image under 300 KB of flash and 100 KB of RAM
   after the seam extraction.** That turns the headless slice into a roughly
   two-week proof and it is the cheapest possible evidence that `lib/furble`
   carries no hidden ESP dependence.
2. **A measured BLE stack footprint over 400 KB.** That would make it a no
   outright: host plus controller plus toolchain at that size leaves no headroom
   on a 1,024 KB part, and there is no smaller configuration of furble to fall
   back to.

Neither condition has been measured, because no nRF part was bought and none was
built for. The measurement is task 7 in section 10.

## Portability evidence

This is the part of the work that survives whatever the board decision is, so it
is stated here and then supported in section 1 and section 2.

- **The portability checker is clean.** `python3
  tools/check_portability_inventory.py --check` reports `portable contract:
  clean` with 18 protocol files and 27 shared manifest entries `[tree]`.
- **A grep for platform symbols in `lib/furble/protocol` finds only comment
  prose.** Searching for `esp_`, `nimble`, `arduino`, `freertos`, `m5`,
  `lovyangfx`, `sdkconfig`, `nvs` and `zephyr` across the whole directory returns
  seven hits, every one of them a comment saying the file is free of those
  things `[tree]`.
- **The measured camera plus protocol layer is 118,582 bytes, and only 4,640 of
  it is codec code.** The camera layer is 88,656 bytes over 20 objects and the
  protocol layer 29,926 over seven; of the protocol layer, the three camera
  codecs are 4,640, GPS is 19,157 and provisioning is 6,129 `[measured: esp32s3]`.
- **The coupling is 65 sites in 17 files, collapsing to five seams plus two
  includes.** The seams are a microsecond clock (19 sites), `esp_random` (5), the
  factory address (2), `heap_caps` (4) and FreeRTOS queue, task and delay (35).
  The two includes are `<Preferences.h>` and `<sdkconfig.h>` `[tree]`.
- **38 of the 40 NimBLE symbols `lib/furble` calls are portable through
  `n-able`, and the remaining two come from `NimBLERemoteValueAttribute`.** Both
  `readValue` and `writeValue` are declared there, so the API furble uses maps
  onto `NimBLE-Arduino` without a facade `[vendor S11]`.
- **M5GFX does not port.** LovyanGFX, its upstream, lists "ESP32 SPI, I2C,
  8bitParallel / ESP8266 SPI, I2C / ATSAMD51 SPI" as its supported combinations
  and no nRF52 or Zephyr target `[vendor S3]`. An LVGL display seam is needed
  instead, and it is small: 32 `M5.Display` sites in `src/FurbleUI.cpp` plus 6
  `M5.getBoard()` switches `[tree]`.

## 1. What the tree says about portability

### 1.1 Platform-free today, and machine-checked

`python3 tools/check_portability_inventory.py --check` reports `portable
contract files: 18`, `shared source manifest files: 27` and `portable contract:
clean` `[tree]`. The 27 manifest entries in
`tools/portable_core_manifest.txt` are:

| Group | Files | Platform-free because |
| :--- | ---: | :--- |
| `lib/furble/protocol` | 18 (7 `.cpp`, 11 `.h`) | Only `<algorithm>`, `<array>`, `<cctype>`, `<cmath>`, `<cstdio>`, `<cstdlib>`, `<cstring>`, `<initializer_list>`, `<limits>`, `<optional>`, `<string>`, `<string_view>`, `<utility>`, `<vector>` and their own headers `[tree]` |
| `lib/hal` | 9 (4 `.cpp`, 5 `.h`) | The register-bus seam from plan 322; `HalBus.h` is the only include of another `lib/hal` file `[tree]` |

A grep for `esp_`, `nimble`, `arduino`, `freertos`, `m5`, `lovyangfx`,
`sdkconfig`, `nvs` and `zephyr` across `lib/furble/protocol` returns seven hits,
all of them prose in comments that says the file is free of those things. The two
that state the contract are `lib/furble/protocol/GpsCasic.h:14` and
`lib/furble/protocol/GpsUbx.h:13` `[tree]`.

Four more files in `lib/furble` are platform-free and are not in the manifest
because the manifest covers codecs, not the camera type layer `[tree]`:

- `lib/furble/FurbleTypes.h` and `lib/furble/FurbleTestSync.h` include nothing
  at all.
- `lib/furble/BtDebugHex.h` includes only `<cstddef>` and `<cstdint>`.

Everything else in the camera layer is coupled. The portability checker reports
`camera library direct coupling: Arduino, ESP-IDF, FreeRTOS, NimBLE` across 61
camera library files and `app-layer direct coupling: Arduino, ESP-IDF, FreeRTOS,
LVGL, M5Unified, NimBLE` across 125 app and public-header files `[tree]`.

### 1.2 The coupling is 65 sites in five seams

A census of ESP-IDF and FreeRTOS identifiers across `lib/furble/*.cpp` and
`lib/furble/*.h` finds 65 hits in 17 files `[tree]`:

| Seam | Sites | Where | What a port needs |
| :--- | ---: | :--- | :--- |
| Microsecond monotonic clock, `esp_timer_get_time()` | 19 | `lib/furble/Camera.cpp:7`, `lib/furble/Ricoh.cpp:14`, `lib/furble/Scan.cpp:4` | A `uint64_t` microsecond clock, `k_uptime_get()` on Zephyr, `millis()`/`micros()` on Arduino |
| `esp_random()` | 5 | `lib/furble/Nikon.cpp:9`, `lib/furble/DJIOsmo.cpp`, `lib/furble/FauxNY.cpp` | Any CSPRNG; used for a passkey and a per-attempt jitter, not for key material `[inferred]` |
| Own address, `esp_read_mac()` and `esp_efuse_mac_get_default()` | 2 | `lib/furble/Device.cpp:2`, `lib/furble/DJIOsmo.cpp:8` | A 6-byte factory address; Nordic has `NRF_FICR->DEVICEADDR` `[inferred]` |
| Heap with capability flags, `heap_caps_calloc()` and `heap_caps_free()` | 4 | `lib/furble/BtDebugJournal.cpp:8` | `calloc`; the capability flags are PSRAM routing, meaningless on nRF |
| FreeRTOS queue, task and delay | 35 | `lib/furble/NikonBase.h:10`, `lib/furble/NikonBase.cpp`, `lib/furble/FujifilmSecure.cpp`, `lib/furble/Lumix.cpp`, `lib/furble/DJIOsmo.cpp:10` | FreeRTOS (n-able ships it) or a `rtos` seam for Zephyr |

Plus two includes that are not in the census because they are not identifiers:
`<Preferences.h>` in `lib/furble/CameraList.h:4` and `lib/furble/Scan.h:28`, and
`<sdkconfig.h>` in `lib/furble/BtDebugJournal.h:9` `[tree]`.

One file already degrades gracefully. `lib/furble/Fujifilm.cpp:20` guards the
FreeRTOS include with `__has_include(<freertos/FreeRTOS.h>)` and falls back to
`<chrono>`/`<thread>`, so the registration wait compiles on a host target with no
FreeRTOS `[tree]`. That fallback is not usable on a Cortex-M target without a
pthread implementation, so it is a host-test convenience and not a port path.

Two `std::thread` uses exist and both are sim-only `[tree]`:
`lib/furble/Scan.cpp:492` is inside the `FURBLE_SIM` start-probe block, and
`include/FurbleUI.h:97` is the simulator UI thread handle.

### 1.3 The NimBLE surface is compatible with NimBLE-Arduino

Furble builds against `h2zero/esp-nimble-cpp` 2.5.0, vendored under
`components/esp-nimble-cpp` with one ownership fix `[tree]` (`src/idf_component.yml`,
`dependencies.lock`). On nRF the equivalent is `NimBLE-Arduino`, by the same
author, driven by the `n-able` Arduino core, which states "No softdevice, NimBLE
provides a complete BLE stack" and lists NimBLE-Arduino 1.4.0 or later as the
requirement `[vendor S10]`.

I probed 40 NimBLE C++ symbols that `lib/furble` actually calls against the
`NimBLE-Arduino` master headers `[vendor S11]`. All 40 are present. The 38 found
directly in the class headers included `setMTU`, `injectConfirmPasskey`,
`injectPassKey`, `getSecurityPasskey`, `isBonded`, `deleteBond`, `setSecurityAuth`,
`setSecurityIOCap`, `setSecurityInitKey`, `setSecurityRespKey`, `setOwnAddrType`,
`setOwnAddr`, `setPower`, `createServer`, `createClient`, `deleteClient`,
`getCreatedClientCount`, `getScan`, `init`, `dataToHexString`,
`setConnectionParams`, `setConnectTimeout`, `secureConnection`, `subscribe`,
`start`, `setInterval`, `setWindow`, `setActiveScan`, `isEncrypted`,
`isAuthenticated`, `getHandle`, `canRead`, `canWrite`, `canNotify`, `canIndicate`
and `canWriteNoResponse`. The remaining two, `readValue` and `writeValue`, are
inherited from `NimBLERemoteValueAttribute`, which declares
`NimBLEAttValue readValue()` and `bool writeValue(...)` `[vendor S11]`.

What that means numerically `[tree]`: of the 61 camera library files, 20 are
`.cpp` and every one of them includes a NimBLE header, so the NimBLE type
dependency is total. It is also the one dependency whose nRF replacement has the
same names, which is the single strongest argument in favour of the n-able route.

## 2. Where the size actually is

`[measured: esp32s3]` from `.pio/build/m5stick-s3` at `81139818`, an ESP32-S3
release build with `CONFIG_COMPILER_OPTIMIZATION_SIZE=y`, ESP-IDF 5.5.3,
`esp-nimble-cpp` 2.5.0 and LVGL 9.4.0. These are per-object compiled sizes from
`xtensa-esp32s3-elf-size -B`, so they are an upper bound on the linked
contribution and not a linker map reading.

| Group | Objects | text | data | bss |
| :--- | ---: | ---: | ---: | ---: |
| `lib/furble` camera layer | 20 | 88,656 | 76 | 1,113 |
| `lib/furble/protocol` | 7 | 29,926 | 0 | 0 |
| BLE library total | 27 | **118,582** | 76 | 1,113 |

The protocol split matters, because a camera-only variant does not need all of
it `[measured: esp32s3]`:

| File | text | Needed by a camera-only variant |
| :--- | ---: | :--- |
| `GpsCasic.cpp` | 10,324 | No |
| `GpsUbx.cpp` | 8,230 | No |
| `ProvisionTLV.cpp` | 6,129 | No (companion provisioning only) |
| `FujifilmProtocol.cpp` | 1,973 | Yes |
| `CameraListProtocol.cpp` | 1,508 | Yes |
| `AdvertisementProtocol.cpp` | 1,159 | Yes |
| `GpsDemux.cpp` | 603 | No |

So the camera protocol codecs are 4,640 bytes and the vendor drivers, `Camera`,
`CameraList`, `Scan` and `Device` carry the other 88,656.

App-layer objects that a BLE-only variant keeps or drops `[measured: esp32s3]`:

| Object | text | BLE-only variant |
| :--- | ---: | :--- |
| `src/FurbleUI.cpp` | 91,058 | Drops with the display |
| `src/FurbleGPS.cpp` | 34,752 | Drops without a GPS |
| `src/FurbleWebUI.cpp` | 29,008 | Drops |
| `src/FurbleMQTT.cpp` | 26,087 | Drops |
| `src/FurbleControl.cpp` | 14,985 | Keeps |
| `src/FurbleCompanionService.cpp` + `FurbleCompanion.cpp` | 23,342 | Optional |
| `src/FurbleSettings.cpp` | 10,093 | Keeps, rewritten |
| `src/FurblePlatform.cpp` | 9,444 | Keeps, rewritten |
| `src/FurbleWiFi.cpp` | 10,162 | Drops |
| `src/FurbleOTAMQTT.cpp` | 8,103 | Drops |
| `src/FurbleTimeKeeper.cpp` + `FurbleTimeKeeperPolicy.cpp` | 4,994 | Keeps, rewritten |

## 3. Do the camera protocols behave differently on nRF?

The protocols are vendor state machines over GATT. Nothing in
`lib/furble/protocol` changes per controller, so the question is whether the
seven vendors depend on ESP32 controller behaviour. One census answers it. Per
vendor driver, the counts below are of `secureConnection()`, `esp_random()`,
`xQueue*`, `subscribe()` and `Scan::` uses `[tree]`:

| Vendor mode | Files | Secure pairing | ESP random | FreeRTOS queue | Notify subscribe | Own scan |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: |
| Sony | `Sony.cpp` | 1 | 0 | 0 | 0 | 0 |
| Nikon Smart and Remote | `Nikon.cpp`, `NikonSmart.cpp`, `NikonRemote.cpp`, `NikonBase.cpp` | 0 | 1 | 6 | 0 | 1 |
| Canon EOS Remote | `CanonEOSRemote.cpp` | 1 | 0 | 0 | 0 | 0 |
| Canon EOS Smart | `CanonEOSSmart.cpp` | 1 | 0 | 0 | 0 | 0 |
| Fujifilm Basic | `FujifilmBasic.cpp` | 0 | 0 | 0 | 5 | 0 |
| Fujifilm Secure | `FujifilmSecure.cpp` | 2 | 0 | 5 | 7 | 1 |
| Ricoh | `Ricoh.cpp` | 1 | 0 | 0 | 7 | 0 |
| Lumix | `Lumix.cpp` | 0 | 0 | 0 | 3 | 0 |
| DJI Osmo | `DJIOsmo.cpp` | 1 | 2 | 0 | 4 | 0 |

Five consequences for a controller swap:

- Six of nine vendor modes require LE Secure Connections pairing. The IO
  capability is selected per vendor at `lib/furble/Camera.cpp:126`. The
  project-wide settings are `setSecurityAuth(true, true, true)` at
  `lib/furble/Camera.cpp:607`, `setSecurityInitKey` and `setSecurityRespKey` with
  ENC plus ID distribution at `lib/furble/Device.cpp:80`, and an own-address type
  of `BLE_OWN_ADDR_RPA_PUBLIC_DEFAULT` at `lib/furble/Device.cpp:82`. These are
  host API calls, but their meaning depends on the controller's resolvable
  private address handling and bond store, so they are the highest-risk part of
  the port `[inferred]`.
- Only DJI asks for a large MTU: `NimBLEDevice::setMTU(517)` at
  `lib/furble/DJIOsmo.cpp:137`. Every other vendor runs on the stack default, so
  a controller that negotiates lower costs DJI throughput and nothing else
  `[inferred]`.
- Only Nikon and Fujifilm Secure use a FreeRTOS queue, and both use it to wait
  for a callback that arrives on the NimBLE host task. That pattern is a
  host-stack threading assumption, not a controller assumption, and the host
  doubles already exercise it `[tree]`.
- Only Nikon and Fujifilm Secure scan from inside the vendor driver, on top of
  the shared `Scan` class, so scan-while-connecting is exercised by two modes
  `[tree]`.
- Notifications are used by every mode except Sony and Nikon, which poll. A
  controller with a different CCCD or descriptor cache policy changes those five
  modes `[inferred]`.
- Scanning is active, with a duty cycle: `setActiveScan(true)` and
  `setInterval`/`setWindow` at `lib/furble/Scan.cpp:84` `[tree]`. The GAP scan
  behaviour of the nRF controller is vendor-specified and does not change the
  vendor protocol `[inferred]`.

One thing genuinely improves. The ESP32 hardware traps recorded in `CLAUDE.md`
for the GPS UART (`UART_SCLK_XTAL` versus `UART_SCLK_REF_TICK`, and the
`ESP_PM_APB_FREQ_MAX` lock the LEDC backlight needs) exist because of ESP clock
gating and DFS. nRF has neither, so a nRF variant deletes a whole class of bug by
not having the peripheral clock tree `[inferred]`.

What cannot be answered from the tree is whether a specific nRF controller
implements a vendor quirk identically. Plan 159 and the peer certification work
already require an exact model and firmware before a feature is certified, and
this plan does not relax that. Every vendor other than the Fujifilm body on the
bench is untested here `[tree]`.

## 4. Toolchain

Five candidates, assessed against what furble needs: C++17, LVGL 9.4, a BLE
central with several concurrent links, a peripheral role for the companion, and a
build that can be pinned.

| Option | Version read | Builds furble's NimBLE C++ calls unchanged | LVGL | LVGL display driver | Verdict |
| :--- | :--- | :--- | :--- | :--- | :--- |
| Adafruit nRF52 Arduino BSP | 1.7.0 `[vendor S6]` | No. It ships Bluefruit over a Nordic SoftDevice, and its own notes say it calls into "the Nordic SoftDevice radio stack" `[vendor S7]` | Yes, LVGL has no external dependencies `[vendor S12]` | Hand written `flush_cb` | Fallback only |
| `sandeepmistry/arduino-nRF5` | README last changed January 2024 `[vendor S8]` | No. It states it "does not contain any Arduino style API's for BLE functionality" and exposes SoftDevice headers only `[vendor S8]` | Possible | Hand written | Reject |
| Arduino mbed core (Nano 33 BLE) | 4.6.0 `[vendor S9]` | No. ArduinoBLE is an mbed wrapper with a different API `[inferred]` | Possible | Hand written | Reject on API |
| `n-able` Arduino core + NimBLE-Arduino | n-able 0.4.0, NimBLE-Arduino 2.5.2 `[vendor S10][vendor S11]` | Yes, all 40 probed symbols `[vendor S11]` | Yes | Hand written | Best fit for the headless proof |
| Zephyr / nRF Connect SDK | NCS 3.3.5, Zephyr 4.3.2 `[vendor S4][vendor S5]` | No. Zephyr's host API is C (`bt_gatt_*`), so `lib/furble` needs the plan 127 `BleCentral`/`BlePeripheral` facade `[inferred]` | Yes, LVGL is a Zephyr module and is in the NCS manifest allowlist `[vendor S13][vendor S14]` | Zephyr display driver plus LVGL module | The only route with a real display |

Two facts decide between them.

`n-able` plus NimBLE-Arduino is the only combination that compiles `lib/furble`
without a BLE facade, and there is a PlatformIO platform for it,
`h2zero/platform-n-able`, which "aims to provide the use of the NimBLE-Arduino
library ... using the n-able Arduino core" `[vendor S15]`. Furble is a
PlatformIO project, so that keeps one build system `[tree]`.

Zephyr through NCS is what plan 127 already recommends, and it is the only
option with a maintained display stack. It is also a different build system
(`west`, not `platformio.ini`), and the PlatformIO `nordicnrf52` platform
pins `framework-zephyr` at `~3.40402.0`, that is Zephyr 3.4.2, against a current
Zephyr 4.3.2 `[vendor S16][vendor S5]`. Using NCS through PlatformIO would
therefore pin a two-major-version-old Zephyr, and using NCS properly means
`west` and a second CI system `[inferred]`.

For the record on the controller: NCS defaults to Nordic's SoftDevice Controller,
an RTOS-agnostic binary library for the nRF52 and nRF53 Series distributed in
`nrfxlib` `[vendor S17]`. Apache NimBLE itself ships both a host and a controller
and states its controller supports nRF51, nRF52 and nRF5340, and that it
"completely replaces the proprietary SoftDevice on Nordic chipsets"
`[vendor S2]`. So the "mature NimBLE" premise is correct for n-able, and only
partly relevant to NCS, which uses Nordic's own controller and a Zephyr host.

## 5. Display: M5GFX or an LVGL seam

M5GFX 0.2.27 is pinned in `platformio.ini` `[tree]` and is a fork of LovyanGFX.
LovyanGFX describes itself as a "Display (LCD / OLED / EPD) graphics library (for
ESP32 SPI, I2C, 8bitParallel / ESP8266 SPI, I2C / ATSAMD51 SPI)" and lists
"ArduinoESP32 and ESP-IDF are supported" `[vendor S3]`. There is no nRF52 port and
no Zephyr port `[vendor S3]`. M5GFX adds M5 board auto-detection on top, and that
detection is what furble's `M5GFX_BOARD` pinning patch exists to constrain `[tree]`.

So M5GFX makes no sense off M5 hardware, for two independent reasons: it targets
ESP32 class peripherals (LCD_CAM, I2S parallel, RMT) that nRF does not have, and
its board layer is a table of M5 products. Porting it would be strictly more work
than writing the driver you actually need `[inferred]`.

The good news is that the seam is small. All display access goes through a
handful of calls in one file `[tree]`:

- 32 `M5.Display` call sites in `src/FurbleUI.cpp`, of which the load-bearing
  ones are `pushImageDMA` at `src/FurbleUI.cpp:1967`, `waitDMA` at `:1862`,
  `setBrightness` at `:527`, `sleep` at `:1863` and `wakeup` at `:1901`.
- 6 `M5.getBoard()` switches in the same file, four of them geometry and two
  input wiring, as plan 322 records.
- Across `src/`, `M5.<method>` appears 189 times, dominated by `M5.Display` (41),
  `M5.Touch` (28), `M5.Imu` (24), `M5.Power` (20) and `M5.Rtc` (18). A portable
  variant needs only the first, plus `M5.In` and `M5.Btn*` for input.

An `IDisplay` seam of the shape plan 322 already describes (panel class, bus,
pins, size, rotation, brightness method, minimum brightness, per-panel sleep
dwell) plus a `flush_cb` is the right abstraction. It is roughly the work of one
LVGL porting port per panel and it is useful to the ESP32 boards whether or not a
nRF target ever lands `[inferred]`.

## 6. Buyable nRF boards

Read from vendor pages on 2026-11-26. `[vendor]` throughout this section.

| Board | SoC | Flash / RAM | Display | Battery | Price | Fit |
| :--- | :--- | :--- | :--- | :--- | ---: | :--- |
| Adafruit Feather nRF52840 Express (4062) | nRF52840 | 1 MB / 256 KB | None. The 2.4 inch 320x240 TFT FeatherWing (3315, ILI9341, TSC2007 touch) stacks on top | JST LiPo plus charger on board | not read | Bench only |
| Adafruit Feather nRF52840 Sense (4516) | nRF52840 | 1 MB / 256 KB | None | JST LiPo plus charger | $44.95 | Bench only, IMU and PDM mic |
| Adafruit CLUE (4500) | nRF52840 | 1 MB / 256 KB | 1.3 inch 240x240 IPS TFT | JST PH, 3 V to 6 V in, **no LiPo charging** | $44.95 | Real display, no charger |
| Seeed XIAO nRF52840 / Sense | nRF52840 | 1 MB / 256 KB, 2 MB onboard QSPI | None | Charge and discharge management on board, no JST | not read | Smallest bench board |
| Arduino Nano 33 BLE (Sense Rev2) | nRF52840 | 1 MB / 256 KB | None | UNVERIFIED | not read | Reject, mbed plus ArduinoBLE |
| nRF5340 DK | nRF5340 | 1 MB / 512 KB app, 256 KB / 64 KB net | None on board. Arduino Uno Rev3 shield compatible | USB or 1.7 to 5.0 V in, CR2032 holder | not read | Dev kit only |
| LilyGO T-Echo | nRF52840 | vendor page says "FLASH 2MB / RAM 2MB", which contradicts Nordic and is the QSPI flash figure | 1.54 inch SPI e-paper | 850 mAh | not read | Needs a new UI tier |
| Seeed Wio Tracker L1 Pro | nRF52840 | 1 MB / 256 KB | 1.3 inch OLED | 2000 mAh, 1.0 mm 2-pin JST, solar input | $47.90 | Best display plus battery board |
| HolyIoT modules | nRF52840 | as nRF52840 | None | None | not read | Not a candidate |

Three corrections to the brief in the request. Each is recorded as a
correction rather than applied silently, so the next reader does not repeat the
same assumption.

1. **The brief assumed an "Adafruit nRF52840 Feather and its TFT variants".**
   There is no such variant. The 2.4 inch 320x240 TFT is the FeatherWing that
   stacks on any Feather `[vendor S18]`, so a Feather plus display plus battery
   is three stacked boards with no case, not a product.
2. **The brief listed the CLUE as a board with a display and a battery.** It has
   both, but it explicitly "does not have built in LiPoly battery charging", for
   safety with alkaline and NiMH cells `[vendor S20]`. It is a desk board unless
   a charger is added, so it does not satisfy "a battery" in the sense the brief
   meant.
3. **The brief would have taken the LilyGO T-Echo's published "RAM 2MB" at face
   value.** That figure is wrong for an nRF52840 `[vendor S7a]`, whose product
   page states 1 MB Flash and 256 KB RAM `[vendor S1]`. Treat the T-Echo as
   256 KB of RAM and its 2 MB as the external QSPI flash.

The nRF5340 DK has four buttons and four LEDs, no display, and an Arduino Uno
Rev3 form factor `[vendor S19]`, so a TFT shield is possible on the bench. It is
not a product.

## 7. Flash and RAM budget

Everything below uses the measured 118,582-byte BLE library from section 2 and a
Cortex-M4 at 64 MHz as the comparison target. Cortex-M4 and Xtensa code density
differ, and none of this was cross-compiled for nRF, so the library figure is
`[inferred]` as a nRF number even though the ESP32 number is measured.

### 7.1 Headless, no display, no GPS

| Item | Bytes | Basis |
| :--- | ---: | :--- |
| `lib/furble` camera layer | 88,656 | measured |
| Camera protocol codecs | 4,640 | measured |
| Control, settings and timekeeping rewrite | ~25,000 | `[inferred]`, smaller than the ESP objects because the RTC and NVS paths shrink |
| NimBLE host plus controller | UNVERIFIED | No readable page gives an nRF52840 figure |
| n-able core, FreeRTOS, newlib | UNVERIFIED | |
| Bootloader | UNVERIFIED | |

Even with the three UNVERIFIED rows at a pessimistic 250 KB combined, the total
is about 370 KB of a 1,024 KB part. **The headless variant fits on nRF52840 with
room for a dual-bank bootloader.** That is the case with a real chance.

### 7.2 With a display

| Item | Bytes | Basis |
| :--- | ---: | :--- |
| BLE library | 118,582 | measured |
| `src/FurbleUI.cpp` | 91,058 | measured on ESP32 |
| Icons component | 48,943 | measured, `[measured: esp32s3]` from `components/icons` |
| LVGL 9.4 | 200,000 to 300,000 | vendor claim: "for a typical UI, you need only ~100kB RAM, ~200-300kB flash" `[vendor S12]` |
| App glue, settings, companion | ~40,000 | `[inferred]` |
| BLE stack plus bootloader | UNVERIFIED | |

The low end is 498,583 bytes and the high end 598,583 bytes before any BLE stack.
On the nRF52840's 1,024 KB that leaves 426 KB to 526 KB, so a single image fits
and a second OTA slot of the same size does not `[inferred]`.

The nRF5340 does not help: its application core also has 1 MB of flash, and only
its RAM doubles to 512 KB `[vendor S1][vendor S1a]`. The same arithmetic applies.

The silicon that does fit is nRF54L15 at 1.5 MB NVM and 256 KB RAM, or
nRF54LM20A at 2 MB NVM and 512 KB RAM `[vendor S21][vendor S22]`. Neither is
supported by n-able `[vendor S10]`, so both mean Zephyr or NCS.

### 7.3 RAM

Nothing measured applies directly. LVGL's own figure is about 100 KB for a typical
UI `[vendor S12]`, and furble's ESP32 static DRAM budgets are 48,128 to 57,344
bytes with the LVGL draw buffers allocated at run time `[tree]`
(`tools/size_budget.json`). Adding a BLE host and controller, 256 KB is tight but
plausible for a display variant, and comfortable for headless `[inferred]`.
UNVERIFIED until measured.

## 8. What must be rebuilt

| Area | Current size and location | Today | nRF replacement | Effort |
| :--- | :--- | :--- | :--- | :--- |
| Settings persistence | 10,093 B, `src/FurbleSettings.cpp`, `lib/preferences/Preferences.cpp`, `CameraList` | ESP-IDF NVS through an Arduino-style wrapper; `nvs.h` and `nvs_flash.h` at `lib/preferences/Preferences.cpp:18`, ESP types in the public header | Zephyr `settings`/NVS, or n-able's bundled EEPROM subset `[vendor S10]`, or a small append-only store. The key set, wire IDs and migration rules stay shared | M |
| Camera list | 12,219 B, `lib/furble/CameraList.cpp` | `Preferences.h` include in the header at `lib/furble/CameraList.h:4` | A `KeyValueStore` interface behind the same policy | S |
| OTA | 15,820 B across seven `src/FurbleOTA*` objects, one of them empty on this board | ESP partition API, `esp_ota_*` | MCUboot through NCS, or no OTA at all on the headless bench build. The state machine, validation and interruption scenarios stay shared if OTA is kept | L |
| Timekeeping | 4,994 B, `src/FurbleTimeKeeper*.cpp` | PCF8563/PCF85063 through `m5::RTC8563_Class`, plus `esp_timer_get_time()` | Any I2C RTC driver, or BLE-derived time with no RTC. The monotonic clock seam from 1.2 covers the rest | M |
| GPS UART | 41,527 B, `src/FurbleGPS.cpp` and `FurbleGPSUbx.cpp` | ESP-IDF UART event driver with a clock-source workaround | A Zephyr async UART or an Arduino `HardwareSerial`. Optional on a BLE-only variant | M |
| Power and rails | 10,954 B, `src/FurblePlatform.cpp`, `src/FurblePower.cpp` | M5Unified PMICs, `esp_pm` DFS, deep sleep and GPIO wake | nPM1100 or nPM1300, or nothing but a divider on ADC | M |
| Watchdog and reset reason | spread through `FurblePlatform` | ESP task watchdog and `esp_reset_reason()` | Zephyr `wdt` or nRF `WDT`, `RESETREAS` | S |
| Display | 91,058 B, `src/FurbleUI.cpp` | M5GFX plus LVGL 9.4 | `IDisplay` seam plus an LVGL port, or Zephyr display plus the LVGL module | L |
| Wi-Fi, WebUI, MQTT, Ethernet, SD | 74,000 B and more | ESP-IDF networking, FATFS | Deleted from a BLE-only variant by definition | none |

The estimate that matters: the five seams in section 1.2 plus the two includes
are a small, mechanical extraction. The three large items are display, OTA and
settings, and only settings is unavoidable `[inferred]`.

## 9. Decision

The front matter states the recommendation and the two conditions that flip it.
This section is the long form: four reasons, then what the flip conditions mean
in practice.

1. The only nRF variant that fits the measured code is headless. A headless
   furble is a library port, not a product: the differentiating features in this
   tree are the UI (91,058 B of `src/FurbleUI.cpp`), the intervalometer, GPS
   geotagging, the companion GATT and MQTT `[tree]`. Spending a platform on the
   one configuration that ships none of them is poor value `[inferred]`.
2. The display variant does not fit on nRF52840 or nRF5340. Section 7.2 puts it
   at 498 to 598 KB before a BLE stack on a 1 MB part with no OTA slot. The parts
   that fit are nRF54L15 and nRF54LM20A, which have no n-able support and
   therefore require the full plan 127 facade plus Zephyr `[inferred]`.
3. The battery goal is already reachable without a port. Plan 318 records the
   Espressif NimBLE `power_save` figures: 3.3 mA on the main XTAL versus 230 uA
   with a 32.768 kHz crystal on ESP32-S3, and it picks the T-Beam Supreme because
   it has that crystal. The nRF advantage over a crystal-equipped ESP32-S3 is
   not established by any figure I could read, and plan 318's bench item 1 is the
   experiment that would settle it [318](318-next-board-battery-gps.md)
   `[inferred]`.
4. The extraction pays back on ESP32 regardless. The five seams are exactly what
   plan 127 asks for, they shrink the coupling ratchet, and they are what a
   future Zephyr target needs anyway. Doing them for the ESP boards first costs
   nothing extra `[inferred]`.

Two conditions flip the answer, both stated as measurements so the decision can
be revisited without re-arguing it:

1. **A measured headless nRF52840 image under 300 KB of flash and 100 KB of RAM
   after the seam extraction.** The headless slice then becomes a two-week proof
   and is worth doing, because it is the cheapest possible evidence that
   `lib/furble` carries no hidden ESP dependence.
2. **A measured BLE stack footprint over 400 KB.** That would make it a no
   outright: the headless variant then loses its headroom on a 1,024 KB part.

A display product is a separate question and not one of the two. It needs the
598,583-byte image plus a BLE stack plus a second slot: about 1.7 MB of NVM with
OTA, or 1.5 MB without. That is nRF54LM20A or nRF54L15 territory, both mean
Zephyr, so it is revisited only with a Zephyr build already green.

### 9.1 The minimum vertical slice, if it is done at all

One board: Seeed XIAO nRF52840 Sense, because `n-able` lists it explicitly
`[vendor S10]` and it is the cheapest thing that is not a dev kit.

Build: `platform-n-able` plus NimBLE-Arduino 2.5.2, compiling the unchanged
`lib/furble` camera layer and the unchanged `lib/furble/protocol` camera codecs
from the shared manifest. No settings, no display, no GPS, no companion, no OTA.
A `main.cpp` that scans, lists, connects to one camera, sends shutter, and logs
over USB serial.

The five seams are stubbed to the nRF equivalents, not abstracted: `micros()`,
`NRF_FICR->DEVICEADDR`, `random()`, `calloc`, and FreeRTOS as n-able ships it.

### 9.2 What the first milestone must prove

Four things, all measurable:

1. **Same sources.** The build consumes the same files listed in
   `tools/portable_core_manifest.txt` plus the same 20 camera `.cpp` files, with
   no copy under a `ports/` root. `python3 tools/check_portability_inventory.py`
   still passes and reports zero declared Nordic port sources.
2. **Same bytes on the wire.** A Fujifilm connect and shutter produces the same
   GATT write sequence as the ESP32-S3 headless build for the same camera. The
   existing `BtDebugJournal` output `[tree]` is the comparison artifact.
3. **A size.** Report the linked text, data and bss for the image, and the
   per-object cost of `lib/furble` on Cortex-M4, against the 118,582-byte ESP32
   figure. That number is what decides whether section 9's threshold is met.
4. **A power number.** BLE-connected current with the panel absent and nothing
   else running, against plan 318's 3.3 mA ESP32-S3 floor and 230 uA crystal
   figure. Without this the whole motivation is unevidenced.

If any of the four fails, the plan closes as a documented no and the ESP32 board
path in plan 318 continues alone.

## 10. Task table

Estimate is calendar effort for one experienced embedded developer, `[inferred]`
throughout. Nothing here is scheduled and no task is started by this plan.

| # | Task | Depends on | Estimate | Gate |
| ---: | :--- | :--- | :--- | :--- |
| 1 | Extract the microsecond clock seam (`Clock`) from `esp_timer_get_time()` across `Camera.cpp`, `Ricoh.cpp`, `Scan.cpp` | none | 2 d | host tests, ESP builds byte-comparable |
| 2 | Extract the random and own-address seam from `esp_random()`, `esp_read_mac()` and `esp_efuse_mac_get_default()` | none | 2 d | `NikonSmart` passkey and `Device` address host tests |
| 3 | Extract the `KeyValueStore` seam out of `Preferences` for `CameraList` and `Scan` | none | 3 d | settings round-trip and corruption host tests |
| 4 | Extract an `rtos` seam for the five FreeRTOS queue and delay sites, or confirm FreeRTOS on the target | 1 | 3 d | Nikon and Fujifilm Secure registration tests |
| 5 | Add a `FURBLE_NO_DISPLAY` headless firmware target on ESP32-S3 and record its size | none | 2 d | builds and boots, `check_size_budget.py` row |
| 6 | Re-measure plan 318 bench item 1 on a crystal-equipped ESP32-S3 before buying any nRF part | none | 2 d | measured BLE-connected current, both boards |
| 7 | Spike: `platform-n-able` plus NimBLE-Arduino, XIAO nRF52840, `lib/furble` unchanged, one Fujifilm body | 1 to 4 | 5 d | section 9.2 items 1 to 3 |
| 8 | Decide on sections 9.2 items 3 and 4 against the section 9 thresholds | 5, 7 | 1 d | written go or no in this plan |
| 9 | If go: nRF settings backend and camera list persistence | 3, 7 | 5 d | power-loss and migration host tests |
| 10 | If go: companion GATT peripheral role on nRF, concurrent with a camera link | 7 | 8 d | scan-while-connected and two-link scenarios |
| 11 | If go to a display product: `IDisplay` seam, then a Zephyr or Arduino LVGL port for one panel | 7 | 15 d | one panel flushes at 30 fps, sleep dwell per plan 322 |
| 12 | If go to a display product: NCS `west` build, second CI system, MCUboot | 11 | 10 d | clean build, dual-bank OTA image |
| 13 | Documentation: `docs/supported-hardware.md`, README Supported Controllers, `docs/components/board-map.md` if a new board ships | any of 9 to 12 | 1 d | doc gate per `CLAUDE.md` |

Tasks 1 to 6 are worth doing on their own terms and do not commit the project to
nRF. Tasks 7 and 8 are the decision point. Tasks 9 to 12 are the project.

## 11. Risks

| # | Risk | Likelihood | Impact | Mitigation |
| ---: | :--- | :--- | :--- | :--- |
| R1 | NimBLE host plus controller is much larger on nRF than assumed, and the headless image loses its headroom | Medium | Kills the headless variant | Task 7 measures it before anything else is built |
| R2 | `n-able` is a one-maintainer core with 0.4.0 as its latest release `[vendor S10]`, and it is not part of the PlatformIO `nordicnrf52` platform `[vendor S16]` | High | Long-term support risk | Keep the ESP32 path primary; treat n-able as a proof vehicle, not a product base |
| R3 | Security and bonding behaviour differs on the nRF controller, and Canon, Ricoh or Fujifilm Secure fail to pair | Medium | Blocks four of the seven vendors | The passkey and confirmation flows already have host doubles; a Fujifilm Secure bench gate is mandatory |
| R4 | The 517-byte MTU request from DJI is refused or negotiated lower | Low | DJI only | Measure the negotiated MTU in task 7 |
| R5 | The display variant is attempted on nRF52840 and stalls on flash | Medium | Sunk time | Section 7.2 is the written bound; the display target is nRF54L15 or nRF54LM20A only |
| R6 | Zephyr through PlatformIO pins 3.4.2, so an NCS build has to leave PlatformIO and duplicate CI | High | Ongoing cost | Accept it explicitly, or do not do the display variant |
| R7 | Vendor protocol behaviour is untested on nRF for six of seven vendors, and only Fujifilm hardware exists `[tree]` | Certain | Support claims | No new vendor claim. Plan 159 certification rules apply unchanged |
| R8 | Cortex-M4 code density differs from Xtensa enough to move the 118,582-byte figure by a double-digit percentage | Medium | Changes the budget | Task 7 reports the measured Cortex-M4 per-object sizes |
| R9 | The port grows a second copy of vendor protocol bytes | Low | Two behaviour trees | `check_portability_inventory.py` already fails a normalized-content copy under `ports/` roots `[tree]` |
| R10 | The extraction in tasks 1 to 4 regresses ESP32 timing | Medium | Ships a bug to the primary platform | Every seam keeps its current implementation behind a compile-time default, and the ESP size and stack budgets run in CI |

## 12. Sources

All read on 2026-11-26.

- S1 nRF52840 product page, "64 MHz Cortex-M4 with FPU", "1 MB Flash, 256 KB
  RAM": https://www.nordicsemi.com/Products/nRF52840
- S1a nRF5340 product page, "128/64 MHz Arm Cortex-M33 application processor with
  1 MB Flash & 512 KB RAM", "64 MHz Arm Cortex-M33 network processor with 256 KB
  Flash & 64 KB RAM": https://www.nordicsemi.com/Products/nRF5340
- S2 Apache NimBLE repository, "an open-source Bluetooth 5.4 stack (both Host &
  Controller) that completely replaces the proprietary SoftDevice on Nordic
  chipsets", controller supports nRF51, nRF52 and nRF5340:
  https://github.com/apache/mynewt-nimble
- S3 LovyanGFX repository description and supported list ("ESP32 SPI, I2C,
  8bitParallel / ESP8266 SPI, I2C / ATSAMD51 SPI", "ArduinoESP32 and ESP-IDF are
  supported"): https://github.com/lovyan03/LovyanGFX and
  https://raw.githubusercontent.com/lovyan03/LovyanGFX/master/README.md
- S4 nRF Connect SDK releases page, latest v3.3.5:
  https://github.com/nrfconnect/sdk-nrf/releases
- S5 Zephyr releases page, latest v4.3.2:
  https://github.com/zephyrproject-rtos/zephyr/releases
- S6 Adafruit nRF52 Arduino BSP releases page, latest 1.7.0:
  https://github.com/adafruit/Adafruit_nRF52_Arduino/releases
- S7 Adafruit nRF52 BSP README, board list and "calling into the Nordic
  SoftDevice radio stack":
  https://raw.githubusercontent.com/adafruit/Adafruit_nRF52_Arduino/master/README.md
  and the same wording on product 4062, https://www.adafruit.com/product/4062
- S7a LilyGO T-Echo product page, "MCU NRF52840", "FLASH 2MB", "RAM 2MB",
  1.54 inch SPI e-paper, 850 mAh:
  https://lilygo.cc/products/t-echo-lilygo
- S8 `sandeepmistry/arduino-nRF5` README, SoftDevice selection and the absence of
  an Arduino BLE API (the latter from the repository description returned by
  search: "This Arduino Core does not contain any Arduino style API's for BLE
  functionality"): https://github.com/sandeepmistry/arduino-nRF5
- S9 ArduinoCore-mbed releases page, latest 4.6.0:
  https://github.com/arduino/ArduinoCore-mbed/releases and the Nano 33 BLE Sense
  Rev2 page, "build upon the nRF52840 microcontroller and runs on Arm Mbed OS":
  https://docs.arduino.cc/hardware/nano-33-ble-sense-rev2/
- S10 `h2zero/n-able-Arduino` README, "No softdevice, NimBLE provides a complete
  BLE stack", "Only the release version 1.4.0 and above of NimBLE-Arduino
  supports Arm devices", supported board list including Adafruit CLUE, Feather
  nRF52840 Express and Sense, and XIAO nRF52840 Sense:
  https://github.com/h2zero/n-able-Arduino
- S11 NimBLE-Arduino repository and the master headers `NimBLEDevice.h`,
  `NimBLEScan.h`, `NimBLEClient.h`, `NimBLERemoteCharacteristic.h`,
  `NimBLERemoteValueAttribute.h`, `NimBLEUtils.h`, `NimBLEConnInfo.h`,
  `NimBLEAttValue.h`; releases page latest 2.5.2:
  https://github.com/h2zero/NimBLE-Arduino and
  https://raw.githubusercontent.com/h2zero/NimBLE-Arduino/master/src/NimBLERemoteValueAttribute.h
- S12 LVGL repository README, "LVGL has no external dependencies",
  "For a typical UI, you need only ~100kB RAM, ~200-300kB flash, and a buffer
  size of 1/10": https://github.com/zephyrproject-rtos/lvgl (the LVGL fork used
  as a Zephyr module, whose README carries the same text as the upstream)
- S13 `zephyrproject-rtos/lvgl`, a Zephyr compatible LVGL module:
  https://github.com/zephyrproject-rtos/lvgl
- S14 nRF Connect SDK `west.yml`, `lvgl` in the Zephyr `name-allowlist`:
  https://github.com/nrfconnect/sdk-nrf/blob/main/west.yml
- S15 `h2zero/platform-n-able`, "This platform aims to provide the use of the
  NimBLE-Arduino library for Nordic and other ARM based devices that are
  supported by the underlying NimBLE stack, using the n-able Arduino core":
  https://github.com/h2zero/platform-n-able (description returned by search; the
  repository README could not be read directly)
- S16 `platformio/platform-nordicnrf52` `platform.json`, version 11.0.0,
  `framework-zephyr` at `~3.40402.0`, `framework-arduinoadafruitnrf52` at
  `~1.10700.0`, `framework-arduino-mbed` at `~4.6.0`, `framework-arduinonordicnrf5`
  at `~1.800.0`:
  https://raw.githubusercontent.com/platformio/platform-nordicnrf52/develop/platform.json
- S17 nrfxlib SoftDevice Controller documentation, "an RTOS-agnostic library
  built for the Nordic Semiconductor nRF52 and nRF53 Series":
  https://nrfconnectdocs.nordicsemi.com/ncs/2.6.3/nrfxlib/softdevice_controller/README.html
  and the `CONFIG_BT_LL_SOFTDEVICE` Kconfig reference:
  https://developer.nordicsemi.com/nRF_Connect_SDK_dev/doc/PR-4798/kconfig/CONFIG_BT_LL_SOFTDEVICE.html
- S18 Adafruit TFT FeatherWing 2.4 inch 320x240 touchscreen for all Feathers,
  ILI9341 display, TSC2007 touch since the October 2023 redesign:
  https://www.adafruit.com/product/3315
- S19 nRF5340 DK product page, four buttons and four LEDs, Arduino Uno Rev3
  hardware compatible, 1.7 to 5.0 V supply, CR2032 holder:
  https://www.nordicsemi.com/Products/Development-hardware/nRF5340-DK
- S20 Adafruit CLUE pinouts, 1.3 inch 240x240 IPS TFT, "2-pin JST PH connector for
  a battery", "the CLUE does not have built in LiPoly battery charging":
  https://learn.adafruit.com/adafruit-clue/pinouts
- S21 nRF54L15 product page, "1.5 MB NVM and 256 KB RAM", 128 MHz Cortex-M33:
  https://www.nordicsemi.com/Products/nRF54L15
- S22 nRF54LM20A product page, "2 MB NVM and 512 KB RAM", 128 MHz Cortex-M33:
  https://www.nordicsemi.com/Products/nRF54LM20A
- S23 Seeed XIAO nRF52840 wiki, nRF52840 Cortex-M4 with FPU at 64 MHz, onboard
  2 MB flash, battery charge and discharge management, PDM mic and LSM6DS3TR-C
  on the Sense:
  https://wiki.seeedstudio.com/XIAO_BLE/
- S24 Seeed Wio Tracker L1 Pro product page, nRF52840 "1 MB flash and 256 kB
  RAM", 1.3 inch OLED, 2000 mAh, 1.0 mm 2-pin JST battery interface, $47.90:
  https://www.seeedstudio.com/Wio-Tracker-L1-Pro-p-6454.html
- S25 Adafruit Feather nRF52840 Express product page, "1MB flash and 256KB SRAM",
  JST LiPo connector and built-in charging, 2 MB QSPI flash:
  https://www.adafruit.com/product/4062
- S26 Adafruit Feather nRF52840 Sense product page, $44.95:
  https://www.adafruit.com/product/4516

In-tree sources, read at `81139818`: `tools/portable_core_manifest.txt`,
`tools/check_portability_inventory.py`, `tools/size_budget.json`,
`src/idf_component.yml`, `dependencies.lock`, and the object sizes under
`.pio/build/m5stick-s3`.

## 13. UNVERIFIED

Each item is a claim this plan needs and no readable page settles. None is
guessed in the body; each is marked in place.

**Item 1 is the one that matters most.** The BLE stack footprint on nRF52840 is
the single number that decides between the two flip conditions in the front
matter, and no page I could read states it: Nordic's product page does not
discuss stack footprints, the `n-able` and `NimBLE-Arduino` pages give no
figure, and the ESP32 numbers are a different port on a different controller.
Until task 7 measures it on the target, the recommendation rests on a
pessimistic assumption rather than a measurement.

1. The flash and RAM footprint of an Apache NimBLE host plus controller on
   nRF52840. No vendor page gives one, and the nRF52840 product page does not
   discuss stack footprints. The ESP32-S3 numbers (128,398 bytes of ESP-IDF
   NimBLE host, 74,633 bytes of `esp-nimble-cpp`) are a different port and a
   different controller, and are not a substitute.
2. The footprint of the `n-able` core, its FreeRTOS configuration, and its boot-
   or no-bootloader layout.
3. Cortex-M4 code density against Xtensa for `lib/furble`. The 118,582 bytes are
   measured on ESP32-S3 only.
4. Whether the nRF controller accepts `NimBLEDevice::setMTU(517)` or negotiates
   lower, which is DJI's requirement.
5. Whether Ricoh, Sony, Canon, DJI, Lumix and Nikon pairing succeeds on an nRF
   controller with the same IO-capability, key-distribution and RPA settings the
   tree uses for ESP32. Only Fujifilm has hardware here.
6. Whether "the Zephyr Bluetooth host is derived from the NimBLE host" is stated
   by a page I could read. `docs.zephyrproject.io` refused every automated read,
   so the plan does not assert it. The decision does not rest on it, because NCS
   ships Nordic's SoftDevice Controller `[vendor S17]` and the Zephyr host API is
   C, which is the fact that matters for `lib/furble`.
7. The nRF5340 DK price. The Digi-Key pages returned no readable price.
8. The Adafruit CLUE and Feather prices other than the two quoted, and the price
   of every other board in section 6.
9. The Arduino Nano 33 BLE battery provision. The Arduino documentation page
   states the SoC and Mbed OS and says nothing about a battery connector.
10. Whether `n-able` supports the plain XIAO nRF52840. Its README lists the Sense
    variant only.
11. Whether a HolyIoT board with a display and a battery exists. Only marketplace
    listings were readable and none documented a display.
12. LVGL's real flash and RAM cost with furble's widget set and fonts. The 200 to
    300 KB figure is LVGL's own "typical UI" claim.
13. Whether the nRF54L15 or nRF54LM20A runs LVGL plus a camera BLE stack at all,
    in any configuration. No board with either part and a display was readable,
    and n-able does not list them.
14. The measured BLE-connected current of any nRF board in furble. Plan 318's
    figures are Espressif's example numbers, not a furble measurement, and this
    plan adds no measurement.

## Implementation state

Research and decision only. No firmware environment, board profile, `ports/`
directory, driver, test or purchase. No size figure in section 2 is a nRF figure.
The plan is a dated snapshot at master `81139818` and is expected to be re-read
against the tree before any task in section 10 starts.
