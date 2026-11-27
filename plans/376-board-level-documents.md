# 376 - Board-level documents for the supported and planned boards

Status: research and document register. This plan adds no firmware, board,
driver or support claim. No hardware was bought, flashed or measured. Every
document was retrieved and hashed on 2026-11-27 against master `1d666b43`. The
artifacts are in
[section 5.1 of the datasheet register](../docs/power/datasheet-register.md#51-board-level-schematic-documents);
the judgement and the open rows are in
[board-document-gaps.md](../docs/power/board-document-gaps.md).

## Motivation

The register and the component corpus were strong on part datasheets and thin
on board-level documents. Section 5 of the register held eleven vendor rows, of
which two were schematics cited by URL and never opened, and the coverage
verdict said outright that no board schematic PDF was in the tree. The gap that
mattered most is plan 318's: it ranks the LilyGO T-Beam Supreme with the
MAX-M10S first for the next board and records that no schematic exists for that
trim, leaving V_IO on module pin 7 and the V_BCKP path unverified. A datasheet
cannot settle either, because both are board nets.

The second reason is that a decent number of facts the register records as
`vendor page` or `unverified` are drawn in a schematic the vendor does publish.
The Core2 v1.1 AXP2101 revision change, the StickS3 rail tree, the Cardputer-Adv
power path and the M5StickC part values all fall in that class.

## Method

1. Enumerate, do not guess. LilyGO keeps its drawings in the product
   repositories, so the file list came from the GitHub trees API for
   `Xinyuan-LilyGO/LilyGo-LoRa-Series`, `Xinyuan-LilyGO/T-Deck` and
   `Xinyuan-LilyGO/LilyGoLib`, filtered on `schematic`, `pdf` and `pinmap`.
   M5Stack and Waveshare publish through product documentation pages, so those
   pages were fetched and their PDF links extracted. Heltec's resource server
   lists its own directories, so `resource.heltec.cn/download/<product>/` was
   enumerated rather than probed.
2. Retrieve each file, then record what the bytes are: the page count from
   `pdfinfo`, the byte size, and the SHA-256 over the served bytes.
3. Read the identity, not just the filename. Every PDF was checked for a title
   block. Where there is one, the revision and date come from it. Where there
   is none, that is recorded, and the revision is attributed to the sheet
   caption when one exists and to the file name otherwise.
4. Label the evidence class. A schematic, a pin map, vendor code and a vendor
   statement are four different things and the register keeps them apart. Plan
   318 established that the LilyGO factory examples are usable evidence for
   rail and pin facts; this plan uses that allowance and tags every such row
   `vendor code`.
5. Read only first-party sources. No mirror was used for a document that
   matters, and the two mirrors that were consulted (fccid.io, and the FCC
   endpoints themselves) are recorded with what they did and did not serve.

What failed, and is recorded so nobody repeats it blind:

| Endpoint | Result on 2026-11-27 |
| :--- | :--- |
| `https://fcc.report/FCC-ID/2AN3WM5STICKC` | HTTP 403 |
| `https://apps.fcc.gov/oetcf/eas/reports/GenericSearch.cfm` | HTTP 403 |
| Aliyun OSS bucket listing on either M5Stack host | `AccessDenied`, so the schematic files can only be found through the documentation pages |
| fccid.io search | still 403, as the register recorded on 2026-11-26; the per-ID pages are served |

## What exists

28 schematic PDFs were read and registered, for 20 boards. The count of
drawings exceeds the count of boards because some boards publish several: three
for the T-Beam Supreme set, three for the T-Deck family, six for the three
Heltec families. Short form, by family:

| Family | Drawings found |
| :--- | :--- |
| LilyGO T-Beam Supreme | 3: the carrier in three sheets (power, SoC, radio and GNSS) at revision V3.0 dated 5/13/2026, and the core board at V3.0 and V3.1 |
| LilyGO T-Deck | 3: the board, and two GPS card variants, MIA-M10Q dated 24-03-29 and L76K dated 24-08-12 |
| LilyGO T-LoRa Pager | 1 file, 5 sheets, Rev V1.0 |
| LilyGO T-Watch Ultra | 1 file, 5 sheets, Rev V1.0 |
| Waveshare ESP32-S3-Touch-AMOLED | 3: the 1.75 in three sheets including an LC76G GNSS block, the 1.8 in one, the 2.06 in three |
| M5Stack CoreS3, Dial, Cardputer, Cardputer base, Cardputer-Adv | 5 files, 2 to 7 sheets each, no title block on any |
| M5Stack StickS3 | 1 file, 4 sheets, file name V0.6; sheet 1 is the M5PM1 rail tree |
| M5Stack StickC, Plus, Plus2 | 3 files: the Plus and Plus2 are schematics, the StickC is a 2019 rebuild pin map only |
| M5Stack Core2 | 2: V1.0 with an AXP192 and v1.1 with an AXP2101 on sheet 4 |
| Heltec WiFi LoRa 32 V3, V4, Wireless Tracker V2 | 6: V3, V3.1, V3.2, V4.2, V4.3 and Tracker FEM V2.3 |

Four more artifacts are registered as substitutes rather than as drawings: the
LilyGO factory `utilities.h` and `LoRaBoards.cpp`, the T-Beam Supreme hardware
pin map page, and the maintainer's answer in issue 225.

## The M10S answer, in one place

There is still no drawing with the MAX-M10S name on it, and there is not going
to be one: the vendor says the L76K drawing is the same board ("It is designed
to be compatible, but the hardware designer did not write the M10 name on it.
They are the same", issue 225, 2025-03-04). Reading that drawing states the
path plan 318 could not find:

- Module pin 7 is marked RESERVED and is wired through a 0 ohm link to the pin
  8 net, which the drawing labels `A_LDO4` (radio and GNSS sheet, revision
  V3.0).
- The factory example sets `XPOWERS_ALDO4` to 3300 mV and enables it under the
  comment `//gps`, which makes `A_LDO4` the AXP2101 ALDO4 GPS rail at 3.3 V
  (`examples/Factory/LoRaBoards.cpp`, the `T_BEAM_S3_SUPREME` block).
- Pin 6 V_BCKP sits on the `VRTC` net in the drawing, which is where plan 318
  already had it, so the `[inferred]` tag stays. The factory example disables
  VBACKUP for this board under `//not use channel`, its only 3300 mV
  `XPOWERS_VBACKUP` write is in the `CONFIG_IDF_TARGET_ESP32` branch, and the
  hardware page lists `LDO1(VRTC)` and `VBACKUP` as unused.

So V_IO has a named supply path for the first time: the pin 7 pad, through a 0
ohm link, to ALDO4 at 3.3 V. It is not certified. No document says a MAX-M10S is
fitted there, the link's population on an M10S assembly is unstated, and the
drawing names another part. The bench check is continuity from pin 7 to the
ALDO4 rail plus a UART exchange on that rail, and it is unchanged from plan
318's bench item 6 except that the expected answer is now known.

## Facts this pass moved

| Fact | Was | Now | State |
| :--- | :--- | :--- | :--- |
| T-Beam M10S V_IO | undocumented | pad to `A_LDO4` through a 0 ohm link, ALDO4 at 3300 mV in the factory code | path named, UNVERIFIED on hardware |
| T-Beam M10S V_BCKP | `[inferred]` | Still `[inferred]`. `VRTC` on the shared drawing, but the factory code disables VBACKUP for this board and the rail table lists `LDO1(VRTC)` and `VBACKUP` as unused | UNVERIFIED, unchanged |
| Core2 v1.1 PMIC | AXP2101 inferred from the product page | AXP2101 drawn on sheet 4; AXP192 drawn on V1.0 | VERIFIED from the drawings |
| StickS3 rails | product page numbers | M5PM1 rail tree on sheet 1, L0 shipping to L3B all-active | VERIFIED from the drawing |
| Cardputer-Adv | no PMIC recorded | charger plus boost on sheet 1, no AXP part on any of the four sheets, TCA8418 on sheet 2 | read from the drawing; the absence is an OCR sweep of all four sheets plus a visual read of sheet 1 |
| M5StickC | "no schematic" | still no schematic; the FCC index lists a 537.1 KB Schematics exhibit that the mirror marks metadata only, and both FCC endpoints answer 403 | UNVERIFIED, file not reachable |
| FCC exhibits | "no claim is made" | the per-ID index does list Schematics exhibits for the StickC, StickC Plus, StickS3 and Core2, with sizes, all metadata only | index read, files UNVERIFIED |
| M5PM1 reference design | assumed a fallback | the chip manual has no application circuit or schematic section | VERIFIED by search of the manual |
| AXP2101 reference design | registers only | revision 1.0 section 7.1 and Figure 7-1, page 58, one page | VERIFIED |

## What stays open

- V_IO on the M10S trim, and the population of the 0 ohm link. A document
  cannot close either; a continuity check can.
- Whether GPIO7 does anything useful on the M10S trim, where pin 5 is EXTINT
  rather than a wake input. The pin map marks wake-up as L76K only.
- The M5StickC part values, until the FCC exhibit is fetched from the FCC
  database directly.
- The drawing dates for the Pager and the Ultra. Both title blocks print
  `Friday, January 8, 2021`, which is a template default: it is the same on two
  products that did not exist then, and each file name carries a 2025 date. No
  document states which is the drawing date.
- The differences between the Heltec revisions. Three V3 drawings and two V4
  drawings exist and the resource server carries no changelog.

## Decisions

- Cite only. No PDF, PNG or vendor text is committed, and `docs/` still holds
  no binary document. The licence findings are per vendor in the register's
  section 5.1.5. The two LilyGoLib drawings are the only documents in the set
  whose repository carries a grant that names documentation files (MIT,
  `Copyright (c) 2025 Xinyuan Electronics`); they are still cited rather than
  stored, because admitting a binary needs the MIT notice on the page and a
  note in the directory, which is a separate decision.
- The register's section 5 keeps its existing vendor-page rows and gains
  section 5.1 as the drawing inventory. The old rows that said a schematic was
  "referenced by URL only" are corrected to say it was read.
- Vendor code stays labelled as vendor code. It is never cited as a drawing,
  and a fact that only vendor code supports says so.

## Follow-ups, in this change

- The register's FCC row no longer claims nothing is known about exhibits; it
  records the four IDs whose index pages list a Schematics exhibit and that
  every one is metadata only.
- The register's Core2 row stops calling the AXP2101 an open document gap and
  points at the v1.1 drawing.
- The register's coverage verdict drops the schematic gap and keeps the ones
  that survive.
- Plan 318 gets a follow-up section, because its "there is no M10S schematic"
  line is now too strong: the drawing exists under the L76K name and the vendor
  says so.
- `docs/components/ublox-max-m10s.md` gains a pointer for the board-level
  supply question its "facts still unverified" list raises.

## Sources

Vendor documentation roots, all read 2026-11-27:

- <https://github.com/Xinyuan-LilyGO/LilyGo-LoRa-Series/tree/master/schematic/T-Beam-S3-Supreme>
- <https://github.com/Xinyuan-LilyGO/T-Deck/tree/HEAD/schematic>
- <https://github.com/Xinyuan-LilyGO/LilyGoLib/tree/master/schematic>
- <https://github.com/Xinyuan-LilyGO/LilyGo-LoRa-Series/issues/225>
- <https://github.com/Xinyuan-LilyGO/LilyGo-LoRa-Series/blob/master/docs/en/t_beam_supreme/t_beam_supreme_hw.md>
- <https://github.com/Xinyuan-LilyGO/LilyGo-LoRa-Series/blob/master/examples/Factory/LoRaBoards.cpp>
- <https://github.com/Xinyuan-LilyGO/LilyGo-LoRa-Series/blob/master/examples/Factory/utilities.h>
- <https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.75> and the 1.8 and 2.06 siblings
- <https://docs.m5stack.com/en/core/CoreS3>, `M5Dial`, `Cardputer`, `Cardputer-Adv`, `StickS3`, `StickC-Plus2`, `Core2`, `Core2_v1.1`
- <https://resource.heltec.cn/download/WiFi_LoRa_32_V3/>, `WiFi_LoRa_32_V4/` and `Wireless_Tracker_V2/` directory listings
- <https://fccid.io/2AN3WM5STICKC>, `2AN3WM5STICKCPLUS`, `2AN3WM5STICKS3` and `2AN3WM5STACK-CORE2` per-ID pages

Every per-file URL, page count, byte size and SHA-256 is in the register
section linked at the top; they are not repeated here.
