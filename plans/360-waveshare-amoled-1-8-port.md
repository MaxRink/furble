# 360 - Port plan for the slgray AMOLED-1.8 board

Status: research and port plan. It adds no board, environment, driver or support
claim. No unit was on the bench and no current was measured. Sources were read
on 2026-11-26 against fork master `bbe4d22c`, and the repo citations were
re-read at `19588003`, the rebase base, where #500 and #505 had added three
profile columns. The two commits that carry the slgray port, `1d360de` and
`01cf429`, were read at those commits. The plan is numbered 360 because 327 to
359, 473 to 475 and 511 are taken on master or by open branches; that was
checked against the plan files on the head of every open pull request on
2026-11-26 and again at the rebase base.

Tags on every external claim. [vendor: W1] a vendor page statement.
[schematic: W3] a net read from the vendor schematic PDF. [vendor code: W2] the
vendor repository at commit `01fd320`. [library: G1] read in the pinned M5GFX
0.2.27 sources. [firmware: S1] read in the slgray fork at `1d360de`. [repo] read
in this repository at `bbe4d22c`. [inferred] my conclusion, stated by no source.
A fact that no source settles carries **UNVERIFIED** with the artifact that
would close it.

Related: [318](318-next-board-battery-gps.md) (the 32.768 kHz finding and the
GNSS rules this board does not change), [322](implemented/322-hardware-abstraction.md) (the
`lib/hal` seams, the `Platform` contract and the 19 column profile row),
[287](287-mcu-portability-and-bundle-hardware.md) (the porting survey),
[320](320-rtc-use.md) (RTC domain use). Plan 336 (PR 510) moves the GPS UART
pins into the profile row and plan 346 (PR 493) adds `include/FurbleRail.h`;
both are in flight, so this plan names the column set on the branch it needs.

## Why this board

The board is a complete ESP32-S3 remote in a 1.8 inch case: AMOLED touch panel,
AXP2101 PMIC, RTC, IMU, TF slot, speaker and microphone, 16 MB flash and 8 MB
PSRAM [vendor: W1]. furble has a driver for none of those parts, but it has a
working port to point at: the slgray fork runs furble on this board with camera
pairing, shutter and focus tested on a Fujifilm X-E4 [firmware: S1].

Two things make it a different target from the T-Beam of plan 318. It is a
finished product rather than a bench board, and it is the first board furble
would drive with a 368 by 448 touch panel. It does not fix the battery floor:
it has no 32.768
kHz crystal on `XTAL_32K`, and the only 32.768 kHz crystal on the board belongs
to the PCF85063 [schematic: W3, `32.768KHz (2.0*1.2)` next to `PCF85063ATL` and
nowhere else], so the BLE connected floor stays the main XTAL figure of plan
318 finding 1.

## What the fork already has

Commit `1d360de` (2026-08-15) is the port, commit `01cf429` (2026-08-17) is its
hardware reference [firmware: S1].

| Commit | Files | What it does |
| :--- | :--- | :--- |
| `1d360de` | `src/waveshare_board.cpp` (406 lines) and `.h`, `src/m5_shim.h`, `src/pin_config.h`, `platformio.ini`, `sdkconfig.waveshare-esp32s3-amoled18`, `src/FurblePlatform.cpp`, `src/FurbleUI.cpp`, `src/main.cpp`, `lib/furble/Scan.h` | A bare CO5300 QSPI driver on `spi_master`, a SensorLib CST816/FT3168 auto-detect touch driver, XPowersLib AXP2101 power, and an `m5::m5_t M5` shim so the UI keeps calling `M5.Display`, `M5.Touch`, `M5.Power` and `M5.BtnA..PWR`. |
| `01cf429` | `WAVESHARE-ESP32-S3-TOUCH-AMOLED-1_8.md` (193 lines) | The hardware reference: parts, I2C scan, pin map, CO5300 init table and QSPI protocol, the CST820 Y axis correction, AXP2101 behaviour, and a ten item trap list. |

The fork builds with ESP-IDF 5.4 (`framework-espidf@3.50402.0`) and LVGL 9.4 as
a managed component, moves the UI to a 32 KB task, and flushes LVGL with
`lv_draw_sw_rgb565_swap` before a full screen PSRAM frame buffer
[firmware: S1]. It is a second display stack beside M5GFX, not a board profile.
That is why this plan still has work: the fork answers "can furble run here",
not "can furble own this board".

## Parts and the document for each

| Part | Function | Document, revision or commit | Status |
| :--- | :--- | :--- | :--- |
| ESP32-S3R8 | SoC, 240 MHz, WiFi and BLE 5, 8 MB PSRAM in the package | ESP32-S3 series datasheet v2.2 (E1); [vendor: W1] "stacked 8MB PSRAM" | documented |
| 40 MHz crystal | SoC main XTAL | [schematic: W3, `40MHz(2.5*2)`] | documented |
| 32.768 kHz crystal | PCF85063 only, never `XTAL_32K` | [schematic: W3] | documented, and the reason the sleep floor stands |
| W25Q128JVSIQ | 16 MB flash | [schematic: W3]; [vendor: W1] "external 16MB Flash" | documented |
| CO5300 | AMOLED controller, 368x448 over QSPI, 480x480 GRAM (V2) | No datasheet in the tree. Pin and init facts from [firmware: S1 section 3.1] and [vendor code: W2 `examples/arduino-v2/libraries/GFX_Library_for_Arduino/src/display/Arduino_CO5300.h`] | **UNVERIFIED** against a datasheet |
| SH8601 | The same panel on V1 units | [vendor: W1] version note; [vendor code: W2 `examples/arduino-v2/libraries/GFX_Library_for_Arduino/src/display/Arduino_SH8601.h`] | documented as a separate variant |
| CST820 | Capacitive touch at 0x15 (V2) | No datasheet in the tree. Registers from [firmware: S1 section 3.2] | **UNVERIFIED** against a datasheet |
| FT3168 | Touch at 0x38 on V1 units | No datasheet in the tree | **UNVERIFIED** |
| AXP2101 | PMIC and charger at 0x34 | X-Powers AXP2101 datasheet revision 1.0 (X1), the copy plan 322 cites and [vendor code: W2 `examples/arduino/libraries/XPowersLib/datasheet/AXP2101_Datasheet_V1.0_en.pdf`] | documented |
| TCA9554PWR | 8 bit I2C IO expander at 0x20 | TI TCA9554 datasheet not opened. Part read from [schematic: W3]; address and use from [vendor code: W2 `examples/arduino-v2/examples/14_LVGL_SD_Test/14_LVGL_SD_Test.ino:140`] and [firmware: S1] | part documented, register map **UNVERIFIED** |
| PCF85063ATL | RTC at 0x51 with its own 32.768 kHz crystal | NXP PCF85063A product data sheet not opened. Part read from [schematic: W3] and [vendor: W1]. A copy ships at [vendor code: W2 `examples/arduino-v2/libraries/SensorLib/datasheet/PCF85063A.pdf`] | part documented, map **UNVERIFIED** |
| QMI8658C | 6 axis IMU at 0x6B | QMI8658A datasheet Rev A ships at [vendor code: W2 `examples/arduino-v2/libraries/SensorLib/datasheet/QMI8658A Datasheet Rev A.pdf`], not opened | part documented, map **UNVERIFIED** |
| ES8311 | Audio codec on I2S | No datasheet in the tree. Part read from [vendor: W1] and [schematic: W3] | **UNVERIFIED** |
| NS4150B | Speaker amplifier, enable on GPIO 46 | [schematic: W3, `U10 NS4150B`, `PA_CTRL` on GPIO46] | documented |
| TF slot | microSD, 1 bit SDMMC | [vendor code: W2 `examples/arduino-v2/examples/14_LVGL_SD_Test/14_LVGL_SD_Test.ino:147-148, 189-196`] | documented |

The wiki contradicts itself twice and both stay on the record. Its page text
says "the ESP32-S3-Touch-LCD-1.8 V1 version is discontinued" while the page is
the AMOLED board, and its Screen Description section still describes the FT3168
that only V1 has, one screen after the version note that gives V2 a CST820
[vendor: W1]. The part is settled by the back label, not by the page [vendor:
W1], so the first bench item is reading that label.

## Pin map

Pins come from the vendor `pin_config.h` at [vendor code: W2
`examples/arduino-v2/libraries/Mylibrary/pin_config.h`], which is byte
identical to the fork's copy at [firmware: S1]. Nets come from [schematic: W3].
The ESP32-S3 has GPIO 0 to 21 and 26 to 48; 22 to 25 do not exist (E1).

| Pin | Board use | Furble today | Verdict |
| :--- | :--- | :--- | :--- |
| 0 | BOOT button, active low, pulled up [vendor code: W2] | `M5.BtnA` source on every board | free, strapping pin |
| 1 | TF CMD, net labelled `MOSI` [schematic: W3] | `FurbleSD` wants `M5.getPin(sd_spi_cs)` on the display bus | collision of assumption, see C2 below |
| 2 | TF CLK, net `SCK` [schematic: W3] | same | same |
| 3 | TF D0 or D3, net `MISO` or `CD/D3` [schematic: W3] | same | same |
| 4 | LCD QSPI SIO0 | M5GFX autodetect M5Dial probe drives GPIO 4 | **collision**, see C1 below |
| 5 | LCD QSPI SIO1 | same probe | **collision** |
| 6 | LCD QSPI SIO2 | same probe | **collision** |
| 7 | LCD QSPI SIO3 | same probe, used as the panel ID read pin | **collision** |
| 8 | I2S data into the codec, net `I2S_DSDIN` [schematic: W3] | M5GFX probe drives GPIO 8 low as a reset; StickC family LED is 10 | **collision** |
| 9 | I2S bit clock | StickC and StickC Plus IR pin | free by row, see C4 |
| 10 | I2S data from the codec, net `I2S_ASDOUT` | StickC and StickC Plus LED pin | free by row |
| 11 | LCD QSPI clock, net `QSPI_SCL` | none | free |
| 12 | LCD chip select, net `LCD_CS` | none | free |
| 13 | `LCD_RESET` net exists; the drawing pairs it with a pin the text export does not show | none | pin number **UNVERIFIED** |
| 14 | I2C SCL, net `ESP32_SCL`, pull-ups 2.2k [schematic: W3] | `M5.In_I2C` on the M5 boards | free |
| 15 | I2C SDA, net `ESP32_SDA` | same | free |
| 16 | I2S master clock | none | free |
| 17 | Free pad, on the exposed header [schematic: W3] | the fork's GPS TX [firmware: S1] | usable for a GPS UART |
| 18 | Free pad, on the exposed header [schematic: W3] | the fork's GPS RX | usable for a GPS UART |
| 19 | USB D minus | USB Serial/JTAG console on the StickS3; Plus2 IR and LED pin | free by row; the console must stay USB |
| 20 | USB D plus | same | free by row |
| 21 | `TP_INT` touch interrupt [vendor code: W2] | none | free |
| 26 to 32 | Flash, and the octal PSRAM die | none | not available |
| 33 to 37 | Octal PSRAM strap of the S3 group, not routed on the board [inferred from E1 and W3] | none | not available |
| 38 to 42 | Test points and pads, functions **UNVERIFIED** [schematic: W3] | none | treat as reserved until the drawing is read |
| 43 | U0TXD, on the exposed header [schematic: W3] | console is USB Serial/JTAG on the S3 rows | free, but it is the ROM log pin |
| 44 | U0RXD, on the exposed header | same | free |
| 45 | I2S word clock, strapping pin | none | free, strapping |
| 46 | Speaker amplifier enable, net `PA_CTRL`, strapping pin | StickS3 IR pin in `src/FurbleBoard.cpp:75-77` | **collision** if a row is copied |

### Collisions with a peripheral furble already drives

| Id | Collision | Evidence | Consequence |
| :--- | :--- | :--- | :--- |
| C1 | M5GFX autodetect runs before any furble code chooses a board, and on the S3 it drives the M5Dial probe pins 4, 5, 6, 7 and 8, including `_pin_reset(GPIO_NUM_8, use_reset)` | [library: G1 `M5GFX.cpp`, the `board_M5Dial` probe] and `src/FurblePlatform.cpp:108` | The probe toggles this board's four QSPI data lines and the I2S input. Same failure class as the W5500 pins 9 to 14 in plan 42, and the reason that image pins `M5GFX_BOARD`. This board must not let autodetect run. |
| C2 | `FurbleSD` mounts only through `M5.getPin(sd_spi_cs)` on the panel's `lgfx::bus_spi` | `src/FurbleSD.cpp:755` and `:986` | Returns a negative CS and refuses. The board's slot is a 1 bit SDMMC host with the card enable on expander output 7, so sharing the QSPI bus is wrong even if a CS existed. |
| C3 | `M5.getPin(port_a_pin1)` and `port_a_pin2` give the GPS UART pins | `src/FurbleGPS.cpp:167-169` | -1 on a board M5Unified does not know, so no GPS. Plan 336 (PR 510) adds `gpsPinSource`, `gpsRxPin` and `gpsTxPin` to the row for exactly this. |
| C4 | The per-row pins are inferred from the board macro in practice, and the StickC, Plus, Plus2 and StickS3 rows already use 9, 10, 19 and 46 for IR and LED, and 35 and 13 for motion wake | `src/FurbleBoard.cpp:66-105` | Any new row must set `irPin` and `ledPin` explicitly. A copied row drives the speaker amplifier or the codec clock. |
| C5 | M5Unified's power class switches Port A on known boards only | `src/FurblePlatform.cpp:108`, [repo] `include/FurblePowerBackend.h:46-48` | `Rail::GPS` has no meaning here, and the `gpsRail` column would be false because `M5.Power` does nothing, not because the board has no switchable rail. |

## Rails

The AXP2101 output list and the voltage labels come from the schematic's
parameter table [schematic: W3]. `lib/hal/AXP2101` already names all fourteen
outputs, so the driver fits the part without a change [repo
`lib/hal/AXP2101.h:32-48`].

| Output | Label on the schematic | Consumer | Boot state | Failure direction |
| :--- | :--- | :--- | :--- | :--- |
| DCDC1 | VCC3V3 | SoC, flash, expander, IMU, RTC VDD, I2C pull-ups [schematic: W3, `VCC3V3` in every block] | on, must stay on | cut kills the node. It is the first entry in the protected mask. |
| DCDC2 | 0.9 V | none identified | **UNVERIFIED** | unknown |
| DCDC3 | 1.2 V | none identified | **UNVERIFIED** | unknown |
| DCDC4 | 1.8 V | the 1.8 V group, which includes the panel IO candidate `VDDIO`/`VCI` | **UNVERIFIED** | cutting it blanks the panel or the touch segment, fail dark |
| DCDC5 | VBAT boost, `Vsys/DCDC5` | system | on | cut kills the node |
| ALDO1 | VL1_3.3V | **UNVERIFIED**; `VCC-RTC` appears in the same group | **UNVERIFIED** | if it feeds `VCC-RTC`, a cut stops the RTC and latches the VL flag, which furble must show as no time rather than a wrong time |
| ALDO2 | VL2_3.3V | **UNVERIFIED** | **UNVERIFIED** | unknown |
| ALDO3 | VL3_3V | **UNVERIFIED** | **UNVERIFIED** | unknown |
| ALDO4 | VL3_1.8V | the 1.8 V group | **UNVERIFIED** | fail dark |
| BLDO1 | VL_2.8V | **UNVERIFIED** | **UNVERIFIED** | unknown |
| BLDO2 | VL_2.8V | **UNVERIFIED** | **UNVERIFIED** | unknown |
| CPUSLDO | VCL_1.2V | **UNVERIFIED** | **UNVERIFIED** | unknown |
| RTCLDO | VCC-RTC | the PCF85063 | on while VBAT is present | It has no enable bit in the register map [repo `lib/hal/AXP2101.h`], so the clock is not switchable. Power off keeps it up. |
| VBACKUP | charger for a backup cell | the reserved RTC battery pads [vendor: W1] | off | Charging an absent cell is the one setting that can damage a fitted non-rechargeable cell. Keep it off until the pad is populated. |

What the rails table cannot yet say, recorded rather than guessed. The
text export of the schematic gives net names, not a drawing, so it pairs a
handful of nets and no more. Every "**UNVERIFIED**" row above closes with one of
three artifacts: the vector drawing, a continuity check on the unit, or the
vendor ESP-IDF BSP if it configures the rails.

The board also has a second power gate the PMIC does not own: `DSI_PWR_EN`
[schematic: W3]. Which expander output drives it is **UNVERIFIED**, and so is
the order it must come up in relative to the QSPI init. That ordering is the
one rail fact that can stop a port, so it is bench item 7.

## Display and touch path

### Can M5GFX drive the panel

M5GFX 0.2.27 has the machinery and not the class [library: G1].

- `Panel_CO5300` exists as `struct Panel_CO5300 : public Panel_AMOLED`
  (`src/lgfx/v1/panel/Panel_CO5300.hpp:37`), compiled only when `LGFX_USE_QSPI`
  is defined (line 27), which `src/lgfx/v1/platforms/esp32/common.hpp:108-110`
  sets for every target except the P4 on ESP-IDF 4.4 and later.
- Its constructor hardcodes 502 by 410 (line 43) and its init list hardcodes
  the T-Watch Ultra window, CASET `0x0016` to `0x01AF` and RASET `0` to
  `0x01F5` (lines 59-60). That is a different panel.
- `Panel_AMOLED::setWindow` adds `_colstart` to every column, and `_colstart`
  is `_cfg.offset_x` from `setRotation` (`Panel_AMOLED.cpp:207`), so the
  16 column offset the panel needs is a configuration value, not code.
- `Bus_SPI` switches to quad when all four of `pin_io0` to `pin_io3` are set
  (`src/lgfx/v1/platforms/esp32/Bus_SPI.cpp:189-211`).
- The library already subclasses `Panel_CO5300` for a second geometry:
  `Panel_StopWatch` in `M5GFX.cpp:820` sets 480 by 480 and its own init
  list. Subclassing for a third panel follows the library's own pattern.

Verdict: a board-local `Panel_CO5300` subclass of about 30 lines, not a new
driver. It sets `panel_width` 368, `panel_height` 448, `memory_width` 480,
`memory_height` 480, `offset_x` 16, and provides `getInitCommands()` with the
vendor table: `0xFE 00`, `0xC4 80`, `0x3A 55`, `0x35 00`, `0x53 20`, `0x51 FF`,
`0x63 FF`, CASET `0` to `367`, RASET `0` to `447`, `0x11` plus 100 ms, `0x29`,
`0x51 FF` [firmware: S1 section 3.1; vendor code: W2 the same table in the
Arduino `Arduino_CO5300` example path `01_HelloWorld.ino`, which passes a
column offset of 16 in the sixth constructor argument].

Three facts the port must respect and one it must measure.

1. Brightness is panel command `0x51` over 0 to 255, and 0 does not switch the
   display off [firmware: S1 section 3.1; library: G1
   `Panel_AMOLED::setBrightness` writes 0x51]. There is no backlight, so the
   `ESP_PM_APB_FREQ_MAX` rule has no LEDC reason on this board. Whether the
   QSPI clock needs it under DFS is **UNVERIFIED**; the SPI driver recomputes
   its divider, which is why RMT is safe and PWM is not. Keep the lock until
   bench item 12 measures it, then drop it in the same change that changes the
   panel class.
2. Sleep. M5GFX writes `0x10` with no delay on sleep in and `0x11` plus 150 ms
   on sleep out [library: G1 `Panel_AMOLED::setSleep`]. furble already owns a
   120 ms dwell in both directions (`include/FurbleUI.h:1136`,
   `src/FurbleUI.cpp:1895-1896`), and Arduino's CO5300 header states 120 ms for
   both transitions [vendor code: W2
   `examples/arduino-v2/libraries/GFX_Library_for_Arduino/src/display/Arduino_CO5300.h`,
   `CO5300_SLPIN_DELAY` and `CO5300_SLPOUT_DELAY`]. The existing single rule
   covers this panel with no new constant.
3. The odd window rule. `Panel_AMOLED` drops any operation whose left edge or
   width is odd: `setWindow` at `Panel_AMOLED.cpp:409`, `drawPixelPreclipped`
   at `:464`, `writeFillRectPreclipped` at `:483` and `writeImage` at `:505`.
   LVGL flushes rectangles it chooses. A 368 pixel wide panel makes full width
   stripes even, but nothing stops LVGL from invalidating a 37 pixel wide
   patch, and the drop is silent. furble must either restrict its flush areas
   to even edges and even widths or pad them, and **UNVERIFIED** until bench
   item 3 runs is whether LVGL on this panel ever emits an odd area at all.
4. The QSPI pixel opcode address differs between the two references. The fork
   sends opcode `0x32` with address `0x003C00` [firmware: S1]; M5GFX sends
   `0x002C0032`, so address `0x002C00` [library: G1
   `Panel_AMOLED::start_qspi`]. Both are read from working code paths, one on
   this board and one on other AMOLED boards, so whichever path the port takes
   uses its own value. Do not copy one into the other. **UNVERIFIED** which one
   the CO5300 prefers; the datasheet is not in the tree.

There is no backlight and therefore no `minBrightness` floor in the LEDC sense.
The column still has to be filled, and the honest fill is 1 with the reason
"the register accepts 1, the panel is readable, and 0 leaves the panel lit at
its dimmest", then a bench measurement of what the eye and the meter say.

### V1 units need a second class

M5GFX 0.2.27 has no SH8601 panel class: the panel directory holds AMOLED,
CO5300, ED2208, EPDiy, GC9A01, GDEW0154D67, GDEW0154M09, ILI9342, IT8951, LCD,
M5HDMI, M5UnitGLASS, M5UnitLCD, SSD1306, SSD1677, ST7735, ST7789 and TM1680 and
nothing else [library: G1]. A V1 unit therefore needs a new panel class built on
`Panel_LCD` with the vendor `Arduino_SH8601` init table, and its touch is an
FT3168 at 0x38 rather than a CST816 at 0x15 [vendor: W1]. Ship V2 only and say
so, or carry two classes. The plan recommends V2 only, because the vendor dates
the V2 switch to 2026-05-30 [vendor: W1] and V1 is discontinued.

### Touch

M5GFX has a `Touch_CST816S` driver in `src/lgfx/v1/touch/Touch_CSTxxx.hpp:35`,
and three of its behaviours matter here [library: G1].

- It does not validate the chip. `_check_init` reads three bytes from register
  0xA7 and keeps whatever answers (`Touch_CSTxxx.cpp:53-60`), so a CST820 with
  ID 0xB7 initialises.
- It reads X and Y from registers 0x03 to 0x06 as 12 bit halves
  (`Touch_CSTxxx.cpp:150-171`), which is the register pair the fork's reference
  lists [firmware: S1 section 3.2].
- It applies no scaling and never disables auto sleep. The fork measures the
  CST820 as reporting raw Y near (screen Y + 15) times 8/7, corrects it as
  `(rawY + 15) * 7 / 8`, applies the correction only when the chip ID is 0xB7,
  and calls `disableAutoSleep()` to keep polling responsive [firmware: S1
  section 3.2]. Both are furble's work, in a small `ITouch` subclass or in the
  input backend, and that path needs the chip ID read that M5GFX throws away.

The touch segment is its own net pair, `TP_SCL` and `TP_SDA`, not
`ESP32_SCL` and `ESP32_SDA` [schematic: W3]. Either a level translator or an
expander gate separates them, and which one is **UNVERIFIED**. It matters
because a gated segment means the touch disappears when the panel power is cut
and must be brought back in the right order.

furble reads touch through `M5.Touch` at more than thirty sites. Plan 322
recorded the input backend as a description until the first non-M5 board. This
is that board. The fork's answer was a shim header that keeps `M5.Touch` and
puts one board class behind it [firmware: S1 `src/m5_shim.h`], which is the
cheapest landing and the wrong long term shape, because it re-implements the
M5Unified API surface rather than the furble seam.

## Battery and charging

The battery is a user cell on an MX1.25 header [vendor: W1], so capacity is
unknown at build time and the profile column is 0 for that reason rather than
for a missing gauge.

| Measurement | Source | Furble today |
| :--- | :--- | :--- |
| Level, percent | AXP2101 gauge [vendor: W1]; the fork reads `getBatteryPercent()` and refuses it when `isBatteryConnect()` is false [firmware: S1] | `battery_caps_t.level` true, and `lib/hal/AXP2101` refuses percent without a battery or gauge enable |
| Voltage | AXP2101 ADC | `battery_caps_t.voltage` true |
| Current | No current sense in the driver and none claimed by any source | `battery_caps_t.current` false, and `meetsPowerBackend` has no current member to fill |
| Charging | AXP2101 STATUS bits; the fork reports `isCharging()` without gating on battery detection "which can read false even when charging" [firmware: S1] | `battery_caps_t.charging` true |

Failure direction for the two odd cases. With no cell fitted, `readBattery()`
must report level 0 and charging false and the UI must show no battery rather
than an empty battery, which is the same rule the `UNKNOWN` row already
follows (`src/FurbleBoard.cpp:103`). With a cell at full charge, the AXP2101
stops charging and `isCharging()` goes false while VBUS is present, which the
fork documents as normal [firmware: S1]; furble's auto-off policy must not read
that as "on battery" and start a shutdown countdown. That is the plan 13
charging opt-in rule and it applies unchanged.

## Which of RTC, IMU, SD, IR and speaker exist

| Function | On the board | Furble path today | Verdict |
| :--- | :--- | :--- | :--- |
| RTC | PCF85063ATL at 0x51, own crystal, `VCC-RTC` rail, `RTC_INT` net, backup pads [vendor: W1, schematic: W3] | `M5.Rtc` is M5Unified's `RTC_Class`, which is the PCF8563 register map (M5Unified 0.2.20 `M5Unified.hpp:229`, with `RTC8563_Class` kept as an alias at `:80`), and `Platform` drives PCF8563 registers directly (`src/FurblePlatform.cpp:40-70`); `TimeKeeper` gates on `Board::profile().rtcBatteryBacked` (`src/FurbleTimeKeeper.cpp:319`) | A new driver, or none. The time registers of the two NXP parts are laid out the same way, so a read may work through the existing path, but the alarm registers are not, and **UNVERIFIED** either way because the PCF85063A data sheet was not opened. No alarm means no timed wake. |
| IMU | QMI8658C at 0x6B with `QMI_INT1` and `QMI_INT2` nets [schematic: W3] | `M5.Imu` plus a BMI270 and an MPU6886 backend (`src/FurbleIMU.cpp:58`, `:196-201`) | No driver. Spirit level, motion wake and shake gesture are absent until one lands. The INT nets do reach the SoC but their pins are **UNVERIFIED**. |
| SD | TF slot, 1 bit SDMMC on 2/1/3, card enable on expander output 7 [vendor code: W2 `examples/arduino-v2/examples/14_LVGL_SD_Test/14_LVGL_SD_Test.ino`] | `FurbleSD` shares the panel's SPI bus (`src/FurbleSD.cpp:755`, `:986`) | Exists on the board and cannot be driven. It needs an SDMMC host path and an expander write before init. `FURBLE_HAS_SD` stays 0 until then. |
| IR | No emitter and no receiver. The `PA_CTRL` net on GPIO 46 is the speaker amplifier | Row `irPin` (`src/FurbleIR.cpp:160`) | `irPin` -1 |
| Speaker | ES8311 codec with the NS4150B amplifier and an onboard speaker [vendor: W1, schematic: W3] | `M5.Speaker` (`src/FurbleFeedback.cpp:305-325`) | Exists and cannot be driven. M5Unified's speaker is not built for this codec, so `feedbackSound` is false until an I2S path exists. |
| Microphone | Analog microphone into the ES8311 [vendor: W1] | furble sets `cfg.internal_mic` false (`src/FurblePlatform.cpp`) | Unused, and plan 91 stays deferred |

The SD case is the sharpest one. The board has the slot and the firmware has
the feature, and the two cannot meet without a new host path, because
`FurbleSD` assumes an SPI slot sharing the display bus. Recording that as
`sdSlot` false is the current column's only honest answer, and plan 322 already
named that gap.

## Profile row proposal

The row is written against `include/FurbleBoard.h:46-77` as it stands on
`19588003`, twenty-two columns. Three of them arrived after the survey this plan
started from: `powerOffPath` with #500, and `ledActiveLow` and `irMarkLow` with
#505. Plan 336 (PR 510) adds `gpsPinSource`, `gpsRxPin` and `gpsTxPin`, and the
three values that belong there are in the note under the row.

```
constexpr Profile AMOLED = {"AMOLED", {true, true, false, true}, 0, true, -1,
    MotionWake::NONE, -1, TimedWake::NONE, -1, false, false, true, true, true,
    1, false, false, false, -1, false, false, false};
```

Column by column, with the reason for every value.

| # | Column | Value | Reason |
| ---: | :--- | :--- | :--- |
| 1 | `name` | `"AMOLED"` | Six characters, the boot info block is narrow. |
| 2 | `battery` | `{true, true, false, true}` | Level, voltage and charging from the AXP2101; no current sense. |
| 3 | `batteryCapacityMah` | 0 | The cell is the user's [vendor: W1]. |
| 4 | `rtcBatteryBacked` | true | The RTC is fed from the PMIC and has backup pads [vendor: W1, schematic: W3]. The column describes the hardware, and `g_RtcAvailable` still gates its use, so nothing is claimed while there is no driver. |
| 5 | `irPin` | -1 | No emitter. |
| 6 | `motionWake` | `NONE` | The QMI8658 interrupt nets exist and nothing drives them. |
| 7 | `motionWakePin` | -1 | Follows 6. |
| 8 | `timedWake` | `NONE` | The AXP2101 has no wake timer, and the PCF85063 timer has no known SoC pin. |
| 9 | `powerHoldPin` | -1 | Nothing to hold. `PWRON` drives `CHIP_PU` from the PMIC [schematic: W3]. |
| 10 | `pmicClickStreak` | false | The AXP2101 power key is readable through its interrupt status. |
| 11 | `sdSlot` | false | The slot is not on the display SPI bus. Column cannot say "present, other bus". |
| 12 | `canPowerOff` | true | `HAL::AXP2101::powerOff()` exists. The row must land with the `Platform` that calls it, or the UI offers a power off that reboots. |
| 13 | `powerOffPath` | true | Same call, and the AXP2101 removes the rails rather than deep sleeping the SoC with no wake source. |
| 14 | `modemSleep` | true | The S3 supports it. The 32.768 kHz finding is separate and is not this column. |
| 15 | `minBrightness` | 1 | Brightness is panel register `0x51`, where 0 leaves the panel lit. A measurement should replace this value, not a guess. |
| 16 to 18 | `feedbackSound`, `feedbackLight`, `feedbackVibration` | false, false, false | No speaker path, no LED, no motor. |
| 19 | `ledPin` | -1 | No LED. |
| 20 | `gpsRail` | false | No Port A output, and no rail named GPS. |
| 21 | `ledActiveLow` | false | There is no LED stage to describe. False is not a polarity claim, unlike a board that has one. |
| 22 | `irMarkLow` | false | There is no emitter stage to describe, so the RMT output stays un-inverted. |

Columns that cannot be filled honestly, recorded rather than papered over:

- `sdSlot`. False means "no slot on the display SPI bus" in every existing row.
  This board has the slot on its own SDMMC host behind an expander output.
- `gpsRail`. False means "M5.Power does not switch a Port A output". This board
  has nothing to switch for a GPS, which is a different fact that happens to
  share the value.
- `minBrightness`. It is a backlight floor. An AMOLED has no backlight, and the
  register value is not proportional to the light output, so any number is a
  placeholder.
- `batteryCapacityMah`. Zero means unknown. Here it means user supplied, which
  is unknowable at build time rather than unknown at runtime.
- `rtcBatteryBacked`. True is a hardware fact, but the firmware cannot read the
  RTC yet, so a reader of the row cannot tell "backed and readable" from
  "backed and unreadable".
- Nothing expresses a switchable panel rail. `include/FurbleRail.h` on plan
  346's branch names holders of one 5 V rail (`GPS`, `IR`, `GROVE`), which is
  not this board's shape either: here the panel power is an expander output and
  the PMIC rail is unnamed.

The three columns plan 336 adds would carry `GpsPinSource::PROFILE`,
`gpsRxPin` 18 and `gpsTxPin` 17, because those two pads are free and unused
[schematic: W3] and they are where the fork put its UART [firmware: S1].

## Simulator work

The simulator builds one panel class per invocation from a table of three in
`docs/sim.md`, and none of them is a touch panel (the SDL panel gets a
mouse-driven touch device, and every modeled board is non-touch). This board is
the first large touch panel, so the work is larger than a size change.

1. A fourth panel class, 368 by 448, in `sim/build.sh` and the `docs/sim.md`
   table, selected by a new `FURBLE_PANEL_368X448` in
   `include/FurbleBoardCaps.h`. The capability scan rejects a `FURBLE_PANEL_*`
   name the header does not define, so the macro and the use land together.
2. A touch layout that is not the Core2's. The two touch-layout scenarios today
   are the 320 by 240 pair `bughunt/core-connected-grid.txt` and
   `-large.txt`; a 368 by 448 panel needs its own, and the safe area has to
   clear the rounded glass that no vendor document states [vendor: W1 gives no
   radius; **UNVERIFIED**].
3. A board in the profile matrix and a `board-profile` host row per column of
   the table above, in `tests/host/board_profile_test.cpp`.
4. `sim/coverage/hardware.json` entries for the PMIC, expander, RTC, IMU, SD
   and panel, with the modelled behaviours the boundaries suite links.
5. `sim/coverage/board_heap.json` entry from `tools/derive_sim_heap.py`. It
   needs a built firmware ELF, so it follows the environment, not the plan.
6. Power model entries in `tools/power-model/board-currents.yaml`: an AMOLED
   display term with no backlight, a PMIC term, and the peripheral set. A
   baseline in `tools/power-model/baseline/` only after the first report is
   reviewed, because the committed baselines are relative evidence.
7. Scenario ownership in `sim/scenarios/manifest.json`, then
   `python3 tools/check_sim_scenarios.py` and
   `sh sim/scripts/check-doc-tokens.sh`.
8. `sim/scripts/docs-capture.sh` gains the panel, and
   `docs/ui-walkthrough.md` is regenerated in the same change that changes a
   page for it.

## Bench checklist

Run each on a V2 unit unless stated. Record the board label, the firmware
commit and the meter.

1. Read the back label and record V1 or V2 [vendor: W1]. Pass: the version
   matches the part the build assumes. Everything below assumes V2.
2. Display bring-up with the new panel class. Pass: 368 by 448 fills the glass,
   white is white, the colour bars are neither swapped nor byte reversed, and
   `0x51` from 0 to 255 sweeps brightness.
3. Odd window. Force a flush at an odd x with an odd width. Pass: it lands. A
   blank rectangle proves the `Panel_AMOLED` drop rule and forces the
   even-strip flush change.
4. Touch identity. Read register 0xA7. Pass: the value is recorded, and if it
   is 0xB7 the `(rawY + 15) * 7 / 8` correction is in and the four corner raw
   values are recorded for calibration.
5. Touch polling for 60 s with the finger away. Pass: the first touch after the
   wait is reported without a retry, which settles whether auto sleep has to be
   disabled.
6. Touch segment. Confirm what separates `TP_SCL` and `TP_SDA` from
   `ESP32_SCL` and `ESP32_SDA` [schematic: W3] and whether it is enabled by an
   expander output. Pass: the name of the gate and its boot state are recorded.
7. Panel power order. Identify the expander output behind `DSI_PWR_EN` and the
   delay the panel needs after it before the first command. Pass: a cold boot
   sequence that never shows a white or green frame.
8. Rails. Measure every AXP2101 output at boot before furble touches it, with
   the vendor firmware and with the port. Pass: the table above is filled with
   measured states, and any output switched off is explained.
9. RTC. Read 0x02 to 0x08, confirm BCD time advances, then cut `VCC-RTC` and
   confirm the VL flag latches. Pass: furble reports no time rather than a
   stale time after the cut.
10. SD. Mount a card through a 1 bit SDMMC host after driving expander output 7
    high [vendor code: W2]. Pass: the card mounts, and the IO voltage is
    recorded, because the fork's note says 1.8 V and no vendor document does.
11. IMU. Confirm the part answers at 0x6B and find which SoC pin `QMI_INT1`
    reaches. Pass: the pin is recorded and a motion interrupt is seen.
12. Power. Off current, idle with the screen off, connected with the screen
    off, and the same with `ESP_PM_APB_FREQ_MAX` released. Pass: the display
    lock decision is made on numbers, and the connected floor is compared to
    the 3.3 mA StickS3 figure of plan 318.
13. Strapping. Boot ten times with the speaker enabled, then with it muted.
    Pass: no download mode entry, no first stage reset. GPIO 45 and 46 are
    strapping pins and GPIO 46 drives the amplifier.
14. Flash. Build with `--flash_mode dio` and confirm the bootloader header. The
    fork records a `TG0WDT` boot loop when a merge step rewrote the header to
    QIO [firmware: S1 section 7].

## Open items

Facts a document settles:

- The CO5300 data sheet, including the QSPI pixel opcode address and the
  SLPIN to SLPOUT timing. Not in the tree, not public as far as this pass
  found.
- The CST820 data sheet, including whether it has an auto sleep mode and what
  register disables it.
- The TI TCA9554 data sheet, for the eight bit register map. The vendor ships
  the XL9555 data sheet instead, and `lib/hal/XL9555` is that 16 bit part.
- The NXP PCF85063A data sheet, for the timer and the absence of an alarm, and
  for whether `m5::RTC8563_Class` reads the time registers correctly.
- The ES8311 data sheet, for the I2S word length and the register sequence the
  codec needs before it passes audio.

Facts a device settles:

- Which pin drives `LCD_RESET`, `LCD_TE`, `TP_RESET` and `DSI_PWR_EN`, and which
  expander output each one is.
- What each AXP2101 output feeds, and its boot state.
- Whether the exposed header is the 7 pins the wiki counts, and which they are.
- The rounded glass radius, for the touch layout safe area.
- Whether `XTAL_32K` has a crystal. The drawing shows none, and the light sleep
  floor depends on it.
- The SD IO voltage, and whether the card enable through expander output 7 is a
  power switch or a chip select.
- Whether an unpinned M5GFX autodetect damages anything on this board, or only
  disturbs the panel. Item C1 above says it drives the pins; the consequence on
  a real unit is unmeasured.

## Verification

Read on 2026-11-26.

| Source | What was read | Revision or commit |
| :--- | :--- | :--- |
| S1 and S2 | The two fork commits, every changed file, the hardware reference in full | `1d360de`, `01cf429` |
| W1 | The vendor documentation page, version note, features, onboard resources, screen section | page as served on 2026-11-26, no revision string |
| W2 | The vendor repository tree and five files: `pin_config.h`, `01_HelloWorld.ino`, `14_LVGL_SD_Test.ino`, `Arduino_CO5300.h`, `Arduino_SH8601.h` (presence), the data sheet folder listing | `01fd320`, 2026-11-18 |
| W3 | The schematic PDF, read as a text export: net names, the parameter table, the crystal, the expander, the amplifier | the file served at the wiki resource path on 2026-11-26, no title block revision in the export |
| G1 | M5GFX 0.2.27: `Panel_CO5300.hpp`, `Panel_AMOLED.cpp`, `Panel_AMOLED.hpp`, `Touch_CSTxxx.cpp`, `Touch_CSTxxx.hpp`, `M5GFX.cpp`, `Bus_SPI.cpp`, `common.hpp`, the panel and touch directory listings | the library PlatformIO resolved for `m5stick-s3`, version 0.2.27 |
| `repo` | `src/FurbleBoard.cpp`, `include/FurbleBoard.h`, `include/FurbleBoardCaps.h`, `include/FurblePowerBackend.h`, `src/FurblePlatform.cpp`, `src/FurbleUI.cpp`, `include/FurbleUI.h`, `src/FurbleSD.cpp`, `src/FurbleGPS.cpp`, `src/FurbleIMU.cpp`, `src/FurbleIR.cpp`, `src/FurbleFeedback.cpp`, `src/FurbleTimeKeeper.cpp`, `lib/hal/AXP2101.h`, `lib/hal/XL9555.h`, `docs/power/datasheet-register.md`, `docs/power/display.md`, `docs/sim.md`, `plans/318`, `plans/322` | `bbe4d22c` |
| In flight | `include/FurbleRail.h` and the rail accounting on `origin/fix/ext5v-rail-ownership` (PR 493, plan 346); `GpsPinSource`, `gpsRxPin` and `gpsTxPin` on `origin/feat/gps-profile-pins` (PR 510, plan 336); the Waveshare row on `origin/fix/waveshare-board-row` (PR 500, plan 349) | branch heads on 2026-11-26 |

Not opened: the AXP2101 data sheet beyond the register facts plan 322 already
cites, the ESP32-S3 data sheet beyond the pin numbering, the TCA9554, PCF85063A,
QMI8658A, ES8311 and CO5300 documents, and the schematic as a drawing rather
than text. Every claim that rests on one of those carries **UNVERIFIED** above.

## Sources

- S1 https://github.com/slgray/furble commit `1d360ded14aa8bee86ef1d5938d73134e7dad00d`,
  2026-08-15, and the files it adds, `PORT_NOTES.md`, `src/waveshare_board.cpp`,
  `src/waveshare_board.h`, `src/m5_shim.h`, `src/pin_config.h`.
- S2 https://github.com/slgray/furble commit `01cf429777a75ca3684866f942bda2a7a90c0813`,
  2026-08-17, which adds `WAVESHARE-ESP32-S3-TOUCH-AMOLED-1_8.md`.
- W1 https://www.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-1.8 and its
  documentation mirror https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.8 .
  Sections read: Version Compatibility Notes, Features, Hardware Description,
  Onboard Resources, Screen Description.
- W2 https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.8 commit
  `01fd320`, 2026-11-18. Files read:
  `examples/arduino-v2/libraries/Mylibrary/pin_config.h`,
  `examples/arduino-v2/examples/01_HelloWorld/01_HelloWorld.ino`,
  `examples/arduino-v2/examples/14_LVGL_SD_Test/14_LVGL_SD_Test.ino`,
  `examples/arduino-v2/libraries/GFX_Library_for_Arduino/src/display/Arduino_CO5300.h`
  and `Arduino_SH8601.h` for presence, and the `SensorLib` and `XPowersLib`
  data sheet folders for what the vendor ships.
- W3 https://files.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-1.8/ESP32-S3-Touch-AMOLED-1.8.pdf
  read as a text export on 2026-11-26. Nets cited: `LCD_CS`, `LCD_RESET`,
  `LCD_TE`, `DSI_PWR_EN`, `TP_SCL`, `TP_SDA`, `TP_INT`, `TP_RESET`,
  `ESP32_SCL`, `ESP32_SDA`, `VCC-RTC`, `RTC_INT`, `QMI_INT1`, `QMI_INT2`,
  `A3V3`, `SDCS`, `PA_CTRL`, `I2S_SCLK`, `I2S_LRCK`, `I2S_DSDIN`,
  `I2S_ASDOUT`, `AXP_IRQ`, `PWRON`, `CHIP_PU`, and the parameter table entries
  `DCDC1` to `DCDC5`, `ALDO1` to `ALDO4`, `BLDO1`, `BLDO2`, `CPUSLDO`,
  `RTCLDO` and `VBACKUP`.
- G1 M5GFX 0.2.27, the version `platformio.ini` pins, as resolved for the
  `m5stick-s3` environment.
- X1 X-Powers AXP2101 data sheet revision 1.0, cited through
  `lib/hal/AXP2101.h` and plan 322 rather than reopened here.
- E1 Espressif ESP32-S3 series data sheet v2.2, for the GPIO 0 to 21 and 26 to
  48 numbering and the octal PSRAM pin group, as cited in
  `docs/power/datasheet-register.md` section 1.
- The repository files in the verification table are cited inline as
  [repo] with a file and a line.

## Implementation state

Research and port plan only. No environment, capability macro, board row,
panel class, driver or simulator panel exists, and no doc outside this one
changed. The three landing steps, in the order they can merge:

1. The display and input backend: the panel subclass, the touch adapter with
   the chip ID correction, the M5GFX autodetect pinning, and the platformio
   environment with `partitions_two_ota_16m.csv` and `CONFIG_SPIRAM_MODE_OCT`.
2. The power backend: a `Platform` for a board M5Unified does not know, on
   `lib/hal/AXP2101`, with DCDC1 protected, the expander driver, and the row
   from this plan.
3. The simulator panel, the RTC and IMU drivers, and the SDMMC host path,
   each with the bench items above as its gate.
