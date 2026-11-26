# Espressif ESP32

**Part and package.** Espressif ESP32, a dual-core XTensa LX6 SoC with Wi-Fi and
Bluetooth, in a QFN package. The exact variant per board (ESP32-D0WD-V3,
ESP32-U4WDH) is a board fact and is UNVERIFIED on this page.

**Role in furble.** The SoC on the StickC family and the Core. This page holds the
peripheral facts the hardware traps depend on: UART, RMT, LEDC, SPI and I2C.

**Documents.** [ESP32-DS], [ESP32-TRM]

## Documents used

| Key | Title | Revision or date | URL | Retrieved | Opened in this change |
| :--- | :--- | :--- | :--- | :--- | :--- |
| ESP32-DS | ESP32 Series Datasheet | Version 5.3 | <https://documentation.espressif.com/esp32_datasheet_en.pdf> | 2026-11-25 | Title page, revision string, the peripheral section list and the electrical chapter titles |
| ESP32-TRM | ESP32 Technical Reference Manual | Not retrieved | <https://documentation.espressif.com/esp32_technical_reference_manual_en.pdf> | not retrieved | Nothing. Every register-level clock fact below is therefore UNVERIFIED against the TRM. |

The tree's power register cites the same data sheet at v5.3 for the current
tables. Those tables were not read here.

## Facts furble depends on

| # | Fact | Document and section | Code | State |
| :--- | :--- | :--- | :--- | :--- |
| 1 | The SoC has three UART interfaces | ESP32-DS section 4.8.3 UART | `src/FurbleGPS.cpp:172` to `:193` (GPS on UART 2) | VERIFIED |
| 2 | The SoC has an SPI interface with the usual master and slave roles | ESP32-DS section 4.8.2 SPI | `src/FurbleEthernet.cpp:150` to `:165` (SPI2_HOST) | VERIFIED |
| 3 | The SoC has an I2C interface | ESP32-DS section 4.8.4 I2C Interface | `src/FurblePlatform.cpp:114`, `:115` | VERIFIED |
| 4 | The SoC has a remote control peripheral, RMT | ESP32-DS section 4.8.6 Remote Control Peripheral | `src/FurbleIR.cpp:226` to `:296` | VERIFIED |
| 5 | The SoC has an LED PWM controller with up to 16 channels | ESP32-DS section 4.8.8 LED PWM Controller | Backlight through the board support library, `src/FurbleUI.cpp:508` | VERIFIED |
| 6 | The GPS UART must be clocked from the crystal, not from APB, because DFS scales APB and would move the baud rate | Root `CLAUDE.md`, hardware traps. The TRM clock table was not read | `src/FurbleGPS.cpp:176` to `:186` uses `UART_SCLK_REF_TICK` on this SoC (claim from a code comment, TRM not read) | UNVERIFIED |
| 7 | A UART clocked from REF_TICK keeps its baud rate while APB scales | ESP32-TRM, clock selection for UART. Not read | `src/FurbleGPS.cpp:185` | UNVERIFIED |
| 8 | RMT keeps its own APB power lock, so it is DFS safe without an extra lock | Root `CLAUDE.md`, hardware traps. Not a data sheet statement | `src/FurbleIR.cpp:261` to `:292` | UNVERIFIED |
| 9 | The backlight PWM is clocked from APB, so APB must be pinned while the display is on | Root `CLAUDE.md`, hardware traps. The TRM clock tree was not read | `src/FurbleUI.cpp:508` to `:511`, `:1879` (claim from the trap note, TRM not read) | UNVERIFIED |
| 10 | One RMT memory block is 64 symbols on this SoC | ESP32-TRM RMT chapter. Not read | `src/FurbleIR.cpp:233` to `:235` | UNVERIFIED |
| 11 | The 45 kOhm internal pull-up gives about 73 uA per asserted active-low line at 3.3 V | ESP32-DS DC characteristics, via `docs/power/datasheet-register.md` section 1 | `src/FurblePlatform.cpp:482` to `:490` | CITED |

## Where the code implements it

- `src/FurbleGPS.cpp:172` to `:193`: UART configuration, driver install, pin
  routing, newline pattern detection and flush.
- `src/FurbleIR.cpp:226` to `:296`: RMT channel creation, the copy encoder, the
  carrier, transmit and the bounded wait.
- `src/FurbleEthernet.cpp:131` to `:165`: the SPI pin map at 20 MHz, then
  `spi_bus_initialize` with DMA.
- `src/FurblePlatform.cpp:482` to `:565`: the GPIO interrupt and wake path.
- `src/FurbleUI.cpp:508` to `:511` and `:1879`: the APB lock around the backlight.

## Citation gaps

| Constant or fact | Driver location | Why no section | Page row |
| :--- | :--- | :--- | :--- |
| none | - | No `lib/hal` driver. The peripheral facts live in `src/` and are cited above. | - |

## Facts still unverified

- The UART clock source table. Sections 4.8.3 and the TRM chapter were not read,
  so the REF_TICK and crystal claims are CITED or UNVERIFIED.
- The RMT memory block size and the APB power lock behaviour.
- The LED PWM clock source and its divider chain.
- The SPI clock ceiling for the Ethernet part.
- The current tables at v5.3, including the deep sleep and light sleep figures the
  power model uses.
- The variant fitted to each board.

## Redistribution

The data sheet carries an Espressif copyright notice and no redistribution grant,
so it is cited and not stored, per the rule in
[README.md](README.md#committed-documents).

[ESP32-DS]: https://documentation.espressif.com/esp32_datasheet_en.pdf
[ESP32-TRM]: https://documentation.espressif.com/esp32_technical_reference_manual_en.pdf
