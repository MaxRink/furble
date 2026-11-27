# 512 - L5 and dual-band GNSS support

Status: implemented on `feat/l5-dual-band`. Sources were read on 2026-11-27.
Facts about this checkout were read at `18bbcb5a`.

Number 512 was free across every local and remote branch on 2026-11-27: the
highest plan number in the tree is 511 (`plans/511-review-feedback.md`), and no
branch carries a `plans/512-*` file. PR 512 is a different sequence and is
already merged; this document is a plan number, not a PR number.

## 1. Why this exists

furble configures a u-blox receiver from one fixed picture of the hardware: an
M10 firmware with L1 signals only. That picture is written into the signal table
(`lib/furble/protocol/GpsUbx.cpp`), into the rate ceiling, into the power-save
handling, and into the GPS settings page. A dual-band receiver is not that
picture, and nothing in furble knows the difference:

- The M10 firmware has no L5 signal key at all (u-blox M10 SPG 5.10 Interface
  description, UBX-21035062 R03, 4.9.21 page 152, Table 42). The F10 firmware
  has four (u-blox F10 SPG 6.00 Interface description, UBX-23002975 R02, 4.9.20
  pages 158 and 159, Table 46).
- A firmware that does not know a key refuses the whole message that names it
  with UBX-ACK-NAK (M10 IFD 3.10.4 page 53 and 3.10.5 page 55; F10 IFD 3.10.4
  pages 54 and 55). So guessing the band does not cost one key, it costs the
  batch: the rate, the dynamic model and every L1 signal in that VALSET.
- A dual-band firmware also has no GLONASS signal key and no power-save key
  (F10 IFD 4.9.20 pages 158 and 159; the interface description has no CFG-PM
  group at all). The current plan writes both.
- The rate ceiling depends on the band, not only on the constellation mask.

The result today is a receiver that answers NMEA and takes whatever
configuration survives, with the refused writes only visible as a NAK counter.
This plan makes the band a first-class capability: measured from the receiver,
carried through the signal model, gated in the UI, reported in the console and
in diagnostics, and modelled in the simulator so a wrong plan fails a test.

## 2. Documents

"IFD" and "F10-IFD" are the two interface descriptions the protocol page names.
Page numbers are the document's own, which match the PDF page one to one in all
six.

| Key | Title | Revision and date | Pages | URL | Read for |
| :--- | :--- | :--- | ---: | :--- | :--- |
| IFD | u-blox M10 SPG 5.10 Interface description, protocol 34.10 | UBX-21035062 R03, 27-Jun-2023 | 176 | <https://content.u-blox.com/sites/default/files/u-blox-M10-SPG-5.10_InterfaceDescription_UBX-21035062.pdf> | 1.2 page 10, 3.10.4 page 53, 3.10.5 page 55, 4.9.21 page 152 |
| F10-IFD | u-blox F10 SPG 6.00 Interface description, protocol 40.00 | UBX-23002975 R02, 13-Jun-2024 | 181 | <https://content.u-blox.com/sites/default/files/documents/u-blox-F10-SPG-6.00_InterfaceDescription_UBX-23002975.pdf> | 1.2 page 10, 3.10.4 pages 54 and 55, 4.9.20 pages 158 and 159, the absent CFG-PM group |
| DS-MAX | MAX-M10S data sheet, ROM SPG 5.10 | UBX-20035208 R08, 30-Jan-2026 | 24 | <https://content.u-blox.com/sites/default/files/MAX-M10S_DataSheet_UBX-20035208.pdf> | 1.3 note 13 page 6, Table 2 and 3 page 4, Table 15 and 16 page 13 |
| IM-MAX | MAX-M10S integration manual | UBX-20053088 R05 | 102 | <https://content.u-blox.com/sites/default/files/MAX-M10S_IntegrationManual_UBX-20053088.pdf> | 2.1.3, 3.6.2 |
| RN-M10 | u-blox M10 ROM 5.10 release notes | UBX-22001426 R02 | 15 | <https://content.u-blox.com/sites/default/files/u-blox-M10-ROM-5.10_ReleaseNotes_UBX-22001426.pdf> | 5.1.5 page 8, 7.2 page 15 |
| DS-F10N | NEO-F10N data sheet, L1/L5 | UBX-23002117 R05, 09-Oct-2025 | 22 | <https://content.u-blox.com/sites/default/files/documents/NEO-F10N_DataSheet_UBX-23002117.pdf> | 1.1 and 1.3 pages 4 and 5, Table 2 and 3 page 4, Table 4 and 6 page 5, Table 15 and 16 page 12 |
| IM-F10N | NEO-F10N integration manual | UBXDOC-963802114-12193 R04, 12-Dec-2024 | 72 | <https://content.u-blox.com/sites/default/files/documents/NEO-F10N_IntegrationManual_UBXDOC-963802114-12193.pdf> | 2.1.3 page 10, 2.1.4 page 11, appendix migration table page 64 |

## 3. The L5 signal keys

Every key is one bit in one byte (type L). The ids are the F10 interface
description's, 4.9.20, and the table spans two pages.

| Key | Id | Meaning | Citation |
| :--- | :--- | :--- | :--- |
| `CFG-SIGNAL-GPS_L5_ENA` | 0x10310004 | GPS L5 | F10-IFD 4.9.20 page 158, Table 46 |
| `CFG-SIGNAL-GAL_E5A_ENA` | 0x10310009 | Galileo E5a | F10-IFD 4.9.20 page 158, Table 46 |
| `CFG-SIGNAL-BDS_B2A_ENA` | 0x10310028 | BeiDou B2a | F10-IFD 4.9.20 page 158, Table 46 |
| `CFG-SIGNAL-QZSS_L5_ENA` | 0x10310017 | QZSS L5 | F10-IFD 4.9.20 page 159, Table 46 |

The F10 table also has `NAVIC_L5_ENA` 0x1031001d, which furble does not model:
the constellation mask has no NavIC bit and the module defaults leave NavIC off
([UNVERIFIED](#12-unverified) item 1).

BeiDou B2a is 0x10310028, not the 0x10310010 a naive reading of the id sequence
suggests: the F10 table skips the ids the F10 firmware does not use (the older
M8/M9 B2 id 0x1031000e and its neighbours) and assigns B2a a new one. This is
the single most likely place to write a plausible but wrong constant, so the
host test asserts all four ids by value (`tests/host/gps_ubx_test.cpp`).

## 4. How L5 interacts with the L1 keys

The interface description states the rule for the whole group: the signal
enable items are governed by their constellation enable item, and at least one
signal of a major constellation must be on (F10-IFD 4.9.20 page 158). The
integration manual states the half that matters for a dual-band part: it is not
possible to disable one of the two bands of a constellation, both are enabled
and disabled together, and an unsupported combination is refused with
UBX-ACK-NAK (IM-F10N 2.1.3 page 10). The data sheet says the same: single-band
operation is not supported, both L1 and L5 must be enabled or disabled for GPS,
Galileo, BeiDou and QZSS (DS-F10N 1.3 page 4).

So the plan writes each L5 key with the same value as its L1 sibling and lets
the constellation enable key do the switching, which is exactly how the L1
signal keys have always worked. That is the only shape the documented firmware
accepts, and it is why this change adds no band setting: a "L1 only" toggle on a
dual-band receiver would be a control the receiver refuses.

## 5. Exclusivity matrix

| Constellation or signal | Cannot run with | Where it is enforced | Citation |
| :--- | :--- | :--- | :--- |
| BeiDou B1I | BeiDou B1C, GLONASS L1OF | receiver, and furble never builds it | DS-MAX 1.3 note 13 page 6; IM-MAX 2.1.3 |
| BeiDou B1I | a NEO-F10N at all (it has B1C and B2a only) | furble deletes B1I and never writes it | DS-F10N Table 4 page 5 |
| BeiDou B1C | cyclic tracking and on/off power save | receiver | RN-M10 7.2 page 15; DS-MAX note 28 page 13 |
| BeiDou B1C | AssistNow Autonomous | furble leaves ANA off | RN-M10 5.1.5 page 8 |
| GPS L5 | GPS L1 C/A, as separate bands | receiver: both bands move together | IM-F10N 2.1.3 page 10 |
| Galileo E5a | Galileo E1, as separate bands | receiver: same rule | IM-F10N 2.1.3 page 10 |
| BeiDou B2a | BeiDou B1C, as separate bands | receiver: same rule | IM-F10N 2.1.3 page 10 |
| QZSS L5 | QZSS L1 C/A, as separate bands | receiver: same rule | IM-F10N 2.1.3 page 10 |
| QZSS, NavIC L5 | GPS disabled | receiver | IM-F10N 2.1.3 page 10; DS-F10N 1.3 page 5 |
| GLONASS | the F10 firmware | furble refuses the mask before writing | F10-IFD 4.9.20 pages 158 and 159 (no GLO key) |
| Power save modes | the F10 firmware | furble writes no CFG-PM key | F10-IFD 4.9 (no CFG-PM group); IM-F10N appendix page 64 |

Two consequences are deliberate:

- A mask that asks for GLONASS on a dual-band receiver is refused whole, exactly
  as any invalid mask is: `gnss_refused` is set, no signal key is written, and
  the receiver keeps its preset. Dropping the GLONASS bit silently would give a
  different constellation set from the one on screen.
- B1I is deleted from BBR on a dual-band receiver rather than written as zero,
  so a value an earlier session stored cannot survive, and the plan never sends
  a B1I write the module might refuse.

## 6. The update-rate ceiling

The ceiling follows the band as well as the mask.

| Receiver | Set | Ceiling | Citation |
| :--- | :--- | ---: | :--- |
| M10, L1 | one constellation | 18 Hz | DS-MAX Table 3 page 4 |
| M10, L1 | GPS and Galileo | 10 Hz | DS-MAX Table 2 page 4 |
| M10, L1 | GPS, Galileo and GLONASS | 6 Hz | DS-MAX Table 2 page 4 |
| M10, L1 | GPS, Galileo and B1C | 8 Hz | DS-MAX Table 2 page 4 |
| M10, L1 | four constellations | 4 Hz | DS-MAX Table 2 page 4 |
| M10, L1 | the B1I default set | 3 Hz | DS-MAX Table 2 page 4 |
| F10, L1 and L5 | one constellation | 20 Hz | DS-F10N Table 3 page 4 |
| F10, L1 and L5 | any multi-GNSS set | 10 Hz | DS-F10N Table 2 page 4 |

The dual-band firmware publishes two figures where the M10 publishes six, so
`maxRateHz(mask, caps)` takes the dual-band ceiling whenever `BAND_L5` is set and
keeps the M10 table otherwise. The practical effect for furble is that a dual
receiver at 5 Hz (200 ms) is held to 10 Hz rather than to the 4 Hz a four-GNSS
mask costs on an M10, and a single-constellation dual-band set may run at 20 Hz
where an M10 field would be clamped to 18 Hz. `rate_clamped` still reports every
clamp, so the change is visible in the console rather than silent.

Nothing in either document says the receiver NAKs an over-ceiling rate, only
that the set cannot run faster, so furble keeps clamping on the client side and
the simulator does not model a refusal ([UNVERIFIED](#12-unverified) item 3).

## 7. Power

The L5 band costs current, and the two module documents do not measure the same
thing, so the comparison below crosses module families.

| Figure | MAX-M10S, L1 | NEO-F10N, L1 and L5 | Citation |
| :--- | ---: | ---: | :--- |
| Acquisition, default set, 3.0 V | 11.5 mA | 26 mA | DS-MAX Table 15 page 13; DS-F10N Table 15 page 12 |
| Tracking, default set, 3.0 V | 9.5 mA | 21 mA | DS-MAX Table 15 page 13; DS-F10N Table 15 page 12 |
| Tracking, one constellation, 3.0 V | 7.5 mA | 18 mA | DS-MAX Table 15 page 13; DS-F10N Table 15 page 12 |
| Cyclic tracking, 3.0 V | 5 mA | not supported | DS-MAX Table 15 page 13; IM-F10N appendix page 64 |
| Software standby | 46 uA at V_IO 3.3 V | 49 uA | DS-MAX Table 17 page 13; DS-F10N Table 16 page 12 |
| Hardware backup | 28 uA | 31 uA | DS-MAX Table 17 page 13; DS-F10N Table 16 page 12 |

What that means for a battery remote: the dual-band part draws about 2.2 times
the L1 part's VCC current while it navigates, and it has no cyclic tracking mode
to cut that in half. The standby and backup figures are within a few
microamps, so the duty-cycle and rail-cut policies that dominate furble's GPS
energy budget keep working unchanged; what grows is the cost of every window in
which the receiver actually navigates. furble cannot avoid it: the module
cannot run single-band (DS-F10N 1.3 page 4), so "L1 only" is not a power setting
on this hardware, it is a different part.

The band is therefore not a reason to change any default that ships today: no
board in the tree carries an F10 part, and the AT6668 unit on the test bench is
L1 only. See [UNVERIFIED](#12-unverified) item 2 for the one measurement that
would settle the real cost.

## 8. Detection

Two steps, in this order, and the band is L1 until the second one says
otherwise.

1. **MON-VER names the family.** Every u-blox receiver answers the poll whatever
   its configuration. `PROTVER` is 34.10 on the M10 firmware and 40.00 on the
   F10 one (IFD 1.2 page 10, F10-IFD 1.2 page 10), and the `MOD` extension names
   the module when the protocol version is missing. `capsFromVersion()` maps
   that answer to `FAMILY_M10`, `FAMILY_F10` or `FAMILY_UNKNOWN`, and each
   family carries its bands, its power-save support and its GLONASS support.
   This is a hint: a firmware string is a string.
2. **The capability probe asks the receiver.** `CapsProbe` sends one
   CFG-VALGET for the four L5 keys at the default layer. A firmware that has
   them answers with one item each; a firmware that does not refuses the message
   with UBX-ACK-NAK, which is the documented answer for an unknown key (M10 IFD
   3.10.4 page 53, F10 IFD 3.10.4 pages 54 and 55). Nothing is written, so the
   receiver keeps its configuration either way. Two polls 1000 ms apart, then
   L1.

The configuration is built only after the probe answers (`GPS::serviceCaps`,
called from `GPS::applyConfiguration`), so no L5 key reaches a receiver that has
not advertised the band. `ubx_status_t` carries the answer: `bands`,
`bands_probed`, `family`, `psm_supported`, `l5_written`, `l5_refused`,
`psm_unsupported` and `b1i_refused`.

Failure handling, which is the part that has to be honest:

- An unknown or unnamed firmware keeps the keys furble always wrote (L1 signals,
  power save, GLONASS) and is never given L5. That is `CAPS_UNKNOWN`, and it is
  a deliberate choice over "refuse everything": a NEO-M9N answers MON-VER with
  an unnamed protocol version and does implement the L1 and GLONASS keys, so
  turning those off would be a regression, while L5 is new and unproven.
- A receiver that ignores the probe stays at L1 for the session.
- A signal batch that carries an L5 key and is NAKed anyway demotes the receiver
  to L1 for the rest of the session, sets `l5_refused`, and logs the refusal.
  The batch's L1 signals went down with it, and the closing VALGET reports that
  as `config_verified` = 2 rather than hiding it.
- Every NAK of a queued CFG message is logged with its class and id, so a
  refusal is never only a counter.

## 9. Code changes

| Area | Change | File |
| :--- | :--- | :--- |
| Protocol keys | the four L5 ids, `l5Keys()`, `isL5Key()` | `lib/furble/protocol/GpsUbx.h`, `.cpp` |
| Capability model | `BAND_L1`, `BAND_L5`, `FAMILY_*`, `Caps`, `CAPS_UNKNOWN`, `CAPS_M10`, `CAPS_F10`, `capsFromVersion()` | same |
| Probe | `CapsProbe`, a two-poll one-shot with an L1 answer on silence | same |
| Rules | `gnssValid(mask, caps)` refuses GLONASS without GLONASS keys; `maxRateHz(mask, caps)` takes the band ceiling | same |
| Plan | `Request::caps`; the L5 rows follow their constellation; B1I is deleted and never written on a dual band; no CFG-PM key without power save; `PlanSummary::bands`, `psm_unsupported`, `b1i_refused`, `l5_refused` | same |
| Driver | MON-VER to caps, the probe stage in `serviceConfig`, NAK reporting, the demotion path, the new status fields | `src/FurbleGPSUbx.cpp`, `src/FurbleGPS.cpp`, `include/FurbleGPS.h` |
| Console | `gps ubx` reports `family`, `bands` and its source, `power_save_supported`, `l5_signals`, and the B1I note | `src/FurbleConsole.cpp` |
| UI | the receiver diagnostics page shows `ubx l1` or `ubx l1+l5` (with `(guess)` before the probe answers); the power-save rollers are disabled for a receiver that has no power save mode, refreshed when the GPS page opens | `src/FurbleUI.cpp`, `include/FurbleUI.h` |

There is no new setting, no NVS key and no wire id. The band is a capability,
not a preference, and the settings contract gate stays as it is.

## 10. Simulator

`sim/ubx_m10.{h,cpp}` is the virtual receiver. It now models two variants:

| Variant | `seed gps_model` | Firmware strings | Keys | Current |
| :--- | :--- | :--- | :--- | :--- |
| M10, L1 only | `m10`, `m10-mia` | `ROM SPG 5.10`, `PROTVER=34.10` | no L5 key, no `CFG-PM` gap, GLONASS present | 9.5 mA tracking |
| F10, L1 and L5 | `f10` | `ROM SPG 6.00`, `PROTVER=40.00`, `MOD=NEO-F10N` | four L5 keys, no GLONASS key, no `CFG-PM` key | 21 mA tracking |

The model refuses what the documents refuse: an L5 key on the L1 variant
(unknown key), a plan that splits one constellation's bands, B1I on the dual
variant, any GLONASS write and any power-save write on the dual variant. It also
publishes its own rate ceiling per variant so the host test can check furble
against the data sheet rather than against itself, and it answers the capability
probe the way each firmware would.

Three scenarios, one per behaviour:

| Scenario | Receiver | What it proves |
| :--- | :--- | :--- |
| `sim/scenarios/e2e/gps-ubx-dual-band.txt` | `f10` | the probe is answered, `bands` is 3, the four L5 keys are written beside their L1 siblings, no PM key is sent, nothing is NAKed, the read-back matches |
| `sim/scenarios/e2e/gps-ubx-l1-only-refuses-l5.txt` | `m10` | the probe is refused with exactly one NAK, `bands` stays 1, no L5 key is written, the L1 configuration still verifies |
| `sim/scenarios/e2e/gps-ubx-dual-band-glonass-refused.txt` | `f10` | a GLONASS mask is refused before it is written: `gnss_refused` is set, nothing is NAKed, the receiver keeps its preset |

## 11. Tests

Host:

- `tests/host/gps_ubx_test.cpp` `testDualBand()`: the four key ids, `isL5Key`,
  `capsFromVersion()` for M10, F10, a module name without `PROTVER` and an
  unnamed firmware, `gnssValid()` with caps, both `maxRateHz()` tables, a
  dual-band plan (L5 rows, deleted L5 rows, no B1I write, no GLONASS key, no PM
  key, both bands reported), a BeiDou plan, a power-save refusal, a GLONASS
  refusal, the rate clamp at 10 Hz and the 20 Hz single-constellation case, and
  an L1 plan that names no L5 key in either list. The whole `CapsProbe` state
  machine is covered there too.
- `tests/host/gps_ubx_m10_test.cpp` `testDualBandVariant()`: MON-VER for both
  variants, the probe answer and the probe refusal, each L5 key accepted on the
  dual variant and refused on the L1 one, the four documented exclusions, a
  whole dual-band signal batch accepted and read back, the two ceilings, and the
  current and TTFF figures.
- `tests/host/fuzz/gps_ubx_fuzz.cpp`: a fifth invariant builds a plan from a
  random request, including random capabilities, and fails if the plan names a
  key the capability says the firmware lacks, or plans a rate above the band
  ceiling.

Without the change the first two suites do not compile against the new model,
and the fuzz invariant fails on any L1 request, because the old plan wrote no L5
key but also had no capability to check.

Simulator: the three scenarios above, run with `sim/scripts/run-e2e.sh` and
registered in `sim/scenarios/manifest.json`. `check_sim_scenarios.py` passes.

## 12. UNVERIFIED

1. **NavIC L5.** The F10 table lists `CFG-SIGNAL-NAVIC_ENA` 0x10310026 and
   `NAVIC_L5_ENA` 0x1031001d (F10-IFD 4.9.20 page 159), and the NEO-F10N data
   sheet lists NavIC SPS-L5 (Table 4 page 5), but furble has no NavIC bit in the
   constellation mask and does not write either key. Whether a production
   NEO-F10N ships NavIC on or off is not stated in the documents read.
2. **What the L5 band actually costs on the bench.** The 21 mA against 9.5 mA
   comparison crosses two modules and two firmware families, and the F10N
   cannot be configured single-band to measure the delta (DS-F10N 1.3 page 4).
   No measurement on any part is claimed here.
3. **Whether the receiver NAKs an over-ceiling navigation rate.** Both data
   sheets give a maximum navigation update rate; neither says what a CFG-RATE
   write above it is answered with. furble clamps on the client side and the
   simulator accepts any rate at or above 25 ms.
4. **Which combinations the F10 firmware refuses beyond the band pairs.** The
   NEO-F10N manual states that unsupported combinations are refused and that
   "inv sig cfg" is reported (2.1.3 page 10), without listing them. The model
   enforces the band pairs, the absence of B1I and the absence of GLONASS keys,
   which is what the documents state, and nothing more.
5. **`CFG-SIGNAL-GPS_L5` health override.** The manual documents a
   configuration string that makes the receiver ignore the pre-operational GPS
   L5 health status (2.1.4 page 11, Table 4). The key in that frame is
   0x10320001, in group 0x32, which the F10 interface description does not
   name, and u-blox marks the override as the customer's own risk. furble does
   not implement it: enabling the band is not the same as forcing unhealthy
   satellites into the fix. Whether L5 enters a real fix without it is open.
6. **The virtual receiver's MON-VER extension text.** `PROTVER`, `FWVER` and
   `MOD` follow the two interface descriptions; the `GPS;GAL;BDS` and
   `SBAS;QZSS;NAVIC` extension strings are this model's, not a capture from a
   production module.
7. **The other dual-band vendors.** A Quectel LC29H and a Unicore UC6580 are
   dual-band receivers furble may meet, and neither speaks UBX, so this change
   does not configure them: they stay on the NMEA path. Their configuration
   protocols were not read in this pass and no claim is made about them.
8. **Which module `MOD=` names on each part.** The model uses `MAX-M10S`,
   `MIA-M10Q` and `NEO-F10N`, which are the module names, not a captured
   `MOD=` value. `capsFromVersion()` also accepts `MAX-F10S` and `DAN-F10N`,
   whose `MOD=` strings are equally unconfirmed.
9. **An M10 with an L5 key.** The M10 interface description has none, so the
   M10 firmware cannot have one; a future SPG release on the same RF part is not
   covered by the documents read. The probe handles it if it happens, because it
   asks instead of assuming, but that path has no hardware behind it.

## 13. Documentation updated in this PR

| Page | Change |
| :--- | :--- |
| `docs/protocols/gnss-ubx.md` | the four L5 keys, the band model, the capability probe, the exclusivity matrix, the band-aware ceiling |
| `docs/components/ublox-max-m10s.md` | L1-only statement, the NAK on an unknown key, the probe, cross-link |
| `docs/components/ublox-mia-m10q.md` | the same two facts for the MIA variant |
| `docs/components/ublox-neo-f10n.md` | new page for the dual-band family furble can now configure |
| `docs/components/README.md`, `docs/README.md` | index row for the new page |
| `docs/supported-hardware.md`, `README.md`, `docs/wiki/` | the GNSS section: which receivers furble configures and which it only reads |
| `docs/console-commands.md` | the new `gps ubx` fields |
| `docs/settings-and-controls.md` | the power-save control is disabled for a receiver without it |
| `docs/sim.md` | the `f10` receiver model and its three scenarios |
| `plans/README.md` | this plan's index row |

## 14. Implementation state

Implemented on `feat/l5-dual-band`: the protocol model, the driver, the console
report, the UI gate, the simulator variants, the three scenarios, the host tests
and the fuzz invariant. Deviations from the first draft of this plan:

- The draft proposed a `GPS_BAND` setting. The documents rule it out: a
  dual-band firmware cannot run one band of a constellation, so the setting
  would offer a state the receiver refuses (IM-F10N 2.1.3 page 10). The band is
  reported and gated instead, and no setting was added.
- The draft proposed refusing every key furble could not confirm. That would
  have stopped configuring GLONASS and power save on an unnamed but working
  M10-generation receiver, so only L5 is withheld without evidence.
- `capsFromVersion()` keeps the module-name table, which the documents do not
  give as a `MOD=` value; it is a fallback for a missing `PROTVER` and is listed
  as UNVERIFIED item 8.
- The first draft made BeiDou B1C need the BeiDou mask bit on a dual-band
  receiver. That split the B1C and B2a pair whenever BeiDou was switched off,
  which the virtual receiver refused, and it was the furble plan that was wrong:
  every signal key is one and the constellation enable key does the switching.
  B1C now carries no dependency of its own on a dual-band receiver, and the
  simulator refused the earlier plan before any hardware did.
- The scenario seeds grew from GPS and Galileo to GPS, Galileo, QZSS and SBAS
  once the QZSS L5 key turned out to be the one a mask can silently leave out.
