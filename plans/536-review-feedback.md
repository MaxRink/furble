# 536 - Review feedback for the enclosure and battery branch

Review-feedback record for PR #536 (`docs/364a-enclosed-devices`), branch head
`d901c882`, base `master` at `b509486b`. An independent adversarial review
raised eight findings (F1 to F8) and one structural gap (S1). Each is recorded
with a disposition and the evidence.

Every one of the nine new sources 56 to 64 was fetched on 2026-11-26 and read,
because the subject matter is vendor facts. All nine URLs resolve.

## The blocker

### F1 - "No battery base for this model" is false

Defect. `docs/hardware-catalog.md:153` says of the CoreS3:

```
| M5Stack CoreS3 | included | 500 mAh | No battery base for this model. |
```

M5Stack sells one:

```
$ gh api repos/... # shop.m5stack.com/products/m5go-battery-bottom3-for-cores3-only
M5GO Battery Bottom3 (for CoreS3 only) SKU:A014-D Sale price $7.61 Regular price $8.95
integrating an IR LED emitter and a 500mAh lithium battery
Specifications ... Battery 3.7V@500mAh ... Compatible with magnetic charging base
```

It also appears in the vendor's own store search next to the Bottom2 the table
recommends. The row exists to answer "can I double this device", so the wrong
answer is worse than no row. Either correct it, or cite the SKU and say why it
does not fit the use.

## Findings

### F2 - the ranked list contradicts itself six lines apart

Defect. `docs/hardware-catalog.md:172` says the StickS3 stack is "The only
listed device that is a finished article and runs furble today", and `:182` in
the same list says of the Core2 stack "Runs today". The Ready-to-use table row
for the Core2 stack is tier T0 with "This stack works today", and
`docs/supported-hardware.md:22` lists `m5stack-core2` as a supported
environment. Two finished articles in one list cannot have one "only".

### F3 - the Heltec shell sentence is contradicted by its own source

Defect. `docs/hardware-catalog.md:190-191` says "The Heltec Tracker V2 has
neither an official nor a common shell." Source [64] is that vendor page, and it
lists a case as a necessary accessory:

```
Necessary Accessories 02
SMA Antenna ... $0.90
Solar Case for Dev-board  Waterproof Enclosure for Outdoor Meshtastic/MeshCore  $45.20
```

Whether that case is a good fit is a fair opinion; "no official shell" is not a
fact the cited page supports. Say what the page says, or cite a source that
says the case does not fit.

### F4 - "the largest cell for the money" is refuted on the same page

Defect, low severity. `docs/hardware-catalog.md:177` ranks the T-Deck Plus as
"the largest cell for the money" at 2000 mAh for 70.99 USD. The Ready-to-use
table two sections earlier lists the Seeed Wio Tracker L1 Pro at 2000 mAh for
47.90 USD, sourced at [8]. Equal capacity for 23 USD less is not the best cell
per dollar in this catalog. "Large cell plus touch and keyboard" would be
defensible.

### F5 - the Core2 is named as glove-tolerant against the same document's rule

Defect. `docs/hardware-catalog.md:193` lists "the StickS3 buttons and the Core2
front zones" as the glove-tolerant controls, three lines after saying "the
touch-only options are unusable gloved". The document's own Integration notes
say "Capacitive touch is poor with gloves", and
`docs/supported-hardware.md:22` describes the Core2 input as "On-screen shutter
buttons", which is the touch panel. Either drop the Core2 from that sentence or
name the physical control that survives a glove.

### F6 - the section's third question is never answered

Structural gap. `docs/hardware-catalog.md:139-143` sets up three questions per
device and calls the third, whether the cell survives a rail-cut reboot, the one
"what decides hot versus cold GNSS". The table has three columns (Case, Battery
in the box, Note) and none of the fourteen rows answers it. The catalog's GNSS
table has a "Backup or standby" column that could carry it. As written the
preamble promises the section's most interesting content and the section does
not deliver it.

### F7 - the T-Deck Plus receiver is both known and unverified

Defect, low severity. `docs/hardware-catalog.md:216-217` lists "Which u-blox
part the T-Deck Plus u-blox trim ships" as unverified, but `:118` in this same
document states `MIA-M10Q, UBX`, and the vendor wiki states it twice
("T-Deck Plus adds a GPS module (MIA-M10Q)", "GPS MIA-M10Q GNSS",
`wiki.lilygo.cc/products/t-deck-series/t-deck-plus`). The open question is which
part the *store variant* maps to, not which part the board uses. Reword, and add
the wiki URL to the sources, because the catalog's own MIA-M10Q claim for this
board is currently uncited.

### F8 - four of the nine new source numbers alias a page already in the list

Defect, low severity, and one misplaced in-text pointer.

- `[59]` and `[16]` are both `waveshare.com/esp32-s3-touch-amoled-1.75.htm`.
- `[62]` and `[22]` are both `shop.m5stack.com/products/m5stack-cardputer-adv-version-esp32-s3`.
- `[64]` and `[13]` are both `heltec.org/project/wireless-tracker-v2/`.
- `[56]` is `lilygo.cc/products/t-deck-plus`, which answers `301` to
  `lilygo.cc/products/t-deck-plus-1`, the URL of `[1]`.

That is nine new numbers for five new pages. It also produces one wrong pointer:
`:176` cites `[62]` for the 29.90 USD Cardputer Adv price, but the `[62]` entry
describes only "1500 mAh and 160 gf keys"; the price belongs to `[22]`. A
reader checking `[62]`'s entry text does not find the number.

## Minor

- `docs/hardware-catalog.md:149` (T-Deck Pro) and `:153` (CoreS3) carry no
  citation at all in a table whose PR body says it "adds the evidence it was
  missing". CoreS3's 500 mAh is already in `[10]`, so citing is free.

## Attacked and cleared

- **All nine new URLs.** Each fetched 2026-11-26 and each numeric claim read out
  of the page: Pager `106 x 89 x 23 mm`, 1500 mAh, BQ25896, BQ27220, the LilyGoLib
  750 mA note and the "1/4-inch fixed screw interface ... lanyard" line; T-Watch
  Ultra 1100 mAh and "POWER ... + BOOT (GPIO0)" as the only buttons, "63.5 x 49 x
  22 mm without the strap"; Waveshare trims exactly as written, `-B` is "Standard
  version with protective case", `-G` is "GPS version, with built-in LC76G
  module", `-1.75C-EN` is "Standard version, without Lithium Batt" and `-1.75C`
  "Comes with a 3.7V Lithium Batt", CNC aluminium case, no GNSS on either;
  Bottom2 `SKU:A014-C-V13`, `Sale price $8.46`, `Regular price $9.95`,
  `500mAh @ 3.7V`, TP4057; Cardputer Adv 1500 mAh, "56 keys (4 x 14)", 160 gf,
  `price $29.90`; StickS3 `price 21.50`, 250 mAh, `0 ~ 40°C`, "Grove interface at
  the bottom of the device"; Heltec packing list "Wireless Tracker x1, Spring LoRa
  Antenna x1, SH1.25x2 Battery Connector x1, Pin Header x2"; Wio L1 Pro "PC+ABS
  injection-molded enclosure, offering better durability and consistency than
  traditional 3D-printed nylon housings" and built-in 2000 mAh; Cardputer K132
  "120 mAh + 1400 mAh (in base)" and the EOL banner. Every quoted figure matched.
- **Cross-document contradictions.** `docs/supported-hardware.md` states no
  case, battery capacity or price, so it cannot contradict the new table. Its
  only overlap is the GPS table's "Built in" antenna for the T-Watch Ultra and
  T-LoRa Pager, which the new "Unverified" bullet does not contradict. Neither
  does `plans/364-hardware-candidates.md`: its Pager rows (BQ25896 plus BQ27220
  per the LilyGoLib document) and its Cardputer Adv "no PSRAM" line agree.
- **Citation numbering.** Sources run 1 to 64 with no gap and no duplicate
  number; every in-text `[N]` resolves; every new `[56]` to `[64]` is cited at
  least once; no citation points outside the list.
- **Em dashes.** None in the added lines or anywhere in the file, per
  `CLAUDE.md:176`.
- **Marketing language.** The new prose ("the best finished pocket article",
  "sealed body") is in the voice the file already uses for its buying guide
  ("Best development target", "The best all-round upgrade"), so it is not a
  style defect by itself. The only evaluative sentences that fail are the two
  factual ones, F4 and F2.
- **Gates.** `python3 tools/check_doc_citations.py --root .` reports 0 errors of
  3108 citations in 175 files. `python3 tools/pre_review.py --base origin/master`
  reports PASS: 13 gates. `python3 tools/check_pr_hygiene.py origin/master...HEAD`
  reports 0 failures, 0 warnings. The PR body's gate claims reproduce.
- **Stale prices outside the diff.** The Core2 shop page now reads `[EOL]` at
  `$46.90`, while the catalog keeps 49.90 from `[10]`. That drift predates this
  PR; it is not introduced here and is only worth a separate pass.

## Resolution

Every finding is accepted. All are fixed in the follow-up commit on this
branch.

| Finding | State | Fix |
| :--- | :--- | :--- |
| CoreS3 battery base claim | fixed | The row now records the M5GO Battery Bottom3, SKU A014-D, 500 mAh with an IR emitter, 7.61 USD sale and 8.95 USD list, with a new source [60]. Re-fetched from the product page before writing. |
| F2 finished article claim | fixed | Item 1 now reads "the smallest finished article that runs furble today, and the only current one", so item 6 no longer contradicts it. |
| F3 Heltec shell claim | fixed | Now states that Heltec lists a solar case accessory at 45.20 USD [13], so enclosure there is a separate purchase rather than a search. |
| F4 largest cell for the money | fixed | Item 3 now says the largest cell among the ESP32 devices, and names the Wio L1 Pro as the cheaper 2000 mAh device that is not portable. |
| F5 Core2 glove claim | fixed | The paragraph now says capacitive touch is poor gloved on every touch model here, and that the only glove-tolerant control is the StickS3 button pair. |
| F6 promised column | fixed | The introduction no longer promises a third column and points at plan 318 for the backup-domain facts that are documented. |
| F7 T-Deck Plus part | fixed | The bullet now says the part is MIA-M10Q per the vendor wiki and that the L76K trim is a different purchase, rather than calling the part unverified. |
| F8 duplicate sources | fixed | [56] to [64] are gone. The duplicates now cite the existing numbers ([1], [3], [13], [16], [22]) and only four genuinely new URLs remain, renumbered 56 to 60 with the Bottom3 added. |
