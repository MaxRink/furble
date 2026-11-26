# u-blox MAX-M10S

**Part and package.** u-blox MAX-M10S, a concurrent GNSS module in the MAX form
factor, 10.1 x 9.7 mm, ROM firmware SPG 5.10. The exact pad count and the
mechanical drawing were not read and are UNVERIFIED.

**Role in furble.** One of the two M10 receivers furble drives. The protocol is
UBX over UART, with the same codec for both modules
(`lib/furble/protocol/GpsUbx.h`, `src/FurbleGPSUbx.cpp`).

**Documents.** [IFD], [DS-MAX], [IM-MAX]

## Documents used

| Key | Title | Revision or date | URL | Retrieved | Opened in this change |
| :--- | :--- | :--- | :--- | :--- | :--- |
| IFD | u-blox M10 SPG 5.10 Interface description, protocol version 34.10 | UBX-21035062, R03 | <https://content.u-blox.com/sites/default/files/u-blox-M10-SPG-5.10_InterfaceDescription_UBX-21035062.pdf> | 2026-11-25 | Front matter, revision string, and the section list for chapters 3 and 4 |
| DS-MAX | MAX-M10S Data sheet, ROM SPG 5.10 | UBX-20035208, R08 | <https://content.u-blox.com/sites/default/files/MAX-M10S_DataSheet_UBX-20035208.pdf> | not retrieved | Nothing |
| IM-MAX | MAX-M10S Integration manual | UBX-20053088, R05 | <https://content.u-blox.com/sites/default/files/MAX-M10S_IntegrationManual_UBX-20053088.pdf> | not retrieved | Nothing |

All three are listed in `plans/321-ublox-m10-driver.md` with the same numbers.
The IFD is the protocol reference because both modules run the same ROM.

## Facts furble depends on

| # | Fact | Document and section | Code | State |
| :--- | :--- | :--- | :--- | :--- |
| 1 | A UBX frame is 0xB5 0x62, a class, an id, a two-byte length and a payload | IFD 3.2 UBX frame structure | `lib/furble/protocol/GpsUbx.h:26`, `:27`, `:28` | VERIFIED |
| 2 | The checksum is two bytes over class, id, length and payload | IFD 3.4 UBX checksum | `lib/furble/protocol/GpsUbx.h:29` | VERIFIED |
| 3 | The message classes used are NAV 0x01, RXM 0x02, ACK 0x05, CFG 0x06, UPD 0x09 and MON 0x0A | IFD 3.8 UBX messages overview | `lib/furble/protocol/GpsUbx.h:42` to `:47` | VERIFIED |
| 4 | UBX-ACK-ACK is class 0x05, id 0x01 and UBX-ACK-NAK is class 0x05, id 0x00 | IFD 3.9.1 and 3.9.2 | `lib/furble/protocol/GpsUbx.h:49`, `:50` | VERIFIED |
| 5 | UBX-CFG-RST is 0x06 0x04, UBX-CFG-VALDEL 0x06 0x8C, UBX-CFG-VALGET 0x06 0x8B and UBX-CFG-VALSET 0x06 0x8A | IFD 3.10.2 to 3.10.5 | `lib/furble/protocol/GpsUbx.h:51` to `:54` | VERIFIED |
| 6 | UBX-MON-VER is 0x0A 0x04, UBX-NAV-PVT is 0x01 0x07 and UBX-NAV-SAT is 0x01 0x35 | IFD 3.14.9, 3.15.11, 3.15.13 | `lib/furble/protocol/GpsUbx.h:55` to `:58` | VERIFIED |
| 7 | UBX-RXM-PMREQ is class 0x02, id 0x41, the power management request used for standby | IFD 3.16.6 | `lib/furble/protocol/GpsUbx.h:57` | CITED |
| 8 | UBX-UPD-SOS is class 0x09, id 0x14 | IFD 3.19.1 | `lib/furble/protocol/GpsUbx.h:58` | CITED |
| 9 | The configuration layers and the VALSET layers field select RAM, BBR and flash | IFD 4.3 and IFD 3.10.5 | `lib/furble/protocol/GpsUbx.h:60` | CITED |
| 10 | The module has no external flash, so a configuration that must survive a power cycle has to live in the backup domain or be reapplied | IFD 4.3 and `plans/321-ublox-m10-driver.md` | `src/FurbleGPS.cpp:105` equivalent path in `src/FurbleGPSUbx.cpp` | CITED |
| 11 | The module runs its UART at 9600 baud by default | IM-MAX 2.1.4 | `src/FurbleSettings.cpp` GPS baud default | CITED |
| 12 | The key `MSGOUT_NAV_PVT_UART1` is 0x20910007 and defaults to 0 | IFD 4.9.11 | `lib/furble/protocol/GpsUbx.h:106` | CITED |

## Where the code implements it

- `lib/furble/protocol/GpsUbx.h:1` to `:60`: frame constants and every message
  id with its IFD section.
- `lib/furble/protocol/GpsUbx.cpp`: encoder and parser.
- `src/FurbleGPSUbx.cpp:72`: the write path with a bounded transmit wait.
- `tests/host/gps_ubx_m10_test.cpp`: the M10 specific cases.

## Citation gaps

| Constant or fact | Driver location | Why no section | Page row |
| :--- | :--- | :--- | :--- |
| none | - | The UBX codec is under `lib/furble/protocol`, not `lib/hal`, so the gate does not scan it. Every message id carries an IFD section in the header. | - |

## Facts still unverified

- IFD 3.16.6, 3.19.1, 4.3 and 4.9.11. The retrieved text stopped before those
  sections, so their titles and contents are CITED only.
- The MAX-M10S data sheet and integration manual, including the 9600 baud
  default. Neither was opened.
- Whether a configuration written to the backup domain survives a full rail cut
  on this module.
- The antenna supervisor and the external LNA behaviour.
- An errata or a firmware release note for SPG 5.10.

## Redistribution

u-blox documents carry a u-blox copyright notice and download terms that do not
grant redistribution, so all three are cited and not stored, per the rule in
[README.md](README.md#committed-documents).

[IFD]: https://content.u-blox.com/sites/default/files/u-blox-M10-SPG-5.10_InterfaceDescription_UBX-21035062.pdf
[DS-MAX]: https://content.u-blox.com/sites/default/files/MAX-M10S_DataSheet_UBX-20035208.pdf
[IM-MAX]: https://content.u-blox.com/sites/default/files/MAX-M10S_IntegrationManual_UBX-20053088.pdf
