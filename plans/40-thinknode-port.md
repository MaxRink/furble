# 40 - ThinkNode port feasibility

Status: feasibility study. No code in this document.

## Goal

Run furble on an Elecrow ThinkNode device as a dedicated headless GPS sidecar.
The device carries its own GNSS receiver. It connects to a Fujifilm camera over
BLE and feeds it position and time for geotagging. It runs for days on battery.
Setup and interaction happen through the companion app from plans/50, because
the device has no screen.

## Motivation

furble today needs an M5Stack device plus a wired Grove GPS unit. That stack
works but it is a remote control with GPS bolted on. The ThinkNode devices are
the opposite shape: GNSS, battery, and BLE in a sealed pocketable case, built
to run unattended for long periods. If furble ran on one, the user would clip a
card to the camera strap and get geotags with zero interaction. The 7000 mAh
ThinkNode M4 in particular promises weeks, not hours.

The question this document answers: is that a port, or a rewrite.

## Hardware findings

All specifications below were checked against the Elecrow wiki, the Meshtastic
hardware documentation, and the Meshtastic firmware source. The BerryBase
listings confirm availability and battery figures but carry almost no technical
detail. One listing detail is misleading: the M3's "Wi-Fi" is the LR1110's
passive Wi-Fi scanning for geolocation, not an ESP32 Wi-Fi radio.

### ThinkNode M3 tracker card

- MCU: Nordic nRF52840, Bluetooth 5 LE. Not an ESP32.
- LoRa: Semtech LR1110 on SPI. Also provides Wi-Fi scan and GNSS-assisted
  geolocation features used by the LoRaWAN variant.
- GNSS: ATGM336H-5NR32 (AT6558 family), GPS/BDS/GLONASS/GALILEO, NMEA at
  9600 baud. The Meshtastic variant defines `PIN_GPS_EN`, `PIN_GPS_RESET` and
  `PIN_GPS_STANDBY`, so the receiver can be power gated and put in standby.
- Display: none. One RGB indicator LED.
- Input: one button (power on/off, SOS).
- Battery: 770 mAh lithium (Elecrow wiki says 760 mAh in one place). Magnetic
  pogo pin cable carrying USB, DFU, serial logging and charging.
- Runtime under Meshtastic: Elecrow quotes 12 to 18 hours.
- Case: 64 x 64 x 10 mm, 40 g, IP66, internal antennas.
- Sensors: temperature, humidity, accelerometer.
- Flashing: UF2 bootloader. Hold reset 20 s, an "ELECROWBOOT" drive appears,
  drag the .uf2 in. The Meshtastic firmware repo has a full board definition
  (`boards/ThinkNode-M3.json`: Adafruit nRF52 BSP, S140 SoftDevice v6.1.1) and
  variant with complete pin definitions
  (`variants/nrf52840/ELECROW-ThinkNode-M3/variant.h`). No schematics are
  published; the variant file is the de facto pinout.

### ThinkNode M4 powerbank

- MCU: Nordic nRF52840-QIAA, Bluetooth 5.4 LE advertised. Not an ESP32.
- LoRa: Semtech LR1110 on SPI, internal antenna.
- GNSS: multi-constellation (GPS, BeiDou, GLONASS, QZSS), ceramic antenna.
  Elecrow does not name the chip. Same driver family as the M3 is likely but
  unverified.
- Display: none. Status LEDs only.
- Input: touch power button plus a LoRa function button.
- Battery: 18650 pack, 7000 mAh nominal at 3.65 V, 25.2 Wh. Powerbank output
  5 V/3 A, USB-C in/out with PD/QC fast charge, Qi wireless output.
- Sensors: temperature, humidity, six-axis IMU.
- Flashing: same UF2 path as the M3 (hold reset 20 s, drag the .uf2). The
  official Meshtastic firmware ships `firmware-nrf52840-thinknode_m4` builds, so
  a supported board definition and variant exist. No schematics published. The
  powerbank charge and boost circuitry is a separate opaque subsystem with an
  unknown quiescent draw.

### ThinkNode M2, for completeness

The only ESP32 device in the ThinkNode family. ESP32-S3, SX1262, 1.3 inch
OLED, 1000 mAh, USB-C, about $22. It has no GNSS receiver. A furble port to the
M2 is technically straightforward but it cannot be a GPS sidecar, so it does
not meet the goal of this document.

## Feasibility verdict per device

### M3: no port. A rewrite, and the battery is marginal.

The premise that the M3 might be ESP32-family is wrong. It is an nRF52840.
There is no ESP-IDF, no esp-nimble-cpp, no NVS, no ESP-IDF UART driver, and no
ESP power management API on this chip. Nothing below `lib/furble`'s protocol
logic survives. See the M4 verdict for what a rewrite means; the two devices
share the same MCU, LoRa chip, bootloader and firmware ecosystem, so a working
M4 rewrite would cover the M3 almost for free.

The battery makes the M3 the weaker target anyway. The ATGM336H draws roughly
20 to 30 mA while tracking. Continuous tracking from 770 mAh is about 30 hours
before the nRF and the LR1110 draw anything. "Days" requires aggressive GNSS
duty cycling through `PIN_GPS_STANDBY`, and even then a week is the realistic
ceiling. The form factor is excellent. The energy budget is not.

### M4: no port. A rewrite, but the right hardware for the mission.

Same MCU family, same verdict: porting furble to the M4 means replacing the
entire platform layer and the entire BLE plumbing. Concretely:

- `lib/furble` is hard-coupled to esp-nimble-cpp. `Camera` inherits
  `NimBLEClientCallbacks` and owns a `NimBLEClient *`
  (`lib/furble/Camera.h:20,168`). A count across `lib/furble` finds roughly 25
  distinct `NimBLEClient` calls plus heavy use of `NimBLEUUID`,
  `NimBLERemoteCharacteristic`, `NimBLERemoteService`, `NimBLEAdvertisedDevice`,
  `NimBLEScan` and `NimBLEAddress`. On nRF52 the realistic stacks are the
  Adafruit nRF52 BSP with Bluefruit on the S140 SoftDevice (what Meshtastic
  uses on this board) or Zephyr with the nRF Connect SDK. Neither exposes the
  NimBLE C++ API.
- `FurbleSettings` is ESP-IDF NVS through a Preferences shim
  (`src/FurbleSettings.cpp`). Needs a storage backend swap.
- `FurbleGPS` is the ESP-IDF UART event driver plus TinyGPS++
  (`src/FurbleGPS.cpp`). The TinyGPS++ half is portable, the UART half is not.
- `FurblePlatform` is M5Unified plus `esp_pm` (`src/FurblePlatform.cpp`).
  Full rewrite by definition.
- `FurbleUI` (2136 lines of LVGL) is simply dropped for a headless device.
- `FurbleControl`'s logic is portable C++ over FreeRTOS queues and tasks. The
  Adafruit nRF52 core runs FreeRTOS, so this file ports nearly as-is on that
  stack. On Zephyr it needs a queue and thread shim.

What survives untouched or nearly untouched: the camera protocol logic itself.
The vendor files (`Fujifilm*`, `Sony`, `Nikon*`, `CanonEOS*`, `Ricoh`, about
2200 lines) are byte-level protocol code with no M5Unified and no LVGL in them
(verified by grep). Their only platform dependency is the NimBLE client API.
Put a thin BLE-central abstraction under them and they become portable. That
extraction is the one piece of this project that pays off even if no ThinkNode
firmware ever ships, because it is also what any future non-ESP32 target needs.

Why the M4 is worth a rewrite when the M3 is not: energy. 25.5 Wh is 33 times
the M3's budget. Even a sloppy 5 mA average gives about eight weeks. The
nRF52840's sleep currents (single digit microamps with RAM retention) fit the
always-paired sidecar pattern far better than any ESP32 light sleep mode. The
open risk is the powerbank subsystem's own quiescent draw, which must be
measured. USB-C instead of a proprietary magnetic cable also helps development.

Scale estimate, honestly: this is a second firmware, not a port. Roughly 2900
lines of furble survive behind the abstraction (vendor protocols, CameraList,
Control logic, TinyGPS++ handling). New code is the BLE abstraction backend,
platform layer, storage, GNSS driver, companion GATT service and sidecar loop,
call it 3000 to 4000 lines plus build system. As a hobby-pace effort: the
abstraction refactor is 2 to 4 weeks, the M4 firmware another 2 to 3 months to
something trustworthy with one camera vendor.

### Verdict summary

| Device | MCU | Port? | Meets "days on battery"? |
|---|---|---|---|
| M3 | nRF52840 | No. Rewrite. | Marginal, duty cycled only |
| M4 | nRF52840 | No. Rewrite, tractable via protocol extraction. | Yes, by a wide margin |
| M2 | ESP32-S3 | Yes, but no GNSS. | Not applicable |

No ThinkNode device is both ESP32 and GNSS-equipped. The dream target for a
true port (S3 module, GNSS, big battery, no screen) does not exist in this
family.

## Port plan

The plan has two tracks. Track A lands in the furble repository and is useful
upstream regardless of any ThinkNode outcome. Track B is a new sibling firmware
and only starts if track A lands and an M4 measures well.

### Phase 0 (track A): BLE-central abstraction in lib/furble

Scope: introduce a minimal interface set sized from the actual usage grep:
client (connect, disconnect, secureConnection, getService, setValue, getValue,
connection params, callbacks), remote service and characteristic (read, write,
subscribe), scanner and advertised device, UUID and address value types. Back
it with the existing esp-nimble-cpp so behavior on ESP32 is byte for byte
unchanged. This is a mechanical but wide refactor.

Files: every file in `lib/furble/` (Camera.h/.cpp, Device.*, Scan.*,
CameraList.*, all vendor files), no `src/` changes beyond includes.

Exit criteria: all five existing PlatformIO envs build, FauxNY passes, real
Fujifilm pairing and geotag regression on hardware.

### Phase 1 (track B): M4 board bring-up

Scope: new repository (working name furble-nrf). Stack decision: start with
the Adafruit nRF52 BSP under PlatformIO, because the Meshtastic board JSON and
variant already target it, the UF2 bootloader is already on the device, and
the BSP's FreeRTOS keeps `FurbleControl`'s queue and task code. Zephyr/NCS is
the fallback if Bluefruit's central-role security turns out insufficient (top
risk, see Risks). Bring-up items: LED, button, battery ADC, GNSS UART at 9600
with power gating, LR1110 forced into sleep over SPI, flash-based settings.

Exit criteria: NMEA sentences decoded by TinyGPS++, measured sleep current
with GNSS off and LR1110 asleep.

### Phase 2 (track B): BLE central and Fujifilm only

Scope: implement the phase 0 abstraction on the nRF stack. Port `CameraList`,
`Control`, and only the Fujifilm vendor classes first. Pair, connect, geotag.
Other vendors follow once the abstraction has proven itself on two stacks.

Exit criteria: an X-series camera shows the location icon and writes correct
EXIF GPS from the M4.

### Phase 3 (track B): headless provisioning and companion service

Scope: implement the plans/50 GATT service on the nRF peripheral role, plus
the headless additions listed below. First-boot behavior: with no companion
bond stored, advertise a pairing window automatically; afterwards only on a
long button press. All camera pairing is driven from the phone through the
new camera management characteristic.

Exit criteria: factory-reset device to geotagging camera using only the phone.

### Phase 4 (track B): GPS sidecar loop

Scope: the actual product. State machine: bonded camera known, reconnect with
backoff, GNSS duty cycle keyed to fix freshness (the same 30 s freshness rule
as `GPS::update()` in `src/FurbleGPS.cpp:161-192`), push fix and timesync on
the existing `updateGeoData` path, LED signals for fix and link, IMU motion
gating (stationary means longer GNSS off periods, mirroring plans 18).

Exit criteria: a full day of unattended geotagging on a walk, EXIF verified.

### Phase 5 (track B): power tuning

Scope: measure and tune. GNSS standby versus power-off tradeoffs (ephemeris
retention), connection interval and slave latency, LR1110 sleep verification,
powerbank quiescent characterization. Publish a measured runtime table.

Exit criteria: measured multi-day runtime figure to put in the README.

## Companion app extensions vs plans/50

plans/50 assumes a device with a screen and a button, and a phone that mostly
adds convenience. On a headless sidecar the app is the only interface, and the
GPS direction reverses: the device has its own GNSS and does not need the
phone as a fix source (the location characteristic stays as an indoor
fallback). What must be added on top of plans/50:

- First-boot provisioning mode. plans/50 starts the pairing window from the
  on-device menu. There is no menu. Rule: advertise the pairing window when no
  companion bond exists, stop after success, re-open only on long button press.
- Pairing security downgrade, stated honestly. plans/50 requires numeric
  comparison via `SECURE_DISPLAY_YESNO`. Without a display that association
  model is impossible. The headless device falls back to Just Works plus a
  physical button press to accept within the window. That loses MITM
  protection during the one-time companion pairing and the protocol document
  must say so.
- Camera management characteristic. New, not in plans/50: ops for scan start,
  scan results notify, connect to selected result, list saved, delete saved.
  This replaces the entire on-device scan and connect UI. It is the
  load-bearing piece: without it a headless device can never pair a camera.
- Status characteristic additions: GNSS duty cycle state and time to next fix
  attempt, so the app can explain why the icon is grey.
- Settings over the plans/50 TLV characteristic becomes mandatory rather than
  a convenience, since NVS-equivalent settings have no other editor.
- OTA loses urgency. The Adafruit nRF52 bootloader already provides BLE DFU
  and USB UF2 drag-and-drop, so the plans/50 OTA sketch is not needed on this
  target.

Everything else in plans/50 (fix record layout, status layout, trigger rules,
advertising policy, one-bond rule, TLV settings) carries over unchanged. The
protocol document from plans/50 rollout step 2 should be written once and
shared by both firmwares.

## Risks

- Bluefruit central-role security. Fujifilm secure models and the companion
  service both need bonding as shown by `secureConnection` use in `lib/furble`.
  Bluefruit's central pairing support is the least proven part of the chosen
  stack. Mitigation: prototype camera bonding in week one of phase 2; fall
  back to Zephyr if it fails, which raises effort but not feasibility.
- No schematics. Elecrow publishes no schematics for M3 or M4. The Meshtastic
  variant files are the only pinout. Anything Meshtastic does not use (the
  powerbank controller, touch button internals) is undocumented.
- Powerbank quiescent draw on the M4. If the charge/boost subsystem burns
  milliamps continuously, the 7000 mAh advantage shrinks. Must be measured
  before committing to phase 2.
- M4 GNSS chip unidentified. Duty cycle and standby behavior cannot be planned
  precisely until the chip is identified on real hardware.
- Abstraction regression risk. Phase 0 touches every vendor protocol. Only
  Fujifilm can be hardware tested by this effort (per plans/README). Other
  vendors rely on FauxNY and review, and must be flagged untested in the PR.
- Maintenance split. A second firmware repo is a permanent cost. The phase 0
  abstraction keeps the shared protocol code in one place, which is the only
  sustainable shape for this.

## Verification

Hardware to buy before any commitment:

- One ThinkNode M4 (Meshtastic version, EU 868), about $60 at Elecrow or via
  BerryBase. First tests: confirm UF2 bootloader access, flash a blinky,
  identify the GNSS chip from boot NMEA text, measure cell-rail current with
  radios idle, measure powerbank quiescent draw.
- Optionally one ThinkNode M3 later, only if the M4 firmware works and a
  strap-mounted form factor is wanted despite the battery ceiling.
- Existing Fujifilm test camera for phases 2 through 4.

Go/no-go gate: the M4 measures under 1 mA with GNSS off and LR1110 asleep, and
Bluefruit completes a bonded connection to the Fujifilm camera. If either
fails on both stacks, stop after phase 0, which is still a win for furble.

## References

Verified by fetch during this study.

Vendor listings:

- [BerryBase: ThinkNode M3 tracker card](https://www.berrybase.de/elecrow-thinknode-m3-tracker-card-fuer-meshtastic-lora-860-960-mhz-gps-wi-fi-bluetooth-770-mah)
- [BerryBase: ThinkNode M4 powerbank](https://www.berrybase.de/elecrow-thinknode-m4-powerbank-fuer-meshtastic-nrf52840-lora-868-915-mhz-gps-bluetooth-7000-mah)
- [Elecrow wiki: ThinkNode M3](https://www.elecrow.com/wiki/ThinkNode_M3_Meshtastic_Tracker_With_GPSWiFiBLE_function_For_Indoor_and_Outdoor_Positioning.html)
- [Elecrow wiki: ThinkNode M4](https://www.elecrow.com/wiki/ThinkNode-M4_Power_Bank_LoRa_Device_with_Meshtastic_Function_Powered_By_nRF52840.html)
- [Elecrow product: ThinkNode M2 (ESP32-S3, no GNSS)](https://www.elecrow.com/thinknode-m2-meshtastic-lora-signal-transceiver-powered-by-esp32-s3-with-1-3-oled-display.html)

Firmware ecosystem:

- [Meshtastic hardware docs: ThinkNode series](https://meshtastic.org/docs/hardware/devices/elecrow/thinknode/)
- [Meshtastic firmware: ThinkNode M3 variant.h (pin definitions)](https://raw.githubusercontent.com/meshtastic/firmware/develop/variants/nrf52840/ELECROW-ThinkNode-M3/variant.h)
- [Meshtastic firmware: ThinkNode-M3 board JSON (Adafruit BSP, S140 SoftDevice)](https://github.com/meshtastic/firmware/blob/develop/boards/ThinkNode-M3.json)

furble source anchors:

- `lib/furble/Camera.h:20,168` NimBLE client coupling
- `src/FurbleGPS.cpp` ESP-IDF UART driver plus TinyGPS++, fix freshness at 161-192
- `src/FurblePlatform.cpp` M5Unified and esp_pm platform layer
- `src/FurbleSettings.cpp` NVS storage
- `plans/50-companion-app-design.md` companion GATT service this plan extends

## Draft issue

Title: Interest in a ThinkNode M4 GPS sidecar target?

Elecrow's ThinkNode M4 is a 7000 mAh powerbank with an nRF52840, a
multi-constellation GNSS receiver and an open UF2 bootloader, which makes it
near-ideal dedicated hardware for a set-and-forget BLE geotagging sidecar that
runs for weeks. It is not an ESP32, so supporting it means putting a thin BLE
abstraction under lib/furble's vendor protocols and building a small headless
firmware on the nRF side, while the protocol code stays shared. Before anyone
spends time on that abstraction: is a non-ESP32 target something this project
would welcome in principle?
