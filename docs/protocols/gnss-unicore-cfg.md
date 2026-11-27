# Unicore `$CFG` protocol

The text command protocol of the Unicore UFirebird II family, which includes the
UC6580. The codec is `lib/furble/protocol/GpsUnicore.{h,cpp}` on top of the
shared seam in `lib/furble/protocol/GpsText.{h,cpp}`, the driver is
`src/FurbleGPSText.cpp`, and the simulator receiver is `sim/vendor_gnss.cpp`.

| Item | Value |
| :--- | :--- |
| Primary source | Unicore "UFirebird II Series Protocol Specification", UC-04-FB2 EN R1.5, November 2025. Cited as UC |
| Applicable parts | UC6580, UM670A, UM680A, UM681A (UC 3.2 page 26) |
| Codec | `lib/furble/protocol/GpsUnicore.h:17-28` |
| Plan | [388](../../plans/388-gnss-vendor-text.md) |

## Frame layout

| Offset | Size | Field | Citation |
| :--- | ---: | :--- | :--- |
| 0 | 1 | `$`, 0x24 | UC 3.1 page 24 |
| 1 | n | message name and comma-separated fields | UC 3.1 page 24 |
| 1+n | 1 | `*`, 0x2A | UC 3.1 page 24 |
| 2+n | 2 | checksum, two ASCII hex digits | UC 3.1 page 25 |
| 4+n | 1-2 | CR, LF, or both, the output always CR LF | UC 3.1 page 24 |

The checksum is the XOR of every character between `$` and `*`. An input
checksum is optional but is checked when present: a wrong one earns
`$FAIL,1` and the command is not executed (UC 3.1 page 25, 3.2 page 31). The
whole message is capped at 128 bytes (UC 3.1 page 24), which is
`GpsText::MAX_LINE` (`lib/furble/protocol/GpsText.h:46`). Unsigned integers are
decimal or hexadecimal with a leading `h` or `H` (UC 1.3 pages 3 and 4).

## Acknowledgements

| Reply | Meaning | Citation |
| :--- | :--- | :--- |
| `$OK` | the command executed | UC 3.2 Table 3-6, page 30 |
| `$FAIL,0` | wrong parameters | UC 3.2 Table 3-7, page 31 |
| `$FAIL,1` | wrong checksum | UC 3.2 Table 3-7, page 31 |
| nothing | an illegal command gets no reply at all | UC 3.2 page 31 |

**Neither `$OK` nor `$FAIL` names the command it answers.** That is why the seam
allows exactly one command in flight on a Unicore receiver and why a timed-out
acknowledgement is a real, if narrow, ambiguity (plan 388 section 12.12). A
command that carries data repeats its own name instead, such as `$PDTINFO` and
`$CFGSYS`.

## Identification

`$PDTINFO` (UC 3.2 Table 3-1 page 26) is sent with every identification window,
alongside the u-blox MON-VER poll and the Quectel `$PQTMVERNO` query. The reply
`$PDTINFO,<pdtName>,<config>,<hwVer>,<fwVer>,<PN>,<SN>` decides the Unicore
backend at once, and the driver reports the model, the constellation flag and
the firmware version through `gps text`.

## Commands furble sends

| Operation | Sentence | UC table, page | Notes |
| :--- | :--- | :--- | :--- |
| fix rate | `CFGNAV,<ms>,<ms>,` | 3.19, 43 | measRate must equal navRate or the receiver answers FAIL; 50 to 1000 ms |
| sentence rate | `CFGMSG,0,<id>,<rate>` | 3.10 to 3.12, 34-36 | ids 0 GGA, 1 GLL, 2 GSA, 3 GSV, 4 RMC, 5 VTG; rate 0 or 1 |
| constellation | `CFGSYS,h<mask>` | 3.20 and 3.21, 44-46 | the receiver resets on it, so it is last in the queue |
| save | `CFGSAVE` | 3.24, 47 | flash-version products only; do not power off for one second |
| restart | `RESET,0,<mask>` | 3.5, 29 | h00 hot, h01 warm, h85 cold |
| port and baud | `CFGPRT,1,0,<baud>,1,3` | 3.8 and 3.9, 31-33 | built and host-tested, not yet sent by a setting |

`sysMask` bits the driver uses (UC 3.3 Table 3-21 pages 44 to 46):

| Bit | Signal | furble constant |
| ---: | :--- | :--- |
| 0 | GPS L1CA | `Unicore::SYS_GPS_L1CA` |
| 4 | BDS B1I | `Unicore::SYS_BDS_B1I` |
| 8 | GLONASS L1 | `Unicore::SYS_GLO_L1` |
| 12 | Galileo E1 | `Unicore::SYS_GAL_E1` |
| 16 | QZSS L1CA | `Unicore::SYS_QZSS_L1CA` |
| 20 | SBAS | `Unicore::SYS_SBAS` |

The mask is written in the document's own hexadecimal form, for example
`$CFGSYS,h100011` for GPS L1CA, BDS B1I and SBAS. L5 keys exist in the same table
and are not written: furble's signal table is L1 only.

## The constellation rules

UC 3.3 page 46 states three dependencies that `Unicore::gnssSupported()`
(`lib/furble/protocol/GpsUnicore.h:114`) enforces:

- QZSS functions only when GPS is enabled.
- SBAS functions only when GPS or BDS is enabled.
- When L5 of several constellations is configured, each constellation must be
  enabled or disabled at the same time. furble writes no L5 key, so this rule is
  not reachable from its settings.

A mask that breaks a rule is refused before it is sent, and the driver logs it.

## What there is no command for

UC R1.5 contains **no standby, sleep or power-save command**, and no dynamic
platform model. Two settings therefore cannot be honoured on this part:

- `GPS_PSM` (standby) is reported refused, the receiver keeps running for the
  whole duty cycle, and `configureUnicore()` logs that once.
- `GPS_PLATFORM` is reported refused.

Both are visible in `gps text`, which is the point: a setting that does nothing
says so.

## The save path

`$CFGSAVE` writes the current configuration to flash (UC 3.3 Table 3-24 page 47).
The document warns that the product must not lose power within one second of the
command, so the rail cut waits for the acknowledgement with a 1.5 s budget
(`src/FurbleGPSText.cpp:406`). A product without flash refuses the command with
`$FAIL,0`, and the driver records that and does not retry it in the same session.

## Simulation

`seed gps_model unicore` puts `Sim::VirtualUnicore` on the port. It answers
`$PDTINFO`, acknowledges valid commands with `$OK`, refuses a mask that breaks a
rule or a `CFGNAV` whose two rates differ with `$FAIL,0`, answers `$FAIL,1` to a
bad checksum, resets on `CFGSYS`, keeps its settings across a rail cut only after
`CFGSAVE`, and stays silent for a command it does not know. `seed
gps_unicore_no_flash true` models a product without flash and `seed
gps_unicore_silent true` a part that answers nothing at all, which keeps the NMEA
backend. Scenarios read its state through the `text.` query keys.

## Open rows

- Whether an empty `DRNavRate` field is accepted as "leave it alone" is not
  stated for a UC6580, only that the parameter is invalid for non-integrated
  products (UC 3.1 page 24, 3.3 page 43).
- Whether class 0 of `CFGMSG` also changes a second port the product may expose
  is not stated (UC 3.2 Table 3-12 page 36).
- No Unicore document in this tree describes a standby command, so "not
  supported" is a statement about UC R1.5, not about every Unicore firmware.
