# Board-level document gaps

Status: compiled 2026-11-27 against master `1d666b43`. This page answers one
question per board: does the vendor publish a board-level drawing, and if not,
what first-party evidence stands in for it and what stays unproven. The
document identities, revisions, page counts, byte sizes and SHA-256 values are
in [section 5 of the datasheet register](datasheet-register.md#51-board-level-schematic-documents).
This page carries the judgement, the register carries the artifacts.

"Board-level drawing" means a production schematic for the assembled board: a
full drawing set with the nets, the parts and their values. It is a different
artifact from a chip datasheet, a product page, a pin map or an example sketch,
and the four are never interchanged here.

## 1. Evidence classes

| Class | What it is | What it can carry | What it cannot carry |
| :--- | :--- | :--- | :--- |
| Schematic | A vendor drawing set, cited by revision and page | Nets, part values, pin numbers, rail names | Nothing it does not draw, such as an unpopulated option |
| Pin map | A vendor table or image of GPIO to function | Pin assignments, and the trims a function exists on | Net names, parts, values, rail sources |
| Vendor code | A factory example or library the vendor maintains | Which rail a vendor's own firmware enables for a peripheral, and at what voltage | Confidentiality, errata, an unpopulated option, or the wiring itself |
| Vendor statement | A maintainer's written answer | The vendor's position on a compatibility question | A drawing; it is not a controlled document |
| FCC exhibit | A filing attachment | A drawing or block diagram when the file is reachable | Anything when the index lists it as metadata only |

## 2. State by board

Every board on the candidate list and every supported board is here. A board
with a schematic may still be in this table, because a trim of it has none.

| Board | Board-level schematic | What stands in when it does not | What the substitute settles | Still open |
| :--- | :--- | :--- | :--- | :--- |
| LilyGO T-Beam Supreme, L76K trim | Yes. Carrier drawing in three sheets, revision V3.0, plus the core board at V3.0 and V3.1 | not needed | Module TXD on pin 2 reaches the `GPS_RX` net through a 100 ohm series resistor and module RXD on pin 3 reaches `GPS_TX` through a second, 1PPS on pin 4 reaches `GPS_PPS` through a series 1N4148, WAKE_UP on pin 5 comes from GPIO7, V_BCKP on pin 6 comes from the `VRTC` net, and pin 7 is marked RESERVED and wired to the pin 8 net through a 0 ohm link (radio and GNSS sheet, revision V3.0) | Whether the L76K trim leaves pin 7 unpopulated, and whether that 0 ohm link is fitted on an L76K assembly |
| LilyGO T-Beam Supreme, MAX-M10S trim | No drawing of its own. See section 3 | The L76K carrier drawing, the vendor compatibility statement, the vendor pin map and the factory example code | That the vendor considers one drawing to cover both trims, that GPIO7 is documented as L76K-only wake-up, and that the GPS rail is ALDO4 at 3300 mV while the backup rail is VBACKUP at 3300 mV | V_IO (MAX-M10S pin 7) is named by no vendor document as a supply. The drawing shows that pad tied to the GPS rail through a 0 ohm link, which is the only candidate path, and section 3 records why that is a reading and not a certification |
| LilyGO T-Deck | Yes, one sheet, title block empty except the date 2023/6/1 | not needed | The board as drawn | Nothing for a board furble does not port |
| LilyGO T-Deck Plus | Yes, two GPS card variants: MIA-M10Q, dated 24-03-29, and L76K, dated 24-08-12 | not needed | Which receiver each card carries, and that both exist under the Plus | Which variant a given unit ships with is a purchase question, not a document one |
| LilyGO T-LoRa Pager | Yes, five sheets, Rev V1.0 | not needed | That a five-sheet set exists for the Pager and carries the GNSS block; the receiver module name appears on the drawing | The printed date is a template default, so the drawing date is UNVERIFIED. The BQ25896 and XL9555 parts the component pages record were not re-read against this drawing in this pass |
| LilyGO T-Watch Ultra | Yes, five sheets, Rev V1.0 | not needed | That a five-sheet set exists for the Ultra and carries the GNSS block; the receiver module name appears on the drawing | The printed date is a template default, so the drawing date is UNVERIFIED. The CO5300 panel and the AXP2101 backup path were not re-read against this drawing in this pass |
| Waveshare ESP32-S3-Touch-AMOLED-1.75 | Yes, three sheets, no title block | not needed | The AXP2101 rail set, the TCA9554 expander and an LC76G GNSS block for the `-G` variant | Which parts are populated on the non-`-G` variant, since the drawing covers both |
| Waveshare ESP32-S3-Touch-AMOLED-1.8 | Yes, one sheet, no title block | not needed | The board as drawn | The drawing has no revision field, so a reissue cannot be told from the original |
| Waveshare ESP32-S3-Touch-AMOLED-2.06 | Yes, three sheets, no title block | not needed | The board as drawn | As the 1.8 |
| M5Stack CoreS3 | Yes, seven sheets, no title block | not needed | The AXP2101 rail page, and the parts the component pages record | Nothing; the firmware is driven by M5Unified, not by a register table |
| M5Stack Dial | Yes, two sheets, no title block | not needed | The board as drawn | The file name carries no version |
| M5Stack Cardputer | Yes, two sheets plus a one-sheet keyboard base, no title block | not needed | The board as drawn | The file names carry no version, and the PMIC part was not identified in this pass |
| M5Stack Cardputer-Adv | Yes, four sheets, no title block | not needed | That sheet 1 has a charger and a boost converter and no AXP part, and that the TCA8418 keyboard scanner sits on sheet 2 | The file name carries v1.0 and an export stamp; the drawing has no revision field |
| M5Stack StickS3 | Yes, four sheets, no title block, file name V0.6 | not needed | The M5PM1 rail tree on sheet 1: PM1_VIN, PM1_CHG, SYS, L0, L1, L2, L3A and L3B, with the ESP32-S3-PICO-1-N8R8 IO map beside it | The sheet states a date and an empty revision field, so V0.6 is a file name |
| M5Stack StickC | **No.** Only a 2019 rebuild pin map | The rebuild pin map, which is a block diagram with net names and no part values; the FCC filing 2AN3WM5STICKC lists a Schematics exhibit of 537.1 KB that the mirror serves as metadata only | Which net reaches which block on the AXP192, the USB-UART bridge, the ESP32, the LCD, the IMU, the RTC and the HAT header | Every part value and every resistor divider, including the IR LED series resistance and GPIO35's three open-drain sources that the register already lists as unverified |
| M5Stack StickC Plus | Yes, one sheet, no title block | not needed | The AXP192 rail use the Plus documentation describes | Nothing for furble beyond what the vendor pin map already gave |
| M5Stack StickC Plus2 | Yes, three sheets, no title block | not needed | The board with no PMIC: the charger and the divider on GPIO38 | The divider values are drawn but not recorded in this pass, so the 100 uA class estimate stays a range |
| M5Stack Core2, V1.0 | Yes, one sheet, no title block | not needed | The AXP192 revision of the board | Nothing |
| M5Stack Core2 v1.1 | Yes, five sheets, no title block | not needed | That the AXP2101 sits on sheet 4 of the v1.1 drawing while the V1.0 drawing has an AXP192, which is the revision change the register called an open document gap | Whether a given unit in the field is v1.0 or v1.1 is a hardware question resolved by the PMIC probe the firmware already does, not by a document |
| M5Stack M5PM1 and X-Powers AXP2101 reference designs | Not needed: every board furble ships these on publishes a board-level schematic | The M5PM1 chip manual has no application circuit at all, so the StickS3 drawing is the only board source. The AXP2101 datasheet has one, section 7.1 Typical Application, Figure 7-1, page 58 of revision 1.0 | The AXP2101 figure gives the vendor's own wiring for the part, which is a cross-check for a board that publishes nothing | A board that publishes no schematic and fits one of these parts would need the reference figure read pin by pin, which no current target does |
| Heltec WiFi LoRa 32 V3 | Yes, three drawing revisions: V3, V3.1 and V3.2 | not needed | The revisions differ, which is why all three are registered | Which revision a given unit is. The documents do not say what changed between them |
| Heltec WiFi LoRa 32 V4 | Yes, two drawing revisions: V4.2 and V4.3 | not needed | The board at each revision | As the V3: the drawings carry no changelog |
| Heltec Wireless Tracker V2 | Yes, one sheet, rev V2.3 by file name, and it is a front-end drawing | The V2.3 pin map image sits beside the drawing on the vendor resource server | The radio front end and the control nets | Whether the single sheet is the whole board or the front-end section of a larger set. Nothing on the sheet says, so the set's completeness is UNVERIFIED |

## 3. The T-Beam Supreme MAX-M10S trim

This is the case plan 318 opened and could not close. It recorded "there is no
M10S schematic" and left V_IO on module pin 7 and the V_BCKP path unverified
(`plans/318-next-board-battery-gps.md`, the GNSS table and the bench item 6).
What this pass adds, and what it does not:

1. **The carrier drawing covers the trim, by the vendor's own statement.** In
   issue 225 a LilyGO maintainer answers a request for the M10S schematic with
   "It is designed to be compatible, but the hardware designer did not write the
   M10 name on it. They are the same" (comment 2025-03-04). That is a vendor
   statement, not a drawing, and it is the basis for treating the L76K drawing
   as the M10S drawing.
2. **The drawing shows one pin, one net and one link for the disputed pad.**
   On the radio and GNSS sheet of revision V3.0, module pin 7 is labelled
   RESERVED, and the pad is wired through a 0 ohm link to the same net as pin
   8, the module VCC pin. The link's value is printed `0R` and no reference
   designator is readable beside it at 1600 dpi. The pin 8 net is labelled
   `A_LDO4`.
3. **The factory example names that net's rail and voltage.** Under
   `T_BEAM_S3_SUPREME` the vendor's own `LoRaBoards.cpp` sets
   `XPOWERS_ALDO4` to 3300 mV and enables it under the comment `//gps`, and sets
   `XPOWERS_VBACKUP` to 3300 mV under `// GNSS RTC PowerVDD`. So `A_LDO4` is
   the AXP2101 ALDO4 GPS rail at 3.3 V, and the `VRTC` net on pin 6 is the
   AXP2101 backup rail at 3.3 V.
4. **What that makes newly verifiable.**
   - V_IO supply path: on this board the pad a MAX-M10S uses for V_IO is tied
     to the GPS VDD rail at 3.3 V through a 0 ohm link. This is the first
     statement of the path in any document, and it is read off the drawing and
     the vendor code, both first party. It replaces "what supplies V_IO is
     undocumented" with a named path.
   - V_BCKP path: the drawing's `VRTC` net and the factory code's
     `XPOWERS_VBACKUP` at 3300 mV agree, so the backup supply plan 318 marked
     `[inferred]` for the M10S trim is now supported by a drawing plus vendor
     code, and stops being a bare inference.
5. **What stays open, and why.**
   - V_IO is still **UNVERIFIED as a supply**. No document states that a
     MAX-M10S is fitted to that pad, that the link is populated, or that V_IO draws
     from it. The drawing names an L76K, and a 0 ohm link to the GPS rail is
     consistent with a dual-footprint design rather than proof of one. The
     check is continuity from module pin 7 to ALDO4 on an M10S unit, plus a
     UART exchange with the module powered from that rail. No document can
     replace it.
   - Whether the 0 ohm link is fitted on the M10S assembly is a population
     question. The drawing gives its value and no population note.
   - The pin 7 pad's L76K function is "RESERVED", so anything the L76K datasheet
     says about that pin says nothing about the M10S. The two pinouts are
     different parts that share a footprint, which is exactly why the drawing
     alone cannot settle it.
   - The vendor pin map marks GNSS wake-up as L76K only, and on the M10S pin 5
     is EXTINT rather than a wake input. Whether GPIO7 drives EXTINT usefully
     on the M10S trim is **UNVERIFIED**; the drawing shows GPIO7 on pin 5.

## 4. Facts this pass changed

| Fact | Before | Now | State |
| :--- | :--- | :--- | :--- |
| T-Beam Supreme M10S V_IO supply | "undocumented, including what supplies V_IO" (plan 318) | The pad is tied to the GPS rail `A_LDO4` through a 0 ohm link, and that rail is ALDO4 at 3300 mV per the vendor example code | path named, supply UNVERIFIED on hardware |
| T-Beam Supreme M10S V_BCKP | `[inferred]` from the L76K sheet | `VRTC` on pin 6 of the shared drawing, and `XPOWERS_VBACKUP` at 3300 mV in the vendor code | documented for the drawing, UNVERIFIED for the M10S assembly |
| T-Beam Supreme M10S existence of a drawing | "there is no M10S schematic" | No drawing names the M10S. The vendor states the L76K drawing is the same board | vendor statement, not a document |
| Core2 v1.1 PMIC | "fits an AXP2101 under the same board ID", AXP2101 datasheet called an open gap | Sheet 4 of the v1.1 drawing carries the AXP2101; sheet 1 of V1.0 carries the AXP192 | VERIFIED from the drawings |
| StickS3 rail tree | product page numbers only | Sheet 1 of the K150 drawing is the M5PM1 rail tree, L0 to L3B, with the SoC IO map | VERIFIED from the drawing |
| Cardputer-Adv power | the catalog calls it a BLE plus display board with no GNSS | No PMIC on the drawing: a charger and a boost converter, and the TCA8418 on sheet 2 | VERIFIED from the drawing |
| M5StickC full schematic | no vendor schematic, rebuild pin map only | Still no vendor schematic. The FCC filing index lists a Schematics exhibit of 537.1 KB and a Block Diagram of 91.8 KB, both marked metadata only | UNVERIFIED, the file was not reachable |
| FCC exhibits in general | "no claim is made about schematics or internal photos in any of these filings" | The per-ID index pages do list Schematics exhibits, with sizes, for the StickC, StickC Plus, StickS3 and Core2. Every one of those rows carries the mirror's metadata-only flag, and the FCC attachment endpoints still answer 403 | index read 2026-11-27, file UNVERIFIED |
| M5PM1 reference design | assumed available as a fallback | The chip manual has no application circuit and no schematic section | VERIFIED by searching the manual for application, typical, circuit, schematic and reference |
| AXP2101 reference design | cited for registers only | Datasheet revision 1.0 section 7.1 and Figure 7-1, page 58, is a one page typical application | VERIFIED |

## 5. Boards with no schematic

Exactly one board on the target list has no board-level schematic at all:

- **M5Stack StickC (the original `m5stick-c` environment).** The vendor
  documentation page links one drawing, the 2019 rebuild pin map, which is a
  block diagram: AXP192, USB-UART, ESP32, LCD, IMU, RTC and HAT blocks with net
  names and no part values. The FCC filing 2AN3WM5STICKC contains a Schematics
  exhibit of 537.1 KB and a Block Diagram of 91.8 KB; fccid.io lists both with
  its metadata-only flag, and `https://fcc.report/FCC-ID/2AN3WM5STICKC` and the
  FCC OET search endpoint both returned HTTP 403 on 2026-11-27. The exhibit is
  therefore known to exist and is not reachable from here.

One trim has no schematic of its own, with a stand-in that section 3 describes:
the **LilyGO T-Beam Supreme MAX-M10S**.

Everything else on the list, including all three Waveshare AMOLED sizes, all
four M5Stack Cardputer and Dial variants, the M5StickS3, all three StickC
family members except the original, both Core2 revisions and all three Heltec
families except the original WiFi LoRa 32 V1 and V2, publishes a drawing.

## 6. What would close each open row

| Open row | Closes with |
| :--- | :--- |
| T-Beam M10S V_IO supply | Continuity from module pin 7 to the ALDO4 rail on an M10S unit, and a UART exchange with the module on that rail |
| T-Beam M10S V_BCKP | The same unit, backup rail measured while the 18650 is removed |
| T-Beam M10S GPIO7 to EXTINT | A scope or a UBX `CFG` attempt on the M10S unit with GPIO7 toggled |
| T-Beam 0 ohm link population | A photograph of the M10S assembly, or the continuity check above |
| StickC part values | The FCC Schematics exhibit, fetched from the FCC database directly rather than through a mirror |
| Pager and Ultra drawing dates | A vendor reissue with a filled date field, or a statement from LilyGO |
| Heltec revision differences | A vendor changelog per revision, which the resource server does not carry |

## 7. Redistribution

No document on this page is committed. The licence findings, per vendor, are in
the register's [section 5.1.5](datasheet-register.md#515-licences-and-why-nothing-is-committed);
the summary is that only the LilyGoLib repository carries a grant that
plausibly reaches a schematic, and the two drawings in it are cited only.
