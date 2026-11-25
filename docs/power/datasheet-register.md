# Datasheet and document register

Status: compiled from the seven lane reports. This file records every datasheet,
application note, errata, board file, vendor document, library, and project
document that a lane cited, with the part, the document, the revision, and the
limit that mattered to the lane. Section 13 maps every board and part to its
document status and names the artifact that closes each gap.

Rules:

- The limit column quotes the number or behavior the lane used. It is not a new
  verification.
- The lane status column says what the lane did: opened the document, quoted it
  through the repository model, or cited it by URL only. "Not reopened" means
  the lane did not check the revision itself.
- No errata document is published for the cited parts, so none is cited. That is
  recorded as a gap with its substitute, not filled with an invented document;
  section 15 of the SX1261/2 datasheet is the substitute for that part.
- A datasheet number is not a local measurement. See `measurement-plan.md` for
  the boundary and promotion rules.
- Per-part register and timing citations live in
  [`docs/components/`](../components/README.md). This file records the document
  inventory a power lane cited. That corpus records what the firmware depends on,
  with the code location and a verification state per fact.

## 1. SoC, radio, and module datasheets

| Part | Document | Revision | Relevant limit | Lane status | Cited by |
| --- | --- | --- | --- | --- | --- |
| Espressif ESP32 | ESP32 Series Datasheet, Tables 4-2 and 5-4, https://documentation.espressif.com/esp32_datasheet_en.pdf | v5.3 | Modem-sleep active ranges per frequency (min all peripheral clocks off, max all on); light sleep 0.8 mA; deep sleep 10 uA with RTC timer and memory; hibernation 5 uA; BLE TX 130 mA typ at 0 dBm and 50 percent TX duty; BLE RX 95 to 100 mA | Quoted through `tools/power-model/README.md`; Board lane did not reopen | BLE, SoC, Board, Model |
| Espressif ESP32-S3 | ESP32-S3 Series Datasheet, Tables 5-8 to 5-10, https://documentation.espressif.com/esp32-s3_datasheet_en.pdf | v2.2 | Active 40.2 mA at 80 MHz, 56.9 mA at 160 MHz, 73.8 mA at 240 MHz; WAITI 29.1 mA at 80 MHz; light sleep 240 uA; deep sleep 7 uA RTC memory only, 8 uA with RTC peripherals; add 140 uA for 8 MB 8-line PSRAM in light sleep; internal weak pull-up 45 kOhm typical; BLE TX 176 mA at 0 dBm and 193 mA at 9 dBm at 100 percent duty; BLE RX 93 mA | Opened by Board lane; other lanes quote the same tables | BLE, SoC, Display, GPS/IMU, Board |
| Espressif ESP32-S3-WROOM-1 and WROOM-1U | ESP32-S3-WROOM-1 and WROOM-1U Datasheet, Table 9, excerpted from the SoC power-consumption section | v1.0 | Wi-Fi TX 802.11b 1 Mbps at 20.5 dBm 355 mA; 802.11g 54 Mbps at 18 dBm 297 mA; HT20 MCS7 at 17.5 dBm 286 mA; RX HT20 95 mA, HT40 97 mA. Peak current at 3.3 V, 25 C, 100 percent TX duty | Quoted by Wi-Fi lane; not reproduced | Wi-Fi |
| Espressif ESP32-S3 | USB Serial/JTAG and internal pull-up limits, same SoC datasheet | v2.2 | USB Serial/JTAG module cannot work under light sleep; 45 kOhm internal pull-up gives about 73 uA per asserted active-low line at 3.3 V | Quoted by SoC and Board lanes | SoC, Board |
| Espressif ESP32 | ESP32 Technical Reference Manual, https://documentation.espressif.com/esp32_technical_reference_manual_en.pdf, SHA-256 `1007b90e6fafc16541d693d7444b63e1c5b01e46e60cce9b6810e02340a64b82` | v5.9 | UART clock is 80 MHz APB_CLK or REF_TICK only, selected by `UART_TICK_REF_ALWAYS_ON` (19.3.2 p. 312), and REF_TICK is derived from APB_CLK with a per-source divider (7.2.4.2 and Table 7.2-5 p. 169). RMT: eight channels, each TX or RX but not both, sharing one 512 x 32-bit RAM in eight 64-word blocks (30.2.1 and 30.2.2 p. 728) | Opened for the vendor document pass | SoC, Board |
| Espressif ESP32-S3 | ESP32-S3 Technical Reference Manual, https://documentation.espressif.com/esp32-s3_technical_reference_manual_en.pdf, SHA-256 `4484bf8a69035ec42a731c58c64ada6fbd1f1618c5559409f134d9ea083f444f` | v1.8 | UART Core has three sources, APB_CLK, RC_FAST_CLK and XTAL_CLK, by `UART_SCLK_SEL` with a fractional divider of 1 to 256 (26.3 and 26.4.1 p. 926, field values 1/2/3 p. 961). RMT: four TX plus four RX channels sharing one 384 x 32-bit RAM in eight 48-word blocks (37.2 p. 1415, 37.3.2.2 p. 1417). USB Serial/JTAG: RTS 1 with DTR 0 resets the SoC (Table 33.3-2 p. 1248), SRST resets the digital system but not the controller (33.3.5 p. 1249), and the module needs the PLL and APB_CLK at 40 MHz or more, is gated in light sleep and powered down in deep sleep (33.4.2 p. 1253, sequences in Tables 33.4-3 and 33.4-4 p. 1254) | Opened for the vendor document pass | SoC, Board, GPS/IMU |
| Espressif ESP32-S3-PICO-1 | ESP32-S3-PICO-1 Series Datasheet, https://documentation.espressif.com/esp32-s3-pico-1_datasheet_en.pdf, SHA-256 `44daca9121111a9c7df437e18c3c153258a20e68fe57fcd3bd9be593ad162979` | v1.2 | N8R2 is 8 MB quad flash plus 2 MB quad PSRAM, N8R8 is 8 MB quad flash plus 8 MB octal PSRAM (Table 1-1 p. 9). On N8R8, SPICS1 and GPIO33 to GPIO37 belong to the octal PSRAM and are unavailable for other uses (pin table note 4 p. 13) | Opened for the vendor document pass | Board |
| Espressif ESP32-S3 module pin rules | ESP32-S3 Series Datasheet v2.2 | v2.2 | GPIO33 to GPIO37 carry DQ4 to DQ7 plus DQS/DM only in octal mode (Table 2-14 p. 31, section 2.3.5 p. 26), and the notice forbids using in-package flash or PSRAM pins for anything else | Opened for the vendor document pass | Board |
| Semtech SX1261/2 | SX1261/2 datasheet, DS.SX1261-2.W.APP, ECO 067443, https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1198/DS_SX1261_2_V2-2.pdf and https://wiki.lilygo.cc/datasheet/DS_SX1261_SX1262.pdf (byte-identical, SHA-256 `6d783125dbef567954ce53095ad068d0950b2a8de52f95bb2cfde4146c4306c2`). The Semtech product page link is form and login gated | Rev 2.2, December 2024 | NRESET held low typically 100 us (8.1 p. 53). NSS rising to BUSY rising 600 ns maximum, BUSY held high through 20 kOhm in sleep (8.3.1 p. 55). Switching: SLEEP to STBY_RC 3500 us cold and 340 us warm, STBY_RC to STBY_XOSC 31 us, to FS 50, to RX 83, to TX 126, STBY_XOSC to FS 40, to TX 105, to RX 62, FS to RX 41, to TX 76, RX to FS 15, to TX 92 (Table 8-2 p. 56). NSS falling to SCK 100 us minimum when leaving sleep (Table 8-1 t10 and 8.2.2 p. 54). Section 15 p. 109 to 111 lists four known limitations | Opened for the vendor document pass. Not fitted on any supported board | Board |

## 2. Display, panel, and touch datasheets

Every row in this section was downloaded and read on 2026-11-26. The revision
cell carries the document version, the page count, the first eight hex digits of
the sha256 of the file that was read, and the read date. The limit cell quotes
the page the number came from, so a reader can recheck one page instead of the
whole document.

| Part | Document | Revision | Relevant limit | Lane status | Cited by |
| --- | --- | --- | --- | --- | --- |
| Sitronix ST7735S | ST7735S datasheet, https://cdn.sparkfun.com/assets/9/0/2/5/8/ST7735S_v1.1.pdf | v1.1, 2011-11-21, 201 pages, sha256 0322a527, read 2026-11-26 | Table 3 page 30: normal mode 0.9 mA typ, 2.0 mA max; sleep-in 15 uA typ, 30 uA max | Opened at Table 3 and the page footers | Display, BLE, Board |
| Sitronix ST7789V | ST7789V datasheet, https://newhavendisplay.com/content/datasheets/ST7789V.pdf | v1.3, 2014/03, 316 pages, sha256 8ecf0e43, read 2026-11-26 | Table 3 page 40: normal mode black image at 60 Hz 6.0 mA typ, 7.5 mA max; sleep-in 15 uA typ, 30 uA max; partial plus idle mode 5.0 mA. `FRCTRL2` (C6h) frame-rate control. Dwell: 5 ms after SLPIN and after SLPOUT before any other command, and 120 ms from SLPOUT to the next SLPIN only (sections 9.1.11 and 9.1.12, pages 182 and 184). Table 9 reset timing on page 48 | Opened at Table 3, Table 9 and both sleep sections | Display, BLE, Board, SoC |
| Sitronix ST7789V2 | ST7789V2 datasheet, https://files.waveshare.com/upload/c/c9/ST7789V2.pdf | v1.0, 2016/11, 319 pages, sha256 89376784, read 2026-11-26 | Same sleep wording as ST7789V v1.3: 5 ms after SLPIN and after SLPOUT, 120 ms from SLPOUT to the next SLPIN (sections 9.1.11 and 9.1.12, pages 184 and 186). Table 3 page 40 sleep-in 15 uA typ, 30 uA max. Table 9 reset timing on page 48 | Opened at Table 3, Table 9 and both sleep sections | Display |
| Sitronix ST7789P3 | ST7789P3 datasheet, https://files.waveshare.com/wiki/ESP32-S3-GEEK/ST7789P3.pdf | v0.1, 2022/03, 276 pages, sha256 26e62a24, read 2026-11-26 | Table 3 page 34: normal mode 6.0 mA typ, 7.5 mA max; sleep-in 20 uA typ, 50 uA max; partial plus idle mode 5.0 mA typ, 6.0 mA max. Same sleep wording as ST7789V (sections 9.1.11 and 9.1.12, pages 143 and 145). Table 8 reset timing on page 41: TRW 10 us minimum, TRT 120 ms maximum. This is the m5stick-s3 panel | Opened at Table 3, Table 8 and both sleep sections | Display, Board |
| Sitronix ST7796S | ST7796S datasheet, https://dl.espressif.com/dl/schematics/ST7796S_SPEC_V1.0.pdf | v1.0, 2014/11, 279 pages, sha256 212f7a8e, read 2026-11-26 | 320x480, 262K colours. 5 ms after SLPIN, and 120 ms from SLPOUT to the next SLPIN only (sections 9.2.12 and 9.2.13, pages 159 and 160). MIPI DSI in section 8.5, page 94 | Opened at the cover and both sleep sections | Display |
| Sitronix ST7796U | ST7796U datasheet, https://www.smartdisplaytech.com/wp-content/uploads/2023/03/ST7796S_spec.pdf | v1.0, 2020/02, 287 pages, sha256 c907c64e, read 2026-11-26 | 320x480, 16.7M colours. 5 ms after SLPIN, and 120 ms from SLPOUT to the next SLPIN only (sections 9.2.12 and 9.2.13, pages 163 and 164). MIPI DSI in section 8.5, page 98. The host file is named `ST7796S_spec.pdf` while the cover and the page footers say ST7796U. LilyGo's T-Lora Pager document names ST7796U for its 480x222 panel | Opened at the cover, the contents and both sleep sections | Display |
| ILITEK ILI9342C | ILI9342C datasheet, https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/datasheet/core/ILI9342C-ILITEK.pdf | V101, document ILI9342C_DTS_V101_20111214, 235 pages and complete at 235 of 235, sha256 604a35c0, read 2026-11-26 | No supply current table anywhere in the document. Section 18.2 page 227 gives only plus or minus 0.1 uA logic input leakage, so the model keeps its ST7789V analogy tagged estimated: 6 to 8 mA operating, 15 to 100 uA sleep-in. Dwell is 120 ms in both directions, SLPIN to SLPOUT and SLPOUT to the next SLPIN (sections 8.2.11 and 8.2.12, pages 100 and 101). Section 19 page 235 carries both the 2011/12/14 V101 line and a 2011/12/26 item | Opened at the cover, the contents, both sleep sections, the DC characteristics and the revision history | Display, Board |
| ILITEK ILI9341 | ILI9341 datasheet, https://cdn-shop.adafruit.com/datasheets/ILI9341.pdf | V1.11, document ILI9341_DS_V1.11.pdf, 245 pages, sha256 a9bbfdf6, read 2026-11-26 | Page 236: standby current IST 100 uA maximum at VCI 2.8 V and 25 C, used as the ILI9342C sleep-in high bound. The same 120 ms pair in both directions as the ILI9342C | Opened at the cover and page 236 | Display, Board |
| Chipone CO5300 | CO5300 datasheet V0.01, https://files.waveshare.com/wiki/common/Co5300_Datasheet.pdf | V0.01, 2023/07/24, 226 pages, sha256 2ad0461a, read 2026-11-26 | Page 103: deep stand-by IDSTB 2 uA VDDI plus 1 uA VCI; Sleep In 110 uA VDDI plus 25 uA VCI on DSI LP, 85 uA plus 25 uA on DSI ULP. Revision history page 2 records 0.01 "Update Deep Stand-by mode current". Commands start at section 7.1, page 117. The last page is 226 | Opened at the cover, the revision history, the current table, the command table start and the last page | Display |
| Chipone CO5300 | CO5300 datasheet V0.00, https://files.waveshare.com/wiki/common/CO5300_Datasheet_V0.00.pdf | V0.00, 2023/03/28, 226 pages, sha256 20161057, read 2026-11-26 | The same page count and the same Sleep In rows as V0.01, with no deep stand-by row on page 103 | Opened at page 103 to compare | Display |
| Sino Wealth SH1106 | SH1106 datasheet V2.3, https://www.pololu.com/file/0J1813/SH1106.pdf | V2.3, June 2013, 49 pages, sha256 ab5ce5f3, read 2026-11-26 | Page 35: sleep current 5 mA maximum, not microamps, in both rows, the VDD1 and VDD2 row at 3 V and 3.7 V and the external VPP row at 9 V, both at TA 25 C. Revision history page 49: 2.1 April 2012 raises the maximum VPP to 14.0 V, 2.2 April 2012, 2.3 June 2013 changes the power on and off sequence on pages 32 to 34 | Opened at page 35 and page 49 | Display |
| Sino Wealth SH1106, OCR copy | SH1106 V2.3 datasheet, https://www.crystalfontz.com/controllers/uploaded/SH1106-Revision_2.3.pdf | V2.3, 49 pages, sha256 3baf1812, read 2026-11-26 | The same text as the Pololu copy at 0.965 similarity. The PDF metadata names `SH1106-Revision_2.3.pdf` and `SH1106 V2.3.doc`, and the producer field records an Acrobat Paper Capture OCR pass created 2017-08-16 | Opened the metadata and compared the text | Display |
| Hynitron CST9217 | CST9217 datasheet V1.0, https://raw.githubusercontent.com/ScripTerasu/cst92xx-touch-driver/0e4cede38d860ba08cf98c7a69400e10e8274608/docs/CST9217.pdf | V1.0, 2022-08-11, 10 pages, sha256 74cf16e6, read 2026-11-26 | Section 9.1 page 5: dynamic 1.3 mA at 100 Hz, monitor 12 uA at 30 Hz, sleep 2 uA. Page 8: the default 8-bit I2C address is 0xB4 and 0xB5, so 0x5A in 7-bit form, and it is configurable in the chip. Section 10.5 page 9: TPOR 100 ms, TPR 1 ms, TRON 100 ms, TRST 0.1 ms. There is no register map in the document | Opened all ten pages | Display |

## 3. Power management ICs

| Part | Document | Revision | Relevant limit | Lane status | Cited by |
| --- | --- | --- | --- | --- | --- |
| X-Powers AXP192 | AXP192 enhanced single-cell Li-battery PMIC datasheet, http://images.shoutwiki.com/mindworks/8/8b/2020_infrasonic_wildfire_detector_APX192_Enhanced_Single_Cell_datasheet_en.pdf | No revision string; cover says X-Powers 2010 | Off mode 27 uA at BAT 3.8 V; suspend 86 uA; DCDC1 26 uA no load; DCDC2 and DCDC3 20 uA each; LDO2 and LDO3 100 uA each; LDOIO0 90 uA; `REG 82H` ADC enable 1; `REG 83H` ADC enable 2; `REG 84H` ADC sample rate, 25 Hz default; `REG 12H` rail enables; `REG 28H` LDO2/LDO3 voltage; `REG 27H` DCDC3; `REG 33H` charge enable, 4.2 V, current | Opened by Board lane; no revision string in the fetched copy | Board, Display, SoC |
| Injoinic IP5306 | IP5306 datasheet, https://www.laskakit.cz/user/related_files/ip5306.pdf | V1.01 | Standby 50 uA; input quiescent 100 uA; boost shuts off after 32 s of continuous load below 45 mA at the power-on default of `SYS_CTL2` bits 3:2 = `01`; M5Unified 0.2.20 writes `0x02 = 0x6C`, bits 3:2 = `11`, so the shipped Core cuts at 64 s; the public datasheet does not tabulate the register map | Opened by Board lane | Board, Display, SoC |
| Injoinic IP5306 register document | https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/datasheet/core/IIC_IP5306_REG_V1.4_cn.pdf | V1.21 (document revision V1.4) | `SYS_CTL0` 0x00 bit 1 BOOST output normally open; `SYS_CTL2` 0x02 bits 3:2 light-load shutdown 8/32/16/64 s; `Charger_CTL1` 0x21 bits 7:6 battery-side stop-charge 200/400/500/600 mA; `CHG_DIG_CTL0` 0x24 charge current. M5Unified writes 0x00 = 0x31 (bit 1 clear) and 0x02 = 0x6C (64 s) | Opened for plan 354 | Board |
| M5Stack M5PM1 | M5Stack M5PM1 Chip User Manual, https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1207/M5PM1_Datasheet_EN.pdf, SHA-256 `c6daffd0ab89d8de50c0a19f35f94321bc6c9511ad017dd242a3e765d88a96b6`. A second byte-different V1.9 build is https://raw.githubusercontent.com/m5stack/M5PM1/main/docs/M5PM1_Datasheet_EN.pdf, SHA-256 `f008794c8a6b672b61452c77a9359b6332d82194dce92f9a21494e283783b656`; every page quoted here agrees between the two | V1.9 | `PWR_CFG` 0x06 default 0x17 (LED enable, 5 V boost off, 3.3 V LDO on, 3.3 V DCDC on, charge on, p. 13); `I2C_CFG` 0x09 `SLP_TO` bits 3:0, called `SLEEP[3:0]` in section 6, sets the idle sleep time, 0 disables it, and after an idle sleep wake the first transaction only wakes the part and fails (section 6 p. 27, note 2 p. 15); idle sleep is invalid while PWM is enabled and while entering Download mode (p. 28); `WDT_CNT` 0x0A seconds, 0 disables; `WDT_KEY` 0x0B feed 0xA5; `SYS_CMD` 0x0C is `KEY` 0xA plus `CMD` 01 power off, 10 restart, 11 Download (p. 15); `HOLD_CFG` 0x07 retains 5 V, LDO, and GPIO state (p. 13); IRQ Status 1/2/3 at 0x40/0x41/0x42 with masks at 0x43/0x44/0x45, and the IRQ line is released only when all three status registers are cleared (p. 28). The 0x2F note couples WAKE_SRC to IRQ Status 3 bit 1, but the manual also gives WAKE_SRC as 0x05 (p. 4, p. 13 and p. 23) and 0x04 (p. 29); the address is inconsistent inside V1.9 | Opened by Board lane, re-read page by page for the vendor document pass | Board, SoC |

## 4. Sensors, GNSS, audio, storage, and actuators

| Part | Document | Revision | Relevant limit | Lane status | Cited by |
| --- | --- | --- | --- | --- | --- |
| Bosch BMI270 | BST-BMI270-DS000-08, https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmi270-ds000.pdf | Revision 1.6, March 2026; file name suffix 08. 150 pages | Normal accel plus gyro 685 uA; accelerometer low power 4 to 10 uA depending on `ACC_CONF`; suspend 3.5 uA; registers `PWR_CTRL` 0x7D, `PWR_CONF` 0x7C, `ACC_CONF` 0x40. All typical at VDD 1.8 V. Section 5.2.40 FEATURES, pages 92 and 93, also documents the motion engines: ANYMO_1 at feature page 0x3C holds the duration in bits 12:0 in 50 Hz samples (5 is 100 ms), and ANYMO_2 at feature page 0x3E holds the threshold in bits 10:0 over 0 to 1 g, default 0xAA, stated as 83 mg. One count is 1 g / 2048 = 0.4883 mg | Opened by the IMU motion scale audit on 2026-11-25, sha256 prefix 0dab4065; the Board lane quotes it through the model and did not reopen it | SoC, GPS/IMU, Board |
| Bosch BMI270 SensorAPI | BMI270 SensorAPI, https://github.com/boschsensortec/BMI270_SensorAPI | Not stated | Corroborates the section 5.2.40 field layout and masks, and its `any_motion_interrupt` and `no_motion_interrupt` examples state the same 20 ms per duration count and the 83 mg and 70 mg defaults. It is secondary to the datasheet now that revision 1.6 documents the engines | Quoted by GPS/IMU lane | GPS/IMU |
| TDK MPU-6886 | MPU-6886 datasheet DS-000193, https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/datasheet/core/MPU-6886-000193%2Bv1.1_GHIC_en.pdf | Revision 1.1. 59 pages | Six-axis low-noise gyro plus accel 2.79 mA typ, 3.0 mA max; accelerometer low power at 100 Hz 40 uA typ; sleep 6 uA typ; registers `PWR_MGMT_1` 0x6B, `PWR_MGMT_2` 0x6C. Table 3 is marked target spec. Table 15 and sections 8.20 to 8.22 place `ACCEL_WOM_X/Y/Z_THR` at 0x20, 0x21 and 0x22 and give no per-count figure for them, so the mg per count in this project is an assumption. Section 5.1 step 4 of the same document says to write `ACCEL_WOM_THR` at 0x1F, while table 15 has no 0x1F entry at all: a vendor document defect, recorded in plan 332, and our driver uses the three table 15 addresses | Opened by the IMU motion scale audit on 2026-11-25, sha256 prefix 6751256f; the Board lane quotes it through the model | GPS/IMU, SoC, Board |
| M5StickC-Plus MPU6886 driver | M5StickC-Plus `MPU6886.h`, https://github.com/m5stack/M5StickC-Plus/blob/master/src/utility/MPU6886.h | Repository copy, not a datasheet | WOM registers `ACCEL_WOM_X/Y/Z_THR` 0x20 to 0x22, `INT_PIN_CFG` 0x37, `INT_ENABLE` 0x38, `ACCEL_INTEL_CTRL` 0x69. Its threshold addresses agree with datasheet table 15, not with section 5.1 step 4 | Vendor code read by GPS/IMU lane | GPS/IMU |
| QST QMI8658A | 13-52-25, https://www.qstcorp.com/upload/pdf/202512/13-52-25%20QMI8658A%20Datasheet%20Rev%20D.pdf | Rev D. 78 pages | Any, no and significant motion run in the on-chip co-processor. CTRL9 command code 0x0E configures the engines, CTRL8 bits 1, 2 and 3 enable them with bit 6 routing to the interrupt pin, and STATUS1 bits 7:5 carry the status. Sections 9.4 to 9.6 | Opened by the IMU motion scale audit on 2026-11-25, sha256 prefix edda798c. No board, driver or backend in this tree uses the part | GPS/IMU |
| CASIC ATGM336H module | ATGM336H module manual C90770_ATGM336H-5N31, https://github.com/Albresky/GPS-ATGM336H-Library/blob/master/manual/C90770_ATGM336H-5N31_2016-12-26.pdf | 2016-12-26 | GPS plus BDS typical below 25 mA at 3.3 V continuous; peak 100 mA excluding the antenna; VBAT backup 10 uA; no acquisition versus tracking split | Quoted through the model; byte-identical mirror verified by the model author | GPS/IMU, BLE, Board |
| CASIC AT6668 | AT6668/AT6668U datasheet, DS-AT6668, https://cdn.icstop.com/upload/pdfs/ca/27/ca27336d2c2d0967e811278e3e53824a.pdf ; the only vendor-domain copy is V1.0 at https://15960580.s21i.faiusr.com/61/ABUIABA9GAAghKelrAYo_ID6tgY.pdf | V1.4, 2023.09.04 | QFN40 5 x 5 x 0.75 mm. Continuous 37 mA typical at 3.3 V with BDS, GPS and GLONASS enabled; battery backup 8 uA; sleep 30 uA with ON_OFF low; active antenna detect 2.5 mA; antenna short-circuit limit 45/50/65 mA; reset 160 ms; 26 MHz TCXO | Opened for this change on 2026-11-25; every figure here carries its section and page on [docs/components/at6668.md](../components/at6668.md) | GPS/IMU, Board |
| CASIC AT6558 | AT6558 datasheet, https://dratek.cz/docs/produkty/1/1045/at6558.pdf | Not stated | Tracking 23 mA typical BD plus GPS; standby below 10 uA; VBAT 10 uA typical, 40 uA max; active antenna feed 2.4/3.0/3.6 mA. No longer the AT6668 proxy: the AT6668 datasheet above is the part's own document and gives 37 mA | Quoted by GPS/IMU lane; superseded as the proxy | GPS/IMU, Board |
| M5Stack Unit GPS v1.1 | Product page, https://docs.m5stack.com/en/unit/Unit-GPS%20v1.1 | Web page, no revision | 5 V 31.64 mA at the Grove input, including the onboard regulator; ATGM336H with AT6668; no backup supply listed; built-in patch antenna marginal indoors | Vendor document, quoted | GPS/IMU, BLE, Board |
| CASIC protocol | Multimode navigation receiver protocol, http://www.espruino.com/files/CASIC_en.pdf | Not stated | `$PCAS` family, `BA CE` framing, `AID-INI`, `CFG-NAVX`, `MON-HW`; the printed checksum formula is wrong and the L76K id-first form is the one the code uses | Quoted by GPS/IMU lane | GPS/IMU |
| Quectel L76K | GNSS protocol specification v1.1, https://www.waveshare.net/w/upload/d/dd/Quectel_L76K_GNSS_Protocol_Specification_V1.1.pdf | v1.1 | PCAS02, PCAS03, PCAS04, PCAS10; GSV and GSA fields; id-first checksum | Quoted by GPS/IMU lane | GPS/IMU |
| NS4168 | NS4168 datasheet, https://www.chipsourcetek.com/Uploads/file/NS4168.pdf | Not stated | 13 mA quiescent at 5 V; 1 uA shutdown leakage | Quoted through the model | Board, Display |
| WIZnet W5500 | WIZnet W5500 datasheet version 1.1.0, dated 17DEC2022, https://docs.wiznet.io/img/products/w5500/W5500_ds_v110e.pdf, 1,893,745 bytes, sha256 `7b826b808084ccd986bcc22904c00a07a508ef42fb93d079fe7150a4c4f1a63d`. The corpus v1.0.6 link is a 404, checked 2026-11-25 | v1.1.0, 17DEC2022 | Section 1.1 page 10, `RSTn` pin 37: "RESET should be held low at least 500 us for W5500 reset". Section 5.5.1 page 60 and figure 22, Reset Timing: `TRC` minimum 500 us, `TPL` (RSTn released to internal PLOCK) maximum 1 ms. Section 5.5.2 page 60: regulator wake 10 us. Section 3.1 page 29, section 4.1 page 32, `SHAR` registers 0x0009 to 0x000E page 33. Section 5.3 page 59: `IDD1` 132 mA, `IDD2` 13 mA in PHY power-down. Section 5.4 page 60: link currents 128, 75 and 65 mA. The pinned PHY hard-codes 100 us (`components/esp_eth/src/spi/w5500/esp_eth_phy_w5500.c:160`), the defect recorded in `plans/implemented/42-waveshare-eth-node.md`; `src/FurbleEthernet.cpp` now drives the 500 us pulse itself in `resetW5500()`, which meets `TRC`, and passes `reset_gpio_num = -1` so the PHY adds no second, under-spec assertion. It adds no wait for `TPL`, so the release-to-PLL guard is an open item, not a compliance claim | Opened for the vendor document pass and for the RESETn fix; copy size and sha256 verified above | Board, Wi-Fi |
| Vybronics VC1030B028F | Coin vibration motor datasheet, https://www.vybronics.com/coin-vibration-motors/with-brushes/v-c1030b028f | Not stated | 64 mA typical operating, 85 mA rated maximum, more at start and stall. Representative part because M5Stack names no motor | Quoted through the model | Board |
| BM8563 RTC | BM8563 documentation, cited for the Plus2 alarm wake | Not stated | RTC alarm is the Plus2 timed-wake source | Cited, not opened | Board, SoC |
| NXP PCF8563 | PCF8563 Real-Time Clock/Calendar product data sheet, https://www.nxp.com/docs/en/data-sheet/PCF8563.pdf | Rev. 11.1, 19 January 2026 | Timer control 0x0E and timer value 0x0F carry the Plus2 alarm wake. M5Unified writes 0x0E = 0x82 for a 1 Hz tick and 0x83 for a 1/60 Hz tick, so the same count means seconds or minutes. The register and timing facts are on docs/components/pcf8563.md | Front matter and the voltage-low section opened; the timer section number was not supplied | Board, SoC |
| FT6336U touch controller | Core2 touch documentation and FT6336U datasheet | Not stated | Monitor and hibernate modes exist; current not in the tree; the datasheet was not opened by the Board lane | Not opened | Board, Display |
| ES8311 codec and AW8737 amplifier | M5Stack StickS3 product page | Web page, no revision | Parts are named; quiescent currents are not in the tree | Cited, not opened | Board |

## 5. Vendor board documents and schematics

| Board or part | Document | Revision | Relevant detail | Lane status | Cited by |
| --- | --- | --- | --- | --- | --- |
| M5Stack StickS3 | Product page, https://docs.m5stack.com/en/core/StickS3 | Web page, no revision | Power off 14.02 uA; L1 52.47 uA; L2 102.40 uA; L3A 36.69 mA; full load 519.02 mA at 4.2 V; L state semantics not defined; BMI270; 250 mAh; EXT_5V feeds Grove, Hat EXT_5V, and IR TX/RX; ES8311 and AW8737 | Quoted by Board and GPS lanes | Board, GPS/IMU, BLE |
| M5Stack StickS3 low power | M5PM1 low-power page, https://docs.m5stack.com/en/arduino/m5sticks3/m5pm1, SHA-256 `f0a92685260dfc3af9dcdcb3ea334eaecf56d05a2623842d8a0315a7aec59524` | Web page, no revision | PM1 timer wake; I2C idle sleep; after an idle-sleep wake the first ESP32-S3 to M5PM1 communication only wakes the PMIC and fails, and the next transaction is the first valid one; BMI270 INT1 to M5PM1 GPIO4; PYG1_IRQ to ESP32-S3 GPIO13 | Opened for the vendor document pass, 2026-11-25 | GPS/IMU, SoC |
| M5Stack M5PaperMono power levels | M5PM1 and M5IOE1 page, https://docs.m5stack.com/en/arduino/papermono/m5pm1_m5ioe1, SHA-256 `2bd391ac1a4b48444ae1f2945406ab36c354f1c056646c59c76993769ad40e4c` | Web page, no revision | L1 to L3B all source from L0 (SYS_VBUS) rather than from the preceding level, so the levels are independent, not a series; after M5PM1 power on, L1, L2 and L3A are enabled automatically and M5Unified enables L3B through the M5IOE1 | Opened for the vendor document pass, 2026-11-25. Not a StickS3 statement | Board |
| M5Stack M5PM1 library | Function reference README, https://github.com/m5stack/M5PM1/blob/main/README_FUNCTION_EN.md | Library 1.0.6 | `gpioSetWakeEnable`, `gpioSetWakeEdge`, `irqSetGpioMask`, `irqGetGpioStatus`, `irqClearGpioAll` | Vendor code read | GPS/IMU, Board |
| M5Stack StickC | Schematic rebuild pinmap, 20191118__StickC_A04_3110_Schematic_Rebuild_PinMap.pdf, https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/schematic/Core/M5StickC/20191118__StickC_A04_3110_Schematic_Rebuild_PinMap.pdf | 2019-11-18 | IR_LED on GPIO9; contains no resistor values | URL only; Board lane notes no schematic PDF or netlist is in the tree | Board, BLE |
| M5Stack StickC Plus2 | Plus2 documentation | Web page, no revision | HOLD on GPIO4; backlight PWM on GPIO27; battery divider on GPIO38 with 2.0 ratio; LED and IR share GPIO19; no PMIC and no charging telemetry | Quoted by Board lane | Board |
| M5Stack Core and Core2 | microSD documentation | Web page, no revision | No SD load switch; unmount stops transactions but does not remove power | Quoted by Board lane | Board |
| M5Stack Core2 | Touch documentation and AXP192 vendor driver, https://github.com/m5stack/M5Core2/blob/master/src/AXP192.cpp | Repository copy | Touch controller left running for tap wake; DCDC3 2.4 to 3.3 V maps to brightness 0 to 100 | Vendor code read | Board, Display |
| Waveshare ESP32-S3-ETH | Wiki, https://www.waveshare.com/wiki/ESP32-S3-ETH | Web page, no revision | W5500 on SPI; WS2812 on GPIO21; microSD in 1-line mode and unused by furble; PoE HAT; no battery or PMIC | Vendor document | Board, Wi-Fi |
| Waveshare ESP32-S3-ETH | Schematic, https://files.waveshare.com/wiki/ESP32-S3-ETH/ESP32-S3-ETH-Schematic.pdf | Not stated | Board nets and resistor values; the PDF is referenced by URL only and is not in the tree | URL only | Board |

## 6. Code at pinned versions

These are board files and code revisions, not datasheets. They decide pin,
rail, and register behavior where the repository has no board file of its own.

| Component | Version | File or area | Relevant behavior | Cited by |
| --- | --- | --- | --- | --- |
| M5Unified | 0.2.20 | `Power_Class.cpp`, `AXP192_Class.cpp`, `IP5306_Class.cpp`, `M5PM1_Class.cpp`, `M5Unified.cpp`, `IMU_Class`, `I2C_Class` | AXP192 register block at `M5.begin()`; IP5306 `0x00 = 0x31`, `0x02 = 0x6C`; PM1 `I2C_CFG = 0x00`; Plus2 `_powerOff()` reaches `esp_deep_sleep_start()` without a wake source; `setExtOutput`; `getBatteryCurrent`; IMU `sleep`; I2C register helpers | Board, SoC, Display, GPS/IMU |
| M5GFX | 0.2.27 | `src/M5GFX.cpp`, `src/lgfx/v1/LGFXBase.hpp`, `src/lgfx/v1/panel/Panel_LCD.inl`, `src/lgfx/v1/platforms/esp32/Light_PWM.inl`, `Panel_ST7789.hpp` | StickS3 `_set_pwm_backlight(GPIO_NUM_38, 7, 256, false, 16)`; Core `_set_pwm_backlight(GPIO_NUM_32, 7, 44100)`; AXP192 `Light_M5StickC` and `Light_M5StackCore2`; `sleep()` is `setBrightness(0)` then `setSleep(true)`; SLPIN and SLPOUT; 9-bit PWM default clock; `CMD_FRCTR2` commented out for some panel variants | Display, Board |
| M5PM1 | 1.0.6 | Library | `timerSet`, `shutdown`, `btnGetState`, watchdog, GPIO and IRQ registers, `SLP_TO` path | Board, SoC, GPS/IMU |
| esp-nimble-cpp | 2.5.0 | `NimBLEScan.cpp`, `NimBLEScan.h`, `NimBLEClient.cpp`, `NimBLEClient.h` | `setConnectionParams` defaults initiator scan to 16/16 units; `setActiveScan`; scan interval and window conversion; no GATT cache API exposed in 2.5 | BLE |
| TinyGPSPlus | fork `gkoh/TinyGPSPlus#92ac8c21b8179201c2c8c534e46af3320b76d3e7` | Parser | One-byte encode under the GPS mutex; about 30 accessors per snapshot | GPS/IMU |
| Furble WebUI branch | `origin/feat/33d-webui` at `777b4251` | `src/FurbleWebUI.cpp`, `include/FurbleWebUI.h` | Page polls `/api/status` every 3 s; three TLS requests at load; HTTPS server with TCP keepalive off and four SSL sockets; HTTP connection reuse unverified; supervisor every 500 ms | Wi-Fi |
| LVGL | 9.4.0 | Refresh period, partial rendering, `LV_LABEL_LONG_SCROLL_CIRCULAR` | `CONFIG_LV_DEF_REFR_PERIOD=33` about 30 fps; circular scroll animates while the label text overflows | Display |

## 7. Framework and RTOS documents

The build pins ESP-IDF 5.5.3 (`framework-espidf` 3.50503.0). The Wi-Fi lane notes
that some Kconfig help text was read from a locally installed 5.4.2 tree
(3.50402.0) and must be rechecked at the pinned version before a number is
relied on.

| Area | Document or source | Revision | Relevant limit | Cited by |
| --- | --- | --- | --- | --- |
| Power management | `components/esp_pm/Kconfig`; `components/esp_pm/pm_impl.c`; API guide https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-reference/system/power_management.html | 5.5.3, guide v5.5 | `ESP_PM_APB_FREQ_MAX` maps to `PM_MODE_APB_MAX` and forces CPU and APB to 80 MHz; `ESP_PM_NO_LIGHT_SLEEP` maps to `PM_MODE_APB_MIN`; light sleep requires a lower mode; `PM_RTOS_IDLE_OPT` about 20 us per sleep at 160 MHz; `PM_SLP_DISABLE_GPIO` about 200 to 300 uA; `PM_SLP_IRAM_OPT` about 310 us; `PM_POWER_DOWN_CPU_IN_LIGHT_SLEEP` about 650 uA | SoC, Board, Display, Wi-Fi |
| Sleep modes | `components/esp_hw_support/sleep_modes.c`; `components/esp_hw_support/Kconfig`; API guide https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/system/sleep_modes.html | 5.5.3 source; guide v5.4 | `DEFAULT_SLEEP_OUT_OVERHEAD_US = 382` with `PM_SLP_IRAM_OPT`; RTC calibration about 67 us slow plus 127 us fast, about 194 us; flash-ready wait about 700 us; GPIO wake is light sleep only and level triggered; flash power-down cannot be enabled with PSRAM | SoC, GPS/IMU, Board |
| Clock configuration | `components/esp_hw_support/port/esp32s3/rtc_clk.c` | 5.5.3 | Valid S3 CPU frequencies below the 40 MHz XTAL: 40, 20, 13, 10 MHz | SoC |
| FreeRTOS | `components/freertos/Kconfig` | 5.5.3 | Tick rate default 1000 Hz; tickless idle required for automatic light sleep; `IDLE_TIME_BEFORE_SLEEP` default 3 ticks; automatic sleep disabled when tickless idle is off | SoC, Board |
| USB Serial/JTAG | `components/esp_driver_usb_serial_jtag/Kconfig` | 5.5.3 | Module on by default after power-on; disabling it saves its clock and PHY; it cannot work under light sleep; `USJ_NO_AUTO_LS_ON_CONNECTION` holds `ESP_PM_NO_LIGHT_SLEEP` while a host is attached | SoC, Board |
| Console | `components/esp_system/Kconfig` | 5.5.3 | Secondary console selection; USB Serial/JTAG secondary enables the module | SoC |
| Logging | `components/log/Kconfig.level` | 5.5.3 | Default level limits compiled output; maximum level is the highest selectable at runtime | SoC |
| Wi-Fi | `components/esp_wifi/Kconfig`; `esp_wifi_types_generic.h`; `esp_wifi.h` | 5.5.3 | `WIFI_PS_MIN_MODEM` wakes at every DTIM; `WIFI_PS_MAX_MODEM` uses `listen_interval`, default 3 when 0; `ESP_WIFI_SLP_IRAM_OPT` reduces power-save average current; IRAM cost is reported as 1.3 KB in the Wi-Fi lane and 7.3 KB in the SoC lane, unresolved (X-20); `SLP_DEFAULT_MIN_ACTIVE_TIME` 50 ms, `MAX_ACTIVE_TIME` 10 s, `WAIT_BROADCAST_DATA_TIME` 15 ms; `STA_DISCONNECTED_PM_ENABLE`; `esp_wifi_set_max_tx_power` uses 0.25 dBm units, 44 maps to 11 dBm, 52 to 13 dBm; SoftAP beacon 100 TU and DTIM 1 defaults | Wi-Fi, SoC |
| PHY | `components/esp_phy/Kconfig` | 5.5.3 | `ESP_PHY_MAX_WIFI_TX_POWER` 20 dBm; `ESP_PHY_MAC_BB_PD` about 5.3 KB RAM when both radios are enabled; `ESP_PHY_REDUCE_TX_POWER` after brownout; `ESP_PHY_ENABLE_USB` can interfere with Wi-Fi | Wi-Fi, SoC |
| MQTT | `components/mqtt/esp-mqtt/include/mqtt_client.h` | 5.4.2 header in the installed tree | `keepalive` default 120 s, client pings at half; `reconnect_timeout_ms` default 10 s; `timeout_ms` default 10 s | Wi-Fi |
| HTTP server | `components/esp_http_server/include/esp_http_server.h` | 5.4.2 header | `max_open_sockets` default 7; `lru_purge_enable` false; `keep_alive_enable` false (TCP keepalive probes, not HTTP reuse) | Wi-Fi |
| HTTPS server | `components/esp_https_server/include/esp_https_server.h` | 5.4.2 header | `HTTPD_SSL_CONFIG_DEFAULT()` sets TCP keepalive false and `max_open_sockets` 4; about 40 KB RAM per SSL socket; HTTP connection reuse unverified | Wi-Fi |
| BLE host | `components/bt/host/nimble/nimble/nimble/host/include/host/ble_gap.h` | 5.5.3 | `BLE_GAP_INITIAL_CONN_ITVL_MIN` 24 units (30 ms), `_MAX` 40 units (50 ms); `BLE_GAP_INITIAL_SUPERVISION_TIMEOUT` 0x0100 units of 10 ms | BLE, Wi-Fi |
| BLE controller | `components/bt/controller/esp32c3/Kconfig.in` and `esp32/Kconfig.in` | 5.5.3 | `BT_CTRL_MODEM_SLEEP`, `MODE_1`, `LPCLK_SEL_MAIN_XTAL`, `BLE_MAX_ACT` 6, `SCAN_BACKOFF_UPPERLIMITMAX`, duplicate cache 100 and refresh 0, `CE_LENGTH_TYPE`, `LE_PING_EN` | BLE, SoC |
| NimBLE config | `components/bt/host/nimble/port/include/esp_nimble_cfg.h` | 5.5.3 | `MYNEWT_VAL(BLE_EXT_ADV)` is 0 with `EXT_ADV=n`, so the legacy scan path is used | BLE |
| LEDC | `components/esp_driver_ledc/include/driver/ledc.h`; `components/hal/esp32s3/include/hal/ledc_ll.h` | 5.5.3 | RC_FAST is DFS and light-sleep compatible; `LEDC_SLEEP_MODE_KEEP_ALIVE` keeps PWM output during light sleep | Display, SoC, Board |
| SPI master | `components/esp_driver_spi/src/gpspi/spi_master.c` | 5.5.3 | The driver takes its own APB lock during a transaction; whether M5GFX uses this driver for the panel is not verified | Display, SoC |
| UART | API guide https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/uart.html | v5.4 and stable | UART wake loses the triggering character and every character before it; a fixed clock is required during sleep | GPS/IMU |
| GPIO | API guide https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/peripherals/gpio.html | v5.4 | `gpio_wakeup_enable` accepts only low or high level | GPS/IMU |
| RF coexistence | API guide https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-guides/coexist.html | v5.5 | One 2.4 GHz RF module, time-division multiplexed; Wi-Fi CONNECTED plus BLE CONNECTED gets 50/50 slices in a period anchored to TBTT and longer than 100 ms; SCAN and CONNECTING get longer Wi-Fi slices; S3 SoftAP rows against BLE are marked unstable | Wi-Fi, BLE |
| BLE low power | API guide https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/low-power-mode/low-power-mode-ble.html | Stable | 500 ppm sleep clock accuracy and the 136 kHz RC limitation | BLE |
| Coexistence issue | ESP-IDF issue 15833, cited in plan 65 | Not stated | Sub-50 ms BLE intervals lose coexistence arbitration under Wi-Fi traffic | Wi-Fi, BLE |
| NimBLE power save example | https://github.com/espressif/esp-idf/tree/master/examples/bluetooth/nimble/power_save | Master branch, not pinned | ESP32 modem sleep 14.1 mA; ESP32-S3 modem sleep 17.9 mA; ESP32-S3 light sleep from the main crystal 3.3 mA; connection interval not stated | BLE, SoC, Board |
| Wi-Fi low-power figures | Espressif low-power-mode figures quoted in `plans/33-wifi-hub.md:630-634` | Not stated | Station with modem sleep in a shielded box: DTIM1 40.1 mA, DTIM3 38.7 mA, DTIM10 38.2 mA. Not reproduced; clock not stated | Wi-Fi |
| Nordic power profiler validation | https://devzone.nordicsemi.com/nordic/nordic-blog/b/blog/posts/nrf52-online-power-profiler | Not stated | 14.022 uC per BLE advertising event at 0 dBm, 31 byte payload, 3.0 V, nRF52832, Agilent N6705B. A methodology reference only, explicitly not an ESP32 number | BLE, model |

## 8. Instruments cited by the measurement lane

These are instruments, not datasheets. They are listed so a record can name the
exact model and capability.

| Code | Instrument | Capability the plan relies on |
| --- | --- | --- |
| I1 | Keysight N6705C with N6781A, or Joulescope JS220 plus an external regulated supply | 1 uA to 1 A, at least 16 bit, at least 100 kSa/s; 1 MSa/s for events; source mode at 3.7 V and 5 V when the analyzer can source; the JS220 is measure-only |
| I2 | Nordic Power Profiler Kit II or Otii Ace | 1 uA to 1 A, at least 10 kSa/s, long capture |
| I3 | Keysight 34465A or Keithley DMM6500 | 6.5 digits, 4-wire resistance, DC average reference |
| I4 | Oscilloscope with a differential probe | DC to 20 MHz, 1 MSa/s or faster across the shunt |
| I5 | Electronic load with coulomb counter, or a sink-capable source-measure unit | At least 1 Sa/s, programmable cutoff |
| I6 | Calibrated thermocouple logger or thermal camera | +/-0.5 degC, ambient and board contact |
| I7 | Bench SMU or inline USB analyzer with logging | 1 mA resolution, at least 1 kSa/s |
| I8 | Nordic Sniffer, Ellisys, or Teledyne front-line | Connection events, interval, PHY, payload length, retries |
| I9 | Phone camera or bench camera | Screen state, camera link, GPS fix; phone radio off |

## 9. Board, build, and model files in this repository

These are not vendor documents, but they are the board files the power claims
depend on. They belong in the register because a board-file change can change a
power number.

| File | Role | Relevant content |
| --- | --- | --- |
| `boards/waveshare-esp32-s3-eth.json` | Board file | `f_cpu` 240 MHz; 8 MB octal PSRAM. `platformio.ini` overrides `board_build.f_cpu` to 80 MHz for every env; the runtime default is 160 MHz. The three values are unreconciled |
| `platformio.ini` | Build matrix | Six release envs plus headless and debug; `board_build.f_cpu = 80000000L`; the Waveshare release env no longer inherits `-DFURBLE_CONSOLE=1 -DFURBLE_USB_CONSOLE=1` and its debug env adds them back; MQTT compiled into four envs |
| `sdkconfig.m5stick-c` | Board config | ESP32, AXP192, BTDM modem sleep ORIG, tickless idle on, log INFO, watchdog, PM settings |
| `sdkconfig.m5stick-c-plus` | Board config | Plus and Plus2 share the image; AXP192 or no PMIC; tickless idle on |
| `sdkconfig.m5stick-s3` | Board config | ESP32-S3, M5PM1, modem sleep mode 1 on main XTAL, MAC/BB power down, 6 controller activities and 9 host connections, PSRAM block, USB Serial/JTAG secondary, INFO logging, 1000 Hz tick, tickless idle on |
| `sdkconfig.m5stack-core` | Board config | ESP32, IP5306, tickless idle on |
| `sdkconfig.m5stack-core2` | Board config | ESP32, AXP192, tickless idle on |
| `sdkconfig.esp32-s3-headless` | Board config | ESP32-S3 devkit, tickless idle on, console enabled, no PMIC or display |
| `sdkconfig.waveshare-s3-eth` | Board config | ESP32-S3, no battery, tickless idle off, `PM_DFS_INIT_AUTO` off, MQTT symbols unset, MAC_BB_PD unset |
| `sdkconfig.m5stack-core-usb-debug` | Developer config | USB-UART console, single factory partition, PM profiling, LVGL sysmon and perf monitor |
| `sdkconfig.debug` | Debug fragment | PM profiling and run-time stats used by debug builds |
| `partitions_single_factory_4m.csv` | Partition table | Single factory app, used by the Core USB debug profile |
| `partitions_two_ota_8m.csv` | Partition table | Two OTA slots, 8 MB |
| `partitions_two_ota_16m.csv` | Partition table | Two OTA slots, 16 MB |
| `partitions_waveshare_s3_eth.csv` | Partition table | Waveshare layout |
| `tools/power-model/board-currents.yaml` | Model input | Per-board `mcu`, `radio`, `display`, `gps`, `pmic`, `peripherals` entries with `value_ma`, `min`, `max`, `source`, `confidence`, `note`; shared anchors; confidence tags `datasheet`, `published-measurement`, `estimated`, `measured-local` |
| `tools/power-model/README.md` | Model method | How each family was derived; known limitations; numbers that could not be sourced |
| `tools/power-model/compare.py` | Comparator | Two-sided relative threshold, default 0.10; accounting-mode and digest checks; fail-closed on malformed reports |
| `tools/power-model/baseline/*.json` | Baselines | Ten committed relative estimates for the S3, including `screen-off-30s.json` at 0.510703 mA |
| `sim/power_profiler.cpp` | Simulator model | Charges MCU, display, radio, GPS, PMIC, and a fixed 0.0035 mA peripheral term; no scan or advertising state; no brightness input |
| `sim/BleSim.cpp` | Simulator BLE peer | Virtual advertiser at 100 ms, not charged; shutter, focus, and geotag events |
| `sim/scenarios/manifest.json` | Scenario ownership | Suites including `power-gate`; the current power-gate selection has ten scenarios |
| `.github/workflows/power-gate.yml` | CI gate | Runs the certified power-gate scenarios and `compare.py`; plan 98's "not wired" claim is stale |

## 10. Project documents used as evidence

These are repository plans and docs, not external documents. They are listed
because a lane quote is only traceable if the source document and its revision
are named.

| Document | What it supplies | Lanes |
| --- | --- | --- |
| `plans/00-hardware-experiments.md` | Experiment A (no 32 kHz crystal; the 3.3 mA floor is cited by later plans, but its drain row is not in this file); Experiment B (`$PCAS12`, pruning 710 to 91 characters/s, 107.7 s cold re-fix, no backup supply, ANTENNA OPEN) | BLE, SoC, GPS/IMU, Board, Wi-Fi |
| `plans/implemented/01-cpu-freq-setting.md` | CPU frequency setting and its risks | SoC, Board |
| `plans/implemented/06-power-module.md` | Lock design and the no-manual-sleep rule | SoC |
| `plans/implemented/07-ble-sleep.md` | Modem sleep and per-board floors | BLE, SoC |
| `plans/implemented/08-scan-tuning.md` | Scan presets and the 60 s targeted scan rationale | BLE, Board |
| `plans/implemented/09-reconnect-backoff.md` | Backoff bounds | BLE |
| `plans/implemented/10-adaptive-conn-params.md` | Idle profile and hardware verification | BLE, Board |
| `plans/implemented/11-adaptive-tx-power.md` | TX power policy | BLE, Board |
| `plans/implemented/12-display-off.md` | Display states, APB lock, PMIC backlight paths | Display, SoC, Board |
| `plans/implemented/13-auto-off-low-batt.md` | Auto-off and low-battery policy | Board, SoC |
| `plans/implemented/14-gps-pcas.md` | PCAS02/03/04/10 definitions, no PCAS00 | GPS/IMU |
| `plans/implemented/15-gps-power.md` | Burst window, UART wake limitation, standby versus rail cycling, 15 s duty ceiling | GPS/IMU, SoC |
| `plans/17-imu-gestures.md` | Gesture thresholds, the 25 Hz tap tradeoff, and the hardware tuning gate | GPS/IMU |
| `plans/18-gps-motion.md` | Motion detector phase 1, no consumer, 60 s hold, blind spots | GPS/IMU |
| `plans/implemented/19-interval-deep-sleep.md` | Interval deep sleep design and exclusions | SoC, Board |
| `plans/implemented/21-imu-dead-reckoning.md` | Position hold when the fix is lost; defines the 30 s `MAX_AGE_MS` freshness budget | GPS/IMU |
| `plans/implemented/26-pm1-watchdog.md` | PM1 watchdog, feed cadence, `SLP_TO` readback, deep-sleep conflict | SoC, Board |
| `plans/implemented/32-gps-advanced.md` | Autobaud, AID-INI, ephemeris replay, MON-HW, unverified receiver items | GPS/IMU |
| `plans/33-wifi-hub.md` | Wi-Fi hub design; station power figures; no OTA while connected | Wi-Fi |
| `plans/implemented/42-waveshare-eth-node.md` | Waveshare node policy, including the fixed-frequency claim | Board |
| `plans/63-sim-power-analysis.md` | Simulator energy model design | Display, Measurement |
| `plans/65-bt-coexistence.md` | Coexistence recommendations and field data | BLE, Wi-Fi, Board |
| `plans/implemented/71-ui-bug-batch.md` | StickS3 LED handling | Board |
| `plans/implemented/77-battery-saver-profile.md` | Battery Saver bundle | BLE, SoC, Board |
| `plans/95-engineering-lessons.md` | Engineering lessons cited by the Board lane | Board |
| `plans/96-wifi-bt-ble-hardening.md` | BLE and Wi-Fi hardening inventory | BLE, Wi-Fi, Board |
| `plans/98-power-optimization-audit.md` | Audit table and ranked actions | All |
| `plans/implemented/98b-power-followup.md` | Degraded GPS retry and the +37 mA held-lock figure | GPS/IMU, BLE, Board |
| `plans/111-power-recommendations.md` | External measurement protocol and state list | SoC, Display, GPS/IMU, Board |
| `plans/125-esp-optimization-program.md`, `plans/126-esp-optimization-audit.md` | Wider optimization program | SoC |
| `plans/implemented/143-pm1-watchdog-window.md` | M5PM1 45 s watchdog window and the 30 s OTA health deadline | Board, SoC |
| `plans/158-sim-scheduler-parity.md` | Scheduler and hardware parity plan | All |
| `plans/159-camera-peer-certification.md` | Fail-closed virtual-peer evidence model | BLE, Measurement |
| `plans/161-sim-real-control.md` | Production Control, Camera, CameraList, and Scan in the simulator | BLE, Measurement |
| `plans/implemented/285-power-accounting.md` | Opt-in synthetic accounting | Display, Measurement |
| `docs/supported-hardware.md` | Supported GPS units, patch antenna limits, baud defaults | GPS/IMU |
| `docs/settings-and-controls.md` | User-facing settings, defaults, apply timing | GPS/IMU, Board |
| `docs/console-commands.md` | Console surfaces | Measurement, SoC |
| `docs/sim.md` | Simulator behavior | Measurement |
| `CLAUDE.md` | Attached-hardware policy | Board |

## 11. Documents cited but not opened

The lanes are explicit about which documents they did not open. These rows are
not verified for this register.

| Part or area | Gap | Consequence |
| --- | --- | --- |
| AXP192 | No revision string in the fetched copy; cover says 2010 | A register claim must name the copy used, not a revision number. Plan 350 could not verify the register details in its pass either, so the ADC mask bits and the `0x12` output enables rest on the M5Unified and M5GFX writes quoted there |
| W5500 | Datasheet not opened for PHY current; the reset minimum is quoted from version 1.1.0 section 5.5.1 while the linked copy is v1.0.6 | NB-02 and the Waveshare rows cannot quote a current |
| FT6336U | Datasheet not opened; current not in the tree | P-069 uses a placeholder 0.5 to 2 mA range |
| ES8311 and AW8737 | No documents opened; quiescent currents not in the tree | RL-09 assumes a closed speaker is idle, but does not measure it |
| BM8563 | Datasheet cited, not opened; M5Stack Tough schematic and pinmap opened | The Plus2 alarm wake path, the Plus BM8563-to-AXP192 power-key coupling through R401 and C401, and the voltage-low flag at register `0x02` bit 7 are read from the vendor documentation and M5Unified, not from a scope or a schematic in this tree. The Tough alarm wake path relies on BM8563 INT -> AXP192 PWR (pinmap) and BT1 BATTERY_RTC (schematic); still hardware-unverified |
| M5Stack Core / Core2 SD | No card datasheet or slot current | RL-06 says the card's standby current is unmeasured |
| M5StickC and Plus schematics | No PDF, netlist, or resistor value in the tree. Plan 350 could not open them either, and worked from a third-party pass instead | GPIO35's three open-drain sources, the IR LED series resistance, and divider values are unverified. So is the EXTEN-to-Grove-5V routing: `AXP192 0x12 = 0x4D` at boot is a library write, not proof that bit 6 reaches the Grove 5 V pin |
| Plus2 HOLD and PWR wake | Hardware behavior unverified | P-098 is a gate, not a saving |
| ESP-IDF Kconfig at the pinned version | Wi-Fi lane read some help text from 5.4.2 while the build pins 5.5.3 | Recheck any Kconfig number at 5.5.3 before relying on it |
| M5GFX SPI path | Whether M5GFX uses the IDF SPI master driver is not established | The APB lock claim for the panel bus has an unverified assumption |
| Camera advertising intervals | No vendor document states them | Every scan-duty row is conditional on a capture |
| Camera GATT databases | Not documented per vendor | P-055 is gated on a handshake capture |

The W5500 row this table used to carry is closed. Section 4 now cites W5500
Ver 1.1.0 with the reset timing, the register block and the IDD figures, so
NB-02 and the Waveshare rows may quote a current. What is still unverified is
whether the chip figures apply to the assembled board: no Waveshare current
measurement exists, and the flash gate is deferred (see `plans/313`).

## 12. Gaps and register notes

1. **No errata document is published for the cited parts, and none is cited.**
   The Semtech product pages for the SX1261 and the SX1262 contain zero
   occurrences of the word errata and serve no direct datasheet PDF link, which
   is why the byte-identical mirrors in section 1 are cited. The functional
   substitute is the datasheet's own section 15, "Known Limitations", Rev 2.2
   pages 109 to 111: 500 kHz LoRa bandwidth modulation quality (bit 2 of
   `0x0889`), PA clamping under antenna mismatch (`TxClampConfig` bits 4:1 to
   `1111` on the SX1262 after power on or cold start), implicit header mode
   timeout (`0x0902` and `0x0944`), and inverted IQ (bit 2 of `0x0736`). A part
   with a real errata sheet must add the document, revision, and affected limit
   here before any lane cites it.
2. **No application note is cited by any lane.** The ESP-IDF API guides and
   Kconfig help are the closest material, and they are listed in section 7 as
   vendor documentation, not as application notes.
3. **Revision conflicts.** The AXP192 copy has no revision string. The ESP32-S3
   copy is v2.2. The IP5306 copy is V1.01. The M5PM1 copy is V1.9, and two
   byte-different V1.9 builds exist, so a register claim names the build hash.
   The ESP32 copy is v5.3. The ESP32 TRM is v5.9 and the ESP32-S3 TRM v1.8. The
   ST7789V copy is v1.3. The ST7735S copy is v1.1. The MPU-6886 copy is v1.1.
   The W5500 copy is v1.1.0, dated 17DEC2022, with the size and checksum in
   section 4; its history on pages 66 to 67 changes only the RPU minimum, the
   package descriptions and adds the maximum junction temperature TJMAX. The
   v1.0.6 copy named by an earlier revision of this register is superseded and
   its URL now returns HTTP 404, so the register quotes v1.1.0 only. The BMI270
   copy is revision 1.6, March 2026, and the QMI8658A copy is Rev D. Do not
   copy a number from a secondary document into a primary claim without opening
   the primary.
4. **Board files are split.** The repository has one board file
   (`boards/waveshare-esp32-s3-eth.json`). Panel pins, backlight drive, and
   brightness mapping come from M5GFX 0.2.27 and M5Unified 0.2.20. There is no
   Furble display or backlight board file. That is a traceability gap, not a
   missing datasheet.
5. **The model is not a datasheet.** `tools/power-model/board-currents.yaml` is
   machine-readable input with confidence tags. Its `datasheet` tag means the
   cited URL was fetched and the number verified by the model author. It does
   not make the entry a local measurement.
6. **Three values in the register are known to be estimates, not datasheet
   lines:** the AXP192 whole-chip operating quiescent (the model uses 0.2 mA;
   plan 98 says about 2 mA; the Board lane block sum is about 316 uA), the
   ILI9342C operating current (ST7789V analogy), and the `$PCAS12` in-circuit
   standby current (chip standby below 10 uA, module rails stay up, model uses
   0.5 mA with a 10 uA to 2 mA range).
7. **The 3.3 mA StickS3 floor is not in a datasheet and is not in the plan 00
   results table.** It is tagged `measured-local` in the model and repeated by
   plans 98 and 111. Treat its provenance as incomplete until P09 reproduces or
   replaces it.
8. **The 40.1 mA Wi-Fi station figure is a published vendor measurement, not a
   datasheet line.** It is a whole-chip average, not a delta, and the clock is
   not stated.
9. **The W5500 reset gap is closed, its current gap is not.** The v1.1.0 copy in
   section 4 is opened and checksummed, so the 500 us `RSTn` minimum, the 1 ms
   `TPL` release-to-PLL maximum, the 13 mA PHY power-down current and the three
   link currents are quoted from a primary document rather than a secondary
   one. None of them is a local measurement: NB-02 still needs a bench run. The
   reset path itself has an open item, because `src/FurbleEthernet.cpp` pulses
   `RSTn` for 500 us and then talks SPI without waiting the up-to-1 ms `TPL`,
   and the ESP-IDF PHY adds no wait of its own once `reset_gpio_num` is -1.
10. **The display and touch documents may not be redistributed.** Sitronix and
   ILITEK print the same sentence in every page footer: the document "shall not be
   distributed, reproduced, or disclosed in whole or in part without prior
   written permission" of the vendor. Chipone's Important Notice on page 226 of
   the CO5300 datasheet and Hynitron's declaration on page 10 of the CST9217
   datasheet grant no redistribution right either. This register therefore
   records the URL, the revision, the page count and the hash, and never a copy,
   and no vendor PDF is committed to this repository.
11. **A register value taken from a driver is labelled as such.** The Linux
   `hynitron_cstxxx.c` driver is GPL-2.0-only and the ESPHome CST92xx component
   is GPLv3, so neither can be consulted for this MIT tree. The only driver
   source used for touch registers is the MIT-licensed Rust `cst92xx` crate,
   `src/registers.rs` at commit `0e4cede3`, which records that it was itself
   ported from the MIT SensorLib. The CST9217 datasheet carries no register map,
   so every register address in this corpus is code-derived and is attributed to
   that crate.
12. **There is no vendor copy of the SH1106 datasheet and there will not be
   one.** `sino-wealth.com` returns NXDOMAIN and its Wayback captures are HTML
   only. The live `sinowealth.com` is a Chinese MCU vendor site. Its Wayback
   set holds 106 PDF URLs, all MCU part documents, and none of them an SH1106.
   The two usable copies are the Pololu mirror and the Crystalfontz OCR copy,
   both V2.3.
   `buydisplay.com/download/ic/SH1106.pdf` answers 403 to an automated fetch.
13. **A vendor prefix names a vendor.** Sitronix parts start with ST, so
   ST7735S, ST7789V, ST7789P3, ST7796S and ST7796U are Sitronix. Sino Wealth
   parts start with SH, so SH1106 is Sino Wealth. "Sino Wealth ST7796S"
   conflates two vendors. Sitronix's own AIoT DDI table lists ST7796U, ST7796UI,
   ST7796HU and ST7796U2 and does not list ST7796S.

## 13. Per-board component and document coverage

Workstream H asks for every MCU, PMIC, charger, gauge, battery, GPS, IMU,
display, radio, board, FCC entry, manual, and Bluetooth Core document. This
matrix maps each board to its parts and states the document status. A row is
`documented` when a primary document with a revision is in sections 1 to 7,
`vendor page` when only a product page exists, and `unknown` when no document
was opened. Every `unknown` names the artifact that closes it.

### 13.1 Boards

| Board | MCU and radio | PMIC, charger, gauge | Display and touch | IMU | Audio or other | Battery | FCC | Status |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `m5stick-c` | ESP32 v5.3 | AXP192, no revision string | ST7735S v1.1, read 2026-11-26 | MPU6886 v1.1 | IR LED | 95 mAh, vendor page | 2AN3WM5STICKC, grant 2019-06-04 | documented, battery gap; board FCC ID found |
| `m5stick-c-plus` | ESP32 v5.3 | Plus: AXP192. Plus2: no PMIC, external charger | ST7789V v1.3, read 2026-11-26 | MPU6886 v1.1 | BM8563 RTC cited, not opened (Plus2) | Plus 120 mAh, Plus2 200 mAh, vendor page | 2AN3WM5STICKCPLUS (product code M5STICKCPLUS, device name "M5StickC-Plus SE"), grant 2026-01-23 | documented, RTC and battery gaps; Plus board FCC ID found, no Plus2 ID found |
| `m5stick-s3` | ESP32-S3 v2.2, PICO-1 N8R8 v1.2 | M5PM1 V1.9 | ST7789P3 v0.1 2022/03, 276 pages, sha256 26e62a24, read 2026-11-26 | BMI270 revision 1.6 | ES8311 and AW8737, vendor page only | 250 mAh, vendor page | 2AN3WM5STICKS3, K150 grant 12/16/2025, read from a page render (OCR-derived) | documented, audio and battery gaps; panel and FCC closed |
| `m5stack-core` | ESP32 v5.3 | IP5306 V1.01 and register document V1.21 | ILI9342C V101, 235 pages, complete, sha256 604a35c0, read 2026-11-26 | Gray variant part not recorded | SD, speaker | 110 mAh at 3.7 V, vendor page (2020.3) | no board-level filing found; module grant only | documented, IMU, SD and battery gaps, no board FCC ID |
| `m5stack-core2` | ESP32 v5.3 | AXP192, no revision string. Core2 v1.1 fits an AXP2101 under the same board ID, and the boot code gates the AXP192 ADC trim on the detected PMIC; no AXP2101 document is in the tree | ILI9342C V101, 235 pages, complete, sha256 604a35c0, read 2026-11-26, plus FT6336U, still not opened | MPU6886 v1.1 | NS4168, BM8563, SD | 500 mAh at 3.7 V, vendor page (2023.10) | Core2 2AN3WM5STACK-CORE2, K010 grant 08/20/2020; Tough 2AN3W-M5STACK-TOUGH, K034 grant 04/19/2021 | documented for AXP192; AXP2101 datasheet is an open gap; FCC closed |
| `waveshare-s3-eth` | ESP32-S3 v2.2, W5500 Ver 1.1.0 | none | none | none | microSD, WS2812 | none | no board-level filing; the product page contains no occurrence of FCC, so it relies on the module grant 2AC7Z-ESPS3WROOM1 | documented; W5500 and module FCC closed, no board FCC ID |
| `esp32-s3-headless` | ESP32-S3 v2.2 | board-specific, no PMIC document | none | none | none | board-specific | module grant 2AC7Z-ESPS3WROOM1 applies to the WROOM-1 module, not to a devkit assembly | documented for the SoC, board document missing |
| `m5stack-core-usb-debug` | ESP32 v5.3 | IP5306 V1.01 | ILI9342C V101, 235 pages, complete, sha256 604a35c0, read 2026-11-26 | as Core | as Core | as Core | as Core, no board-level filing found | same gaps as Core |

### 13.2 Batteries

| Board | Capacity | Document | Closing artifact |
| --- | ---: | --- | --- |
| StickC | 95 mAh | Vendor page | Cell datasheet or a measured discharge |
| Plus | 120 mAh | Vendor page | Cell datasheet or a measured discharge |
| Plus2 | 200 mAh | Vendor page | Cell datasheet or a measured discharge |
| StickS3 | 250 mAh | Vendor page | Cell datasheet or a measured discharge |
| Core2 | 500 mAh | Vendor page (changed from 390 mAh in 2023.10) | Cell datasheet or a measured discharge |
| Core | 110 mAh | Vendor page | Cell datasheet or a measured discharge |
| Tough | 390 mAh | M5Tough README | Cell datasheet or a measured discharge |

No cell datasheet is in the tree, so the discharge plan uses the vendor
capacity only. Every runtime row in `measurement-plan.md` is therefore an
arithmetic estimate on a nominal capacity, not a measured runtime.

### 13.3 FCC, manuals, errata, and Bluetooth Core

| Item | Status | Evidence or closing artifact |
| --- | --- | --- |
| FCC ID for the ESP32-S3-WROOM-1 module | 2AC7Z-ESPS3WROOM1, grant 01/21/2022, Nemko North America, single modular | Espressif certification PDF, https://documentation.espressif.com/ESP32-S3-WROOM-1%20FCC%20Certification.pdf, SHA-256 `edb08720467ab0b1bff5269dec375847836179f60c038a7f977e1127aeeb7908`, text layer, read 2026-11-25 |
| FCC IDs for the assembled M5Stack boards | StickS3 2AN3WM5STICKS3 (K150, grant 12/16/2025); Core2 2AN3WM5STACK-CORE2 (K010, grant 08/20/2020); Tough 2AN3W-M5STACK-TOUGH (K034, grant 04/19/2021) | Vendor-hosted grants under https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/certification/, read 2026-11-25. The StickS3 grant has no text layer, so its ID was read from a 300 dpi page render and is OCR-derived; the other two were read from their text layers. No board-level filing was found for the Plus2 or the Core Basic. The StickC and the StickC Plus do have one: 2AN3WM5STICKC (grant 2019-06-04) and 2AN3WM5STICKCPLUS (grant 2026-01-23), both read from fccid.io, which mirrors the FCC database, on 2026-11-26. This row said none was found until the retrospective review checked the ID database rather than only the vendor-hosted certification files |
| FCC ID for the assembled Waveshare board | none found; certified through the module grant | The ESP32-S3-ETH product page, https://docs.waveshare.com/ESP32-S3-ETH, SHA-256 `00fd70c1b2406485a4636b8f046084311544a166fffa1769783c8fafa034cf0d`, contains zero occurrences of FCC, so no board-level filing is claimed by the vendor. Silence on a product page is not evidence that no filing exists: the ID-database search endpoint returned HTTP 403 to the research agent, so this row is a not-found, not a negative |
| FCC exhibit databases | search inaccessible, per-ID pages readable | The fccid.io and fcc.report search endpoints returned HTTP 403 to the research agent, so no claim is made about schematics or internal photos in any of these filings. A per-FCC-ID page is served, which is how 2AN3WM5STICKC and 2AN3WM5STICKCPLUS were read on 2026-11-26 |
| FCC ID for the ESP32-WROOM-32 module | unknown | No Espressif or M5Stack grant for a WROOM-32 module on a supported board was located. Close from the module label or a vendor-hosted grant |
| Vendor board manuals | vendor page | Product pages and wikis in sections 5 and 7. No PDF manual for the M5Stack boards is in the tree |
| Vendor camera manuals | out of this register | They live in the private corpus `docs/research/model-manuals-*.md` |
| Bluetooth Core Specification, ESP32 | cited, not opened | ESP32 v5.3 datasheet states Bluetooth 4.2. Cite Core v4.2 for any LE limit. No lane quotes a numeric limit from the Core spec |
| Bluetooth Core Specification, ESP32-S3 | cited, not opened | ESP32-S3 v2.2 datasheet states Bluetooth LE 5.0. Cite Core v5.0, Vol 6 for the LE Controller. The power rows use only 1M PHY and the default initiator scan, so no Core-only number is needed |
| Errata | none published for the cited parts | The Semtech product pages for the SX1261 and SX1262 contain zero occurrences of the word errata, and the datasheet carries no errata reference. Section 15 "Known Limitations" of DS.SX1261-2.W.APP Rev 2.2, pages 109 to 111, is the functional substitute; see section 12 item 1 |
| Application notes | two vendor pages cited as application notes | The M5StickS3 low-power page and the M5PaperMono M5PM1 and M5IOE1 page are listed in section 5 and state the idle-sleep wake rule and the multi-level rail structure. ESP-IDF API guides and Kconfig help remain vendor documentation in section 7 |

### 13.4 Coverage verdict

- Every board has a documented SoC, PMIC where fitted, display where fitted,
  and IMU where the part is recorded.
- The open document gaps are the Core Basic IMU part, the StickS3 panel, the
  touch controller current, the RTC, the audio parts, the SD slot, the
  ESP32-WROOM-32 module FCC ID, and the cell datasheets. Each is listed with its
  closing artifact. The assembled-board FCC gaps for the StickS3, Core2, Tough,
  StickC and StickC Plus are closed. The Waveshare board is recorded as having
  no board-level filing found, which is a product-page negative: the ID-database
  search was blocked, so the row says unverified rather than none.
- No gap blocks a safe-now change. Every gap blocks only a number that the
  bench plan already marks as measurement-gated.
- No errata or application note is invented to fill a row.
