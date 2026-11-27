# 378: Vendor document verification for the SoC, radio and board docs

State: documentation only. No firmware, settings, partition or build change, so
no simulator or flash gate applies. Every page cited below was opened and read
in the local copy on 2026-11-25 (UTC), after the earlier corpus left the items
unverified.

## Why

The power research corpus (`docs/power/`) names three ESP32 and ESP32-S3 items
as unverified, has no revision or page for the SX1262 timing that
`plans/implemented/322-hardware-abstraction.md` turns into a `sleep()` sequence, records the
W5500 datasheet as "not opened" (`datasheet-register.md` section 11), and lists
every FCC ID as unknown (`datasheet-register.md` section 13.3). A doc claim
without a document, revision and page is not evidence, and two of the claims
that do carry a limit are wrong: `docs/power/soc.md` says the GPS UART has "no
APB dependency" on both SoCs, and `plans/322` settles an SX1262 reset with an
invented 500 us. This plan records what the vendor documents actually say, so
the affected docs can cite them and stop repeating the two errors.

## Method

- Read each document page by page from a local copy, with the PDF page number
  checked against the printed page number on the same page. Where the two
  differ the printed page is quoted and the offset is stated.
- No vendor PDF is committed to the repository. This plan records the
  revision, the URL, the SHA-256 of the exact copy read, and the page.
- The Semtech product-page link for the SX1261/2 datasheet is form and login
  gated, so the two byte-identical mirrors below are the citable copies.
- Anything that could not be settled from a document is listed under "What
  remains unverified" instead of being written into a doc as fact.

## Documents opened

All hashes are SHA-256 of the whole file, read 2026-11-25.

| Document | Revision | URL | SHA-256 | Bytes |
| --- | --- | --- | --- | ---: |
| ESP32 Technical Reference Manual | v5.9 | https://documentation.espressif.com/esp32_technical_reference_manual_en.pdf | `1007b90e6fafc16541d693d7444b63e1c5b01e46e60cce9b6810e02340a64b82` | 10,193,172 |
| ESP32-S3 Technical Reference Manual | v1.8 | https://documentation.espressif.com/esp32-s3_technical_reference_manual_en.pdf | `4484bf8a69035ec42a731c58c64ada6fbd1f1618c5559409f134d9ea083f444f` | 15,215,232 |
| ESP32-S3 Series Datasheet | v2.2 | https://documentation.espressif.com/esp32-s3_datasheet_en.pdf | `2d5a7cb7fd559d8d972bd88db32669c0196d23f22d7afaafb0f63d099b589a3f` | 1,098,115 |
| ESP32-S3-PICO-1 Series Datasheet | v1.2 | https://documentation.espressif.com/esp32-s3-pico-1_datasheet_en.pdf | `44daca9121111a9c7df437e18c3c153258a20e68fe57fcd3bd9be593ad162979` | 774,379 |
| SX1261/2 datasheet, DS.SX1261-2.W.APP, ECO 067443 | Rev 2.2, Dec 2024 | https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1198/DS_SX1261_2_V2-2.pdf | `6d783125dbef567954ce53095ad068d0950b2a8de52f95bb2cfde4146c4306c2` | 4,459,535 |
| Same document, second mirror | Rev 2.2, Dec 2024 | https://wiki.lilygo.cc/datasheet/DS_SX1261_SX1262.pdf | `6d783125dbef567954ce53095ad068d0950b2a8de52f95bb2cfde4146c4306c2` | 4,459,535 |
| W5500 datasheet | Ver 1.1.0, 17DEC2022 | https://docs.wiznet.io/img/products/w5500/W5500_ds_v110e.pdf | `7b826b808084ccd986bcc22904c00a07a508ef42fb93d079fe7150a4c4f1a63d` | 1,893,745 |
| M5PM1 Chip User Manual | V1.9 | https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1207/M5PM1_Datasheet_EN.pdf | `c6daffd0ab89d8de50c0a19f35f94321bc6c9511ad017dd242a3e765d88a96b6` | 814,731 |
| Same manual, second build | V1.9 | https://raw.githubusercontent.com/m5stack/M5PM1/main/docs/M5PM1_Datasheet_EN.pdf | `f008794c8a6b672b61452c77a9359b6332d82194dce92f9a21494e283783b656` | 828,393 |
| M5StickS3 low-power page | Web page, no revision | https://docs.m5stack.com/en/arduino/m5sticks3/m5pm1 | `f0a92685260dfc3af9dcdcb3ea334eaecf56d05a2623842d8a0315a7aec59524` | 401,739 |
| M5PaperMono M5PM1 and M5IOE1 page | Web page, no revision | https://docs.m5stack.com/en/arduino/papermono/m5pm1_m5ioe1 | `2bd391ac1a4b48444ae1f2945406ab36c354f1c056646c59c76993769ad40e4c` | 444,586 |
| M5StickS3 FCC grant, K150 | Grant 12/16/2025 | https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/certification/K150/FCC/FCC-ID.pdf | `c1913a67cae52e628b56fc9695dcf98cb1bde34b65b310cea6f4cba2eff62690` | 179,374 |
| M5Stack Core2 FCC grant, K010 | Grant 08/20/2020 | https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/certification/K010/FCC/FCC-ID.pdf | `c0ac55d3e4b889b07d90d808404ea65baab05716ffe6501301a07fa7e5a09b3a` | 164,342 |
| M5Stack Tough FCC grant, K034 | Grant 04/19/2021 | https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/certification/K034/FCC/FCC-ID.pdf | `6cc7dbe3e39c96855b280b2553cab3b020f016c60fd66d7288d9ea6deeecc8a2` | 164,273 |
| ESP32-S3-WROOM-1 FCC certification | Grant 01/21/2022 | https://documentation.espressif.com/ESP32-S3-WROOM-1%20FCC%20Certification.pdf | `edb08720467ab0b1bff5269dec375847836179f60c038a7f977e1127aeeb7908` | 352,224 |
| Waveshare ESP32-S3-ETH product page | Web page, no revision | https://docs.waveshare.com/ESP32-S3-ETH | `00fd70c1b2406485a4636b8f046084311544a166fffa1769783c8fafa034cf0d` | 63,725 |
| Semtech SX1261 product page | Web page, no revision | https://www.semtech.com/products/wireless-rf/lora-connect/sx1261 | `93316c3dd09341470344235f806bb7445bc7a28e684815a0e6486bc02fff169f` | 245,082 |
| Semtech SX1262 product page | Web page, no revision | https://www.semtech.com/products/wireless-rf/lora-connect/sx1262 | `9e48a456fffe278de7327d2d46bd544382422a271cdd4072399993bb1d9af7f1` | 244,606 |

The M5PM1 manual exists as two byte-different V1.9 builds. Page numbering and
register text agree on every page quoted here, so a citation names V1.9 and the
build hash rather than a single file.

## 1. ESP32 and ESP32-S3 UART clock tree

| Fact | Document, revision | Section and page | What the page says | Where it lands |
| --- | --- | --- | --- | --- |
| ESP32 UART has two clock sources | ESP32 TRM v5.9 | 19.3.2, page 312 | "The UART block can derive its clock from two sources: the 80-MHz APB_CLK, or the reference clock REF_TICK ... selected by configuring UART_TICK_REF_ALWAYS_ON" | `soc.md` peripheral clock table, `soc.md` checked list, `CLAUDE.md` DFS bullet |
| ESP32 selector values | ESP32 TRM v5.9 | `UART_TICK_REF_ALWAYS_ON` register field | "1: APB clock; 0: REF_TICK" | same |
| REF_TICK is derived from APB_CLK | ESP32 TRM v5.9 | 7.2.4.2 and Table 7.2-5, page 169 | "REF_TICK is derived from APB_CLK ... When CPU_CLK source changes, users need to make sure the REF_TICK frequency remains unchanged by setting a correct divider value", with one divider per CPU source (`SYSCON_PLL_TICK_NUM`, `_APLL_`, `_XTAL_`, `_CK8M_`) | `soc.md`, `CLAUDE.md` |
| S3 UART has three clock sources | ESP32-S3 TRM v1.8 | 26.3 (continued on page 926) and 26.4.1 Clock and Reset, page 926 | "The UART Core has three clock sources: 80 MHz APB_CLK, RC_FAST_CLK and external crystal clock XTAL_CLK ... selected by configuring UART_SCLK_SEL"; fractional divider, divisor 1 to 256 | `soc.md`, `CLAUDE.md` |
| S3 selector values | ESP32-S3 TRM v1.8 | `UART_SCLK_SEL` register field, page 961 | "1: APB_CLK; 2: RC_FAST_CLK; 3: XTAL_CLK" | same |

Consequence recorded in the docs: on the ESP32, `UART_SCLK_REF_TICK` is not an
escape from the APB clock family, because REF_TICK is derived from APB_CLK and
its divider has to be reprogrammed on every CPU clock source change. On the
ESP32-S3 the XTAL source is a genuinely independent third source, so
`UART_SCLK_XTAL` is the DFS-safe choice there. The previous "no APB dependency"
wording was right for the S3 and wrong for the ESP32.

Page correction against the brief: there is no ESP32-S3 section 26.3.2. Section
26.3 opens on page 925 with two figures and no text, and the three-source
sentence is on page 926, inside 26.3 and repeated in 26.4.1.

## 2. RMT memory

| Fact | Document, revision | Section and page | What the page says | Where it lands |
| --- | --- | --- | --- | --- |
| ESP32 channel and RAM shape | ESP32 TRM v5.9 | 30.2.1 RMT Architecture, page 728 | "eight channels. Each channel has both a transmitter and a receiver, but only one of them can be active in every channel. The eight channels share a 512 x 32-bit RAM block" | `soc.md` RMT rows, `plans/implemented/22-ir-remote-trigger.md` |
| ESP32 block per channel | ESP32 TRM v5.9 | 30.2.2 RMT RAM, page 728 | "Normally, only one block of 64 x 32-bit worth of data can be sent or received ... The RAM block is divided into eight 64 x 32-bit blocks. By default, each channel uses one block" | same |
| ESP32 RMT clock | ESP32 TRM v5.9 | 30.2.1, page 728 | "Each channel is clocked by a divided-down signal derived from either the APB bus clock or REF_TICK" | `soc.md` |
| S3 channel and RAM shape | ESP32-S3 TRM v1.8 | 37.2 Features, page 1415 | "Four TX channels", "Four RX channels", "Eight channels share a 384 x 32-bit RAM"; 37.1 on the same page names channels 0 to 3 as TX and 4 to 7 as RX | `soc.md`, `plans/implemented/22-ir-remote-trigger.md` |
| S3 block per channel | ESP32-S3 TRM v1.8 | 37.3.2.2 Use of RAM, page 1417 | "The RAM is divided into eight 48 x 32-bit blocks. By default, each channel uses one block" | same |

Page correction against the brief: 37.3.2 opens on page 1416 with a heading and
no body text; the eight-block split is in 37.3.2.2 on page 1417.

Consequence: `SOC_RMT_MEM_WORDS_PER_CHANNEL` is 64 words on the ESP32 and 48 on
the S3 for the reason in Table 30.2-2 and 37.3.2.2, so plan 22's constant is
correct but was uncited, and plan 22's "the S3 has 4 TX channels" now states
that the S3 has four dedicated TX and four dedicated RX channels while the
ESP32 has eight channels that are individually either TX or RX.

## 3. USB Serial/JTAG reset behaviour

| Fact | Document, revision | Section and page | What the page says | Where it lands |
| --- | --- | --- | --- | --- |
| RTS 1, DTR 0 resets the SoC | ESP32-S3 TRM v1.8 | 33.3.2, Table 33.3-2, page 1248 | `1 0` action "Reset ESP32-S3"; `1 1` "No action"; `0 1` set download flag; `0 0` clear it | `CLAUDE.md` USB bullet, `docs/sim-coverage.md` |
| SRST resets the digital system only | ESP32-S3 TRM v1.8 | 33.3.5 JTAG Command Processor, page 1249 | "SRST is connected to the reset logic of the digital circuitry in the SoC and a high level on this line will cause a digital system reset. Note that the USB Serial/JTAG Controller itself is not affected by SRST" | same |
| Clock requirement | ESP32-S3 TRM v1.8 | 33.4.2 Runtime Operation, page 1253 | USB device depends on the PLL for the 48 MHz PHY clock and on APB_CLK, "an APB_CLK of 40 MHz or more is required for proper USB compliant operation", the clock is gated in light sleep and the controller and CPUs are powered down in deep sleep | `soc.md`, `CLAUDE.md` |
| Host-side sequences | ESP32-S3 TRM v1.8 | Tables 33.4-3 and 33.4-4, page 1254 | Tables 33.4-3 (reset into download mode) and 33.4-4 (reset into booting from flash) | `docs/sim-coverage.md` |

This is why the port survives a reset: the controller is on the other side of
SRST, so the host keeps the COM port across the reset the RTS and DTR sequence
triggers.

## 4. Octal versus quad PSRAM, and GPIO33 to GPIO37

| Fact | Document, revision | Section and page | What the page says | Where it lands |
| --- | --- | --- | --- | --- |
| Octal versus quad parts | ESP32-S3 Series Datasheet v2.2 | Table 1-1, page 13 | S3R8 8 MB Octal SPI PSRAM, S3R16V 16 MB Octal, S3RH2 2 MB Quad, FN8 8 MB Quad flash, FH4R2 4 MB Quad flash plus 2 MB Quad PSRAM | `board-power.md` inventory, `datasheet-register.md` |
| Pin consequence | ESP32-S3 Series Datasheet v2.2 | 2.3.5, page 26 | GPIO33 to GPIO37 are "the higher 4 bits data line interface and DQS interface for the SPI0/1 interface in 8-line SPI mode, and can be GPIO pins if the chip is not connected to flash or PSRAM in 8-line SPI mode" | `board-power.md` |
| Pin table and notice | ESP32-S3 Series Datasheet v2.2 | 2.6 and Table 2-14, page 31 | DQ4 to DQ7 and DQS/DM on GPIO33 to GPIO37 appear only in the Octal SPI/OPI column; "Notice: Do not use the pins connected to in-package flash/PSRAM for any other purposes"; SPICS1 is pin 28 | `board-power.md` |
| PICO-1 variants | ESP32-S3-PICO-1 Series Datasheet v1.2 | Table 1-1, page 9 | N8R2 is 8 MB Quad flash plus 2 MB Quad PSRAM; N8R8 is 8 MB Quad flash plus 8 MB Octal PSRAM | `board-power.md` |
| PICO-1 pin loss | ESP32-S3-PICO-1 Series Datasheet v1.2 | pin table note 4, page 13 | "For ESP32-S3-PICO-1-N8R2, SPICS1 is connected to the Quad SPI PSRAM and is not available for other uses. For ESP32-S3-PICO-1-N8R8, SPICS1 and GPIO33 to GPIO37 are connected to the Octal SPI PSRAM and are not available for other uses" | `board-power.md` |
| Waveshare part | Waveshare ESP32-S3-ETH page | product overview | "Based on the high-performance ESP32-S3R8 chip" | `board-power.md` |

So the StickS3's N8R8 does lose GPIO33 to GPIO37 and SPICS1, and the Waveshare
board's S3R8 is octal PSRAM, matching the inventory rows that already named
those parts. Nothing in either image uses those pins: the IR pins are GPIO46
and GPIO42, the W5500 sits on GPIO9 to GPIO14.

## 5. SX1261/2

All page numbers are printed pages of the 118 page Rev 2.2 PDF.

| Fact | Section and page | What the page says | Where it lands |
| --- | --- | --- | --- |
| Reset pulse | 8.1, page 53 | NRESET pin 15, "The pin should be held low for typically 100 us for the Reset to happen", followed by the standard calibration, context lost | `plans/322` |
| BUSY handshake | 8.3.1, page 55 | BUSY low means ready; "The max value for TSW from NSS rising edge to the BUSY rising edge is, in all cases, 600ns"; "In Sleep mode, the BUSY pin is held high through a 20kOhm resistor" | `plans/322` |
| Switching times | Table 8-2, page 56 | SLEEP to STBY_RC cold 3500 us, warm 340 us; STBY_RC to STBY_XOSC 31 us, to FS 50, to RX 83, to TX 126; STBY_XOSC to FS 40, to TX 105, to RX 62; FS to RX 41, to TX 76; RX to FS 15, to TX 92. Switching time is measured from the NSS rising edge that ends the SPI transaction to the falling edge of BUSY | `plans/322` |
| SPI wake delay | Table 8-1 t10 and 8.2.2, page 54 | t10 "NSS falling edge to SCK setup time when switching from SLEEP to STDBY_RC mode" minimum 100 us; 8.2.2 explains the regulator and initialization sequence | `plans/322` |
| Known limitations | 15.1 to 15.4, pages 109 to 111 | 500 kHz LoRa bandwidth modulation quality (bit 2 of `0x0889`), PA clamping under antenna mismatch (`TxClampConfig` bits 4:1 to `1111` on the SX1262 after power on or cold start), implicit header mode timeout (`0x0902` and `0x0944`), inverted IQ (bit 2 of `0x0736`) | `datasheet-register.md` |
| No errata document | none | The Semtech product pages fetched cleanly (HTTP 200) and contain zero occurrences of the word errata, and serve no direct PDF link; section 15 is the functional substitute | `datasheet-register.md` |

Page correction against the brief: section 15 runs to page 111, not page 110.
15.1 and 15.2 are on pages 109 and 110, and 15.3 and 15.4 are on page 111.

`plans/322`'s "500 us settle" looked unsourced. Checking the page instead of
the section reference showed that it is the datasheet's own window: after
SetSleep the device is unresponsive for around 500 us while it saves the
configuration, and the host must send no SPI command during it (13.1.1, p. 72).
The plan now keeps the number, cites it, and adds the rest of the table: the
100 us NRESET pulse, the 600 ns BUSY assertion, the 340 us warm and 3500 us
cold start, and the 100 us NSS-to-SCK delay when leaving sleep. `lib/hal`
already carried those citations, so the code needed no change; only the plan
text did.

## 6. W5500

| Fact | Section and page | What the page says | Where it lands |
| --- | --- | --- | --- |
| Reset pulse | 1.1 pin description, pin 37, page 10 | "RESET should be held low at least 500 us for W5500 reset" | `datasheet-register.md`, `docs/sim-coverage.md`, NB-02 |
| Reset timing | 5.5.1 and Figure 22, page 60 | TRC Reset Cycle Time minimum 500 us; TPL "RSTn to internal PLOCK (PLL Lock)" maximum 1 ms | same |
| Wake up | 5.5.2, page 60 | "Voltage Regulator Wake up Time: 10us" | same |
| Register block | 3.1 Common Register Block, page 29 | Table 3 offset map, SHAR0 at `0x0009` | `datasheet-register.md` |
| Common registers | 4.1, page 32 | Common register descriptions begin with `MR` at `0x0000` | same |
| MAC address registers | SHAR, page 33 | "SHAR (Source Hardware Address Register) [R/W] [0x0009 - 0x000E]" | same |
| Supply current | 5.3 DC Characteristics, page 59 | IDD1 normal operation 132 mA; IDD2 PHY power down 13 mA; RPU 50/77/112 kOhm on SCSn, RSTn and PMODE | same |
| Link currents | 5.4 Power Dissipation, page 60 | 100M link 128 mA, 10M link 75 mA, un-link 65 mA, 100M transmitting 132 mA, 10M transmitting 79 mA, power down 13 mA | same |
| Revision history | pages 66 and 67 | v1.1.0 17DEC2022 changed only "Modify Pull-Up Resister Min Value RPU (P.59)" and "Updated latest Package Descriptions (P.64)" | same |

Consequence: the reset figures are unchanged since v1.0.6, because v1.1.0
touched the pull-up minimum and the package text only. The corpus link to
`W5500_ds_v1.0.6.pdf` is confirmed dead (404); 1.1.0 replaces it as the cited
revision.

## 7. M5PM1

Printed page = PDF page minus 2 in both V1.9 builds.

| Fact | Section and page | What the page says | Where it lands |
| --- | --- | --- | --- |
| I2C idle sleep | section 6 I2C Idle Sleep, page 27 | "Configure SLEEP[3:0] to set the idle sleep time; 0 = Disable sleep", followed by the sleep procedure bullet list | `board-power.md` |
| Idle sleep invalid cases | page 28 | "When the PWM function is enabled, I2C idle sleep is invalid; when entering Download mode, I2C idle sleep is invalid" | `board-power.md` |
| First transaction after wake | `I2C_CFG` note 2, page 15 | "the first I2C transaction is used only for wake-up and will fail. Subsequent communications will operate normally", and a 300 ms rule for an incomplete address | `board-power.md`, `CLAUDE.md` |
| System command | `SYS_CMD` `0x0C`, page 15 | `[7:4]` KEY `0xA`, `[1:0]` CMD `01` power off, `10` restart, `11` Download; write is KEY plus CMD | `board-power.md`, `datasheet-register.md` |
| IRQ status and masks | section 8 IRQ Handling, page 28, and the register map, page 9 | IRQ Status 1/2/3 at `0x40`/`0x41`/`0x42`; the IRQ line is released only when all three are cleared; masks at `0x43`/`0x44`/`0x45`, a set bit masks the interrupt | `board-power.md`, `datasheet-register.md` |
| Wake source coupling | register map page 9, and section 8 detail | "WAKE_SRC (0x2F) is related to bit1 of IRQ Status 3. That is, if WAKE_SRC (0x2F) is not cleared, IRQ Status 3 bit1 will remain 1" | `board-power.md` |
| Board rails | PaperMono page | "The power inputs for the L1 ~ L3B switches all originate from the L0 (SYS_VBUS) source rather than from the preceding power level"; "After the M5PM1 powers on, L1, L2, and L3A are enabled automatically" | `board-power.md` |
| Wake retry requirement | M5StickS3 low-power page | "After entering sleep mode, the first communication between ESP32-S3 and M5PM1 will be used to wake up M5PM1. Since this communication will fail, valid communication will occur on the next transaction after wake-up" | `board-power.md`, `CLAUDE.md` |

Internal contradiction, recorded rather than resolved: the manual gives
WAKE_SRC as `0x05` in Table 3 on page 5 and in the register detail on page 13,
as `0x04` in the section 8 note on page 29, and as `0x2F` in the IRQ Status 3
note on page 9 and in the IRQ Status detail. Table 3 also lists `0x2B` to
`0x2F` as reserved. The docs quote the `0x2F` text as the vendor writes it and
say the address is inconsistent inside the manual, so no code or board claim
depends on which of the three is correct.

## 8. FCC

| Board or module | FCC ID | Grant | Source | How it was read |
| --- | --- | --- | --- | --- |
| M5StickS3 (K150) | 2AN3WM5STICKS3 | 12/16/2025 | vendor-hosted grant, K150 | OCR-derived: the PDF has no text layer, so the ID and the date were read from a 300 dpi page render |
| M5Stack Core2 (K010) | 2AN3WM5STACK-CORE2 | 08/20/2020 | vendor-hosted grant, K010 | text layer, read directly |
| M5Stack Tough (K034) | 2AN3W-M5STACK-TOUGH | 04/19/2021 | vendor-hosted grant, K034 | text layer, read directly |
| ESP32-S3-WROOM-1 | 2AC7Z-ESPS3WROOM1 | 01/21/2022 | Espressif certification PDF | text layer, plus grantee and module class |
| Waveshare ESP32-S3-ETH | none found | n/a | Waveshare product page | the page contains zero occurrences of "FCC", so the vendor claims no board-level filing and the board is certified through the module grant. A page-level silence is not a database search: the ID search endpoint returned HTTP 403, so the row is a not-found, not a negative. See the 2026-11-26 correction below. |

The grantee for the three M5Stack grants is M5Stack Technology Co.,Ltd, and all
three are Part 15 spread spectrum or digital transmission system grants. The
K150 grant was issued by KL-Certification GmbH; K010 and K034 by Bay Area
Compliance Laboratory Corp.

Not recorded as fact: nothing about schematics or internal photos in the FCC
filings. The FCC exhibit databases returned HTTP 403 to the research agent, so
no exhibit other than the vendor-hosted grant was read.

Boards without any filing found are the Core Basic, the Waveshare board and the
headless development board. This paragraph used to name the ESP32 StickC family
as well; that was wrong and the 2026-11-26 correction below records both IDs.
For the Core Basic the module grant is the only applicable ID, and no vendor page
was found that states a board-level ID.

## 9. Code check: does furble retry the first PM1 transaction after a wake

Yes, on every access that goes through the helper, and no on two paths.

- `include/FurblePlatform.h:370-394`: `m5pm1Access()` runs the access, and on
  any result other than `M5PM1_OK` it increments a retry counter and runs the
  same access a second time. Two failures increment the failure counter.
  The comment states the vendor rule, so the helper exists for exactly this.
- `src/FurblePlatform.cpp:278-283`: `wakeM5PM1()` reads the battery voltage and
  discards the value, so the wake itself is the retried transaction.
- `src/FurblePlatform.cpp:439-441`: the power-off path notes that the first
  access only wakes the PMIC and goes through the helper for that reason.
- Not retried: `btnGetState()` in `Platform::update()` (`src/FurblePlatform.cpp:991`)
  calls the library directly, and so do `begin()`, `setSingleResetDisable()` and
  `setDoubleOffDisable()` at `src/FurblePlatform.cpp:125-127`. A failed button
  read costs one 20 ms poll, because `PlatformPolicy::pm1PollDue` already limits
  the poll to `PM1_POLL_MS = 20` (`include/FurblePlatformPolicy.h:19-24`). A
  failed init call is logged by the library path, and `begin()` has to run
  before any retry could help.

The same check showed that both the board-power PM-03 row and the soc.md OS-07
row were stale: they described 200 button reads per second with no throttle,
and OS-07 was still written as an open recommendation. The code has throttled
the poll to 50 Hz since the policy header landed, so both rows are corrected
here rather than left describing behaviour that no longer exists. The 40 to
150 uA saving stays `inferred` until a measurement exists.

## Files changed

- `docs/power/soc.md`: UART clock tree row and checked-list row, RMT rows with
  channel and RAM shape, USB Serial/JTAG citations, PSRAM pin note, the OS-07
  row and the wake-budget row, reference list.
- `docs/power/board-power.md`: StickS3 and Waveshare inventory cells, the
  StickS3 board note, the M5PM1 section, PM-03, and the section 7 register rows
  for the two TRMs, the SX1262 and the W5500.
- `docs/power/datasheet-register.md`: new section 1 rows for both TRMs, the
  octal and quad rows, the PICO-1 row and the SX1262 row; the existing
  section 4 W5500 row replaced with the 1.1.0 content rather than duplicated;
  the M5PM1 and StickS3 low-power rows; the section 11 W5500 row removed;
  section 12 errata and revision notes; section 13.1 and 13.3 FCC rows.
- `docs/sim-coverage.md`: hardware facts that the doubles deliberately do not
  model, with citations.
- `CLAUDE.md`: the DFS clock bullet, the M5PM1 bullet, the S3 USB bullet and
  the `lib/hal` SX1262 pointer.
- `plans/implemented/322-hardware-abstraction.md`: the `SX1262::sleep()` sequence.
- `plans/implemented/22-ir-remote-trigger.md`: the RMT memory constant and the
  channel-count note.

## What remains unverified

1. **WAKE_SRC address inside the M5PM1 manual.** Three different addresses for
   one register in one revision, as listed in section 7. The register map and
   the `0x2F` note cannot both be right. Closing it needs a vendor reply or a
   scope capture of the register the PMIC writes, not another read of the same
   PDF.
2. **Which M5PM1 V1.9 build is authoritative.** Both are V1.9, both are 35
   pages, and they differ byte for byte. Nothing quoted here differs between
   them, but a future register claim must name the build hash.
3. **The StickS3 grant exhibits.** The K150 grant names the M5StickS3 itself
   (Notes: M5StickS3, Equipment Class DTS), so it is a board-level filing and
   not a module grant. Only the grant page was read: any exhibit beyond it,
   including schematics or internal photos, is unverified because the FCC
   exhibit databases returned 403. The FCC ID is OCR-derived, so it is a
   page-render reading and not a text-layer reading, unlike the K010 and
   K034 grants.
4. **StickC, StickC Plus, Plus2 and Core Basic board-level FCC IDs.** No vendor
   page or grant was found for these boards, so section 13.3 keeps them as a
   module-grant-only statement rather than an ID.
5. **W5500 IDD1 and IDD2 applicability to the Waveshare board.** The figures are
   the chip at 3.3 V and 25 C with auto-negotiation; the board adds a PoE
   header, an LED and the ESP32-S3. No board-level measurement exists, and the
   flash gate is still deferred (#407 was not hardware-retested), so the
   inventory keeps these as vendor figures, not measured board current.
6. **M5PM1 idle sleep in the shipped path.** The manual's first-transaction
   failure only matters if idle sleep is enabled. The current library path
   writes `SLP_TO = 0`, which disables it, and the code comment in
   `board-power.md` says so. Whether any future settings change enables it is a
   code question, not a document one, so the docs keep both statements.
7. **The SX1262 is not on any supported board.** The driver in `lib/hal` has no
   hardware behind it in this tree, so every timing above is document-only and
   no measurement contradicts or confirms it.

## Correction, 2026-11-26: the StickC board IDs exist

Source: the retrospective review of #495 (`plans/357-review-feedback.md`), which
asked whether the StickC claim was understated the way the Waveshare one was. It
was.

The FCC ID database serves one page per ID even though its search endpoint
returns HTTP 403, so every ID above was re-read that way. Two board-level grants
exist for boards this plan listed as having none:

| Board | FCC ID | Original grant | Grantee | Equipment class |
| --- | --- | --- | --- | --- |
| M5Stick | 2AN3WM5STICKC | 2019-06-04 | M5Stack Technology Co.,Ltd | DTS |
| M5StickC-Plus SE | 2AN3WM5STICKCPLUS | 2026-01-23 | M5Stack Technology Co.,Ltd | DTS |

Read from `https://fccid.io/2AN3WM5STICKC` and
`https://fccid.io/2AN3WM5STICKCPLUS`, which mirror the FCC database entries,
grantee code 2AN3W, product codes M5STICKC and M5STICKCPLUS. The 2AN3WM5STICKC
page lists the original equipment application of 2019-06-04, a Bay Area
Compliance Laboratories test firm and a 2,402 to 2,480 MHz range.

The same read confirms the four IDs the table above already carried, and dates
them to their original grants rather than to later permissive changes:
2AN3WM5STICKS3 2025-12-16, 2AN3WM5STACK-CORE2 2020-08-20 (the page also lists a
2023-01-10 class II permissive change), 2AN3W-M5STACK-TOUGH 2021-04-19 and
2AC7Z-ESPS3WROOM1 2022 (the page lists 2022-09-07 and 2022-09-08 class II
changes and a 2026-02-27 one).

What the method could not do: the fccid.io search endpoint returned HTTP 403 to
this run as well, so no board was searched by name. Only the two candidate IDs
that the FCC ID format made guessable were checked, which is why the Plus2, the
Core Basic and the Waveshare board keep a not-found status rather than a
negative. The Waveshare row in the FCC table is scoped to the product page for
the same reason.
