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
| ACK-ACK / ACK-NAK | 05 01 / 05 00 | 2 | 3.9.1 / 3.9.2 | Configuration result, within one second (3.5.1) |
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
  3.5.1), three attempts, and an optional settle time after the ACK.
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
  identified, so a u-blox receiver is never sent `$PCAS12`. On a CASIC
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
