# NMEA 0183 sentences

NMEA is the text half of the receiver stream. furble reads it three ways: the
pinned TinyGPS++ fork for the fix, a direct GSV and GSA parser for satellites,
and its own checksum and date validation for time.

| Item | Value |
| :--- | :--- |
| Sentences from TinyGPS++ | GGA and RMC only. The pinned fork parses nothing else (`lib/furble/protocol/GpsCasic.h:88-96`). |
| Sentences parsed directly | GSV and GSA (`lib/furble/protocol/GpsCasic.cpp:431-453`) |
| Sentences written by furble | `$PCAS` configuration, see [CASIC and PCAS](gnss-casic-pcas.md#pcas-sentences) |
| Checksum | XOR of the characters between `$` and `*`, compared to two hex characters (`lib/furble/protocol/GpsCasic.cpp:96-116`) |

## Sentence scope

| Sentence | Fields furble uses | Citation |
| :--- | :--- | :--- |
| GGA | position, altitude, satellite count, fix quality, through TinyGPS++ | `src/FurbleGPS.cpp:5` |
| RMC | position, speed, course, date and time, through TinyGPS++ | `src/FurbleGPS.cpp:1130-1134` |
| GSV | per satellite PRN, elevation, azimuth and C/N0, reassembled across the numbered sentences of a set | `lib/furble/protocol/GpsCasic.h:85-96`, `GpsCasic.cpp:235-330` |
| GSA | the used-satellite list, PDOP, HDOP, VDOP and fix type | `lib/furble/protocol/GpsCasic.h:100-105`, `GpsCasic.cpp:348-390` |

## Talker prefixes

A talker maps to a constellation, and the mapping has to tolerate firmware
variants (`lib/furble/protocol/GpsCasic.cpp:202-222`).

| Talker | Constellation id | Note |
| :--- | :--- | :--- |
| `GP` | 1 GPS | |
| `GL` | 2 GLONASS | |
| `GA` | 3 Galileo | |
| `BD`, `GB` | 4 BeiDou | older firmware emits `BD`, newer `GB` |
| `GQ`, `PQ`, `QZ` | 5 QZSS | the prefix varies across firmware revisions |
| anything else | 0 unknown | treated as its own set |

These ids are the L76K numbering appendix values
(`lib/furble/protocol/GpsCasic.h:108-115`).

## Parsing rules the code relies on

- A GSV sentence carries at most four satellites, and the parser splits on the
  quad boundaries rather than trusting a declared count
  (`lib/furble/protocol/GpsCasic.cpp:235-330`).
- A GSA sentence replaces the current solution for its system. A combined
  `GNGSA` with no system id replaces every system
  (`lib/furble/protocol/GpsCasic.cpp:360-382`). NMEA 4.1 appends a system id,
  which takes precedence over the talker prefix.
- C/N0 is defined as 00 to 99 dB-Hz and is clamped (`lib/furble/protocol/GpsCasic.cpp:309`).
- The satellite table keeps at most 32 satellites in view and 64 used entries
  (`lib/furble/protocol/GpsCasic.h:122-123`).
- A DOP snapshot carries `pdop`, `hdop`, `vdop`, a fix type of 1, 2 or 3, and a
  validity flag (`lib/furble/protocol/GpsCasic.h:100-105`).

## Date validation before a clock is set

Only `$GPRMC` and `$GNRMC` with a valid checksum and a six-digit date field in
the expected position are accepted as a date source
(`src/FurbleGPS.cpp:1130-1167`). The field position is counted, not searched, so
a sentence with a short field cannot shift the date into range.

## Stream separation

NMEA is 7-bit text, so neither binary sync pair (`BA CE` for CASIC, `B5 62` for
UBX) can appear inside a sentence. Bytes up to the next sync pair are NMEA, which
is how the demultiplexer separates the three protocols on one UART
(`lib/furble/protocol/GpsDemux.h:8-13`).

## Open rows

| Missing | Why the repository cannot close it | Document and section needed |
| :--- | :--- | :--- |
| Which NMEA version the AT6668 emits | The parser tolerates 4.1 system ids in GSA but does not assert a version | NMEA 0183 version 4.10 or later, GSA and GSV sections, plus the AT6668 firmware release note |
| Sentence field order beyond the used fields | TinyGPS++ owns GGA and RMC field parsing | NMEA 0183 GGA and RMC field tables |
| Talker variants for future receivers | Only the five constellations above are mapped | NMEA 0183 talker identifier list |
