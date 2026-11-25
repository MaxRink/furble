# 332 - GNSS configuration audit against the vendor documents

## Motivation

Six vendor documents for the two GNSS receivers furble supports were located
after the CASIC and u-blox configuration code had already shipped. The code
cites documents it was written from, but no review had checked every command and
every configuration key against the revision that a current M5Stack unit is
documented against. The audit was asked to answer seven questions with evidence,
fix what is wrong with the smallest safe change, and report the rest so the
component and power documents can be corrected by their owners.

## Documents

| Document | Revision | Used for |
| --- | --- | --- |
| CASIC multi-mode satellite navigation receiver protocol specification, `CASIC_Multi-mode_Satellite_Navigation_Receiver_Protocol_Specification.pdf` (m5stack-doc mirror) | V6.3.2, 2025-06-24, 144 pages | `$PCAS03`, `$PCAS04`, `$PCAS10`, `$PCAS11`, `$PCAS12`, CFG-NAVX absence |
| CASIC multi-mode satellite navigation receiver protocol specification, `CASIC多模卫星导航接收机协议规范-20210301.pdf` | V4.2.0.3, 2021-03-01, 85 pages | `$PCAS03`, `$PCAS04`, `$PCAS10`, CFG-NAVX `navSystem` bits |
| AT6668/AT6668U data sheet, `DS-AT6668 V1.4` | V1.4, 19 pages | cold start, chip variants, protocol absent |
| u-blox M10 SPG 5.10 interface description, `UBX-21035062` | R03 | UBX keys, CFG-VALSET semantics, CFG-SIGNAL reset |
| u-blox M10 ROM 5.10 release notes, `UBX-22001426` | R02, 2023-01-17 | PSM with B1C, GLO L1OF with B1I, AssistNow with B1C |
| MIA-M10Q data sheet, `UBX-22015849` | R08, 2026-01-30 | default constellation, rate table, TTFF, signal rules |

Page numbers below are PDF page numbers, which is how the audit questions cite
them. The CASIC V6.3.2 footer prints a smaller number (its PDF page 43 is
printed 37 of 144), while V4.2.0.3 and both u-blox documents agree with the
file. Both CASIC revisions print `$PCAS10` as a restart and nothing else.

## 1. `$PCAS10` is a restart, never a working mode: nothing to fix

- V6.3.2 section 2.7.9 page 48 (printed 42) and V4.2.0.3 section 1.6.8 page 31
  define `$PCAS10,rs` as "receiver restart": 0 hot, 1 warm, 2 cold and 3
  factory. The argument selects a start type, not a navigation system.
- The driver sends it in exactly one place, `GPS::restart()`
  (`src/FurbleGPS.cpp:1267`, the write at `:1279`), and the comment above it says
  the same. It is reached from the diagnostics "Hot restart" button
  (`src/FurbleUI.cpp:10215`, mode 0) and from the simulator driver's restart
  step. No configuration path sends it, so no configuration write can be lost
  behind a reboot.
- `restart()` refuses mode 3 even though both revisions define it. That is
  deliberate: a factory start clears the receiver's whole configuration, and no
  caller asks for it.
- `tests/test_gnss_command_contract.py` now pins the rule: the only `$PCAS10`
  literal in `src/FurbleGPS.cpp` must sit inside `GPS::restart()`.

What is wrong is the surrounding prose in `docs/power/gps-imu.md`, which another
agent owns. See the fold-in list at the end.

## 2. `$PCAS04` encoding: the code sends the decimal mode, and 1 to 7 is valid on both revisions

- V4.2.0.3 section 1.6.5 page 28: `$PCAS04,mode` with one decimal argument,
  1 GPS, 2 BDS, 3 GPS+BDS, 4 GLONASS, 5 GPS+GLONASS, 6 BDS+GLONASS,
  7 GPS+BDS+GLONASS.
- V6.3.2 section 2.7.5 page 43: `$PCAS04,mode1,mode2,mode5` with one to three
  hexadecimal band bitmasks. mode1 BIT0 GPS L1C/A, BIT1 BDS B1I, BIT2 GLONASS
  L1, BIT3 Galileo E1, BIT4 BDS B1C, BIT5 QZSS L1, BIT6 SBAS L1C/A, BIT7 QZSS
  L1S.
- The code sends the decimal form. `configure()` stores
  `GPS_CONSTEL` as `m_NavxConstelVal` (`src/FurbleGPS.cpp:1622`) and builds
  `"PCAS04," + std::to_string(m_NavxConstelVal)` (`:1633` and `:2815`), bounded
  by `CONSTELLATION_MAX = 7` (`include/FurbleGPS.h:394`). The roller labels in
  `include/FurbleUI.h:848` are the V4.2.0.3 enumeration.
- An AT6668 therefore receives a value it understands. 1 to 7 are valid hex
  digits and select the same systems on a V6.3.2 receiver, because GPS, BDS and
  GLONASS sit in bits 0, 1 and 2 in the same order in both revisions. The
  agreement is a property of those three systems, not of the command.
- The same value is written to `CFG-NAVX` `navSystem` at offset 13, whose bits
  are B0 GPS, B1 BDS and B2 GLONASS (V4.2.0.3 section 2.11.8 page 68 note 4), so
  the binary and NMEA paths mean the same thing.
- No bug. The limitation is worth recording: the setting cannot express Galileo
  E1 (mode1 bit 3, value 8), BDS B1C (bit 4, value 16), QZSS (bit 5), SBAS
  (bit 6) or QZSS L1S (bit 7), and must not simply extend the decimal range,
  because 8 means "unused decimal mode" on V4.2.0.3 and "Galileo E1" on V6.3.2.
  `tests/test_gnss_command_contract.py` pins the bound at 7 with that reason.
- V6.3.2 has no CFG-NAVX (0x06 0x07). Its message table (section 3.13, PDF page
  86 onwards) lists CFG-NAV-LIMIT 0x06 0x0A, CFG-NAV-MODE 0x06 0x0B and
  CFG-NAV-BAND 0x06 0x0F, the last of which is the replacement for a
  constellation configuration. A receiver that follows V6.3.2 answers nothing to
  the CFG-NAVX query, so the three binary attempts time out and the `$PCAS04`
  and `$PCAS11` fallback runs. That is the designed path and needs no change.
  Using CFG-NAV-BAND instead would be a feature, not a fix, and needs a unit to
  confirm which revision the current module actually runs.

## 3. `$PCAS03` field count: a real bug, the prune left four sentences at their old rate

- V6.3.2 section 2.7.4 page 42 defines fifteen fields: the message id and
  fourteen rate arguments, `nGGA` to `nGST`, with two reserved slots at
  positions 12 and 13. V4.2.0.3 section 1.6.4 page 27 defines nineteen: the same
  fourteen at the same positions, then `res3`, `res4`, `res5` and `nTIM`.
- Both revisions define an empty field as "keep the receiver's existing
  configuration", and the V4.2.0.3 worked example uses empty fields.
- The code wrote eight arguments: `PCAS03,1,0,0,0,1,0,0,0` for the prune,
  `PCAS03,1,0,1,1,1,0,0,0` while the satellite page is open, and
  `PCAS03,1,1,1,1,1,1,1,1` to restore the default set
  (`src/FurbleGPS.cpp:1606`, `:3097`, `:3100` and `:3103` at the audit base).
  Eight arguments set GGA, GLL, GSA, GSV, RMC, VTG, ZDA and ANT and leave DHV,
  LPS, reserved, reserved, UTC and GST alone.
- So the `GPS_NMEA` prune, whose promise is "RMC and GGA only", only reached the
  eight sentences it named. A receiver whose defaults include GST or UTC, or
  which an earlier session had enabled, kept emitting them at the old rate. On a
  V4.2.0.3 receiver `nTIM` also stayed as it was.
- Fix: `Casic::sentenceCommand()` builds all three bodies with fourteen fields.
  The prune and satellite forms write 0 for DHV, LPS, UTC and GST, so pruning
  reaches them. The standard form writes the eight sentences a CASIC receiver
  ships with and leaves the remaining fields empty, because "default" means the
  receiver's own set and not one furble invented.
- The reserved slots stay empty, as the V4.2.0.3 example does, and the driver
  keeps writing fifteen fields rather than nineteen: V6.3.2 documents that a
  command with more configurable sentences than it defines is still recognised
  and configures the first fourteen, but there is no reason to send four
  arguments the current unit's revision does not define. A V4.2.0.3 receiver
  therefore keeps its `res3`, `res4`, `res5` and `nTIM` rates. That is the one
  remaining difference the setting cannot reach, and it is recorded here rather
  than guessed at.

## 4. UBX keys and order: two documented rejections were avoided, two were not

Vendor rules:

- RN 7.2 page 15: PSM cannot be used with BeiDou B1C. "Configuration
  combinations of PSM (both CT and OO) and GNSS configuration which include B1C
  will be rejected by the receiver and an explanatory ERROR message will be
  output."
- RN 4.3.6 page 7 and DS-MIA table 4 note 13 page 6: GLO L1OF and BeiDou B1I
  cannot be enabled together.
- RN 5.1.5 page 8: "Neither AssistNow (Online, Offline or Autonomous) nor power
  save mode is supported when using BeiDou B1C signals." DS-MIA table 5 page 6
  lists AssistNow Autonomous for B1I, not for B1C.
- IFD 4.9.21 page 152: a write to any CFG-SIGNAL item restarts the GNSS
  subsystem, so wait for the acknowledgement and 0.5 s before the next command.
- IFD 4.6 page 127: the transactionless mechanism applies each request as it
  arrives, and keys that depend on each other "can cause the configuration to be
  rejected" if sent in the wrong order. IFD 4.9.15 page 147 makes
  `CFG-PM-OPERATEMODE=FULL` the way to leave a power save mode.

Checked, no change needed:

- B1I and GLONASS are never enabled together. `Ubx::plan()` writes B1I only when
  BeiDou is asked for and GLONASS is not, and B1C when both are asked for
  (`lib/furble/protocol/GpsUbx.cpp:604` to `:606`). The signal table orders B1I
  off before B1C on before GLONASS on, so no intermediate state of the batch
  breaks the rule either. `testB1cRejections()` pins that order.
- PSM with B1C is refused by the plan, and `plan.psm_refused` reaches the console
  and the simulator as `gps.ubx.psm_refused`. The existing scenario
  `gps-ubx-power-save-b1c` covers it.
- The 0.5 s settle after the signal write is applied after the acknowledgement,
  as IFD 4.9.21 asks (`src/FurbleGPSUbx.cpp`, `SIGNAL_SETTLE_MS`).

Bug 1, AssistNow Autonomous with B1C: the plan wrote `CFG-ANA-USE_ANA=1`
whatever the signal set was, so a user with `GPS_ANA` on and BeiDou plus GLONASS
selected asked for a feature the release notes say is unsupported with B1C.
`plan.ana` now carries the value actually written, it is false with B1C, the
driver logs the refusal, and `getUbxStatus().ana` reports the effective value
rather than the requested one.

Bug 2, the order when leaving power save for B1C: a power save mode stored by an
earlier session is in RAM when this session starts, and the receiver rejects a
signal write that includes B1C while it is active. The driver sent the signal
batch first and the `OPERATEMODE=FULL` write last, so the constellation change
was refused with a NAK, nothing retried it, and the setting only took effect on
a later session. `plan.power_before_signals` is true when the plan enables B1C
with power save off, and the driver now sends the power write first. Entering
power save still writes `OPERATEMODE` last.

## 5. MIA-M10Q default constellation: the code already assumes GPS, Galileo and B1I

- DS-MIA section 1.3 page 6: "The default configuration on MIA-M10Q is concurrent
  reception of GPS, Galileo, and BeiDou B1I with QZSS and SBAS enabled."
- `GNSS_MASK_MODULE_DEFAULT` is GPS, Galileo, BeiDou, QZSS and SBAS with that
  citation (`lib/furble/protocol/GpsUbx.h:404`), and `plan()` treats BeiDou as
  B1I whenever GLONASS is off, so B1I is the signal a BeiDou selection gets, not
  an opt-in. With no constellation setting at all, `gnss_set` is 0, no signal key
  is written, and the receiver keeps its own preset, B1I included.
- No bug. `testB1cRejections()` pins that a GPS plus BeiDou request writes B1I 1
  and GLONASS 0, and that four constellations write B1C with B1I 0.
- One limitation: `GPS_GNSS` cannot ask for B1C without GLONASS, because the plan
  derives B1C from the BeiDou and GLONASS pair. Nothing in the documents forbids
  B1C alone; the setting simply has no bit for it.

## 6. The 108 s cold start: a real local observation, mislabelled, and no timeout derives from it

- The number is measured, not documented. `plans/00-hardware-experiments.md:188`
  records Experiment B part 1 on 2026-08-16: a StickS3 with a Unit GPS v1.1, a
  fresh fix, the 5 V rail cut for 60 s, then a 107.7 s re-fix. That is a cold
  re-acquisition after the receiver lost power, on that unit, in that sky view.
- The documented cold start is 23 s: AT6668 data sheet section 1.3 page 1
  ("冷启动 TTFF ≤23s") and the M5Stack Unit GPS v1.1 page ("Cold Start: 23
  seconds"). The u-blox side documents 27 s for the module default set (DS-MIA
  table 2 page 5, 23 to 28 s across the sets).
- So `docs/power/gps-profile.md`, `docs/power/board-power.md`,
  `docs/hardware-catalog.md` and the plans that call 108 s "the cold start"
  attach the right measurement to the wrong vendor term. The observation stands;
  the label does not. That is a documentation error, and those files belong to
  another agent.
- No timeout in the firmware is derived from 108 s. The only acquisition bound is
  `ACQUIRE_TIMEOUT_MS = 30 s` (`include/FurbleGPS.h:533`), and it is a silence
  bound, not a time-to-first-fix bound: an expired deadline only acts when no
  checksum-valid sentence has arrived (`src/FurbleGPS.cpp:804` to `:806`), and a
  receiver that is working prints GGA and RMC with empty fields from the moment
  it boots. A 23 s or a 108 s fix therefore neither trips it nor needs it raised.
  Raising it to 108 s would only keep the rail on longer for a receiver that is
  completely silent, which is the case it exists to bound.
- The simulator's cold start is the documented figure for the module default set,
  `TTFF_COLD_MS = 27000` (`sim/ubx_m10.cpp:25`), which matches DS-MIA table 2.
  Nothing models the 107.7 s measurement, and nothing should: it is one unit in
  one sky view.

## 7. Simulator GNSS shim: what it models and what it does not

Modelled:

- The PSM with B1C rejection. `VirtualM10::valid()` refuses a candidate state
  with a power save mode and B1C (`sim/ubx_m10.cpp:169` to `:172`), and
  `handle()` NAKs the whole VALSET. The scenario
  `gps-ubx-power-save-b1c` asserts the firmware avoids it.
- The B1I, B1C and GLONASS rule, in the same predicate.
- The GNSS subsystem restart on a signal write, as a hot restart.
- `$PCAS03` acceptance only for the exact satellite-page string
  (`sim/fake_uart.cpp:639`) and `$PCAS12` timing (`:659`). The longer
  `$PCAS03` body still contains that prefix, so the satellite fixture still
  answers.

Not modelled, for the simulation alignment work:

- Default baud. The modelled receiver answers at whatever rate the driver
  programs (`receiverBaud` 0, `sim/fake_uart.cpp:137` to `:138`, `:297`), so the
  autobaud ladder always locks on its first step. The documented default of the
  current unit is 115200 (`Unit GPS v1.1`, `ATGM336H-6N` user manual section 1.2
  page 2), and `uartBaud` merely starts at 9600 until the driver sets it. A
  scenario that proves the first-step lock on a 115200 unit has to seed
  `gps_receiver 115200` explicitly; `gps-ubx-mia-autobaud` is the working
  example at the M10Q's 38400.
- Default sentence set. The fake receiver replays one canned NMEA stream
  regardless of `$PCAS03` or `CFG-MSG`, so a prune changes nothing observable and
  the power model cannot see the sentence count. The 710 to 91 characters per
  second measurement has no model behind it.
- `$PCAS02`, `$PCAS04`, `$PCAS10` and `$PCAS11` are accepted and ignored. There
  is no constellation change, no rate change (the shim reads `GPS_RATE`, not the
  command) and no restart.
- The `$PCAS10` restart latency. A real restart stops output for about a second
  and reloads the configuration; the shim leaves the stream running.
- The explanatory ERROR of RN 7.2. The virtual M10 answers NAK and nothing else;
  the release notes describe an `ERROR` message as well.
- AssistNow with B1C, which the model does not refuse even though RN 5.1.5 says
  the combination is unsupported. The firmware no longer asks for it, so no
  scenario can hit it, but the model is more permissive than the receiver. The
  nearest coverage is a scenario that seeds `gps_ana true` together with a B1C
  mask such as `gps_gnss 15` and asserts `gps.ubx.ana 0`; `gps-ubx-ana` seeds
  AssistNow without a constellation, so it still expects 1.

## Design

- `Casic::sentenceCommand(SentenceSet)` owns the three `$PCAS03` bodies. The
  rate tables are the only place a field count appears, and the driver has no
  `$PCAS03` literal left. The enum is `PRUNE`, `SATELLITE` and `STANDARD`.
- `Ubx::PlanSummary` gains `ana`, the value actually written, and
  `power_before_signals`, the one ordering decision the signal and power writes
  need. `plan()` decides both; the driver only follows them.
- `configureUbx()` logs the AssistNow refusal next to the existing PSM refusal,
  sends the power write before the signal write when the plan asks for it, and
  reports the effective `ana` in the UBX status.

Everything else stays as it was: no new setting, no new key, no change to the
CASIC binary path, no change to the simulator.

## Tests

| Test | What it proves |
| --- | --- |
| `tests/test_gnss_command_contract.py` | `$PCAS10` only inside `GPS::restart()`; no `$PCAS03` literal in the driver; fourteen rate fields in every table; `$PCAS04` bounded at 7 with the reason |
| `gps-casic`, `testSentenceCommand()` | the three exact bodies, fifteen fields each, no checksum |
| `gps-ubx`, `testB1cRejections()` | B1C selected by BeiDou plus GLONASS; ANA off with B1C; the power write ordered before the signals; B1I keeps ANA and the power-last order; B1I off before B1C on before GLONASS on |
| `gps-ubx-m10`, `testB1cConfigOrder()` | the virtual receiver NAKs signals before `OPERATEMODE=FULL` and accepts the plan's order, ending on B1C and GLONASS with B1I off |

Red evidence, on the tree before the fixes:

- `python3 -m unittest tests.test_gnss_command_contract`: two failures,
  `PCAS03,1,0,0,0,1,0,0,0`, `PCAS03,1,0,1,1,1,0,0,0` and
  `PCAS03,1,1,1,1,1,1,1,1` in the driver and no builder in the codec.
- `gps-ubx`: `FAIL: AssistNow Autonomous is not written on with B1C`
  (1 of 281 checks) with the ANA assertion alone.
- `gps-ubx`: `FAIL: the B1C plan clears power save before it changes the
  signals` and `gps-ubx-m10`: `FAIL: the plan leaves power save before the
  signal write` with `power_before_signals` forced false.

## What to fold into the documents another agent owns

`docs/power/gps-imu.md`:

1. Line 130 to 136 says "There is no UBX path". Plan 321 merged the u-blox M10
   backend; `src/FurbleGPSUbx.cpp` is that path and `docs/sim.md` already
   documents its status keys.
2. Line 138 quotes `$PCAS03,1,0,0,0,1,0,0,0` as the prune. The body is now
   `PCAS03,1,0,0,0,1,0,0,0,0,0,,,0,0`: fourteen rate fields, because an empty
   field keeps the receiver's rate (V6.3.2 2.7.4 page 42).
3. Line 127 and line 143 list `$PCAS04` as one command without saying that
   V4.2.0.3 defines a decimal mode and V6.3.2 a hex band bitmask. The two agree
   only for 1 to 7.
4. Line 152 to 154 is accurate about `$PCAS10` being a restart with 0, 1 and 2
   and about it being reachable only from the diagnostics page.

`docs/power/gps-profile.md`:

5. Line 15 quotes the three short `$PCAS03` forms. Same correction as item 2.
6. Line 35 recommends the "minimal sentence set `PCAS03,1,0,0,0,1,0,0,0`". Same
   correction, with the reason: the old form could not turn GST or UTC off.

`docs/hardware-catalog.md`, `docs/power/board-power.md`, `docs/power/soc.md`,
`docs/power/measurement.md`, `docs/power/opportunity-matrix.md`,
`tools/power-model/README.md` and `plans/00-hardware-experiments.md`:

7. "Cold start about 108 s" should read "measured 107.7 s re-fix after a rail
   cut, against a documented 23 s cold start". The measurement and its
   conditions are in `plans/00-hardware-experiments.md:188`.

`docs/components` (the datasheet corpus):

8. The AT6668 data sheet is a chip data sheet with no protocol chapter. Its
   section 1.6 page 2 variant table is what decides which signals a unit has:
   `AT6668-6N-22` is BDS only, `AT6668-6N-32` is GPS/QZSS plus BDS, and
   `AT6668-6N-74` adds Galileo E1 and GLONASS R1. The Unit GPS v1.1 is an
   `ATGM336H-6N` module at 115200 baud, whose variants are `-32` and `-74`.
9. The current unit's protocol authority is the V6.3.2 document, which M5Stack
   links from the product page. The V4.2.0.3 document is the older revision; the
   AT6558 based first-generation unit predates both and is not in this set.

## Hardware-settled questions

These need a unit, and the code is unchanged for them:

- Whether a current AT6668 accepts the fifteen field `$PCAS03`, and how many
  characters per second the wider prune actually removes. The vendor documents
  say it must; the 710 to 91 measurement predates the wider form.
- Whether a V6.3.2 receiver answers the `CFG-NAVX` query at all, and whether the
  `$PCAS04` fallback then lands. V6.3.2 has no CFG-NAVX section.
- Whether the AT6668 stops printing before its first fix. `ACQUIRE_TIMEOUT_MS`
  depends on it printing from boot; no document states either way.
- Whether the M10 NAKs the B1C signal write while a PSM mode is active, as the
  release notes describe, or also emits the ERROR message the notes mention. The
  virtual receiver models the NAK only.

## Implementation state

Implemented on `fix/gnss-doc-audit` (plan 332):

- `$PCAS03` built from fourteen rate fields in
  `lib/furble/protocol/GpsCasic.{h,cpp}`, used by all three call sites in
  `src/FurbleGPS.cpp`.
- `Ubx::plan()` reports `ana` and `power_before_signals`;
  `src/FurbleGPSUbx.cpp` follows both and logs the AssistNow refusal.
- Three host tests and one source contract test, listed above.

Deviations from the audit as asked:

- The audit asked whether `$PCAS10` is used as a mode select. It is not, so
  there is nothing to fix and the finding is a test.
- The audit expected the AT6668 to receive a `$PCAS04` it cannot parse. It
  cannot: the decimal range 1 to 7 is also a valid hex bitmask. The real
  restriction is that the setting cannot grow past 7 in its current form, which
  is now a test rather than a code change.

## Follow-ups

- Add a `boundary_gps_test` that compiles `src/FurbleGPS.cpp` against a UART,
  NVS and platform double. There is no GPS entry in the boundary suite today, so
  the driver's sentence building is covered through the pure codec and the
  source contract test only.
- The simulator gaps in section 7 belong to the simulation alignment work.
- The four hardware questions above are the release gate for the `$PCAS03`
  change on a real unit.
