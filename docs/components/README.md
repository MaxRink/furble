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

Wire-level protocol pages are in [docs/protocols](../protocols/README.md). The
support claim for a board or a GNSS unit stays in
[supported hardware](../supported-hardware.md); this directory is electrical and
driver detail, not a support list.

## Status vocabulary

Every fact row carries exactly one of four states in its State column, and only
one. Nuance belongs in the preceding cell, so a reader or a script can decide a
row from the state alone. They are not interchangeable.

| State | Meaning |
| :--- | :--- |
| VERIFIED | The document was opened for this corpus and the section heading, page or value in the row was read in it. The retrieval date on the page says when. |
| REPORTED | An independent verification pass read the document and reported the fact with its section and page. This corpus did not reopen it, and the row keeps the section the pass gave. |
| CITED | The tree already cites the document and section, and this corpus did not reopen it. Treat as a lead, not as a check. |
| UNVERIFIED | No document could be found, or the document does not state the fact, or the section could not be reached. Never promoted to a fact. |

Independent verification passes supplied the findings recorded on the pages that
own them, with the revision, section and page the pass gave, and a code defect is
marked where one exists:

- BQ27220 command pacing and the unconnected driver, bq27220.md
- SX1262 reset-to-ready timing and the STDBY_RC timeout, sx1262.md
- AXP2101 REG 90 bit 4 vendor defect and the identity register, axp2101.md
- The host test citation gaps, axp2101.md, bq27220.md, xl9555.md
- IP5306 light-load timeout, charge status and the undocumented level register,
  ip5306.md
- W5500 reset pulse defect, w5500.md
- MPU-6886 revision, threshold scale and the page 24 register misprint,
  mpu6886.md
- The one-way panel sleep rule, st7789.md and ili934x.md
- PCF8563 timer semantics behind the Plus2 timed wake, pcf8563.md
- The BM8563 and CST9217 source gaps, bm8563.md and cst9217.md
- The AT6668 protocol revision and the retired AT6558 current proxy,
  at6668.md
- The MIA-M10Q default constellation, ublox-mia-m10q.md

## Support status

The component pages record what a document says. This column records what the
firmware does, which is a different question and is the one a reader usually
asks first.

| Status | Meaning |
| :--- | :--- |
| shipped | The firmware drives the part on a supported board and a board profile selects it. |
| board support | M5Unified or M5GFX drives it. furble configures or reads it through that library and holds no register table. |
| future | A pure driver exists in `lib/hal/`, or a page exists for a planned board, and no shipped board instantiates it. The only caller today is `tests/host/hal_drivers_test.cpp`, and `plans/322-hardware-abstraction.md` is the first board that will use the part. |

A second pass on the same day covered the ten power and sensor pages again and
every fact it supplied was reopened here before it was written down, so those
rows are VERIFIED rather than REPORTED. Where the pass and the document
disagreed the document won, and the page says so:

- AXP192: the complete document was in the tree's own citations, in three
  53-page copies. The M5Stack English file is a 4-page feature sheet.
- AXP2101: the per-register subsections are reachable, and REG 90 bit 4 is
  settled by three revisions of the same table.
- IP5306: the two Injoinic documents grant conditional reproduction, so the
  blanket "every document failed condition 1" was wrong.
- BM8563: the vendor original was always reachable and is byte-identical to the
  M5Stack copy already listed. The page's premise was wrong.
- BMI270: the any-motion layout is in the datasheet, at section 5.2.40 page 92.
- QMI8658A: Rev D is on QST's own site. The "URL blocked" note was wrong.
- Corrections to the pass itself, with the evidence, on axp192.md (the 100 uA
  LDO figure exists, as IQ), bq25896.md (the watchdog does not reset IINLIM) and
  bm8563.md (the two sleep-current figures are two rows of one table).

## Index

The index carries the support status above and no per-fact state. Each page's
own tables are authoritative for what is verified, reported, cited or unverified
for that part, because a single summary state per part hides which of its facts
are weak. A row here says which page covers the part and which documents that
page names.

| Component | Page | Status | Role | Implementation | Documents |
| :--- | :--- | :--- | :--- | :--- | :--- |
| M5Stack M5PM1 | [m5pm1.md](m5pm1.md) | shipped | PMIC, watchdog, power button, status LED on the StickS3 | `src/FurblePlatform.cpp`, `include/FurblePlatform.h` | M5PM1 V1.9 |
| X-Powers AXP2101 | [axp2101.md](axp2101.md) | future | PMIC for boards M5Unified does not know | `lib/hal/AXP2101.cpp` | AXP2101 rev 1.4, rev 1.0 and rev 0.1 |
| X-Powers AXP192 | [axp192.md](axp192.md) | board support | PMIC on the Core2 and the StickC family | `src/FurblePlatform.cpp` via M5Unified | AXP192 V1.2 and two unrevisioned copies of the same 53 pages |
| TI BQ27220 | [bq27220.md](bq27220.md) | future | Fuel gauge | `lib/hal/BQ27220.cpp` | SLUUBD4A |
| TI BQ25896 | [bq25896.md](bq25896.md) | future | Charger on the T-LoRa Pager, no driver yet | none | SLUSC76C |
| XINLUDA XL9555 | [xl9555.md](xl9555.md) | future | 16-bit IO expander for GPS enable and rails | `lib/hal/XL9555.cpp` | Rev 2.3 |
| Semtech SX1262 | [sx1262.md](sx1262.md) | future | LoRa radio, held in sleep | `lib/hal/SX1262.cpp` | Rev 2.2 |
| CASIC AT6668 | [at6668.md](at6668.md) | shipped | GNSS receiver in the M5 GPS unit | `src/FurbleGPS.cpp`, `lib/furble/protocol/GpsCasic.cpp` | DS-AT6668 V1.4, CASIC R6, CASIC V4.2.0.3 |
| u-blox MAX-M10S | [ublox-max-m10s.md](ublox-max-m10s.md) | shipped, hardware untested | GNSS receiver, ROM SPG 5.10 | `lib/furble/protocol/GpsUbx.h` | UBX-21035062 R03, UBX-20035208 R08 |
| u-blox MIA-M10Q | [ublox-mia-m10q.md](ublox-mia-m10q.md) | shipped, hardware untested | GNSS module variant, same ROM | `lib/furble/protocol/GpsUbx.h` | UBX-22015849 R08, UBX-21028173 R05 |
| Sitronix ST7789 and ST7789V2 | [st7789.md](st7789.md) | board support | Panel on the StickS3 and most M5 boards | M5GFX panel, `src/FurbleUI.cpp` | V1.3 and V1.0 |
| ILITEK ILI934x | [ili934x.md](ili934x.md) | board support | Panel on the Core2 and the Core | M5GFX panel, `src/FurbleUI.cpp` | ILI9342C V101 |
| Sino Wealth SH1106 | [sh1106.md](sh1106.md) | future | OLED for the T-Beam Supreme profile | none yet | none found, a distributor mirror only |
| CHIPONE CO5300 | [co5300.md](co5300.md) | future | AMOLED for the T-Watch Ultra profile | none yet | V0.00 |
| Hynitron CST9217 | [cst9217.md](cst9217.md) | future | Touch for the T-Watch Ultra profile | none yet | none found |
| Hynitron CST816 | [cst816.md](cst816.md) | board support | Touch controller family M5GFX already supports | M5GFX | CST816D V1.3 |
| TI TCA8418 | [tca8418.md](tca8418.md) | future | Keypad scanner for the Cardputer Adv and Pager | none yet | SCPS215G |
| NXP PCF8563 | [pcf8563.md](pcf8563.md) | board support | RTC with a voltage-low flag | `src/FurbleTimeKeeper.cpp` | Rev 11.1 |
| Belling BM8563 | [bm8563.md](bm8563.md) | shipped | RTC on the StickC Plus2 | `src/FurbleTimeKeeper.cpp` via M5Unified | BM8563 V1.1, Chinese, the vendor original |
| Injoinic IP5306 | [ip5306.md](ip5306.md) | board support | Power bank SoC on the Core | M5Unified power path | Register document V1.21, datasheet V1.31 and V1.01 |
| QST QMI8658 | [qmi8658.md](qmi8658.md) | future | IMU on the T-Watch Ultra and T-Beam Supreme | none yet | QMI8658A Rev D |
| TDK InvenSense MPU-6886 | [mpu6886.md](mpu6886.md) | shipped | IMU and wake-on-motion on the StickC family | `src/FurbleIMUMotionMPU6886.cpp` | DS-000193 revision 1.1, pre-production |
| Bosch BMI270 | [bmi270.md](bmi270.md) | shipped | IMU and any-motion on the StickS3 | `src/FurbleIMUMotionBMI270.cpp` | BST-BMI270-DS000-08 rev 1.6 |
| Espressif ESP32 | [esp32.md](esp32.md) | shipped | SoC: UART, RMT, LEDC, SPI, I2C traps | `src/FurbleGPS.cpp`, `src/FurbleIR.cpp`, `src/FurbleUI.cpp` | v5.3 |
| Espressif ESP32-S3 | [esp32-s3.md](esp32-s3.md) | shipped | SoC: same traps, no REF_TICK, USB Serial/JTAG | `src/FurbleGPS.cpp`, `src/FurbleIR.cpp` | v2.2 |
| Sitronix ST7735S | [st7735.md](st7735.md) | board support | Panel on the StickC | M5GFX panel | v1.1 |
| WIZnet W5500 | [w5500.md](w5500.md) | shipped | Ethernet on the Waveshare board | `src/FurbleEthernet.cpp` | v1.1.0 |

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

Most of the vendor documents this corpus relies on fail condition 1, and the
two examples below are recorded on their pages. The Sitronix panel
specifications carry "Sitronix Confidential The information contained herein is
the exclusive property of Sitronix and shall not be distributed, reproduced, or
disclosed in whole or in part without prior written permission of Sitronix." The
X-Powers PMIC datasheet carries "Copyright Shenzhen X-Powers Technology Co.,
Ltd. All Rights Reserved."

Two documents do not fail it. Both Injoinic documents grant conditional
reproduction, which satisfies condition 1: the IP5306 register document V1.21
section 4 and the IP5306 datasheet V1.31 page 11 each permit an unaltered copy
that keeps the notices, quoted in full on [ip5306.md](ip5306.md#redistribution).
Condition 4 still blocks committing those particular bytes, because both copies
came from a mirror. So an earlier version of this paragraph was wrong to say
that every document failed condition 1, and it is wrong to generalise from the
two refusals above to the rest of the corpus: each page states its own finding.

No document is stored. This is why `docs/components/` holds no `*.pdf` and why
every page works from the URL and the section numbers alone.

## Page layout

Each page has the same sections: part and package, status, role in furble,
documents used, facts furble depends on, where the code implements it, citation
gaps, facts still unverified, open rows, and redistribution. The citation gaps
table is machine read by `tools/check_datasheet_citations.py`: a `lib/hal` token
with no nearby datasheet line passes only when its name appears in backticks in
the first column.

The open rows table carries an ID from the gap register in
`plans/332-docs-gap-closure.md`, the document and section that would close the
row, and the state. A row is closed by filling the fact table with the document
revision and page, not by deleting the row.

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

- No SH1106 or CST9217 vendor document could be confirmed on 2026-11-25. Those
  two pages carry no document-backed register fact. The SH1106 page records
  only what `plans/322-hardware-abstraction.md` claims, marked UNVERIFIED, and
  the copy a distributor mirror serves is not a vendor document. QMI8658A is no
  longer in this list: Rev D is on QST's own site and [qmi8658.md](qmi8658.md)
  now cites it.
- The AT6668 gap closed on 2026-11-25: the chip datasheet DS-AT6668 V1.4 and
  both CASIC protocol revisions were opened and are cited on
  [at6668.md](at6668.md). The two protocol revisions disagree about `$PCAS04`,
  `$PCAS15`, the binary classes and the MON-HW antenna status encoding, and the
  unit does not say which one its ROM implements, so those rows carry both.
- The XINLUDA XL9555 copy confirmed Rev 2.3 but not the section headings; its
  electrical characteristics table was reached as a heading only.
- The ILI9342C has no supply current table, so every current figure for that
  panel is a proxy.
- The MPU-6886 revision in the firmware comment (1.2) disagrees with the copy
  this corpus opened (1.1). The firmware comment is a code defect and is still
  open.
- The BMI270 source comments at `src/FurbleIMUMotionBMI270.cpp:39`, `:45`, `:62`
  and `:72` still carry the section numbers the datasheet disproves, and the file
  header still says the any-motion layout is not in the datasheet when section
  5.2.40 page 92 carries it. All four are firmware comment changes.
- The AXP2101 `CHIP_ID` 0x4A at `lib/hal/AXP2101.cpp:27`. Revision 0.1 documents
  0x47 or 0x57, and the two later revisions drop register 0x03 entirely.
