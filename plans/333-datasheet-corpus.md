# 333 - Datasheet citation corpus

Status: documentation and tooling only, with comment corrections in `lib/hal`
and five in `src/FurbleIMUMotionBMI270.cpp`.
No firmware behaviour, setting, build or hardware change. Every register fact
carries a document, revision and section, or is marked UNVERIFIED. The corpus is
on branch `docs/datasheet-corpus`, PR 475. All three review rounds are applied:
twelve findings in the first, eight in the second and five non-blocking
follow-ups in the third, and the three resolution tables are below. A fourth
pass audited the page `file:line` references and corrected eleven of them, and
the rebase onto #479 is recorded at the end.

A second pass over the ten power and sensor pages is stacked on that branch as
`docs/datasheet-corpus-verify`, on PR 475's head rather than on master. It
reopened every fact in those ten pages and records, per fact, the document
revision, the page and the date read. See
[Second pass, ten power and sensor pages](#second-pass-ten-power-and-sensor-pages).
The GNSS and SoC pages belong to a separate change.

Related: [322](322-hardware-abstraction.md) (the drivers the corpus documents),
[287](287-mcu-portability-and-bundle-hardware.md) (the board survey),
[321](321-ublox-m10-driver.md) (the M10 interface description),
[95](95-engineering-lessons.md) (the hardware traps).

## Why

`lib/hal/CLAUDE.md` requires a datasheet section next to every register, bit and
command. There was nowhere to record the rest of a citation: the document title,
the revision, the URL, the retrieval date, the code location and what is still
uncertain. The same gap applied outside `lib/hal`. The hardware traps in the root
`CLAUDE.md` rest on facts from the ESP32 and ESP32-S3 datasheets, the panel
documents, the M5PM1 manual and the IP5306 behaviour, and no page held those
facts with a section number.

Three failure modes motivated the work:

1. A plausible constant with a stale comment. The comment is the only place the
   evidence lives, and nothing checks it.
2. A citation that drifts from the revision it was written against. The MPU-6886
   comment cites register map revision 1.2; the document is DS-000193 revision
   1.1 and revision 1.2 does not exist.
3. A licensing mistake. A vendor PDF is easy to commit and hard to defend, and
   several of these documents forbid redistribution outright.

## What landed

| Area | Change |
| :--- | :--- |
| `docs/components/` | One index and 27 component pages, 2167 lines |
| `tools/check_datasheet_citations.py` | The gate, 339 lines |
| `tests/test_check_datasheet_citations.py` | 26 cases, 327 lines |
| `lib/hal` comments | Confirmed sections added where the driver had none: AXP2101 section 6.3 and 6.13.2, the BQ27220 per-command sections, the SX1262 windows, the XL9555 command bytes |
| `lib/hal/SX1262.h` | The 3.5 ms figure attributed to SLEEP to STDBY_RC, and the 10 ms budget named an engineering margin |
| `src/FurbleIMUMotionBMI270.cpp` | Five BMI270 comments corrected to the sections the verification pass read: INT_STATUS_0 5.2.28, INTERNAL_STATUS 5.2.33, FEAT_PAGE and FEATURES 5.2.39 and 5.2.40, INT1_MAP_FEAT 5.2.61, INT_MAP_DATA 5.2.63. The file is under `src/`, so the gate does not scan it. |
| `lib/hal/CLAUDE.md` | Points at `docs/components/`, states the conditional PDF rule and the gap-table rule |
| `docs/hardware-catalog.md` | A "Datasheet citations" section that links the corpus |
| `docs/power/datasheet-register.md` | A pointer to the corpus and the missing PCF8563 document row |

## Component inventory

Every part furble drives or plans to drive. "Planned" means the plan names it and
the tree has no driver.

| Component | Page | Role | Implementation |
| :--- | :--- | :--- | :--- |
| M5Stack M5PM1 | [m5pm1.md](../docs/components/m5pm1.md) | PMIC, watchdog, power button, status LED on the StickS3 | `src/FurblePlatform.cpp` |
| X-Powers AXP2101 | [axp2101.md](../docs/components/axp2101.md) | PMIC for boards the board support library does not know | `lib/hal/AXP2101.cpp` |
| X-Powers AXP192 | [axp192.md](../docs/components/axp192.md) | PMIC on the Core2 and StickC family | `src/FurblePlatform.cpp` |
| TI BQ27220 | [bq27220.md](../docs/components/bq27220.md) | Fuel gauge | `lib/hal/BQ27220.cpp` |
| TI BQ25896 | [bq25896.md](../docs/components/bq25896.md) | Charger on the T-LoRa Pager, no driver | none |
| XINLUDA XL9555 | [xl9555.md](../docs/components/xl9555.md) | IO expander for GPS enable and rails | `lib/hal/XL9555.cpp` |
| Semtech SX1262 | [sx1262.md](../docs/components/sx1262.md) | LoRa radio, held in sleep | `lib/hal/SX1262.cpp` |
| CASIC AT6668 | [at6668.md](../docs/components/at6668.md) | GNSS receiver in the M5 GPS unit | `src/FurbleGPS.cpp`, `lib/furble/protocol/GpsCasic.cpp` |
| u-blox MAX-M10S | [ublox-max-m10s.md](../docs/components/ublox-max-m10s.md) | GNSS receiver, ROM SPG 5.10 | `lib/furble/protocol/GpsUbx.h` |
| u-blox MIA-M10Q | [ublox-mia-m10q.md](../docs/components/ublox-mia-m10q.md) | GNSS module variant, same ROM | `lib/furble/protocol/GpsUbx.h` |
| Sitronix ST7789 and ST7789V2 | [st7789.md](../docs/components/st7789.md) | Panel on the StickS3 and most M5 boards | board support library, `src/FurbleUI.cpp` |
| ILITEK ILI934x | [ili934x.md](../docs/components/ili934x.md) | Panel on the Core2 and the Core | board support library, `src/FurbleUI.cpp` |
| Sino Wealth SH1106 | [sh1106.md](../docs/components/sh1106.md) | OLED for the T-Beam Supreme profile | planned |
| CHIPONE CO5300 | [co5300.md](../docs/components/co5300.md) | AMOLED for the T-Watch Ultra profile | planned |
| Hynitron CST9217 | [cst9217.md](../docs/components/cst9217.md) | Touch for the T-Watch Ultra profile | planned |
| Hynitron CST816 | [cst816.md](../docs/components/cst816.md) | Touch family the library already supports | board support library |
| TI TCA8418 | [tca8418.md](../docs/components/tca8418.md) | Keypad scanner for the Cardputer Adv and Pager | planned |
| NXP PCF8563 | [pcf8563.md](../docs/components/pcf8563.md) | RTC with a voltage-low flag and the timer behind the Plus2 wake | `src/FurbleTimeKeeper.cpp` |
| Belling BM8563 | [bm8563.md](../docs/components/bm8563.md) | RTC on the StickC Plus2 | `src/FurbleTimeKeeper.cpp` |
| Injoinic IP5306 | [ip5306.md](../docs/components/ip5306.md) | Power bank SoC on the Core | board support library |
| QST QMI8658 | [qmi8658.md](../docs/components/qmi8658.md) | IMU on the T-Watch Ultra and T-Beam Supreme | planned |
| TDK InvenSense MPU-6886 | [mpu6886.md](../docs/components/mpu6886.md) | IMU and wake-on-motion on the StickC family | `src/FurbleIMUMotionMPU6886.cpp` |
| Bosch BMI270 | [bmi270.md](../docs/components/bmi270.md) | IMU and any-motion on the StickS3 | `src/FurbleIMUMotionBMI270.cpp` |
| Espressif ESP32 | [esp32.md](../docs/components/esp32.md) | UART, RMT, LEDC, SPI and I2C traps | `src/FurbleGPS.cpp`, `src/FurbleIR.cpp` |
| Espressif ESP32-S3 | [esp32-s3.md](../docs/components/esp32-s3.md) | The same traps, plus USB Serial/JTAG and PSRAM | `src/FurbleGPS.cpp`, `src/FurbleIR.cpp` |
| Sitronix ST7735S | [st7735.md](../docs/components/st7735.md) | Panel behind the power model | board support library |
| WIZnet W5500 | [w5500.md](../docs/components/w5500.md) | Ethernet on the Waveshare board | `src/FurbleEthernet.cpp` |

## Page format

Every page has the same seven sections, so a reader and a script find the same
things in the same place.

1. Part and package, with the package marked UNVERIFIED when it was not read.
2. Documents used: key, title, revision or date, URL, retrieval date, and what
   this change actually opened.
3. Facts furble depends on: one row per fact, with the document and section, the
   code as `file:line`, and a state.
4. Where the code implements it.
5. Citation gaps: the `lib/hal` tokens with no section of their own, in a table
   the gate reads.
6. Facts still unverified, per page.
7. Redistribution: what the licence says and why nothing is stored, or the
   licence statement when a document is stored.

## Citation states

| State | Meaning |
| :--- | :--- |
| VERIFIED | The document was opened on 2026-11-25 for this corpus and the section heading, page or value in the row was read in it. |
| REPORTED | An independent verification pass read the document and reported the fact with its section and page. This corpus did not reopen it. |
| CITED | The tree already cites the document and section and this corpus did not reopen it. A lead, not a check. |
| UNVERIFIED | No document, no stated fact, or an unreachable section. Never promoted. |

REPORTED exists because two independent verification passes supplied twelve
findings this change did not reopen. Folding those into VERIFIED would overclaim,
and dropping them would lose the evidence, so they get their own state and keep
the section and page the pass gave.

## The gate

`tools/check_datasheet_citations.py` enforces the section half of the
`lib/hal/CLAUDE.md` rule.

- A token is a numeric `constexpr` named `REG_*`, `CMD_*`, `OPCODE_*`,
  `ADDRESS*` or `CHIP_ID*`, or one ending `_US` or `_MS`, plus a raw hex literal
  in the register position of a bus call.
- The gate reads `constexpr`, `static const`, `const` and `#define` forms, and
  walks `lib/hal` recursively over the suffixes the format gate formats, so a
  nested driver directory and a `.cxx`, `.hh` or `.hxx` driver cannot hide.
- A definition counts when it initialises with an integer literal: bare, in
  parentheses or in braces, with any integer suffix, and every declarator on the
  line rather than only the first.
- A definition whose initialiser is not such a literal is not a token, so
  `0x10 | 0x01`, a cast and a value on the next line are invisible to the gate.
  That is the one fail-open path. `lib/hal/CLAUDE.md` states it and a test
  asserts the skip, so it cannot widen unnoticed.
- A token passes when a comment within six lines holds a well formed reference:
  a dotted section, a plain section, a table, or a section sign. A fragment such
  as `section 6x` or `Table Item` is not a reference.
- A cited comment also covers the code run it heads, bounded at 16 lines, so one
  datasheet line over a register table covers the table and one heading cannot
  cover a whole file. A trailing `//` comment on the token line counts, and only
  a `//` outside a string or character literal opens one, so a URL in a string
  cannot fabricate a citation.
- Bit masks written `1U << n` and switch labels are not tokens. The register's
  citation covers the bit, and the host test states the bit numbers from the
  datasheet.
- A token with no citation passes only when the component page lists its name in
  the `Citation gaps` table. The gate validates that table's four columns on
  every page under `docs/components/`, driver or not, so a malformed row is an
  error rather than a silent pass. Every other case fails and the message names
  the page, at `lib/hal/<file>:<line>: <token>`.
- Exit 0 when clean, 1 on a violation, 2 for an invalid invocation.

Current tree: 34 tokens, 30 cited, 4 documented gaps (`CHIP_ID`,
`SLEEP_CONFIRM_BUDGET_US`, `POLL_US`, `ADDRESS_BASE`), 0 errors. The unit test pins that token count and the per-file
counts, so a driver that stops being scanned fails the suite. The driver stem
maps to a page through `PAGE_BY_STEM`, and an unmapped stem maps to `<stem>.md`,
so a new driver lands on a predictable path and fails if the page is missing.

The gate is wired into `tools/pre_review.py` as the "datasheet citations" gate
and its test is in the pre-review unit-test list, locally and in
`.github/workflows/pre-review.yml`, so the gate count is 11.

## Licence decision

No PDF is committed. The rule applied to each document, in order:

1. The licence must plainly permit redistribution. Confidential, all rights
   reserved, or "do not distribute" is a refusal, and silence is also a refusal.
2. The exact licence statement goes on the component page with its URL, and the
   directory that holds the PDF gets a LICENSE or README note.
3. The file stays under about 10 MB and nothing is stripped from it.
4. A document taken from a mirror is never committed.

Most documents fail condition 1. Two are decisive: the Sitronix panels carry
"Sitronix Confidential The information contained herein is the exclusive
property of Sitronix and shall not be distributed, reproduced, or disclosed in
whole or in part without prior written permission of Sitronix.", and the
X-Powers
AXP2101 carries "Copyright Shenzhen X-Powers Technology Co., Ltd. All Rights
Reserved." The TI, Semtech, u-blox, NXP and Bosch documents carry copyright
notices with no redistribution grant. The M5PM1, XL9555, AXP192, ST7735S and
ILI9341 copies are silent or distributor mirrors.

Two documents pass condition 1 and the earlier version of this section was
wrong to say that none did. The IP5306 register document V1.21 section 4 and the
IP5306 datasheet V1.31 page 11 each permit reproduction of an unaltered copy
that keeps the notices, which is a conditional grant. Condition 4 still blocks
committing those bytes because both were fetched from a mirror, and the pages
record the two statements verbatim. A general claim about "every document" was
the mistake: each page states its own finding, and one page's silence says
nothing about the next.

The zero-PDF outcome is therefore the rule working, not an unfinished task.
`docs/components/README.md` holds the `Committed documents` table with a `none`
row and states that documents without a redistribution licence are cited, not
stored. A future PDF needs all four conditions met and a row added to that table.

## Facts still unverified by component

| Group | Pages | Open items |
| :--- | :--- | :--- |
| No document found at all, or found but not opened | AT6668, SH1106, CST9217 | 6, 4 and 6 items. For AT6668: the `$PCAS` argument ranges, the 108 s cold start, the missing backup rail and the baud rate. DS-AT6668 V1.4 was located and its section 4.3 page 9 gives the supply current, and #490 opened it; nothing on those pages is a register fact. QMI8658 left this group in the second pass. |
| Document reached, body not | XL9555, PCF8563, ST7789, ILI934x | XL9555 sections 9.1 and 9.2; the PCF8563 timer section number; the panel sleep timing sections. AXP2101 and IP5306 left this group in the second pass, which read every register body in both. |
| Datasheet not opened | W5500 timing, ST7735S, the M10 module data sheets and integration manuals | 6, 4 and 5 items. The section numbers there are CITED from the source comments, not reopened. BMI270 and MPU-6886 left this group in the second pass. |
| TRM not opened | ESP32, ESP32-S3 | 6 items each: UART clock sources, the RMT memory block, the RMT APB lock, the LED PWM clock tree, USB Serial/JTAG reset behaviour, and the current tables. |
| Board or policy facts | M5PM1, CO5300, CST816, SX1262 | Package and pinout for almost every part, the M5PM1 register section numbers and the 0xA1 value, the CO5300 brightness command, the CST816 register chapter, the SX1262 Table 13-1 and 13-2 encodings. AXP192, BQ25896, TCA8418 and BQ27220 left this group in the second pass: their register facts are now read from the documents, and only package and pinout remain open. |

136 bullets in total after the second pass, three to seven per page. No errata
document was found for any part, and no page claims one.

## Latent hazards the verification found

None reach a board today: no file under `src/`, `include/` or `components/`
includes any of the four `lib/hal` headers. They are recorded so the first board
that wires a driver does not inherit them silently.

| Hazard | Location | State |
| :--- | :--- | :--- |
| BQ27220 pacing: `read()` asks the bus for the 66 us gap before every packet after the first, and the hook's default is a no-op, so a firmware adapter must override it | `lib/hal/BQ27220.cpp:44` to `:49`, `lib/hal/HalBus.h:52` | Driver fixed in #479, adapter override latent |
| SX1262 BUSY timeout leaves the radio in STDBY_RC at 0.6 mA rather than 160 nA, and the driver has no retry. Both transitions are waited for inside budgets since #479 | `lib/hal/SX1262.cpp:31-33`, `:46` | Latent, no caller |
| The AXP2101 driver reads REG 90 bit 4 as BLDO1 where the revision 1.0 table labels it aldo1 twice. Revision 1.4 and the revision 0.1 field name both say BLDO1, so the driver is right; the comment at `lib/hal/AXP2101.cpp:47` to `:49` must stay | `lib/hal/AXP2101.cpp:47` to `:49`, `:50` | Settled, comment must stay |
| The W5500 hardware reset is 100 us in the pinned stack against a 500 us minimum, and the host double asserts only the GPIO number | `esp_eth_phy_w5500.c:160`, `src/FurbleEthernet.cpp:192`, `tests/host/boundaries/ethernet_w5500_test.cpp:183` | Live on the Waveshare board |
| The MPU-6886 wake threshold assumes 4 mg per count with no datasheet scale, at a plus or minus 8 g range set by `ACCEL_CONFIG` 0x10, so the effective threshold may be about four times the intended value | `src/FurbleIMUMotionMPU6886.cpp:41-43`, `:109` | Live on the StickC family |
| The panel sleep wait runs from SLPIN to SLPOUT, while the documents require 120 ms after SLPOUT before the next SLPIN plus 5 ms before any other command | `src/FurbleUI.cpp:1888-1897`, `include/FurbleUI.h:1135` | Live on every panel board |
| The IP5306 light-load timeout is 64 s, not the 32 s stated in three files, because M5Unified writes `SYS_CTL2` = 0b01101100 | `M5Unified Power_Class.cpp:617`, `include/FurbleSettingsDefaults.h:15`, `docs/power/board-power.md:24`, `:113`, `:162` | Documentation error, live behaviour is 64 s |
| The MPU-6886 comment cites a register map revision that does not exist | `src/FurbleIMUMotionMPU6886.cpp:17-18` | Comment only |

No firmware behaviour changed in this PR. The three live hazards that need code
changes, the W5500 pulse, the MPU-6886 threshold and the panel dwell direction,
have no owner yet and are the follow-up work.

## Verification

```
python3 tools/check_datasheet_citations.py
  -> 30 cited, 4 page gaps, 0 errors of 34 tokens, exit 0
python3 -m unittest tests.test_check_datasheet_citations -v
  -> Ran 26 tests, OK
python3 -m unittest discover -s tests -p 'test_check_datasheet_citations.py'
  -> OK, which is the main.yml discovery path
clang-format --dry-run --Werror on the changed C++ files
  -> clean
python3 tools/check_portability_inventory.py
  -> portable contract: clean
sh sim/scripts/check-doc-tokens.sh
  -> tokens present
python3 tools/pre_review.py --base origin/master
  -> PASS: 11 gates
```

A link scan over `docs/components/` found no missing local target, and every page
carries the seven sections. No hardware was involved. No device behaviour
changed.

## Remaining work

1. Find an SH1106 or CST9217 document. Either turns a page from plan-only into a
   citation. AT6668 and QMI8658 are closed: see the second pass below.
2. Open the two TRMs for the ESP32 and ESP32-S3 clock, RMT and USB facts.
3. Read the XL9555 section 9.1 and 9.2 tables and the PCF8563 timer section. The
   AXP2101 register bodies are read, in the second pass below.
4. Fix the three live hazards in their own PRs.
5. Correct the 32 s claim in the three files once the IP5306 64 s value is
   accepted.
6. Correct the four BMI270 section numbers the source comments still carry at
   `src/FurbleIMUMotionBMI270.cpp:39`, `:45`, `:62` and `:72`, and the file
   header at `:17` to `:21` that still says the any-motion layout is not in the
   datasheet. The page now has every correct number and page.
7. Correct the MPU-6886 revision 1.2 comment at
   `src/FurbleIMUMotionMPU6886.cpp:17` to `:18`, which cites a revision that does
   not exist.
8. Decide the AXP2101 `CHIP_ID` 0x4A at `lib/hal/AXP2101.cpp:27`. Revision 0.1
   documents 0x47 or 0x57 and the later revisions drop register 0x03, so the
   constant needs a part on a bench before it changes.
9. Read the QMI8658A CAL register encodings so the motion thresholds can carry
   units, and the CTRL1 and CTRL2 interrupt pin registers.

## Second pass, ten power and sensor pages

A second verification pass on 2026-11-25 covered `axp192`, `axp2101`, `ip5306`,
`bq25896`, `bq27220`, `tca8418`, `bm8563`, `mpu6886`, `bmi270` and `qmi8658`.
Every fact the pass supplied was reopened here, in the revision below, at the
page below, on 2026-11-25, before it was written into the page. The GNSS and SoC
pages are owned by a separate change and are untouched.

The pass found that four of these parts had a vendor document reachable all
along, and that the corpus had recorded a gap where it had not looked:

| Page | What the page claimed | What was true |
| :--- | :--- | :--- |
| axp192.md | No vendor copy exists; every row CITED from a 2010 mirror | Three complete 53-page copies exist, one of them at a vendor-controlled host, and all three carry the register map from page 29 |
| axp2101.md | The register bodies after section 6.13.2 cannot be reached | Every subsection from 6.13.2.1 to 6.13.2.88 is extractable in the revision 1.0 copy. That was an extraction failure on this corpus's side |
| bm8563.md | The Belling datasheet is not retrievable; every fact rests on a distributor copy | The vendor original is on Belling's site, and the M5Stack copy the tree already listed is the same file, sha256 a847c85a on both |
| qmi8658.md | No reachable vendor copy; the revision came from a search snippet | QMI8658A Rev D is on QST's own site, 78 pages, sha256 edda798c |

Each of those four claims is now gone from its page. The point of the corpus is
to record what a document says, not to record that nobody looked.

### Documents and dates

| Key | Document | Revision | sha256 prefix | Pages | Read |
| :--- | :--- | :--- | :--- | :--- | :--- |
| AXP192-CN | AXP192 enhanced single cell Li-battery PMIC, M5Stack copy, Chinese | DATASHEET V1.2, Aug.8 2011 | 791d72fb | 53 | 2026-11-25 |
| AXP192-EN | The same document, English, mindworks mirror | No revision string, cover 2010, last page 2011 | 6b3e45b9 | 53 | 2026-11-25 |
| AXP192-CHIPBAY | The same document, Chinese, distributor copy | No revision string | f01a4c4c | 53 | 2026-11-25 |
| AXP192-M5EN | AXP192 feature summary, the M5Stack English file | No revision string | 5271a672 | 4 | 2026-11-25 |
| AXP2101-DS10 | AXP2101 Single Cell NVDC PMU with E-gauge | 1.0, May 31 2021 | 42101198 | 64 | 2026-11-25 |
| AXP2101-DS14 | The same document | 1.4, Oct. 13 2022 | 9e51d16d | 63 | 2026-11-25 |
| AXP2101-DS01 | The same document | 0.1, April 28 2019 | 0745cc5e | 52 | 2026-11-25 |
| IP5306-REG | IP5306 寄存器文档, file named IIC_IP5306_REG_V1.4_cn.pdf | V1.21, copyright 2017 | 38f0f94a | 9 | 2026-11-25 |
| IP5306-DS131 | IP5306 power bank SoC datasheet | V1.31, copyright 2016 | 54c61806 | 11 | 2026-11-25 |
| IP5306-DS101 | The same document | V1.01, copyright 2016 | 8c059bdd | 10 | 2026-11-25 |
| BQ25896-DS | bq25896 I2C controlled 3 A fast charger | SLUSC76C, July 2015, revised May 2018 | f45ec93b | 65 | 2026-11-25 |
| BQ27220-TRM | BQ27220 Technical Reference Manual | SLUUBD4A, April 2016, revised November 2022 | 44779f22 | 91 | 2026-11-25 |
| TCA8418-DS | TCA8418 keypad scan IC | SCPS215G, September 2009, revised June 2018 | fdd74a30 | 53 | 2026-11-25 |
| BM8563-CN | BM8563 real time clock and calendar, Chinese, vendor original | V1.1 | a847c85a | 18 | 2026-11-25 |
| MPU6886-DS | MPU-6886 MotionTracking device | DS-000193 revision 1.1, released 03/29/2017, pre-production | 6751256f | 59 | 2026-11-25 |
| BMI270-DS | BMI270 datasheet | revision 1.6, March 2026, BST-BMI270-DS000-08 | 0dab4065 | 150 | 2026-11-25 |
| QMI8658A-DS | QMI8658A 6D IMU with motion co-processor | Rev D, QST-PD-B002-22 | edda798c | 78 | 2026-11-25 |

### axp192.md

| Fact | Document and revision | Page | Read |
| :--- | :--- | :--- | :--- |
| Section 9.10 is the host interface and interrupt section | AXP192-CN V1.2, AXP192-EN, AXP192-CHIPBAY | 28 in all three | 2026-11-25 |
| Section 9.11 Registers starts the register map | AXP192-CN V1.2, AXP192-EN, AXP192-CHIPBAY | 29 in all three | 2026-11-25 |
| REG 82H is ADC enable 1, REG 83H is ADC enable 2 | AXP192-EN | 41 | 2026-11-25 |
| REG 82H defaults to 83H and its eight channel bits | AXP192-EN | 41 | 2026-11-25 |
| 0xCC and 0x00 keep only the four channels furble uses | AXP192-EN | 41 | 2026-11-25 |
| REG 83H defaults to 80H, bit 7 temperature and bits 3:0 GPIO | AXP192-EN | 41 | 2026-11-25 |
| REG 84H defaults to 32H, bits 7:6 are 25, 50, 100 and 200 Hz | AXP192-EN | 41 | 2026-11-25 |
| EXTEN is bit 2 of REG 10H and bit 6 of REG 12H | AXP192-EN | 34 | 2026-11-25 |
| The REG 12H note maps bit 6 and bit 4 onto REG 10H bit 2 and bit 0 | AXP192-EN | 34 | 2026-11-25 |
| REG 28H is LDO2 bits 7:4 and LDO3 bits 3:0, 1.8 to 3.3 V, default CFH | AXP192-EN | 36 | 2026-11-25 |
| Off mode 27 uA and USB suspend 86 uA | AXP192-EN section 5 | 7 | 2026-11-25 |
| LDO2 and LDO3 supply 200 mA each and draw IQ 100 uA each | AXP192-EN section 5 | 8 | 2026-11-25 |

Two corrections to the pass on this page. It reported that section 5 has no
100 uA LDO2 or LDO3 figure. Page 8 does carry one, the `IQ` row, and page 7's
27 uA and 86 uA are whole-chip figures in a different table. The page now
separates the two instead of deleting the 100 uA, and it records that ILDO2 and
ILDO3 are the 200 mA output-current rows, not current draws.

### axp2101.md

| Fact | Document and revision | Page | Read |
| :--- | :--- | :--- | :--- |
| Slave address 0x68/0x69 in 8-bit form | AXP2101-DS10 1.0 | 18 | 2026-11-25 |
| Section 6.13 register, 6.13.1 list, 6.13.2 descriptions | AXP2101-DS10 1.0 | 29 and 31 | 2026-11-25 |
| Off state keeps only RTCLDO, about 40 uA on battery | AXP2101-DS10 1.0 | 19 | 2026-11-25 |
| REG 00 bit 5 VBUS good, bit 3 battery present | AXP2101-DS10 1.0 | 31 | 2026-11-25 |
| REG 01 bits 6:5 current direction | AXP2101-DS10 1.0 | 31 | 2026-11-25 |
| REG 34 bits 7:6 debug select, bits 5:0 vbat high | AXP2101-DS10 1.0 | 40 | 2026-11-25 |
| REG A4 battery percentage | AXP2101-DS10 1.0 | 57 | 2026-11-25 |
| REG 18 bit 3 gauge enable, default 1 | AXP2101-DS10 1.0 | 34 | 2026-11-25 |
| REG 80 bits 0 to 4 DCDC1 to DCDC5 enables | AXP2101-DS10 1.0 | 50 | 2026-11-25 |
| REG 90 bits 0 to 7 and REG 91 bit 0 | AXP2101-DS10 1.0 | 53 | 2026-11-25 |
| REG 92 to 97, 0.5 to 3.5 V, code 31 reserved | AXP2101-DS10 1.0 | 54 and 55 | 2026-11-25 |
| REG 18 bit 2 backup charge and REG 6A in prose | AXP2101-DS10 1.0 section 6.12.2 | 29 | 2026-11-25 |
| REG 6A bits 2:0 termination voltage, default 011b | AXP2101-DS10 1.0 | 50 | 2026-11-25 |
| REG 49 power key status, write 1 to clear | AXP2101-DS10 1.0 | 44 | 2026-11-25 |
| REG 41 bits 3:2 power key enable | AXP2101-DS10 1.0 | 42 | 2026-11-25 |
| REG 10 bit 0 soft power off, bit 1 restart | AXP2101-DS10 1.0 | 32 | 2026-11-25 |
| REG 90 bit 4 prints "aldo1 enable", a duplicate of bit 0 | AXP2101-DS10 1.0 | 53 | 2026-11-25 |
| REG 90 bit 4 prints "bldo1 enable" | AXP2101-DS14 1.4 | 53 | 2026-11-25 |
| REG 90 bit 4 field name is `bldo1_en` | AXP2101-DS01 0.1 | 44 | 2026-11-25 |
| Revision 1.2 revised reg 90 in chapter 6.13 | AXP2101-DS14 1.4 revision history | 2 | 2026-11-25 |
| Register 0x03 PMU CHIP ID is in the register list | AXP2101-DS01 0.1 | 19 | 2026-11-25 |
| 0x03 bits 7:6 chip_id_h, 5:4 chip_version, 3:0 chip_id_l, 01_0111 AXP2101 | AXP2101-DS01 0.1 | 22 | 2026-11-25 |
| Neither revision 1.0 nor 1.4 defines register 0x03 | AXP2101-DS10 1.0, AXP2101-DS14 1.4 | whole document | 2026-11-25 |

Two corrections to the pass on this page, both about pages rather than facts.
It gave 8.1 as page 18 and 8.2 as page 21 for the identity register. The
register list is page 19 and the register body is page 22; page 21 holds the 8.2
heading. It also gave the revision 0.1 `bldo1_en` field table as page 43, which
is page 44. The page numbers in axp2101.md are the ones read here.

### ip5306.md

| Fact | Document and revision | Page | Read |
| :--- | :--- | :--- | :--- |
| I2C protocol, 400 kHz, slave address 0xEA | IP5306-REG V1.21 | 1 | 2026-11-25 |
| The standard part has no I2C and the I2C build is a separate order | IP5306-REG V1.21 | 2 | 2026-11-25 |
| 0x70 bit 3 charge state and 0x71 bit 3 full charge, in the usage note | IP5306-REG V1.21 | 2 | 2026-11-25 |
| The chip measures no voltage or current, an external ADC is advised | IP5306-REG V1.21 | 2 | 2026-11-25 |
| SYS_CTL0 0x00, and the note that a disabled boost loses light-load shutdown | IP5306-REG V1.21 | 3 | 2026-11-25 |
| SYS_CTL2 0x02 bits 3:2 are 64, 16, 32 and 8 s | IP5306-REG V1.21 | 4 | 2026-11-25 |
| The register list is 0x00, 0x01, 0x02, 0x20 to 0x24, 0x70, 0x71, 0x72, 0x77 | IP5306-REG V1.21 | 3 to 8 | 2026-11-25 |
| 0x70 bit 3 charge_en | IP5306-REG V1.21 | 7 | 2026-11-25 |
| 0x71 bit 3 full charge flag | IP5306-REG V1.21 | 7 | 2026-11-25 |
| 0x72 bit 2 light load flag | IP5306-REG V1.21 | 7 | 2026-11-25 |
| 0x77 bits 2, 1, 0 key double, long and short, write 1 to clear | IP5306-REG V1.21 | 7 and 8 | 2026-11-25 |
| Conditional reproduction grant | IP5306-REG V1.21 section 4 | 9 | 2026-11-25 |
| ISTB 50 uA at VIN 0 V and VBAT 3.7 V | IP5306-DS131 V1.31, IP5306-DS101 V1.01 | 5 of 11, 5 of 10 | 2026-11-25 |
| The 100 uA row is IBAT in V1.31 and Input Quiescent Current in V1.01 | IP5306-DS131 V1.31, IP5306-DS101 V1.01 | 5 of 11, 4 of 10 | 2026-11-25 |
| TloadD 32 s at a continuous load below 45 mA | IP5306-DS131 V1.31, IP5306-DS101 V1.01 | 6 of 11, 5 of 10 | 2026-11-25 |
| The model selection table marks I2C present for IP5306 | IP5306-DS131 V1.31 | 3 of 11 | 2026-11-25 |
| The model selection table marks I2C present for IP5306 | IP5306-DS101 V1.01 section 5 | 3 of 10 | 2026-11-25 |
| Conditional reproduction grant | IP5306-DS131 V1.31 | 11 of 11 | 2026-11-25 |

Both licence findings are corrections. The corpus said that every vendor document
failed condition 1. These two grant conditional reproduction, so it did not, and
the blanket claim is now stated per page. The mirror rule is what blocks
committing these bytes, not the licence.

V1.31 has no numbered sections and no section 8 heading, so the page cites it by
page number only. V1.01 does have a section 8, which is why the older citation
was right for V1.01 and would have been wrong for V1.31.

### bq25896.md

| Fact | Document and revision | Page | Read |
| :--- | :--- | :--- | :--- |
| There is no section 5.3; I2C timing is section 8.6 | BQ25896-DS SLUSC76C | 12 | 2026-11-25 |
| Register maps start at section 9.4 with slave address 6BH | BQ25896-DS SLUSC76C | 32 | 2026-11-25 |
| REG00 IINLIM, 100 mA offset, 50 mA step, 100 mA to 3.25 A, default 500 mA | BQ25896-DS SLUSC76C | 32 | 2026-11-25 |
| REG05 IPRECHG and ITERM, 64 mA offset, defaults 128 mA and 256 mA | BQ25896-DS SLUSC76C | 37 | 2026-11-25 |
| REG07 bits 5:4 watchdog, 40 s default | BQ25896-DS SLUSC76C | 39 | 2026-11-25 |
| REG0B bits 4:3 CHRG_STAT | BQ25896-DS SLUSC76C | 43 | 2026-11-25 |
| Default mode, host mode, and the six registers a watchdog expiry keeps | BQ25896-DS SLUSC76C section 9.3.1 | 31 | 2026-11-25 |

One correction to the pass, recorded on the page. It gave REG05 as "IPRECHG and
ITERM 64 mA offset default 128 mA". Both fields have the 64 mA offset, and only
IPRECHG defaults to 128 mA; ITERM defaults to 256 mA. It also implied a watchdog
expiry rewrites every register. Section 9.3.1 keeps IINLIM, VINDPM, VINDPM_OS,
BATFET_RST_EN, BATFET_DLY and BATFET_DIS, so a host's input limit survives.

### bq27220.md

| Fact | Document and revision | Page | Read |
| :--- | :--- | :--- | :--- |
| Table 2-1 standard commands, two bytes each, low byte first | BQ27220-TRM SLUUBD4A | 17 | 2026-11-25 |
| Table 2-1 prints `Current()` with unit mAh | BQ27220-TRM SLUUBD4A | 17 | 2026-11-25 |
| Table 2-1 prints `RelativeStateOfCharge()` at 0x2C | BQ27220-TRM SLUUBD4A | 17 | 2026-11-25 |
| Voltage() 0x08 and 0x09 in mV | BQ27220-TRM SLUUBD4A section 2.6 | 22 | 2026-11-25 |
| BatteryStatus() 0x0A and 0x0B | BQ27220-TRM SLUUBD4A section 2.7 | 23 | 2026-11-25 |
| Table 2-6 BATTPRES bit 3 and DSG bit 0 | BQ27220-TRM SLUUBD4A | 23 | 2026-11-25 |
| Current() 0x0C and 0x0D signed, units are mA | BQ27220-TRM SLUUBD4A section 2.8 | 23 | 2026-11-25 |
| TimeToEmpty() 0x16 and 0x17, 65535 means not discharging | BQ27220-TRM SLUUBD4A section 2.11 | 23 | 2026-11-25 |
| StateOfCharge() 0x2C and 0x2D, headed StateOfCharge | BQ27220-TRM SLUUBD4A section 2.21 | 23 | 2026-11-25 |
| 7-bit address 1010101, 0xAA write and 0xAB read | BQ27220-TRM SLUUBD4A section 5.1 | 63 | 2026-11-25 |
| t(BUF) at least 66 us, individual 1-byte writes above 100 kHz | BQ27220-TRM SLUUBD4A section 5.3 | 63 | 2026-11-25 |
| No more than two standard commands per second or the watchdog resets the gauge | BQ27220-TRM SLUUBD4A section 5.3 | 64 | 2026-11-25 |

Both hardware-constraint rows move from REPORTED to VERIFIED, so the gap rows for
`ADDRESS` and `NO_ESTIMATE` are closed and the page's Citation gaps table is
empty. The two manual errors are the vendor's and are recorded as such rather
than as driver defects.

### tca8418.md

| Fact | Document and revision | Page | Read |
| :--- | :--- | :--- | :--- |
| 10 columns by 8 rows, 80 keys | TCA8418-DS SCPS215G section 8.1 | 15 | 2026-11-25 |
| The pin list has no address pin | TCA8418-DS SCPS215G section 5 | 3 | 2026-11-25 |
| Table 8 gives the fixed address 0x34 | TCA8418-DS SCPS215G section 8.6.1 | 25 | 2026-11-25 |
| Register map Table 9, FIFO at 0x04 to 0x0D | TCA8418-DS SCPS215G section 8.6 | 25 | 2026-11-25 |
| KEY_LCK_EC 0x03 bits 3:0 hold the FIFO count | TCA8418-DS SCPS215G Table 9 | 25 | 2026-11-25 |
| KP_GPIO1 to 3 at 0x1D to 0x1F, GPI_EM1 to 3 at 0x20 to 0x22 | TCA8418-DS SCPS215G Table 9 | 26 | 2026-11-25 |
| DEBOUNCE_DIS1 to 3 at 0x29 to 0x2B | TCA8418-DS SCPS215G Table 9 | 27 | 2026-11-25 |
| Table 1 Key Event Table for the keypad array | TCA8418-DS SCPS215G section 8.3.1.1 | 15 | 2026-11-25 |
| GPI events are decimal 97 to 114, R0 to R7 and C0 to C9 | TCA8418-DS SCPS215G section 8.3.1.2 | 16 | 2026-11-25 |
| Bit 7 of the event byte is press or release | TCA8418-DS SCPS215G section 8.3.1.2 | 16 | 2026-11-25 |
| The FIFO read procedure | TCA8418-DS SCPS215G section 8.3.1.3 | 16 | 2026-11-25 |
| INT is open drain and needs a pull-up | TCA8418-DS SCPS215G section 8.3.5 | 20 | 2026-11-25 |
| The debounce is a two-stage circuit on a nominal 50 us clock, up to 100 us, guarded at 120 us | TCA8418-DS SCPS215G note to section 6.5, and section 8.6.2.15 | 6 and 33 | 2026-11-25 |
| CFG 0x01 bit 7 is auto-increment | TCA8418-DS SCPS215G Table 11 | 36 | 2026-11-25 |
| No scan-rate register exists anywhere in Table 9 | TCA8418-DS SCPS215G | 25 to 28 | 2026-11-25 |

Two absences are recorded as facts because a reader would look for them: there is
no scan-rate register, and there is no interrupt polarity register.

### bm8563.md

| Fact | Document and revision | Page | Read |
| :--- | :--- | :--- | :--- |
| The vendor original is on Belling's site and every page is footed www.belling.com.cn | BM8563-CN V1.1 | all 18 | 2026-11-25 |
| The M5Stack copy is byte-identical, sha256 a847c85a on both | BM8563-CN V1.1 | n/a | 2026-11-25 |
| The GATEMODE LCSC URL returns text/html and no PDF | n/a | n/a | 2026-11-25 |
| 16 registers and the address map | BM8563-CN V1.1 Table 1 | 7 | 2026-11-25 |
| Control and status 1 at 00H, TEST bit 7, STOP bit 5, TESTC bit 3 | BM8563-CN V1.1 Table 3 | 7 | 2026-11-25 |
| Control and status 2 at 01H, TI/TP bit 4, AF bit 3, TF bit 2, AIE bit 1, TIE bit 0 | BM8563-CN V1.1 Table 4 | 8 | 2026-11-25 |
| Seconds 02H bit 7 VL, 1 means a power loss occurred | BM8563-CN V1.1 Table 6 | 9 | 2026-11-25 |
| Each alarm register has AE at bit 7 and AE=0 enables | BM8563-CN V1.1 Table 15 and section 2 | 10 | 2026-11-25 |
| Timer control 0EH, TE bit 7 and TD1/TD0 bits 1:0 | BM8563-CN V1.1 Table 21 | 11 | 2026-11-25 |
| TD1/TD0 give 4096, 64, 1 and 1/60 Hz | BM8563-CN V1.1 Table 22 | 12 | 2026-11-25 |
| The countdown period is n divided by the clock | BM8563-CN V1.1 Table 23 | 12 | 2026-11-25 |
| 7-bit address 0x51, 0xA2 write and 0xA3 read | BM8563-CN V1.1 section 1 | 1 | 2026-11-25 |
| Feature list says 1.5 to 5.5 V, both electrical tables say 1.8 to 5.5 V | BM8563-CN V1.1 | 1, 3 and 4 | 2026-11-25 |
| DC table gives 550 nA at 5.0 V and 250 nA at 3.0 V in one IDD1 row | BM8563-CN V1.1 | 3 | 2026-11-25 |
| The cover prints MAX1937 under the title | BM8563-CN V1.1 | 1 | 2026-11-25 |

One correction to the pass, recorded on the page. It reported the sleep current
as a vendor inconsistency between a 2021 catalogue and the datasheet. Both
figures are in the datasheet's own DC table, 550 nA at 5.0 V and 250 nA at 3.0 V,
in the same IDD1 row at 25 C with CLKOUT enabled. There is no inconsistency to
record, and the catalogue was not reopened.

The pass also gave table 22 as page 11. Table 21 is page 11 and table 22 is
page 12; the page cites 12.

### mpu6886.md

| Fact | Document and revision | Page | Read |
| :--- | :--- | :--- | :--- |
| Document number, revision 1.1 and release date 03/29/2017 | MPU6886-DS DS-000193 | 1 | 2026-11-25 |
| The document is pre-production and every page 2 to 59 is marked confidential | MPU6886-DS DS-000193 | 1 and 2 to 59 | 2026-11-25 |
| Section 5.1 wake-on-motion steps 1 to 8 | MPU6886-DS DS-000193 | 24 | 2026-11-25 |
| Slave address b110100X | MPU6886-DS DS-000193 section 6.2 | 25 | 2026-11-25 |
| Register map section 7, and the map has no 0x1F entry | MPU6886-DS DS-000193 | 29 | 2026-11-25 |
| Table 15 ends the register map at 0x7E | MPU6886-DS DS-000193 | 31 | 2026-11-25 |
| PWR_MGMT_1 0x6B and PWR_MGMT_2 0x6C | MPU6886-DS DS-000193 Table 15 | 30 | 2026-11-25 |
| ACCEL_CONFIG 0x1C bits 4:3 ACCEL_FS_SEL | MPU6886-DS DS-000193 section 8.17 | 37 | 2026-11-25 |
| The three WOM threshold registers and no mg per LSB | MPU6886-DS DS-000193 sections 8.20 to 8.22 | 39 and 40 | 2026-11-25 |
| INT_PIN_CFG 0x37 bit 5 LATCH_INT_EN | MPU6886-DS DS-000193 section 8.25 | 41 | 2026-11-25 |
| INT_STATUS 0x3A bits 7, 6 and 5 | MPU6886-DS DS-000193 section 8.28 | 42 | 2026-11-25 |
| ACCEL_INTEL_CTRL 0x69 bits 7 and 6 | MPU6886-DS DS-000193 section 8.36 | 46 | 2026-11-25 |
| WHO_AM_I 0x75 value 0x19 | MPU6886-DS DS-000193 section 8.43 | 49 | 2026-11-25 |

One correction and one vendor defect. WHO_AM_I is page 49, not the 48 the pass
gave. Section 5.1 step 4 tells the reader to set the threshold in
ACCEL_WOM_THR at 0x1F, which Table 15 does not define, while the three real
threshold registers are at 0x20 to 0x22; and the step list runs 5 then 7. The
driver follows Table 15, so the driver is right and page 24 step 4 is wrong.

The wake-on-motion threshold resolution stays UNVERIFIED, now for a stated
reason: sections 8.20 to 8.22 hold three lines each and give no scale, and no
other page in the 59 gives one. The 4 mg per count and the 64 mg equivalence are
not datasheet values.

### bmi270.md

| Fact | Document and revision | Page | Read |
| :--- | :--- | :--- | :--- |
| Revision 1.6, March 2026, BST-BMI270-DS000-08 | BMI270-DS | 1 | 2026-11-25 |
| CHIP_ID 0x00 resets to 0x24 | BMI270-DS section 5.2.1 | 75 | 2026-11-25 |
| INT_STATUS_0 0x1C and bit 6 any_motion_out | BMI270-DS section 5.2.28 | 82 | 2026-11-25 |
| INTERNAL_STATUS 0x21 | BMI270-DS section 5.2.33 | 85 | 2026-11-25 |
| FEAT_PAGE 0x2F | BMI270-DS section 5.2.39 | 87 | 2026-11-25 |
| FEATURES 0x30, feature page 1 label | BMI270-DS section 5.2.40 | 88 and 90 | 2026-11-25 |
| ANYMO_1 at 0x3C and ANYMO_2 at 0x3E with their fields | BMI270-DS section 5.2.40, any-motion feature page | 92 | 2026-11-25 |
| NOMO_1 at 0x30 and NOMO_2 at 0x32, feature page 2 label | BMI270-DS section 5.2.40, no-motion feature page | 93 | 2026-11-25 |
| Anymotion behaviour | BMI270-DS section 4.8.2 and Figure 1 | 43 and 44 | 2026-11-25 |
| INT1_IO_CTRL 0x53 bits 1, 2 and 3 | BMI270-DS section 5.2.58 | 109 | 2026-11-25 |
| INT_LATCH 0x55, 0x00 non-latched | BMI270-DS section 5.2.60 | 111 | 2026-11-25 |
| INT1_MAP_FEAT 0x56 | BMI270-DS section 5.2.61 | 111 | 2026-11-25 |
| INT_MAP_DATA 0x58 | BMI270-DS section 5.2.63 | 112 | 2026-11-25 |
| PWR_CONF 0x7C reset 0x03, adv_power_save bit 0 | BMI270-DS section 5.2.84 | 119 | 2026-11-25 |
| PWR_CTRL 0x7D | BMI270-DS section 5.2.85 | 120 | 2026-11-25 |

Row 8 of the page was wrong and is rewritten. The page said the any-motion
feature layout was described only by the SensorAPI. The datasheet carries it:
ANYMO_1 at 0x3C with a 13-bit duration in 50 Hz samples and per-axis select
bits, and ANYMO_2 at 0x3E with an 11-bit threshold from 0 to 1 g, default
0xAA = 83 mg, and four out_conf bits, on page 92. The no-motion block is on
page 93 in the same section. Nothing in the page now says the layout is API-only.

PWR_CTRL 5.2.85 page 120 closes the last open section number the page carried.

The 0.488 mg per count figure is arithmetic on two datasheet values, 83 mg and
0xAA, and the page labels it as a derivation rather than a quoted specification.

Four source comments and one file header still contradict the document:
`src/FurbleIMUMotionBMI270.cpp:39`, `:45`, `:62`, `:72` and the header at
`:17` to `:21`. They are firmware comment changes and are not in this PR.

### qmi8658.md

| Fact | Document and revision | Page | Read |
| :--- | :--- | :--- | :--- |
| The document is QST-PD-B002-22 Rev D on every page and the part is QMI8658A | QMI8658A-DS Rev D | 1 and footer | 2026-11-25 |
| SA0 selects 0x6A or 0x6B | QMI8658A-DS Rev D | 10 | 2026-11-25 |
| WHO_AM_I 0x00 reads 0x05, REVISION_ID 0x01 reads 0x7C | QMI8658A-DS Rev D section 5.2 | 29 | 2026-11-25 |
| CTRL8 0x09 and CTRL9 0x0A field tables | QMI8658A-DS Rev D section 5.3 | 33 | 2026-11-25 |
| STATUS1 0x2F bits 7, 6 and 5 | QMI8658A-DS Rev D section 5.5 | 35 | 2026-11-25 |
| CTRL9 protocol | QMI8658A-DS Rev D section 5.10.1 | 39 | 2026-11-25 |
| CTRL_CMD_CONFIGURE_MOTION is 0x0E | QMI8658A-DS Rev D section 5.10.2 | 40 | 2026-11-25 |
| The motion engine takes two CTRL9 passes of CAL parameters | QMI8658A-DS Rev D section 9.4 | 57 | 2026-11-25 |
| CTRL8 bits 1, 2, 3 and 6 enable the engines and select the pin | QMI8658A-DS Rev D section 9.5 | 57 | 2026-11-25 |
| STATUS1 bits 7:5 report the three motion events | QMI8658A-DS Rev D section 9.6 | 57 | 2026-11-25 |
| I2C address selection, 0x6A or 0x6B | QMI8658A-DS Rev D section 14.3.1 | 75 | 2026-11-25 |

Rev D is QMI8658A. The pass reports that Rev A is also QMI8658A and older, and
that Rev 0.9 copies are QMI8658C, a different part. This corpus opened neither,
and the page says so rather than repeating it as a fact.

## What this second pass changes

| Area | Change |
| :--- | :--- |
| Ten component pages | Every fact reopened in its document, states promoted to VERIFIED where the fact was read, and the per-fact revision, page and date recorded above |
| Four wrong premises | Removed from axp192.md, axp2101.md, bm8563.md and qmi8658.md: each page had claimed a document was missing or unreachable when it was neither |
| Four licence claims | Corrected on ip5306.md and in the corpus README, where "every document failed condition 1" was false for both Injoinic documents |
| Three corrections to the pass | Recorded with evidence on axp192.md, bq25896.md and bm8563.md rather than copied into the pages as if the pass were the document |
| Three vendor defects | MPU-6886 section 5.1 step 4 names a register that does not exist, and its steps run 5 then 7. AXP2101 REG 90 bit 4 was mislabelled until revision 1.2. BM8563 prints MAX1937 on its own cover |

No document was stored. Apart from the corpus README and this plan, nothing
outside `docs/components/` changed.

## Review record, PR 475

The independent review returned changes required. Two blocking findings and ten
smaller ones were applied, and the branch head was amended.

| # | Finding | Resolution |
| :--- | :--- | :--- |
| 1 | IP5306 standby marked VERIFIED at 100 uA | The page now lists 50 uA standby at VIN 0 V and VBAT 3.7 V, and 100 uA input quiescent at VIN 5 V as a separate row. The unresolved-pins bullet is gone. |
| 2 | The index Worst state column contradicted 16 pages | The column is removed. The README states that each page's own tables are authoritative and that a fact row carries exactly one state, with nuance in the preceding cell. Every compound state cell was split. |
| 3 | BMI270 revision and section numbers | Revision 1.6 recorded. INT1_IO_CTRL is 5.2.58, INT_LATCH 5.2.60, PWR_CONF 5.2.84, and the internal status register is 0x21. PWR_CTRL's section is left unnamed. |
| 4 | AXP2101 DLDO rows | DLDO1 is REG 0x90 bit 7 and DLDO2 REG 0x91 bit 0, matching `lib/hal/AXP2101.cpp:60-61`. |
| 5 | W5500 duplicate row number and wrong reference link | Rows renumbered 1 to 4, and the reference now points at v1.1.0. |
| 6 | Gate hardening | Citation shapes are validated, gap rows must have the four documented columns, `#define` and `const` forms are read, `lib/hal` is walked recursively, the block rule is bounded at 16 lines, and an inline citation counts. Tests cover each, plus a pinned token count. |
| 7 | The bit mask test did not exercise a REG_ name | It now uses `REG_BIT = 1U << 5`, and separate cases cover inline citations and the block rule. |
| 8 | `POLL_US` sat inside a cited block | Marked as a driver choice in its own comment. |
| 9 | SH1106 asserted an address and a contrast command with no document | Both rows are UNVERIFIED plan claims, and the README gap list says so. |
| 10 | Sitronix quote punctuation and case | Quoted exactly as the document prints it, in `st7789.md`, the README and this plan. |
| 11 | SX1262 cited a blank line | The row now points at `lib/hal/SX1262.cpp:11` to `:18`. |
| 12 | Gate not wired, CLAUDE.md out of date | Wired into `pre_review.py` and the pre-review workflow, `lib/hal/CLAUDE.md` describes the enforced rules, and the gate count is 11. |

## Review record, PR 475, second round

The second review returned two blocking findings and six hardening items, and
all eight are applied. It also caught figures this plan had carried since the
first round: 213 lines for the tool, nine cases of 108 lines for the test file,
2099 lines for the corpus and 162 unverified bullets. The tree it read had 265
lines, 20 cases of 239 lines, 2124 lines and, on a recount, 148 bullets. The
numbers below are the tree this pass leaves behind, which also includes the
five cases the hardening added; the third round refreshes the four counts in row
7.

| # | Finding | Resolution |
| :--- | :--- | :--- |
| 1 | BMI270 section numbers still wrong in five places | The page rows at `docs/components/bmi270.md:25`, `:26` and `:29` now read INT_STATUS_0 5.2.28, INTERNAL_STATUS 5.2.33, FEAT_PAGE 5.2.39, FEATURES 5.2.40, INT1_MAP_FEAT 5.2.61 and INT_MAP_DATA 5.2.63. The five driver comments at `src/FurbleIMUMotionBMI270.cpp:27`, `:29`, `:34`, `:48` and `:50` carry the same numbers, the correction table has all ten rows, and the follow-up references are `:39`, `:45` and `:62`. |
| 2 | IP5306 rows cited the front page for names and conditions it does not carry | Both rows cite section 8 Electrical Characteristics: the control system table for I STB 50 uA at VIN 0 V and VBAT 3.7 V, and the charging system table for the 100 uA Input Quiescent Current at VIN 5 V not switching. The "Opened in this change" cell says so and records that the front page pairs the two figures the other way. The 45 mA light-load threshold is facts row 1b at section 8, and the bullet that claimed it was unread is gone. |
| 3 | `COVER_MAX_LINES` could be raised unnoticed | The bounded-cover test asserts the literal 16 before it counts, and the cited and failing counts as literals, so a raised bound fails the test. |
| 4 | The definition regexes were loose | Optional parentheses, any integer suffix, optional whitespace after `#`, brace initialisers, and every declarator on a line rather than only the first. The gate reads them with `finditer`. A shift or an or of names after the value is still not a definition. |
| 5 | A URL in a string could fabricate an inline citation | A line is split into code and comment by a scanner that only accepts a `//` outside a string or character literal, and blanks the literal bodies in the code half. A definition inside a string is not a token, and a URL cannot cite. Both are tested. |
| 6 | The gate read fewer suffixes than the format gate, and only validated pages with a driver | It reads the eight suffixes `tools/check_format.py` formats, pinned by a test against that module, and it validates the `Citation gaps` table on every page under `docs/components/`, so a page with no driver cannot carry a malformed table. |
| 7 | Stale figures in this plan and the gate missing from two pre-review lists | The tool is 339 lines, the test file 26 cases and 327 lines, the corpus 27 pages plus the index at 2167 lines, and the unverified bullets count 147, four to seven per page. The datasheet gate is named in the `pre_review.py` lists in `CLAUDE.md` and `CONTRIBUTING.md`. |
| 8 | Nothing said what the gate cannot check | `docs/components/README.md` states that the gate checks citation shape and never document identity: a plausible number passes, and only reading the document settles the section, the revision and the fact. |

## Review record, PR 475, third round

The third review returned PASS with five non-blocking follow-ups, and all five
are applied. The branch head is amended, not stacked.

| # | Follow-up | Resolution |
| :--- | :--- | :--- |
| 1 | `src/FurbleIMUMotionBMI270.cpp:72` still said 5.2.45 for PWR_CTRL while `bmi270.md` declined to confirm that section | `:72` is named in the open-comment list at `docs/components/bmi270.md:53` to `:58` and in plan 333 follow-up item 6, and the page's unverified bullet records the number. The comment is unchanged, so the source diff stays five comments. |
| 2 | The status line and the What landed table omitted the five comment changes under `src/` | The status names the file, and the table gives it a row with the five corrected sections and the reason the gate does not scan it |
| 3 | IP5306 row 1b cited section 8 with no table name and no load condition | Row 1b names the control system table, the Automatic load detection time T loadD row, the 32 s and the continuous load below 45 mA condition, and page 5 of 10 |
| 4 | The 45 mA was REPORTED and not read here on the page while `tools/power-model/board-currents.yaml:475-480` recorded it as datasheet | It is a datasheet value: V1.01 section 8 control system table, read for this change, so row 1b is VERIFIED. The Code cell carries `pmic.auto_off_threshold` and the page states that the YAML's `confidence: datasheet` was right. The stale unverified bullet is gone, which also makes the second-round row 2 record true. |
| 5 | The gate skipped a token whose initialiser is not a bare, parenthesised or braced integer literal, so a bitwise or, a cast or a wrapped value was invisible | `lib/hal/CLAUDE.md` states the trade-off and names it the one fail-open path, plan 333 repeats it, and `test_non_literal_initialiser_is_out_of_scope` asserts the skip for all three forms |

## Line-reference audit

A fourth pass checked every backticked `file:line` under `docs/components/`
against the tree at the branch head. Two hundred and sixty references resolve
to a line that exists and is not blank, after eleven edits in seven pages and
the reconciliation with #479; three more name a pinned library or ESP-IDF file outside the tree. Nine
pointers resolved to a blank line, which a reader cannot follow: the SX1262
`SLEEP_SETTLE_US` row, the SX1262 and AXP2101 header blocks, the `HalBus.h` seam
range, both `FurbleIR.cpp` ranges, the `esp32-s3.md` RMT comment row and the
u-blox frame row. The `esp32-s3.md` USB row named `src/FurbleCrashRecord.h`,
which has only ever lived at `include/FurbleCrashRecord.h`. The u-blox rows cited
`GpsUbx.h:31` to `:34` for the sync bytes and the checksum, which are at `:26`
to `:29`.

The audit is a script over the pages, not a gate, so nothing stops the next line
shift. Two loose spans are accurate only as regions: `ublox-mia-m10q.md` cites
`GpsUbx.h:6` to `:16` for the statement that both modules run the same ROM, which
is at `:15` to `:16`, and both Espressif pages cite `FurbleIR.cpp:226` to `:296`
for the whole RMT implementation. A follow-up should teach
`tools/check_datasheet_citations.py` to resolve the page references, which is the
one part of the page format nothing checks today.

## Rebase onto master after #479

The branch was queued against b711a44a. #479 landed first and changed three
`lib/hal` drivers the corpus describes, so the rebase onto bebf7caf was both
textual and factual. One file conflicted, `lib/hal/CLAUDE.md`: master's bus
pacing, enable-what-you-decode, wait-for-a-level and fake-bus rules follow the
citation paragraphs, and the corpus pages, the gate description and the
conditional PDF rule stay, so the file carries both sets with one PDF rule.

The reconciliation then followed the merged code. The SX1262 page cites the new
`waitBusy` helper and the two budgets, and `SLEEP_CONFIRM_BUDGET_US` is a new
citation gap because no tabulated value bounds how soon BUSY goes high. The
BQ27220 page records the pacing #479 implemented, so the old "no delay hook" gap
becomes the adapter's obligation to override a no-op default. The hazard table
marks both drivers, and the AXP2101 comment reference moved with the file. The
gate counts move with the tree: 34 tokens, 30 cited, 4 documented gaps, 0
errors.
