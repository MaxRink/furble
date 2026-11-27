# 382 - GNSS datasheet verification

Status: documentation only, plus two comment-only corrections in
`src/FurbleGPS.cpp` and `lib/furble/protocol/GpsCasic.h`. No behaviour, setting,
build or hardware change.

Stacked on [333](333-datasheet-corpus.md), which is PR 475 and not yet merged:
this branch starts at `origin/docs/datasheet-corpus`, and it has to be rebased
onto the newest head of that branch before it is pushed, because more than one
agent is pushing to it.

Related: [333](333-datasheet-corpus.md) (the corpus and its gate),
[321](implemented/321-ublox-m10-driver.md) (the M10 driver),
[98](98-power-optimization-audit.md) (the power model),
[00](00-hardware-experiments.md) (Experiment B, the rail-cut cold start).

## Why

Three pages in the corpus carried a gap that was no longer true. `at6668.md`
said the headline fact was that no AT6668 datasheet could be confirmed, and
every row on it was UNVERIFIED. `ublox-max-m10s.md` and `ublox-mia-m10q.md` left
the M10 module documents unopened, so their protocol rows were CITED and their
bullets stood in for a revision check. The power documents inherited the
consequence: `docs/power/datasheet-register.md` and `docs/power/gps-imu.md`
justified a 23 mA AT6558 proxy with "no AT6668 datasheet is findable", and
`docs/power/opportunity-matrix.md` pinned the MON-HW decode to "CASIC 2.14.2",
which is the AID-HUI section.

A research pass located the documents on 2026-11-25. This change verifies each
of its claims against the bytes, records the facts on the pages that own them,
and writes down what is still open. The pass's own page numbers were a mix of
printed and PDF numbering; where that mattered the correction is below.

## Documents read

Every document was downloaded and read page by page on 2026-11-25. The two
sha256 prefixes the pass reported match the bytes read here, so this is the same
copy it saw.

| Document | Revision, date, length | sha256 | Printed vs PDF page |
| :--- | :--- | :--- | :--- |
| AT6668/AT6668U datasheet, DS-AT6668 | V1.4, 2023.09.04, 19 pages | `7013e3771492c0e2b76122b3779d50e402d34799a14c638aa97945663faec18a` | identical |
| AT6668 datasheet, vendor-domain copy | V1.0, 2022.04.20, 18 pages | `fadb62b9046957606b4ae675e23cf153277bfa7053ea7d4575bff353bb05669b` | identical |
| CASIC BDS/GNSS receiver protocol specification, document number R6 | V6.3.2, 2025-06-24, 150 PDF pages, 144 printed | `61e2567dcf04b0d8e4d5ec6c9c841f8ac0caa846f5b54645a64b7704d800fe04` | printed = PDF - 6 |
| CASIC multimode receiver protocol specification, 内部文档 | V4.2.0.3, 2020.01.06, 85 pages | `8db63d179ee3929014c5895fa2b7b3e63340f2407753aaf55aaa4fde8114152a` | identical |
| ATGM336H-6N module manual | V1.1, 2023.01.11, 17 pages | `7b1c10193709d7600e0234445a1c15603bf3443e14780ecde4818091ec2eda32` | identical |
| M5Stack Unit GPS v1.1 | no revision, "Update Time: 2026-09-04", 6 pages | `1151dc81573c9d9085cb9dd9e95054f3e95cd6af57c14131f85337717f455102` | identical |
| u-blox M10 SPG 5.10 interface description | UBX-21035062 R03, 27-Jun-2023, 176 pages | `b20859d86b043458494a3473c28f03ec8ad1e9e9d6a61ea2d220aa0f739248b6` | identical |
| MAX-M10S data sheet | UBX-20035208 R08, 30-Jan-2026, 24 pages | `e36d9c8585809c0dc486804d2634c66cd1888ad71af0f28ce0b830ffa8df6bec` | identical |
| MAX-M10S integration manual | UBX-20053088 R05, 28-Apr-2026, 102 pages | `5a7510ef84f7e2757c57e362a25e3c16bcf8c80af5a4f70790c2e51032dbcd13` | identical |
| MIA-M10Q data sheet | UBX-22015849 R08, 30-Jan-2026, 27 pages | `bd8e04d2520251d70565a5187369db84b4e979fc14cf0a0b47c1adfdc2131a43` | identical |
| MIA-M10Q integration manual | UBX-21028173 R05, 30-Jan-2026, 99 pages | `679a1d0ce1859922dfbee5f75b5565be82149e7294e862aafb0a63ddff5597f0` | identical |
| u-blox M10 ROM 5.10 release notes | UBX-22001426 R02, 17-Jan-2023, 15 pages | `f1c326f9c6d5f1b2443e8c4b93ee61bc56f6d4c3d5bd45ad01babd8206f44741` | identical |

The pass gave only the directory for the release notes. That document is at
`https://content.u-blox.com/sites/default/files/u-blox-M10-ROM-5.10_ReleaseNotes_UBX-22001426.pdf`,
which does not follow the naming of the other three, and the names that do
return the site's 404 page.

## Verification record

What each fact now rests on. The page itself carries the same section and page
next to every row, which is what the corpus vocabulary asks for.

### AT6668, the chip

| Fact | Rests on | State |
| :--- | :--- | :--- |
| Part, order codes, package | DS-AT6668 V1.4 section 1.5 page 1 (QFN40, 5 x 5 x 0.75 mm), section 1.6 page 2 (AT6668 -6N-22, -6N-32, -6N-74 and AT6668U -6N-74 with their default constellations) | VERIFIED |
| Pinout, 40 pins plus exposed pad | DS-AT6668 V1.4 section 2.1 page 3, section 2.2 pages 3 to 4 | VERIFIED |
| Configuration is by UART command, the command set is not in this document | DS-AT6668 V1.4 section 5.12 page 15 | VERIFIED |
| 37 mA typical tracking with BDS, GPS and GLONASS | DS-AT6668 V1.4 section 4.3 page 9, repeated in section 1.2 page 1 | VERIFIED |
| 8 uA battery backup, 30 uA sleep with ON_OFF low, 2.5 mA antenna detect, 45/50/65 mA antenna short-circuit limit, 160 ms reset, 26 MHz TCXO | DS-AT6668 V1.4 section 4.3 page 9 | VERIFIED |
| Three-state antenna supervision | DS-AT6668 V1.4 section 3.4 page 6 | VERIFIED |
| VBAT is 1.4 to 3.6 V on the chip, 1.5 to 3.6 V at 10 uA on the module | DS-AT6668 V1.4 section 4.2.1 page 8; ATGM336H-6N sections 2.4 page 9 and 2.5 page 10 | VERIFIED |
| A backup supply is optional and enables the hot start | DS-AT6668 V1.4 section 5.11 page 14 | VERIFIED |
| Whether this unit has a backup supply | nothing. See "Left open" | UNVERIFIED |

### CASIC protocol, both revisions

| Fact | Rests on | State |
| :--- | :--- | :--- |
| `BA CE` framing and the 32-bit checksum | R6 section 3.2 page 47 (PDF 53) and V4.2.0.3 section 2.2 page 36, the same construction in both | VERIFIED |
| `$PCAS10` is a restart command, 0 hot, 1 warm, 2 cold, 3 factory | R6 section 2.7.9 page 42; V4.2.0.3 section 1.6.8 page 31 | VERIFIED |
| `$PCAS12` is a timed re-power after N seconds, N up to 65535 | R6 section 2.7.11 page 44. V4.2.0.3 section 1.6.9 page 32 is a heading with no format, no parameters and "Air530Z 不支持该指令" | VERIFIED |
| `$PCAS02` takes 1000, 500, 200 or 100 ms in R6 and 1000, 500, 250, 200 or 100 in V4.2.0.3 | R6 section 2.7.3 page 35; V4.2.0.3 section 1.6.3 page 26 | VERIFIED |
| `$PCAS03` carries 15 numbered fields in R6 and 19 in V4.2.0.3, and R6 recognises at most 14 sentences | R6 section 2.7.4 page 36 with its note; V4.2.0.3 section 1.6.4 page 27 | VERIFIED |
| `$PCAS04` is one decimal mode 1 to 7 in V4.2.0.3 and a hex band bitmask of one to three arguments in R6 | V4.2.0.3 section 1.6.5 page 28; R6 section 2.7.5 pages 37 to 38 | VERIFIED |
| The two `$PCAS04` encodings coincide for 1 to 7, because R6 bits 0, 1 and 2 are GPS, BDS B1I and GLONASS and their decimal sums are the V4.2.0.3 modes | read from the same two sections | VERIFIED |
| `$PCAS11` sets the motion mode, `dy` 0 to 7, and is absent from V4.2.0.3 | R6 section 2.7.10 page 43; the V4.2.0.3 section 1.6 list has no CAS11 | VERIFIED |
| `$PCAS07` turns one sentence on or off and is absent from V4.2.0.3 | R6 section 2.7.8 page 41; no CAS07 in V4.2.0.3. R6's own command summary on page 6 omits it | VERIFIED |
| `$PCAS15` renumbered its system ids: 2 is BDS 1-32 in V4.2.0.3 and GLONASS in R6, 4 is SBAS and then QZSS, 5 is QZSS and then SBAS | V4.2.0.3 section 1.6.11 page 34; R6 section 2.7.12 page 45 | VERIFIED |
| The binary classes moved: NAV 0x01, TIM 0x02, RXM 0x03 in V4.2.0.3 became NAV2 0x11, TIM2 0x12, RXM2 0x13 in R6, and the old three are not documented there | V4.2.0.3 section 2.6 pages 38 to 39; R6 section 3.6 pages 49 to 51 | VERIFIED |
| MON-HW is 0x0A 0x09 with a 56-byte payload and identical offsets in both revisions, and `antStatus` is encoded differently: 0 is "initialising" in V4.2.0.3 with 4 for open, and "open" in R6 with no fourth value | V4.2.0.3 section 2.13.2 page 81; R6 section 3.15.9 page 139 (PDF 145) | VERIFIED |
| The R6 copy is the AT6668 unit's document: M5Stack lists it under "Datasheets" on the unit product page, and its own `GPTXT` examples carry `HW=AT6668` and `IC=AT6668-6X-32-...` | product page fetched 2026-11-25; R6 sections 2.6.2 page 20 and 2.6.3 page 21 | VERIFIED |

### u-blox M10, both modules

| Fact | Rests on | State |
| :--- | :--- | :--- |
| RXM-PMREQ is 0x02 0x41 with a version byte, a duration, the `backup` and `force` flags and the wake sources | IFD 3.16.6 page 116 | VERIFIED |
| UPD-SOS is 0x09 0x14 with the poll, create, clear and two answer forms | IFD 3.19.1 page 122 | VERIFIED |
| The configuration layers are RAM, BBR, flash and default, selected in the VALSET `layers` byte by bits 0, 1 and 2 | IFD 4.3 page 125 and the `layers` field in IFD 3.10.5 page 55 | VERIFIED |
| The MAX-M10S UART default is 9600 baud, called out as differing from the firmware default | IM-MAX section 2.1.4 pages 11 to 12, section 3.2.1 pages 24 to 25 with Table 16; DS-MAX section 5.3 page 16 Table 20 | VERIFIED |
| `MSGOUT_NAV_PVT_UART1` is 0x20910007 and defaults to 0 | IFD 4.9.11, Table 19 page 138 for the key, Table 69 page 166 (caption page 168) for the default | VERIFIED |
| The MAX-M10S outline is 10.1 x 9.7 x 2.5 mm at 0.5 g | DS-MAX Table 21 page 17 | VERIFIED |
| The "no external flash" claim | IM-MAX Table 2 page 9 lists RAM and BBR only, and IFD 4.3 page 125 makes the flash layer conditional. Nothing asserts the absence, so the row says it is an inference | UNVERIFIED |
| PSM combined with BeiDou B1C is refused, and the CS pin is not a wake source in PSM | RN-M10 section 7 page 15, section 7.2 in particular | VERIFIED |
| The MIA-M10Q UART default is 38400 baud, 9600 in safe boot | DS-MIA section 5.3 page 18 Table 21 with footnote 36; IM-MIA section 3.2.1 page 26 | VERIFIED |
| The MIA-M10Q default output is NMEA only | DS-MIA Table 21 page 18 | VERIFIED |
| The MIA-M10Q module default is GPS, Galileo and B1I with QZSS and SBAS, and B1I excludes B1C and GLONASS L1OF | DS-MIA section 1.3 page 6 with Table 4 and footnote 13; IM-MIA section 2.1.3 page 11 | VERIFIED |
| The ROM defaults differ from the module default: BDS and GLONASS constellations off, BDS B1C and GLO L1 on, BDS B1I off | IFD configuration defaults, Table 79 page 171 | VERIFIED |
| The fifteen constellation and signal key ids | IFD 4.9.21 page 152, Table 42 | VERIFIED |

## Corrections to the pass's claims

Each of these was checked and kept, kept with a narrower reading, or rejected.
The page numbers are the only systematic problem.

| Claim as handed over | What the document shows | Resolution |
| :--- | :--- | :--- |
| "R6 section 3.2 page 53" for `BA CE` framing | Section 3.2 is printed page 47, PDF page 53 | Kept, cited as printed 47 and PDF 53 |
| "R6 section 2.7.11 page 44" for `$PCAS12` | Printed page 44, PDF page 50 | Kept |
| "R6 section 2.7.5 page 43" for `$PCAS04` | Section 2.7.5 is printed page 37, PDF page 43 | Kept with both numbers; the claim used the PDF page |
| "R6 section 2.7.10 page 43" for `$PCAS11` | Printed page 43, PDF page 49 | Kept |
| "R6 3.15.9 page 145" for MON-HW | Printed page 139, PDF page 145 | Kept with both numbers |
| "`$PCAS12` absent from V4.2.0.3" | The heading exists, section 1.6.9 page 32, with no format, no parameters and "Air530Z 不支持该指令" | Rejected as written. The page says the semantics are documented only in R6 |
| "`$PCAS02` accepts 100 to 1000 ms only" | Both revisions list discrete values, not a range, and they differ by the 250 ms step | Rewritten as the two documented sets |
| "`$PCAS03` 15 fields in V6.3.2 and 19 in V4.2.0.3" | True of the numbered parameter fields, which count the message id; 14 and 18 sentence flags | Kept, with the counting stated |
| "`$PCAS10` selects a working mode" (the row that was there) | Both revisions call it a restart, 0 hot, 1 warm, 2 cold, 3 factory | Rejected. The row is rewritten; the code was already right |
| "IM 2.1.4 page 12 and 3.2.1 page 25" for the 9600 default | Section 2.1.4 opens on page 11 with the baud note on page 12; section 3.2.1 opens on page 24 with the default sentence and Table 16 on page 25 | Kept, with the span stated, and the data sheet Table 20 added as a second source |
| "IFD 4.9.11 page 133" for the `MSGOUT_NAV_PVT_UART1` default | Section 4.9.11 opens on page 133, the key row is on page 138 and the default is in the defaults table on page 166 | Kept and refined |
| "DS-MIA Table 5 page 6" for the constellation set | Table 5 is the A-GNSS services table. The constellation set is the section 1.3 text on page 6 and Table 4 | Rejected. Row rewritten, and the old citation is recorded in a "Correction" section on the page |
| "B1I is off at the SPG 5.10 default" | The module documents say B1I is on by default, and the IFD ROM defaults say off | Split into two rows so both are true |
| "AT6668 datasheet section 2.1 and 2.2 page 3" | Section 2.1 is page 3, section 2.2 runs from page 3 to page 4 | Kept, with the span stated |
| "the module manual gives VBAT 1.5 to 3.6 V at 10 uA" | ATGM336H-6N section 2.5 page 10; the chip itself is 1.4 to 3.6 V, section 4.2.1 page 8 | Kept, both figures recorded |
| "V1.0 of 2022.04.20 is the only vendor-domain copy" | Confirmed, and it differs from V1.4 on two numbers a reader could trip on: 0.85 mm thick, 32 s cold start | Kept, and the differences are recorded |
| "the 23 mA AT6558 proxy is retired" | Correct in the docs; the model input still holds it | Recorded in the docs and left in the model on purpose, see below |

## Facts left open, and why

- **The unit's back-up supply.** DS-AT6668 section 5.11 page 14 says a coin
  cell or supercapacitor on VBAT enables the hot start, and that VBAT may float
  when the hot start is not wanted. ATGM336H-6N section 2.5 page 10 gives the
  module VBAT input as 1.5 to 3.6 V at 10 uA. Unit GPS v1.1 page 3 lists no
  backup supply and its pinmap on page 4 exposes only 5 V, GND and the two UART
  lines. M5Stack publishes a 23 s cold start, the chip and the module documents
  repeat 23 s, and this project measured 107.7 s after a 60 s rail cut. The
  document that would settle it is the unit schematic M5Stack links from the
  product page, which was not opened. So the row stays UNVERIFIED and records
  the conflict rather than a conclusion.
- **Which protocol revision the unit's ROM follows.** The 2025 specification and
  the 2020 specification disagree about `$PCAS04`, `$PCAS15`, the binary classes
  and the antenna status encoding. The 2023 chip datasheet documents none of
  them, and nothing on the wire states a protocol revision. Both numbers are
  recorded on the page. Two rows were written so that neither reading looks
  confirmed: the `$PCAS04` values furble sends are safe under both, and the
  `antStatus` value 0 is not.
- **The factory default baud rate of the chip.** No document read states one.
  The unit is published at 115200, `$PCAS01` selects 4800 to 460800, and the
  comment in `lib/furble/protocol/GpsCasic.h` calling 9600 the CASIC factory
  default is not supported by either revision. It was left alone: it describes
  the ladder's assumption, not a datasheet fact, and changing it is the GPS
  driver's business, not this pass.
- **The default sentence set at power-up.** `$PCAS03` and `$PCAS07` are both
  documented as setters, and neither revision says what is on before a host
  touches it.
- **The in-circuit `$PCAS12` standby current.** The 30 uA sleep figure is for
  ON_OFF low, which is a different path from a `$PCAS12` standby. The model's
  estimate is unchanged.
- **The 250 ms `$PCAS02` step** is documented only by V4.2.0.3, and `RATE_MS`
  does not offer it.
- **The MIA-M10Q outline.** Section 6 page 19 is a drawing plus a 0.07 g
  weight; the dimensions are in the drawing and were not read.
- **Whether either M10 module has an external flash.** Both pages now say the
  claim is an inference from the omission, not a quoted statement.

## The firmware audit's findings

A parallel firmware audit (`fix/gnss-doc-audit`, plan 377) reported facts that
land on these pages. Each was checked in the code before it was used.

| Finding | Checked where | What these pages now say |
| :--- | :--- | :--- |
| The tree wrote only 8 of the 14 `$PCAS03` rate fields, so the RMC and GGA prune never turned DHV, LPS, UTC or GST off | `src/FurbleGPS.cpp:3093` to `:3106` on this branch sends `PCAS03,1,0,0,0,1,0,0,0` and the two shorter forms, and the documents make an empty field keep the existing rate | `at6668.md` row 7 is new: the empty-field rule, the 14 shared positions, the consequence, and the three corrected bodies on `fix/gnss-doc-audit`. `gps-imu.md` no longer quotes the eight-field prune |
| Both revisions define 14 rate fields at the same positions, R6 section 2.7.4 and V4.2.0.3 section 1.6.4, and an empty field keeps the rate | Read field by field in both sections: GGA, GLL, GSA, GSV, RMC, VTG, ZDA, ANT, DHV, LPS, reserved, reserved, UTC, GST, with V4.2.0.3 appending res3, res4, res5 and nTIM after them | `at6668.md` rows 6 and 7 |
| `$PCAS04` stays valid across both revisions for 1 to 7 because they are valid hex digits and GPS, BDS and GLONASS sit in bits 0 to 2 in both; 8 would diverge, invalid on V4.2.0.3 and Galileo E1 on V6.3.2 | V4.2.0.3 section 1.6.5 page 28 and R6 section 2.7.5 page 37, compared value by value | `at6668.md` row 9, and the `gps-imu.md` transmit-path table and step 3 |
| `$PCAS10` is used only by `GPS::restart()` and the diagnostics hot-restart button | `src/FurbleGPS.cpp:1267` and its only caller, `src/FurbleUI.cpp:10215`, which passes 0 | `at6668.md` row 3 |
| The AT6668 datasheet has no protocol chapter, so R6 is the authority for the command set, and the variant table at section 1.6 page 2 decides which signals a part has | DS-AT6668 section 5.12 page 15 and section 1.6 page 2 with footnote 1, which makes 6N-22 a single-system BDS part | `at6668.md` row 19, and the part paragraph now reads the variant table rather than calling it unreadable |
| The 107.7 s figure is a re-fix after a 60 s rail cut, not the documented 23 s cold start, and no code timeout derives from it | `plans/00-hardware-experiments.md` line 188; `include/FurbleGPS.h:533` `ACQUIRE_TIMEOUT_MS` is 30 s and is armed only while no valid sentence has arrived | `at6668.md` row 18 and its unverified section, `gps-imu.md`, `board-power.md`, `opportunity-matrix.md` and `tools/power-model/README.md` all call it a re-fix |

Two consequences worth stating for whoever merges this. First, `fix/gnss-doc-audit`
also edits `src/CLAUDE.md`, `lib/furble/CLAUDE.md` and `lib/furble/protocol/GpsCasic.h`;
the hunks there do not overlap this branch's, but the line numbers on the
`GpsCasic.h` rows of `at6668.md` will move by the length of its new
`SentenceSet` block, which is why every citation there names the symbol as well
as the line. Second, once that branch lands, `at6668.md` row 7's code cell and
the `setSatelliteCapture()` bullet are the two lines to refresh.

### Pages this PR does not own

The same two claims, still stale outside this diff, for the owner of those files:

| File | What is stale |
| :--- | :--- |
| `docs/power/gps-profile.md` lines 15 and 35 | Quotes the eight-field `PCAS03` bodies (`1,0,0,0,1,0,0,0` and `1,0,1,1,1,0,0,0`); they need the fourteen-field forms |
| `docs/hardware-catalog.md` line 93 | "cold about 108 s without backup in the field reports" calls the re-fix a cold start |
| `docs/power/soc.md` line 1236 | "107.7 s GPS cold refix" |
| `docs/power/measurement.md` lines 644 to 648 | The G06 acceptance text wraps the 107.7 s re-fix in a "cold-start window" budget |

`docs/power/summary.md` line 93 already says "107.7 s cold re-fix" and needs no
change, and the 14-field bodies in `docs/power/gps-imu.md` are fixed here because
that file is in this diff.

## The power model: what changed and what did not

The documented AT6668 figure is 37 mA typical with BDS, GPS and GLONASS, at
3.3 V. The ATGM336H-6N manual gives the module as 42 mA typical at 3.3 V, and
M5Stack gives the unit as 31.64 mA at the 5 V Grove input. The retired proxy was
23 mA from the AT6558 datasheet, capped at 25 mA by the older ATGM336H-5N31
manual of 2016.

What changed here: `docs/power/datasheet-register.md` has an AT6668 row with the
37 mA figure and its section, the AT6558 row says it is superseded as the proxy,
`docs/power/gps-imu.md` and `docs/power/board-power.md` cite 37 mA where they
cited 23 mA, and `docs/components/at6668.md` carries the per-fact record.

`tools/power-model/board-currents.yaml` was **not** changed, and it needs to be.
`_shared.gps_unit_v11.module_tracking_3v3` still reads

```
    module_tracking_3v3:
      value_ma: 23
      min: null
      max: 25
      source: https://dratek.cz/docs/produkty/1/1045/at6558.pdf
      confidence: estimated
      note: "AT6558 datasheet typ (23 mA BD+GPS) as proxy for the AT6668; capped by the module manual 25 mA"
```

The correct replacement is `value_ma: 37` with the AT6668 data sheet as the
source and `confidence: datasheet`, and the `max` of 25 has to go: it comes from
a different module's manual and it is below the chip figure. That was left for
its own change for three reasons.

1. The file is a model input, not a document. Changing it moves the GPS term in
   every board row, including `connected-gps-active-30s` and
   `gps-duty-standby-cycle`, which are compared two-sided against the checked-in
   baselines under `tools/power-model/baseline/`. A 23 to 37 mA step is about a
   60 percent rise in that term, well past the default 10 percent band, so the
   sim power gate would fail until the baselines are regenerated. That is a
   model re-fit with its own measurement gate in
   [98](98-power-optimization-audit.md), not a citation fix.
2. PR 482 (`fix/sim-hw-align`) already holds that file for the IP5306
   light-load provenance. Two changes in one input file in one stack is how a
   rebase loses a number.
3. A raised current with no measurement behind it would make every documented
   saving in `docs/power/` look larger than it is, in the optimistic direction,
   which is the direction the power docs are careful about.

So the docs now state what the receiver draws and the model still states what it
used to assume. `docs/components/at6668.md` row 14 names the stale key so the
next reader cannot miss the disagreement.

## The IP5306 45 mA provenance

Nothing here touches it. PR 482 replaces "32 s below 45 mA" with the register
reading in `SYS_CTL2` bits 3:2, 32 s at the power-on default of `01` and 64 s at
the `11` M5Unified writes, in both `docs/power/datasheet-register.md` and
`tools/power-model/board-currents.yaml`. This change edits the AT6668 and AT6558
rows of the same table and leaves the IP5306 row alone, so the two do not
conflict. If the rebase does land on top of 482, the AT6668 row is a whole-row
addition and the AT6558 edit is confined to its own limit and lane-status cells.

## Files changed

| File | Change |
| :--- | :--- |
| `docs/components/at6668.md` | Rewritten: the part, package and pinout are VERIFIED, six protocol facts replace the UNVERIFIED placeholders, the `$PCAS04`, `$PCAS07`, `$PCAS11`, `$PCAS15` and binary-class differences are new rows, and the 37 mA figures replace the proxy |
| `docs/components/ublox-max-m10s.md` | Rows 7, 8, 9, 11 and 12 promoted, the layers and baud rows cite both documents, row 10 narrowed, the errata row added, RN-M10 added to the documents |
| `docs/components/ublox-mia-m10q.md` | The `Table 5` citation for the constellation set replaced with section 1.3 and Table 4, the B1I claim split, the 38400 baud row promoted and the key ids added, plus a "Correction" section |
| `docs/components/README.md` | The AT6668 index row names its documents and the known-gaps list says which gap closed and what remains |
| `docs/power/datasheet-register.md` | An AT6668 row at 37 mA, and the AT6558 row no longer the proxy |
| `docs/power/gps-imu.md` | 37 mA in the reference load table, the G-09 evidence and the datasheet list; the G-21 MON-HW citation and the `antStatus` split; the false "there is no UBX path" paragraph replaced with the two-backend description; the eight-field prune replaced with the fourteen-field body; the two `$PCAS04` encodings stated |
| `docs/power/board-power.md` | The same 23 to 37 mA correction in the RL-01 citation and the datasheet table and link list, and the 107.7 s figure relabelled a re-fix |
| `tools/power-model/README.md` | The GPS section no longer says no AT6668 datasheet is findable; it records the documented 37 mA, why the yaml still holds 23, and that the 107.7 s is a re-fix |
| `docs/power/opportunity-matrix.md` | P-093 cites `2.13.2` page 81 and R6 `3.15.9` page 139 instead of "2.14.2", and P-163's risk cell names the re-fix |
| `docs/console-commands.md`, `docs/wiki/Console-Commands.md` | The `gps monhw` and `gps platform` paragraphs cite the right sections and the `antStatus` split |
| `src/FurbleGPS.cpp` | Comment only at the `$PCAS11` fallback: the numbering, the R6 section, and that V4.2.0.3 has no PCAS11 |
| `lib/furble/protocol/GpsCasic.h` | Comment only at the MON-HW struct: the right sections, and the `antStatus` encoding difference between revisions |
| `src/CLAUDE.md`, `lib/furble/CLAUDE.md` | Both directories are touched, so both guides now point at the per-revision CASIC record and at the `antStatus` split |
| `plans/README.md`, this file | The record |

No PDF is committed and none may be. See the redistribution paragraph on each
page: the 2020 specification prints 内部文档 on every body page, the 2023
datasheet and the module manual carry no licence statement at all, and the 2025
specification copy is a vendor mirror, which the corpus rule forbids committing
even where the licence would allow it.

## Gates

`python3 tools/pre_review.py --base origin/master` passes: 11 gates. The host
was running several parallel agent sessions at a load average near 20 and the
aggregate run lost two subprocesses to `SIGKILL` (`exit -9`, once on
`git diff --check` and once on the scanner unit tests), so each gate was also
run on its own, which is the state recorded here:

| Gate | Result |
| :--- | :--- |
| whitespace, `git diff --check origin/master...HEAD` | clean |
| diff hygiene, `check_pr_hygiene.py` | PASS, 0 findings |
| settings contracts | PASS, 0 FAIL and 6 WARN, none on a file this PR touches |
| clang-format 21.1.2 | PASS, three changed C and C++ files formatted |
| C++ safety | PASS, 8 files, 0 errors and 0 warnings |
| CI workflow contract | PASS |
| simulator scenario manifest | PASS |
| simulator hardware coverage | PASS |
| coupling ratchet | PASS, clean |
| datasheet citations | PASS, 27 cited, 4 page gaps, 0 errors of 31 tokens |
| scanner unit tests | PASS, all seven modules |
| `sh sim/scripts/check-doc-tokens.sh` | PASS |

No build was run: no firmware source changed except two comments, and no
behaviour, setting or default moved.
