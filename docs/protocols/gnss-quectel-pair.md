# Quectel `$PAIR` and `$PQTM` protocol

The text command protocol of the Quectel LC76G and LC29H families. The codec is
`lib/furble/protocol/GpsQuectel.{h,cpp}` on top of the shared seam in
`lib/furble/protocol/GpsText.{h,cpp}`, the driver is `src/FurbleGPSText.cpp`, and
the simulator receiver is `sim/vendor_gnss.cpp`.

| Item | Value |
| :--- | :--- |
| Primary source | Quectel "LC26G (AB)&LC76G&LC86G Series GNSS Protocol Specification", V1.1, 2022-12-20. Cited as Q76 |
| Second source | Quectel "LC29H Series&LC79H (AL) GNSS Protocol Specification", V1.2, 2023-06-20. Cited as Q29 |
| Codec | `lib/furble/protocol/GpsQuectel.h:20-31` |
| Plan | [388](../../plans/388-gnss-vendor-text.md) |

Where the two documents disagree about which variant supports a command, the
driver obeys the narrower one and reports the refusal.

The driver's `$PAIR and $PQTM` backend is compiled out of one profile:
`m5stack-core-debug` sets `FURBLE_GPS_TEXT=0` because its fixed OTA slot
cannot hold the full debug image, so that build reads NMEA from the part
and sends no configuration command. Every other build keeps it.

## Frame layout

| Offset | Size | Field | Citation |
| :--- | ---: | :--- | :--- |
| 0 | 1 | `$`, 0x24 | Q76 2.1 Table 3, page 11 |
| 1 | n | body: `PAIR<id>[,<Data>]` or `PQTM<Name>[,<Data>]` | Q76 2.3 page 32, 2.4 page 95 |
| 1+n | 1 | `*`, 0x2A | Q76 2.1 Table 3, page 11 |
| 2+n | 2 | checksum, two ASCII hex digits | Q76 2.1 Table 3, page 12 |
| 4+n | 2 | CR LF | Q76 2.1 Table 3, page 12 |

The checksum is the 8-bit XOR of every character between but not including the
`$` and the `*` (Q76 2.1 Table 3, page 12). A vendor line is capped at
`GpsText::MAX_LINE` = 128 bytes (`lib/furble/protocol/GpsText.h:46`), the Unicore
bound, which is generous for a Quectel sentence. The splitter enforces the cap on
a finished line too: a terminated vendor line longer than that, terminator
included, is dropped whole rather than parsed, so `GpsText::parse()` never sees
more than the cap.

## Acknowledgements

`$PAIR001,<CommandID>,<Result>` acknowledges a PAIR command (Q76 2.3.1 page 32,
Q29 2.4.1 pages 51 and 52). The result codes furble decodes are in
`lib/furble/protocol/GpsText.cpp`, `match()`:

| Result | Meaning | furble |
| ---: | :--- | :--- |
| 0 | command sent | acknowledged |
| 1 | being processed | wait, no retry spent |
| 2 | sending failed | refusal |
| 3 | command id not supported | refusal, "this part does not have it" |
| 4 | parameter error, including a checksum error | refusal |
| 5 | MNL busy | refusal |

A PQTM set or get repeats the command name instead: `$PQTMSAVEPAR,OK` and
`$PQTMSAVEPAR,ERROR,<code>`, and `$PQTMVERNO,<VerStr>,<BuildDate>,<BuildTime>`
(Q76 2.4.3 page 98, 2.4.5 page 100). A reply whose checksum fails is reported as
a malformed reply, never as a verdict.

## Identification

`$PQTMVERNO` (Q76 2.4.5 pages 99 and 100, Q29 2.3.1 page 33) is sent with every
identification window, alongside the u-blox MON-VER poll and the Unicore
`$PDTINFO` query. A reply decides the Quectel backend at once. The version string
and the build stamp are reported by `gps text`.

## Commands furble sends

| Operation | Sentence | Q76 | Q29 | Driver decision |
| :--- | :--- | :--- | :--- | :--- |
| fix rate | `PAIR050,<ms>` | 2.3.9, 38 | 2.4.9, 57 | 100 to 1000 ms; outside is refused before it is sent |
| sentence rate | `PAIR062,<type>,<n>` | 2.3.12, 40 | 2.4.13, 61 | types 0 to 5 only, rate 0 or 1 only |
| constellation | `PAIR066,<GPS>,<GLO>,<GAL>,<BDS>,<QZSS>,0` | 2.3.15, 43-44 | 2.4.15, 63-65 | last in the queue, because an LC76G reboots its GNSS engine on it |
| SBAS | `PAIR410,<0/1>` | 2.3.34, 60 | 2.4.35, 84 | refused in fitness mode |
| navigation mode | `PAIR080,<mode>` | 2.3.23, 50 | 2.4.23, 72 | 0 normal for Portable, 1 fitness for Pedestrian |
| sleep lock | `PAIR382,1` | 2.3.30, 56 | 2.4.31, 80 | only on the save path |
| GNSS off | `PAIR003` | 2.3.3, 34 | 2.4.3, 53 | only on the save path |
| save settings | `PAIR513` | 2.3.45, 68 | 2.4.46, 94 | save path |
| save PQTM settings | `PQTMSAVEPAR` | 2.4.3, 98 | 2.3.2, 34 | save path |
| save navigation data | `PAIR511` | 2.3.44, 67 | 2.4.45, 93 | save path, only without a backup supply |
| backup mode | `PAIR650,<s>` | 2.3.46, 69 | 2.4.47, 95 | 10 s or more; 0 means no timer |
| restart | `PAIR004`, `PAIR005`, `PAIR006` | 2.3.4 to 2.3.6, 34-36 | 2.4.4 to 2.4.6, 54-55 | hot, warm, cold |
| baud rate | `PAIR864,0,0,<baud>` | 2.3.56, 78 | 2.4.53, 100 | built and host-tested, not yet sent by a setting |

Sentence type ids of PAIR062 (Q76 2.3.12 page 40): 0 GGA, 1 GLL, 2 GSA, 3 GSV,
4 RMC, 5 VTG, 6 ZDA, 7 GRS, 8 GST, 9 GNS. furble writes 0 to 5, the six both
documents define and the six an LC29H (BA, CA, DA, EA) accepts.

## The constellation rule

Q76 2.3.15 note 2 page 44 lists the supported search modes. GPS is in every one.
Beyond GPS, exactly zero, exactly one or all three of GLONASS, Galileo and BDS
appear. **Two of the three is not listed**, so `Quectel::gnssSupported()`
(`lib/furble/protocol/GpsQuectel.h:113`) refuses it and the driver logs the
refusal instead of sending it. SBAS is not part of PAIR066: it is PAIR410, and it
is refused in fitness and swimming mode (Q76 2.3.34 note page 60).

## The standby gap

`PAIR650` accepts 0 or 10 s to 62208000 s (Q76 2.3.46 page 69). furble's duty
ladder offers 5, 10 and 15 s, so 5 s cannot be expressed. `Quectel::standby()`
(`lib/furble/protocol/GpsQuectel.h:155`) refuses it and the driver reports the
refusal rather than sending a value the receiver will reject with result 4.

## The save path

Above 1 Hz the GNSS engine must be powered off before `PAIR513` or `PAIR511`,
and `PAIR382,1` has to go first so the port still accepts the save (Q76 2.3.45
note 2 page 69, 2.3.30 note 2 page 56). The driver sends `PAIR382,1`, `PAIR003`,
`PAIR513`, `PQTMSAVEPAR` and, when the board has no backup supply, `PAIR511`
(`src/FurbleGPSText.cpp:406`). It does so whatever the fix rate: the extra frames
are short and the rail is about to be cut.

If the board then refuses the cut, the receiver is powered with its GNSS engine
off. Q76 2.3.44 note 2 page 69 documents the re-power after a save, so the driver
sends `$PAIR002` when the cut is refused (`Quectel::powerBackOn()`), or `$PAIR004`
hot start on an LC86G, which has no `$PAIR002` (Q76 2.3.2 note page 34). Without
that frame the receiver would stay fixless until the next power cycle.

## Simulation

`seed gps_model quectel` puts `Sim::VirtualQuectel` on the port. It answers every
command above, returns result 3 for an id it does not have, result 4 for a value
out of range and for a bad checksum, keeps its settings across a rail cut only
after a save, and enters backup mode with a wake timer. `seed
gps_quectel_no_fix_rate true` models an LC76G (PA)/(PB) that refuses PAIR050 and
`seed gps_quectel_no_search_mode true` an LC29H (BA, CA, DA, EA) that refuses
PAIR066. Scenarios read its state through the `text.` query keys.

## Open rows

- Whether a part answers an unknown `$` name at all is not stated by either
  document, so detection depends on `$PQTMVERNO` being answered.
- Whether `PAIR002` is needed to restart the GNSS engine when a rail cut is
  refused after `PAIR511` is not stated. The driver does not send it.
- The first epoch after a `PAIR650` wake has no documented settling time.
