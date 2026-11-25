# u-blox MIA-M10Q

**Part and package.** u-blox MIA-M10Q, a concurrent GNSS module in the MIA form
factor, 10.1 x 9.7 mm, ROM firmware SPG 5.10, the same protocol as the
MAX-M10S. Pad count and mechanical drawing were not read and are UNVERIFIED.

**Role in furble.** The second M10 receiver furble drives. It shares the UBX
codec with the MAX-M10S and differs in module settings, most visibly the default
baud rate (`lib/furble/protocol/GpsUbx.h:6` to `:16`).

**Documents.** [IFD], [DS-MIA], [IM-MIA]

## Documents used

| Key | Title | Revision or date | URL | Retrieved | Opened in this change |
| :--- | :--- | :--- | :--- | :--- | :--- |
| IFD | u-blox M10 SPG 5.10 Interface description, protocol version 34.10 | UBX-21035062, R03 | <https://content.u-blox.com/sites/default/files/u-blox-M10-SPG-5.10_InterfaceDescription_UBX-21035062.pdf> | 2026-11-25 | Front matter, revision string, and the section list for chapters 3 and 4 |
| DS-MIA | MIA-M10Q Data sheet | UBX-22015849, R08 | <https://content.u-blox.com/sites/default/files/documents/MIA-M10Q_DataSheet_UBX-22015849.pdf> | not retrieved | Nothing |
| IM-MIA | MIA-M10Q Integration manual | UBX-21028173, R05 | <https://content.u-blox.com/sites/default/files/documents/MIA-M10Q_IntegrationManual_UBX-21028173.pdf> | not retrieved | Nothing |

## Facts furble depends on

| # | Fact | Document and section | Code | State |
| :--- | :--- | :--- | :--- | :--- |
| 1 | The module runs the same ROM SPG 5.10 protocol, so the IFD is the reference for both modules | IFD front matter, protocol version 34.10 | `lib/furble/protocol/GpsUbx.h:6` to `:16` | VERIFIED |
| 2 | UBX framing, checksum, message classes and ids are the IFD facts listed on the MAX-M10S page | IFD 3.2, 3.4, 3.8, 3.9.1, 3.9.2, 3.10.2 to 3.10.5, 3.14.9, 3.15.11, 3.15.13 | `lib/furble/protocol/GpsUbx.h:31` to `:58` | VERIFIED |
| 3 | The module runs its UART at 38400 baud by default, four times the MAX-M10S default | DS-MIA Table 21 | `src/FurbleSettings.cpp` GPS baud default | CITED |
| 4 | The module has no external flash, so configuration that must survive must live in the backup domain or be reapplied | IFD 4.3 and `plans/321-ublox-m10-driver.md` | `src/FurbleGPSUbx.cpp` configuration path | CITED |
| 5 | GPS, Galileo and BeiDou B1I are used; BeiDou B1C is not, and B1I is off at the SPG 5.10 default | DS-MIA Table 5 and the IFD key set | `lib/furble/protocol/GpsUbx.h` signal key block | CITED |

## Where the code implements it

- `lib/furble/protocol/GpsUbx.h:1` to `:16`: the header states that both modules
  run the same ROM and that the IFD governs.
- `lib/furble/protocol/GpsUbx.cpp`: shared codec.
- `tests/host/gps_ubx_m10_test.cpp`: covers the module detection and the baud
  difference between the two receivers.

## Citation gaps

| Constant or fact | Driver location | Why no section | Page row |
| :--- | :--- | :--- | :--- |
| none | - | The UBX codec is under `lib/furble/protocol`, not `lib/hal`, so the gate does not scan it. | - |

## Facts still unverified

- DS-MIA Table 21 and Table 5. The data sheet was not opened.
- The 38400 baud default.
- The constellation defaults and which signal keys the firmware actually writes.
- The antenna and power supply characteristics of the MIA package.
- Whether any module setting differs from the MAX-M10S beyond the baud rate.

## Redistribution

The u-blox documents carry a u-blox copyright notice and download terms with no
redistribution grant, so they are cited and not stored, per the rule in
[README.md](README.md#committed-documents).

[IFD]: https://content.u-blox.com/sites/default/files/u-blox-M10-SPG-5.10_InterfaceDescription_UBX-21035062.pdf
[DS-MIA]: https://content.u-blox.com/sites/default/files/documents/MIA-M10Q_DataSheet_UBX-22015849.pdf
[IM-MIA]: https://content.u-blox.com/sites/default/files/documents/MIA-M10Q_IntegrationManual_UBX-21028173.pdf
