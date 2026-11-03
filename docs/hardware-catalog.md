# furble candidate hardware catalog

This is a buying guide for hardware that could run furble, feed it position
data, or serve as a porting target. It is not a support claim. The supported
boards are the environments listed in
[supported-hardware.md](supported-hardware.md), including the Waveshare
ESP32-S3-ETH wired node.

The catalog was checked on 2026-11-01. Prices are vendor list prices in the
currency the vendor shows, excluding tax and shipping unless the vendor says
otherwise. They move. Check before ordering.

The analysis that turns this list into porting decisions is
[plans/287-mcu-portability-and-bundle-hardware.md](../plans/287-mcu-portability-and-bundle-hardware.md).

## The hard requirement

furble is a BLE central. It scans, connects, discovers GATT services, writes
characteristics, subscribes to notifications, bonds, and holds several camera
links at once. A device that only advertises, or only acts as a BLE peripheral,
cannot run the camera side of furble. Every device in the main tables supports
BLE central mode. Most have BLE 5. The original ESP32 Core2 is BLE 4.2 and
still works for the camera protocols. The porting question is how much of
furble survives above that radio.

GNSS compatibility is the second filter. Only two things matter:

- Does the receiver answer `$PCAS` commands? If yes, the code in
  `src/FurbleGPS.cpp` and the plans 14 and 32 work applies unchanged.
- Does it have a rail switch or standby line? If yes, the plans 15 and 19 duty
  cycle work applies.

Everything else is a new GNSS driver, which is a contained but real cost.

## The short answer

All four requirements, BLE plus GNSS plus touch display plus WiFi, are met by
only a handful of devices. All of them are ESP32 boards. The forward-looking
targets are the ESP32-S3 and ESP32-P4 boards. The original ESP32 Core2 plus GPS
is the zero-port exception: it already meets all four requirements.

| Goal | Device | Why |
| :--- | :--- | :--- |
| Best development target | LilyGO T-Deck Plus | 16 MB flash, 8 MB PSRAM, touch, keyboard, GPS, 2000 mAh, 70.99 USD |
| Best GPS, with a UBX driver | LilyGO T-Deck Plus or T-Watch Ultra | u-blox MIA-M10Q, -167 dBm tracking, 1 second hot or assisted fix, 25 mW class |
| Best GPS, no new driver | Elecrow ThinkNode M5 or T-Beam Supreme L76K | L76K `$PCAS`, standby and a rail switch. Same driver as today. |
| Best wearable | LilyGO T-Watch Ultra | 2.06 inch AMOLED touch, GPS, IP65, 1100 mAh, 78.32 USD |
| Best `$PCAS` sidecar | Elecrow ThinkNode M5 | ESP32-S3, L76K `$PCAS`, e-paper, 1200 mAh, 53.90 USD |
| Best sidecar runtime, with a port | LilyGO T-Echo Plus | nRF52840, e-paper, 2400 mAh, 47 hours of continuous GNSS |
| Best runtime with the current code | LilyGO T-Beam Supreme, L76K trim | swappable 3400 mAh 18650, 66 hours of continuous GNSS, 37.12 USD plus cell |
| Best big screen | LilyGO T-Display P4 | 4.1 inch AMOLED touch, ESP32-P4 plus C6, L76K, 119.30 USD |
| Best zero-port option | M5Stack Core2 plus GPS unit | already a furble environment, touch, 1500 mAh battery module |
| Cheapest complete BLE plus GPS plus OLED | Seeed Wio Tracker L1 Pro | nRF52840, L76K, 2000 mAh, 47.90 USD |
| Cheapest BLE plus GPS | Seeed XIAO ESP32S3 plus L76K | board plus 10.90 USD module, no display |
| Cheapest touch plus GPS plus WiFi | Waveshare ESP32-S3-Touch-AMOLED-1.75-G | 39.99 USD, battery extra |

## Requirement matrix

| Device group | BLE | GNSS | Display | Touch | WiFi |
| :--- | :--- | :--- | :--- | :--- | :--- |
| T-Deck Plus, T-Deck Pro, T-Watch Ultra, T-Watch S3 Plus, T-Display P4 | yes | yes | yes | yes | yes |
| Waveshare AMOLED 1.75-G, CoreS3 plus GPS, Core2 plus GPS, Tab5 plus GPS, Dial plus GPS | yes | yes | yes | yes | yes |
| ThinkNode M5, ThinkNode M9, Heltec Wireless Tracker V2, T-Beam Supreme, T114, T-Echo Plus, Wio L1 Pro, WisMesh Pocket, ThinkNode M1, T-LoRa Pager | yes | yes | yes | no | mixed, see tables |
| ThinkNode M3, M4, M6, SenseCAP T1000-E | yes | yes | no | no | no |
| Wio Tracker 1110 | yes | scan plus solver | no | no | no |
| Cardputer Adv, Tab5 without GPS, CrowPanel, Waveshare P4 without GPS | yes | no | yes | mixed | yes |

## GNSS reuse classes

The class decides whether furble's existing GPS code and power work apply.

| Class | Receivers | furble work |
| :--- | :--- | :--- |
| `$PCAS` | Quectel L76K, AT6668, ATGM336H | Existing code. Probe with `$PCAS06`. Plans 14, 15, 19 and 32 apply. |
| UBX | u-blox MIA-M10Q, MAX-M10S, NEO-M10S, ZOE-M8Q | New driver. NMEA parsing still works, but the configuration and standby commands do not. |
| `$CFG` | Unicore UC6580 | New driver. Different sentence set and 115200 baud. |
| Other | Quectel LC76G, MediaTek AG3335, LR1110 scan, unnamed modules | Treat as a new driver until proven otherwise. The LR1110 needs a position solver, not just a driver. Probe on hardware before promising reuse. |

A Meshtastic variant file that names an L76K is strong evidence, not proof.
Elecrow's own documentation does not always name the receiver. The device in
hand decides.

## GNSS receivers in this catalog

The receiver matters more than the board for the "only locks outdoors"
complaint. Sensitivity and antenna decide whether a fix happens at all.
Assistance and backup power decide how long it takes.

| Receiver | Bands | Sensitivity | TTFF | Current | Backup or standby | Assistance | furble class | Verdict |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| AT6668, M5 Unit GPS v1.1 | L1 | -162 dBm tracking, -148 dBm cold | cold about 108 s without backup in the field reports | 31.64 mA at 5 V | no backup rail; firmware can send `$PCAS12` timed standby | CASIC aiding and ephemeris cache when `GPS_ASSIST` is on | `$PCAS` | The current baseline. Fine outdoors, weak indoors, slow after a rail cut. The lack of a backup rail is the hardware gap, not a lack of standby or assistance support. |
| Quectel L76K | L1 | -162 dBm tracking, -148 dBm cold | 30 s cold, 5.5 s hot | 41 mA tracking, 360 uA standby | standby rail | vendor AGPS | `$PCAS` | Same sensitivity as the AT6668, but real standby and a rail switch. The ThinkNode M5 and T-Beam L76K trim use it. |
| u-blox MIA-M10Q | L1, four GNSS | -167 dBm tracking, -148 dBm cold | 27 s cold, 1 s hot, 1 s AssistNow | 25 mW, about 8 to 12 mA at 3 V | 28 to 32 uA backup, 46 uA software standby | AssistNow Online, Offline, Autonomous | UBX | The best all-round upgrade. Lower power, better sensitivity, one second hot or aided fix, real backup. |
| u-blox MAX-M10S | L1, four GNSS | -167 dBm tracking, -148 dBm cold | 27 s cold, 1 s hot, 1 s AssistNow | 25 mW | same as M10 | AssistNow | UBX | Same silicon as the MIA-M10Q in a larger module. The T-Beam M10S trim uses it. |
| u-blox NEO-F10N | L1 and L5 | -148 dBm cold, tracking figure in the data sheet | 28 s cold, 2 s hot | about twice an L1 receiver | backup rail | AssistNow | UBX | Better in urban multipath, not an indoor fix. Costs power. Useful only if urban accuracy is the goal. |
| Unicore UC6580 | L1 and L5 | vendor figures, verify | vendor figures, verify | vendor figures, verify | vendor figures, verify | vendor dependent | `$CFG` | Dual band on the Heltec Wireless Tracker V2. Needs a new driver and more power. |
| Quectel LC76G | L1 | vendor figures, verify | vendor figures, verify | vendor figures, verify | vendor dependent | vendor dependent | other | Used on the Waveshare AMOLED 1.75-G. Verify the command set before assuming reuse. |
| MediaTek AG3335 | L1 | vendor figures, verify | vendor figures, verify | vendor figures, verify | vendor dependent | vendor dependent | other | Used on the SenseCAP T1000-E. A new driver. |
| Semtech LR1110 GNSS | L1 scan engine | vendor figures, verify | needs a solver, no standalone NMEA fix | vendor figures, verify | not applicable | cloud or local solver; WiFi scan too | other | Used on the Wio Tracker 1110. It scans GNSS and WiFi and needs a position solver, so it is not a drop-in NMEA receiver. The ThinkNode M3 uses an ATGM336H. The ThinkNode M4 receiver is unnamed. |

Three conclusions from this table.

- A better L1 receiver buys sensitivity and power, not an indoor fix. The
  M10 class is the sensible upgrade. It has the same cold start sensitivity as
  the AT6668 but much better tracking, 1 second hot starts, and a 28 uA backup.
- L1/L5 buys multipath rejection in cities. It does not penetrate buildings
  better in any way that matters. It also roughly doubles GNSS power.
- The only reliable indoor position is an assisted one: the phone, a WiFi
  scan, or a known last fix. furble already has the phone path. Use it.

## Ready-to-use devices

These have a case, a battery and a display. The GNSS receiver is onboard or on
a short Grove cable.

| Device | MCU | GNSS | Display and touch | WiFi | Battery | Price | Tier | Notes |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| LilyGO T-Deck Plus | ESP32-S3FN16R8, 16 MB, 8 MB PSRAM | MIA-M10Q, UBX | 2.8 inch 320x240 IPS, touch, keyboard, trackball | yes | 2000 mAh | 70.99 USD [1] | T1 | The best single development target. 16 MB flash leaves room for two large OTA slots. |
| LilyGO T-Deck Pro | ESP32-S3FN16R8, 16 MB, 8 MB PSRAM | MIA-M10Q, UBX | 3.1 inch 320x240 e-paper, touch, keyboard | yes | 1400 to 1500 mAh, sources conflict | 84.96 USD audio, 99.98 USD 4G [2] | T1 | E-paper refresh is slow for LVGL. The 4G variant is irrelevant to furble. |
| LilyGO T-Watch Ultra | ESP32-S3R8, 16 MB, 8 MB PSRAM | MIA-M10Q, UBX | 2.06 inch 410x502 AMOLED, touch | yes | 1100 mAh | 78.32 USD [3] | T1 | IP65. Best wearable shape. Heavy for a camera strap. |
| LilyGO T-Watch S3 Plus | ESP32-S3, 16 MB, 8 MB PSRAM | MIA-M10Q, UBX | 1.54 inch 240x240 IPS, touch | yes | 940 mAh | 58.74 USD [4] | T1 | Smaller and cheaper than the Ultra. Micro USB. |
| LilyGO T-LoRa Pager | ESP32-S3, 16 MB, 8 MB PSRAM | MIA-M10Q, UBX | 2.33 inch 480x222 IPS, no touch, keyboard and encoder | yes | internal LiPo | 87.35 USD [5] | T1 | Pager shape. Stock comes and goes. |
| Elecrow ThinkNode M5 | ESP32-S3, 4 MB flash per vendor wiki, 8 MB PSRAM | L76K, `$PCAS` | 1.54 inch 200x200 e-paper, no touch | yes | 1200 mAh | 53.90 USD [6] | T1 | The best sidecar. Vendor flash claims conflict, verify the module. |
| Elecrow ThinkNode M9 | ESP32-S3R8, 16 MB, 8 MB PSRAM | Multi-constellation, chip unnamed | 2.4 inch 240x320 TFT, no touch, 37 keys | yes | 2300 mAh | 74.90 USD [6] | T1 | Keyboard terminal. Touch is absent by specification. |
| Elecrow ThinkNode M1 | nRF52840, 1 MB, 256 KB RAM | L76K, `$PCAS` | 1.54 inch e-paper, no touch, rotary knob | no | 1200 mAh | 53.90 USD [6] | T2 | Low power, but a Nordic rewrite. |
| LilyGO T-Echo Plus | nRF52840, 1 MB internal, 256 KB RAM. Vendor lists 2 MB. | L76K, `$PCAS` | 1.54 inch e-paper, no touch | no | 2400 mAh | 64.22 USD [7] | T2 | Clip and tripod mount. Good camera-bag shape. |
| Bangle.js 2 | nRF52840, 1 MB internal plus 8 MB external, 256 KB RAM | AT6558 or AT6558R, `$PCAS` | 1.3 inch 176x176 always-on LCD, touch | no | 175 mAh | 99.60 GBP [44] | T2 | IP67 watch with a full touchscreen and a `$PCAS` receiver. The 175 mAh cell limits continuous GNSS to about 6 hours. |
| Seeed Wio Tracker L1 Pro | nRF52840, 1 MB, 256 KB RAM | L76K, `$PCAS` | 1.3 inch 128x64 OLED, no touch, joystick | no | 2000 mAh | 47.90 USD [8] | T2 | The cheapest complete BLE plus GPS plus display device. |
| RAK WisMesh Pocket V2 and V3 | nRF52840, 1 MB, 256 KB RAM | Onboard GNSS, module not published | 1.3 inch OLED, no touch | no | 3200 mAh | 77.97 USD V2, 84.97 USD V3 [9] | T2 | V3 adds an IP67 case and a buzzer. |
| M5Stack CoreS3 plus Unit GPS v1.1 | ESP32-S3, 16 MB, 8 MB PSRAM | AT6668, `$PCAS` | 2.0 inch 320x240 IPS, touch | yes | 500 mAh internal | 59.90 USD plus 9.95 USD [10] | T1 | M5Unified knows the panel, but furble has no CoreS3 input callbacks and no CoreS3 battery backend. Needs a board profile plus input and power integration. |
| M5Stack Core2 plus Unit GPS plus Battery Module 13.2 | ESP32, 16 MB | AT6668, `$PCAS` | 2.0 inch 320x240 IPS, touch | yes | internal plus 1500 mAh module | 49.90 plus 9.95 plus 11.90 USD [10] | T0 | This stack works today. It is the zero-port reference. |
| M5Stack Tab5 plus Unit GPS | ESP32-P4 plus ESP32-C6, 16 MB, 32 MB PSRAM | AT6668, `$PCAS` | 5 inch 1280x720 IPS, touch | WiFi 6 through C6 | NP-F550, 2000 mAh | 55 to 60 USD plus 9.95 USD [11] | T1P | Big and heavy. The P4 path needs ESP-Hosted and two images to update. |

## Bare boards with onboard GNSS

These need a case, a battery, or both. Some have the GNSS receiver onboard;
some use a short Grove, FeatherWing, or UART connection. Several are still the
right buy for a bench prototype.

| Device | MCU | GNSS | Display and touch | WiFi | Battery | Price | Tier | Notes |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| LilyGO T-Beam Supreme, L76K trim | ESP32-S3FN8, 8 MB, 8 MB PSRAM | L76K, `$PCAS` | 1.3 inch 128x64 OLED, no touch | yes | 18650 holder, cell extra | 37.12 USD [12] | T1 | Best runtime per euro. AXP2101, IMU, BME280, microSD. Bare board with SMA. |
| LilyGO T-Beam Supreme, M10S trim | ESP32-S3FN8, 8 MB, 8 MB PSRAM | MAX-M10S, UBX | 1.3 inch 128x64 OLED, no touch | yes | 18650 holder, cell extra | 37.12 USD and up [12] | T1 | Better receiver, but it voids the `$PCAS` work. |
| Heltec Wireless Tracker V2 | ESP32-S3FN8, 8 MB, no PSRAM | UC6580, `$CFG` | 0.96 inch 160x80 TFT, no touch | yes | LiPo connector, solar input | 26.90 USD, 34.95 EUR [13] | T1 | Cheapest ESP32-S3 with GNSS. Needs a new GNSS driver. |
| Heltec WiFi LoRa 32 V4 plus GPS kit | ESP32-S3, 8 MB | GPS module in the kit, chip to verify | 0.96 inch OLED, no touch | yes | kit battery | about 30 EUR, verify [14] | T1 | Cheap OLED node. Verify the GNSS chip before assuming `$PCAS`. |
| Heltec Mesh Node T114 plus GPS module | nRF52840, 1 MB, 256 KB RAM | L76K module, `$PCAS` | 1.14 inch 135x240 TFT, no touch | no | LiPo or solar | 17.90 to 33.70 USD, kit 56.90 USD [15] | T2 | 11 uA deep sleep. Display and GPS are options, not standard. |
| Waveshare ESP32-S3-Touch-AMOLED-1.75-G | ESP32-S3R8, 16 MB, 8 MB PSRAM | LC76G, other | 1.75 inch 466x466 AMOLED, touch | yes | battery pads, cell extra | 39.99 USD, 46.04 EUR [16] | T1 | Round display. The GPS version does not fit the metal case variant. |
| LilyGO T-Display P4 | ESP32-P4 plus ESP32-C6, 16 MB, 32 MB PSRAM | L76K, `$PCAS` | 4.1 inch AMOLED, touch | WiFi 6 through C6 | charge management, cell extra | 119.30 USD [17] | T1P | Best big-screen plus `$PCAS` combination. Also has LoRa and a camera. |
| Elecrow ThinkNode M3 | nRF52840, 1 MB, 256 KB RAM | ATGM336H, `$PCAS` | none | no | 770 mAh | see vendor [6] | T2 | 64x64x10 mm, IP66. Pogo-pin USB. |
| Elecrow ThinkNode M4 | nRF52840, 1 MB, 256 KB RAM | Multi-constellation, chip unnamed | none | no | 7000 mAh 18650 pack | about 69.90 EUR [18] | T2 | Power bank shape. The battery argument, not the software argument. |
| Elecrow ThinkNode M6 | nRF52840, 1 MB, 256 KB RAM | L76K, `$PCAS` | none | no | 6 W solar plus two 18650 cells | see vendor [6] | T2 | Outdoor node. Not a remote. |
| Seeed SenseCAP T1000-E | nRF52840, 1 MB, 256 KB RAM | MediaTek AG3335, other | none | no | 700 mAh | 39.90 USD [19] | T2 | Credit-card tracker, IP65, pogo charging. |
| Seeed Wio Tracker 1110 | nRF52840 plus LR1110 | LR1110 scan, other | none | no | LiPo connector | see vendor [20] | T2 | Development board. GNSS and WiFi are scans that need a position solver, not a standalone NMEA fix. |
| M5Stack Dial v1.1 plus Unit GPS | ESP32-S3FN8, 8 MB | AT6668, `$PCAS` | 1.28 inch 240x240 round, touch, encoder | yes | LiPo connector, cell extra | 34.90 plus 9.95 USD [21] | T0 to T1 | M5Unified knows the Dial. Verify the exact board branch before claiming zero port. |
| M5Stack Cardputer Adv plus Unit GPS | ESP32-S3FN8, 8 MB | AT6668, `$PCAS` | 1.14 inch 240x135, no touch, 56 keys | yes | 1500 mAh | 29.90 plus 9.95 USD [22] | T0 to T1 | Cheap keyboard terminal. No touch. |
| Adafruit ESP32-S3 TFT Feather plus Ultimate GPS FeatherWing | ESP32-S3, 4 MB, 2 MB PSRAM | PA1616D, other | 1.14 inch TFT, no touch | yes | LiPo connector | 24.95 plus 24.95 USD [23] | T1 | Easy stacking. 4 MB flash is tight for dual OTA. |

## Combinations

These are not single products. They are the shortest path to a shape that no
vendor sells off the shelf.

| Combination | MCU | GNSS | Display and touch | WiFi | Price | Tier | Notes |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| Seeed XIAO ESP32S3 plus L76K module | ESP32-S3 | L76K, `$PCAS` | none, add the 1.28 inch round display | yes | board plus 10.90 USD module, about 18 to 21 USD [24] | T1 | Cheapest `$PCAS` path. No case. |
| Seeed XIAO nRF52840 plus L76K module | nRF52840 | L76K, `$PCAS` | none | no | about 21 USD [24] | T2 | Same shape, Nordic rewrite. |
| Raspberry Pi Pico 2 W plus GNSS plus SPI display | RP2350 | NMEA module | add | yes | 7 USD plus parts [25] | T3 | BTstack, not NimBLE. The CYW43439 controller firmware lacks extended advertising, which furble does not use. The cost is the BTstack rewrite. |
| Waveshare ESP32-P4-WIFI6-Touch-LCD plus GPS module | ESP32-P4 plus C6 | L76K or AT6668 over UART | 3.4 to 10.1 inch, touch | WiFi 6 through C6 | see vendor [26] | T1P | Big HMI. Same P4 transport work as the Tab5. |
| Elecrow CrowPanel Advance plus GPS module | ESP32-S3 | module over UART | 2.4 to 7 inch, touch | yes | see vendor [27] | T1 | HMI panel, not a handheld. Needs a battery and a case. |

## Hypothetical battery life at a common GNSS load

These are first-order estimates, not measurements. The model is deliberately
simple:

```
runtime_hours = usable_capacity_mAh / average_current_mA
usable_capacity = nameplate_mAh * 0.85
```

The 0.85 factor covers regulator loss, cell aging, and the difference between
nameplate and usable capacity. Cold weather can cost another 20 to 30 percent.
The current figures come from the repo power audit for the ESP32-S3, from
vendor data sheets for the receivers and radios, and from the table below for
the display classes. Where a board does not ship a battery, the table assumes
a 2000 mAh cell.

The currents are mixed at their native rails. The AT6668 figure is at 5 V and
the cells are at 3.7 V, so the battery-side GNSS draw is higher than the number
shown because of the boost. The model also omits the UART wake and NMEA parse
cost, cold-start inrush, display refresh peaks, and user behavior. Use it to
rank options, not to promise a runtime.

| Scenario | What is on |
| :--- | :--- |
| A | Screen off, BLE connected, GNSS off, `SLEEP_CONN` on |
| B | Screen off, BLE connected, GNSS continuous |
| C | Screen off, BLE connected, GNSS 25 percent duty |
| D | Screen on 25 percent of the time, BLE connected, GNSS off |
| E | Screen on, BLE connected, GNSS continuous |

Hours, after the 0.85 derating. The ESP32-S3 rows assume `SLEEP_CONN` on. It defaults on for the StickS3. The row with it off shows the difference. All rows use a generic 40 mA GNSS load, including M10 devices, so the M10 rows are conservative.

| Device | Battery | A | B | C | D | E |
| :--- | :--- | ---: | ---: | ---: | ---: | ---: |
| M5StickS3, default (`SLEEP_CONN` on) | 250 mAh | 59 | 4.9 | 15.6 | 9.2 | 1.8 |
| M5StickS3, `SLEEP_CONN` off | 250 mAh | 4.9 | 2.5 | 4.0 | 4.0 | 1.8 |
| M5Stack CoreS3 | 500 mAh | 118 | 9.7 | 31.2 | 18.5 | 3.5 |
| M5Stack Core2 plus 1500 mAh module | 1890 mAh | 114 | 28.1 | 64.6 | 52.0 | 12.9 |
| LilyGO T-Deck Plus | 2000 mAh | 472 | 39.0 | 125 | 73.9 | 14.0 |
| LilyGO T-Deck Pro, e-paper | 1450 mAh | 342 | 28.3 | 90.6 | 94.8 | 15.2 |
| LilyGO T-Watch Ultra | 1100 mAh | 260 | 21.4 | 68.8 | 46.2 | 8.5 |
| LilyGO T-Watch S3 Plus | 940 mAh | 222 | 18.3 | 58.8 | 45.0 | 8.0 |
| Waveshare AMOLED 1.75-G, assumed cell | 1000 mAh | 236 | 19.5 | 62.5 | 44.7 | 8.1 |
| Elecrow ThinkNode M5, e-paper | 1200 mAh | 283 | 23.4 | 75.0 | 80.0 | 12.7 |
| Elecrow ThinkNode M9 | 2300 mAh | 543 | 44.8 | 144 | 96.5 | 17.7 |
| LilyGO T-Beam Supreme | 3400 mAh | 803 | 66.3 | 213 | 190 | 32.0 |
| LilyGO T-Display P4, assumed cell | 2000 mAh | 170 | 34.0 | 85.0 | 41.2 | 9.7 |
| M5Stack Tab5 | 2000 mAh | 113 | 30.9 | 68.0 | 24.3 | 6.2 |
| Elecrow ThinkNode M1 | 1200 mAh | 408 | 23.4 | 80.0 | 389 | 23.2 |
| LilyGO T-Echo Plus, e-paper | 2400 mAh | 816 | 46.9 | 160 | 777 | 46.4 |
| Seeed Wio Tracker L1 Pro | 2000 mAh | 680 | 39.1 | 133 | 332 | 31.5 |
| RAK WisMesh Pocket V2 | 3200 mAh | 1088 | 62.5 | 213 | 531 | 50.4 |
| Heltec T114 kit | 2000 mAh | 680 | 39.1 | 133 | 223 | 26.6 |
| Raspberry Pi Pico 2 W, assumed cell | 2000 mAh | 113 | 30.9 | 68.0 | 67.3 | 17.7 |
| Generic nRF54L15 or LM20A, assumed cell | 2000 mAh | 850 | 39.5 | 139 | 243 | 27.0 |

### M10 receiver-specific runtime

These rows use the MIA-M10Q's 12 mA tracking load instead of the common 40 mA.
A and D do not change because GNSS is off. The T-Beam M10S trim is L1-only.

| Device | Cell | B | C | E |
| :--- | ---: | ---: | ---: | ---: |
| LilyGO T-Deck Plus | 2000 mAh | 109 | 258 | 18.2 |
| LilyGO T-Deck Pro | 1450 mAh | 79.0 | 187 | 23.2 |
| LilyGO T-Watch Ultra | 1100 mAh | 59.9 | 142 | 11.4 |
| LilyGO T-Watch S3 Plus | 940 mAh | 51.2 | 121 | 11.1 |
| LilyGO T-Beam Supreme M10S | 3400 mAh | 185 | 438 | 46.5 |

How to read the table.

- The battery dominates everything. The jump from the 250 mAh StickS3 to the
  2000 mAh T-Deck Plus is eight times the runtime before any software change.
- The display is the second lever in the screen-on scenarios. Scenario B has
  the screen off, so it cannot show a display saving. The T-Deck Plus at 39
  hours and the T-Deck Pro at 28 hours differ by the cell ratio, 2000 versus
  1450 mAh. At equal capacity, the e-paper Pro would model at about 131 hours
  in scenario D against 74 hours for the LCD Plus, and about 21 hours in
  scenario E against 14 hours. That is the display saving.
- GNSS is the third lever. The main table adds a common 40 mA. The M10 table
  above shows the same devices at 12 mA, which is the receiver-specific figure.
  Cutting GNSS to 25 percent duty roughly triples sidecar runtime. The L76K
  standby and the M10 backup are what make this usable without a 30 second
  cold start each time.
- The MCU is the smallest lever. An ESP32-S3 at 3.6 mA versus an nRF54 at
  2 mA is a 1.6 mA difference. With GNSS on, that is under 4 percent runtime.
  With the screen off and GNSS off, the current is 44 percent lower, which is a
  1.8x runtime improvement at equal capacity. That matters only for a device
  that does nothing most of the day.
- The port therefore buys very little for the battery complaint unless it also
  changes the display, the receiver, the duty cycle, and the cell.

## Development kits for a port

These are not products. They are the cheapest way to prove a silicon family
before a board exists.

| Kit | SoC | Memory | Price | Why it matters |
| :--- | :--- | :--- | :--- | :--- |
| ESP32-C6-DevKitC-1 | ESP32-C6 | 512 KB SRAM, 4 MB flash | 9 USD [28] | The cheapest way to test the same ESP-IDF and NimBLE code on a single-core RISC-V part. |
| ESP32-P4-Function-EV-Board | ESP32-P4 plus C6 | 768 KB SRAM plus PSRAM | see Espressif [29] | The reference for ESP-Hosted, dual-image update and a big display. |
| nRF52840 DK | nRF52840 | 256 KB RAM, 1 MB flash | 48.95 USD [30] | The Nordic baseline. It cannot hold the current image. |
| nRF5340 DK | nRF5340 | 512 KB plus 64 KB RAM, 1 MB plus 256 KB flash | 48.95 USD [30] | Dual-core. Headless only. |
| nRF54L15 DK | nRF54L15 | 256 KB RAM, 1.5 MB NVM | 39 USD [30] | Best low-power headless candidate. No USB on the SoC, the DK has a debugger. |
| nRF54LM20A DK | nRF54LM20A | 512 KB RAM, 2 MB NVM, HS USB | 45 USD [30] | The best Nordic fit on paper. |
| nRF54H20 DK | nRF54H20 | 1 MB RAM, 2 MB NVM | see Nordic [30] | Overkill for a remote. |
| STM32WBA65I-DK1 | STM32WBA65 | 512 KB RAM, 2 MB flash | 80 to 100 USD [31] | Zephyr board exists. ST radio stack is binary. |
| XG24-EK2703A | EFR32MG24 | 256 KB RAM, 1.5 MB flash | 43.04 USD [32] | Zephyr support, good BLE, binary radio stack. |
| FRDM-MCXW71 | MCX W71 | 128 KB RAM, 1 MB flash | see NXP [33] | 128 KB RAM rules out anything close to the current stack. |
| Apollo4 Blue Plus EVB | Apollo4 Blue Plus | 2.75 MB RAM, 2 MB NVM | 249.99 USD [34] | MIPI DSI and a 2.5D GPU. The most interesting display SoC outside ESP32. |
| Alif Balletto B1 DK or SK-B1 | B1 | 1.8 MB MRAM, 2 MB SRAM | 151 USD DK, 51 USD StartKit [35] | Zephyr board and BLE 5.x. The Ensemble E1C has no radio, so it is not a furble target without a companion. |
| Raspberry Pi Pico 2 W | RP2350 plus CYW43439 | 520 KB SRAM, 4 MB flash | 7 USD [25] | BTstack rewrite. The CYW43439 controller firmware lacks extended advertising, which furble does not use. Cheap experiment, not a product path. |
| Seeed XIAO nRF54L15 | nRF54L15 | 256 KB RAM, 1.5 MB NVM | 9.90 USD [36] | Tiny nRF54 experiment board. |
| Raspberry Pi Zero 2 W | BCM2710 plus CYW43438 | 512 MB RAM, microSD | see Raspberry Pi | Linux and BlueZ. It meets every hardware requirement and none of the power ones. |

## Excluded hardware

These are common suggestions that fail a requirement.

| Device | Why it is excluded |
| :--- | :--- |
| Heltec MeshPocket | nRF52840, e-paper and a large power bank, but no GNSS. |
| M5Stack Cardputer and Cardputer Adv | No onboard GNSS. A Grove unit adds it. |
| M5Stack Tab5 without a GPS unit | No onboard GNSS. It is a P4 tablet, not a tracker. |
| Waveshare ESP32-P4-WIFI6 boards without a GPS module | No onboard GNSS. Add one over UART. |
| Elecrow CrowPanel | No onboard GNSS. Add one over UART. |
| LilyGO T-Watch S3, non-Plus | No GNSS. The Plus adds it. |
| PineTime, Watchy | No GNSS, and in some cases no BLE central role. |
| Particle Tracker One | nRF52840, GNSS and LTE, no display, about 240 USD. Poor value for this use case. |
| nRF52840 Dongle | USB stick. No display, no battery, no GPS. |

## Integration notes

These decide whether the hardware works in the hand, not just on the datasheet.

- GNSS antenna. Keep the patch or the active antenna at least 20 mm from the
  2.4 GHz antenna and from the battery. A ground plane of 40 by 40 mm or more
  is worth more than a receiver upgrade. A metal case blocks GNSS. The
  Waveshare AMOLED GPS version does not fit its metal case because the module
  is taller, which is a physical fit issue, not an RF one.
- Antenna connectors. An SMA or U.FL connector lets the antenna move away from
  the electronics. A board with a built-in patch and no connector is a closed
  design decision.
- Battery. 18650 holders usually take unprotected flat-top cells. Protected
  cells are longer and may not fit. Check the charger temperature range and do
  not charge below 0 C. A 3400 mAh cell is about 12.6 Wh.
- Sealed cases. An IP65 or IP67 device has an internal antenna and no room for
  a connector. Opening it for an external antenna is destructive. Buy the
  sealed device only if the internal antenna is good enough.
- Certification. The radio module is certified. A changed antenna, a metal
  enclosure, or a modified ground plane can invalidate that. The LoRa radio is
  unused by furble, but it is part of the certification of the board.
- Display. E-paper is readable in sunlight and cheap to hold, but slow. AMOLED
  is bright and thin, but burns power on a bright screen. LCD needs a backlight
  and an APB power lock.
- Touch. Capacitive touch is poor with gloves. A device that will be used
  outdoors in winter needs at least one physical shutter button.

## Buying notes

- LoRa is irrelevant to furble. Most of this hardware exists because of
  Meshtastic. The LoRa radio is unused unless a second firmware is stacked on
  the same device.
- WiFi 6 on the ESP32-P4 boards comes from a companion ESP32-C6. The P4 itself
  has no radio.
- The ESP32-S3 devices with 16 MB flash are the only ones that comfortably hold
  two large OTA slots. 4 MB parts trade update headroom for price.
- A u-blox receiver is not a drop-in for `$PCAS`. It is usually a better
  receiver and a new driver.
- An external antenna connector matters more than the receiver model in a
  camera bag. The T-Beam and Heltec boards win there.
- EU stock changes weekly. BerryBase, OpenELAB, TinyTronics, Hexaspot and
  Passion Radio were the useful shops at the check date. Vendor-direct
  shipping from China is usually cheaper and slower.

## Sources

Fetched on 2026-11-01. Prices are from the vendor page unless marked.

1. LilyGO T-Deck Plus, 70.99 USD: https://lilygo.cc/en-us/products/t-deck-plus-1
2. LilyGO T-Deck Pro, 84.96 USD audio and 99.98 USD 4G:
   https://wiki.lilygo.cc/products/t-deck-series/t-deck-pro and
   https://www.cnx-software.com/2025/04/03/lilygo-t-deck-pro-esp32-s3-lora-messenger-e-paper-touch-display-keyboard-and-4g-lte-or-audio-codec-option
3. LilyGO T-Watch Ultra, 78.32 USD:
   https://lilygo.cc/en-us/products/t-watch-ultra and
   https://wiki.lilygo.cc/products/t-watch-series/t-watch-ultra
4. LilyGO T-Watch S3 Plus, 58.74 USD:
   https://lilygo.cc/en-us/products/t-watch-s3-plus and
   https://wiki.lilygo.cc/products/t-watch-series/t-watch-s3-plus
5. LilyGO T-LoRa Pager, 87.35 USD:
   https://lilygo.cc/en-us/products/t-lora-pager
6. Elecrow ThinkNode series, M5 53.90 USD, M9 74.90 USD, M1 53.90 USD:
   https://meshtastic.org/docs/hardware/devices/elecrow/thinknode/ and
   https://www.elecrow.com/thinknode-m5-meshtastic-lora-signal-transceiver-esp32-s3-1-54-screen-gps-function.html
   and
   https://www.elecrow.com/thinknode-m9-meshcore-communication-terminal-with-full-keyboard-2-4inch-lcd-esp32-s3-lr1110-gps-2300mah.html
   and
   https://www.elecrow.com/thinknode-m1-meshtastic-lora-signal-transceiver-powered-by-nrf52840-with-154-screen-support-gps.html
7. LilyGO T-Echo Plus, 64.22 USD:
   https://wiki.lilygo.cc/products/t-echo-series/t-echo-plus
8. Seeed Wio Tracker L1 Pro, 47.90 USD:
   https://www.seeedstudio.com/Wio-Tracker-L1-Pro-p-6454.html
9. RAK WisMesh Pocket V2 and V3, 77.97 and 84.97 USD:
   https://store.rakwireless.com/products/wismesh-pocket and
   https://meshtastic.org/docs/hardware/devices/rak-wireless/wismesh/pocket
10. M5Stack CoreS3, Core2 and Unit GPS v1.1:
    https://docs.m5stack.com/en/core/CoreS3 and
    https://docs.m5stack.com/en/unit/Unit_GPS_v1.1 and
    https://shop.m5stack.com/products/gps-bds-unit-v1-1-at6668
11. M5Stack Tab5:
    https://shop.m5stack.com/products/m5stack-tab5-iot-development-kit-esp32-p4
12. LilyGO T-Beam Supreme, 37.12 USD and GNSS trims:
    https://wiki.lilygo.cc/products/t-beam-series/t-beam-supreme/
13. Heltec Wireless Tracker V2, 26.90 USD and 34.95 EUR:
    https://heltec.org/project/wireless-tracker-v2/ and
    https://hexaspot.com/products/heltec-wireless-tracker-v2
14. Heltec WiFi LoRa 32 V4 with GPS, about 30 EUR, listing seen 2026-11-01:
    https://heltec.org/project/wifi-lora-32-v3/ (the V4 kit is listed at
    OpenELAB; the price was not on a stable product URL, verify before ordering)
15. Heltec Mesh Node T114, 17.90 to 33.70 USD and 56.90 USD kit:
    https://heltec.org/project/mesh-node-t114 and
    https://muzi.works/products/heltec-t114-kit
16. Waveshare ESP32-S3-Touch-AMOLED-1.75-G, 39.99 USD and 46.04 EUR:
    https://www.waveshare.com/esp32-s3-touch-amoled-1.75.htm and
    https://openelab.io/products/waveshare-esp32-s3-amoled-touch-display
17. LilyGO T-Display P4, 119.30 USD:
    https://lilygo.cc/en-us/products/t-display-p4
18. Elecrow ThinkNode M4, about 69.90 EUR:
    https://www.berrybase.de/elecrow-thinknode-m4-powerbank-fuer-meshtastic-nrf52840-lora-868-915-mhz-gps-bluetooth-7000-mah
19. Seeed SenseCAP T1000-E, 39.90 USD:
    https://www.seeedstudio.com/SenseCAP-Card-Tracker-T1000-E-for-Meshtastic-p-5913.html
20. Seeed Wio Tracker 1110:
    https://www.seeedstudio.com/Wio-Tracker-1110-Dev-Board-p-5799.html
21. M5Stack Dial v1.1, 34.90 USD:
    https://shop.m5stack.com/products/m5stack-dial-v1-1
22. M5Stack Cardputer Adv, 29.90 USD:
    https://shop.m5stack.com/products/m5stack-cardputer-adv-version-esp32-s3
23. Adafruit ESP32-S3 TFT Feather and Ultimate GPS FeatherWing, 24.95 USD each:
    https://www.adafruit.com/product/5483 and
    https://www.adafruit.com/product/3133
24. Seeed L76K module, 10.90 USD:
    https://www.seeedstudio.com/L76K-GNSS-Module-for-Seeed-Studio-XIAO-p-5864.html
25. Raspberry Pi Pico 2 W, 7 USD:
    https://www.raspberrypi.com/products/raspberry-pi-pico-2/
26. Waveshare ESP32-P4-WIFI6 touch boards:
    https://www.waveshare.com/esp32-p4-wifi6-touch-lcd-3.4c.htm
27. Elecrow CrowPanel Advance:
    https://www.elecrow.com/wiki/CrowPanel_Advance_3.5inch_Arduino_Course_V13.html
28. ESP32-C6-DevKitC-1, 9 USD: DigiKey listing
29. ESP32-P4-Function-EV-Board:
    https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32p4/esp32-p4-function-ev-board/index.html
30. Nordic development kits, 39 to 48.95 USD:
    https://www.digikey.com/en/products/detail/nordic-semiconductor-asa/NRF54L15-DK/25601516
    and
    https://www.digikey.com/en/products/detail/nordic-semiconductor-asa/NRF5340-DK/8593726
    and
    https://www.digikey.com/en/products/detail/nordic-semiconductor-asa/NRF52840-DK/8593726
31. STM32WBA65I-DK1, 80 to 100 USD:
    https://www.st.com/en/evaluation-tools/stm32wba65i-dk1.html and
    https://docs.zephyrproject.org/latest/boards/st/stm32wba65i_dk1/doc/index.html
32. Silicon Labs XG24-EK2703A, 43.04 USD: DigiKey listing
33. NXP FRDM-MCXW71:
    https://docs.zephyrproject.org/latest/boards/nxp/frdm_mcxw71/doc/index.html
34. Ambiq Apollo4 Blue Plus EVB, 249.99 USD:
    https://ambiq.com/product/apollo4-blue-plus
35. Alif Balletto B1 DK, 151 USD and 51 USD StartKit:
    https://alifsemi.com/support/kits/ensemble-e1cdevkit/ and
    https://www.digikey.com/en/products/detail/alif-semiconductor/DK-B1/29296119
36. Seeed XIAO nRF54L15, 9.90 USD:
    https://www.seeedstudio.com/XIAO-nRF54L15-p-6493.html
37. u-blox MIA-M10Q data sheet, sensitivity, current and AssistNow:
    https://content.u-blox.com/sites/default/files/documents/MIA-M10Q_DataSheet_UBX-22015849.pdf
38. u-blox MAX-M10S data sheet:
    https://content.u-blox.com/sites/default/files/MAX-M10S_DataSheet_UBX-20035208.pdf
39. u-blox NEO-F10N L1/L5 data sheet and price:
    https://www.u-blox.com/en/product/neo-f10n-module and
    https://content.u-blox.com/sites/default/files/documents/NEO-F10N_DataSheet_UBX-23002117.pdf
40. u-blox dual-band multipath white paper:
    https://content.u-blox.com/sites/default/files/documents/GPS-signals-migration-wp.pdf
41. Quectel L76K sensitivity, current and TTFF:
    https://wiki.seeedstudio.com/get_start_l76k_gnss/
42. M5Stack Unit GPS v1.1 data sheet:
    https://docs.m5stack.com/en/unit/Unit_GPS_v1.1
43. furble power audit, S3 current model and display split:
    plans/98-power-optimization-audit.md and plans/implemented/07-ble-sleep.md
44. Bangle.js 2 specifications, GPS and current figures:
    https://www.espruino.com/Bangle.js2 and
    https://shop.espruino.com/banglejs2
