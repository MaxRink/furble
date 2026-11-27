# 390 - LilyGO T-Beam Supreme port

Status: port in this PR. The firmware environment builds, the board row is
host-tested, and the simulator runs the new panel class. No device has run this
image and no measurement backs a single number in it. The GPS path is the
best-supported part of the port; the receiver is chosen at runtime, so both
T-Beam trims share one image.

The plan is numbered 390 because 365 and 367 were both claimed twice on master
(`365-core-sim-align.md` and `365-review-feedback.md`, both from PR 513), 366 is
the receiver decision this port answers to, and 367 was claimed by the plan
renumbering itself. It was written as 365 against master `79df13a7`, renumbered
to 367 on the rebase onto `baa4ac23`, and to 390 by the arbiter's renumbering.

Related: [318](318-next-board-battery-gps.md) (the board decision and the
32.768 kHz finding), [322](implemented/322-hardware-abstraction.md) (the
`lib/hal` seams and the profile row), [383](383-hardware-candidates.md) (the
port-cost ranking that put this board first, numbered 364 before the plan
renumbering), [366](366-gps-receiver-selection.md) (the receiver decision, its
lever ordering and the UNVERIFIED backup domain),
[376](376-board-level-documents.md) (the board-level drawings and what they do
and do not settle about V_IO and V_BCKP), [336](336-gps-pin-seam.md) (the GPS
pin seam this port uses), [321](implemented/321-ublox-m10-driver.md) (the UBX
backend the MAX-M10S trim needs).

## Why this board, and why the receiver moved to the front

Port cost and GPS quality point at the same first target. Every chip is already
in tree: the AXP2101 and SX1262 in `lib/hal`, the SH1106 in M5GFX's
`Panel_SH110x`, the `$PCAS` and UBX GPS backends in `src/`. The board also has
the two hardware levers plan 318 was looking for: a 32.768 kHz crystal on
`XTAL_32K` and a receiver with a backup supply.

The deciding factor for this port is the GPS, not the port cost. The T-Beam
Supreme is sold with either a Quectel L76K or a u-blox MAX-M10S. The M10 is the
stronger receiver class, and plan 366 states the size of the advantage exactly:
-167 dBm tracking against -162 dBm for the L76K and the AT6668, with cold-start
sensitivity flat at -146 to -148 dBm across the whole class. The 5 dB is a
tracking advantage, the one that holds a fix under trees and at a window, not an
indoor acquisition one. Plan 366 also puts the antenna and the backup domain
above the receiver part in the lever ordering, and it names this board's M10S
trim as its first option. None of that changes the port: the two trims are not
distinguished by the board's documents beyond the module name, the vendor
factory code uses one pin map for both, and the row must stay flexible about
antenna, backup supply and receiver. So this port wires the board's UART pins
and lets the existing MON-VER probe decide: a MAX-M10S takes the UBX backend, an
L76K keeps the NMEA and `$PCAS` one.

## Evidence

Every fact below is tagged. `[vendor]` is the LilyGO wiki page for the product,
read 2026-11-27 at the revision that documents T-Beam-Supreme V3.0; its sections
Product Parameters, I2C Device Addresses, AXP2101 Power Channels and Pin
Mapping are the ones quoted. `[schematic]` is the LilyGO schematic set named in
plan 318's source list. `[vendor code]` is
`examples/Factory/utilities.h` and `examples/Factory/LoRaBoards.cpp` in
`Xinyuan-LilyGO/LilyGo-LoRa-Series` at master, branch `T_BEAM_S3_SUPREME`.
`[datasheet]` is a part document. `[repo]` is this checkout. `[inferred]` is a
conclusion no source states.

| Fact | Value | Source |
| :--- | :--- | :--- |
| SoC, flash, PSRAM | ESP32-S3FN8, 8 MB flash, 8 MB PSRAM (QSPI) | [vendor] Product Parameters |
| Geomagnetic sensor | QMC6309 or QMC6310U/N at 0x1C, 0x3C or 0x7C on bus 0 | [vendor] I2C Device Addresses |
| Environment sensor | BME280 at 0x77 on bus 0 | [vendor] I2C Device Addresses |
| OLED | SH1106, 1.3 inch, 128x64, bus 0, address 0x3C or 0x3D | [vendor] Product Parameters, I2C Device Addresses |
| Display pins | I2C SDA 17, SCL 18 | [vendor] Pin Mapping and [vendor code] `I2C_SDA`/`I2C_SCL` |
| PMIC pins | AXP2101 at 0x34 on bus 1, SDA 42, SCL 41, IRQ 40 | [vendor] I2C Device Addresses, Pin Mapping; [vendor code] `I2C1_SDA`/`I2C1_SCL`/`PMU_IRQ` |
| RTC | PCF85063ATL per the feature list, PCF8563 per the address table and both schematics, at 0x51 on bus 1, IRQ 14 | [vendor] Product Parameters against I2C Device Addresses; [schematic] RTC block |
| GNSS UART | GNSS TX 8, GNSS RX 9, PPS 6 | [vendor] Pin Mapping; [vendor code] `GPS_RX_PIN` 9 and `GPS_TX_PIN` 8 |
| GNSS wake | GPIO7, marked "L76K only" | [vendor] Pin Mapping |
| Receiver | MAX-M10S or L76K, selectable | [vendor] Overview and Product Parameters |
| GPS rail | ALDO4, "GPS" | [vendor] AXP2101 Power Channels |
| Display, sensor, magnetometer rail | ALDO1, "BME280 + Display + MAG Sensor" | [vendor] AXP2101 Power Channels |
| Sensor rail, radio, SD, header | ALDO2 "Sensor", ALDO3 "Radio", BLDO1 "SD Card", BLDO2 "External pin header" | [vendor] AXP2101 Power Channels |
| GPS backup | "provided by the 18650 battery. Remove the battery and GPS hot start will not be available." | [vendor] AXP2101 Power Channels note |
| SoC rail and M.2 | DC1 "ESP32-S3", DC3/DC4/DC5 "External M.2 Socket" | [vendor] AXP2101 Power Channels |
| Rail voltages and order | ALDO4 3300 first, ALDO3 3300, cold-boot 250 ms cycle of ALDO1/ALDO2/BLDO1, then ALDO1, ALDO2, BLDO1, BLDO2 at 3300, DCDC3 3300, DCDC4 at its maximum, DCDC5 3300, and DCDC2, DLDO1, DLDO2 and VBACKUP off | [vendor code] `LoRaBoards.cpp`, branch `T_BEAM_S3_SUPREME` |
| BOOT key | GPIO0, active low | [vendor] Pin Mapping; [vendor code] `BUTTON_PIN` 0 |
| microSD | SCK 36, MISO 37, MOSI 35, CS 47, its own SPI bus | [vendor] Pin Mapping; [vendor code] `SDCARD_*` |
| LoRa | SX1262, SCK 12, MISO 13, MOSI 11, CS 10, RESET 5, DIO1 1, BUSY 4 | [vendor] Pin Mapping; [vendor code] `RADIO_*` |
| IMU | QMI8658, CS 34, INT 33 | [vendor] Pin Mapping; [vendor code] `IMU_CS`/`IMU_INT` |
| MAX-M10S pins 5, 6, 7 | EXTINT, V_BCKP, V_IO | [datasheet] MAX-M10S data sheet UBX-20035208 table 10 |
| MAX-M10S default UART rate | 38400 baud | [datasheet] MAX-M10S data sheet, default interface settings |
| L76K backup pin | V_BCKP is module pin 6, driven from VRTC on the board's schematic | [schematic] Radio and GNSS sheet, rev V3.0 |

Facts the documents do not settle, and what the code does about each:

- **The MAX-M10S trim's V_IO (module pin 7).** No M10S schematic exists. What
  supplies it is UNVERIFIED, and the code cannot fail closed about a pin it
  never touches: it drives no enable and no level on the GNSS connector, so a
  missing V_IO shows up as a receiver that never answers, not as a driven pin.
  Bench item 6 in plan 318 closes it.
- **GPIO7 on the MAX-M10S trim.** The wiki marks the wake row L76K only, and on
  the MAX-M10S that pad is EXTINT. The row has no GPS enable column and this
  image never writes GPIO7, so an EXTINT input is left alone on either trim.
- **The MAX-M10S trim's V_BCKP net.** The vendor statement is board level ("the
  18650 backs the GPS"); the pin-level path is traced only on the L76K sheet.
  The image therefore does not define `FURBLE_GPS_BACKUP_SUPPLY`. Being wrong
  in the conservative direction costs one UPD-SOS save, up to one second, before
  a rail cut; being wrong in the other direction costs a cold start.
- **Which RTC part is fitted.** The wiki feature list says PCF85063ATL where its
  own address table and both schematics say PCF8563. The two do not share a
  register map, so the row keeps `rtcBatteryBacked` false and `rtcOnBus` false:
  the calendar is not read, and TimeKeeper keeps its normal checkpoint budget.
  Both parts are battery backed, which is why the row's false is a code fact
  rather than a hardware claim.
- **The OLED address and the panel's column offset.** The address is strapped by
  R51 and R52 per radio band. The driver probes both 0x3C and 0x3D, always, and
  reads the acknowledgement from the transaction's stop condition, because
  M5GFX reports a NACK there rather than at the start. Both addresses answering
  selects 0x3D: the board cannot carry two devices at one address and the
  magnetometer is the one that also answers 0x3C, so a panel at 0x3C and a
  magnetometer at 0x3C cannot both exist. Neither answering leaves the display
  unready and nothing is drawn. The column offset is `_cfg.offset_x`, applied by
  `Panel_SH110x::display()`; M5GFX 0.2.27 has no `setWindow()` on this class, so
  it is left at 0 and the measured value is a bench check.

## The board row

`src/FurbleBoard.cpp` gains `TBEAM_SUPREME`, selected by
`Board::compiledProfile()` under `FURBLE_HAS_AXP2101_HAL` instead of by
`M5.getBoard()`. The image pins M5GFX at the display-less StampS3 family only
to take the no-probe return before M5GFX drives another board's pins, so the
detected value is a lie about the hardware and must never pick a row.

| Column | Value | Why |
| :--- | :--- | :--- |
| `name` | T-Beam S | boot info block |
| `battery` | level, voltage, charging | AXP2101 E-gauge, VBAT and REG 01 bits 6:5; no current sense |
| `batteryCapacityMah` | 0 | the 18650 is the user's cell |
| `rtcBatteryBacked` | false | part unsettled and not read; see above |
| `rtcOnBus` | false | the RTC is on bus 1, not the bus M5Unified would use |
| `irPin` | -1 | no emitter |
| `motionWake`, `motionWakePin` | NONE, -1 | QMI8658 is on SPI and has no backend |
| `timedWake`, `powerHoldPin` | NONE, -1 | no PMIC wake timer; the RTC alarm is not wired |
| `pmicClickStreak` | false | the AXP2101 key reports its own presses |
| `sdSlot` | false | the slot is on its own SPI bus, not the display bus |
| `canPowerOff`, `powerOffPath` | true, true | the AXP2101 has a soft power-off command |
| `modemSleep` | false | the setting owns it; the crystal makes it viable |
| `minBrightness` | 32 | the panel's floor is a contrast value, a placeholder as plan 322 records |
| `feedbackSound/Light/Vibration`, `ledPin` | false, false, false, -1 | no speaker, LED or motor |
| `gpsRail` | true | ALDO4 has its own enable bit |
| `ledActiveLow`, `irMarkLow` | false, false | no driven stage |
| `gpsPinSource`, `gpsRxPin`, `gpsTxPin` | PROFILE, 9, 8 | the board's UART, from the vendor pin map |

## What this PR builds

1. Room for the board in the capability set. `FURBLE_HAS_AXP2101_HAL` and
   `FURBLE_PANEL_128X64` are new; `FURBLE_HAS_SD`, `FURBLE_HAS_INTERNAL_I2C`
   and `FURBLE_HAS_USB_SERIAL_JTAG_CONSOLE` gain the new board. The 128x64
   panel joins `FURBLE_PANEL_NARROW`, so it takes 24 px icons and the short
   unit labels.
2. `include/FurbleDisplay.h` and `src/FurbleDisplay.cpp`: the display seam the
   UI now draws through. On the M5 boards it forwards to `M5.Display` from
   `src/FurblePlatform.cpp`, byte for byte the object the UI used before. On the
   T-Beam it owns an `LGFX_Device` with a 128x64 class derived from
   `Panel_SH110x` over `Bus_I2C` on port 0 at SDA 17 and SCL 18. It probes both
   strapped addresses with an address-only transaction and takes the
   acknowledgement from the stop condition, where M5GFX reports the NACK, then
   configures 64 rows and multiplex 0x3F to match the module. Both addresses
   answering selects 0x3D because the magnetometer owns 0x3C on that variant;
   neither answering leaves the display unready and nothing is drawn. That
   decision is `Display::Detail::choosePanelAddress()` in the header, and
   `display-probe` drives it with a bus double that NACKs. The UI's touch
   calibration call is a compile-time absence on this panel.
3. `src/FurblePlatform.cpp`: the T-Beam power backend under
   `FURBLE_HAS_AXP2101_HAL`. It opens I2C port 1 at SDA 42 and SCL 41, probes the
   AXP2101 at 0x34 three times before giving up, brings the rails up in the
   vendor order, enables the power key interrupt, and reports battery level,
   voltage and charge direction from the driver. The retry matters because a
   failed identity read disables the rails, the gauge and power off while the
   row still advertises a power-off path, and the 0x4A identity value is
   inferred rather than documented (`docs/components/axp2101.md` row 17,
   C-19a). `setRail()` maps `Rail::EXT_5V`, the only rail, to ALDO4 with a
   readback and one retry. `powerOff()` is the driver's soft power off.
   `Platform::update()` polls the PMIC key at 50 Hz into `M5.BtnPWR` and
   `getPWRClickCount()`, and samples BOOT on GPIO0 into `M5.BtnA`.
4. `platformio.ini` and `sdkconfig.lilygo-tbeam-supreme`: an 8 MB dual-OTA
   environment, MQTT and WebUI on, BMI270 driver compiled out, the
   `partitions_two_ota_8m.csv` layout, quad PSRAM instead of the StickS3's
   octal, the RTC clock on the external 32.768 kHz crystal, and the Bluetooth
   controller's low-power clock on RTC_SLOW so it is the same crystal. A
   `-debug` variant reuses the release sdkconfig with the usual fragments.
5. `tests/host/board_profile_test.cpp` gains the T-Beam block and the tree gains
   a `board-profile-tbeam` CTest target compiled with the board macro, which
   pins every column and proves the GPS pins come from the row rather than from
   M5Unified's Port A table.
6. The simulator. `sim/hardware_model.cpp` gains a `tbeam-supreme` profile row,
   `sim/build.sh` can build that board, and `create_scenario`'s board list
   gains it. `src/FurbleDisplay.cpp` builds the 128x64 model from a `Panel_sdl`
   of that size and thresholds every pushed block to the two colours a 1-bit
   panel can show, so a scenario renders at the real geometry and bit depth.
   Two scenarios cover the same row with each receiver.

## Deviations from plan 322's step list

- **No SX1262 sleep.** Plan 322's step 5 runs `SX1262::sleep()` and then cuts
  ALDO3. This PR leaves ALDO3 off from boot instead: furble never drives the
  radio's SPI or control pins, so an unpowered part has nothing to drive, and
  the factory's sleep sequence stays bench item 5 in plan 318. Cutting the rail
  of a part nobody talks to is the lower-current and lower-risk default, and it
  is one line to reverse.
- **No M.2 or external-header rails.** DCDC3/4/5 and BLDO2 stay off. The socket
  is not used, and `XPOWERS_AXP2101_DCDC4_VOL2_MAX` has no millivolt value this
  driver can state, so enabling it would be a guess.
- **SD stays off.** `FurbleSD` mounts only through
  `M5.Display.getPanel()->getBus()`, which is the display bus on every ported
  board. The T-Beam's card is on its own SPI2 bus with CS 47, so the row reports
  no slot and the image compiles the SD stack out. A bus seam in `FurbleSD` is
  the follow-up; until then a scenario cannot mount a card and the UI cannot
  offer GPX logging or settings backup.
- **No three-key input tier.** The board has BOOT and the PMIC key. The UI's
  third input reports a permanent release, so the focus moves one way and wraps,
  and the select key works. A ring or a long-press scheme for a two-control
  128x64 board is a UI design change, not a port detail.
- **The 128x64 UI tier is a first pass.** The panel is real, the geometry is
  real, and every page renders, but four 16 px rows are all that fit. The tier
  sets zero row padding and 24 px icons in this PR. The page-by-page layout
  work plan 322 called "the largest single item" is not done and is the largest
  open item below.
- **`rtcBatteryBacked` is false**, where plan 322's verification pass recorded
  true. The hardware answer is true; the code answer has to be false while the
  image does not read that RTC, because the column suppresses TimeKeeper
  checkpoints.

## Open items

- Every bench item in plan 318, in particular the BLE-connected light sleep
  floor this board exists to prove, the 38400 baud MAX-M10S at cold start, and
  the AXP2101 off-current outlier.
- The MAX-M10S V_IO supply and the V_BCKP net (plan 318 item 6, plan 366
  section 12). When V_BCKP is confirmed, define `FURBLE_GPS_BACKUP_SUPPLY` for
  the image. Until then the image takes the conservative side that costs one
  UPD-SOS save before a rail cut.
- Which receiver and antenna the row should describe, once plan 366's levers are
  measured on a device. The row deliberately carries no trim, no antenna and no
  backup assumption: the UART pins and `gpsRail` are the only GPS facts it
  states, and adding a `FURBLE_GPS_BACKUP_SUPPLY` or an enable pin is a row
  change, not a rewrite.
- Which RTC part is fitted, then wire it on bus 1 and flip `rtcOnBus` and
  `rtcBatteryBacked`.
- An `FurbleSD` bus seam for a card on its own SPI host, and the card-detect
  pin if the board has one.
- The 128x64 page layouts: which pages keep icons, how the header and battery
  fit, and whether the OLED's contrast floor is usable at 32.
- `SX1262::sleep()` plus the ALDO3 cut, if the bench prefers it to an unpowered
  radio.
- The 128x64 panel's measured column offset, and both OLED addresses on real
  band variants.

## Review feedback

`plans/551-review-feedback.md` records an independent adversarial review at head
`8691165a`. Every hardware fact in the port matched the vendors, and three
blockers were in the parts no test exercised. Each is answered here.

1. **The probe could not fail.** The address probe treated
   `beginTransaction().has_value()` as the acknowledgement, but M5GFX raises the
   NACK inside `i2c_wait()` and reports it through `endTransaction()`, whose
   result was discarded. 0x3C was therefore always taken, 0x3D was never tried,
   and the init sequence could land in the magnetometer that also answers 0x3C.
   The probe now reads the stop condition's result, always probes both
   addresses, and the address decision is `Display::Detail::choosePanelAddress()`
   in the header. `display-probe` drives it with a bus double whose NACK is a
   real `false`: a panel at 0x3D with 0x3C NACKing selects 0x3D, a NACK on both
   selects nothing, and both addresses are probed before the decision.
2. **The panel was 128x128 while the model was 128x64.** `Panel_SH110x` defaults
   to 128 rows and programs multiplex 0x7F, which a 64-common module decodes as
   pages 0 to 15 collapsed onto pages 0 to 7. The image now derives a 128x64
   class, sets panel and memory height to 64 and sends multiplex 0x3F, and the
   value is `Display::Detail::sh1106MuxRatio(TBEAM_PANEL_HEIGHT)`, derived from
   the same constant the simulator builds its window with. `display-probe`
   asserts both. The SH1106's 2-column RAM offset is `_cfg.offset_x`, applied by
   `Panel_SH110x::display()`; the earlier claim that `setWindow()` handled it was
   wrong, M5GFX 0.2.27 has no such method, so the offset is 0 and a bench check.
3. **One identity read cost the whole power path.** A failed AXP2101 identity
   read disabled the rails, the gauge and power off while the row still
   advertised `canPowerOff`. The probe now makes three attempts, the same
   treatment the M5PM1 path gives its first transaction, and the 0x4A value is
   recorded in `lib/hal/AXP2101.cpp` and
   [axp2101](../docs/components/axp2101.md) as inferred rather than documented:
   it comes from the X-Powers reference library, M5Unified compares against the
   same value, and the datasheet revisions the driver cites do not define
   register 0x03 at all.

The non-blocking items are also closed: the ALDO1 comment now describes the
cold-boot cycle that does cut it, the boot splash gets a mid contrast at the end
of `Display::begin()` because the init list leaves the panel at 0x00 and the UI
applies the stored brightness only once it exists, the `-debug` environment is
in the reproducible matrix instead of being a pinned budget nothing built, and
`REQUIRED_SELECTIONS` lists the board so the two scenarios cannot be dropped.

## Verification

Recorded on this branch, macOS host, no device attached:

- `FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e lilygo-tbeam-supreme`:
  SUCCESS. `-e lilygo-tbeam-supreme-debug` also builds, with the console and
  the debug fragments.
- `python3 tools/check_size_budget.py --env lilygo-tbeam-supreme`: PASS, flash
  1,819,664 of 3,145,728 bytes, DRAM 47,308 of 48,128, IRAM 110,848 of 111,616.
  The `-debug` env records flash 2,215,360, DRAM 52,404 and IRAM 129,792
  against the same slot, rebuilt on this head rather than carried over from the
  pre-rebase tree.
- `python3 tools/stack_budget.py --env lilygo-tbeam-supreme` and the `-debug`
  env: PASS, every task under its stack, and no new entry was needed in
  `tools/stack_budget.json`.
- `python3 tools/derive_sim_heap.py --env lilygo-tbeam-supreme` records the
  boot heap in `sim/coverage/board_heap.json`: 47,312 static DRAM, 284,208 bytes
  at app start, 86,944 attributable and 197,264 derived free.
- Host suite: `ctest` 228 of 228 pass, including `board-profile`,
  `board-profile-tbeam` and the new `display-probe`.
- The 1-bit model is measured, not assumed: a splash capture from the T-Beam
  build is 128x64 and contains exactly two colours, black and white. Before the
  direct draws were thresholded as well as `push()`, the same capture held six
  colours because the splash draws with the panel primitives.
- The two simulator scenarios named above pass on a build made with
  `FURBLE_SIM_FURBLE_BOARD=FURBLE_LILYGO_TBEAM_SUPREME`, run through
  `sim/scripts/run-e2e.sh` with `FURBLE_SIM_BOARD_ID=lilygo-tbeam-supreme`.
- The same suite on the M5StickS3 binary: 274 of 274 scenarios pass on the
  rebased tree, so the display seam changed no M5 board's behaviour.
- `python3 tools/pre_review.py --base origin/master`: 13 of 13 gates.
  `sh sim/scripts/check-doc-tokens.sh` and
  `python3 tools/check_doc_citations.py` are clean.

Nothing here is a hardware result. The image has never run on a T-Beam.
