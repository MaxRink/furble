# UBX protocol

The u-blox binary protocol, used when a receiver answers MON-VER. The codec is
`lib/furble/protocol/GpsUbx.{h,cpp}`, the driver is `src/FurbleGPSUbx.cpp`, and
the simulator receiver is `sim/ubx_m10.cpp`.

| Item | Value |
| :--- | :--- |
| Primary source cited in code | u-blox M10 SPG 5.10 Interface Description, UBX-21035062 R03, protocol 34.10 (`lib/furble/protocol/GpsUbx.h:10-27`) |
| Second source | MAX-M10S integration manual UBX-20053088 section 3.6.3.2 (`lib/furble/protocol/GpsUbx.h:318-322`) |
| Component pages | [MAX-M10S](../components/ublox-max-m10s.md) and [MIA-M10Q](../components/ublox-mia-m10q.md) |
| Plan | [321](../../plans/implemented/321-ublox-m10-driver.md) |

## Frame layout

| Offset | Size | Field | Citation |
| :--- | ---: | :--- | :--- |
| 0 | 1 | sync 1, 0xB5 | `lib/furble/protocol/GpsUbx.h:35` |
| 1 | 1 | sync 2, 0x62 | `lib/furble/protocol/GpsUbx.h:36` |
| 2 | 1 | class | IFD 3.2 |
| 3 | 1 | id | IFD 3.2 |
| 4 | 2 | payload length, little endian | IFD 3.2 |
| 6 | n | payload | IFD 3.2 |
| 6+n | 1 | checksum A | IFD 3.4 |
| 7+n | 1 | checksum B | IFD 3.4 |

The checksum is the 8-bit Fletcher over class, id, length and payload, computed
by `checksum()` (`lib/furble/protocol/GpsUbx.h:132-133`). Overhead is 8 bytes and
the largest accepted payload is 1024 bytes (`lib/furble/protocol/GpsUbx.h:37-49`).

## Stream rules

`Stream` consumes one byte at a time with a bounded buffer. Bytes before a sync
pair, a length past the limit and a failed checksum all resynchronise on the next
sync pair inside the rejected bytes, so a frame that starts inside garbage is
still found (`lib/furble/protocol/GpsUbx.h:155-172`). `scan()` classifies the head
of a buffer as `NEED_MORE`, `BAD` or `FRAME`
(`lib/furble/protocol/GpsUbx.h:142-151`).

The receiver stream is shared with NMEA text and CASIC frames, and the
demultiplexer decides which is which before either parser sees the bytes
(`lib/furble/protocol/GpsDemux.h:8-19`).

## Message classes and ids

| Message | Class | Id | IFD section | Citation |
| :--- | ---: | ---: | :--- | :--- |
| NAV-PVT | 0x01 | 0x07 | 3.15.11 | `lib/furble/protocol/GpsUbx.h:64` |
| NAV-SAT | 0x01 | 0x35 | 3.15.13 | `lib/furble/protocol/GpsUbx.h:65` |
| RXM-PMREQ | 0x02 | 0x41 | 3.16.6 | `lib/furble/protocol/GpsUbx.h:66` |
| ACK-ACK | 0x05 | 0x01 | 3.9.1 | `lib/furble/protocol/GpsUbx.h:57` |
| ACK-NAK | 0x05 | 0x00 | 3.9.2 | `lib/furble/protocol/GpsUbx.h:58` |
| CFG-RST | 0x06 | 0x04 | 3.10.2 | `lib/furble/protocol/GpsUbx.h:59` |
| CFG-VALSET | 0x06 | 0x8A | 3.10.5 | `lib/furble/protocol/GpsUbx.h:62` |
| CFG-VALGET | 0x06 | 0x8B | 3.10.4 | `lib/furble/protocol/GpsUbx.h:61` |
| CFG-VALDEL | 0x06 | 0x8C | 3.10.3 | `lib/furble/protocol/GpsUbx.h:60` |
| UPD-SOS | 0x09 | 0x14 | 3.19.1 | `lib/furble/protocol/GpsUbx.h:67` |
| MON-VER | 0x0A | 0x04 | 3.14.9 | `lib/furble/protocol/GpsUbx.h:63` |

Class constants are `CLASS_NAV` 0x01, `CLASS_RXM` 0x02, `CLASS_ACK` 0x05,
`CLASS_CFG` 0x06, `CLASS_UPD` 0x09 and `CLASS_MON` 0x0A
(`lib/furble/protocol/GpsUbx.h:51-56`).

## Configuration keys

The value size is encoded in bits 28 to 30 of the key, so the codec needs no type
table (`lib/furble/protocol/GpsUbx.h:79-85`). Type letters are the interface
description's: L is one bit in a byte, U1, U2 and U4 are unsigned widths, E1 is
an enumerated byte.

| Key | Address | Type | Interface description section | Default stated in code | Citation |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `RATE_MEAS` | 0x30210001 | U2 ms | 4.9.17 | 1000 | `lib/furble/protocol/GpsUbx.h:88` |
| `RATE_NAV` | 0x30210002 | U2 cycles | | 1 | `lib/furble/protocol/GpsUbx.h:89` |
| `NAVSPG_DYNMODEL` | 0x20110021 | E1 | 4.9.12 | 0 | `lib/furble/protocol/GpsUbx.h:90` |
| `SIGNAL_GPS_ENA` | 0x1031001f | L | 4.9.21 | | `lib/furble/protocol/GpsUbx.h:91` |
| `SIGNAL_GPS_L1CA` | 0x10310001 | L | 4.9.21 | | `lib/furble/protocol/GpsUbx.h:92` |
| `SIGNAL_GPS_L5` | 0x10310004 | L | F10-IFD 4.9.20 | | `lib/furble/protocol/GpsUbx.h:93` |
| `SIGNAL_SBAS_ENA` | 0x10310020 | L | 4.9.21 | | `lib/furble/protocol/GpsUbx.h:94` |
| `SIGNAL_SBAS_L1CA` | 0x10310005 | L | 4.9.21 | | `lib/furble/protocol/GpsUbx.h:95` |
| `SIGNAL_GAL_ENA` | 0x10310021 | L | 4.9.21 | | `lib/furble/protocol/GpsUbx.h:96` |
| `SIGNAL_GAL_E1` | 0x10310007 | L | 4.9.21 | | `lib/furble/protocol/GpsUbx.h:97` |
| `SIGNAL_GAL_E5A` | 0x10310009 | L | F10-IFD 4.9.20 | | `lib/furble/protocol/GpsUbx.h:98` |
| `SIGNAL_BDS_ENA` | 0x10310022 | L | 4.9.21 | | `lib/furble/protocol/GpsUbx.h:99` |
| `SIGNAL_BDS_B1` | 0x1031000d | L | 4.9.21 | | `lib/furble/protocol/GpsUbx.h:100` |
| `SIGNAL_BDS_B1C` | 0x1031000f | L | 4.9.21 | | `lib/furble/protocol/GpsUbx.h:101` |
| `SIGNAL_BDS_B2A` | 0x10310028 | L | F10-IFD 4.9.20 | | `lib/furble/protocol/GpsUbx.h:102` |
| `SIGNAL_QZSS_ENA` | 0x10310024 | L | 4.9.21 | | `lib/furble/protocol/GpsUbx.h:103` |
| `SIGNAL_QZSS_L1CA` | 0x10310012 | L | 4.9.21 | | `lib/furble/protocol/GpsUbx.h:104` |
| `SIGNAL_QZSS_L1S` | 0x10310014 | L | 4.9.21 | | `lib/furble/protocol/GpsUbx.h:105` |
| `SIGNAL_QZSS_L5` | 0x10310017 | L | F10-IFD 4.9.20 | | `lib/furble/protocol/GpsUbx.h:106` |
| `SIGNAL_GLO_ENA` | 0x10310025 | L | 4.9.21 | | `lib/furble/protocol/GpsUbx.h:107` |
| `SIGNAL_GLO_L1` | 0x10310018 | L | 4.9.21 | | `lib/furble/protocol/GpsUbx.h:108` |
| `PM_OPERATEMODE` | 0x20d00001 | E1 | 4.9.15 | FULL | `lib/furble/protocol/GpsUbx.h:109` |
| `PM_POSUPDATEPERIOD` | 0x40d00002 | U4 s | | | `lib/furble/protocol/GpsUbx.h:110` |
| `PM_ACQPERIOD` | 0x40d00003 | U4 s | | | `lib/furble/protocol/GpsUbx.h:111` |
| `PM_ONTIME` | 0x30d00005 | U2 s | | | `lib/furble/protocol/GpsUbx.h:112` |
| `PM_DONOTENTEROFF` | 0x10d00008 | L | | | `lib/furble/protocol/GpsUbx.h:113` |
| `PM_WAITTIMEFIX` | 0x10d00009 | L | | | `lib/furble/protocol/GpsUbx.h:114` |
| `PM_UPDATEEPH` | 0x10d0000a | L | | | `lib/furble/protocol/GpsUbx.h:115` |
| `ANA_USE_ANA` | 0x10230001 | L | 4.9.1 | 0 | `lib/furble/protocol/GpsUbx.h:116` |
| `MSGOUT_NAV_PVT_UART1` | 0x20910007 | U1 | 4.9.11 | 0 | `lib/furble/protocol/GpsUbx.h:117` |
| `MSGOUT_NAV_SAT_UART1` | 0x20910016 | U1 | 4.9.11 | | `lib/furble/protocol/GpsUbx.h:118` |
| `UART1OUTPROT_UBX` | 0x10740001 | L | 4.9.29 | 1 | `lib/furble/protocol/GpsUbx.h:119` |
| `UART1OUTPROT_NMEA` | 0x10740002 | L | 4.9.29 | | `lib/furble/protocol/GpsUbx.h:120` |

The four L5 keys are absent from the M10 firmware: its CFG-SIGNAL table stops
at GLONASS L1 (u-blox M10 SPG 5.10 Interface description, UBX-21035062 R03,
4.9.21 page 152, Table 42). They are listed here because furble writes them to
the dual-band firmware that has them (u-blox F10 SPG 6.00 Interface
description, UBX-23002975 R02, 4.9.20 pages 158 and 159, Table 46). A firmware
that does not know a key refuses any VALSET, VALGET or VALDEL that names it
with UBX-ACK-NAK: 3.10.4 page 53 for VALSET and VALGET and 3.10.3 pages 51 and
52 for VALDEL on the M10 firmware, and 3.10.4 page 55 with 3.10.3 page 52 on the
F10 one. That is why the band is measured before any key is written. See
[Signal bands](#signal-bands).

`valueSize()` returns the byte count implied by the key, and 0 for a reserved
size (`lib/furble/protocol/GpsUbx.h:122`. A `KeyValue` wider than the key's size
is truncated, not rejected (`lib/furble/protocol/GpsUbx.h:125-129`).

## Layers

| Name | Value | Meaning | Citation |
| :--- | ---: | :--- | :--- |
| `LAYER_RAM` | 0x01 | VALSET and VALDEL bit for the RAM layer | `lib/furble/protocol/GpsUbx.h:69` |
| `LAYER_BBR` | 0x02 | battery backed RAM | `lib/furble/protocol/GpsUbx.h:70` |
| `LAYER_FLASH` | 0x04 | external flash, which these modules do not have | `lib/furble/protocol/GpsUbx.h:71` |
| `GET_RAM`, `GET_BBR`, `GET_FLASH`, `GET_DEFAULT` | 0, 1, 2, 7 | VALGET layer selectors, codes rather than a bit mask | `lib/furble/protocol/GpsUbx.h:73-77` |

A VALSET, VALGET or VALDEL carries at most 64 keys
(`lib/furble/protocol/GpsUbx.h:77`).

## Constellation mask

| Name | Value | Citation |
| :--- | ---: | :--- |
| `GNSS_MASK_GPS` | 0x01 | `lib/furble/protocol/GpsUbx.h:408` |
| `GNSS_MASK_BDS` | 0x02 | `lib/furble/protocol/GpsUbx.h:409` |
| `GNSS_MASK_GLO` | 0x04 | `lib/furble/protocol/GpsUbx.h:410` |
| `GNSS_MASK_GAL` | 0x08 | `lib/furble/protocol/GpsUbx.h:411` |
| `GNSS_MASK_QZSS` | 0x10 | `lib/furble/protocol/GpsUbx.h:412` |
| `GNSS_MASK_SBAS` | 0x20 | `lib/furble/protocol/GpsUbx.h:413` |
| `GNSS_MASK_ALL` | 0x3f | `lib/furble/protocol/GpsUbx.h:415` |
| `GNSS_MASK_MODULE_DEFAULT` | GPS, Galileo, BeiDou B1I, QZSS, SBAS | the modules' production preset, data sheet 1.3, `lib/furble/protocol/GpsUbx.h:416-418` |

The low three bits are the CASIC `navSystem` bits that the GPS constellation
setting already uses, so a CASIC selection maps across unchanged
(`lib/furble/protocol/GpsUbx.h:403-407`).

`gnssFromConstellation()`, `gnssValid()`, `maxRateHz()` and
`dynModelForPlatform()` apply the rules, and `plan()` turns a request into the
three key lists plus the unset list
(`lib/furble/protocol/GpsUbx.h:552-572`).

| Rule | Behaviour | Citation |
| :--- | :--- | :--- |
| One major constellation, and QZSS or SBAS only with GPS | a rejected mask sets `gnss_refused` and the signals are left alone | `gnssValid()` at `lib/furble/protocol/GpsUbx.h:583`, `PlanSummary::gnss_refused` at `:513` |
| No GLONASS on a receiver without GLONASS keys | the mask is refused before it is written | `gnssValid(mask, caps)` at `lib/furble/protocol/GpsUbx.h:592` |
| Rate ceiling per mask and band | exceeding it sets `rate_clamped` | `maxRateHz()` at `lib/furble/protocol/GpsUbx.h:595` and `:605`, `PlanSummary::rate_clamped` at `:515` |
| BeiDou B1C excludes cyclic tracking power save | `psm_refused`, the receiver stays at full power | `PlanSummary::psm_refused` at `lib/furble/protocol/GpsUbx.h:514` |
| Power save on a receiver that has none | `psm_unsupported`, and no CFG-PM key is written or deleted | `PlanSummary::psm_unsupported` at `lib/furble/protocol/GpsUbx.h:524` |
| Send order | base, signals (a GNSS restart, so 0.5 s settle follows), power save with OPERATEMODE last | `struct Plan` at `lib/furble/protocol/GpsUbx.h:572-578` |
| Managed keys left alone | `unset` is deleted from BBR so an earlier session's value does not outlive the setting | `Plan::unset` at `lib/furble/protocol/GpsUbx.h:577` |
| Whether a fix is usable, dated and time-worthy | three predicates on NAV-PVT, `positionValid`, `dateTimeValid` and `timeValid` | `lib/furble/protocol/GpsUbx.h:227-233` |

## Signal bands

A band is a property of the receiver firmware, not a setting. The M10 firmware
implements L1 signals only: its CFG-SIGNAL table ends at GLONASS L1 and carries
no L5 key (u-blox M10 SPG 5.10 Interface description, UBX-21035062 R03, 4.9.21
page 152, Table 42). The F10 firmware implements GPS L5, Galileo E5a, BeiDou B2a
and QZSS L5 beside the L1 signals (u-blox F10 SPG 6.00 Interface description,
UBX-23002975 R02, 4.9.20 pages 158 and 159, Table 46).

| Name | Value | Meaning | Citation |
| :--- | ---: | :--- | :--- |
| `BAND_L1` | 0x01 | the L1 signals furble has always written | `lib/furble/protocol/GpsUbx.h:436` |
| `BAND_L5` | 0x02 | the L5 band the dual-band firmware adds | `lib/furble/protocol/GpsUbx.h:437` |
| `FAMILY_M10` | 1 | protocol 34.10, L1 only | `lib/furble/protocol/GpsUbx.h:441` |
| `FAMILY_F10` | 2 | protocol 40.00, L1 and L5 | `lib/furble/protocol/GpsUbx.h:442` |
| `CAPS_UNKNOWN` | L1, power save, GLONASS | what an unnamed firmware is given: nothing new, nothing taken away | `lib/furble/protocol/GpsUbx.h:469` |
| `CAPS_M10` | L1, power save, GLONASS | the MAX-M10S and MIA-M10Q firmware | `lib/furble/protocol/GpsUbx.h:472` |
| `CAPS_F10` | L1 and L5, no power save, no GLONASS | the F10 firmware and a NEO-F10N module | `lib/furble/protocol/GpsUbx.h:483` |

`capsFromVersion()` reads the family from a MON-VER answer: `PROTVER` 34.10 is
the M10 firmware and 40.00 the F10 one, and the `MOD` extension names the module
when the protocol version is missing (`lib/furble/protocol/GpsUbx.h:486`). Those
values are the ones each firmware prints (M10 IFD 1.2 page 10, F10 IFD 1.2 page
10), and the family is a hint until the receiver confirms it.

### The capability probe

`CapsProbe` sends one CFG-VALGET for the four L5 keys at the default layer
(`lib/furble/protocol/GpsUbx.h:678`). A firmware that has them answers with one
item each; a firmware that does not refuses the message with UBX-ACK-NAK,
because that is how an unknown key is reported (M10 IFD 3.10.4 page 53, F10 IFD
3.10.4 pages 54 and 55). Nothing is written, so the receiver configuration is
untouched whatever the answer.

The probe's refusal is counted in the receiver NAK count, so a healthy L1-only
receiver reports one NAK after every GPS start. The console says which exchange
it was: `bands: L1 (asked the receiver)` next to `naks: 1`.

The probe is the only way furble learns the band, and it runs before the first
configuration is built (`GPS::applyConfiguration` at `src/FurbleGPS.cpp:1795`, which
calls `GPS::serviceCaps` and returns, and `GPS::serviceConfig` at
`src/FurbleGPS.cpp:1755` resumes it). Two polls 1000 ms apart, then
L1: a receiver that stops answering is never sent a key it has not advertised,
because one unknown key costs the whole VALSET that carries it, the rate and the
dynamic model included. When a signal batch carrying an L5 key is refused anyway,
the driver demotes the receiver to L1 for the rest of the session and reports it
(`PlanSummary::l5_refused` at `lib/furble/protocol/GpsUbx.h:540`).

### One profile builds without L5

`FURBLE_GPS_L5` (`lib/furble/protocol/GpsUbx.h`) defaults to 1 and is set to 0
only for `m5stack-core-debug`. That profile's app partition is 1,740,800 bytes
and the release Core build already leaves 768 of them free
(`tools/size_budget.json`), while the L5 keys, the capability probe and their
console and UI reporting cost about 2.9 KB. The verbose debug console is the
point of that developer-only profile, so the profile builds L1 only: it never
polls for the L5 keys, never writes one, reports `no L5 support in this build`
in `gps ubx`, and shows `ubx l1`. `m5stack-core-usb-debug` inherits the Core
debug flags but has the single factory partition and sets the flag back to 1.
Every release profile and every other debug profile keeps the feature. The
per-profile size is `tools/check_size_budget.py --env m5stack-core-debug`.

### Exclusions a plan must respect

| Constellation or mode | Excluded with | Who refuses | Citation |
| :--- | :--- | :--- | :--- |
| BeiDou B1I | BeiDou B1C and GLONASS L1OF | the M10 firmware, and furble never builds the pair | MAX-M10S data sheet UBX-20035208 R08, 1.3 note 13 page 6, and IM 2.1.3 |
| BeiDou B1I | a dual-band receiver running B1C | the F10 module has B1C and B2a only, so the plan writes B1I off in the same RAM and BBR batch as the other signal keys, which clears a value an earlier furble left in RAM | NEO-F10N data sheet UBX-23002117 R05, Table 4 page 6 |
| BeiDou B1C | cyclic tracking and on/off power save | the M10 firmware refuses the combination | UBX-22001426 R02 section 7 page 15, and the note 28 of the MAX-M10S data sheet Table 15 page 13 |
| BeiDou B1C | AssistNow Autonomous | the M10 firmware does not cover it | UBX-22001426 R02 section 5.1.5 page 8 |
| BeiDou B1C on a dual-band receiver | AssistNow Autonomous | nothing: that coverage note is about the M10 firmware, and the F10 data sheet lists ANA with no BeiDou caveat, so the plan leaves it on | NEO-F10N data sheet UBX-23002117 R05, Table 5 page 6 |
| GPS L5 | GPS L1 C/A, per constellation | nothing: both bands of one constellation are enabled and disabled together, and a split is refused with UBX-ACK-NAK | NEO-F10N integration manual UBXDOC-963802114-12193 R04, 2.1.3 page 10 |
| Galileo E5a, BeiDou B2a, QZSS L5 | their own L1 signal, same rule | as above | NEO-F10N integration manual R04, 2.1.3 page 10 |
| QZSS and NavIC L5 | GPS disabled | the dual-band firmware, because QZSS is functional only with GPS | NEO-F10N integration manual R04, 2.1.3 page 10, and data sheet R05 Table 6 page 6 |
| GLONASS | the F10 firmware at all | no `CFG-SIGNAL-GLO-*` key exists in the F10 interface description | F10 IFD 4.9.20 pages 158 and 159, Table 46 |
| Power save modes | the F10 firmware at all | no `CFG-PM-*` key exists in the F10 interface description | F10 IFD 4.9, and NEO-F10N integration manual R04, appendix migration table, page 64 |
| No L5 key on an L1 firmware | every L5 write and delete | furble never names one; a VALDEL of an unknown key is NAKed too | F10-IFD 3.10.3 page 52 |

The rate ceiling follows the band as well as the mask. An L1 receiver keeps the
published M10 figures: 18 Hz for one constellation, 10 Hz for GPS and Galileo,
6 Hz for GPS, Galileo and GLONASS, 8 Hz for GPS, Galileo and B1C, 4 Hz for four
constellations and 3 Hz for the B1I default set (MAX-M10S data sheet
UBX-20035208 R08, Table 2 and Table 3 page 5). A dual-band receiver takes the
two figures the F10 documents publish: 20 Hz for a single constellation and
10 Hz for any multi-GNSS set (NEO-F10N data sheet UBX-23002117 R05, Table 2 and
Table 3 page 5). `maxRateHz(mask, caps)` decides which table applies
(`lib/furble/protocol/GpsUbx.h:605`).

## NAV-PVT

92 bytes, one field group per the interface description section 3.15.11
(`lib/furble/protocol/GpsUbx.h:190-241`).

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
(`lib/furble/protocol/GpsUbx.h:227-233`).

## NAV-SAT

An 8-byte header then 12 bytes per signal
(`lib/furble/protocol/GpsUbx.h:242-262`).

| Field | Type | Unit |
| :--- | :--- | :--- |
| `gnssId` | U1 | IFD 1.5 numbering: 0 GPS, 1 SBAS, 2 Galileo, 3 BeiDou, 5 QZSS, 6 GLONASS |
| `svId` | U1 | satellite id |
| `cno` | U1 | dBHz |
| `elev` | I1 | degrees |
| `azim` | I2 | degrees |
| `flags` | U4 | bits 0 to 2 qualityInd, bit 3 svUsed |

`encodeNavSat()` refuses more than 84 satellites, and the codec's MAX_PAYLOAD
bound is why (`lib/furble/protocol/GpsUbx.h:243-247`).

## MON-VER

`swVersion[30]`, `hwVersion[10]`, then 30-byte extensions
(`lib/furble/protocol/GpsUbx.h:263-274`). `field()` extracts a `KEY=value` from an
extension, for example `FWVER`.

## RXM-PMREQ

The M10 documents only the 16-byte version 0 form
(`lib/furble/protocol/GpsUbx.h:311-318`).

| Field | Type | Meaning |
| :--- | :--- | :--- |
| `duration_ms` | U4 | 0 sleeps until a wake source fires |
| `flags` | U4 | bit 1 backup, bit 2 force, bit 3 UART RX wake, bit 5 EXTINT0, bit 6 EXTINT1, bit 7 SPI CS |
| `wakeup_sources` | U4 | mask |

The integration manual requires the force flag for software standby and warns
that standby clears the RAM configuration
(`lib/furble/protocol/GpsUbx.h:318-322`). The driver sends
`PMREQ_FLAG_BACKUP | PMREQ_FLAG_FORCE | PMREQ_WAKE_UARTRX` for the duty interval
with V_IO kept up, so BBR and the RTC survive and the wake is a hot start
(`src/FurbleGPS.cpp:761-764`).

## CFG-RST

Never acknowledged (`lib/furble/protocol/GpsUbx.h:350-361`).

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
| `SOS_CREATE` | 0 | create a backup | `lib/furble/protocol/GpsUbx.h:287` |
| `SOS_CLEAR` | 1 | clear a backup | `lib/furble/protocol/GpsUbx.h:288` |
| `SOS_ACK` | 2 | backup creation acknowledge | `lib/furble/protocol/GpsUbx.h:289` |
| `SOS_RESTORED` | 3 | system restored from backup | `lib/furble/protocol/GpsUbx.h:290` |
| restore results | 0 unknown, 1 failed, 2 ok, 3 none | the `response` field of SOS_RESTORED | `lib/furble/protocol/GpsUbx.h:292-296` |

The backup lives in the receiver's flash file system, and neither MAX-M10S nor
MIA-M10Q has one, so the create command is expected to be refused
(`lib/furble/protocol/GpsUbx.h:280-284`).

## Detection

`Detect` polls MON-VER twice with 1000 ms per attempt. A u-blox receiver answers
whatever its configuration, and a receiver that ignores the frame keeps the NMEA
path (`lib/furble/protocol/GpsUbx.h:565-615`). One attempt is a whole second
because the answer is about 190 bytes queued behind up to a second of NMEA at
9600 baud (`lib/furble/protocol/GpsUbx.h:565-573`).

## Error handling

- ACK-NAK fails the pending configuration and the driver records which key was
  refused (`lib/furble/protocol/GpsUbx.h:182-188`, `src/FurbleGPSUbx.cpp`).
- A configuration frame whose acknowledgement never arrives is bounded by the
  driver's ack timeout, and the driver then reports `TIMEOUT`
  (`src/FurbleGPS.cpp:607-612,1285-1298`).
- `Settle` is a wrap-safe one-shot deadline, and an unarmed gate is open. A plain
  `deadline = 0` compared with a signed difference reads as the future for ticks
  in [2^31, 2^32), which stalled every configuration command between 24.8 and
  49.7 days of uptime (`lib/furble/protocol/GpsUbx.h:363-401`).
- A controlled reset never gets an answer, so no code waits for one
  (`lib/furble/protocol/GpsUbx.h:350-352`).

## Open rows

| Missing | Why the repository cannot close it | Document and section needed |
| :--- | :--- | :--- |
| NAV-PVT field offsets in bytes | The struct order is the layout, and the code never states an offset table | UBX-21035062 R03 section 3.15.11, the offset column |
| `flags2` bit meanings | The field is parsed and unused | Same section |
| PM state values for `PM_OPERATEMODE` | Only FULL is named | UBX-21035062 R03 section 4.9.15, the enumeration |
| `flags` bits 5 to 7 of RXM-PMREQ | Not decoded | UBX-21035062 R03 section 3.16.6 |
| MON-VER extension keys | Only `FWVER` is named | UBX-21035062 R03 section 3.14.9 and the module data sheets |
| Flash layer availability | No module in scope has an external flash | MAX-M10S and MIA-M10Q data sheets, flash section |
