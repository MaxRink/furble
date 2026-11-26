# CASIC and PCAS protocol

The binary CASIC protocol and the `$PCAS` sentence mode of the AT6668 receiver,
plus the pieces of the receiver behaviour that the firmware depends on.

| Item | Value |
| :--- | :--- |
| Codec | `lib/furble/protocol/GpsCasic.{h,cpp}` and `GpsNavx.h` |
| Driver | `src/FurbleGPS.cpp` |
| Component page | [AT6668](../components/at6668.md) |
| Source status | no vendor document in the tree. The checksum and frame layout come from the Quectel L76K specification plus recorded frames, cited in plan 32. |

## Frame layout

| Offset | Size | Field | Citation |
| :--- | ---: | :--- | :--- |
| 0 | 1 | sync 0xBA | `lib/furble/protocol/GpsCasic.h:37` |
| 1 | 1 | sync 0xCE | `lib/furble/protocol/GpsCasic.h:37` |
| 2 | 2 | payload length, little endian | `lib/furble/protocol/GpsDemux.h:41-43` |
| 4 | 1 | class id | `lib/furble/protocol/GpsCasic.h:37` |
| 5 | 1 | message id | `lib/furble/protocol/GpsCasic.h:37` |
| 6 | n | payload | |
| 6+n | 4 | checksum | `lib/furble/protocol/GpsDemux.h:42` |

The header is 6 bytes and the checksum 4, so overhead is 10 and the largest
accepted payload is 2048 bytes (`lib/furble/protocol/GpsDemux.h:40-43`).

## The checksum quirk

The checksum is a 32-bit sum of little-endian words over class, id, length and
payload, with **the message id first and the class second**.

The primary CASIC specification section 2.2 prints the operands class-first,
which is wrong. The Quectel L76K specification prints them id-first, and that is
the form every recorded frame balances under, verified against the L76K worked
examples and 33 live frames. The code comment warns the next reader of the CASIC
PDF not to reintroduce the class-first bug
(`lib/furble/protocol/GpsCasic.h:21-29`, `src/FurbleGPS.cpp:1183-1200`).

## Message classes and ids

| Message | Class | Id | Role | Citation |
| :--- | ---: | ---: | :--- | :--- |
| CFG-MSG | 0x06 | 0x01 | per-message output rate | `include/FurbleGPS.h:506-507` |
| CFG-RATE | 0x06 | 0x04 | measurement period | `src/FurbleGPS.cpp:1610-1613` |
| CFG-NAVX | 0x06 | 0x07 | constellation and dynamic model | `include/FurbleGPS.h:508` |
| AID-INI | 0x0B | 0x01 | time and position aiding | `include/FurbleGPS.h:512-513` |
| MON-HW | 0x0A | 0x09 | interference snapshot | `lib/furble/protocol/GpsCasic.h:236-237` |
| MSG-GPSEPH | 0x08 | 0x07 | GPS ephemeris | `lib/furble/protocol/GpsCasic.h:176` |
| MSG-GPSION | 0x08 | 0x06 | GPS ionosphere | `lib/furble/protocol/GpsCasic.h:177` |
| MSG-GPSUTC | 0x08 | 0x05 | GPS UTC parameters | `lib/furble/protocol/GpsCasic.h:178` |

## CFG-NAVX payload

44 bytes, query then write, with the mask at offset 0
(`lib/furble/protocol/GpsNavx.h:8-23`).

| Offset | Field | Mask bit | Citation |
| ---: | :--- | :--- | :--- |
| 0 | change mask, 32-bit little endian | | `lib/furble/protocol/GpsNavx.h:51-56` |
| 4 | `dyModel` | bit 0 | `lib/furble/protocol/GpsNavx.h:11,18` |
| 6 | `minSVs` | not modified, named so a test can prove a constellation write does not clobber it | `lib/furble/protocol/GpsNavx.h:8-14` |
| 13 | `navSystem` | bit 8 | `lib/furble/protocol/GpsNavx.h:14,19` |

The build fails if those offsets drift, because each is pinned by a
`static_assert` (`lib/furble/protocol/GpsNavx.h:25-29`). Applying a config to a
payload that is not 44 bytes returns 0 without touching the buffer
(`lib/furble/protocol/GpsNavx.h:44-49`).

## `$PCAS` sentences

`$PCAS` is NMEA-shaped: `$` then a body then `*` then two hex checksum
characters, computed over the body (`src/FurbleGPS.cpp:1169-1181`).

| Sentence | Argument | Meaning | Citation |
| :--- | :--- | :--- | :--- |
| `$PCAS01` | baud index | selects the UART rate. The firmware changes its own UART rate instead of sending it | `lib/furble/protocol/GpsCasic.h:44-46` |
| `$PCAS02` | ms | navigation rate | `src/FurbleGPS.cpp:1613` |
| `$PCAS03` | eight flags | sentence mask | `src/FurbleGPS.cpp:3097-3103` |
| `$PCAS04` | 1 to 7 | constellation | `src/FurbleGPS.cpp:1632` |
| `$PCAS10` | 0, 1, 2 | hot, warm or cold restart | `src/FurbleGPS.cpp:1279` |
| `$PCAS11` | platform minus one | dynamic model, provisional numbering | `src/FurbleGPS.cpp:1627-1639` |
| `$PCAS12` | seconds | timed standby | `src/FurbleGPS.cpp:765` |

The three `$PCAS03` masks are `1,0,0,0,1,0,0,0` for the minimal set,
`1,0,1,1,1,0,0,0` while the satellite page is open and `1,1,1,1,1,1,1,1` for the
full set (`src/FurbleGPS.cpp:3085-3105`).

## Autobaud ladder

| Item | Value | Citation |
| :--- | :--- | :--- |
| Rates | 115200, 9600, 38400, 57600, 19200, 4800 | `lib/furble/protocol/GpsCasic.h:49` |
| Step duration | 1200 ms, one 1 Hz burst arrives per second | `lib/furble/protocol/GpsCasic.h:51-52` |
| Lock criterion | two checksum-passing sentences in one step, which guards against a garbled stream passing by chance at 1 in 256 per sentence | `lib/furble/protocol/GpsCasic.h:54-56` |
| States | `IDLE`, `PROBING`, `LOCKED`, `NO_RECEIVER` | `lib/furble/protocol/GpsCasic.h:58-63` |
| Factory default | 9600, and the Unit GPS v1.1 ships at 115200 | `lib/furble/protocol/GpsCasic.h:41-45` |

## Ephemeris cache

The receiver publishes its own ephemeris as the three periodic messages above. A
CFG-MSG poll at rate 0xFFFF asks for one copy, and furble stores the frames
verbatim so replay is a straight byte write with no re-framing
(`lib/furble/protocol/GpsCasic.h:170-175`).

| Item | Value | Citation |
| :--- | :--- | :--- |
| Cache limit | 4096 bytes, about 32 satellites at 72 payload bytes plus 10 framing | `lib/furble/protocol/GpsCasic.h:196-197` |
| Replay split | walks `BA CE` frames, validating length and checksum, and stops at the first malformed frame so a corrupt cache cannot desynchronise the send | `lib/furble/protocol/GpsCasic.h:214-219` |
| Freshness | `REPLAY`, `TOO_OLD` or `IMPLAUSIBLE`, decided from the receiver's own UTC | `lib/furble/protocol/GpsCasic.h:180-193` |
| Validity window | GPS ephemeris is valid for roughly four hours | `lib/furble/protocol/GpsCasic.h:174-175` |
| Replay gap | 30 ms between frames | `src/FurbleGPS.cpp:600-603` |

The receiver is the only clock that can answer freshness. furble's kept clock
cannot, because after a power cut it restores the last persisted epoch plus the
monotonic time since boot and knows nothing about how long the board was off
(`lib/furble/protocol/GpsCasic.h:184-190`).

## MON-HW

56 bytes, documented in CASIC section 2.14.2 by the code comment. Offsets:
noise at 0, AGC at 12, antenna status at 20, jamming word array at 24
(`lib/furble/protocol/GpsCasic.h:225-240`).

| Field | Offset | Meaning |
| :--- | ---: | :--- |
| `noise` | 0 | `noisePerMs0` |
| `agc` | 12 | `agcData0` |
| `antenna_status` | 20 | 0 init, 1 unknown, 2 ok, 3 short, 4 open |
| `jam_indicator` | 24 | first interference word |

The offsets are spec-derived and the values still need a live-unit check, which
is why the console prints the raw bytes beside the decoded fields
(`lib/furble/protocol/GpsCasic.h:228-232`).

## Error handling

- A frame that fails its length or checksum check drops only its first sync byte,
  and the search resumes right after it, so a real frame hidden behind a false
  sync is still found (`lib/furble/protocol/GpsDemux.h:8-17`).
- A short MON-HW payload returns `valid = false` (`lib/furble/protocol/GpsCasic.h:242-243`).
- A configuration frame that is not acknowledged within the driver's timeout is
  marked `TIMEOUT` and the `$PCAS` fallback is sent
  (`src/FurbleGPS.cpp:607-612,1285-1298`).

## Open rows

| Missing | Why the repository cannot close it | Document and section needed |
| :--- | :--- | :--- |
| The class-first checksum in the primary specification | The code records that the printed formula is wrong and uses the L76K form | CASIC protocol specification section 2.2, plus an errata or the L76K specification section that states the working form |
| CFG-NAVX 44-byte field map | Only four offsets are asserted | CASIC protocol specification, CFG-NAVX payload table |
| `$PCAS` command set | Every sentence is used from plan 32 material, not from a document in the tree | CASIC or AT6668 protocol specification, `$PCAS` chapter |
| MON-HW field meanings and units | Recorded as spec-derived and needing a live check | CASIC protocol specification section 2.14.2 |
| 0xFFFF poll rate semantics in CFG-MSG | The code uses it to ask for one copy | CASIC protocol specification, CFG-MSG section |
