# 287 - MCU portability, battery life, GPS quality, and input changes

Status: research and hardware survey. This change adds no board, firmware
target, or support claim. The buyable hardware data is in
[`docs/hardware-catalog.md`](../docs/hardware-catalog.md).

Checked against `origin/master` at `7383561a` on 2026-11-01. No hardware was
purchased and no current measurement was taken for this document.

## Why this exists

Plans 40, 41, 88 and 127 answered separate questions. Plan 40 asked whether a
Nordic Meshtastic board could run furble. Plan 41 asked which ESP32 sidecar to
buy. Plan 88 asked which remote with a screen has the right inputs. Plan 127
defined the contract a Nordic port would have to satisfy.

This document answers the question that actually blocks a purchase: the two
current pain points are a GPS that only acquires a lock outdoors and battery
life, so which hardware and which port improve those two things, and what does
the port cost everywhere else.

The short answer is uncomfortable for a porting project:

- A different MCU does not fix the GPS. The antenna, the receiver class, the
  backup rail, the assistance source, and the phone fallback do.
- A different MCU barely fixes battery life. The cell capacity, the display,
  the GNSS duty cycle, and deep sleep do. The MCU idle floor is the smallest
  of the four levers.
- The cheapest useful change is a different board with the same ESP32-S3, not
  a different architecture.
- A non-ESP32 port is justified only as a separate, headless, low-power product
  with a reduced feature set. It is not a path to the current product on
  another chip.

## Executive summary

| Question | Answer |
| :--- | :--- |
| Will a port fix the outdoor-only GPS? | No. A better antenna, an M10-class receiver with a backup rail, and phone or WiFi assistance do. The board matters only because it decides which of those are possible. |
| Will a port fix battery life? | Only at the margin. The 250 mAh cell and the 41 mA display dominate. A 2000 mAh board is eight times the runtime before any firmware change. |
| Cheapest way to improve both | Buy the T-Beam Supreme L76K trim, the ThinkNode M5, or the T-Deck Plus. Add an external active antenna. Keep GNSS in standby instead of cutting the rail. Enable the phone location path. |
| Cheapest way to improve GPS only | M5 Unit-GPS with the SMA active antenna, which is already supported. Then enable assisted start and fix hold. |
| Best full-feature port target | ESP32-S3 or ESP32-P4 plus C6. Same ESP-IDF, same NimBLE API, same `lib/furble`. |
| Best non-ESP32 target | nRF54LM20A with Zephyr and a NimBLE-shaped facade. Headless, reduced feature set, 4 to 6 months of work. |
| Worst outcome | Porting the 10,324-line touch UI and the 11,613-line NimBLE camera library to nRF52840 to save 1.6 mA. |

## 1. The two pain points

### 1.1 GPS that only locks outdoors

#### What the current stack does

The reference receiver is the M5Stack Unit GPS v1.1 with an AT6668. The repo
records these facts:

- The module draws 31.64 mA at 5 V and has no backup supply. A rail cut costs a
  cold start of about 108 seconds.
- The built-in ceramic patch is marginal indoors. The SMA variant with an
  external active antenna is the documented fix for weak reception.
- The firmware already implements `$PCAS` detection, baud probing, sentence
  pruning, constellation selection, a power policy with rail cut and standby,
  a duty cycle, a fix hold, extrapolation, and a tier 2 ephemeris cache.
- The companion service already exposes a location characteristic
  (`b57f4f5f-087b-4740-b71d-8262cf26ebbc`). A fresh companion fix is used when
  the wired receiver has no fresh fix. The phone path exists in both companion
  apps.

So the software is not the missing piece. The missing pieces are physical and
operational. The apps already expose the phone location toggle: Android has an
enable switch, an interval that defaults to 10 seconds, and a count of fixes
sent. Apple uses CoreLocation with a when-in-use prompt. The iOS target did not
declare the location background mode, so a fix stopped when the app was
backgrounded. This PR adds the mode and the background update flag as a quick
win. Android has no foreground location service yet, so continuous background
delivery is still open there. Both are app lifecycle work, not a port.

#### Why it still fails indoors

Open-sky satellite signals arrive at about -130 dBm. A modern receiver
acquires cold at around -148 dBm and tracks at -160 to -167 dBm. Building
attenuation is 20 to 40 dB, and the patch inside a small plastic case has a
poor ground plane. No receiver class solves that. The options are:

1. More antenna gain and a better ground plane. This is the only passive fix
   that changes the link budget in a meaningful way.
2. A better receiver. The M10 class tracks at -167 dBm instead of -162 dBm and
   has a much better hot start. It buys margin, not penetration.
3. Assistance. A known position and time, or predicted ephemeris, removes the
   time to first fix but not the need to hear satellites.
4. A non-GNSS source. The phone, a WiFi scan, or the last known fix. This is
   the only path that works deep indoors.

#### Fix ladder

Ordered by effect per euro and per week of work.

| Fix | Hardware cost | Software cost | Expected effect | Verdict |
| :--- | :--- | :--- | :--- | :--- |
| External active antenna on the existing SMA unit | 10 to 25 EUR | none | The largest single improvement for weak and indoor reception. The patch is the weak point. | Do this first. |
| Enable assisted start and fix hold | none | settings only | Faster repeat fixes and fewer visible dropouts. The ephemeris path is already implemented. | Do this with the antenna. |
| Keep the receiver in standby instead of cutting the rail | board must expose standby or backup | small board profile change | Hot start instead of a 108 second cold start. L76K standby is 360 uA. M10 backup is 28 uA. | Do this on the next board. |
| M10-class receiver, MIA-M10Q or MAX-M10S | 10 to 25 EUR module, or a board that has one | a UBX driver, about 2 to 4 weeks | -167 dBm tracking, 25 to 37 mW, 1 second hot start, 1 second AssistNow. This is the best receiver upgrade. | Strong second step. |
| Phone location as a first-class fallback | none | 1 to 2 weeks to document, test, and keep the app sending | A fix indoors, where GNSS cannot work. The characteristic already exists. | Do this before any port. |
| WiFi scan positioning | ESP32 has WiFi, LR1110 boards have a scanner | 3 to 6 weeks plus a cloud or offline database | Coarse indoor position, 20 to 100 m. Privacy and network dependency. | Only if phone location is not acceptable. |
| L1 and L5 dual band, NEO-F10N or UC6580 | 16 to 40 EUR module | a new driver, about 2 to 4 weeks | Better urban multipath and accuracy. It roughly doubles GNSS power and does not fix deep indoors. | Only for urban accuracy, not for the indoor complaint. |
| Different MCU | a new board | 2 to 9 months | No change to the antenna or the satellites. At best, a different receiver on the new board. | Not a GPS fix. |

#### Receiver comparison

The full table is in the catalog. The decision summary:

| Receiver | Class | Power | Hot or aided fix | Indoor margin | Recommendation |
| :--- | :--- | :--- | :--- | :--- | :--- |
| AT6668 | `$PCAS` | 31.64 mA at 5 V | cold only after a rail cut | baseline | Keep as the reference. Add the SMA antenna. |
| L76K | `$PCAS` | 41 mA, 360 uA standby | 5.5 s hot | similar to AT6668 | The best drop-in for a board profile. It adds standby and a rail switch. |
| MIA-M10Q or MAX-M10S | UBX | 25 to 37 mW, 28 uA backup | 1 s hot, 1 s AssistNow | about 5 dB better tracking | The best receiver upgrade. Needs a driver. |
| NEO-F10N | UBX | about twice L1 | 2 s hot | better urban multipath, not deep indoor | Only if urban accuracy is the goal. |
| UC6580 | `$CFG` | vendor figure, verify | vendor figure, verify | dual band | New driver and more power. Not first. |

The single most important sentence in this section: a receiver upgrade buys
margin, and an antenna buys margin, but only an assisted source buys an indoor
fix. furble already has an assisted source. It needs to be used and documented.

### 1.2 Battery life

#### The current numbers

The repo power audit gives an ESP32-S3 model that this document uses as the
anchor:

| State | Current |
| :--- | ---: |
| Screen on, menu idle | 81.2 mA, of which 41.0 mA is the display |
| BLE connected, screen off, default | 43.6 mA |
| BLE connected, screen off, `SLEEP_CONN` | 3.6 mA |
| Screen off, no connection | 0.25 mA |
| GPS active, model | 21.5 to 23.0 mA |
| GPS module at 5 V, measured | 31.64 mA |
| ESP32 modem sleep | 14.1 mA |
| ESP32-S3 light sleep, main crystal | 3.3 mA |

The battery in the reference StickS3 is 250 mAh. That is the real problem. The
device is not a low-power design with a small cell. It is an 81 mA screen and a
43 mA connected idle with a 250 mAh cell.

One board detail matters more than the MCU: the 32.768 kHz crystal. Experiment
A is resolved: the 2026-08-16 boot test found no external 32.768 kHz crystal on
the StickS3. The 230 uA BLE light sleep mode is therefore unreachable on the
reference board, and 3.3 mA is the floor. A new candidate board with the crystal
can reach 230 uA, a 14x improvement in the screen-off connected state. Verify
the crystal on every new board. It is a board feature, not an MCU feature.

#### The levers, ranked

| Lever | Change | Effect on runtime | Effort |
| :--- | :--- | :--- | :--- |
| Cell capacity | 250 mAh to 2000 mAh | 8x | Buy a different board. |
| Cell capacity | 250 mAh to 3400 mAh | 13.6x | Buy a T-Beam and an 18650. |
| Display | LCD on to e-paper or screen off | 2x with the screen on, no change with it off | Board choice plus an e-paper flush driver. |
| GNSS duty | Continuous to 25 percent | about 3x in a sidecar | Already implemented. Tune the interval and keep the ephemeris. |
| Deep sleep between shots | 3.6 mA to 10 uA | 360x between frames | Plan 19, not implemented. Conflicts with the watchdog plan. |
| Better receiver | AT6668 to M10 | about 1.5x of the GNSS budget | New driver. |
| MCU floor | ESP32-S3 3.6 mA to nRF54 2 mA | 1.8x with everything else off, under 4 percent with GNSS on | 4 to 6 months. |

The MCU is last for a reason. If the device is a camera remote with the screen
on, the screen is 41 mA and the MCU is 40 mA. If it is a sidecar with GNSS on,
GNSS is 40 mA and the MCU is 3.6 mA. Only a device that spends most of its life
with the screen off and GNSS off benefits from a lower MCU floor, and that is a
different product from the current remote.

#### Runtime estimates

The catalog has the full table. Representative results, with a 0.85 derating.
The ESP32-S3 rows assume `SLEEP_CONN` on; the default is 43.6 mA screen-off
connected. The catalog also shows the default StickS3 row and a separate M10
table. The T-Deck Plus, T-Deck Pro and T-Watch rows below use the common 40 mA
load; the catalog's M10 table shows the receiver-specific figures, for example
about 109 h for the T-Deck Plus in B.

| Device | Cell | Screen off BLE | Sidecar GNSS on | Sidecar GNSS 25 percent | Screen on 25 percent | Screen and GNSS on |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: |
| M5StickS3, `SLEEP_CONN` on | 250 mAh | 59 h | 4.9 h | 15.6 h | 9.2 h | 1.8 h |
| M5StickS3, default `SLEEP_CONN` off | 250 mAh | 4.9 h | 2.5 h | 4.0 h | 4.0 h | 1.8 h |
| CoreS3 | 500 mAh | 118 h | 9.7 h | 31.2 h | 18.5 h | 3.5 h |
| T-Deck Plus | 2000 mAh | 472 h | 39.0 h | 125 h | 73.9 h | 14.0 h |
| T-Watch Ultra | 1100 mAh | 260 h | 21.4 h | 68.8 h | 46.2 h | 8.5 h |
| ThinkNode M5, e-paper | 1200 mAh | 283 h | 23.4 h | 75.0 h | 80.0 h | 12.7 h |
| T-Beam Supreme | 3400 mAh | 803 h | 66.3 h | 213 h | 190 h | 32.0 h |
| T-Echo Plus, e-paper, nRF52 | 2400 mAh | 816 h | 46.9 h | 160 h | 777 h | 46.4 h |

Scenario B has the screen off, so it does not show a display saving. The
T-Echo Plus lasts twice as long as the ThinkNode M5 because it has twice the
cell. At equal capacity the two are within a few percent, because the 40 mA
GNSS receiver dominates. The display effect shows in D and E: at equal 2000
mAh, the e-paper ThinkNode M5 would model at about 133 hours in D against 74
hours for the LCD T-Deck Plus, and about 21 hours in E against 14 hours. The
MCU effect shows in A, where the nRF52 is about 1.4 times the ESP32-S3. Neither
matches the factor that a larger cell gives.

## 2. What the stack costs to port

### 2.1 Inventory

The numbers are from this tree, not from a plan estimate.

| Area | Lines | Platform coupling |
| :--- | ---: | :--- |
| Camera library, `lib/furble` | 11,613 | 53 files. Arduino, ESP-IDF, FreeRTOS, NimBLE. 25 distinct NimBLE types and classes in the production tree. |
| Declared portable core | 2,351 | 10 files in `lib/furble/protocol` plus the manifest. The only code with a portability guarantee today. |
| App UI and generated icons | 17,338 | LVGL 9 and M5Unified. `FurbleUI.cpp` alone is 10,324 lines and has 127 M5 calls. |
| App control and state | 2,508 | FreeRTOS queues and ESP timers. |
| GPS | 3,992 | ESP-IDF UART, `$PCAS`, TinyGPSPlus, rail and standby policy. |
| Settings and persistence | 1,604 | ESP NVS through a Preferences wrapper. |
| Power and platform | 1,425 | `esp_pm`, M5Unified, M5PM1, PMIC, ADC. |
| Companion app service | 2,650 | NimBLE server, mbedTLS HMAC, NVS. |
| OTA | 2,075 | `esp_ota_ops`, `esp_partition`, MQTT transport. |
| Network | 2,975 | `esp_wifi`, lwIP, `esp-mqtt`, W5500 Ethernet. |
| SD and GPX | 1,262 | SDSPI, FATFS. |
| Sensors, feedback, IR | 1,836 | M5Unified, RMT, I2C. |
| Console | 2,957 | ESP console, USB Serial/JTAG. |
| Simulator | 14,066 | 34 shim headers, SDL, 215 scenarios. |
| Host tests | 35,699 | 103 C++ test files. |

Production files with platform dependencies: 46 include ESP-IDF headers, 48
mention NimBLE, 23 use M5, 11 use LVGL, 16 use FreeRTOS. The portability checker
prints the same conclusion:

```
camera library direct coupling: Arduino, ESP-IDF, FreeRTOS, NimBLE
app-layer direct coupling: Arduino, ESP-IDF, FreeRTOS, LVGL, M5Unified, NimBLE
portable contract: clean
```

The flash budget is the other hard wall. The M5StickS3 release image measured
1,264,496 bytes against a 1,740,800 byte OTA slot, 72.6 percent. The 4 MB Core
debug image measured 1,732,597 bytes before auth integration and 1,743,717
bytes after, over the 1,740,800 byte slot. A port that keeps the current feature
set needs at least two app slots, so about 4 MB of flash, and the full UI plus
WiFi plus MQTT is already close to 1.3 MB per slot.

### 2.2 Port strategies

| Strategy | What changes | What survives | Effort | Examples |
| :--- | :--- | :--- | :--- | :--- |
| S0, board profile on the same MCU | pins, PMIC, display, buttons | everything | 1 to 3 weeks for a headless board, 4 to 8 weeks for a new touch board | ThinkNode M5, T-Deck Plus |
| S1, another ESP32 family part | board profile plus a new target, same ESP-IDF and NimBLE | everything | 2 to 6 weeks | ESP32-C6, C5, H2 |
| S1P, ESP32-P4 plus a C6 companion | ESP-Hosted transport, two images to update, new power model | everything above IDF | 6 to 10 weeks | Tab5, T-Display P4 |
| S2, another MCU with a NimBLE-compatible facade | new BLE facade over Zephyr or a vendor stack, new settings, OTA, power, display | protocol bytes, GPS parser, UI layout, companion framing | 4 to 6 months headless, 8 to 12 months with UI | nRF54LM20A, STM32WBA65 |
| S3, vendor SDK rewrite | every HAL, every build, every test double | protocol bytes and test corpus | 6 to 9 months headless | STM32CubeWB, TI SimpleLink, Renesas FSP |
| S4, Linux and BlueZ | the whole BLE and lifecycle model | GPS parser and protocol bytes | 3 to 6 months, but a different product | Raspberry Pi Zero 2 W |

The estimates are planning estimates, not measured. They assume one experienced
embedded engineer and an existing board.

### 2.3 Why the BLE library is the wall

The camera library is not just "NimBLE calls in a few files". `Camera.h`,
`Device.h`, `Scan.h`, `CameraList.h` and every vendor header expose NimBLE
types. A non-NimBLE target has two choices:

1. Provide a NimBLE-compatible C++ facade over the target stack. It must cover
   25 types and classes: client, server, remote service and characteristic, address,
   advertised device, connection info, attribute value, scan, advertising,
   callbacks, security, bonding. This is roughly 4,000 to 6,000 lines and it
   must pass the same 215 simulator scenarios and 103 host test files.
2. Rewrite the vendor implementations against a new BLE interface. That touches
   all 53 camera files and 11,613 lines.

There is no third option that keeps the current source compatible without
either NimBLE itself or a facade that looks like it.

The public headers also leak ESP types. `esp_power_level_t` appears in
`Camera.h` and `Device.h`, and `BLE_HS_IO_*` constants appear in
`Camera::SecurityMode`. Those are small, but they prove the boundary is not
clean today.

Implementation state: this leak is removed. Port stack step 4 replaced both
with furble-owned `Furble::TxPower` and plain `SecurityMode` enumerators,
mapped to ESP-IDF and NimBLE values only in `Device.cpp`, `Camera.cpp` and
`FurbleControl.cpp`. See plans/127-nordic-port-baseline.md.

### 2.4 BLE feature contract

A chip that says "BLE 5" is not automatically a furble target. The camera
library uses these features in production today:

| Feature | Why furble needs it | Families that meet it |
| :--- | :--- | :--- |
| Central role plus GATT client | Every camera mode. | All BLE-capable parts in the matrix. |
| LE Secure Connections, legacy pairing, passkey and numeric comparison | Fujifilm Secure, Canon, and companion pairing. | ESP32, Nordic, ST, Silabs, TI, NXP, Infineon, Ambiq, Alif. |
| Bonding and bond persistence | Saved cameras reconnect without re-pairing. | Same. The store differs per stack. |
| Nine concurrent links | Multi-connect across cameras. | ESP32 NimBLE is configured for 9. Nordic and ST controller limits must be measured. Pico BTstack is lower. |
| 2M PHY | Optional. DJI Osmo sets a 517 byte MTU, which works over 1M. 2M improves throughput and latency. | ESP32-S3 and newer, Nordic, ST, Silabs, TI, NXP, Infineon, Ambiq, Alif. The original ESP32 has no 2M PHY. |
| Extended scanning | NimBLE is configured with `CONFIG_BT_NIMBLE_EXT_SCAN`. The scan path must handle extended reports. | BLE 5 parts. Controllers without extended scanning can still work if the cameras advertise legacy, but that must be tested. |
| Extended advertising | Not used. `CONFIG_BT_NIMBLE_EXT_ADV` is disabled. A target without it is acceptable. | Any BLE 5 part. The Pico 2 W CYW43439 controller firmware lacks it and that is not a blocker. |
| Connection parameter control | Fast connect, then a slow idle interval for power. | Most stacks. The API is not uniform. |
| Notification and indication | Camera state and companion responses. | All. |
| Privacy and resolvable addresses | Some cameras rotate addresses. | Most BLE 5 parts. The host must persist the identity key. |
| TX power control | The settings menu exposes TX power and adaptive TX. | ESP32 has a dBm API. Nordic and ST expose it differently. |

The practical consequence is that the BLE feature contract is mostly satisfied
by any modern BLE 5 part. The hard parts are the API shape, the nine-link
budget, and the persistence model, not the radio features.

### 2.5 What survives a port

| Component | Reuse | Work |
| :--- | :--- | :--- |
| Protocol codecs, `lib/furble/protocol` | Source compatible | None, keep the manifest gate. |
| Vendor protocol logic | Semantic reuse | Replace NimBLE types, or provide the facade. |
| TinyGPSPlus and NMEA parsing | Source compatible | UART adapter. |
| `$PCAS` command set | Source compatible | UART and power adapter. |
| Settings schema and wire IDs | Source compatible | NVS or Zephyr settings backend. |
| Companion framing and HMAC | Source compatible | GATT server adapter and crypto backend. |
| LVGL layout and widgets | Mostly source compatible | Display flush, input device, and font or size profiles. |
| OTA state machine | Source compatible | Image writer and bootloader adapter. |
| Simulator and host tests | Source compatible | New HAL doubles. This is the acceptance gate. |
| ESP Web Tools installer | No | UF2, MCUboot, or vendor DFU. |

### 2.6 Platform traps that follow the MCU

These are documented in `plans/95-engineering-lessons.md`. They are not
optional details. Each one is a bug that already cost time on the current
boards.

| Trap | Why it matters on a port |
| :--- | :--- |
| GPS UART clock source | With DFS and BLE modem sleep, an APB-clocked UART breaks. The GPS needs `UART_SCLK_XTAL`. A new MCU has a different clock tree. |
| GPS power lock and light sleep | The lock is S3-only today. On the other ESP32 boards, light sleep through NMEA loses sentences. A C6 or H2 port must port the lock before claiming the S3 battery number. |
| Display APB lock | The LCD backlight PWM flickers unless the display holds `ESP_PM_APB_FREQ_MAX`. A new panel driver must reacquire it after sleep. |
| Panel sleep dwell | ST7789 and ILI934x need a 120 ms dwell between SLPIN and SLPOUT. M5GFX does not enforce it. |
| PMIC first transaction | The M5PM1 fails its first I2C transaction after idle sleep. A new PMIC will have its own wake quirk. |
| Native USB reset on open | The S3 resets when the host opens the USB Serial/JTAG port. A port with a different USB stack changes the recovery and flashing flow. |
| PSRAM and DMA | Display buffers must stay in internal DMA-capable RAM. A port with PSRAM must repeat the allocation audit. |
| NVS per task | Every NVS transaction needs its own Preferences wrapper. A shared handle across tasks corrupts. |
| BLE bonds and partitions | Bonds live in NVS. A new partition layout or a new NVS backend loses saved cameras unless it is migrated. |
| Watchdog and PMIC recovery | The StickS3 watchdog and PMIC sequence are board specific. A new board needs an equivalent recovery path or a documented absence. |

## 3. The chip matrix

Scope: every part family sold as a BLE 5 central-capable MCU, plus the near
misses that people ask about. The verdict column says whether it can hold a
reduced furble. The long tail is listed after the tables with the reason it
fails.

### 3.1 Espressif

| Part | CPU | Radio | RAM | NVM | USB | BLE host | Toolchain | Port | Verdict |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| ESP32 | 2x LX6 240 MHz | BLE 4.2 plus BR/EDR, WiFi 4 | 520 KB | external | bridge | NimBLE, Bluedroid | ESP-IDF | S0 | Current 4 MB boards. No BLE 5 features. |
| ESP32-S3 | 2x LX7 240 MHz | BLE 5, WiFi 4 | 512 KB plus PSRAM | external | native | NimBLE | ESP-IDF | S0 | The reference. Best ecosystem fit. |
| ESP32-C3 | 1x RISC-V 160 MHz | BLE 5, WiFi 4 | 400 KB | external | Serial/JTAG | NimBLE | ESP-IDF | S1 | Cheap. No PSRAM, single core. Verify the UI budget. |
| ESP32-C5 | 1x RISC-V 240 MHz | BLE 5, WiFi 6 dual band | 384 KB | external | Serial/JTAG | NimBLE | ESP-IDF 5.5 or newer | S1 | 5 GHz is irrelevant to furble. Smallest RAM of the C family. |
| ESP32-C6 | 1x RISC-V 160 MHz | BLE 5.3, WiFi 6 | 512 KB | external | Serial/JTAG | NimBLE | ESP-IDF | S1 | The best cheap ESP32 alternative. Same RAM as the S3. |
| ESP32-H2 | 1x RISC-V 96 MHz | BLE 5.3, 802.15.4, no WiFi | 320 KB | external | Serial/JTAG | NimBLE | ESP-IDF | S1 | BLE only. Drops MQTT and the web installer's WiFi path. |
| ESP32-H4 | 2x RISC-V 96 MHz | BLE 5.4, no WiFi | 384 KB | external | Serial/JTAG | NimBLE | ESP-IDF 6.x | S1 | New. Only worth it for LE Audio, which furble does not use. |
| ESP32-P4 | 2x RISC-V 400 MHz plus LP | none on chip | 768 KB plus PSRAM | external | HS USB | NimBLE over ESP-Hosted | ESP-IDF 5.3 or newer | S1P | The display chip. Needs a C6 or C5 companion for radio. |
| ESP32-S31 | not checked | BLE 5.4 plus BR/EDR, WiFi 6 | not checked | external | yes | NimBLE | ESP-IDF 6.x | S1 | Too new to plan around. Recheck when boards ship. |

### 3.2 Nordic

| Part | CPU | Radio | RAM | NVM | USB | BLE host | Toolchain | Port | Verdict |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| nRF52832 | M4F 64 MHz | BLE 5 | 64 KB | 512 KB | no | Zephyr host or SoftDevice | NCS, Zephyr | S3 | Too small. |
| nRF52840 | M4F 64 MHz | BLE 5 | 256 KB | 1 MB | yes | Zephyr host or SoftDevice | NCS, Zephyr | S2 | Cannot hold the current image. Headless subset only. |
| nRF5340 | M33 128 plus M33 64 | BLE 5.3 | 512 plus 64 KB | 1 MB plus 256 KB | yes | Zephyr host plus SoftDevice Controller | NCS, Zephyr | S2 | Headless only. Dual-core complexity for little gain over the LM20A. |
| nRF54L05 | M33 128 MHz | BLE 5.4 | 96 KB | 500 KB | no | Zephyr | NCS | S3 | Too small. |
| nRF54L10 | M33 128 MHz | BLE 5.4 | 192 KB | 1 MB | no | Zephyr | NCS | S3 | Too small. |
| nRF54L15 | M33 128 MHz | BLE 5.4 | 256 KB | 1.5 MB | no | Zephyr | NCS | S2 | Best low-power headless candidate. Single-slot update only. |
| nRF54LM20A | M33 128 MHz | BLE 5.4 | 512 KB | 2 MB | HS USB | Zephyr | NCS | S2 | The best Nordic fit. Headless; two slots only if the image stays under about 950 KB. |
| nRF54H20 | M33 plus RISC-V | BLE 5.4 | 1 MB | 2 MB | HS USB | Zephyr | NCS | S2 | Overkill and expensive. |

### 3.3 STMicroelectronics

| Part | CPU | Radio | RAM | NVM | USB | BLE host | Toolchain | Port | Verdict |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| STM32WB55 | M4 64 plus M0+ 32 | BLE 5.2 plus 802.15.4 | 256 KB | 1 MB | yes | ST binary stack | CubeWB, Zephyr | S3 | Memory is too small for the full stack. |
| STM32WBA55 | M33 100 MHz | BLE 5.4 plus 802.15.4 | 128 KB | 1 MB | yes | ST binary stack | CubeWBA, Zephyr | S3 | Too small. |
| STM32WBA65 | M33 100 MHz | BLE 5.4 plus 802.15.4 | 512 KB | 2 MB | yes | ST binary stack | CubeWBA, Zephyr board | S2 | A credible second source. The radio stack is binary. |
| STM32U5 plus ST67W611M1 | M33 160 MHz plus companion | WiFi and BLE via companion | 786 KB | 2 MB | yes | companion stack | CubeU5 | S3 | Two chips and a companion firmware update. Not worth it. |

### 3.4 NXP, Silicon Labs, TI, Infineon

| Part | CPU | Radio | RAM | NVM | USB | BLE host | Toolchain | Port | Verdict |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| MCX W71 | M33 96 plus M3 radio | BLE 5.3 plus 802.15.4 | 128 KB | 1 MB | yes | NXP NBU firmware | MCUXpresso, Zephyr | S3 | 128 KB RAM rules it out. |
| MCX W72 | M33 plus radio | BLE 5.3 plus 802.15.4 | more than W71 | more than W71 | yes | NXP NBU | MCUXpresso, Zephyr | S2 | Recheck the memory if NXP is required. |
| RW612 | M33 260 MHz | WiFi 6, BLE 5.4, 802.15.4 | 1.2 MB | external | HS USB | NXP stack | MCUXpresso, Zephyr | S2 | Big RAM and WiFi. High power. |
| EFR32BG24 | M33 78 MHz | BLE 5.3 | 256 KB | 1.5 MB | not checked | Silabs binary stack | Simplicity, Gecko SDK | S2 | Headless only. No WiFi. |
| EFR32MG24 | M33 78 MHz | BLE 5.3 plus 802.15.4 | 256 KB | 1.5 MB | not checked | Silabs binary stack | Simplicity | S3 | Same memory as BG24, extra radio unused. |
| SiWG917 | M4 180 plus NWP | WiFi 6 plus BLE 5.4 | 192 to 320 KB | up to 8 MB | not checked | Silabs binary stack | Simplicity | S3 | The RAM is too tight for the UI. |
| CC2755R10 | M33 96 MHz | BLE 6.0 plus 802.15.4 | 162 KB | 1 MB | not checked | TI binary stack | SimpleLink, Zephyr | S3 | Too small. |
| CC2652R7 | M4F 48 MHz | BLE 5.2 plus 802.15.4 | 88 KB | 704 KB | yes | TI binary stack | SimpleLink | S3 | Too small. |
| CYW20829 | 2x M33 96 plus 48 | BLE 5.4 | 256 plus 96 KB | 1 to 2 MB | no | Infineon binary stack | ModusToolbox, Zephyr | S2 | Headless only. Good radio. |

### 3.5 Renesas, Ambiq, Alif, Raspberry Pi

| Part | CPU | Radio | RAM | NVM | USB | BLE host | Toolchain | Port | Verdict |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| RA4W1 | M4 48 MHz | BLE 5.0 | 96 KB | 512 KB | yes | Renesas binary stack | FSP, e2 studio | S3 | Too small. |
| DA14531 | M0+ 16 MHz | BLE 5.1 | 48 KB | 128 KB | no | Renesas binary stack | FSP | S3 | Too small. |
| Apollo4 Blue Lite | M4F 192 MHz | BLE 5.4 | 1.4 MB | 2 MB | yes | Ambiq binary stack | AmbiqSuite, Zephyr | S2 | Big RAM, no WiFi, niche toolchain. |
| Apollo4 Blue Plus | M4F 192 MHz | BLE 5.4 | 2.75 MB | 2 MB | yes | Ambiq binary stack | AmbiqSuite, Zephyr | S2 | The best display SoC outside Espressif on paper. 2 MB NVM limits updates. |
| Alif Balletto B1 | M55 160 MHz | BLE 5.x | 2 MB | 1.8 MB | yes | Alif stack | Alif SDK, Zephyr | S2 | Big memory and NPU. Complex boot and security tooling. |
| Alif Ensemble E1C | M55 160 MHz | no radio | 2 MB | 1.86 MB | yes | none | Alif SDK, Zephyr | S3 | Not a BLE part. Needs a companion. |
| RP2040 plus CYW43439 | 2x M0+ 133 MHz | BLE 5.2, WiFi 4 | 264 KB | 2 MB | yes | BTstack | Pico SDK | S3 | Too little RAM and a BTstack rewrite. |
| RP2350 plus CYW43439 | 2x M33 150 MHz | BLE 5.2, WiFi 4 | 520 KB | 4 MB | yes | BTstack | Pico SDK | S3 | Cheap and capable. The CYW43439 controller firmware lacks extended advertising, which furble does not use. The cost is the BTstack rewrite. |

### 3.6 Long tail

These are real parts, but one hard limit each makes them a poor furble target.

| Family | Parts | Why not |
| :--- | :--- | :--- |
| Bouffalo | BL616, BL618 | WiFi 6 and BLE 5.2, but the SDK and BLE host are immature for a central with nine links. |
| Realtek | RTL8720DN, RTL8721 | Ameba SDK, BLE 5, limited central examples, poor Zephyr support. |
| Telink | TLSR9518, TLSR9528 | Excellent low power, small RAM, vendor SDK, no WiFi. |
| WCH | CH582, CH592, CH32V | Small RAM and a thin BLE ecosystem. |
| Microchip | PIC32CX-BZ2, PIC32CX-BZ3 | MPLAB and Harmony, binary BLE stack, limited open CI. |
| ON Semiconductor | RSL15 | 80 KB RAM, BLE 5.2, no display class. |
| Atmosic | ATM33, ATM34 | Energy harvesting focus, small RAM, niche SDK. |
| Qualcomm | QCC743, QCC744 | WiFi and BLE combo aimed at audio and appliances, large closed SDK. |
| Espressif | ESP32-S2, ESP32-E22 | S2 has no BLE. E22 is a radio companion with no application CPU. |
| Nordic | nRF52832, nRF52810, nRF54LS05 | Too little RAM for the current stack. |

### 3.7 What the matrix says

- The only parts that keep the current code are Espressif. That is not a
  loyalty argument, it is the location of the NimBLE API, the ESP-IDF UART,
  NVS, `esp_pm`, and the OTA partition model.
- The ESP32-C6 is the best cheap alternative to the S3. Same 512 KB SRAM,
  BLE 5.3, WiFi 6, and a smaller and cheaper package. It trades the second core
  and native USB for cost.
- The ESP32-P4 is the best display alternative, but it is two chips and two
  images. It belongs in a large-screen product, not in a pocket remote.
- Nordic's advantage is the idle floor, not the feature set. The nRF54LM20A
  can hold a headless furble. It cannot hold the current UI, WiFi, MQTT, and
  dual OTA.
- Everything else needs a vendor SDK, a binary radio stack like the ESP32 has,
  and a new test harness. That is a second project.

## 4. Toolchains and existing software

### 4.1 Toolchain comparison

| Family | SDK | RTOS | BLE host | Central and GATT client | OTA | Display | Power | Flashing | Pinned build | CI fit |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| ESP32 | ESP-IDF 5.x | FreeRTOS | NimBLE or Bluedroid | yes | `esp_ota` | LVGL, M5GFX | `esp_pm` | esptool, web installer | yes, controller binary | Best. PlatformIO plus host tests. |
| Nordic | nRF Connect SDK | Zephyr | Zephyr host plus SoftDevice Controller | yes | MCUboot | LVGL module | Zephyr PM | nrfutil, UF2 on some boards | yes, controller binary | Good. West plus twister. |
| ST | STM32CubeWB or WBA | FreeRTOS | ST binary stack | yes | MCUboot or SBSFU | TouchGFX, LVGL | STM32 low power | CubeProgrammer | yes, stack binary | Fair. |
| Silicon Labs | Gecko SDK | FreeRTOS | Silabs binary stack | yes | Gecko Bootloader | LVGL | EM2 and EM4 | Simplicity Commander | yes, stack binary | Fair. |
| TI | SimpleLink SDK | FreeRTOS | TI binary stack | yes | OAD | LVGL | power driver | UniFlash | yes, stack binary | Fair. |
| NXP | MCUXpresso | FreeRTOS or Zephyr | NXP NBU | yes | MCUboot | LVGL | power driver | ISP, J-Link | partly, NBU binary | Fair. |
| Infineon | ModusToolbox | FreeRTOS | Infineon binary stack | yes | DFU | LVGL | power modes | KitProg | yes, stack binary | Fair. |
| Renesas | FSP | FreeRTOS or ThreadX | Renesas binary stack | yes | bootloader | LVGL | low power | e2 studio | yes, stack binary | Weak. |
| Ambiq | AmbiqSuite | FreeRTOS | Ambiq binary stack | yes | secure OTA | LVGL, MIPI DSI | SPOT | J-Link | yes, stack binary | Weak. |
| Alif | Alif SDK | Zephyr | Alif stack | yes | MCUboot | LVGL, MIPI DSI | PM | SETOOLS | partly | Weak. |
| Raspberry Pi | Pico SDK | FreeRTOS optional | BTstack | yes | UF2, no A/B | LVGL | sleep modes | BOOTSEL | yes | Good, but the BTstack rewrite is the limit. |
| Linux | BlueZ | Linux | BlueZ | yes | A/B | LVGL DRM | kernel PM | dd or image | yes | Good, but power is wrong. |

### 4.2 What furble would reuse

| Layer | ESP32 today | Nordic target | ST target | Linux target |
| :--- | :--- | :--- | :--- | :--- |
| BLE central | NimBLE | Zephyr host plus facade | ST binary plus facade | BlueZ plus facade |
| Settings | NVS | Zephyr settings | NVS or flash | file |
| Clock and timers | `esp_timer` | Zephyr kernel | STM32 HAL | POSIX |
| Power | `esp_pm`, PMIC | Zephyr PM | STM32 low power | kernel |
| OTA | `esp_ota` | MCUboot | MCUboot or SBSFU | A/B |
| Display | M5GFX, LVGL | LVGL plus panel driver | LVGL plus panel driver | LVGL plus DRM |
| Input | M5 buttons, touch | GPIO, touch, encoder, keypad | same | evdev |
| GPS | ESP UART | Zephyr UART | STM32 UART | tty |
| Companion | NimBLE server | Zephyr GATT server | ST GATT server | BlueZ GATT |
| Network | WiFi, lwIP, MQTT | optional, nRF70 or none | none | WiFi |
| Flashing | esptool and web | nrfutil, UF2 | CubeProgrammer | image |

The table is the reason a port is not a build system change. Every row above
the camera library is a new adapter, and every adapter needs a host double so
the simulator can still test the state machine.

### 4.3 Licensing and reproducibility

- ESP-IDF ships an open NimBLE host and a binary Bluetooth controller and PHY.
  The application image is reproducible with pinned binary inputs. The radio
  firmware cannot be rebuilt from source either. NimBLE is the host, not the
  whole radio implementation.
- Nordic's Zephyr and nRF Connect SDK follow the same model: the application
  image is reproducible, the SoftDevice Controller is a pinned binary.
- ST, Silabs, TI, NXP, Infineon, Renesas, Ambiq and Alif also ship a binary
  radio image. That is not a new disadvantage relative to ESP-IDF. The
  differences are licensing, tooling, and CI integration.
- A port should still pin every binary input and record its digest. Plan 127
  already requires a pinned west manifest and toolchain digest. The claim is a
  reproducible application image, not a rebuildable radio.

## 5. Battery model in detail

### 5.1 The model

```
average_mA = mcu_mA + display_mA + gnss_mA * gnss_duty + wifi_mA * wifi_duty
runtime_h = (cell_mAh * 0.85) / average_mA
```

Assumptions:

- The ESP32-S3 mcu figure is 3.6 mA with `SLEEP_CONN`, 40.2 mA with the display
  APB lock, and 43.6 mA with the default screen-off connection.
- The display is 41 mA for a 2.8 inch IPS at default brightness, 10 to 30 mA
  for the smaller panels, 0 for e-paper, and 80 to 200 mA for the 4 to 5 inch
  panels.
- GNSS is a generic 40 mA continuous load in every runtime row, including M10
  devices. The AT6668 and L76K are about 32 to 41 mA at the module, so this is
  representative. The M10 is 25 to 37 mW, about 8 to 12 mA at 3 V, so its rows
  are conservative. An L1/L5 part at 60 to 80 mA would reduce its rows. The
  receiver table in the catalog uses datasheet figures, not this generic load.
- WiFi is 100 mA when active and 0 when off. MQTT is a duty-cycled burst, not a
  continuous load.
- The 0.85 factor is a planning derating, not a measurement.
- The currents are mixed at their native rails. The AT6668 figure is at 5 V, the
  cells are at 3.7 V, and the model does not apply a boost conversion. The GNSS
  contribution is therefore a lower bound at the battery. The model also omits
  the UART wake and NMEA parse cost, cold-start inrush, display refresh peaks,
  and user behavior. It ranks options; it does not promise a runtime.

### 5.2 What the estimates mean

| Comparison | Ratio | Comment |
| :--- | ---: | :--- |
| 250 mAh to 2000 mAh | 8.0 | The largest single improvement available. |
| 250 mAh to 3400 mAh | 13.6 | The T-Beam with an 18650. |
| LCD screen on to screen off | 1.9 | 81.2 mA to 43.6 mA before light sleep. |
| Screen off default to `SLEEP_CONN` | 12.1 | 43.6 mA to 3.6 mA. Already merged, opt-in. |
| GNSS continuous to 25 percent duty | 3.2 to 3.5 | Depends on the MCU floor. |
| ESP32-S3 to nRF54 with GNSS on | 1.04 | 3.6 mA to 2.0 mA against 40 mA of GNSS. |
| ESP32-S3 to nRF54 with GNSS off | 1.8 | 3.6 mA to 2.0 mA. |
| Deep sleep instead of connected idle | 360 | 3.6 mA to 10 uA. Plan 19, not implemented. |

### 5.3 The honest conclusion

If the goal is a camera remote that lasts a day of shooting, buy a 2000 mAh
board and keep the screen off when idle. If the goal is a multi-day sidecar,
buy an e-paper board or a 3400 mAh 18650 board and duty-cycle the GNSS with
ephemeris retention. Neither goal needs a new MCU. Both goals need a different
board.

## 6. GPS in detail

### 6.1 What the current firmware already does

- `$PCAS` probe and configuration for AT6668 and L76K class receivers.
- Baud auto detection at 115200, 9600, 38400, 57600, 19200, and 4800.
- Constellation selection, sentence pruning, 1 to 10 Hz rate.
- Power policies: always on, standby, and rail cycle.
- Duty cycle with a minimum wake dwell.
- Fix hold and extrapolation.
- Assisted start with position and time, plus an optional ephemeris cache in
  NVS.
- A companion location characteristic that supplies an external fix.
- A source field that reports UART, companion, or none.

That is more GPS engineering than most commercial remotes have. The remaining
gap is hardware and antenna, not missing firmware.

### 6.2 What to change, in order

1. Antenna. Move from the small patch to an external active antenna on the SMA
   unit, or buy a board with an SMA and a real ground plane. No code change.
2. Standby and backup. Stop cutting the rail on every idle. Keep the receiver
   in `$PCAS12` standby on the AT6668, or use the L76K standby line, or use an
   M10 backup rail. The firmware already supports standby on the S3.
3. Assistance. Enable `GPS_ASSIST` with position and time, then with
   ephemeris. The code exists. Measure the TTFF improvement on the target
   board.
4. Phone fallback. Document the companion location path. Make sure the app
   sends location while the user is shooting, not only when the app is in the
   foreground. The firmware already prefers a fresh companion fix when the
   UART has no fix.
5. Receiver. Move to an M10-class module if the board allows it. This needs a
   UBX driver, but the payoff is 25 to 37 mW, -167 dBm tracking, and a one second hot
   start.
6. Dual band. Only after the above. L1/L5 improves urban multipath. It does
   not fix deep indoors.

### 6.3 What a port changes

A port changes the receiver only if the new board has a different receiver and
antenna. The ThinkNode M5 has an L76K, which is a sideways move from the
AT6668. The T-Beam Supreme L76K trim is the same. The T-Deck Plus and T-Watch
Ultra have an M10, which is a real upgrade. The Heltec Wireless Tracker V2 has
a UC6580, which is dual band but a new driver.

So the GPS recommendation is a board recommendation, not an MCU
recommendation. If the new board is an ESP32-S3 with an M10 and an SMA, the
port is a board profile and the GPS gets better. If the new board is an nRF52
with an L76K, the GPS is the same and the port is a rewrite.

## 7. UI and input changes

### 7.1 The current UI

- LVGL 9 with a 320 by 240 maximum canvas.
- Two DMA-capable internal buffers of 320 by 16 pixels, RGB565.
- Three encoder-style indev objects plus an optional pointer indev.
- M5 buttons: PWR, A, B, C, plus the Core2 touch panel.
- Themes, text sizes, focus ring, gesture handling, spin values, a spirit
  level, a bulb timer, an intervalometer, and a diagnostics audit page.
- `FurbleUI.cpp` is 10,324 lines. The UI files plus generated icons are 17,338
  lines.

The UI is not a thin layer. It is the largest single file in the project.

### 7.2 Input classes

| Input class | Examples | What LVGL needs | What furble needs |
| :--- | :--- | :--- | :--- |
| Touch only | Tab5, T-Display P4, Waveshare AMOLED, CoreS3 | pointer indev with a flush and a touch read | touch calibration, larger hit targets, no focus ring |
| Touch plus buttons | Core2, CoreS3, T-Watch, T-Deck | pointer plus encoder group | keep the existing focus model, map buttons |
| Keyboard | T-Deck, T-Deck Pro, ThinkNode M9, T-LoRa Pager, Cardputer | keypad indev, text area, focus group | shortcuts, text entry, tab order |
| Encoder | Dial, T-LoRa Pager, ThinkNode M5 knob | encoder indev plus group | acceleration, long press, select |
| Joystick | Wio Tracker L1 Pro | encoder or keypad | five-way mapping |
| Buttons only | T-Beam, T114, T-Echo, WisMesh, M5Stick | encoder group | remap A, B, C and long press |
| No display | ThinkNode M3, M4, M6, T1000-E | none | companion and console only, LED and buzzer feedback |

### 7.3 Per-device UI work

| Device | Display | Input | UI tier | Work |
| :--- | :--- | :--- | :--- | :--- |
| M5StickC, Plus, S3, Core2 | 320x240 or 240x135 | buttons, touch | U0 | None. Current UI. |
| M5Stack CoreS3 | 320x240 | touch, buttons | U1 | M5Unified knows the panel, but furble has no CoreS3 input callbacks or battery backend. Add the board profile plus input and power integration. |
| M5Stack Dial v1.1 | 240x240 round | touch, encoder | U2 | Round safe area, encoder mapping, new panel driver. |
| Waveshare AMOLED 1.75-G | 466x466 round | touch, one button | U2 | Round safe area, QSPI panel, CST9217 touch. |
| LilyGO T-Watch Ultra | 410x502 | touch, one button | U2 | Small-screen layout, watch gestures, AMOLED driver. |
| LilyGO T-Watch S3 Plus | 240x240 | touch, one button | U2 | Same as the Ultra, smaller panel. |
| LilyGO T-Deck Plus | 320x240 | touch, keyboard, trackball | U3 | Keyboard indev, trackball to encoder, text entry. |
| LilyGO T-Deck Pro | 320x240 e-paper | touch, keyboard | U3 and U4 | Keyboard plus e-paper flush. |
| Elecrow ThinkNode M9 | 240x320 | 37 keys | U3 | Keyboard indev and a portrait layout. |
| LilyGO T-LoRa Pager | 480x222 | keyboard, encoder | U3 | Keyboard plus encoder, wide layout. |
| Elecrow ThinkNode M5 | 200x200 e-paper | knob, three buttons | U4 | E-paper flush, no animation, partial refresh, inverted colors. |
| LilyGO T-Echo Plus | 200x200 e-paper | one button | U4 | Minimal status screen, e-paper. |
| LilyGO T-Beam Supreme | 128x64 OLED | three buttons | U1 | Small OLED layout, existing focus model. |
| Heltec T114 | 135x240 TFT | one button | U1 | Tiny layout, one-button navigation. |
| Seeed Wio Tracker L1 Pro | 128x64 OLED | joystick | U1 | Small layout, five-way mapping. |
| RAK WisMesh Pocket | 128x64 OLED | buttons | U1 | Small layout. |
| M5Stack Tab5 | 1280x720 | touch | U5 | Large layout, scale factors, 32 MB PSRAM frame buffers. |
| LilyGO T-Display P4 | AMOLED 4.1 inch | touch | U5 | Large layout, P4 PPA and PSRAM. |
| Waveshare P4 touch boards | 3.4 to 10.1 inch | touch | U5 | Large layout, ESP-Hosted input and display. |
| ThinkNode M3, M4, M6, T1000-E | none | one button | U6 | Headless, companion and console, LED and buzzer. |

### 7.4 UI tier definitions and effort

| Tier | Meaning | Effort |
| :--- | :--- | :--- |
| U0 | Existing 320x240 or 240x135 M5 layout. | none |
| U1 | Same layout, new panel and input backend, small screen. | 1 to 2 weeks |
| U2 | Round or watch layout, new touch and button mapping. | 2 to 4 weeks |
| U3 | Keyboard or encoder focus model, text entry, shortcuts. | 3 to 5 weeks |
| U4 | E-paper rendering, partial refresh, no animation. | 2 to 4 weeks |
| U5 | Large-screen layout, scaling, PSRAM buffers. | 3 to 5 weeks |
| U6 | Headless, no LVGL task, companion and console. | 1 to 2 weeks |

These add to the port effort. A T-Deck Plus port is an S0 board profile plus a
U3 UI: about 7 to 12 weeks, not the two weeks a headless board profile takes.
A ThinkNode M5 port is S0 plus U4: about 4 to 8 weeks. A Tab5 port is S1P plus
U5: about 9 to 15 weeks. That is the real reason the current stack has not
spread to more boards.

### 7.5 Proposed input mapping

The current UI assumes three focus buttons and an optional touch panel. The
portable mapping should keep that model and add only what the hardware has.

| Input | Focus or navigation | Select | Back | Shutter | Long press |
| :--- | :--- | :--- | :--- | :--- | :--- |
| Three buttons, current | A and B move focus | B | C | A on the home page | PWR opens power |
| Touch | tap a row | tap | on-screen back | on-screen shutter | long tap for help |
| Keyboard | arrows or Tab | Enter | Esc | S or Space | hold S for bulb |
| Encoder | rotate | press | long press or B | double press | hold for power menu |
| Joystick | up and down | press | left | right or press on home | hold for power |
| Watch | swipe | tap | swipe back | tap the shutter ring | side button for power |
| Headless | none | companion | companion | companion or one button | hold the button for power |

Two rules keep the UI honest across devices.

- The shutter is always reachable with one physical action. A touch-only device
  needs an on-screen shutter that does not scroll away, and a button-only
  device needs a home-page shutter without entering a menu.
- The back action is never only a gesture. Every device needs a physical or
  on-screen back control that works with gloves and in sunlight.

Keyboard shortcuts are a portability feature, not a desktop feature. The
T-Deck, T-Deck Pro, ThinkNode M9, T-LoRa Pager and Cardputer all have more keys
than the current UI uses. Mapping the common actions to keys avoids deep menu
diving on a device that is held like a phone.

### 7.6 What changes in the UI code

- A display HAL with width, height, flush, brightness, sleep, and wake. Replace
  the 127 direct `M5.Display` uses with the HAL.
- An input HAL with pointer, encoder, keypad, and button sources. Replace the
  `M5.Btn*` and `M5.Touch` reads with the HAL.
- A layout profile per panel: canvas size, safe area, font scale, row height,
  and whether animations are allowed.
- A power policy per panel: e-paper can sleep between updates, AMOLED needs a
  brightness curve, LCD needs a backlight PWM and an APB lock.
- Touch calibration storage for panels that need it. The Core2 path exists.
- Keyboard mapping and a text entry widget for the keyboard devices.
- A headless mode that does not start the UI task at all.

The good news is that the LVGL widget code and the menu model can stay. The
bad news is that the M5 calls are spread across the largest file in the tree,
so the HAL extraction is a real refactor, not a compile switch.

### 7.7 Companion app changes for the phone location fallback

The device firmware and the GATT protocol already accept a companion fix. The
gap is in the app experience.

- Both apps already put the phone GPS toggle on the session screen. Showing
  the last fix age and accuracy next to it is still open.
- Keep sending while the camera session is active. Android has no foreground
  location service today: `CompanionPresenceService` is a system-bound
  presence callback and explicitly never becomes a foreground service.
  Continuous background delivery needs a service with
  `foregroundServiceType="location"`, the matching permissions, a notification,
  and validation. iOS now declares the `location` background mode and sets
  `allowsBackgroundLocationUpdates` while the toggle is on. When In Use
  authorization is enough for a user-started session; Always is not required.
- Request precise location on Android 12 and newer. Approximate location is not
  good enough for geotagging.
- Both apps show whether the device is using the receiver or the phone. Android
  already did. iOS was updated in this PR to show `Receiver`, `Phone`, or
  `No fix` from `gps_source`.
- Add a "send one fix now" action for a user who is indoors and wants a single
  geotag without waiting for the receiver.

None of this needs a new characteristic. It is UI and lifecycle work in the
two companion apps, not a firmware change.

## 8. Hardware shortlist by pain point

The full catalog is in `docs/hardware-catalog.md`. This is the short version.

| Need | Pick | Why |
| :--- | :--- | :--- |
| Best GPS with an ESP32-S3 board profile | LilyGO T-Deck Plus or T-Watch Ultra | MIA-M10Q, -167 dBm tracking, 1 second hot start, 25 to 37 mW. Keeps the camera library, needs a UBX driver. |
| Best GPS with `$PCAS` reuse | Elecrow ThinkNode M5 or T-Beam Supreme L76K | L76K plus standby and a rail switch. No new GNSS driver. |
| Best battery, `$PCAS` reuse | T-Beam Supreme L76K with a 3400 mAh 18650 | 13.6 times the current cell, 66 hours of continuous GNSS. |
| Best battery, e-paper | Elecrow ThinkNode M5 | 23 hours of continuous GNSS, 75 hours at 25 percent duty. |
| Best battery, all features | T-Deck Plus with a 2000 mAh cell | 39 h continuous at the common 40 mA load, about 109 h with the M10's 12 mA; 125 h at 25 percent duty at 40 mA. |
| Best zero port | Core2 plus Unit GPS plus battery module | Works today. Touch, 1890 mAh with the module, no new code. |
| Best urban accuracy | NEO-F10N or another L1/L5 board | L1/L5 multipath rejection. The MAX-M10S trim is L1-only and does not qualify. Costs power and a new driver. |
| Best headless sidecar if a port is required | nRF54LM20A with an L76K or M10 | 2 mA idle, 2 MB NVM, 512 KB RAM, Zephyr. Still a 4 to 6 month port. |
| Best large screen | T-Display P4 or Tab5 plus GPS | ESP32-P4 plus C6, 4 to 5 inch touch, L76K or Grove GPS. |

## 9. Recommendation

### 9.1 Do these before any port

1. Buy the M5 Unit-GPS SMA active antenna, or the Unit GPS with the external
   antenna. Measure the fix rate indoors and in the camera bag.
2. Enable `GPS_ASSIST` with position and time, then with ephemeris. Measure the
   TTFF on the current board.
3. Use the companion location path while shooting. The iOS background mode is
   part of this PR. Confirm both apps send location and that the device reports
   `gps_source` as companion when the UART has no fix. Android background
   delivery still needs its foreground location service.
4. Measure the current draw of the current board in the four states that
   matter: screen on, screen off connected, GNSS on, and GNSS standby. The repo
   numbers are a model, not a bench measurement.
5. If the answer is still "not enough", buy a board with a larger cell and a
   display that can be off. The T-Beam Supreme L76K and the ThinkNode M5 are
   the two best value picks.

### 9.2 If a port is still wanted

Do it in this order:

1. A headless ESP32-S3 board profile for the ThinkNode M5 or the T-Beam L76K.
   This is 2 to 4 weeks including the GPS and power work, and it validates the
   board abstraction with the camera library untouched.
2. A T-Deck Plus profile with the M10 driver and the U3 UI. This is 7 to 12
   weeks and gives a full touch and keyboard remote with the best GPS.
3. A P4 plus C6 profile only if a large screen is the product. It is 9 to 15
   weeks and adds ESP-Hosted and a two-image update.
4. A non-ESP32 headless port only as a separate product. The nRF54LM20A is the
   best target. Budget 4 to 6 months for parity with the headless ESP32 build,
   plus the facade, the settings store, the OTA backend, the GPS adapter, and
   the power model. Do not port the UI in the same project.

### 9.3 What not to do

- Do not port to nRF52840 for the current feature set. The 1 MB flash and
  256 KB RAM cannot hold it.
- Do not port the full touch UI to a non-ESP32 part to save 1.6 mA. The display
  and the cell dominate.
- Do not buy a board for its LoRa radio. furble does not use it.
- Do not buy a u-blox board expecting `$PCAS` reuse. UBX is a new driver.
- Do not buy an e-paper board expecting the current animations to survive.
  E-paper needs a different UI tier.

## 10. Verification plan

Before any port PR, measure on the target board:

### GPS

- Cold start with the rail cut, and hot start with standby or backup.
- TTFF with and without `GPS_ASSIST`, at position and time, and with ephemeris.
- Fix rate in the open, in a bag, indoors near a window, and deep indoors.
- Satellite count and C/N0 for the AT6668, L76K, and M10 under the same sky.
- `$PCAS06` probe response on every `$PCAS` candidate.
- Current in tracking, standby, and backup.
- External antenna versus patch, same location and time.

### Power

- Screen on, screen off connected, screen off disconnected, GNSS on, GNSS
  standby, and deep sleep.
- BLE connected with 1, 3, and 9 links.
- WiFi on with MQTT idle and with a TLS publish.
- Display at minimum, default, and maximum brightness.
- Cold temperature if the device is for outdoor use.

### BLE

- Nine concurrent links, 247 and 517 byte MTU, and 2M PHY where available.
- Passkey and numeric comparison on every vendor that needs it.
- Bonding, rebonding, stale bond recovery, and reconnect backoff.
- Scan while connected, connect while scanning, and cancel during a connect.
- Camera notification latency after light sleep.

### UI and input

- Every input class on the target: touch, buttons, encoder, keyboard, joystick.
- Touch calibration after a power cycle.
- E-paper partial refresh and full refresh counts.
- Round safe area and font clipping at the smallest and largest text size.
- Headless boot with no display task and a companion-only session.

### Update and recovery

- OTA over WiFi, MQTT, and USB, with an interrupted write and a rollback.
- The P4 plus C6 two-image update, including a C6-only rollback.
- The bootloader recovery path for each target.

## 11. Open questions

- Does the T-Deck Plus touch controller have a mainline LVGL or LovyanGFX
  driver, and which panel variant ships now?
- Does the T-Display P4 ESP-Hosted BLE path hold nine links, and how is the C6
  image updated in the field?
- Is the ThinkNode M5 flash 4 MB or 8 MB? Vendor pages conflict.
- Which GNSS chip is in the ThinkNode M9, and does it answer `$PCAS`?
- Does the Waveshare LC76G answer any `$PCAS` command, or is it pure NMEA?
- Does the T-Beam Supreme L76K trim stay available in the EU?
- How reliable is the companion location path in the field, and can Android
  deliver it in the background without a dedicated foreground location service?
- What is the real cold-start current profile of the AT6668 and L76K on a
  3.7 V rail, including the 5 V boost loss?

## 12. References

Repo evidence:

- `tools/check_portability_inventory.py --check` output.
- `tools/portable_core_manifest.txt`, 10 files.
- `src/FurbleUI.cpp`, `src/FurbleGPS.cpp`, `src/FurblePlatform.cpp`.
- `sdkconfig.m5stick-s3`, NimBLE roles, 9 connections, 2M and coded PHY.
- `plans/implemented/07-ble-sleep.md`, `plans/15-gps-power.md`,
  `plans/98-power-optimization-audit.md`, `plans/106-flash-size-optimization.md`,
  `plans/127-nordic-port-baseline.md`.
- `docs/supported-hardware.md`, `docs/companion.md`,
  `docs/settings-and-controls.md`.

Hardware and silicon:

- ESP32 family comparison: https://www.espboards.dev/blog/esp32-soc-options
- ESP32-P4 and ESP-Hosted:
  https://www.espressif.com/en/products/socs/esp32-p4 and
  https://components.espressif.com/components/espressif/esp_hosted
- nRF54L15: https://www.nordicsemi.com/Products/nRF54L15
- nRF54LM20A: https://www.nordicsemi.com/Products/nRF54LM20A
- nRF5340: https://www.nordicsemi.com/Products/nRF5340
- STM32WBA65 data sheet:
  https://www.st.com/resource/en/datasheet/stm32wba65mi.pdf
- STM32WB55 data sheet:
  https://www.st.com/resource/en/datasheet/stm32wb55rg.pdf
- EFR32BG24: https://www.silabs.com/wireless/bluetooth/efr32bg24-series-2-socs
- MCX W71: https://www.nxp.com/products/MCX-W71
- CC2755R10: https://www.ti.com/product/CC2755R10
- CYW20829: https://www.infineon.com/part/CYW20829
- SiWG917: https://www.silabs.com/documents/public/data-sheets/siwg917-datasheet.pdf
- RW612: https://www.nxp.com/docs/en/data-sheet/RW612.pdf
- Apollo4 Blue Plus: https://ambiq.com/product/apollo4-blue-plus
- Alif Balletto B1:
  https://www.digikey.com/en/products/detail/alif-semiconductor/DK-B1/29296119
- RP2350 and Pico 2 W:
  https://www.raspberrypi.com/documentation/microcontrollers/raspberry-pi-pico.html
- Pico BTstack limitation:
  https://github.com/raspberrypi/pico-sdk/issues/2313
- Zephyr Bluetooth host roles:
  https://docs.zephyrproject.org/latest/connectivity/bluetooth/bluetooth-le-host.html
- NimBLE host: https://mynewt.apache.org/latest/network/ble_hs/ble_hs.html

GNSS:

- u-blox MIA-M10Q:
  https://content.u-blox.com/sites/default/files/documents/MIA-M10Q_DataSheet_UBX-22015849.pdf
- u-blox MAX-M10S:
  https://content.u-blox.com/sites/default/files/MAX-M10S_DataSheet_UBX-20035208.pdf
- u-blox NEO-F10N: https://www.u-blox.com/en/product/neo-f10n-module
- u-blox dual-band multipath white paper:
  https://content.u-blox.com/sites/default/files/documents/GPS-signals-migration-wp.pdf
- Quectel L76K: https://wiki.seeedstudio.com/get_start_l76k_gnss/
- M5Stack Unit GPS v1.1: https://docs.m5stack.com/en/unit/Unit_GPS_v1.1

## Implementation state

Research plus a small companion app quick win. No firmware source changed
in this plan's own PR. Later work removed the section 2.3 public header type
leak; see the port stack steps in plans/127-nordic-port-baseline.md.

- Added `docs/hardware-catalog.md` with the buyable hardware, the receiver
  classes, and the first-order runtime estimates.
- Added this plan with the stack inventory, chip matrix, toolchain matrix,
  battery model, GPS fix ladder, and UI and input tiers.
- No board, environment, firmware target, or support claim was added.
- No hardware was purchased and no current measurement was taken. Every
  runtime number is a model estimate, not a bench result.
- Prices were checked on 2026-11-01 and are vendor list prices, not offers.
- The next step is measurement on the current board, not a port.

### Quick win implemented in this PR

- iOS companion: the app now declares the `location` background mode, sets
  `allowsBackgroundLocationUpdates` and the system indicator while phone GPS is
  enabled, and keeps stationary updates flowing with
  `pausesLocationUpdatesAutomatically = false`.
- iOS companion: the status row now distinguishes a wired receiver fix from a
  phone fix instead of showing a generic "Available".
- `docs/companion.md` and the wiki Getting Started page document the phone
  location source, the `gps_source` values, and the remaining Android
  background gap.
- `tests/test_companion_location_contract.py` guards the plist, the provider
  flags, the status labels, the Android precise location permission, and the
  reference docs.
- Android already has the phone GPS toggle and the GPS source display. It still
  has no foreground location service, so continuous background delivery
  remains open.

### Review corrections

- AT6668: the missing piece is the backup rail, not standby or assistance. The
  firmware can send `$PCAS12` standby and CASIC aiding with the ephemeris cache.
- ThinkNode M3: the receiver is an ATGM336H, not an LR1110. The M4 receiver
  remains unnamed.
- CoreS3: M5Unified recognition is not a furble port. The board needs input
  callbacks and a battery backend before it can claim zero-port.
- Runtime estimates: the ESP32-S3 rows assume `SLEEP_CONN` on, and the default
  StickS3 row is now shown separately. The display comparison is made at equal
  capacity in screen-on scenarios, not in the screen-off sidecar scenario.
- Bangle.js 2 was added as a real `$PCAS` watch with an AT6558 and a touchscreen.
- 2M PHY is optional, extended scanning is used, and extended advertising is
  not. The Pico 2 W limitation is the CYW43439 controller firmware, not BTstack
  in general.
- ESP-IDF also ships a binary Bluetooth controller and PHY. The reproducible
  claim is about the application image with pinned binary inputs, not a
  rebuildable radio.
- The runtime model uses a generic 40 mA GNSS load. M10 rows are conservative.
- The MAX-M10S is L1-only and cannot satisfy the L1/L5 urban accuracy row.
