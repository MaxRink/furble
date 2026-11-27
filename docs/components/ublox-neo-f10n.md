# u-blox NEO-F10N

**Part and package.** u-blox NEO-F10N, an L1/L5 dual-band GNSS module in the NEO
form factor, built on the u-blox F10 platform with ROM firmware SPG 6.00,
protocol version 40.00 (data sheet UBX-23002117 R05, page 4 and section 1.3;
F10 interface description UBX-23002975 R02, document information page 2). The
outline dimensions and the pad count were not read in this pass and are
UNVERIFIED.

**Status.** supported in firmware, hardware untested. The UBX path is built into
every image, the receiver is autodetected, and the dual-band branch is exercised
against the virtual receiver in `sim/ubx_m10.cpp` (`seed gps_model f10`). No
NEO-F10N has been on the bench.

**Role in furble.** The dual-band receiver furble can configure. It shares the
UBX codec with the M10 modules (`lib/furble/protocol/GpsUbx.h`,
`src/FurbleGPSUbx.cpp`) and differs in what the firmware implements: four extra
signal keys, no GLONASS keys and no power save modes. The L1-only siblings are
[ublox-max-m10s.md](ublox-max-m10s.md) and [ublox-mia-m10q.md](ublox-mia-m10q.md).

**Documents.** [F10-IFD], [DS-F10N], [IM-F10N]

## Documents used

Page numbers are the document's own, which match the PDF page one to one in all
three.

| Key | Title | Revision or date | URL | Retrieved | Opened in this change |
| :--- | :--- | :--- | :--- | :--- | :--- |
| F10-IFD | u-blox F10 SPG 6.00 Interface description, protocol version 40.00 | UBX-23002975, R02, 13-Jun-2024, 181 pages | <https://content.u-blox.com/sites/default/files/documents/u-blox-F10-SPG-6.00_InterfaceDescription_UBX-23002975.pdf> | 2026-11-27 | Sections 1.2 pages 10 to 11, 3.10.4 pages 54 and 55, 4.9.20 pages 158 and 159, and the configuration reference 4.9 for the missing CFG-PM group |
| DS-F10N | NEO-F10N data sheet, L1/L5 | UBX-23002117, R05, 09-Oct-2025, 22 pages | <https://content.u-blox.com/sites/default/files/documents/NEO-F10N_DataSheet_UBX-23002117.pdf> | 2026-11-27 | Sections 1.1, 1.2 and 1.3 pages 4 and 5 with Tables 2, 3, 4 and 6, and Tables 15 and 16 page 12 |
| IM-F10N | NEO-F10N integration manual | UBXDOC-963802114-12193, R04, 12-Dec-2024, 72 pages | <https://content.u-blox.com/sites/default/files/documents/NEO-F10N_IntegrationManual_UBXDOC-963802114-12193.pdf> | 2026-11-27 | Sections 2.1.3 page 10, 2.1.4 page 11, and the appendix migration table page 64 |

## Facts furble depends on

| # | Fact | Document and section | Code | State |
| :--- | :--- | :--- | :--- | :--- |
| 1 | The firmware implements four L5 signal keys beside the L1 ones: GPS L5 0x10310004, Galileo E5a 0x10310009, BeiDou B2a 0x10310028 and QZSS L5 0x10310017 | F10-IFD 4.9.20 pages 158 and 159, Table 46 | `lib/furble/protocol/GpsUbx.h:93`, `:98`, `:102`, `:106` | VERIFIED |
| 2 | Both bands of one constellation are enabled and disabled together, and an unsupported combination is refused with UBX-ACK-NAK plus the "inv sig cfg" warning. Single-band operation is not supported | IM-F10N 2.1.3 page 10; DS-F10N 1.3 page 4 | the L5 rows of the plan in `lib/furble/protocol/GpsUbx.cpp` | VERIFIED |
| 3 | The module receives GPS and QZSS L1C/A and L5, Galileo E1-B/C and E5a, BeiDou B1C and B2a, NavIC SPS-L5 and SBAS L1. BeiDou B1I is not among its signals | DS-F10N 1.3 page 5, Table 4, and Table 6 page 5 for the augmentation systems | the B1I delete at `lib/furble/protocol/GpsUbx.cpp`, which runs on a dual-band receiver instead of a write | VERIFIED |
| 4 | The firmware has no GLONASS signal key and no CFG-PM key at all, so a GLONASS or power-save write is refused | F10-IFD 4.9.20 pages 158 and 159, Table 46, and 4.9 for the absent CFG-PM group; IM-F10N appendix migration table page 64 states that power save is not supported | `gnssValid(mask, caps)` at `lib/furble/protocol/GpsUbx.h:592`, the `psm_unsupported` path at `:524` | VERIFIED |
| 5 | A firmware that does not know a key refuses a CFG-VALGET, VALSET or VALDEL that names it, which is how the capability probe measures the band without writing anything | F10-IFD 3.10.4 pages 54 and 55 | `Ubx::CapsProbe` at `lib/furble/protocol/GpsUbx.h:657` | VERIFIED |
| 6 | The maximum navigation update rate is 10 Hz for every multi-GNSS set and 20 Hz for a single constellation | DS-F10N Table 2 and Table 3 page 4; IM-F10N appendix page 64 repeats both figures | `maxRateHz(mask, caps)` at `lib/furble/protocol/GpsUbx.h:605` | VERIFIED |
| 7 | Tracking current at 3.0 V is 21 mA for the default set, 18 mA for one constellation, and acquisition is 26 mA. Software standby is 49 uA and hardware backup 31 uA | DS-F10N Table 15 and Table 16 page 12 | the virtual receiver's current model in `sim/ubx_m10.cpp` | VERIFIED |
| 8 | A cold start is 28 s and a hot start 2 s in the multi-GNSS default set | DS-F10N Table 2 page 4 | the virtual receiver's TTFF for the dual-band variant in `sim/ubx_m10.cpp` | VERIFIED |
| 9 | GPS L5 is pre-operational and set unhealthy, so it is not used in the navigation solution by default. Overriding the health status is a separate configuration the customer applies at their own risk | DS-F10N 1.1 page 4; IM-F10N 2.1.4 page 11 with Table 4 | not implemented; enabling the band is not the same as forcing unhealthy satellites into the fix | VERIFIED, and see below |
| 10 | The module needs a dual-band L1/L5 antenna and both bands share one RF path | IM-F10N appendix page 63 | none: it is a hardware requirement, not a setting | VERIFIED |

## Pre-operational GPS L5

The manual documents a UBX configuration string that makes the receiver ignore
the GPS L5 health status and reuse the L1 C/A status instead (IM-F10N 2.1.4 page
11, Table 4; the revert string is Table 5). furble does not send it:

- The key inside that frame is 0x10320001, in group 0x32, and the interface
  description does not name it. furble writes only keys it can name and read
  back.
- u-blox marks the override as the customer's own risk and asks for a way back.
  A camera remote's geotag is not the place to force unhealthy signals into a
  fix.

What that means in practice: on a NEO-F10N the L5 keys can be on and L5 still
contributes nothing to the first fix, because the satellites are flagged
unhealthy. **UNVERIFIED:** whether L5 reaches the navigation solution without
the override, and how much it improves the fix when it does. No document read
here quantifies the benefit, and no such hardware has been tested.

## Where the code implements it

- `lib/furble/protocol/GpsUbx.h:483`: `CAPS_F10`, the band, power-save and
  GLONASS capability of this family.
- `lib/furble/protocol/GpsUbx.h:486`: `capsFromVersion()`, which reads
  `PROTVER=40.00` or `MOD=NEO-F10N` out of a MON-VER answer.
- `lib/furble/protocol/GpsUbx.cpp`: the L5 rows of the signal table and the
  dual-band branch of `plan()`.
- `src/FurbleGPSUbx.cpp:137`: `GPS::serviceCaps`, the probe that turns a MON-VER
  hint into a confirmed band.
- `src/FurbleConsole.cpp`: `gps ubx` reports the family and the bands.
- `sim/ubx_m10.cpp`: the virtual receiver, `seed gps_model f10`.
- `tests/host/gps_ubx_m10_test.cpp`: `testDualBandVariant()`.

## Citation gaps

| Constant or fact | Driver location | Why no section | Page row |
| :--- | :--- | :--- | :--- |
| none | - | The UBX codec is under `lib/furble/protocol`, not `lib/hal`, so the gate does not scan it. Every message id and key carries an interface description section and page in the header. | - |

## Facts still unverified

- The outline dimensions and pad count.
- Whether L5 contributes to the fix without the health override (above).
- The real current draw on a furble board, which adds the antenna, the LNA
  supply and the rail switch to the module figures.
- The exact `MOD=` and firmware strings a production NEO-F10N prints. The
  virtual receiver's `PROTVER`, `FWVER` and `MOD` follow the interface
  description's format, and the rest of its extension text is the model's.
- Which unsupported signal combinations the firmware refuses beyond the band
  pairs. The manual says they are refused without listing them.

## Redistribution

u-blox documents carry a u-blox copyright notice and download terms that do not
grant redistribution, so all three are cited and not stored, per the rule in
[README.md](README.md#committed-documents).

[F10-IFD]: https://content.u-blox.com/sites/default/files/documents/u-blox-F10-SPG-6.00_InterfaceDescription_UBX-23002975.pdf
[DS-F10N]: https://content.u-blox.com/sites/default/files/documents/NEO-F10N_DataSheet_UBX-23002117.pdf
[IM-F10N]: https://content.u-blox.com/sites/default/files/documents/NEO-F10N_IntegrationManual_UBXDOC-963802114-12193.pdf
