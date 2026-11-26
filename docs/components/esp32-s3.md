# Espressif ESP32-S3

**Part and package.** Espressif ESP32-S3, a dual-core XTensa LX7 SoC with Wi-Fi,
Bluetooth LE, optional in-package flash and PSRAM, in a QFN package. The exact
variant per board (for example ESP32-S3-WROOM-1) is a board fact and is
UNVERIFIED on this page.

**Role in furble.** The SoC on the StickS3 and the Waveshare board, and the only
SoC where the picture is not a straight ESP32 port: no REF_TICK source, USB
Serial/JTAG instead of a UART bridge, and PSRAM. This page holds the peripheral
facts the hardware traps depend on.

**Documents.** [S3-DS], [S3-TRM]

## Documents used

| Key | Title | Revision or date | URL | Retrieved | Opened in this change |
| :--- | :--- | :--- | :--- | :--- | :--- |
| S3-DS | ESP32-S3 Series Datasheet | Version 2.2 | <https://documentation.espressif.com/esp32-s3_datasheet_en.pdf> | 2026-11-25 | Title page, revision string and the peripheral section list |
| S3-TRM | ESP32-S3 Technical Reference Manual | Not retrieved | <https://documentation.espressif.com/esp32-s3_technical_reference_manual_en.pdf> | not retrieved | Nothing. Register-level clock facts stay UNVERIFIED. |

## Facts furble depends on

| # | Fact | Document and section | Code | State |
| :--- | :--- | :--- | :--- | :--- |
| 1 | The peripheral chapter is 4.2, and each peripheral has its own subsection | S3-DS section 4.2 Peripherals | Whole tree | VERIFIED |
| 2 | The SoC has three UART interfaces | S3-DS section 4.2.1.1 UART Controller | `src/FurbleGPS.cpp:172` to `:193` | VERIFIED |
| 3 | The SoC has two I2C interfaces | S3-DS section 4.2.1.2 I2C Interface | `src/FurblePlatform.cpp:114`, `:115` | VERIFIED |
| 4 | The SoC has an SPI interface with SPI, Dual, Quad, QPI and OPI support | S3-DS section 4.2.1.5 Serial Peripheral Interface | `src/FurbleEthernet.cpp:150` to `:165` | VERIFIED |
| 5 | The SoC has a remote control peripheral, RMT, with transmit and receive | S3-DS section 4.2.1.11 Remote Control Peripheral | `src/FurbleIR.cpp:226` to `:296` | VERIFIED |
| 6 | The SoC has a USB Serial/JTAG controller, which is the console on this part | S3-DS section 4.2.1.8 USB Serial/JTAG Controller | `src/FurbleConsole.cpp:93` | VERIFIED |
| 7 | The SoC has an LED PWM controller with up to 8 channels | S3-DS feature list, "LED PWM controller, up to 8 channels". The subsection number was not captured. | Backlight through the board support library, `src/FurbleUI.cpp:508` (count read, subsection number not read) | UNVERIFIED |
| 8 | This SoC has no REF_TICK clock source, so the GPS UART must use the crystal | `src/FurbleGPS.cpp:176` to `:180`, comment. The TRM clock table was not read. | `src/FurbleGPS.cpp:183` uses `UART_SCLK_XTAL` (claim from a code comment, TRM not read) | UNVERIFIED |
| 9 | One RMT memory block is 48 symbols on this SoC, against 64 on the ESP32 | `src/FurbleIR.cpp:233` to `:235`, comment. The TRM RMT chapter was not read. | `src/FurbleIR.cpp:235` | UNVERIFIED |
| 10 | Opening the native USB Serial/JTAG port resets the chip, so a port open is the safe state transition | `CLAUDE.md`, hardware traps | `include/FurbleCrashRecord.h:150`, `src/FurbleConsole.cpp:2449` (no document statement) | UNVERIFIED |
| 11 | The 8 MB 8-line PSRAM adds about 140 uA in light sleep, and DMA buffers must stay in internal memory | `docs/power/datasheet-register.md` section 1, and `CLAUDE.md` PSRAM trap | `include/FurbleUI.h` buffer allocation | CITED |

## Where the code implements it

- `src/FurbleGPS.cpp:176` to `:186`: the SoC split between crystal and REF_TICK
  clock sources.
- `src/FurbleIR.cpp:226` to `:296`: RMT channel, carrier and transmit.
- `src/FurbleEthernet.cpp:131` to `:165`: SPI pins, clock and DMA.
- `src/FurbleConsole.cpp:93` and `:3685`: the console UART, which is the USB
  Serial/JTAG port on this part.
- `src/FurbleUI.cpp:508` to `:511`: the APB lock for the backlight.

## Citation gaps

| Constant or fact | Driver location | Why no section | Page row |
| :--- | :--- | :--- | :--- |
| none | - | No `lib/hal` driver. The peripheral facts live in `src/` and are cited above. | - |

## Facts still unverified

- The LED PWM subsection number in the data sheet.
- The UART clock source table, and the statement that this part has no REF_TICK.
  That claim comes from a firmware comment.
- The RMT memory block size of 48 symbols.
- The USB Serial/JTAG reset behaviour on port open.
- The current tables, including light sleep with PSRAM.
- Whether any board runs the SoC at 1.8 V flash and what that changes for SPI.

## Redistribution

The data sheet carries an Espressif copyright notice and no redistribution grant,
so it is cited and not stored, per the rule in
[README.md](README.md#committed-documents).

[S3-DS]: https://documentation.espressif.com/esp32-s3_datasheet_en.pdf
[S3-TRM]: https://documentation.espressif.com/esp32-s3_technical_reference_manual_en.pdf
