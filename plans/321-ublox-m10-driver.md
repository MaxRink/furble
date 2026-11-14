# 321 u-blox M10 (UBX) GPS driver

## Motivation

furble's GPS driver speaks NMEA plus the CASIC binary protocol of the
AT6668 and L76K receivers. The boards plan 318 recommends for a better remote
(LilyGO T-Beam Supreme M10S, T-Watch Ultra, T-LoRa Pager) carry u-blox M10
receivers: MAX-M10S and MIA-M10Q. Generic M10 UART modules also fit the
StickS3 Grove port. They all output NMEA by default, so furble already gets a
fix from them, but it cannot configure them: the CASIC frames and `$PCAS`
sentences mean nothing to a u-blox receiver. Plan 318 (PR #443) holds the board
recommendation and the original design sketch for this driver.

This plan adds the UBX protocol, a receiver backend seam, a virtual M10 in the
simulator and the M10 features. No M10 hardware is available. Everything is
verified in host tests, fuzzing and the simulator, and every M10 receiver is
declared simulator-tested and hardware-untested.

## Sources

All official u-blox documents, accessed 2026-11-13. "IFD" below and in code
comments is the first one.

| Short name | Document | Number and revision |
| --- | --- | --- |
| IFD | u-blox M10 SPG 5.10 Interface description, protocol 34.10 | UBX-21035062 R03 |
| IFD 5.20 | u-blox M10 SPG 5.20 Interface description (external image only, not the module ROM) | UBXDOC-304424225-20128 R01 |
| DS-MAX | MAX-M10S Data sheet, ROM SPG 5.10 | UBX-20035208 R08 |
| IM-MAX | MAX-M10S Integration manual | UBX-20053088 R05 |
| DS-MIA | MIA-M10Q Data sheet | UBX-22015849 R08 |
| IM-MIA | MIA-M10Q Integration manual | UBX-21028173 R05 |
| RN | u-blox M10 ROM 5.10 Release notes | UBX-22001426 R02 |

Both modules run ROM SPG 5.10, so the IFD is the protocol reference. IFD 5.20
describes a separately loaded image with different defaults and is not used.
No PDF and no long excerpt is committed.

### Messages

| Message | Class/ID | Length | IFD section | Use |
| --- | --- | --- | --- | --- |
| Frame and checksum | | | 3.2, 3.4 | Sync B5 62, 8-bit Fletcher over class, id, length, payload |
| ACK-ACK / ACK-NAK | 05 01 / 05 00 | 2 | 3.9.1 / 3.9.2 | Configuration result (3.5.1), "at least within one second" (3.9.1, 3.9.2) |
| CFG-RST | 06 04 | 4 | 3.10.2 | Hot, warm, cold restart; GNSS stop before a backup. Never acknowledged |
| CFG-VALDEL | 06 8C | 4+4n | 3.10.3 | Codec only |
| CFG-VALGET | 06 8B | 4+4n / 4+n | 3.10.4 | Read back the applied configuration (console) |
| CFG-VALSET | 06 8A | 4+n | 3.10.5 | All configuration, version 0, at most 64 items |
| MON-VER | 0A 04 | 40+30n | 3.14.9 | Detection and firmware version |
| NAV-PVT | 01 07 | 92 | 3.15.11 | Fix, time, validity, accuracy |
| NAV-SAT | 01 35 | 8+12n | 3.15.13 | Satellite page and console |
| RXM-PMREQ | 02 41 | 16 | 3.16.6 | Software standby; force flag required (IM-MAX 3.6.3.2) |
| UPD-SOS | 09 14 | 0, 4, 8 | 3.19.1 | Save on shutdown and restore status |

### Configuration keys

Value size is bits 28 to 30 of the key (IFD 4.2). Defaults are the IFD
appendix firmware defaults.

| Key | ID | Type | Firmware default | IFD |
| --- | --- | --- | --- | --- |
| CFG-RATE-MEAS | 0x30210001 | U2 ms | 1000, minimum 25 | 4.9.17 |
| CFG-NAVSPG-DYNMODEL | 0x20110021 | E1 | 0 portable | 4.9.12 |
| CFG-SIGNAL-GPS_ENA / GPS_L1CA_ENA | 0x1031001f / 0x10310001 | L | 1 / 1 | 4.9.21 |
| CFG-SIGNAL-SBAS_ENA / SBAS_L1CA_ENA | 0x10310020 / 0x10310005 | L | 1 / 1 | 4.9.21 |
| CFG-SIGNAL-GAL_ENA / GAL_E1_ENA | 0x10310021 / 0x10310007 | L | 1 / 1 | 4.9.21 |
| CFG-SIGNAL-BDS_ENA / BDS_B1_ENA / BDS_B1C_ENA | 0x10310022 / 0x1031000d / 0x1031000f | L | 0 / 0 / 1 | 4.9.21 |
| CFG-SIGNAL-QZSS_ENA / QZSS_L1CA_ENA / QZSS_L1S_ENA | 0x10310024 / 0x10310012 / 0x10310014 | L | 1 / 1 / 1 | 4.9.21 |
| CFG-SIGNAL-GLO_ENA / GLO_L1_ENA | 0x10310025 / 0x10310018 | L | 0 / 1 | 4.9.21 |
| CFG-PM-OPERATEMODE | 0x20d00001 | E1 | 0 FULL (1 PSMOO, 2 PSMCT) | 4.9.15 |
| CFG-PM-POSUPDATEPERIOD | 0x40d00002 | U4 s | 10, at least 5 | 4.9.15 |
| CFG-PM-ONTIME | 0x30d00005 | U2 s | 0 | 4.9.15 |
| CFG-PM-UPDATEEPH | 0x10d0000a | L | 1 | 4.9.15 |
| CFG-ANA-USE_ANA | 0x10230001 | L | 0 | 4.9.1 |
| CFG-MSGOUT-UBX_NAV_PVT_UART1 | 0x20910007 | U1 | 0 | 4.9.11 |
| CFG-MSGOUT-UBX_NAV_SAT_UART1 | 0x20910016 | U1 | 0 | 4.9.11 |
| CFG-UART1OUTPROT-UBX / NMEA | 0x10740001 / 0x10740002 | L | 1 / 1 | 4.9.29 |

The modules ship a production preset on top of the firmware defaults. Both
data sheets (1.3) and IM-MAX 2.1.3 list the module default constellations as
GPS, Galileo and BeiDou B1I with QZSS and SBAS. MAX-M10S runs its UART at 9600
(IM-MAX 2.1.4), MIA-M10Q at 38400 (DS-MIA Table 21). Both are on the existing
autobaud ladder, so detection needs no new rate.

### Constraints that shape the design

- At most four major constellations, and only as GPS L1C/A, Galileo E1,
  BeiDou B1C and GLONASS L1OF (IM-MAX 2.1.3).
- BeiDou B1I cannot run with B1C or with GLONASS (DS-MAX 1.3 note 13, RN
  4.3.6). SBAS and QZSS need GPS (DS 1.3). An invalid set is refused with
  ACK-NAK (IM-MAX 2.1.3).
- Any CFG-SIGNAL change restarts the GNSS subsystem. Wait for the ACK and then
  0.5 s before the next command (IFD 4.9.21).
- Maximum navigation rate at the default CPU clock (DS-MAX, DS-MIA Table 2/3):
  any single GNSS 18 Hz, GPS+GAL 10 Hz, GPS+GAL+GLO 6 Hz, GPS+GAL+BDS B1C 8 Hz,
  four GNSS 4 Hz, the module default with B1I 3 Hz.
- Power save: PSMCT (cyclic tracking) supports 1 Hz, 2 Hz and 2 to 10 s
  periods (IM-MAX 3.6.2.3); PSMOO (on/off) uses POSUPDATEPERIOD, at least 5 s
  (IFD 4.9.15), and needs an RTC. Power save with BeiDou B1C is refused (RN
  7.2, IM-MAX 3.6.2). Set OPERATEMODE last (IM-MAX 3.6.2.6).
- Software standby (RXM-PMREQ) clears the RAM configuration (IM-MAX 3.6.3.2),
  so configuration is written to RAM and BBR. BBR survives while V_IO or
  V_BCKP is supplied.
- UPD-SOS stores BBR in the flash file system (IFD 3.19.1, IM-MAX 3.12.4).
  Neither MAX-M10S nor MIA-M10Q has an external flash, so the backup is
  expected to be refused on both.
- AssistNow Autonomous is supported for GPS, Galileo, GLONASS, QZSS and
  BeiDou B1I, not B1C (DS Table 5, RN 3.1.4). Its default is off on SPG 5.10
  (IFD Table 59, RN 4.2.4).

### Figures the simulator uses (vendor claims)

| Figure | MAX-M10S | MIA-M10Q | Source |
| --- | --- | --- | --- |
| Cold start TTFF, module default set | 27 s | 27 s | DS Table 2 |
| Hot start TTFF | 1 s | 1 s | DS Table 2 |
| AssistNow Autonomous TTFF, 2-day-old data | 3 to 4 s | 3 to 4 s | DS Table 2 |
| Warm start TTFF | not published | not published | modelled as 25 s, see below |
| Continuous tracking, default set, 3.0 V | 9.5 mA | 10.5 mA | DS 4.3 |
| Cyclic tracking, default set, 3.0 V | 5 mA | 5.5 mA | DS 4.3 |
| Software standby, V_IO 3.3 V | 46 uA | 46 uA | DS Table 17/18 |
| Hardware backup, V_BCKP | 28 uA | 28 uA | DS Table 17/18 |

These are vendor claims, not measurements, and are tagged as such in
`tools/power-model/board-currents.yaml`. The warm start figure is not
published for M10; the simulator uses 25 s, a placeholder between hot and
cold that no assertion depends on.

## Design

### Protocol (`lib/furble/protocol/GpsUbx.{h,cpp}`)

Pure code, listed in `tools/portable_core_manifest.txt`. Framing, the
Fletcher checksum, a `scan()` used by the firmware demultiplexer, a bounded
byte-at-a-time `Stream`, decoders for every message above, encoders for every
request plus the outputs the simulator needs, and the key value-size rule.
The receive bound is 1024 bytes (NAV-SAT at 64 signals is 776). A longer
length field drops one byte and resynchronises.

## Implementation state

### PR 1 (`feat/ubx-1-protocol`)

- `GpsUbx` codec, `tests/host/gps_ubx_test.cpp` (CTest `gps-ubx`) and the
  `gps_ubx_fuzz` target (CTest `gps-ubx-fuzz`, gate `host-gps_ubx` in
  `tools/check_fuzz.py`, seeds in `tests/fuzz/seeds.json`).
- Nothing in the firmware references the codec yet, so the images are
  unchanged.
- Deviation: the legacy 8-byte RXM-PMREQ form is not decoded. The IFD
  documents only the 16-byte form for M10.

### PR 2 (`feat/ubx-2-backend-sim`)

- Detection. Once the receiver talks (fixed baud: first characters; Auto:
  after the ladder locks), the GPS task polls UBX-MON-VER through
  `Ubx::Detect`: two polls, one second apart. An answer selects the UBX
  backend. No answer keeps the NMEA backend, which is the existing CASIC and
  `$PCAS` path. A late answer after detection gave up switches to UBX and
  reconfigures. The decision holds for the session and is reset by
  `enable()`, `disable()` and every autobaud lock.
- Backend seam. `GPS::backend_t` is `UNKNOWN`, `DETECTING`, `NMEA` or `UBX`.
  Configure, restart, satellite capture, MON-HW and the ephemeris cache switch
  on it. The UBX code is `src/FurbleGPSUbx.cpp`. UBX commands share the
  existing one-in-flight configuration queue with a 1 s ACK timeout (IFD
  3.9.1, 3.9.2), three attempts, and an optional settle time after the ACK.
- NAV-PVT. The UBX configuration turns NAV-PVT on (RAM and BBR) and keeps
  NMEA on as the fallback. A fresh NAV-PVT (younger than the 30 s freshness
  window) overlays position, altitude, speed, course, satellites, date and
  time, and adds hAcc, vAcc and tAcc to the status snapshot. The fix
  freshness, hold and TimeKeeper paths read the snapshot, so they consume
  NAV-PVT unchanged. The companion's current fix now carries hAcc as its
  accuracy. A NAV-PVT also counts as burst activity, and a valid one marks the
  burst's fix with the same one-fix-per-burst rule as NMEA.
- Configuration read back. A closing CFG-VALGET of the RAM layer compares
  every value that was set; `gps.ubx.verified` and the console report it.
- UPD-SOS. The restore status is polled after configuration.
- CFG-RST replaces `$PCAS10` for hot, warm and cold restarts on UBX.
- Simulator: the virtual M10 in `sim/ubx_m10.{h,cpp}`, `seed gps_model`, the
  M10 seeds and queries in `docs/sim.md`, and the Port A rail hook. Host
  test `gps-ubx-m10` drives the same state machine and `Ubx::Detect`.
- Scenarios: `gps-ubx-detect`, `gps-ubx-mia-autobaud`,
  `gps-ubx-fallback-nmea`, `gps-ubx-nak`, `gps-ubx-cold-start`. Fuzz target
  `fuzz.gps.ubx` runs the host fuzzer seeds and three of them.
- Deviation: the CASIC path is byte-identical in what it configures, but the
  receiver now also receives up to two 8-byte MON-VER polls at GPS start, and
  its configuration starts up to two seconds later. The CASIC receivers ignore
  a UBX frame (it has neither the CASIC sync nor a `$`). Three motion
  scenarios that assert `uart.count 0` clear the journal after detection.
- The duty cycle (standby or rail cycling) waits until the receiver is
  identified and its configuration queue is idle. `$PCAS12` goes only to the
  NMEA (CASIC) backend; from PR 3 a u-blox receiver gets RXM-PMREQ instead. On a CASIC
  receiver with a duty policy this delays the first standby by up to the two
  second detection window. The power gate stays inside its threshold
  (`gps-duty-standby-cycle` -1.61 %).
- Verification: the five UBX scenarios and all 36 certified StickS3 GPS
  scenarios pass on the plain simulator; the ten power-gate scenarios pass
  `compare.py`. `gps-motion-detector` shortens its held-still wait by the
  2.5 s identification baseline it now carries.
- Deviation: the receiver is not positively identified as CASIC. CASIC has a
  MON-VER of its own, but polling it would add CASIC traffic no setting asked
  for, so an unidentified receiver keeps the NMEA path exactly as before.

### PR 3 (`feat/ubx-3-features`)

New settings, all console and companion only (no menu change, so no
screenshots):

| Setting | Wire id | NVS key | Type | Default | Values |
| --- | --- | --- | --- | --- | --- |
| `GPS_GNSS` | 48 | `gps_gnss` | U8 | 0 | 0 follows `GPS_CONSTEL`, else a mask: 1 GPS, 2 BeiDou, 4 GLONASS, 8 Galileo, 16 QZSS, 32 SBAS |
| `GPS_PSM` | 49 | `gps_psm` | U8 | 0 | 0 off, 1 cyclic tracking, 2 on/off |
| `GPS_ANA` | 50 | `gps_ana` | bool | false | AssistNow Autonomous |

The CASIC path reads none of them, so it is unchanged.

- `Ubx::plan()` turns the settings into three VALSETs, all written to RAM and
  BBR, plus one VALDEL and a closing VALGET:
  - base: NAV-PVT output, AssistNow Autonomous, rate and dynamic model;
  - signals: then a 500 ms settle (IFD 4.9.21);
  - power: OPERATEMODE last (IM-MAX 3.6.2.6).
  - The VALDEL removes from BBR every key the settings leave alone, so a value
    an earlier session stored does not outlive its setting. The VALGET reads
    everything back.
- Constellations (4):
  - `GPS_GNSS` wins. Otherwise `GPS_CONSTEL` maps across (GPS, BeiDou,
    GLONASS, plus QZSS and SBAS with GPS), and Galileo is left to the
    receiver, because CASIC has no Galileo bit.
  - BeiDou is B1I unless GLONASS is on, then B1C (IM-MAX 2.1.3).
  - A mask with QZSS or SBAS but no GPS, or with no major system, is refused
    before anything is sent, and the receiver keeps its preset.
  - At most four majors exist, so the M10 limit is implicit in the mask.
- Rate (5): `GPS_RATE` is CFG-RATE-MEAS. It is clamped to the highest rate the
  constellation set supports at the default CPU clock (DS Table 2): 18 Hz
  single, 10 Hz pairs, 8 Hz three with B1C, 6 Hz three with GLONASS, 4 Hz
  four, and 3 Hz with B1I.
- Dynamic model (5): `GPS_PLATFORM` 1 to 4 maps to DYNMODEL 0 portable,
  2 stationary, 3 pedestrian, 4 automotive (IFD Table 23).
- Power save (6):
  - `GPS_PSM` 1 is PSMCT, and forces the rate to 1 Hz or 2 Hz (IM-MAX
    3.6.2.3). `GPS_PSM` 2 is PSMOO, with POSUPDATEPERIOD set to the duty
    interval, at least 5 s.
  - Power save is refused with B1C (RN 7.2), so it stays off with four
    constellations, and the console says so.
  - The duty Standby policy on UBX sends RXM-PMREQ: duty interval, backup and
    force flags, UART RX wake. It never sends `$PCAS12`.
- AssistNow Autonomous (7): off by default, an opt-in (revised after review).
  - The data sheets support it on GPS, Galileo, GLONASS, QZSS and B1I, and the
    gain is a 3 to 4 s fix instead of 27 s on data up to about three days old.
  - It stays at the SPG 5.10 ROM default, off, like every other receiver
    setting furble adds. Its background computation costs current and delays
    power-off (IM-MAX 3.12.5.3).
  - Its orbits live in BBR, which an unbacked rail cut erases, so on every
    supported board it only helps across software standby.
  - B1C is not covered (RN 3.1.4). With four constellations the key is still
    sent, which the M10 model accepts; the effect on hardware is unknown.
- Warm start (8):
  - `Platform::hasGpsBackupSupply()` is false unless a board profile defines
    `FURBLE_GPS_BACKUP_SUPPLY`.
  - Without a backup supply, a planned rail cut first sends CFG-RST GNSS stop
    and UPD-SOS create, then waits up to 1 s for the answer. `disable()` waits
    the same way, inline.
  - A refused or unanswered save is recorded and not retried this session.
    That is expected on MAX-M10S and MIA-M10Q, which have no flash.
  - The restore status is polled after every configuration.
  - With a backup supply nothing is saved, and the receiver keeps BBR and the
    RTC.
- Console (9): `gps ubx` prints the following:
  - module, firmware and protocol;
  - the NAV-PVT fix and accuracy;
  - read-back result and NAK count;
  - constellation mask, rate (and clamp), power save (and refusal),
    AssistNow Autonomous;
  - backup supply, SOS restore and backup;
  - per-constellation NAV-SAT counts.
- Simulator:
  - seeds `gps_gnss`, `gps_psm` and `gps_ana`, and the `gps.ubx.*` plan
    queries;
  - the modelled board's backup supply follows `gps_m10_backup`;
  - a restored SOS backup starts warm (assumption);
  - a waking or powering receiver prints its first epoch one measurement
    period later.
- Scenarios: `gps-ubx-config`, `gps-ubx-constellation`,
  `gps-ubx-constellation-casic-map`, `gps-ubx-constellation-invalid`,
  `gps-ubx-power-save`, `gps-ubx-power-save-b1c`, `gps-ubx-standby`,
  `gps-ubx-warm-start-backup`, `gps-ubx-warm-start-sos`,
  `gps-ubx-warm-start-flash`. `gps-ubx-nak` now expects the power VALSET and
  the VALDEL to go through.
- Not done:
  - The power model has a single, fixed GPS receiver entry and no
    per-receiver selection, so no M10 entry was added to `board-currents.yaml`.
    The virtual M10 integrates its own vendor figures as `m10.avg_ua`.
  - The Android settings metadata lists only a subset of settings and was not
    extended.
  - The in-repo wiki copies are updated; the GitHub wiki needs a push.
- Found by the scenarios and fixed: a duty request that lands after the rail
  is already cut must not try to save again. It used to mark the backup as
  refused.

### Review fixes (on `feat/ubx-3-features`)

Recorded in [321-review-feedback.md](321-review-feedback.md):

- the `Ubx::Settle` tick-wrap gate;
- `Platform::canCutGpsRail()` and the GNSS restart when a cut fails;
- `$PCAS12` only on the NMEA backend;
- `status_t::time_resolved`;
- `GPS_ANA` off by default;
- the pure `GpsDemux` and the reworked fuzzer;
- the duty gate on an idle configuration queue;
- the PVT age rule;
- the per-enable SOS latch;
- four new scenarios: `gps-ubx-tick-wrap`, `gps-ubx-rail-fixed`,
  `gps-ubx-time-unresolved` and `gps-ubx-ana`.

## Hardware-only items

Every M10 behaviour above is simulator-tested and hardware-untested. On a
bench, first measure:

- detection and baud on a MAX-M10S at 9600 and a MIA-M10Q at 38400;
- the real ACK or NAK for every key furble writes, especially AssistNow
  Autonomous with B1C;
- the UPD-SOS answer on a flashless module;
- the RXM-PMREQ UART wake behaviour;
- PSMCT and PSMOO current;
- warm and hot start times with and without V_BCKP.

## Verification (PR 3 head)

| Env | Flash used / slot | DRAM / budget | Result |
| --- | --- | --- | --- |
| m5stick-s3 | 1,861,488 / 3,145,728 | 47,464 / 48,128 | PASS |
| esp32-s3-headless | 1,774,704 / 3,145,728 | 52,208 / 53,248 | PASS |
| m5stick-c | 1,502,784 / 1,740,800 | 55,036 / 55,296 | PASS |
| m5stick-c-plus | 1,552,304 / 1,740,800 | 55,012 / 55,296 | PASS |
| m5stack-core | 1,626,352 / 1,740,800 | 55,140 / 56,320 | PASS |
| m5stack-core2 | 1,911,184 / 6,291,456 | 56,736 / 57,344 | PASS |
| waveshare-s3-eth | 1,708,976 / 6,291,456 | 51,588 / 52,224 | PASS |

- The UBX code fits every release image, including the 4 MB slots, so it is
  not gated behind a CMake profile.
- On the StickS3 the UBX objects are 8.7 kB (`FurbleGPSUbx`) and 8.7 kB
  (`GpsUbx`) of text. The image grew from 1,856,192 bytes (PR 2) to 1,861,488
  bytes (PR 3).
- `stack_budget.py --env m5stick-s3`: the gps task needs 3,424 of 4,096
  bytes; PR 2 needed 3,408.
- Host: all 188 CTest cases pass, and `run_sanitized_host_tests.sh --clean` passes all 186 sanitized cases. `gps-ubx` passes 245 checks and
  `gps-ubx-m10` 61.
- Fuzzing:
  - `check_fuzz.py --smoke` host targets: 16 checks pass.
  - The full `gps_ubx_fuzz` seed set: 7 seeds at 5,000 iterations, 0
    failures.
  - `fuzz.gps.ubx` passes and is merged into `fuzz_results.json`: 11 of 11
    targets.
  - The StickS3 UI fuzz smoke (seeds 1 and 2, touch and no-touch) passes.
- Simulator: all 225 certified StickS3 e2e scenarios pass on the plain build,
  including the 15 `gps-ubx-*` scenarios. All 10 power-gate scenarios pass
  `compare.py`.
