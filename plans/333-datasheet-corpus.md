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

Every document failed condition 1. Two are decisive: the Sitronix panels carry
"Sitronix Confidential The information contained herein is the exclusive
property of Sitronix and shall not be distributed, reproduced, or disclosed in
whole or in part without prior written permission of Sitronix.", and the
X-Powers
AXP2101 carries "Copyright Shenzhen X-Powers Technology Co., Ltd. All Rights
Reserved." The TI, Semtech, u-blox, NXP and Bosch documents carry copyright
notices with no redistribution grant. The M5PM1, XL9555, BM8563, AXP192, ST7735S
and ILI9341 copies are silent or distributor mirrors.

The zero-PDF outcome is therefore the rule working, not an unfinished task.
`docs/components/README.md` holds the `Committed documents` table with a `none`
row and states that documents without a redistribution licence are cited, not
stored. A future PDF needs all four conditions met and a row added to that table.

## Facts still unverified by component

| Group | Pages | Open items |
| :--- | :--- | :--- |
| No document found at all, or found but not opened | AT6668, SH1106, CST9217, QMI8658 | 6, 4, 6 and 5 items. For AT6668: the `$PCAS` argument ranges, the 108 s cold start, the missing backup rail and the baud rate. DS-AT6668 V1.4 was located and its section 4.3 page 9 gives the supply current, reported through an independent pass; no copy was opened for this corpus. Nothing on those pages is a register fact. |
| Second source only | BM8563 | 6 items. The Belling document is not retrievable, so the facts rest on a GATEMODE copy and on PCF8563 family behaviour. |
| Document reached, body not | AXP2101, XL9555, PCF8563, ST7789, ILI934x, IP5306 | AXP2101 subsections 6.13.2.1 to 6.13.2.88 and the REG 90 bit 4 defect body; XL9555 sections 9.1 and 9.2; the PCF8563 timer section number; the panel sleep timing sections; the IP5306 register document V1.21, which was not retrieved and has no URL. |
| Datasheet not opened | BMI270, W5500 timing, ST7735S, MPU-6886 sections 8.20 to 8.22, the M10 module data sheets and integration manuals | 6, 6, 4, 7 and 5 items. The section numbers there are CITED from the source comments, not reopened. |
| TRM not opened | ESP32, ESP32-S3 | 6 items each: UART clock sources, the RMT memory block, the RMT APB lock, the LED PWM clock tree, USB Serial/JTAG reset behaviour, and the current tables. |
| Board or policy facts | M5PM1, AXP192, BQ25896, CO5300, CST816, TCA8418, BQ27220, SX1262 | Package and pinout for almost every part, the M5PM1 register section numbers and the 0xA1 value, the AXP192 ADC bit assignments, the BQ25896 register-level behaviour, the CO5300 brightness command, the CST816 register chapter, the TCA8418 addresses and FIFO layout, the BQ27220 Table 2-6 bits and the 0x55 address, the SX1262 Table 13-1 and 13-2 encodings. |

147 bullets in total, four to seven per page. No errata document was found for
any part, and no page claims one.

## Latent hazards the verification found

None reach a board today: no file under `src/`, `include/` or `components/`
includes any of the four `lib/hal` headers. They are recorded so the first board
that wires a driver does not inherit them silently.

| Hazard | Location | State |
| :--- | :--- | :--- |
| BQ27220 pacing: `read()` asks the bus for the 66 us gap before every packet after the first, and the hook's default is a no-op, so a firmware adapter must override it | `lib/hal/BQ27220.cpp:41-53`, `lib/hal/HalBus.h:52` | Driver fixed in #479, adapter override latent |
| SX1262 BUSY timeout leaves the radio in STDBY_RC at 0.6 mA rather than 160 nA, and the driver has no retry. Both transitions are waited for inside budgets since #479 | `lib/hal/SX1262.cpp:31-33`, `:46` | Latent, no caller |
| The AXP2101 driver reads REG 90 bit 4 as BLDO1 where the vendor table labels it aldo1 twice; the comment at `lib/hal/AXP2101.cpp:47-49` is load bearing | `lib/hal/AXP2101.cpp:47` to `:49`, `:50` | Latent, comment must stay |
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

1. Find an SH1106, CST9217 or QMI8658 document. Any one of them turns a page
   from plan-only into a citation. Open DS-AT6668 V1.4 itself rather than
   resting on the reported current.
2. Open the two TRMs for the ESP32 and ESP32-S3 clock, RMT and USB facts.
3. Read the XL9555 section 9.1 and 9.2 tables, the AXP2101 register bodies and
   the PCF8563 timer section.
4. Fix the three live hazards in their own PRs.
5. Correct the 32 s claim in the three files once the IP5306 64 s value is
   accepted.
6. Correct the three BMI270 section numbers the source comments still carry at
   `src/FurbleIMUMotionBMI270.cpp:39`, `:45` and `:62`, the PWR_CTRL section
   5.2.45 the comment at `:72` names without a confirmed section, and the four
   section numbers the page still takes from those comments.

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
