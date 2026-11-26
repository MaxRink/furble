# 366 - GNSS receiver selection for a geotagging remote

Status: research and decision. This plan buys nothing and changes no driver,
board row or support claim. It records which board, receiver and antenna give
the best real-world positioning for a BLE camera remote that geotags photos,
and it carries three documentation corrections that this research proves are
wrong today (section 9). Sources were read on 2026-11-26. Facts about this
checkout were read at master `79df13a7`.

The document is numbered 366. It was chosen on 2026-11-26 because 365 belonged
to `fix/core-sim-align` (PR 513) and 364 was claimed twice on master, so neither
number was free. Both are settled now: 365 merged as written, and the second
plan at 364 is [383](383-hardware-candidates.md). This document keeps 366, which
no local or remote ref claims. The renumbering table in
[README.md](README.md#plan-renumbering) records the moves.

Tags on every claim:

- `[vendor: S1]` a vendor data sheet or product page, listed in section 11 with
  its retrieval date.
- `[repo: L-A]` read in this repository at the path named. Not a URL.
- `[inferred]` my conclusion, stated by no source.
- **UNVERIFIED** no readable document settles it. Collected in section 12.

Related: [318](318-next-board-battery-gps.md) (the board decision this document
must not overturn), [382](382-gnss-datasheet-verification.md) (the AT6668 and
M10 document pass these numbers come from), [377](377-gnss-vendor-audit.md)
(the 108 s cold-start label, already recorded as a mislabel), and
[383](383-hardware-candidates.md) (port cost, which ranks the same boards by a
different question).

## 1. Why this exists

The question that keeps coming back is "which GPS should I buy for geotagging".
It arrives as a receiver question and it is not one. A geotagged photo needs a
position, an accurate UTC time and a fixed horizontal accuracy, written inside
the camera's clock and geotag window. What decides whether that happens is, in
order: whether the antenna can hear the sky, whether the receiver still has
ephemeris and time when the shutter fires, and only then which receiver part
number is on the board.

This document sets that order with numbers, compares the seven receivers that
the hardware catalog already lists, states which of them furble can configure
today, and gives three buying options. It exists so the decision is reviewable
and does not have to be reconstructed from a transcript.

## 2. The ordering of levers, and the size of each

The order below is the order that decides a real fix. It is not the order of a
data sheet comparison, and the difference matters: the two levers that move the
most decibels are not receiver parts.

| Rank | Lever | Evidence | Size |
| :--- | :--- | :--- | :--- |
| 1 | Antenna radiator and ground plane | A 25 mm patch gives 5 dBi typical peak gain, 18 mm gives 2 dBi, 15 mm 1 dBi, 12 mm 0.5 dBi and 10 mm -2 dBi, and a larger ground plane raises gain further `[vendor: S12]`. A patch on a 70 x 70 mm ground plane shows "very high gain" `[vendor: S13]` | 4 to 7 dB |
| 2 | Antenna placement and the active LNA | An active antenna is "always advisable if the RF-cable length between the receiver and antenna exceeds about 10 cm", because the loss after the LNA no longer enters the system noise figure `[vendor: S13]` | 1 to 3 dB of system noise figure |
| 3 | Backup domain, which decides hot against cold start | Hot start is 1 s on an M10 and 2 s on the L76K, cold is 23 to 30 s `[vendor: S1][S8][S10]`. furble's own rail-cut measurement is 107.7 s `[repo: plans/implemented/15-gps-power.md:115]` | time, not decibels, but it decides whether a geotag is written at all |
| 4 | Receiver tracking sensitivity | The M10 tracks at -167 dBm, the AT6668 and the L76K at -162 dBm `[vendor: S1][S8][S10]` | 5 dB |
| 5 | Cold-start sensitivity | Flat. See section 3 | 0 to 3 dB |
| 6 | L1 against L1 plus L5 | The LC29H, the UC6580 and the NEO-F10N are dual band; every M10 in this table and every other receiver here is L1 `[vendor: S1][S4][S5][S6][S7]` | multipath only, no indoor gain |
| 7 | Constellation count | The M10 position accuracy is 1.5 m CEP in every multi-GNSS mode, from GPS plus Galileo through four systems, in the vendor's own table `[vendor: S1]`. The fourth system bought no accuracy | about 0 on accuracy, availability only |
| 8 | Update rate | The M10 runs 10 Hz on GPS plus Galileo but 3 Hz on the default GPS plus Galileo plus BeiDou B1I set `[vendor: S1]`, and furble clamps to that `[repo: lib/furble/protocol/GpsUbx.cpp:498]`. The L76K stops at 5 Hz `[vendor: S10]` | irrelevant for geotagging, and it costs power |

Two consequences follow, and both are load bearing for the recommendation.

- Constellation count ranks last of the receiver criteria, not first. u-blox
  publishes 1.5 m CEP for GPS plus Galileo and the same 1.5 m CEP for four
  systems `[vendor: S1]`, while the maximum navigation rate falls from 10 Hz to
  3 Hz on its default three-system set. QZSS matters in Japan, NavIC in India
  and SBAS (WAAS and EGNOS) in North America and Europe `[inferred]`. None of
  them is worth a new driver.
- "Concurrent" is weaker than the headline. On the M10, BeiDou B1I cannot be
  enabled simultaneously with B1C or with GLONASS L1OF `[vendor: S1, footnote
  13]`, so "supports four GNSS" and "runs the four you selected" are different
  claims. furble encodes the same rule `[repo: lib/furble/protocol/GpsUbx.cpp:498]`.

### 3. The flat cold-start result

This is the paragraph that changes a purchase. Every receiver whose data sheet
publishes a cold-start sensitivity states **-146 to -148 dBm**: -146 on the
SAM-M10Q and -148 on the MAX-M10S, the MIA-M10Q, the AT6668 and the UC6580. The
three that publish only an acquisition figure are no better, at -148 on the
L76K, -147 on the LC76G and -145 to -147 on the LC29H
`[vendor: S1][S2][S3][S5][S6][S7][S8][S10]`. **The whole spread is 3 dB, and the
best of it is a receiver that is no better than the cheapest.**

Cold-start sensitivity is the number that decides whether the receiver acquires
at all where the photographer is standing. Since it is flat across the class, no
purchase in this table buys an indoor or deep-canyon cold lock. The only levers
that move that case are antenna gain, antenna placement and sky view (ranks 1
and 2 above), or an assisted position from the phone path furble already has
`[repo: docs/supported-hardware.md]`.

An indoor fix is therefore an assistance problem, not a receiver problem. The
repository already says so in the catalog's own conclusions: "the only reliable
indoor position is an assisted one: the phone, a WiFi scan, or a known last
fix" `[repo: docs/hardware-catalog.md:105-110]`.

The M10's 5 dB advantage is real, but it is a tracking advantage. It holds a fix
under trees, at a window and in a pocket better than the AT6668 or the L76K do
`[inferred from S1, S8, S10 tracking figures]`. It does not start in a weaker
signal. That is the distinction the buying decision turns on.

## 4. The receivers, with the numbers that matter

Sensitivity is written as tracking / reacquisition / cold start / hot start, all
in dBm, and "n/s" means the data sheet states no figure for that column. Where a
vendor publishes an acquisition sensitivity instead of a reacquisition one, the
cell says so rather than moving the number into the wrong slot. Current is the
vendor's own test condition, which differs between parts, so it is quoted with
its unit rather than normalised.

| Receiver | Bands | Concurrent systems | Sensitivity (dBm) | TTFF cold / hot | Update rate | Current | Backup | furble driver |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| u-blox MAX-M10S `[vendor: S1]` | L1 only | 4: GPS, GLONASS, Galileo, BeiDou, with QZSS and SBAS riding on GPS | -167 / -160 / -148 / -159 | 23 to 28 s depending on the system set / 1 s, AssistNow Online 1 s, Offline 2 s, Autonomous 3 to 4 s | 10 Hz default on GPS plus Galileo, **3 Hz on the default three-system set**, 20 Hz high performance | 8 to 9.5 mA tracking, 4.5 to 5 mA in power save, 25 mW | Hardware backup mode and software standby, V_BCKP 1.65 to 3.6 V | Yes, UBX, since `511c11d3` |
| u-blox MIA-M10Q `[vendor: S2]` | L1 only | same as MAX-M10S | -167 / -160 / -148 / -159 | 23 to 28 s depending on the set / 1 s | same | same | 28 uA hardware backup, 46 uA software standby, 120 nA on VCC | Yes, UBX |
| u-blox SAM-M10Q, reference row `[vendor: S3]` | L1 only | same | **-165** / -158 / **-146** / -157 | M10 class, not read in this pass | 10 Hz multi-system | 37 mW with four systems | M10 backup modes | Yes, UBX |
| u-blox NEO-F10N, reference row `[vendor: S4]` | **L1 plus L5** | GPS plus Galileo plus BeiDou, or GPS plus NavIC | -167 / -159 / -148 / -159 | 27 to 28 s / 2 s | 10 Hz | not read in this pass | backup rail | UBX, but furble has no L5 keys |
| Quectel LC76G `[vendor: S5]` | L1 only, B1C optional | 5: GPS, GLONASS, BeiDou, Galileo, QZSS | -166 / -159 / n/s / n/s, acquisition -147 | 30 s / 2 s, 15 s / 2 s with EASY | 1 Hz default, 10 Hz maximum | 34 mA tracking, 3.8 mA standby | 13 uA backup | No, `$PAIR` and `$PQTM` |
| Quectel LC29H `[vendor: S6]` | **L1 plus L5**, GLONASS L1 only | 4 plus QZSS, NavIC on the (AI) variant | -165 / -157 to -159 / n/s / n/s, acquisition -145 to -147 | 26 s / 1 s, 5 s cold with AGNSS | 1 to 10 Hz | 23 to 32 mA, 76 to 106 mW | 22 uA backup, no standby figure | No, `$PAIR` and `$PQTM` |
| Unicore UC6580 `[vendor: S7]` | **L1 plus L5 or L2** | 6, including NavIC L5 on specific firmware | better than -162 / -159 / -148 / -156 | not read in the rows this pass opened | 10 Hz maximum | 40 mA at 3 V | 5 uA backup | No, `$CFG` |
| CASIC AT6668, M5 units `[vendor: S8][S9]` | L1 only: BDS B1I plus B1C, L1, E1, R1 | 6: GPS, QZSS, BD2, BD3, Galileo, GLONASS | -162 / n/s / -148 / -156, acquisition -160 | **23 s / 1 s** | 10 Hz | 31.64 mA at 5 V (Unit v1.1); 28.35 mA and 29.43 uA standby (Module v2.1) | VBAT exists on the chip and the module; **no supply is fitted on either M5 unit** | Yes, `$PCAS` and CASIC binary |
| Quectel L76K `[vendor: S10]` | L1 only; L5, B2a, E5a and NavIC are all "no" in its band table | 4: GPS, GLONASS, BeiDou, QZSS. **No Galileo, no SBAS** | -162 / -160 / n/s / n/s, acquisition -148 | **30 s / 2 s**, and 5.5 s is cold **with AGNSS** | 1 Hz default, **5 Hz maximum** | 29 mA, 20 uA standby, 8 uA backup (Quectel); Seeed's page says 41 mA and 360 uA, see section 8 | V_BCKP, 8 uA | Yes, `$PCAS` |

Four rows carry more information than the rest.

- **The SAM-M10Q is the antenna argument in one row.** It is the same M10
  silicon as the MAX-M10S with a 15 x 15 mm integrated patch, and it is 2 dB
  worse on tracking and 2 dB worse on cold start `[vendor: S1][S3]`. The
  antenna, not the chip, moved the number.
- **The L76K is the cheapest and the narrowest.** It has no Galileo and no
  SBAS, caps at 5 Hz and has a 29 mA tracking draw against the M10's 8 to
  9.5 mA `[vendor: S10][S1]`. In Europe the missing Galileo costs the geometry
  of eight satellites, and its V_BCKP keeps the RTC domain and SRAM alive, so a
  rail cut is recoverable `[vendor: S10]`.
- **The AT6668 is the one furble has actually driven.** Its 23 s cold start and
  1 s hot start are vendor specifications `[vendor: S8]`, and its VBAT exists on
  the chip and the module but is not fitted on the two M5 units this repository
  supports `[repo: docs/components/at6668.md:83-84]`.
- **The dual-band parts are not furble options today.** The LC29H, the UC6580
  and the NEO-F10N are the only L1 plus L5 receivers here, and none of them
  speaks a dialect furble configures, except the F10N, whose L5 signal keys are
  absent from the UBX codec `[repo: lib/furble/protocol/GpsUbx.h:85-97]`.

## 5. What furble already drives

The dialects decide which parts are real options, so they are stated from the
code rather than from a catalog row.

| Dialect | Receivers | In this checkout | State |
| :--- | :--- | :--- | :--- |
| `$PCAS` text plus CASIC binary | AT6668, L76K, ATGM336H | `lib/furble/protocol/GpsCasic.h`, `lib/furble/protocol/GpsCasic.cpp` | Implemented and hardware verified on the AT6668 unit. The L76K shares the path and no L76K board has run it |
| UBX | MAX-M10S, MIA-M10Q, SAM-M10Q, NEO-F10N | `lib/furble/protocol/GpsUbx.h`, `lib/furble/protocol/GpsUbx.cpp`, `src/FurbleGPSUbx.cpp` | Implemented in `511c11d3` (#459) after `cd04c38a` (#458), audited in `f1fa36a0` (#501). **Simulator verified only** |
| NMEA read-only | anything | `lib/furble/protocol/GpsDemux.cpp`, TinyGPSPlus | A fix and a time, no configuration, no standby throttle |
| `$PAIR` and `$PQTM` | LC76G, LC29H, Airoha AG3352 | none | Not implemented |
| `$CFG` | Unicore UC6580 | none | Not implemented |

What the UBX path covers today, from the code: detection by `MON-VER`,
`CFG-VALSET` and `CFG-VALGET`, `NAV-PVT` with accuracy, `NAV-SAT`, the
constellation mask, the update rate with a per-mask clamp, the dynamic model,
cyclic and on-off power save, `RXM-PMREQ` software standby, AssistNow
Autonomous and `UPD-SOS` save on shutdown `[repo: docs/supported-hardware.md:124-129]`.

What it does not cover: any L5 signal key. The signal table is L1 only, GPS
L1C/A, SBAS L1C/A, Galileo E1, BeiDou B1I and B1C, QZSS L1C/A and L1S, GLONASS
L1 `[repo: lib/furble/protocol/GpsUbx.h:85-97]`. A dual-band receiver would run
at its ROM defaults and furble could not turn L5 on or off.

The board side matters as much as the dialect. A GPS UART exists on the StickC,
StickC Plus, StickC Plus2, StickS3, Core2 and Tough rows and on neither the Core
nor the Waveshare ESP32-S3-ETH `[repo: src/FurbleBoard.cpp:135]`,
`[repo: src/FurbleBoard.cpp:157]`. No supported board supplies the receiver's
V_BCKP, so `FURBLE_GPS_BACKUP_SUPPLY` is defined nowhere
`[repo: docs/supported-hardware.md:129]`.

## 6. The three ranked options

Prices are the vendor's own, read on 2026-11-26. Each option names the exact
product, its receiver, its antenna provision and the work furble would owe.

### Option 1, best board with an onboard receiver

**LilyGO T-Beam Supreme, UBLOX (MAX-M10S) trim, 50.04 USD** `[vendor: S20]`. The
L76K trim of the same board is 37.12 USD `[vendor: S20]`.

- Receiver: u-blox MAX-M10S, L1, four concurrent systems, -167 dBm tracking,
  1 s hot start `[vendor: S1]`.
- Antenna provision: **SMA** `[repo: plans/318-next-board-battery-gps.md]`, so a
  25 mm-class active patch on 1 to 3 m of cable gets the radiator away from the
  18650 and the 2.4 GHz antenna. It is the only board in this survey that pairs
  an M10 with an external GPS connector.
- Backup: V_BCKP on the AXP2101 VRTC net, from the schematic read in plan 318,
  plus a 32.768 kHz crystal on XTAL_32K. The M10S trim has no schematic, so the
  backup path and V_IO are **UNVERIFIED**.
- furble work: a board port only. LovyanGFX OLED panel, AXP2101 power and
  battery, GPS on GPIO8 and GPIO9 with PPS on GPIO6, an optional
  `FURBLE_GPS_BACKUP_SUPPLY` define. No new GNSS protocol. The UBX driver
  applies unchanged, and it has to be proved on hardware for the first time.

### Option 2, best board plus external module

**M5Stack Core2, already a furble release environment, plus a SparkFun GNSS
Receiver Breakout MAX-M10S (Qwiic) at 45.95 USD** `[vendor: S15]`, plus an
active antenna with an SMA plug. SparkFun's own 3 m magnetic-mount GNSS antenna
is 16.50 USD `[vendor: S17]`.

- Receiver: MAX-M10S, as above `[vendor: S1]`.
- Antenna provision: the breakout is "populated with an SMA connector for a
  secure connection to a patch antenna", and its UART runs at 38400 by default
  `[vendor: S16]`.
- Backup: the board carries an **MS621FE rechargeable cell on the backup RAM**,
  which "allows for much faster position locks (a.k.a. hot start)" and holds
  configuration for about two weeks without power `[vendor: S16]`. This is the
  only combination here that survives a rail cut while staying on a supported
  board, and it is what makes furble's rail-cycle policy stop costing a cold
  start.
- furble work: a Grove-to-header UART harness on Port A at 3.3 V, and then
  hardware validation of the UBX driver that already exists. The 38400 default
  is inside the Auto probe ladder. Optionally a board define for the backed-up
  receiver.

### Option 3, best cheap option

**M5Stack Unit GPS SMA (U190), 10.00 USD** `[vendor: S18]`, on any supported M5
board.

- Receiver: AT6668 (ATGM336H-6N), L1, six systems, -162 dBm tracking, 23 s cold
  and 1 s hot `[vendor: S8]`. An active external antenna is in the box: 1 m
  cable, 1555 to 1580 MHz, 38 x 36 x 13 mm `[vendor: S18]`.
- Antenna provision: SMA on the unit, active antenna supplied. The unit's band
  covers BeiDou B1I at 1561.098 MHz and the L1 signals at 1575.42 MHz, and does
  **not** cover GLONASS L1 at 1598 to 1605 MHz `[vendor: S18][inferred]`.
- Backup: none. Every rail cut is a cold start, 107.7 s measured
  `[repo: plans/implemented/15-gps-power.md:115]`.
- furble work: **none.** The existing `$PCAS` path drives it, and the unit is
  already the documented drop-in fix for weak reception
  `[repo: docs/supported-hardware.md:106]`.

The 8.46 USD GPS/BDS Unit v1.1 `[vendor: S19]` is the same chip with a built-in
ceramic patch. It is the configuration the docs call marginal indoors
`[repo: docs/supported-hardware.md:105]`, so saving 1.54 USD against the SMA
unit is the worst trade in this document.

## 7. Does the antenna beat the receiver upgrade?

**Yes, for the case a camera remote actually has.** Four pieces of evidence.

- The gain range across patch sizes is 4 to 7 dB `[vendor: S12]`, against 5 dB
  for the entire tracking-sensitivity spread between the best and worst
  receiver in the table `[vendor: S1][S8][S10]`.
- Cold-start sensitivity is flat at -146 to -148 dBm (section 3), so there is no
  purchase that moves the acquire-in-weak-signal case. Antenna gain and sky view
  are the only levers.
- A remote's antenna sits next to a LiPo and a 2.4 GHz antenna and rarely points
  at the sky. Moving it out on a cable is worth more than any part swap, and
  u-blox recommends an active antenna above roughly 10 cm of cable `[vendor:
  S13]`.
- The same M10 silicon measures 2 dB worse in the SAM-M10Q's 15 mm patch than on
  a MAX-M10S with a good external radiator `[vendor: S1][S3]`, which is the
  effect in the other direction.

The antenna does **not** win, and the receiver does, in five cases:

1. **The rail is cut.** No antenna buys back a 1 s hot start. Only a live backup
   domain does, whether that is V_BCKP, a backup cell on the module or software
   standby that keeps V_IO up `[repo: src/FurbleGPS.cpp:829-836]`.
2. **The antenna is already adequate.** At a 25 mm patch, a ground plane of 50
   mm or more and a clear view, the next decibel is not available from gain. It
   comes from a second frequency against multipath, which is an L5 conversation
   `[inferred]`.
3. **The antenna's passband does not match the constellation mask.** SparkFun's
   magnetic-mount antenna covers 1575 to 1610 MHz `[vendor: S17]`, which excludes
   BeiDou B1I at 1561.098 MHz. The M5 unit's antenna covers 1555 to 1580 MHz
   `[vendor: S18]`, which excludes GLONASS L1 at 1598 to 1605 MHz `[vendor:
   S10]`. A better antenna on the wrong band is a worse antenna.
4. **"Active" is mistaken for "better".** u-blox states that "a low antenna gain
   cannot be compensated by high LNA gain", and that a single total gain figure
   does not let a buyer judge an antenna `[vendor: S13]`. The M5 unit's "30DBI"
   is an LNA figure, not dBic `[vendor: S18]`.
5. **The receiver has no driver.** An excellent antenna on an LC76G or a UC6580
   leaves furble with an unconfigurable NMEA source at ROM defaults
   `[repo: lib/furble/protocol/GpsUbx.h:85-97]`, `[vendor: S5][S7]`.

## 8. Vendor claims against the evidence

- **No independent sensitivity or TTFF measurement exists for any of these seven
  receivers** that this pass could find. Every dBm and every second above is a
  vendor figure under stated conditions.
- The figures are conditional in their own footnotes. u-blox measures
  sensitivity with all satellites at -130 dBm at room temperature and a good
  external LNA `[vendor: S1, footnotes 7, 8 and 12]`. Unicore states that its
  sensitivity index requires C/N0 of at least 41 dB and a matched external LNA
  `[vendor: S7, footnotes 3 and 4]`. These do not mean "locks at -167 dBm in a
  pocket".
- **The one independent field data set found is for the MAX-M10S, and it is
  sobering.** A peer-reviewed study measured a MAX-M10S with a u-blox ANN-MB-00
  antenna on a backpack under tree canopy and reported horizontal errors from
  **0.007 m to 5.5 m**, with outliers, and significant differences between the
  leaf-on and leaf-off seasons `[vendor: S22]`. The 1.5 m CEP in the data sheet
  is an open-sky figure `[vendor: S1]`.
- Two figures in this repository contradict the vendors, and this PR corrects
  them. The L76K's "5.5 s hot" is the cold-start-with-AGNSS figure from a
  scrambled vendor table, where the hot start is 2 s `[vendor: S10]`. The
  AT6668's "108 s cold" is a local re-fix after a rail cut, not the chip's 23 s
  cold start `[vendor: S8]`, `[repo: plans/implemented/15-gps-power.md:115]`.
- The L76K current draw is a genuine source conflict, not a mistake. Quectel's
  hardware design gives 29 mA tracking, 20 uA standby and 8 uA backup
  `[vendor: S10]`; Seeed's product page gives 41 mA tracking and 360 uA standby
  for its module `[vendor: S23]`. The row keeps both rather than picking one.
- furble's AT6668 tracking current, 31.64 mA at 5 V `[vendor: S8]`, is a unit
  figure that includes the unit's regulator and is not comparable with the 8 to
  9.5 mA at 3.0 V of an M10 `[vendor: S1]`. Both are correct, and neither
  normalises to the other.

## 9. Repository corrections in this PR

Three statements are wrong today. Each is corrected at its source with the
document that settles it.

| File | Before | After | Rests on |
| :--- | :--- | :--- | :--- |
| `docs/hardware-catalog.md:94` | L76K "30 s cold, 5.5 s hot" | "30 s cold, 2 s hot, and 5.5 s is cold with AGNSS" | Quectel L76K Hardware Design V1.0 table 2 `[vendor: S10]`. Seeed's page renders the same table in a scrambled order `[vendor: S23]` |
| `docs/hardware-catalog.md:93` | AT6668 "cold about 108 s without backup in the field reports" | "23 s cold as published; 107.7 s is furble's measured re-fix after a 60 s rail cut" | M5Stack Unit GPS v1.1 `[vendor: S8]`; the measurement is `plans/implemented/15-gps-power.md:115`; the distinction is already recorded at `docs/components/at6668.md:83-84` |
| `docs/hardware-catalog.md:76` | UBX "New driver. NMEA parsing still works, but the configuration and standby commands do not" | "Implemented since `511c11d3` (#459): MON-VER detection, CFG-VALSET, NAV-PVT and NAV-SAT, RXM-PMREQ standby, UPD-SOS. Simulator verified, no M10 hardware tested" | `src/FurbleGPSUbx.cpp`, `lib/furble/protocol/GpsUbx.h`, `docs/supported-hardware.md:124-129`, `plans/implemented/321-ublox-m10-driver.md` |

One further cell is corrected rather than rewritten: the L76K current cell keeps
both vendor figures with their sources, because the two documents disagree
(section 8). The AT6668 verdict cell also gains the VBAT condition, since the
chip has a backup domain and the M5 units do not expose it.

## 10. Recommendation

**Buy an M10-class receiver, put the best antenna you will carry on it, and keep
its backup domain alive.** Concretely:

- The **MAX-M10S is the only receiver here that combines -167 dBm tracking, a
  1 s hot start with a real backup domain and a UBX driver furble already
  ships** `[vendor: S1]`, `[repo: lib/furble/protocol/GpsUbx.h]`.
- **Best onboard solution: the LilyGO T-Beam Supreme UBLOX trim at 50.04 USD,
  with a 25 mm-class active patch on its SMA** `[vendor: S20]`. The connector is
  the point, not the module.
- **Best board plus module combination: the SparkFun MAX-M10S breakout at 45.95
  USD, with its MS621FE backup cell, on an already-supported M5 board**
  `[vendor: S15][S16]`, because it is the only option that survives a rail cut
  without a board port.
- **Zero-work option today: the 10.00 USD M5 Unit GPS SMA** `[vendor: S18]`. It
  needs no code and it has no backup domain, so it is a stepping stone, not the
  destination.

## 11. Task table

Research and documentation only. No code, board or support claim is owed by
this plan, and none is made.

| # | Item | State |
| :--- | :--- | :--- |
| 1 | The lever ordering with magnitudes | Done, section 2 |
| 2 | The seven-receiver comparison with datasheet numbers | Done, section 4 |
| 3 | The dialect and driver inventory from the code | Done, section 5 |
| 4 | Three ranked options with prices | Done, section 6 |
| 5 | The antenna against receiver argument and its five exceptions | Done, section 7 |
| 6 | Three catalog corrections with citations | Done, section 9 |
| 7 | Bench measurement of the UBX path on a MAX-M10S | Not in this plan. Owned by [318](318-next-board-battery-gps.md), bench item 6 |
| 8 | Measuring the AT6668 against its 23 s specification with a fitted backup cell | Not in this plan. Open as C-27e in `docs/components/at6668.md` |

## 12. Sources

All URLs read on 2026-11-26. A vendor page settles only what its own row says;
product pages are quoted for price and provision, data sheets for numbers.

- S1 u-blox MAX-M10S data sheet, UBX-20035208 R08:
  https://content.u-blox.com/sites/default/files/MAX-M10S_DataSheet_UBX-20035208.pdf
- S2 u-blox MIA-M10Q data sheet, UBX-22015849 R08:
  https://content.u-blox.com/sites/default/files/documents/MIA-M10Q_DataSheet_UBX-22015849.pdf
- S3 u-blox SAM-M10Q data sheet, UBX-22013293:
  https://content.u-blox.com/sites/default/files/documents/SAM-M10Q_DataSheet_UBX-22013293.pdf
- S4 u-blox NEO-F10N data sheet, UBX-23002117 R05:
  https://content.u-blox.com/sites/default/files/documents/NEO-F10N_DataSheet_UBX-23002117.pdf
- S5 Quectel LC76G GNSS Specification V1.0:
  https://www.quectel.com/content/uploads/sites/3/2022/12/Quectel_LC76G_GNSS_Specification_V1.0.pdf
- S6 Quectel LC29H Series GNSS Specification V1.7:
  https://quectel.com/content/uploads/2024/03/Quectel_LC29H_Series_GNSS_Specification_V1.7.pdf
- S7 Unicore UC6580x-00 data sheet R1.3:
  https://fr.unicore.com/uploads/file/uc6580x-00-datasheet-en-r1-3.pdf
- S8 M5Stack Unit GPS v1.1, U032-V11:
  https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/static/pdf/static/en/unit/Unit-GPS%20v1.1.pdf
- S9 M5Stack Module GPS v2.1, M003-V21:
  https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/static/pdf/static/en/module/Module_GPS_v2.1.pdf
- S10 Quectel L76K Hardware Design V1.0:
  https://files.waveshare.com/upload/d/db/Quectel_L76K_Hardware_Design_V1.0.pdf
- S11 Quectel L76K GNSS Protocol Specification V1.1:
  https://files.waveshare.com/upload/d/dd/Quectel_L76K_GNSS_Protocol_Specification_V1.1.pdf
- S12 Taoglas, GPS patch integration application note APN-12-8-002/B:
  https://www.taoglas.com/wp-content/uploads/pdf/TAOGLAS-GPS-Patch-Antenna-Integration-Application-NoteAPN-12-8-002.B.pdf
- S13 u-blox, GPS antennas application note GPS-X-08014 (mirror copy, same
  document as the u-blox original):
  https://avrproject.ru/EB-500/GPS_Antennas_ApplicationNote-GPS-X-08014-.pdf
- S14 u-blox, SAM-M8Q white paper, UBX-16026689, for the patch-size and ground
  plane measurements:
  https://content.u-blox.com/sites/default/files/SAM-M8Q_WhitePaper_(UBX-16026689).pdf
- S15 SparkFun GNSS Receiver Breakout MAX-M10S (Qwiic), 45.95 USD:
  https://www.sparkfun.com/sparkfun-gnss-receiver-breakout-max-m10s-qwiic.html
- S16 SparkFun MAX-M10S hardware overview, the SMA connector, the 38400 baud
  default and the MS621FE backup cell:
  https://docs.sparkfun.com/SparkFun_u-blox_MAX-M10S/hardware_overview/
- S17 SparkFun GPS/GNSS magnetic mount antenna, 3 m, SMA, 16.50 USD, 1575 to
  1610 MHz, LNA 28 dB:
  https://www.sparkfun.com/gps-gnss-magnetic-mount-antenna-3m-sma.html
- S18 M5Stack GPS/BDS Unit with SMA antenna (AT6668), U190, 10.00 USD:
  https://shop.m5stack.com/products/gps-bds-unit-with-sma-antenna-at6668
- S19 M5Stack GPS/BDS Unit v1.1 (AT6668), 8.46 USD sale, 9.95 USD list:
  https://shop.m5stack.com/products/gps-bds-unit-v1-1-at6668
- S20 LilyGO T-Beam Supreme, UBLOX trim 50.04 USD and L76K trim 37.12 USD, from
  the product variants file:
  https://lilygo.cc/products/t-beam-supreme.json
- S21 LilyGO T-Deck Plus, 70.99 USD, and its MIA-M10Q module:
  https://lilygo.cc/products/t-deck-plus-1 and
  https://wiki.lilygo.cc/products/t-deck-series/t-deck-plus/
- S22 Evaluation of accuracy and usability of low-cost GNSS receivers under tree
  canopy, MDPI IoT 6(2):34, the independent MAX-M10S field data:
  https://www.mdpi.com/2673-7418/6/2/34
- S23 Seeed Studio, L76K GNSS module for XIAO, the 41 mA and 360 uA figures and
  the scrambled TTFF table:
  https://wiki.seeedstudio.com/get_start_l76k_gnss/

Repository citations, not URLs:

- `docs/hardware-catalog.md:76`, `:93`, `:94` are the three rows section 9
  corrects.
- `docs/components/at6668.md:83-84` already records the 23 s against 107.7 s
  distinction that the catalog row contradicts.
- `docs/supported-hardware.md:105-110` is the unit table these options extend,
  and `:124-129` is the M10 capability paragraph and the V_BCKP statement.
- `plans/implemented/15-gps-power.md:115` is the 107.7 s measurement.
- `plans/318-next-board-battery-gps.md` holds the T-Beam schematic facts, the
  32.768 kHz crystal finding and the bench plan.
- `plans/implemented/321-ublox-m10-driver.md` holds the u-blox document list
  behind every UBX key furble writes.

## 13. UNVERIFIED

1. **The UBX path on real hardware.** Everything the M10 driver does is verified
   against a virtual M10 in the simulator only, and no M10 hardware has been
   tested `[repo: docs/supported-hardware.md:127]`. Every "Option 1" and
   "Option 2" claim about what furble will do on an M10 is a claim about code
   that has passed host tests, not about a receiver on a desk.
2. **The T-Beam Supreme M10S backup path.** V_BCKP on the AXP2101 VRTC net, V_IO
   and the GPIO7 line come from a schematic read of the L76K trim in plan 318.
   The M10S trim has no published schematic.
3. **Whether the M5 units wire VBAT.** The chip and module documents give VBAT
   and say a cell enables the hot start `[repo: docs/components/at6668.md:83]`;
   the unit document lists no backup supply and its pinmap exposes only 5 V, GND
   and the UART pair. The unit schematic was not opened.
4. **The L76K TTFF and current conflict.** Quectel's table 2 and Seeed's page
   disagree on current and Seeed's TTFF row is scrambled. Quectel's own table is
   used for TTFF; both current figures are kept.
5. **The LC29H NavIC claim.** NavIC appears only for the (AI) variant on the
   product page, and the V1.7 specification table this pass read lists GPS,
   GLONASS, Galileo, BeiDou and QZSS for the variants it covers.
6. **The UC6580 and NEO-F10N TTFF values.** The rows these two data sheets print
   were not extracted in this pass, so section 4 marks them "not read" rather
   than quoting a number from memory.
7. **The T-Deck Plus antenna.** Its MIA-M10Q is confirmed `[vendor: S21]`; the
   patch, its ground plane and whether an external connector exists are not.
8. **Any independent sensitivity or TTFF measurement** for any of the seven
   receivers. None was found. The MDPI canopy study covers accuracy only `[S22]`.
9. **The practical gain of the specific antennas quoted.** SparkFun publishes
   band, LNA gain and current `[vendor: S17]`; M5Stack publishes one "30DBI"
   figure that is an LNA number `[vendor: S18]`. Neither states radiator gain in
   dBic, which is the figure section 7 needs.

## Implementation state

Research and decision only, and the three documentation corrections in section
9. No board, environment, driver, profile row or support claim is added. The
document exists so the ranking is reviewable in the repository instead of only
in a transcript. The two bench items in section 11 stay with plan 318 and with
`docs/components/at6668.md` row C-27e.
