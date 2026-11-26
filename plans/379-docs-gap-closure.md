# 379: component and protocol documentation gap closure

Status: implemented on `docs/component-protocol-gaps`, rebased onto
`docs/datasheet-corpus` (#475) and reconciled with the corpus page set.
Documentation only. No firmware, test or simulator behaviour changed.

## Motivation

The documentation described behaviour, settings, boards and camera research, but
not the hardware below the application and not the wire formats the firmware
implements. A reader could learn that the StickS3 has a PMIC and that the M10
speaks UBX, and then had to read `src/` to find an address, a register bit, a
topic name, a characteristic UUID or a frame layout.

Two specific holes made this expensive:

- There was no `docs/components/` directory at all, so every chip the project
  models was documented only by a header comment in `lib/hal/` or a constant in
  `src/`.
- The companion BLE service, the MQTT topic tree and the IR frame timings existed
  only in code. The companion service UUID did not appear anywhere under `docs/`.

This plan records the inventory, closes every gap the repository and the located
documents can answer, and leaves the rest as a shopping list with the exact
document and section each open row needs.

## Method

The inventory came from the repository, not from assumptions:

1. List every page under `docs/`, including `docs/components/` and
   `docs/reference/`, which turned out not to exist.
2. Grep `src/`, `include/`, `lib/hal/` and `sim/shim/` for chip names and for
   register, address, pin and timing constants.
3. Use the simulator manifests as the map of modelled hardware:
   `sim/coverage/hardware_quirks.json` (198 entries across 9 board profiles),
   `sim/hardware_model.cpp`, `sim/ubx_m10.cpp` and `sim/shim/M5PM1.h`.
4. For each protocol, read the codec header rather than the driver, because the
   codec states the frame and the driver only uses it.
5. Compare every constant against `docs/`, and treat a constant with no doc hit
   as a gap.

The second pass read the documents the first pass could not reach. Every row in
[closed by the reconciliation](#closed-by-the-reconciliation) records the
revision and the page the fact was read on. Every row still open records the
document and section it needs.

## Page names

`docs/datasheet-corpus` (#475) independently created `docs/components/` with one
page per part, named after the part: `at6668.md`, `axp192.md`, `axp2101.md`,
`bm8563.md`, `bmi270.md`, `bq25896.md`, `bq27220.md`, `ip5306.md`,
`m5pm1.md`, `mpu6886.md`, `qmi8658.md`, `sx1262.md`, `tca8418.md`,
`ublox-max-m10s.md`, `ublox-mia-m10q.md`, `w5500.md` and `xl9555.md`, plus the
panel and touch pages. This branch had created a parallel set with subsystem
prefixes: `pmic-m5pm1.md`, `pmic-axp192-ip5306.md`, `pmic-axp2101.md`,
`fuel-gauge-bq27220.md`, `io-expander-xl9555.md`, `radio-sx1262.md`,
`ethernet-w5500.md`, `imu-bmi270.md`, `imu-mpu6886.md`, `gnss-at6668.md`,
`gnss-ublox-m10.md`, `display-panels.md`, `rtc-bm8563.md`, `peripherals.md`.

The corpus names won, for four reasons that all outlive this pull request:

1. `tools/check_datasheet_citations.py` maps a `lib/hal` driver stem to a page
   name in `PAGE_BY_STEM`, and every entry is the part name.
2. `docs/power/datasheet-register.md` rows cite the part pages by those names.
3. Four in-flight branches (GNSS, power, SoC, display verification) reference the
   same names, and the GNSS one already stacks on #475.
4. A part name is stable. `pmic-axp192-ip5306.md` had to be split the moment the
   two parts needed different revisions, while `axp192.md` did not.

The content of every parallel page was folded into the part page before the file
was deleted. Two pages were not part pages and survived unchanged in role:
[`board-map.md`](../docs/components/board-map.md) holds the per-board pin,
address, display-policy and peripheral constants, and `README.md` is the index.

## Severity

| Level | Meaning |
| :--- | :--- |
| High | A reader cannot use or safely modify the subsystem without the missing fact. |
| Medium | A reader can use it but cannot verify, tune or reproduce the behaviour. |
| Low | Navigation or completeness only. |

## Gap register

### Components

| ID | Page | Missing | Severity | Source | Status |
| :--- | :--- | :--- | :--- | :--- | :--- |
| C-01 | `docs/components/` | The directory and its index did not exist | High | repository | closed, `docs/components/README.md` |
| C-02 | `docs/components/m5pm1.md` | M5PM1 operations, watchdog, LED, timed wake, retry-once rule | High | repository | closed |
| C-03 | `docs/components/axp192.md` and `ip5306.md` | AXP192 ADC trim registers, IP5306 capability shape | High | repository | closed |
| C-04 | `docs/components/axp2101.md` | AXP2101 register map, rail model, protected mask, VRTC rule | High | repository | closed |
| C-05 | `docs/components/bq27220.md` | BQ27220 command map and read contract | Medium | repository | closed |
| C-06 | `docs/components/xl9555.md` | XL9555 register set and the write-before-direction rule | Medium | repository | closed |
| C-07 | `docs/components/sx1262.md` | SX1262 sleep sequence, timing constants, NSS rule | Medium | repository | closed |
| C-08 | `docs/components/w5500.md` | W5500 pin map, SPI settings, state model, error paths | High | repository | closed |
| C-09 | `docs/components/bmi270.md` | BMI270 registers, feature window layout, tuning values | High | repository | closed |
| C-10 | `docs/components/mpu6886.md` | MPU6886 registers, wake-on-motion values, disarm restore | High | repository | closed |
| C-11 | `docs/components/at6668.md` | AT6668 `$PCAS` command table, CASIC frames, timing constants | High | repository | closed |
| C-12 | `docs/components/ublox-max-m10s.md` and `ublox-mia-m10q.md` | The M10 message set, detection, plan rules, power save | High | repository | closed |
| C-13 | `docs/components/st7789.md`, `ili934x.md`, `st7735.md`, `board-map.md` | Panels, touch, sleep dwell, LVGL buffer maths, brightness floor | High | repository | closed |
| C-14 | `docs/components/bm8563.md` | BM8563 registers, timer arithmetic, wake bounds | Medium | repository | closed |
| C-15 | `docs/components/board-map.md#board-peripherals` | SD, IR emitter, speaker, LED, USB constants | Medium | repository | closed |
| C-16 | `docs/components/board-map.md` | No pin, address or bus map existed anywhere under `docs/` | High | repository | closed |
| C-17 | `docs/components/m5pm1.md#open-rows` | M5PM1 register map, PWR_CFG bit 4, watchdog and RTC RAM rules | Medium | external | closed, see the reconciliation table. Remaining asks are C-17a and C-17b |
| C-18 | `docs/components/axp192.md#open-rows` | AXP192 register map and current scaling | Medium | external | closed, see the reconciliation table. Remaining asks are C-18a and C-18b |
| C-19 | `docs/components/axp2101.md#open-rows` | AXP2101 identity register, REG 90 bit 4, REG 91 bit 0 | Low | external | closed for REG 90 bit 4 and REG 91 bit 0. The identity register stays open as C-19a |
| C-20 | `docs/components/ip5306.md#open-rows` | IP5306 gauge thresholds and register map | Low | external | closed for the register map and the light-load timeout. The gauge thresholds stay open as C-20a |
| C-21 | `docs/components/bq27220.md#open-rows` | BQ27220 status bits beyond two, current scaling | Low | external | closed, see the reconciliation table. Remaining asks are C-21a to C-21c |
| C-22 | `docs/components/xl9555.md#open-rows` | XL9555 interrupt, reset and electrical data | Low | external | closed for the interrupt and the reset behaviour. The electrical table stays open as C-22a |
| C-23 | `docs/components/sx1262.md#open-rows` | SX1262 SPI framing, DIO handling, current | Low | external | open, C-23a to C-23c. The reset, BUSY and switching timings are closed |
| C-24 | `docs/components/w5500.md#open-rows` | W5500 register, PHY power-down and SPI timing | Medium | external | closed for the reset timing, the PHY power-down current and the SPI timing. The register map stays open as C-24a |
| C-25 | `docs/components/bmi270.md#open-rows` | BMI270 feature offsets, register 0x53 bits, power-on sequence | Medium | external | closed for the feature layout and the 0x53 bit fields. The power-on sequence stays open |
| C-26 | `docs/components/mpu6886.md#open-rows` | MPU6886 wake-on-motion semantics, filter values, GPIO 35 limits | Medium | external | closed for the threshold registers and the vendor defect. The mg per count stays open as C-26a |
| C-27 | `docs/components/at6668.md#open-rows` | AT6668 `$PCAS` set, constellation numbering, CFG-NAVX map, MON-HW | High | external | closed for the `$PCAS` set and the constellation numbering. CFG-NAVX and MON-HW stay open as C-27c and C-27d |
| C-28 | `docs/components/ublox-max-m10s.md#open-rows` and `ublox-mia-m10q.md#open-rows` | NAV-PVT field offsets, PM enumeration, AssistNow, flash layer | Medium | external | open, C-28a to C-28g. The PSM and BeiDou B1C errata and the MIA default constellation are closed |
| C-29 | `docs/components/st7789.md#open-rows` and `ili934x.md#open-rows` | Panel command tables, sleep timing, FT6336U, touch current, StickS3 panel id | High | external | closed for the sleep timing on both panels. Command tables, the FT6336U and the StickS3 panel id stay open |
| C-30 | `docs/components/bm8563.md#open-rows` | BM8563 register map, control 2 bits, accuracy | Low | external | closed for the register map and the timer bits. Accuracy stays open as C-30a |
| C-31 | `docs/components/board-map.md#open-rows` | SD timing, speaker amplifier, WS2812 timing, USB electricals | Low | external | closed for the USB clock dependency. SD, the amplifier and the WS2812 stay open as C-31a to C-31c |

### Protocols

| ID | Page | Missing | Severity | Source | Status |
| :--- | :--- | :--- | :--- | :--- | :--- |
| P-01 | `docs/protocols/gnss-ubx.md` | Frame layout, message set, 29 configuration keys, layers, plan rules | High | repository | closed |
| P-02 | `docs/protocols/gnss-casic-pcas.md` | CASIC frame, checksum quirk, `$PCAS` table, CFG-NAVX, ephemeris cache | High | repository | closed |
| P-03 | `docs/protocols/gnss-nmea.md` | Which sentences are parsed, talker mapping, date validation | Medium | repository | closed |
| P-04 | `docs/protocols/camera-remotes.md` | Advertisement matchers, per-vendor services, opcodes, pairing stages | High | repository | closed |
| P-05 | `docs/protocols/ir-remote.md` | Nikon, Sony and Canon IR timings, RMT configuration, error paths | Medium | repository | closed |
| P-06 | `docs/protocols/mqtt.md` | Topic tree, payloads, command semantics, Home Assistant discovery, gates | High | repository | closed |
| P-07 | `docs/protocols/companion-gatt.md` | Service and characteristic UUIDs, every record layout, auth, provisioning | High | repository | closed |
| P-08 | `docs/protocols/companion-gatt.md#provisioning-tlv` | Provisioning TLV record format, tags, limits, error codes | Medium | repository | closed |
| P-09 | `docs/protocols/companion-gatt.md#camera-list-blob` | Saved camera index blob formats and the v2 marker | Medium | repository | closed |
| P-10 | `docs/protocols/ble-hid.md` | The HID surface was undocumented; the code does not implement HID | Medium | repository | closed |
| P-11 | `docs/console-commands.md#ntp`, `#provision` | `ntp` and `provision` had no section, and the TLV blob format was undocumented | Medium | repository | closed |
| P-12 | `docs/protocols/gnss-nmea.md#open-rows` | NMEA 0183 version the AT6668 emits | Low | external | closed, see the reconciliation table |
| P-13 | `docs/protocols/camera-remotes.md#open-rows` | Vendor protocol specifications for every camera family | High | external | open |
| P-14 | `docs/protocols/ir-remote.md#open-rows` | Authoritative IR timing and per-camera IR support | Medium | external | open |
| P-15 | `docs/protocols/mqtt.md#open-rows` | MQTT conformance level, Home Assistant discovery schema revision | Low | external | open |
| P-16 | `docs/protocols/companion-gatt.md#open-rows` | GATT security requirements, app behaviour, OTA characteristic protocol | Medium | external | open |
| P-17 | `docs/protocols/ble-hid.md#open-rows` | HID over GATT specification and usage tables | Low | external | open |

### Index and navigation

| ID | Page and section | Missing | Severity | Source | Status |
| :--- | :--- | :--- | :--- | :--- | :--- |
| I-01 | `docs/README.md` | There was no documentation index above the per-directory READMEs | High | repository | closed |
| I-02 | `docs/protocols/README.md` | No protocol index, and no statement of what counts as documented | Medium | repository | closed |
| I-03 | `docs/components/README.md` | No component index, and no status legend for shipped versus future parts | Medium | repository | closed |
| I-04 | `CLAUDE.md` Documentation table | The doc surface map had no row for component or protocol changes | Medium | repository | closed |
| I-05 | `plans/README.md` | This plan was not registered | Low | repository | closed |
| I-06 | every internal doc link | The relative link sweep checks all of them and finds 0 broken | High | repository | closed |

## Closed by the reconciliation

Every row below was open in the register above and is now answered by a document
that was opened, with the revision and the page the fact was read on. The
component page carries the same citation in its facts table.

| ID | Page | Document | Revision | Section and page | What was read |
| :--- | :--- | :--- | :--- | :--- | :--- |
| C-17 | `m5pm1.md` | M5PM1 Chip User Manual | V1.9, software dated 2026-01-06 | Section 2 registers (7), (10), (11), (12), (13), pages 13 and 15 of 33; section 11 RTC Buffer, page 25 of 33; section 5 System Timer, page 20 of 33 | `PWR_CFG` 0x06 default 0x17 with bit 4 the LED enable; `I2C_CFG` 0x09 `SLP_TO` bits 3:0 and the first-transaction-fails note; `WDT_CNT` 0x0A in seconds with 0 disabling; `WDT_KEY` 0x0B fed 0xA5; `SYS_CMD` 0x0C key 0xA, 0xA1 off and 11 Download; the 32-byte RTC buffer at 0xA0 to 0xBF; the 32-bit timer limit of 214,748,364 s |
| C-18 | `axp192.md` | AXP192 datasheet | V1.2, Aug.8 2011, 53 pages | REG 82H, 83H and 84H, page 41; REG 10H and 12H, page 34; electrical characteristics, pages 7 and 8 | REG 82H default 83H and its bit map, REG 83H default 80H, REG 84H default 32H with bits 7:6 selecting 25, 50, 100 and 200 Hz; EXTEN on REG 10H bit 2 and REG 12H bit 6; off mode 27 uA, suspend 86 uA, LDO2 and LDO3 quiescent 100 uA each |
| C-19 | `axp2101.md` | AXP2101 datasheet | Revision 1.0 and revision 1.4 | Section 6.13.2.75 `REG 90`, page 53 in both, and section 6.13.2.76 `REG 91`, page 53 | Revision 1.0 prints `aldo1 enable` on bit 4 as well as bit 0; revision 1.4 prints `bldo1 enable` on bit 4, `bldo2` on 5, `cpusldo` on 6 and `dldo1` on 7; REG 91 bit 0 is `dldo2 enable` with bits 7:1 read-only |
| C-20 | `ip5306.md` | IP5306 register document, distributed as `IIC_IP5306_REG_V1.4_cn.pdf` | V1.21, 2017 | Section 1 pages 1 and 2, `SYS_CTL2` page 4, registers 0x70, 0x71, 0x72 and 0x77 page 7 | `SYS_CTL2` 0x02 bits 3:2 of 11 is 64 s, 10 is 16 s, 01 is 32 s and 00 is 8 s; 0x70 bit 3 charging; 0x71 bit 3 full; 0x72 bit 2 light load; the address list stops at 0x77 and 0x78 is not in it; the 8-bit slave address is 0xEA |
| C-21 | `bq27220.md` | BQ27220 Technical Reference Manual | SLUUBD4A, revised November 2022 | Table 2-6 page 23, section 2.8 page 23, section 5.1 page 63, section 5.3 page 63 with the figures on page 64 | The full BatteryStatus low and high byte bit names with BATTPRES on bit 3 and DSG on bit 0; `Current()` is a signed value in mA updated every second, so the driver needs no scaling; the 7-bit address is 1010101; 66 us of bus-free time above 100 kHz and no more than two standard commands per second |
| C-22 | `xl9555.md` | XL9535, XL9555 product datasheet | Rev 2.3 | Section 9.1 Table 3 page 11, section 9.2.1 Table 6 page 12, section 9.3 page 14, section 9.5 page 15 | The address byte and 0x20 to 0x27; the command byte map with the 1111 1111 output power-up default; the power-on reset behaviour and the below-0.2 V rule; the interrupt is edge triggered on inputs and clears when the input port is read |
| C-24 | `w5500.md` | W5500 datasheet | Version 1.1.0, 17DEC2022 | Pin description page 10, section 5.3 page 59, section 5.5.1 page 60, section 5.5.4 page 61 | 500 us minimum reset low and 1 ms maximum to PLL lock; IDD1 132 mA and IDD2 13 mA in PHY power down; FSCK 80 MHz theoretical and 33.3 MHz guaranteed, so 20 MHz is inside both |
| C-25 | `bmi270.md` | BMI270 datasheet | Revision 1.6, document BST-BMI270-DS000-08 | Section 5.2.40 `FEATURES[16]`, pages 88 to 92; sections 5.2.58, 5.2.60, 5.2.61, 5.2.63, 5.2.84 and 5.2.85 | ANYMO_1 at feature address 0x3C with the duration in 50 Hz samples, ANYMO_2 at 0x3E with the threshold over 0 to 1 g and default 0xAA; `INT1_IO_CTRL` 0x53 bits 1 to 4 as `lvl`, `od`, `output_en` and `input_en`; the `PWR_CTRL` section is 5.2.85 on page 120 |
| C-26 | `mpu6886.md` | MPU-6886 datasheet, DS-000193 | Revision 1.1, 59 pages | Sections 8.20 to 8.22 pages 39 and 40, and the wake-on-motion procedure page 24 | Registers 32 to 34 (0x20 to 0x22) are one threshold byte per axis and state no mg per LSB; the page 24 step 4 names register 0x1F, which does not exist |
| C-27 | `at6668.md` | CASIC BDS/GNSS receiver interface protocol and DS-AT6668 | V6.3.2 revision R6, 2025-06-24, and DS-AT6668 V1.4 | CASIC R6 sections 2.7.3 to 2.7.12, pages 35 to 43; CASIC V4.2.0.3 section 1.6.5 page 28; DS-AT6668 section 4.3 page 9 | `$PCAS02` interval, `$PCAS03` sentence order, `$PCAS04` hex bitmask per band, `$PCAS10` the four restarts, `$PCAS11` the dynamic model, and the older decimal 1 to 7 table; 37 mA typical, 8 uA backup, 30 uA sleep, 2.5 mA antenna detect and 160 ms reset |
| C-29 | `st7789.md` and `ili934x.md` | ST7789V2 datasheet and ILI9342C datasheet | ST7789V2 V1.0, ILI9342C V101 | ST7789V2 sections 9.1.11 and 9.1.12, pages 184 and 186; ILI9342C sections 8.2.11 and 8.2.12, pages 100 and 101 | Sleep In and Sleep Out each need 5 ms before the next command and 120 ms between Sleep Out and the next Sleep In, in both documents |
| C-30 | `bm8563.md` | BM8563 datasheet, Chinese | V1.1 | Table 6 page 9, control and status register 2 table page 8, tables 21 and 22 pages 11 and 12, table 23 page 12 | The voltage-low flag is bit 7 of 02H; `TE` is bit 7 and `TD1`/`TD0` bits 1:0 of 0EH with 10 for 1 Hz and 11 for 1/60 Hz, which are the values the driver writes; the countdown period is n over the clock frequency |
| C-31 | `board-map.md` | ESP32-S3 Technical Reference Manual | v1.8 | Section 33.4, page 1253 | USB Serial/JTAG needs the PLL and APB_CLK, 40 MHz or more for compliant operation, and APB is gated in light sleep |
| P-12 | `docs/protocols/gnss-nmea.md` | CASIC BDS/GNSS receiver interface protocol | V6.3.2 revision R6 | Section 1.1 page 1 and the NMEA version note page 4 | The receiver is NMEA 0183 and defaults to 4.11 with 2.3 and 3.x compatibility, BeiDou uses talker `BD` up to 4.10 and `GB` from 4.11 |

## Still open

The rows below are the remainder after the closures above. Each names the exact
document and section, and appears in the open-rows table of the page it belongs
to.

| ID | Page | Document and section needed | Why it is still open |
| :--- | :--- | :--- | :--- |
| C-17a | `m5pm1.md` | M5Unified `M5PM1.h`, against M5PM1-UM section 2 register (10) page 15 | The library source, not a document, holds the `SLP_TO` value |
| C-17b | `m5pm1.md` | BMI270-DS section 5.2.36 and M5PM1-UM GPIO registers, pages 17 to 19 of 33 | No document states the wake polarity pairing |
| C-18a | `axp192.md` | AXP192-DS battery current ADC section and the board schematic sense resistor | A board fact as well as a document fact |
| C-18b | `axp192.md` | AXP192-DS REG 33H to 35H, pages 37 and 38 | furble does not read charger state itself yet |
| C-19a | `axp2101.md` | X-Powers AXP2101, a statement of the register 0x03 value | Revision 1.0 defines no identity register, revision 0.1 prints `01_0111` on page 19, which is not the 0x4A the driver expects |
| C-20a | `ip5306.md` | Injoinic register document, an address list that includes 0x78, or the level-register application note | V1.21 stops at 0x77 and the datasheet tabulates no thresholds |
| C-21a | `bq27220.md` | SLUUBD4A section 2.11 page 23, read as a body | The 0xFFFF sentinel was read as a heading only |
| C-21b | `bq27220.md` | SLUUBD4A CEDV configuration and data memory chapters, plus the pack profile | The driver is read-only by design |
| C-21c | `bq27220.md` | Board schematic sense resistor | `Current()` is already in mA, so this is a board fact |
| C-22a | `xl9555.md` | XL9555-DS section 6.5 Electrical Characteristics page 5, read as a table | Heading only was read |
| C-22b | `xl9555.md` | Board schematic for A2, A1, A0 strapping | No document can settle it |
| C-23a | `sx1262.md` | DS.SX1261-2.W.APP section 9.2 SPI interface | The driver writes two bytes and relies on the board adapter |
| C-23b | `sx1262.md` | DS.SX1261-2.W.APP section 9.3 and the GPIO table | Not needed for sleep, needed for any transmit use |
| C-23c | `sx1262.md` | Board schematic plus section 3 current table | Plan 322 lists both ALDO3 options |
| C-24a | `w5500.md` | W5500-DS register descriptions, pages 29 to 57 | Read as a section list; ESP-IDF owns the access |
| C-24b | `w5500.md` | W5500-DS section 5.4 Power Dissipation, page 60 | Not read line by line |
| C-24c | `w5500.md` | W5500-DS section 2.2 and the ESP-IDF W5500 PHY configuration | A stack question |
| C-25a | `bmi270.md` | BST-BMI270-DS000 power-on sequence and soft reset | The driver relies on the board support library's `begin()` |
| C-26a | `mpu6886.md` | MPU6886-DS sections 8.20 to 8.22 state no mg per LSB, so a vendor application note or a bench calibration | The datasheet does not state it |
| C-26b | `mpu6886.md` | MPU6886-DS section 6 and the StickC schematic | A board fact as well |
| C-26c | `mpu6886.md` | An InvenSense errata sheet | None was found, and revision 1.1 still carries the page 24 misprint |
| C-27a | `at6668.md` | CASIC R6 section 2.7.13 CAS12 | Heading only was read |
| C-27b | `at6668.md` | CASIC R6 section 2.7.12 CAS11 page 43 and the AT6668-DS platform table | The grammar does not enumerate the models |
| C-27c | `at6668.md` | CASIC R6 CFG-NAVX section | The code asserts four offsets and a size |
| C-27d | `at6668.md` | CASIC R6 MON-HW section, the 2.14.2 of the older numbering | The code says the values need a live unit |
| C-27e | `at6668.md` | A bench measurement with a known antenna and a fitted backup cell | The 108 s observation against the 23 s specification is a measurement gap |
| C-28a | `ublox-max-m10s.md` | IFD R03 sections 3.15.11 and 3.15.13, read field by field | The pages cite the section list only |
| C-28b | `ublox-max-m10s.md` | DS-MAX current consumption tables | Read for the revision only |
| C-28c | `ublox-max-m10s.md` | IFD R03 AssistNow sections and the service description | Only AssistNow Autonomous is used |
| C-28d | `ublox-max-m10s.md` | DS-MAX flash file system section and a bench attempt | Recorded as an expectation |
| C-28e | `ublox-mia-m10q.md` | DS-MIA interface section, the UART default row | The table cited for the 38400 default did not carry it |
| C-28f | `ublox-mia-m10q.md` | DS-MIA current consumption table | Only the default constellation was read |
| C-28g | `ublox-mia-m10q.md` | DS-MIA feature table and IM-MIA | Only the constellation default was compared |
| C-29a | `st7789.md` | M5StickS3 schematic | No document in the tree names the panel part |
| C-29b | `st7789.md` | ST7789V2-DS section 9 | M5GFX owns the traffic |
| C-29c | `st7789.md` | ST7789V2-DS FRCTRL2 and the porch registers | Only the power model cites FRCTRL2 |
| C-29d | `ili934x.md` | ILI9342C has no supply current table, so an ILITEK application note or a measurement | No document exists in the tree |
| C-29e | `ili934x.md` | FocalTech FT6336U datasheet | No register is touched by this repository |
| C-30a | `bm8563.md` | A bench measurement over temperature | There is no trim register and no trim path |
| C-30b | `bm8563.md` | An English BM8563 V1.1 | Belling publishes only the Chinese revision |
| C-31a | `board-map.md` | SD Physical Layer Simplified Specification, bus timing chapter | The firmware fixes 10 MHz with no cited limit |
| C-31b | `board-map.md` | M5Stack Core schematic and the amplifier datasheet | Not in the tree |
| C-31c | `board-map.md` | WS2812B datasheet, timing table | M5Unified drives it |
| P-13 | `docs/protocols/camera-remotes.md` | One vendor BLE or SDK document per family | None was located |
| P-14 | `docs/protocols/ir-remote.md` | Nikon ML-L3 service manual or a captured waveform, Sony SIRC, Canon RC-1 | None was located |
| P-15 | `docs/protocols/mqtt.md` | OASIS MQTT 3.1.1 or 5.0, Home Assistant discovery documentation | None was retrieved |
| P-16 | `docs/protocols/companion-gatt.md` | Bluetooth Core Specification, Android and Apple BLE notes, the OTA plan | None was retrieved |
| P-17 | `docs/protocols/ble-hid.md` | Bluetooth SIG HID over GATT Profile, USB HID Usage Tables | None was retrieved |

## Counts

| Group | Closed | Open |
| :--- | ---: | ---: |
| Components | 29 | 43 |
| Protocols | 12 | 5 |
| Index | 6 | 0 |
| **Total** | **47** | **48** |

The component count moves in both directions: 16 rows that the repository alone
answered, 13 rows that the reconciliation closed with a document, and two rows
(C-23 and C-28) that stay open as rows. The open count rises because every
closure above split a bundled row into the asks that remain, which is the point
of the register: a row that says "SPI, DIO and current" hides which of the three
is still unknown.

## What changed

New pages: eight under `docs/protocols/`, one protocol index, and
`docs/README.md`, which are new to this branch. `docs/components/` is the corpus
tree, extended with the content of the 14 parallel pages this branch had created
and then deleted.

Rewritten or extended: `docs/console-commands.md` (two new sections),
`CLAUDE.md` (three mapping rows), `plans/README.md` (one register row),
`docs/components/README.md` (support status column, canonical names, board map),
`docs/components/board-map.md` (display policy, board peripherals).

## Deliberate omissions

- No vendor PDF is committed. `lib/hal/CLAUDE.md` forbids it.
- The `HID_GENERIC_REMOTE` decision is recorded, not implemented, see FU-01.
- `docs/hardware-catalog.md` stays a buying guide. It is not a component
  reference and this plan does not merge the two.
- `tools/power-model/board-currents.yaml` still carries the AT6558 proxy rows for
  the GPS unit. The AT6668 page states the datasheet figure, and the power
  verification branch owns the YAML change so the two do not conflict.

## Validation

- `sh sim/scripts/check-doc-tokens.sh` passes, with and without `--links`.
- The relative link sweep over `README.md`, `CONTRIBUTING.md`, `AGENTS.md`,
  `CLAUDE.md`, `docs/`, `plans/`, `sim/` and `tools/` checks every link and finds
  0 broken. Every fragment link into a reconciled page resolves against that
  page's headings.
- `python3 tools/pre_review.py --base origin/docs/datasheet-corpus` passes.
- Every `file:line` citation in the pages was taken from the tree at the commit
  this branch is stacked on. A citation that points at the wrong line is a bug in
  the page and should be fixed with the code change that moved it.

## Follow-ups

- FU-01: `lib/furble/Scan.h:181` defines `HID_GENERIC_REMOTE = 0x180`, which is
  not the Bluetooth assigned HID over GATT service number 0x1812, and nothing
  reads it. Either remove the constant or correct it to 0x1812 and wire it into a
  scan filter, in the pull request that first wants a HID scan. Named because the
  fix is a code change and this pull request is documentation only. See
  [the BLE HID page](../docs/protocols/ble-hid.md#decision-on-the-value).
- FU-02: four source comments in `src/FurbleIMUMotionBMI270.cpp` still carry the
  old section numbers, at `:39`, `:45`, `:62` and `:72`. `:72` names section
  5.2.45 for `PWR_CTRL`, which is 5.2.85 on page 120. A firmware comment change,
  left out of this pull request for the same reason.
- FU-03: `tools/power-model/board-currents.yaml` carries the AT6558 23 mA proxy
  for the GPS unit, which the AT6668 datasheet retires in favour of 37 mA typ at
  `VDD_IO` 3.3 V, page 9. The power verification branch owns that file; this
  branch only records the datasheet value and the retirement.
- The research pass that acquires the documents in the open table should file
  each one against its row and replace the open row with a cited limit.
- When plan 322 lands its first non-M5 board, the `future` rows in
  [the index](../docs/components/README.md#support-status) become `shipped`.
