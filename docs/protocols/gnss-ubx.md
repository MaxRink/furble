# UBX protocol

The u-blox binary protocol, used when a receiver answers MON-VER. The codec is
`lib/furble/protocol/GpsUbx.{h,cpp}`, the driver is `src/FurbleGPSUbx.cpp`, and
the simulator receiver is `sim/ubx_m10.cpp`.

| Item | Value |
| :--- | :--- |
| Primary source cited in code | u-blox M10 SPG 5.10 Interface Description, UBX-21035062 R03, protocol 34.10 (`lib/furble/protocol/GpsUbx.h:10-20`) |
| Second source | MAX-M10S integration manual UBX-20053088 section 3.6.3.2 (`lib/furble/protocol/GpsUbx.h:307-311`) |
| Component pages | [MAX-M10S](../components/ublox-max-m10s.md) and [MIA-M10Q](../components/ublox-mia-m10q.md) |
| Plan | [321](../../plans/321-ublox-m10-driver.md) |

## Frame layout

| Offset | Size | Field | Citation |
| :--- | ---: | :--- | :--- |
| 0 | 1 | sync 1, 0xB5 | `lib/furble/protocol/GpsUbx.h:28` |
| 1 | 1 | sync 2, 0x62 | `lib/furble/protocol/GpsUbx.h:29` |
| 2 | 1 | class | IFD 3.2 |
| 3 | 1 | id | IFD 3.2 |
| 4 | 2 | payload length, little endian | IFD 3.2 |
| 6 | n | payload | IFD 3.2 |
| 6+n | 1 | checksum A | IFD 3.4 |
| 7+n | 1 | checksum B | IFD 3.4 |

The checksum is the 8-bit Fletcher over class, id, length and payload, computed
by `checksum()` (`lib/furble/protocol/GpsUbx.h:121-122`). Overhead is 8 bytes and
the largest accepted payload is 1024 bytes (`lib/furble/protocol/GpsUbx.h:30-42`).

## Stream rules

`Stream` consumes one byte at a time with a bounded buffer. Bytes before a sync
pair, a length past the limit and a failed checksum all resynchronise on the next
sync pair inside the rejected bytes, so a frame that starts inside garbage is
still found (`lib/furble/protocol/GpsUbx.h:144-161`). `scan()` classifies the head
of a buffer as `NEED_MORE`, `BAD` or `FRAME`
(`lib/furble/protocol/GpsUbx.h:131-140`).

The receiver stream is shared with NMEA text and CASIC frames, and the
demultiplexer decides which is which before either parser sees the bytes
(`lib/furble/protocol/GpsDemux.h:8-19`).

## Message classes and ids

| Message | Class | Id | IFD section | Citation |
| :--- | ---: | ---: | :--- | :--- |
| NAV-PVT | 0x01 | 0x07 | 3.15.11 | `lib/furble/protocol/GpsUbx.h:57` |
| NAV-SAT | 0x01 | 0x35 | 3.15.13 | `lib/furble/protocol/GpsUbx.h:58` |
| RXM-PMREQ | 0x02 | 0x41 | 3.16.6 | `lib/furble/protocol/GpsUbx.h:59` |
| ACK-ACK | 0x05 | 0x01 | 3.9.1 | `lib/furble/protocol/GpsUbx.h:50` |
| ACK-NAK | 0x05 | 0x00 | 3.9.2 | `lib/furble/protocol/GpsUbx.h:51` |
| CFG-RST | 0x06 | 0x04 | 3.10.2 | `lib/furble/protocol/GpsUbx.h:52` |
| CFG-VALSET | 0x06 | 0x8A | 3.10.5 | `lib/furble/protocol/GpsUbx.h:55` |
| CFG-VALGET | 0x06 | 0x8B | 3.10.4 | `lib/furble/protocol/GpsUbx.h:54` |
| CFG-VALDEL | 0x06 | 0x8C | 3.10.3 | `lib/furble/protocol/GpsUbx.h:53` |
| UPD-SOS | 0x09 | 0x14 | 3.19.1 | `lib/furble/protocol/GpsUbx.h:60` |
| MON-VER | 0x0A | 0x04 | 3.14.9 | `lib/furble/protocol/GpsUbx.h:56` |

Class constants are `CLASS_NAV` 0x01, `CLASS_RXM` 0x02, `CLASS_ACK` 0x05,
`CLASS_CFG` 0x06, `CLASS_UPD` 0x09 and `CLASS_MON` 0x0A
(`lib/furble/protocol/GpsUbx.h:44-49`).

## Configuration keys

The value size is encoded in bits 28 to 30 of the key, so the codec needs no type
table (`lib/furble/protocol/GpsUbx.h:72-78`). Type letters are the interface
description's: L is one bit in a byte, U1, U2 and U4 are unsigned widths, E1 is
an enumerated byte.

| Key | Address | Type | IFD section | Default stated in code | Citation |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `RATE_MEAS` | 0x30210001 | U2 ms | 4.9.17 | 1000 | `lib/furble/protocol/GpsUbx.h:81` |
| `RATE_NAV` | 0x30210002 | U2 cycles | | 1 | `lib/furble/protocol/GpsUbx.h:82` |
| `NAVSPG_DYNMODEL` | 0x20110021 | E1 | 4.9.12 | 0 | `lib/furble/protocol/GpsUbx.h:83` |
| `SIGNAL_GPS_ENA` | 0x1031001f | L | 4.9.21 | | `lib/furble/protocol/GpsUbx.h:84` |
| `SIGNAL_GPS_L1CA` | 0x10310001 | L | 4.9.21 | | `lib/furble/protocol/GpsUbx.h:85` |
| `SIGNAL_SBAS_ENA` | 0x10310020 | L | 4.9.21 | | `lib/furble/protocol/GpsUbx.h:86` |
| `SIGNAL_SBAS_L1CA` | 0x10310005 | L | 4.9.21 | | `lib/furble/protocol/GpsUbx.h:87` |
| `SIGNAL_GAL_ENA` | 0x10310021 | L | 4.9.21 | | `lib/furble/protocol/GpsUbx.h:88` |
| `SIGNAL_GAL_E1` | 0x10310007 | L | 4.9.21 | | `lib/furble/protocol/GpsUbx.h:89` |
| `SIGNAL_BDS_ENA` | 0x10310022 | L | 4.9.21 | | `lib/furble/protocol/GpsUbx.h:90` |
| `SIGNAL_BDS_B1` | 0x1031000d | L | 4.9.21 | | `lib/furble/protocol/GpsUbx.h:91` |
| `SIGNAL_BDS_B1C` | 0x1031000f | L | 4.9.21 | | `lib/furble/protocol/GpsUbx.h:92` |
| `SIGNAL_QZSS_ENA` | 0x10310024 | L | 4.9.21 | | `lib/furble/protocol/GpsUbx.h:93` |
| `SIGNAL_QZSS_L1CA` | 0x10310012 | L | 4.9.21 | | `lib/furble/protocol/GpsUbx.h:94` |
| `SIGNAL_QZSS_L1S` | 0x10310014 | L | 4.9.21 | | `lib/furble/protocol/GpsUbx.h:95` |
| `SIGNAL_GLO_ENA` | 0x10310025 | L | 4.9.21 | | `lib/furble/protocol/GpsUbx.h:96` |
| `SIGNAL_GLO_L1` | 0x10310018 | L | 4.9.21 | | `lib/furble/protocol/GpsUbx.h:97` |
| `PM_OPERATEMODE` | 0x20d00001 | E1 | 4.9.15 | FULL | `lib/furble/protocol/GpsUbx.h:98` |
| `PM_POSUPDATEPERIOD` | 0x40d00002 | U4 s | | | `lib/furble/protocol/GpsUbx.h:99` |
| `PM_ACQPERIOD` | 0x40d00003 | U4 s | | | `lib/furble/protocol/GpsUbx.h:100` |
| `PM_ONTIME` | 0x30d00005 | U2 s | | | `lib/furble/protocol/GpsUbx.h:101` |
| `PM_DONOTENTEROFF` | 0x10d00008 | L | | | `lib/furble/protocol/GpsUbx.h:102` |
| `PM_WAITTIMEFIX` | 0x10d00009 | L | | | `lib/furble/protocol/GpsUbx.h:103` |
| `PM_UPDATEEPH` | 0x10d0000a | L | | | `lib/furble/protocol/GpsUbx.h:104` |
| `ANA_USE_ANA` | 0x10230001 | L | 4.9.1 | 0 | `lib/furble/protocol/GpsUbx.h:105` |
| `MSGOUT_NAV_PVT_UART1` | 0x20910007 | U1 | 4.9.11 | 0 | `lib/furble/protocol/GpsUbx.h:106` |
| `MSGOUT_NAV_SAT_UART1` | 0x20910016 | U1 | 4.9.11 | | `lib/furble/protocol/GpsUbx.h:107` |
| `UART1OUTPROT_UBX` | 0x10740001 | L | 4.9.29 | 1 | `lib/furble/protocol/GpsUbx.h:108` |
| `UART1OUTPROT_NMEA` | 0x10740002 | L | 4.9.29 | | `lib/furble/protocol/GpsUbx.h:109` |

`valueSize()` returns the byte count implied by the key, and 0 for a reserved
size (`lib/furble/protocol/GpsUbx.h:111`. A `KeyValue` wider than the key's size
is truncated, not rejected (`lib/furble/protocol/GpsUbx.h:114-118`).

## Layers

| Name | Value | Meaning | Citation |
| :--- | ---: | :--- | :--- |
| `LAYER_RAM` | 0x01 | VALSET and VALDEL bit for the RAM layer | `lib/furble/protocol/GpsUbx.h:62` |
| `LAYER_BBR` | 0x02 | battery backed RAM | `lib/furble/protocol/GpsUbx.h:63` |
| `LAYER_FLASH` | 0x04 | external flash, which these modules do not have | `lib/furble/protocol/GpsUbx.h:64` |
| `GET_RAM`, `GET_BBR`, `GET_FLASH`, `GET_DEFAULT` | 0, 1, 2, 7 | VALGET layer selectors, codes rather than a bit mask | `lib/furble/protocol/GpsUbx.h:66-70` |

A VALSET, VALGET or VALDEL carries at most 64 keys
(`lib/furble/protocol/GpsUbx.h:70`).

## Constellation mask

| Name | Value | Citation |
| :--- | ---: | :--- |
| `GNSS_MASK_GPS` | 0x01 | `lib/furble/protocol/GpsUbx.h:397` |
| `GNSS_MASK_BDS` | 0x02 | `lib/furble/protocol/GpsUbx.h:398` |
| `GNSS_MASK_GLO` | 0x04 | `lib/furble/protocol/GpsUbx.h:399` |
| `GNSS_MASK_GAL` | 0x08 | `lib/furble/protocol/GpsUbx.h:400` |
| `GNSS_MASK_QZSS` | 0x10 | `lib/furble/protocol/GpsUbx.h:401` |
| `GNSS_MASK_SBAS` | 0x20 | `lib/furble/protocol/GpsUbx.h:402` |
| `GNSS_MASK_ALL` | 0x3f | `lib/furble/protocol/GpsUbx.h:404` |
| `GNSS_MASK_MODULE_DEFAULT` | GPS, Galileo, BeiDou B1I, QZSS, SBAS | the modules' production preset, data sheet 1.3, `lib/furble/protocol/GpsUbx.h:405-407` |

The low three bits are the CASIC `navSystem` bits that the GPS constellation
setting already uses, so a CASIC selection maps across unchanged
(`lib/furble/protocol/GpsUbx.h:392-396`).

`gnssFromConstellation()`, `gnssValid()`, `maxRateHz()` and
`dynModelForPlatform()` apply the rules, and `plan()` turns a request into the
three key lists plus the unset list
(`lib/furble/protocol/GpsUbx.h:443-463`).

| Rule | Behaviour | Citation |
| :--- | :--- | :--- |
| One major constellation, and QZSS or SBAS only with GPS | a rejected mask sets `gnss_refused` and the signals are left alone | `gnssValid()` at `lib/furble/protocol/GpsUbx.h:455`, `PlanSummary::gnss_refused` at `:429` |
| Rate ceiling per mask | exceeding it sets `rate_clamped` | `maxRateHz()` at `lib/furble/protocol/GpsUbx.h:458`, `:431` |
| BeiDou B1C excludes cyclic tracking power save | `psm_refused`, the receiver stays at full power | `PlanSummary::psm_refused` at `lib/furble/protocol/GpsUbx.h:430` |
| Send order | base, signals (a GNSS restart, so 0.5 s settle follows), power save with OPERATEMODE last | `struct Plan` at `lib/furble/protocol/GpsUbx.h:443-449` |
| Managed keys left alone | `unset` is deleted from BBR so an earlier session's value does not outlive the setting | `Plan::unset` at `lib/furble/protocol/GpsUbx.h:448` |
| Whether a fix is usable, dated and time-worthy | three predicates on NAV-PVT, `positionValid`, `dateTimeValid` and `timeValid` | `lib/furble/protocol/GpsUbx.h:216-222` |

## NAV-PVT

92 bytes, one field group per the interface description section 3.15.11
(`lib/furble/protocol/GpsUbx.h:179-230`).

| Field | Type | Unit or meaning |
| :--- | :--- | :--- |
| `iTOW` | U4 | ms |
| `year`, `month`, `day`, `hour`, `min`, `sec` | | UTC |
| `valid` | U1 | bit 0 validDate, bit 1 validTime, bit 2 fullyResolved |
| `tAcc` | U4 | ns |
| `nano` | I4 | ns, -1e9 to 1e9 |
| `fixType` | U1 | 0 none, 1 dead reckoning, 2 2D, 3 3D, 4 GNSS plus DR, 5 time only |
| `flags` | U1 | bit 0 gnssFixOK, bits 2 to 4 psmState |
| `flags2` | U1 | |
| `numSV` | U1 | satellites used |
| `lon`, `lat` | I4 | 1e-7 degrees |
| `height`, `hMSL` | I4 | mm, ellipsoid and mean sea level |
| `hAcc`, `vAcc` | U4 | mm |
| `gSpeed` | I4 | mm/s |
| `headMot` | I4 | 1e-5 degrees |
| `sAcc` | U4 | mm/s |
| `pDOP` | U2 | 0.01 |
| `flags3` | U2 | bit 0 invalidLlh |

Three derived predicates decide what furble may use: `positionValid()` accepts a
2D, 3D or GNSS plus dead reckoning fix the receiver vouches for,
`dateTimeValid()` requires the valid bits and in-range fields, and `timeValid()`
adds `fullyResolved` before a clock may be set
(`lib/furble/protocol/GpsUbx.h:216-222`).

## NAV-SAT

An 8-byte header then 12 bytes per signal
(`lib/furble/protocol/GpsUbx.h:231-251`).

| Field | Type | Unit |
| :--- | :--- | :--- |
| `gnssId` | U1 | IFD 1.5 numbering: 0 GPS, 1 SBAS, 2 Galileo, 3 BeiDou, 5 QZSS, 6 GLONASS |
| `svId` | U1 | satellite id |
| `cno` | U1 | dBHz |
| `elev` | I1 | degrees |
| `azim` | I2 | degrees |
| `flags` | U4 | bits 0 to 2 qualityInd, bit 3 svUsed |

`encodeNavSat()` refuses more than 84 satellites, and the codec's MAX_PAYLOAD
bound is why (`lib/furble/protocol/GpsUbx.h:232-236`).

## MON-VER

`swVersion[30]`, `hwVersion[10]`, then 30-byte extensions
(`lib/furble/protocol/GpsUbx.h:252-263`). `field()` extracts a `KEY=value` from an
extension, for example `FWVER`.

## RXM-PMREQ

The M10 documents only the 16-byte version 0 form
(`lib/furble/protocol/GpsUbx.h:300-307`).

| Field | Type | Meaning |
| :--- | :--- | :--- |
| `duration_ms` | U4 | 0 sleeps until a wake source fires |
| `flags` | U4 | bit 1 backup, bit 2 force, bit 3 UART RX wake, bit 5 EXTINT0, bit 6 EXTINT1, bit 7 SPI CS |
| `wakeup_sources` | U4 | mask |

The integration manual requires the force flag for software standby and warns
that standby clears the RAM configuration
(`lib/furble/protocol/GpsUbx.h:307-311`). The driver sends
`PMREQ_FLAG_BACKUP | PMREQ_FLAG_FORCE | PMREQ_WAKE_UARTRX` for the duty interval
with V_IO kept up, so BBR and the RTC survive and the wake is a hot start
(`src/FurbleGPS.cpp:761-764`).

## CFG-RST

Never acknowledged (`lib/furble/protocol/GpsUbx.h:339-350`).

| Name | Value | Meaning |
| :--- | ---: | :--- |
| `RST_HOT` | 0x0000 | hot start |
| `RST_WARM` | 0x0001 | warm start |
| `RST_COLD` | 0xFFFF | cold start |
| `RST_MODE_GNSS_ONLY` | 0x02 | controlled software reset, GNSS only |
| `RST_MODE_GNSS_STOP` | 0x08 | controlled GNSS stop |
| `RST_MODE_GNSS_START` | 0x09 | controlled GNSS start |

## UPD-SOS

| Name | Value | Meaning | Citation |
| :--- | ---: | :--- | :--- |
| `SOS_CREATE` | 0 | create a backup | `lib/furble/protocol/GpsUbx.h:276` |
| `SOS_CLEAR` | 1 | clear a backup | `lib/furble/protocol/GpsUbx.h:277` |
| `SOS_ACK` | 2 | backup creation acknowledge | `lib/furble/protocol/GpsUbx.h:278` |
| `SOS_RESTORED` | 3 | system restored from backup | `lib/furble/protocol/GpsUbx.h:279` |
| restore results | 0 unknown, 1 failed, 2 ok, 3 none | the `response` field of SOS_RESTORED | `lib/furble/protocol/GpsUbx.h:281-285` |

The backup lives in the receiver's flash file system, and neither MAX-M10S nor
MIA-M10Q has one, so the create command is expected to be refused
(`lib/furble/protocol/GpsUbx.h:269-273`).

## Detection

`Detect` polls MON-VER twice with 1000 ms per attempt. A u-blox receiver answers
whatever its configuration, and a receiver that ignores the frame keeps the NMEA
path (`lib/furble/protocol/GpsUbx.h:456-487`). One attempt is a whole second
because the answer is about 190 bytes queued behind up to a second of NMEA at
9600 baud (`lib/furble/protocol/GpsUbx.h:456-464`).

## Error handling

- ACK-NAK fails the pending configuration and the driver records which key was
  refused (`lib/furble/protocol/GpsUbx.h:171-177`, `src/FurbleGPSUbx.cpp`).
- A configuration frame whose acknowledgement never arrives is bounded by the
  driver's ack timeout, and the driver then reports `TIMEOUT`
  (`src/FurbleGPS.cpp:607-612,1285-1298`).
- `Settle` is a wrap-safe one-shot deadline, and an unarmed gate is open. A plain
  `deadline = 0` compared with a signed difference reads as the future for ticks
  in [2^31, 2^32), which stalled every configuration command between 24.8 and
  49.7 days of uptime (`lib/furble/protocol/GpsUbx.h:352-390`).
- A controlled reset never gets an answer, so no code waits for one
  (`lib/furble/protocol/GpsUbx.h:339-341`).

## Open rows

| Missing | Why the repository cannot close it | Document and section needed |
| :--- | :--- | :--- |
| NAV-PVT field offsets in bytes | The struct order is the layout, and the code never states an offset table | UBX-21035062 R03 section 3.15.11, the offset column |
| `flags2` bit meanings | The field is parsed and unused | Same section |
| PM state values for `PM_OPERATEMODE` | Only FULL is named | UBX-21035062 R03 section 4.9.15, the enumeration |
| `flags` bits 5 to 7 of RXM-PMREQ | Not decoded | UBX-21035062 R03 section 3.16.6 |
| MON-VER extension keys | Only `FWVER` is named | UBX-21035062 R03 section 3.14.9 and the module data sheets |
| Flash layer availability | No module in scope has an external flash | MAX-M10S and MIA-M10Q data sheets, flash section |
