# Component datasheet corpus

One page per part furble drives or plans to drive. A page records the document
identity, the retrieval URL, the retrieval date, the register, bit, command and
timing facts the firmware depends on with their section numbers, the code that
implements them, and what is still unverified.

The rule this corpus serves, from [`lib/hal/CLAUDE.md`](../../lib/hal/CLAUDE.md):
cite the datasheet title, revision and section next to every register, bit and
command, and do not commit datasheet PDFs. A document is stored only when its
licence plainly permits redistribution, see
[Committed documents](#committed-documents).

The corpus is checked by `tools/check_datasheet_citations.py`. It fails when a
`lib/hal` header or source names a register, command, chip identity or timing
without a datasheet line nearby, and it names the page that must hold the
citation. A tolerated gap has to appear in that page's
[Citation gaps](#page-layout) table, and the gate validates that table on every
page here, whether a driver maps to the page or not.

The gate checks the shape of a citation, never the identity of a document. It
can tell `section 6.13.2` from `section 6x` or from `Table Item`; it cannot tell
whether the section exists in the datasheet, whether the revision on a page is
the one that was read, or whether the fact is true. A plausible number passes.
Reading the document is the only thing that settles those, which is why every
fact carries a state and why REPORTED and CITED are never counted as VERIFIED.

## Status vocabulary

Every fact row carries exactly one of four states in its State column, and only
one. Nuance belongs in the preceding cell, so a reader or a script can decide a
row from the state alone. They are not interchangeable.

| State | Meaning |
| :--- | :--- |
| VERIFIED | The document was opened on 2026-11-25 for this corpus and the section heading, page or value in the row was read in it. |
| REPORTED | An independent verification pass on 2026-11-25 read the document and reported the fact with its section and page. This corpus did not reopen it, and the row keeps the section the pass gave. |
| CITED | The tree already cites the document and section, and this corpus did not reopen it. Treat as a lead, not as a check. |
| UNVERIFIED | No document could be found, or the document does not state the fact, or the section could not be reached. Never promoted to a fact. |

Independent verification passes on 2026-11-25 supplied twelve findings. They
are recorded on the pages that own them, with the revision, section and page the
pass gave, and a code defect is marked where one exists:

- BQ27220 command pacing and the unconnected driver, bq27220.md
- SX1262 reset-to-ready timing and the STDBY_RC timeout, sx1262.md
- AXP2101 REG 90 bit 4 vendor defect and the identity register, axp2101.md
- The host test citation gaps, axp2101.md, bq27220.md, xl9555.md
- IP5306 light-load timeout, charge status and the undocumented level register,
  ip5306.md
- W5500 reset pulse defect, w5500.md
- MPU-6886 revision and threshold scale, mpu6886.md
- The one-way panel sleep rule, st7789.md and ili934x.md
- PCF8563 timer semantics behind the Plus2 timed wake, pcf8563.md
- The BM8563 and CST9217 source gaps, bm8563.md and cst9217.md

## Index

The index carries no status. Each page's own tables are authoritative for
what is verified, reported, cited or unverified for that part, because a single
summary state per part hides which of its facts are weak. A row here says only
which page covers the part and which documents that page names.

| Component | Page | Role | Implementation | Documents |
| :--- | :--- | :--- | :--- | :--- |
| M5Stack M5PM1 | [m5pm1.md](m5pm1.md) | PMIC, watchdog, power button, status LED on the StickS3 | `src/FurblePlatform.cpp`, `include/FurblePlatform.h` | M5PM1 V1.9 |
| X-Powers AXP2101 | [axp2101.md](axp2101.md) | PMIC for boards M5Unified does not know | `lib/hal/AXP2101.cpp` | AXP2101 rev 1.0 |
| X-Powers AXP192 | [axp192.md](axp192.md) | PMIC on the Core2 and the StickC family | `src/FurblePlatform.cpp` via M5Unified | AXP192 datasheet, no revision |
| TI BQ27220 | [bq27220.md](bq27220.md) | Fuel gauge | `lib/hal/BQ27220.cpp` | SLUUBD4A |
| TI BQ25896 | [bq25896.md](bq25896.md) | Charger on the T-LoRa Pager, no driver yet | none | SLUSC76C |
| XINLUDA XL9555 | [xl9555.md](xl9555.md) | 16-bit IO expander for GPS enable and rails | `lib/hal/XL9555.cpp` | Rev 2.3 |
| Semtech SX1262 | [sx1262.md](sx1262.md) | LoRa radio, held in sleep | `lib/hal/SX1262.cpp` | Rev 2.2 |
| CASIC AT6668 | [at6668.md](at6668.md) | GNSS receiver in the M5 GPS unit | `src/FurbleGPS.cpp`, `lib/furble/protocol/GpsCasic.cpp` | none found |
| u-blox MAX-M10S | [ublox-max-m10s.md](ublox-max-m10s.md) | GNSS receiver, ROM SPG 5.10 | `lib/furble/protocol/GpsUbx.h` | UBX-21035062 R03 |
| u-blox MIA-M10Q | [ublox-mia-m10q.md](ublox-mia-m10q.md) | GNSS module variant, same ROM | `lib/furble/protocol/GpsUbx.h` | UBX-21035062 R03 |
| Sitronix ST7789 and ST7789V2 | [st7789.md](st7789.md) | Panel on the StickS3 and most M5 boards | M5GFX panel, `src/FurbleUI.cpp` | V1.3 and V1.0 |
| ILITEK ILI934x | [ili934x.md](ili934x.md) | Panel on the Core2 and the Core | M5GFX panel, `src/FurbleUI.cpp` | ILI9342C V101 |
| Sino Wealth SH1106 | [sh1106.md](sh1106.md) | OLED for the T-Beam Supreme profile | none yet | none, the plan names the part |
| CHIPONE CO5300 | [co5300.md](co5300.md) | AMOLED for the T-Watch Ultra profile | none yet | V0.00 |
| Hynitron CST9217 | [cst9217.md](cst9217.md) | Touch for the T-Watch Ultra profile | none yet | none found |
| Hynitron CST816 | [cst816.md](cst816.md) | Touch controller family M5GFX already supports | M5GFX | CST816D V1.3 |
| TI TCA8418 | [tca8418.md](tca8418.md) | Keypad scanner for the Cardputer Adv and Pager | none yet | SCPS215G |
| NXP PCF8563 | [pcf8563.md](pcf8563.md) | RTC with a voltage-low flag | `src/FurbleTimeKeeper.cpp` | Rev 11.1 |
| Belling BM8563 | [bm8563.md](bm8563.md) | RTC on the StickC Plus2 | `src/FurbleTimeKeeper.cpp` via M5Unified | a Chinese V1.1 copy only |
| Injoinic IP5306 | [ip5306.md](ip5306.md) | Power bank SoC on the Core | M5Unified power path | V1.01 |
| QST QMI8658 | [qmi8658.md](qmi8658.md) | IMU on the T-Watch Ultra and T-Beam Supreme | none yet | Rev 1.0, URL blocked |
| TDK InvenSense MPU-6886 | [mpu6886.md](mpu6886.md) | IMU and wake-on-motion on the StickC family | `src/FurbleIMUMotionMPU6886.cpp` | Revision 1.1 |
| Bosch BMI270 | [bmi270.md](bmi270.md) | IMU and any-motion on the StickS3 | `src/FurbleIMUMotionBMI270.cpp` | BST-BMI270-DS000 |
| Espressif ESP32 | [esp32.md](esp32.md) | SoC: UART, RMT, LEDC, SPI, I2C traps | `src/FurbleGPS.cpp`, `src/FurbleIR.cpp`, `src/FurbleUI.cpp` | v5.3 |
| Espressif ESP32-S3 | [esp32-s3.md](esp32-s3.md) | SoC: same traps, no REF_TICK, USB Serial/JTAG | `src/FurbleGPS.cpp`, `src/FurbleIR.cpp` | v2.2 |
| Sitronix ST7735S | [st7735.md](st7735.md) | Panel behind the power model | M5GFX panel | v1.1 |
| WIZnet W5500 | [w5500.md](w5500.md) | Ethernet on the Waveshare board | `src/FurbleEthernet.cpp` | v1.0.6 |

## Committed documents

No PDF is committed in this corpus. Every page cites its documents instead.

| File in git | Licence | Licence statement and source | Vendor page |
| :--- | :--- | :--- | :--- |
| none | not applicable | not applicable | not applicable |

The rule applied to each document, in the order it was checked:

1. The licence must plainly permit redistribution. A statement that the
   document is confidential, all rights reserved, or "do not distribute" is a
   refusal, not a silence.
2. The exact licence statement is recorded on the component page with its URL,
   and the directory that holds the PDF gets a LICENSE or README note.
3. The file stays under about 10 MB and nothing is stripped from it.
4. A document taken from a mirror is never committed.

The vendor documents this corpus relies on fail condition 1. Two examples are
recorded on their pages. The Sitronix panel specifications carry "Sitronix
Confidential The information contained herein is the exclusive property of
Sitronix and shall not be distributed, reproduced, or disclosed in whole or in
part without prior written permission of Sitronix." The X-Powers PMIC datasheet
carries "Copyright Shenzhen X-Powers Technology Co., Ltd. All Rights Reserved."
Documents without a redistribution licence are cited, not stored.
This is why `docs/components/` holds no `*.pdf` and why every page works from
the URL and the section numbers alone.

## Page layout

Each page has the same sections: part and package, documents used, facts furble
depends on, where the code implements it, citation gaps, facts still unverified,
and redistribution. The citation gaps table is machine read by
`tools/check_datasheet_citations.py`: a `lib/hal` token with no nearby datasheet
line passes only when its name appears in backticks in the first column.

## Adding a component

1. Add the page here and link it from the index.
2. Record the document title, revision or date, URL and the retrieval date.
3. Record each fact with its section, the code location as `file:line`, and a
   state from the vocabulary above.
4. Mark anything you could not confirm against the document UNVERIFIED. Do not
   guess a section number.
5. If the part is a `lib/hal` driver, add its page to `PAGE_BY_STEM` in
   `tools/check_datasheet_citations.py` if the default name does not fit, then
   run the check and the host driver test.

## Known corpus gaps

- No AT6668, SH1106, CST9217 or QMI8658 vendor document could be confirmed on
  2026-11-25. Those four pages carry no document-backed register fact. The SH1106
  page records only what `plans/322-hardware-abstraction.md` claims, marked
  UNVERIFIED.
- The AXP2101 per-register subsection numbers cannot be reached through the
  retrieved copy, so they stay at the state the driver already had.
- The XINLUDA XL9555 copy confirmed Rev 2.3 but not the section headings.
- The MPU-6886 revision in the firmware comment (1.2) disagrees with the copy
  this corpus opened (1.1).
