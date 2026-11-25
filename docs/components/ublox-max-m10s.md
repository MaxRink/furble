# u-blox MAX-M10S

**Part and package.** u-blox MAX-M10S, a concurrent GNSS module in the MAX form
factor, 10.1 x 9.7 mm, 2.5 mm high, 0.5 g, ROM firmware SPG 5.10 (data sheet
Table 21 page 17, the typical column of dimensions A, B and C, and the document
information page 2). The pad count was not counted and is UNVERIFIED.

**Role in furble.** One of the two M10 receivers furble drives. The protocol is
UBX over UART, with the same codec for both modules
(`lib/furble/protocol/GpsUbx.h`, `src/FurbleGPSUbx.cpp`).

**Documents.** [IFD], [DS-MAX], [IM-MAX], [RN-M10]

## Documents used

Page numbers are the document's own, which match the PDF page one to one in all
four.

| Key | Title | Revision or date | URL | Retrieved | Opened in this change |
| :--- | :--- | :--- | :--- | :--- | :--- |
| IFD | u-blox M10 SPG 5.10 Interface description, protocol version 34.10 | UBX-21035062, R03, 27-Jun-2023, 176 pages | <https://content.u-blox.com/sites/default/files/u-blox-M10-SPG-5.10_InterfaceDescription_UBX-21035062.pdf> | 2026-11-25 | Sections 3.2, 3.4, 3.8, 3.9, 3.10.2 to 3.10.5, 3.14.9, 3.15.11, 3.15.13, 3.16.6 page 116, 3.19.1 page 122, 4.3 page 125, 4.9.11, 4.9.21 and the configuration defaults |
| DS-MAX | MAX-M10S data sheet, ROM SPG 5.10 | UBX-20035208, R08, 30-Jan-2026, 24 pages | <https://content.u-blox.com/sites/default/files/MAX-M10S_DataSheet_UBX-20035208.pdf> | 2026-11-25 | The document information page 2, section 5.3 and Table 20 page 16 for the default interface settings |
| IM-MAX | MAX-M10S integration manual | UBX-20053088, R05, 28-Apr-2026, 102 pages | <https://content.u-blox.com/sites/default/files/MAX-M10S_IntegrationManual_UBX-20053088.pdf> | 2026-11-25 | Table 2 page 9 for the configuration memories, section 2.1.4 pages 11 to 12 and section 3.2.1 pages 24 to 25 for the baud rate |
| RN-M10 | u-blox M10 ROM 5.10 release notes | UBX-22001426, R02, 17-Jan-2023, 15 pages | <https://content.u-blox.com/sites/default/files/u-blox-M10-ROM-5.10_ReleaseNotes_UBX-22001426.pdf> | 2026-11-25 | Section 7 page 15, the known limitations and issues, and its revision history |

The IFD is the protocol reference because both modules run the same ROM. The
data sheet and the integration manual are the module-level documents: they
describe the receiver as shipped, which is not always the ROM default.

## Facts furble depends on

| # | Fact | Document and section | Code | State |
| :--- | :--- | :--- | :--- | :--- |
| 1 | A UBX frame is 0xB5 0x62, a class, an id, a two-byte length and a payload | IFD 3.2 UBX frame structure | `lib/furble/protocol/GpsUbx.h:26`, `:27`, `:28` | VERIFIED |
| 2 | The checksum is two bytes over class, id, length and payload | IFD 3.4 UBX checksum | `lib/furble/protocol/GpsUbx.h:29` | VERIFIED |
| 3 | The message classes used are NAV 0x01, RXM 0x02, ACK 0x05, CFG 0x06, UPD 0x09 and MON 0x0A | IFD 3.8 UBX messages overview | `lib/furble/protocol/GpsUbx.h:42` to `:47` | VERIFIED |
| 4 | UBX-ACK-ACK is class 0x05, id 0x01 and UBX-ACK-NAK is class 0x05, id 0x00 | IFD 3.9.1 and 3.9.2 | `lib/furble/protocol/GpsUbx.h:49`, `:50` | VERIFIED |
| 5 | UBX-CFG-RST is 0x06 0x04, UBX-CFG-VALDEL 0x06 0x8C, UBX-CFG-VALGET 0x06 0x8B and UBX-CFG-VALSET 0x06 0x8A | IFD 3.10.2 to 3.10.5 | `lib/furble/protocol/GpsUbx.h:51` to `:54` | VERIFIED |
| 6 | UBX-MON-VER is 0x0A 0x04, UBX-NAV-PVT is 0x01 0x07 and UBX-NAV-SAT is 0x01 0x35 | IFD 3.14.9, 3.15.11, 3.15.13 | `lib/furble/protocol/GpsUbx.h:55` to `:58` | VERIFIED |
| 7 | UBX-RXM-PMREQ is class 0x02, id 0x41, the power management request used for standby. The payload is a version byte, a 32-bit duration in ms capped at 12 days where 0 waits for a wakeup pin, a flags word with `backup` in bit 1 and `force` in bit 2, and a wakeup source word with `uartrx` in bit 3, `extint0` in bit 5, `extint1` in bit 6 and `spics` in bit 7 | IFD 3.16.6, page 116 | `lib/furble/protocol/GpsUbx.h:57`, `src/FurbleGPS.cpp` standby path | VERIFIED |
| 8 | UBX-UPD-SOS is class 0x09, id 0x14, and carries five forms: poll backup restore status with no payload, create backup in flash and clear backup in flash with a one-byte command 0 and three reserved bytes, and the two receiver answers | IFD 3.19.1, page 122 | `lib/furble/protocol/GpsUbx.h:58`, `src/FurbleGPS.cpp` save-on-shutdown path | VERIFIED |
| 9 | The configuration layers are RAM, BBR, flash and default, stacked by priority, and the VALSET `layers` byte selects them with bit 0 RAM, bit 1 BBR and bit 2 flash | IFD 4.3, page 125 and the `layers` field in IFD 3.10.5, page 55 | `lib/furble/protocol/GpsUbx.h:60` to `:63`, written as `LAYER_RAM \| LAYER_BBR` in `src/FurbleGPSUbx.cpp` | VERIFIED |
| 10 | A configuration that has to survive a rail cut cannot be stored in flash on this module. The integration manual's table of memories lists only RAM and BBR, and the IFD says the flash layer exists only where there is a usable external flash. No document asserts the absence of external flash, so this is an inference from the omission, not a quoted statement | IM-MAX Table 2 page 9; IFD 4.3 page 125 | `src/FurbleGPSUbx.cpp` configuration path | UNVERIFIED |
| 11 | The module runs its UART at 9600 baud by default, which the integration manual calls different from the firmware default, kept for backwards compatibility with earlier MAX generations. The data sheet states the same in its default interface settings | IM-MAX 2.1.4 pages 11 to 12 and 3.2.1 pages 24 to 25, with Table 16 page 25; DS-MAX 5.3 page 16, Table 20 | `src/FurbleSettings.cpp` GPS baud default | VERIFIED |
| 12 | The key `MSGOUT_NAV_PVT_UART1` is 0x20910007 and defaults to 0, so UBX-NAV-PVT is not sent on UART1 until furble enables it | IFD 4.9.11, Table 19 page 138 for the key, and the CFG-MSGOUT defaults in Table 69 page 166 with its caption on page 168 | `lib/furble/protocol/GpsUbx.h:106` | VERIFIED |
| 13 | ROM SPG 5.10 refuses power save combined with BeiDou B1C: a PSM configuration, both cyclic tracking and on/off, that includes B1C is rejected by the receiver, which answers with an explanatory ERROR message. The workaround the note gives is B1I with PSM. The same section also records that the CS pin is not a wake-up source in PSM | RN-M10 section 7 page 15, and section 7.2 page 15 in particular | `lib/furble/protocol/GpsUbx.h:430` `psm_refused`, set in `GpsUbx.cpp` and reported at `src/FurbleGPSUbx.cpp:158` | VERIFIED |

## Where the code implements it

- `lib/furble/protocol/GpsUbx.h:1` to `:60`: frame constants and every message
  id with its IFD section.
- `lib/furble/protocol/GpsUbx.cpp`: encoder and parser, and the power-save plan
  that mirrors the errata in row 13.
- `src/FurbleGPSUbx.cpp:72`: the write path with a bounded transmit wait.
- `src/FurbleGPSUbx.cpp:158`: the refusal log when the plan sets
  `psm_refused`.
- `tests/host/gps_ubx_m10_test.cpp`: the M10 specific cases.

## Citation gaps

| Constant or fact | Driver location | Why no section | Page row |
| :--- | :--- | :--- | :--- |
| none | - | The UBX codec is under `lib/furble/protocol`, not `lib/hal`, so the gate does not scan it. Every message id carries an IFD section in the header. | - |

## Facts still unverified

- The pad count. Data sheet Table 21 page 17 gives the outline dimensions but
  the pin layout is a drawing and was not read.
- Whether the module has an external flash at all. Row 10 is an inference.
- Whether a configuration written to BBR survives a full rail cut on this
  module. The IFD says BBR is preserved while a backup supply is provided; no
  document read here says whether the MAX-M10S has V_BCKP connected on the
  boards furble drives, so `src/FurbleGPS.cpp` saves the BBR with UPD-SOS before
  a rail cut rather than trusting it.
- The antenna supervisor and the external LNA behaviour, and the ANTSTATUS
  encodings behind them.
- The GNSS signal defaults the module ships with. The IFD ROM defaults
  (Table 79 page 171) disable the BeiDou and GLONASS constellations while
  leaving BDS B1C and GLO L1 enabled, which is not a set the data sheet
  describes, and the module level default is the other module's business
  (`ublox-mia-m10q.md`).

## Redistribution

u-blox documents carry a u-blox copyright notice and download terms that do not
grant redistribution, so all four are cited and not stored, per the rule in
[README.md](README.md#committed-documents).

[IFD]: https://content.u-blox.com/sites/default/files/u-blox-M10-SPG-5.10_InterfaceDescription_UBX-21035062.pdf
[DS-MAX]: https://content.u-blox.com/sites/default/files/MAX-M10S_DataSheet_UBX-20035208.pdf
[IM-MAX]: https://content.u-blox.com/sites/default/files/MAX-M10S_IntegrationManual_UBX-20053088.pdf
[RN-M10]: https://content.u-blox.com/sites/default/files/u-blox-M10-ROM-5.10_ReleaseNotes_UBX-22001426.pdf
