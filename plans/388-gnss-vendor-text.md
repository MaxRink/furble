# 388 - Vendor text GNSS drivers: Quectel $PAIR/$PQTM and Unicore $CFG

Status: implemented. This plan lands the vendor text command seam
(`lib/furble/protocol/GpsText.{h,cpp}`), the two dialects behind it
(`GpsQuectel`, `GpsUnicore`), the `src/FurbleGPSText.cpp` backend of the GPS
driver, a virtual receiver per vendor in the simulator, host coverage, eight
simulator scenarios and the documentation.

Sources were read on 2026-11-27. Facts about this checkout were read at master
`b509486b`.

Numbering: the arbiter for plan numbers assigned 388 to this plan. 376 is taken
by the board-level documents PR, 377 to 384 by the plan renumbering PR, so the
free range starts above those. This plan was written as 367 and renamed to 388
before merge.

Tags on every claim:

- `[vendor: Q76]` Quectel "LC26G (AB)&LC76G&LC86G Series GNSS Protocol
  Specification", V1.1, 2022-12-20, status Released, 107 pages. Section and page
  of the form "Q76 2.3.9 page 38".
- `[vendor: Q29]` Quectel "LC29H Series&LC79H (AL) GNSS Protocol Specification",
  V1.2, 2023-06-20, status Released, 111 pages.
- `[vendor: UC]` Unicore "UFirebird II Series Protocol Specification",
  UC-04-FB2 EN R1.5, November 2025, applicable to UC6580, UM670A, UM680A and
  UM681A (UC 3.2 page 26).
- `[repo: P]` read in this checkout at the path named.
- `[inferred]` my conclusion, stated by no source.
- **UNVERIFIED** no readable document settles it. Collected in section 12.

Related: [366](366-gps-receiver-selection.md) (the receiver ranking this driver
makes true for two of its rows), [382](382-gnss-datasheet-verification.md) (the
document pass behind the CASIC and u-blox facts),
[321](implemented/321-ublox-m10-driver.md) (the backend split this one follows).

## 1. Why this exists

Two receivers furble's own hardware catalog lists were unconfigurable: the
Quectel LC76G and LC29H speak `$PAIR` and `$PQTM`, and the Unicore UC6580 speaks
`$CFG`. Until now furble read their NMEA output and left every setting alone, so
the rate, the constellation mask, the NMEA sentence set, the standby policy and
save-on-shutdown all silently did nothing on those parts. A user who bought one
got a receiver that fixed, and no control over it.

That is the failure mode this repository treats as a defect: a setting that does
nothing, with no message saying so. The goal is therefore not only "make the
commands work": it is that every setting furble already offers either takes
effect on these parts or is reported as refused, with the reason.

## 2. What furble drove before this PR

| Dialect | Identification | Configuration | In this checkout |
| :--- | :--- | :--- | :--- |
| `$PCAS` text plus CASIC binary | autobaud ladder | `$PCAS01`, `$PCAS02`, `$PCAS03`, `$PCAS04`, `$PCAS11`, `$PCAS12` | `lib/furble/protocol/GpsCasic.{h,cpp}`, hardware verified on the AT6668 |
| UBX | MON-VER poll | CFG-VALSET, CFG-VALGET, CFG-RST, RXM-PMREQ, UPD-SOS | `lib/furble/protocol/GpsUbx.{h,cpp}`, `src/FurbleGPSUbx.cpp`, simulator verified |
| NMEA | none | none | TinyGPSPlus over the demultiplexer |
| `$PAIR` and `$PQTM` | none | none | this PR |
| `$CFG` | none | none | this PR |

## 3. The seam

One abstraction owns what every NMEA-shaped vendor command protocol shares, and
each vendor supplies the parts that differ. `lib/furble/protocol/GpsText.h`:

| Concern | Owner | Interface |
| :--- | :--- | :--- |
| framing | seam | `frame(body)` builds `$` body `*` XX CR LF |
| checksum | seam | `checksum(body)`, the XOR between `$` and `*` |
| reply parse | seam | `parse(line)` splits the name, the fields, the checksum verdict and the terminator |
| prefix routing | seam | `prefix()` tells the demultiplexer whether a `$` line is a vendor line |
| ack matching | seam plus dialect table | `match(line, command)` returns a `Verdict` |
| timeout and retry | seam | `Session`, one command in flight, three attempts, one second each |
| command bodies | dialect | `GpsQuectel`, `GpsUnicore` |
| planner | dialect | `Quectel::plan(request)`, `Unicore::plan(request)` |

### 3.1 Why a dialect enum instead of an interface

A virtual `VendorProtocol` with two subclasses would cost a vtable plus an
allocated object in an image that is already 512 bytes of headroom away from its
DRAM budget (section 8). The two vendors differ in a name table, an
acknowledgement shape and a set of command bodies, not in behaviour: every step
of the state machine is the same. So the dialect is a value
(`GpsText::Dialect`) and the differences are data plus two switches, matching
`Ubx::plan()` rather than an object graph.

### 3.2 The acknowledgement shapes are not the same

This is the part that makes one seam worth having and also the part that must
not be glossed over:

| | Quectel | Unicore |
| :--- | :--- | :--- |
| acknowledgement | `$PAIR001,<CommandID>,<Result>` | `$OK` |
| refusal | `$PAIR001,<id>,<Result>` with result 2, 3, 4 or 5 | `$FAIL,<code>` |
| names the command? | yes, the first field is the packet id | **no**, neither `$OK` nor `$FAIL` carries an identifier |
| data reply | the command name repeats, for example `$PQTMSAVEPAR,OK` | the command name repeats, for example `$PDTINFO,...` |

A Quectel ack is safely matched by id: an ack for another packet id is not a
verdict at all (`Verdict::NONE`). A Unicore `$OK` cannot be matched that way, so
the seam relies on the driver's existing one-command-in-flight rule and says so
in the header. That is a real property of the protocol, not a simplification:
UC 3.2 pages 30 and 31 define the two responses without any command name.

Result codes, Q76 2.3.1 page 32 and Q29 2.4.1 pages 51 to 52:

| Result | Meaning | Verdict |
| ---: | :--- | :--- |
| 0 | command sent successfully | `ACKED` |
| 1 | being processed, wait | `PENDING`, which does not spend an attempt |
| 2 | sending failed | `NACKED` |
| 3 | command id not supported | `UNSUPPORTED` |
| 4 | parameter error, out of range, or checksum error | `PARAM` |
| 5 | MNL service busy | `BUSY` |

Unicore error codes, UC 3.2 page 31: 0 is a rejected parameter (`PARAM`), 1 is a
checksum the receiver refused (`BAD_REPLY`).

## 4. Quectel: the command table

All commands are `$` sentences with the checksum of Q76 2.1 Table 3 pages 11 and
12. The PAIR frame is `$PAIR<PacketType>[,<Data>]<Checksum><CR><LF>` (Q76 2.3
page 32, Q29 2.4 page 51).

| Operation | Command | Q76 section, page | Q29 section, page | Notes |
| :--- | :--- | :--- | :--- | :--- |
| identification | `$PQTMVERNO` | 2.4.5, 99-100 | 2.3.1, 33 | reply `$PQTMVERNO,<VerStr>,<BuildDate>,<BuildTime>` |
| save PQTM settings | `$PQTMSAVEPAR` | 2.4.3, 98 | 2.3.2, 34 | reply `,OK` or `,ERROR,<code>` |
| restore defaults | `$PQTMRESTOREPAR` | 2.4.4, 98 | 2.3.3, 35 | not sent by the driver |
| message rate | `$PQTMCFGMSGRATE,W,PQTMEPE,<n>,2` | 2.4.1, 96-97 | 2.3.5, 36-37 | only PQTMEPE on LC76G |
| fix rate | `$PAIR050,<ms>` | 2.3.9, 38 | 2.4.9, 57 | 100 to 1000 ms; LC76G (PA)/(PB) refuse it (Q76 note 2 page 38) |
| NMEA sentence rate | `$PAIR062,<type>,<n>` | 2.3.12, 40-41 | 2.4.13, 61 | types 0 to 9; LC29H (BA, CA, DA, EA) accept types 0 to 5 and rate 0 or 1 only |
| constellation | `$PAIR066,<GPS>,<GLO>,<GAL>,<BDS>,<QZSS>,0` | 2.3.15, 43-44 | 2.4.15, 63-65 | reboots the GNSS engine on LC76G; refused on LC29H (BA, CA, DA, EA) (Q29 note 4 page 65) |
| navigation mode | `$PAIR080,<mode>` | 2.3.23, 50-51 | 2.4.23, 72 | 0 normal, 1 fitness, 5 drone, 7 swimming |
| sleep lock | `$PAIR382,1` | 2.3.30, 56 | 2.4.31, 80 | required before `$PAIR003` if the port must still answer |
| SBAS | `$PAIR410,<0/1>` | 2.3.34, 60 | 2.4.35, 84 | not supported in fitness or swimming mode |
| GNSS off | `$PAIR003` | 2.3.3, 34 | 2.4.3, 53 | |
| GNSS on | `$PAIR002` | 2.3.2, 34 | 2.4.2, 52 | sent after a refused rail cut; an LC86G has no `$PAIR002` (Q76 2.3.2 note page 34) and takes `$PAIR004` |
| save navigation data | `$PAIR511` | 2.3.44, 67-68 | 2.4.45, 93 | above 1 Hz the GNSS must be off first |
| save settings | `$PAIR513` | 2.3.45, 68-69 | 2.4.46, 94 | same rule |
| backup mode | `$PAIR650,<s>` | 2.3.46, 69 | 2.4.47, 95 | 0, or 10 s to 62208000 s |
| baud rate | `$PAIR864,0,0,<baud>` | 2.3.56, 78 | 2.4.53, 100-101 | 9600 to 921600; reboot required |
| restart | `$PAIR004` / `$PAIR005` / `$PAIR006` | 2.3.4 to 2.3.6, 34-36 | 2.4.4 to 2.4.6, 54-55 | hot, warm, cold |

### 4.1 The constellation rule, and the two-of-three gap

Q76 2.3.15 note 2 page 44 lists the combinations the LC76G family supports: GPS
alone, GPS plus QZSS, GPS plus GLONASS, GPS plus GLONASS plus QZSS, GPS plus
Galileo, GPS plus Galileo plus QZSS, GPS plus BDS, GPS plus BDS plus QZSS, the
full GPS plus GLONASS plus Galileo plus BDS, and that set plus QZSS. **GPS plus
GLONASS plus Galileo is not in the list, and neither is GPS plus Galileo plus
BDS.** `Quectel::gnssSupported()` therefore accepts zero, one or three of the
non-GPS systems and refuses two, and the driver logs the refusal instead of
sending a combination the receiver's own document does not list.

### 4.2 The standby boundary

`$PAIR650,<Second>` takes 0 (no timer) or 10 to 62208000 s (Q76 2.3.46 page 69,
Q29 2.4.47 page 95). furble's duty ladder is 5, 10 and 15 s. **5 s is inside the
undocumented gap**, so `Quectel::standby()` refuses it, `configureQuectel()`
reports `psm_refused`, and `textStandby()` logs why and leaves the receiver
awake. Sending `$PAIR650,5` would earn result 4 and burn a retry on something
that cannot work.

## 5. Unicore: the command table

The frame is `$MSGNAME,data1,data2,*CC` with CR LF (UC 3.1 pages 24 and 25). The
checksum is the XOR of everything between `$` and `*`, an input checksum is
optional but checked when present, and the total length is capped at 128 bytes.
Unsigned integers are decimal, or hexadecimal with a leading `h` or `H` (UC 1.3
pages 3 and 4).

| Operation | Command | UC section, table, page | Notes |
| :--- | :--- | :--- | :--- |
| identification | `$PDTINFO` | 3.2, Table 3-1, 26 | reply `$PDTINFO,<pdtName>,<config>,<hwVer>,<fwVer>,<PN>,<SN>` |
| acknowledgement | `$OK` | 3.2, Table 3-6, 30 | carries no command name |
| refusal | `$FAIL,<code>` | 3.2, Table 3-7, 31 | 0 parameters, 1 checksum; no reply to an illegal command |
| port | `$CFGPRT,1,0,<baud>,1,3` | 3.3, Tables 3-8 and 3-9, 31-33 | keep the UNICORE input on, add NMEA output |
| message rate | `$CFGMSG,0,<id>,<rate>` | 3.3, Tables 3-10 to 3-12, 34-36 | ids 0 GGA to 8 GBS; rate 0 to 100, 0 disables |
| fix rate | `$CFGNAV,<ms>,<ms>,` | 3.3, Tables 3-18 and 3-19, 43 | measRate must equal navRate or the receiver answers FAIL |
| constellation | `$CFGSYS,h<mask>` | 3.3, Tables 3-20 and 3-21, 44-46 | resets the receiver; QZSS needs GPS, SBAS needs GPS or BDS |
| NMEA version | `$CFGNMEA` | 3.3, Table 3-13, 40 | answered `,H52`; not sent by the driver |
| dynamic model | `$CFGDYN,1,<0 or 1>,0` | 3.3.20, Table 3-41, 58 | 0 Portable, 1 Static; Pedestrian and Vehicle have no counterpart |
| save | `$CFGSAVE` | 3.3, Table 3-24, 47 | flash-version products only; do not power off for 1 s |
| clear | `$CFGCLR` | 3.3, Table 3-25, 47 | not sent by the driver |
| restart | `$RESET,0,<mask>` | 3.2, Table 3-5, 29 | h00 hot, h01 warm, h85 or hFF cold |

`sysMask` bits used by the driver (UC 3.3 Table 3-21 pages 44 to 46): bit 0 GPS
L1CA, bit 4 BDS B1I, bit 8 GLONASS L1, bit 12 Galileo E1, bit 16 QZSS L1CA, bit
20 SBAS. L5 keys exist in the same table and are not written, because furble's
signal table is L1 only and its settings have no L5 switch.

### 5.1 What Unicore does not have

There is **no standby, sleep or power-save command anywhere in UC R1.5**, so
`Unicore::plan()` sets `psm_refused` and the driver logs it once. A user who
selects Standby on a UC6580 gets a receiver that keeps running and a console line
saying why, which is the behaviour this PR is for.

The dynamic platform model does exist. `$CFGDYN,mask,DynModel,StaticHoldThresh`
(UC 3.3.20, Table 3-41, page 58) takes DynModel 0 Portable and 1 Static, with
mask bit 0 selecting it and the document's own example `$CFGDYN,1,0,0`. Those two
values are exactly furble's Portable and Stationary, so `Unicore::plan()` queues
them; Pedestrian and Vehicle have no documented counterpart and are reported
refused. A reviewer found this: the earlier text of this section said the
document had no dynamic model at all, which was wrong and is corrected here and
in the three documents that repeated it.

Every refusal the planner makes is visible in three places, not only the log:
`gps text` on the console, the `refused:` line on the Raw NMEA page, and
`gps.text.refused` in the simulator. The first two are what a user without a
serial cable sees.

## 6. Integration points

| Point | Before | After |
| :--- | :--- | :--- |
| demultiplexer | NMEA, CASIC, UBX | plus `Kind::VENDOR`: one complete `$PAIR`, `$PQTM`, `$CFG`, `$PDTINFO`, `$OK` or `$FAIL` line, bounded by `GpsText::MAX_LINE` |
| detection | MON-VER poll, two attempts | MON-VER, `$PQTMVERNO` and `$PDTINFO` in one window, still two attempts, so the NMEA fallback keeps its 2 s budget |
| identity | `backend_t::NMEA` or `UBX` | plus `QUECTEL` and `UNICORE`, reported by `gps status` (`backend:`), `gps text` and `GPS::getBackend()` |
| configuration | CASIC queue or UBX queue | plus one text queue per vendor, same one-in-flight discipline, same status list, `GpsText::Op` in the message-id column |
| rate ladder | `GPS_RATE` to `RATE_MS` | unchanged values; PAIR050 and CFGNAV both accept all four |
| constellation | `GPS_GNSS` mask, else `GPS_CONSTEL` | same request, mapped to PAIR066 or CFGSYS, with each vendor's own validity rule |
| NMEA sentence set | `$PCAS03` for CASIC, untouched for UBX | PAIR062 per type for Quectel, CFGMSG per id for Unicore |
| standby | RXM-PMREQ or `$PCAS12` | plus PAIR650 for Quectel, a reported refusal for Unicore |
| save on shutdown | CFG-RST plus UPD-SOS | plus PAIR382/003/513/PQTMSAVEPAR/511, or CFGSAVE |
| restart | CFG-RST or `$PCAS10` | plus PAIR004/005/006 or `$RESET` |
| satellite page | `$PCAS03` sentence set | text parts keep the configured set; the page reads what the receiver already sends |
| MON-HW | CASIC only | unchanged: the poll is refused on every non-CASIC backend now, not only UBX |

### 6.1 The VENDOR line and the existing three

The splitter treats `$` lines that start a vendor name as one piece and
everything else as it always did. `$PCAS03` is not a vendor name, so the CASIC
path is untouched, and `gps-ubx` (which pins the split byte for byte) still
passes. A vendor line that never terminates is dropped one byte at a time past
`GpsText::MAX_LINE`, so a runaway line cannot grow the receive buffer, and the
fuzzer's `buffer.size() <= MAX_PENDING` invariant still holds.

One trap cost a debug cycle and is worth recording: `lineEnd()` originally
returned the buffer length both for "no terminator" and for "a terminator that
ends exactly at the buffer end", which silently swallowed every vendor line that
arrived in its own read. It now returns a bool plus an out parameter, and the
`gps-text` host test pins the completed-line-at-end-of-buffer case.

### 6.2 Rate, masks and where the values come from

`textRequest()` builds one `GpsText::Request` from the same settings the UBX
planner reads: `GPS_RATE` through `RATE_MS`, `GPS_GNSS` or `GPS_CONSTEL`,
`GPS_PSM`, `GPS_PLATFORM`, `GPS_NMEA` and the applied duty interval. The
constellation bit values are `Ubx::GNSS_MASK_*`, deliberately: it is one stored
setting, and the `gps-text` test asserts the two constant sets are equal so a
change in either fails there.

## 7. Simulator

| Model | File | What it does |
| :--- | :--- | :--- |
| `Sim::VirtualQuectel` | `sim/vendor_gnss.{h,cpp}` | PAIR and PQTM commands, `$PAIR001` acks with every result code, parameter range checks, backup mode with a wake timer, save and rail-cut persistence, and the documented gaps (`noFixRate`, `noSearchMode`) |
| `Sim::VirtualUnicore` | `sim/vendor_gnss.{h,cpp}` | `$PDTINFO`, `$OK` and `$FAIL`, CFGMSG/CFGNAV/CFGSYS/CFGPRT validation, CFGSAVE persistence, a reset on CFGSYS, silence for an illegal command, and the `flash` and `silent` gaps |

Both own no clock and no UART, as the virtual M10 does, so the host test drives
the same state machines the scenarios drive. The fake UART routes writes to
whichever model is selected by `seed gps_model quectel` or `unicore`, and the
scenarios read `text.<field>` for the model's own view.

## 8. Tests

| Test | Covers |
| :--- | :--- |
| `gps-text` (host) | framing against six checksums the Quectel documents print, prefix routing including `$PCAS` and `$GPGGA` negatives, reply parsing, every Quectel result code, `$OK`/`$FAIL`, the session's ack, nack, timeout, retry, busy and pending-cap behaviour, the settle window against the late `$OK` sequence, both planners including `$CFGDYN` and the power-on after a refused cut, the demultiplexer's vendor pieces including the `MAX_LINE` bound on a terminated line |
| `gps-text-sim` (host) | the same seam driven end to end against both virtual receivers: identification, a good command, a nacked command, an unsupported id, a corrupt checksum, a timeout, standby with a wake, the save path with a rail cut, and a whole planner queue |
| `console-commands` (host) | `gps text` and the `backend:` line `gps status` prints |
| `gps-ubx`, `gps-ubx-fuzz` | the existing three dialects still split byte for byte as before |
| `gps-quectel` (sim) | detection, the ten-command plan, the receiver's kept state, the fix |
| `gps-quectel-standby` (sim) | PAIR650 at 10 s, backup mode, no epochs, the wake fix |
| `gps-quectel-refusals` (sim) | a refused PAIR050, the rest of the plan continuing, and the 5 s standby refusal |
| `gps-quectel-save` (sim) | the save sequence, the rail cut, and the settings surviving it |
| `gps-unicore` (sim) | detection, the nine-command plan including CFGDYN static, the system mask, the reset |
| `gps-unicore-refusals` (sim) | no standby, no CFGDYN for Vehicle, a flashless save refusal, the refusal named in `gps.text.refused` and on the Raw NMEA page, and a silent part falling back to NMEA |
| `gps-quectel-rail-refused` (sim) | the save powering the GNSS off, a refused cut, and the `$PAIR002` that brings it back |

Every host test and every scenario fails without the driver: the host tests link
`GpsText.cpp`, `GpsQuectel.cpp` and `GpsUnicore.cpp` and would not build, and the
scenarios assert the backend, the acks and the receiver state the driver has to
produce.

## 9. Size

Measured with `python3 tools/check_size_budget.py --env m5stick-c
--env m5stick-c-plus --env m5stick-s3` after the branch's own builds:

| Env | Flash | Static DRAM | IRAM |
| :--- | :--- | :--- | :--- |
| `m5stack-core-debug` | 1,740,112 of 1,740,800 | 56,348 of 57,344 | 119,036 of 119,808 |
| `m5stick-c` | 1,525,056 of 1,740,800 | 55,172 of 55,296 | 114,024 of 114,688 |
| `m5stick-c-plus` | 1,574,816 of 1,740,800 | 55,148 of 55,296 | 114,024 of 114,688 |
| `m5stick-s3` | 1,884,352 of 3,145,728 | 47,608 of 48,128 | 114,944 of 115,712 |

`m5stack-core-debug` is the one profile that runs with `FURBLE_GPS_TEXT=0`, so its
flash number is after the backend came out; see 9.1.

**No budget is raised.** The first version of this branch put the text backend's
fixed state in `.bss` and needed the S3 budget lifted to 49,152; the review's
size pass then showed the classic ESP32 boards over their 55,296 byte budget
(55,404, 108 over, 512 bytes short of the headroom the budget was written with).
`text_state_t` now lives on the heap instead: `enable()` allocates it before the
UART task is woken, `disable()` frees it after the save path, a failed allocation
leaves the text backends off, and `getTextStatus()` answers an empty record while
it is absent. That removed 232 bytes of `.bss` on every board, which puts all
three under their existing budgets, so `tools/size_budget.json` is unchanged and
the S3 value goes back to 48,128 with 520 bytes of headroom.

What stays in `.bss` is the identity byte, the status mutex and one pointer. The
in-flight configuration command is still an in-DRAM `std::optional` for every
backend; moving it would touch the UBX and CASIC paths for about 150 bytes, which
this PR does not need once the text state is out.

### 9.1 Flash, and the Core debug capability

Flash is the other constraint. The seam, both dialects and the driver backend
measure 4,052 + 6,067 + 5,158 + 9,490 bytes of object text on a classic ESP32
build, about 24 KB before the linker drops what an unused path leaves behind.
The `m5stack-core-debug` profile has a fixed 1,740,800 byte app slot from
`partitions_two_ota_large.csv`, a full verbose debug log level and the console,
and the branch took it past that slot, which cannot be raised without giving up
a second OTA target. So the backend is a build capability:

- `FURBLE_GPS_TEXT` is absent (on) in every environment except
  `m5stack-core-debug`, which sets `-DFURBLE_GPS_TEXT=0` in `platformio.ini`.
- Off `GpsText.cpp` is not compiled, `GpsQuectel.cpp` and `GpsUnicore.cpp` are
  left unreferenced so the linker drops every symbol in them,
  `src/FurbleGPSText.cpp` becomes stubs that keep every entry point linkable,
  and the splitter's vendor routing, the `gps text` console command and its
  dialect helper, the refusal names on the Raw NMEA page and the
  `diag.nmea.refusals` query are gated too. The driver allocates nothing and
  sends no text probe, so identification falls back to MON-VER and then NMEA,
  and a `$PAIR` or `$CFG` line is ordinary NMEA text the parser ignores.
- The integration follows the capability as well, not only the seam: the
  shutdown save queue and its step, the standby dispatch, the rail restart after
  a refused cut, the detection shortcut that reads the answered dialect and the
  `resetText()`/`freeTextState()` calls are all gated, so a text-off build keeps
  master's UBX and NMEA paths and nothing else. With them in, the build was 928
  bytes over its slot even though the seam was out; with them gated it measures
  1,740,112 of 1,740,800 by `check_size_budget.py`, 688 bytes of headroom, and
  pio reports 1,739,605. Master at `d31b8f95` measures 1,740,016, so the whole
  feature costs 96 bytes in this profile.
- A Core debug build therefore keeps the NMEA receiver, the CASIC `$PCAS` path
  and the u-blox backend, and does not configure a Quectel or Unicore part. That
  is a real loss on that profile and it is recorded here rather than hidden; the
  Core release build, both StickC builds, every S3 build and the simulator keep
  the backend.
- `m5stack-core-usb-debug` extends the Core debug environment and inherits the
  flag with the rest of its build string. It has a single factory partition with
  room, so it could keep the backend, but PlatformIO matches `build_unflags`
  against whole list items and the inherited flags arrive as one item; splitting
  it means spelling its flags out, which this PR does not do. A follow-up can
  give that profile its own list.

## 10. Deviations from the request, and why

1. **One file per vendor rather than one driver class per vendor.** The request
   asked for "two drivers behind it"; the two dialects are `GpsQuectel` and
   `GpsUnicore` with one shared session, because the retry policy is identical
   and a second copy would be two places to fix a timeout.
2. **Baud configuration is implemented but not exercised by a setting.** There
   is no `GPS_BAUD` value that asks for a non-default rate beyond Auto, so the
   builders `Quectel::baud()` and `Unicore::port()` exist and are host-tested,
   and no driver path sends them yet. The autobaud ladder still owns rate
   discovery.
3. **The Core debug build compiles the text backend out.** It is the only
   profile whose fixed 1,740,800 byte slot cannot hold the branch, and the
   alternative was a partition change. `FURBLE_GPS_TEXT=0` leaves 96 bytes over
   master's own measurement there; see 9.1. Every other build keeps Quectel and
   Unicore support.
4. **`$PAIR002` (GNSS on) is sent only after a refused cut.** The save path
   powers the GNSS off before a rail cut and the rail does the rest, so there is
   nothing to turn back on unless the cut is refused. That case is real (a Core2
   on a low battery, or an IR hold) and used to leave the receiver fixless, so
   `railOffOrRestart()` now sends `$PAIR002`, or `$PAIR004` on an LC86G. Whether
   every part accepts it after `$PAIR511` at a non-1 Hz rate is UNVERIFIED
   (12.6).

## 11. Documentation updated in this PR

- `docs/protocols/gnss-quectel-pair.md` and `docs/protocols/gnss-unicore-cfg.md`,
  new pages with the tables above.
- `docs/protocols/README.md`, the protocol index.
- `README.md`, the GPS section and the feature table.
- `docs/supported-hardware.md`, the receiver table and the capability paragraph.
- `docs/wiki/Getting-Started.md` and `docs/wiki/Settings-Reference.md`, the
  receiver list and what each setting does on each part.
- `docs/console-commands.md`, `gps text` and the `backend:` line.
- `lib/furble/CLAUDE.md`, `src/CLAUDE.md`, `include/CLAUDE.md`,
  `sim/CLAUDE.md` and `tests/host/CLAUDE.md`, the directory rules.
- `docs/sim.md`, the new seed and query keys.

## 12. UNVERIFIED

1. **Every command on real Quectel hardware.** Nothing here has run on an LC76G,
   an LC29H or any other Quectel part. The formats come from the two protocol
   specifications, the behaviour comes from the simulator model.
2. **Every command on real Unicore hardware.** The same for the UC6580. The
   UC R1.5 document states the frames; no device confirmed that this firmware
   revision accepts them from a host at 115200 with NMEA output enabled.
3. **How a Quectel part answers a command it does not know.** The documents
   define result 3 for an unsupported *packet id* and say nothing about an
   unknown `$` name. Detection therefore relies on `$PQTMVERNO` being answered,
   and a part that stays silent is indistinguishable from a part with no PQTM
   support.
4. **How a CASIC AT6668 or an L76K reacts to `$PQTMVERNO` and `$PDTINFO`.** The
   driver sends both during detection on the assumption that a receiver ignores a
   sentence it does not implement. Neither document states what it does with an
   unknown proprietary sentence. If a CASIC part answered one of them with
   something that parses as a vendor reply, it would be misidentified.
5. **`$PAIR066` field order against Bayerwald-style variants.** The field order
   is read from the synopsis and the worked examples (GPS, GLONASS, Galileo,
   BDS, QZSS, reserved). The examples cover GPS+Galileo+BDS, GPS alone and the
   full four, which is consistent, but no example prints Galileo and BDS
   together with GLONASS off.
6. **`$PAIR002` after a refused rail cut.** Q76 2.3.44 note 2 page 69 documents
   the re-power after a save at more than 1 Hz, and the driver now sends
   `$PAIR002` whenever the cut it powered down for is refused, or `$PAIR004` on
   an LC86G. That an LC86G really has no `$PAIR002` rests on Q76 2.3.2 note page
   34, which the review read and this session could not re-read: the fetched
   copy truncated before page 34. Whether every part accepts the frame after
   `$PAIR511` at a non-1 Hz rate needs a device. The simulator covers the
   sequence, not the silicon.
7. **`$CFGNAV` with an empty `DRNavRate`.** UC 3.1 page 24 says an omitted
   parameter is an empty field before `,` or `,*`, and UC 3.3 page 43 says the
   parameter is invalid for non-integrated products. Whether a UC6580 accepts the
   empty field or wants a number is not stated.
8. **`$CFGMSG` class 0 versus class 200.** Class 0 "configures four ports or
   queries the current port" (UC 3.3 Table 3-12 page 36). The driver writes class
   0 so whichever port furble is on receives the setting. Whether that also
   changes a second port the product may expose is not stated.
9. **The 1 s `$CFGSAVE` power-off window.** UC 3.3 page 47 says not to power off
   within one second of the command. The save budget is 1.5 s and the rail cut
   waits for it, but whether a cut at 1.1 s is safe on a part that answered
   slowly is unmeasured.
10. **The `PENDING` result.** Result 1 means "being processed" (Q76 2.3.1 page
    33). The session treats it as "keep waiting without spending an attempt",
    re-arms the one second deadline, and gives up after five answers as
    `Verdict::STALLED` so a receiver that never finishes cannot hold the queue.
    How long a real MNL takes to move from 1 to 0 for a `$PAIR066` that reboots
    the GNSS engine is unmeasured, so five is a bound, not a measurement.
11. **The `PAIR650` wake path.** The document says the module leaves backup mode
    when the timer expires (Q76 2.3.46 page 69) and that it cannot receive
    commands while it is there. Whether the first epoch after the wake is at the
    configured rate or needs a settling period is not stated, so the burst
    predictor may see one late or early burst per wake.
12. **`$CFGDYN` on a real UC6580.** UC 3.3.20 Table 3-41 page 58 defines
    `$CFGDYN,1,0,0` for Portable and `$CFGDYN,1,1,0` for Static, and the model
    accepts exactly that. Whether the part also wants a non-zero
    StaticHoldThresh for the static model to hold, and whether it answers `$OK`
    or says nothing, is not stated and needs a device.
13. **A Unicore `$OK` arriving late.** Because `$OK` carries no command name, an
    acknowledgement that arrives after its session failed would be taken for the
    next command's answer. The driver now closes the seam for one second after a
    Unicore failure and drops every windowed line inside it, which a host test
    pins against the exact sequence. Whether a real part ever holds a reply for
    longer than that second is unmeasured; if it does, the line would be dropped
    rather than miscredited, which is the safe direction.

## 13. Sources

All URLs read on 2026-11-27.

- Q76 Quectel "LC26G (AB)&LC76G&LC86G Series GNSS Protocol Specification" V1.1,
  mirror:
  https://www.sigmaelectronica.net/wp-content/uploads/2024/07/quectel_lc26gablc76glc86g_series_gnss_protocol_specification_v1-1-2.pdf
- Q29 Quectel "LC29H Series&LC79H (AL) GNSS Protocol Specification" V1.2, mirror:
  https://dfimg.dfrobot.com/wiki/19407/KIT0212_gnss-rtk-high-precision-positioning-kit-4g-for-nmea-thailand_datasheet_1.pdf
- UC Unicore "UFirebird II Series Protocol Specification", UC-04-FB2 EN R1.5:
  https://ko.unicore.com/uploads/file/UFirebirdII%20Series%20Protocol%20Specification_EN_R1.5.pdf
- S5 and S6 of [366](366-gps-receiver-selection.md), the LC76G and LC29H GNSS
  specifications, for the parts themselves.

Repository citations, not URLs:

- `lib/furble/protocol/GpsText.h` holds the seam contract and the document list.
- `lib/furble/protocol/GpsQuectel.h` and `lib/furble/protocol/GpsUnicore.h` hold
  every command with its section and page.
- `src/FurbleGPSText.cpp` is the backend, `sim/vendor_gnss.cpp` the models.

## Implementation state

Landed in this PR: the seam, the two dialects, the driver backend, the
demultiplexer piece, detection, configuration, standby, save, restart, the
console surface, the two virtual receivers, two host suites, eight scenarios and
the documentation listed in section 11. The review of PR 552 added `$CFGDYN`,
the power-on after a refused rail cut, the settle window, the busy and pending
caps, the id-before-checksum match, the `MAX_LINE` bound on a terminated line,
and the refusal line on the Raw NMEA page; `plans/552-review-feedback.md` answers
every finding.

Gates run before the PR: the host suite, the four GPS suites, the GPS fuzz
target, `check_sim_scenarios.py`, `pre_review.py`, the doc token check and both
citation gates, plus `m5stick-c`, `m5stick-c-plus` and `m5stick-s3` firmware
builds and `check_size_budget.py` on all three.

Not done, and not claimed: any run on Quectel or Unicore hardware. Every row of
section 12 needs a device.
