# u-blox MIA-M10Q

**Part and package.** u-blox MIA-M10Q, a concurrent GNSS module in the MIA form
factor with ROM firmware SPG 5.10, the same protocol as the MAX-M10S. The data
sheet's mechanical section, 6 on page 19, is a drawing (Figure 4) plus a typical
weight of 0.07 g, so the outline dimensions and the pad count are UNVERIFIED.
Data sheet page 2 lists four order codes, -00B-01, -01B-01, -00B-02 and -01B-02,
all in initial production.

**Status.** shipped in firmware, hardware untested. The UBX path is built into
every image and the receiver is autodetected; no physical MIA-M10Q has been run.

**Role in furble.** The second M10 receiver furble drives. It shares the UBX
codec with the MAX-M10S and differs in module settings, most visibly the default
baud rate (`lib/furble/protocol/GpsUbx.h:6` to `:16`).

**Documents.** [IFD], [DS-MIA], [IM-MIA], [RN-M10]

## Documents used

Page numbers are the document's own, which match the PDF page one to one in all
four.

| Key | Title | Revision or date | URL | Retrieved | Opened in this change |
| :--- | :--- | :--- | :--- | :--- | :--- |
| IFD | u-blox M10 SPG 5.10 Interface description, protocol version 34.10 | UBX-21035062, R03, 27-Jun-2023, 176 pages | <https://content.u-blox.com/sites/default/files/u-blox-M10-SPG-5.10_InterfaceDescription_UBX-21035062.pdf> | 2026-11-25 | Front matter, sections 3.2 to 3.19, 4.9.21 page 152 and the configuration defaults page 171 |
| DS-MIA | MIA-M10Q data sheet | UBX-22015849, R08, 30-Jan-2026, 27 pages | <https://content.u-blox.com/sites/default/files/documents/MIA-M10Q_DataSheet_UBX-22015849.pdf> | 2026-11-25 | Section 1.3 page 6 with Table 4, Table 5 and footnote 13, Table 21 page 18, section 6 page 19 |
| IM-MIA | MIA-M10Q integration manual | UBX-21028173, R05, 30-Jan-2026, 99 pages | <https://content.u-blox.com/sites/default/files/documents/MIA-M10Q_IntegrationManual_UBX-21028173.pdf> | 2026-11-25 | Section 2.1.3 page 11 and section 3.2.1 page 26 |
| RN-M10 | u-blox M10 ROM 5.10 release notes | UBX-22001426, R02, 17-Jan-2023, 15 pages | <https://content.u-blox.com/sites/default/files/u-blox-M10-ROM-5.10_ReleaseNotes_UBX-22001426.pdf> | 2026-11-25 | Section 7.2 page 15, the PSM errata |

## Facts furble depends on

| # | Fact | Document and section | Code | State |
| :--- | :--- | :--- | :--- | :--- |
| 1 | The module runs the same ROM SPG 5.10 protocol, so the IFD is the reference for both modules | IFD front matter, protocol version 34.10 | `lib/furble/protocol/GpsUbx.h:6` to `:16` | VERIFIED |
| 2 | UBX framing, checksum, message classes and ids are the IFD facts listed on the MAX-M10S page | IFD 3.2, 3.4, 3.8, 3.9.1, 3.9.2, 3.10.2 to 3.10.5, 3.14.9, 3.15.11, 3.15.13 | `lib/furble/protocol/GpsUbx.h:38` to `:58` | VERIFIED |
| 3 | The module runs its UART at 38400 baud by default, eight bits, no parity, one stop bit, four times the MAX-M10S default. Footnote 36 gives 9600 baud in safe boot mode, and the integration manual states the same 38400 default | DS-MIA 5.3 page 18, Table 21 with footnote 36; IM-MIA 3.2.1 page 26 | `src/FurbleSettings.cpp` GPS baud default | VERIFIED |
| 4 | The default interface settings accept NMEA and UBX as input and emit only NMEA GGA, GLL, GSA, GSV, RMC, VTG and TXT, so UBX output has to be enabled by the host before a UBX-only parser sees anything | DS-MIA 5.3 page 18, Table 21 | `src/FurbleGPSUbx.cpp` configuration path, which enables `MSGOUT_NAV_PVT_UART1` | VERIFIED |
| 5 | The module's default constellation set is concurrent GPS, Galileo and BeiDou B1I with QZSS and SBAS enabled, and the supported signals are GPS/QZSS L1C/A, Galileo E1, GLONASS L1OF and BeiDou B1I plus B1C. B1I cannot be enabled together with B1C or with GLONASS L1OF | DS-MIA section 1.3 page 6, the default sentence and Table 4, with footnote 13; IM-MIA 2.1.3 page 11 repeats both statements | `lib/furble/protocol/GpsUbx.h` signal key block | VERIFIED |
| 6 | The ROM's own defaults are not the module default: the IFD disables the BeiDou constellation and B1I while leaving B1C enabled, and disables GLONASS while leaving GLO L1 enabled. So "B1I is off at the SPG 5.10 default" is true of the ROM defaults and false of the module as shipped | IFD configuration defaults, Table 79 page 171 against DS-MIA section 1.3 page 6 | `lib/furble/protocol/GpsUbx.h` signal key block | VERIFIED |
| 7 | The constellation and signal enable keys are `CFG-SIGNAL-GPS_ENA` 0x1031001f, `GPS_L1CA_ENA` 0x10310001, `SBAS_ENA` 0x10310020, `SBAS_L1CA_ENA` 0x10310005, `GAL_ENA` 0x10310021, `GAL_E1_ENA` 0x10310007, `BDS_ENA` 0x10310022, `BDS_B1_ENA` 0x1031000d, `BDS_B1C_ENA` 0x1031000f, `QZSS_ENA` 0x10310024, `QZSS_L1CA_ENA` 0x10310012, `QZSS_L1S_ENA` 0x10310014, `GLO_ENA` 0x10310025 and `GLO_L1_ENA` 0x10310018 | IFD 4.9.21 page 152, Table 42 | `lib/furble/protocol/GpsUbx.h:91` to `:108` | VERIFIED |
| 8 | Power save is refused with BeiDou B1C, so a configuration that enables B1C cannot also use PSM | RN-M10 7.2 page 15 | `lib/furble/protocol/GpsUbx.h:514` `psm_refused` | VERIFIED |
| 9 | A configuration that has to survive a rail cut cannot be stored in flash on this module. The IFD says the flash layer exists only where there is a usable external flash and the module documents list no such memory. No document asserts the absence, so this is an inference from the omission | IFD 4.3 page 125 | `src/FurbleGPSUbx.cpp` configuration path | UNVERIFIED |
| 10 | The module has no L5 signal key at all, because the M10 firmware does not implement the band: the CFG-SIGNAL table ends at GLONASS L1. furble therefore measures the band with a CFG-VALGET of the four L5 keys before writing anything, and an MIA-M10Q answers that poll with UBX-ACK-NAK. The dual-band counterpart is [ublox-neo-f10n.md](ublox-neo-f10n.md) | IFD 4.9.21 page 152, Table 42; IFD 3.10.4 page 53 | the four L5 ids at `lib/furble/protocol/GpsUbx.h:93`, `:98`, `:102` and `:106`; `Ubx::CapsProbe` at `lib/furble/protocol/GpsUbx.h:657` | VERIFIED |

## Where the code implements it

- `lib/furble/protocol/GpsUbx.h:1` to `:16`: the header states that both modules
  run the same ROM and that the IFD governs.
- `lib/furble/protocol/GpsUbx.h:91` to `:97`: the signal keys with their IFD
  section.
- `lib/furble/protocol/GpsUbx.cpp`: shared codec.
- `tests/host/gps_ubx_m10_test.cpp`: covers the module detection and the baud
  difference between the two receivers.

## Citation gaps

| Constant or fact | Driver location | Why no section | Page row |
| :--- | :--- | :--- | :--- |
| none | - | The UBX codec is under `lib/furble/protocol`, not `lib/hal`, so the gate does not scan it. | - |

## Facts still unverified

- The outline dimensions and the pad count. DS-MIA section 6 page 19 is a drawing
  and a weight, and the drawing was not read.
- Whether the MIA-M10Q has an external flash at all. Row 9 is an inference.
- The antenna and power supply characteristics of the MIA package.
- Which module setting differs from the MAX-M10S beyond the 38400 baud default
  and the constellation default.
- Whether a configuration in BBR survives a rail cut on this module.

## Correction

An earlier revision of this page cited "DS-MIA Table 5" for the constellation
set. Table 5 on page 6 is the supported A-GNSS services table; the constellation
set is the section 1.3 text on the same page and Table 4, which is what rows 5
and 6 cite. The same revision claimed that B1I is off at the SPG 5.10 default,
which the module documents contradict and the ROM defaults table happens to
support, as row 6 separates.

## Open rows

| ID | Missing | Document and section needed | State |
| :--- | :--- | :--- | :--- |
| C-28e | The 38400 baud default, which an earlier version of this page attributed to Table 21 | DS-MIA interface section, the UART default row | open, the page and table cited did not carry it |
| C-28f | The MIA-M10Q current consumption in each mode | DS-MIA current consumption table | open, read for the default configuration only |
| C-28g | Whether the module ships with the same feature set as the MAX-M10S | DS-MIA feature table and IM-MIA | open, only the constellation default was compared |

## Redistribution

The u-blox documents carry a u-blox copyright notice and download terms with no
redistribution grant, so they are cited and not stored, per the rule in
[README.md](README.md#committed-documents).

[IFD]: https://content.u-blox.com/sites/default/files/u-blox-M10-SPG-5.10_InterfaceDescription_UBX-21035062.pdf
[DS-MIA]: https://content.u-blox.com/sites/default/files/documents/MIA-M10Q_DataSheet_UBX-22015849.pdf
[IM-MIA]: https://content.u-blox.com/sites/default/files/documents/MIA-M10Q_IntegrationManual_UBX-21028173.pdf
[RN-M10]: https://content.u-blox.com/sites/default/files/u-blox-M10-ROM-5.10_ReleaseNotes_UBX-22001426.pdf
