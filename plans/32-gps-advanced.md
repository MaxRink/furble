# PR32 - Advanced GPS support for the GPS/BDS Unit v1.1

## Goal

Use the rest of what the AT6668 receiver offers, and stop making the user
configure it by hand. PR14 gave furble a transmit path and three settings. This
document covers the parts PR14 deliberately left out: knowing which module is
attached and at what baud, knowing whether a command was accepted, telling the
receiver what kind of motion to expect, shortening the time to first fix, and
showing enough per satellite detail to explain a slow fix.

Five separately mergeable pull requests. Every default keeps current behaviour
except one, and that exception is called out explicitly in PR32a.

All line anchors on `master` were read at commit `2b79ce8`. Anchors marked
"on feat/14" were read on branch `feat/14-gps-pcas`, which is upstream PR #303
and not yet merged.

## Implementation state

Phase 1 is implemented on `feat/32-gps-advanced`.

- The GPS UART now demultiplexes NMEA and CASIC binary frames.
- The corrected ID-first checksum is used for binary transmit and receive.
- `CFG-MSG`, `CFG-RATE` and `CFG-NAVX` use one outstanding command, three
  attempts and a 300 ms acknowledgement timeout. NMEA fallback remains
  available once per configuration pass.
- The USB console has `gps binary`, `gps config` and `gps aid` subcommands.
- `GPS_ASSIST` uses `gps_assist`, wire id 41 and default 0. Renumbered from the
  provisional 36 during the rebase onto master, the settings ledger next-free is
  41. Modes 1 and 2 send
  the phase 1 AID-INI position and time payload. Mode 2 does not replay
  ephemeris yet. The UI exposes the implemented off and position and time
  choices.
- Live fixes update the `gps_fix` cache at most every 10 minutes. AID-INI uses
  the session tick for fresh cache age and coarse wall time after reboot.
- Stale-time guard. When the session tick is unavailable and the wall clock has
  not been set, cache age cannot be bounded, so AID-INI clears the B1 time valid
  flag and sends position-only aiding. A no-backup-rail cold reboot leaves the
  ESP32 clock at the 1970 epoch, and asserting a valid time of unknown age would
  feed the receiver an over-confident wrong time and hurt time to first fix. The
  cached position still narrows the search safely.
Phase 2 is implemented on `feat/32-gps-advanced-phase2`, branched from
`fork/master`. The parseable logic moved into a host-tested module,
`lib/furble/protocol/GpsCasic.{h,cpp}`, covered by the `gps-casic` host test.

- PR32a autobaud and no-receiver state, DONE. `GPS_BAUD` gains the sentinel
  value 0 for Auto. A new `Casic::Autobaud` ladder probes 115200, 9600, 38400,
  57600, 19200 then 4800, one NMEA sentence pair per rate, and locks on the
  first rate that yields two passing checksums. When the ladder finds nothing it
  enters a named `absent` state, drops the 5 V rail, and retries once after 60 s.
  The detection state and detected baud show in `gps status` and on the Raw NMEA
  page. **Deviation from the plan, deliberate:** the default stays `9600`, not
  `Auto`. The task brief requires the default to preserve current behaviour, so
  Auto is opt-in. Existing installs are unchanged; only a user who selects Auto
  gets detection. The `$PCAS06` identity query and `$GPTXT` parse are not
  implemented; detection relies on the checksum criterion alone.
- PR32d tier 2 ephemeris replay, DONE in framing, hardware-tuning-pending in
  effect. `GPS_ASSIST` mode 2 is now selectable in the roller. On a stable fix
  furble polls `MSG-GPSEPH` 0x08 0x07, `MSG-GPSION` 0x08 0x06 and `MSG-GPSUTC`
  0x08 0x05 with a `CFG-MSG` rate 0xFFFF poll, stores the frames verbatim in the
  `gps_eph` NVS blob at most once an hour, and replays them paced one frame at a
  time on the next enable, refusing a cache older than four hours. The store,
  age-bounding and paced-replay framing are host-tested. Whether the replay
  actually shortens time to first fix on this firmware needs the bench, per the
  plan's own measurement caveat.
- PR32e satellite detail page, DONE. A direct GSV and GSA parser
  (`Casic::NmeaSatellites`) fills a per satellite table with id, constellation,
  elevation, azimuth, C/N0 and the used flag, plus PDOP, HDOP, VDOP and fix
  type. It runs only while the Satellites page or `gps sats on` is active, which
  un-prunes GSV and GSA and restores the user's set on close. Multi-sentence GSV
  reassembly and the DOP parse are host-tested.
- PR32c dynamic platform, scaffolded, hardware-tuning-pending. `GPS_PLATFORM`
  (wire id 69, key `gps_plat`, default 0 do-not-send) selects Portable,
  Stationary, Pedestrian or Vehicle. It is applied through the same `CFG-NAVX`
  query-modify-write as the constellation mask, editing `dyModel` at offset 4
  under mask bit B0, with a `$PCAS11` NMEA fallback. The `$PCAS11` numbering is
  third-party attested only, so it is provisional; the on-device effect of
  `dyModel` is unmeasured. Reachable from the console with `gps platform <0-4>`.
- MON-HW interference poll, scaffolded, hardware-tuning-pending. `gps monhw`
  sends the poll and prints the decoded and raw response. The 56-byte layout
  could not be confirmed against a live unit, so `Casic::parseMonHw` reads a
  conservative subset and the console prints the raw bytes and marks it pending.
- Companion-fed assistance (tier 3) remains out of scope; it belongs to
  `plans/50-companion-app-design.md`.
- Data sources: the CASIC binary framing, the id-first checksum, `AID-INI`,
  `CFG-NAVX` `dyModel`, the `MSG-GPSEPH/ION/UTC` ids and `MON-HW` come from the
  CASIC v3.6 specification; the GSV and GSA field layouts, the GNSS numbering and
  the corrected checksum formula come from the Quectel L76K specification; the
  `$PCAS11` stationary value is from the millerjs ATGM336H wiki. All are listed
  under References below and cited inline in `GpsCasic.h`.
- Hardware verification is pending for every Phase 2 item that touches the
  receiver: autobaud lock and no-receiver drop, ephemeris replay effect on TTFF,
  the `dyModel`/`$PCAS11` platform change, and the MON-HW decode. Host tests and
  the five release plus `m5stick-s3-debug` firmware builds pass. On-device
  verification uses the console script in each PR section below.

## Phase 2 behaviour, as implemented after the rebase onto master

This is the behaviour list read out of the diff end to end, not from the plan
text above. Each item names where it is exercised. Wire id 42 was double-owned
(issue #280); the setting renumbered to 69 at rebase and the golden corpus was
regenerated, and the reservation table now lives in `include/CLAUDE.md`.

### Autobaud and the no-receiver state

- `GPS_BAUD` gains the sentinel 0 for Auto. The UI switch became a three way
  roller, `Auto / 9600 / 115200`, and the console accepts `auto`, `0`, `9600`
  and `115200`. The stored default is still 9600, so an existing install is
  unchanged.
- On enable with Auto, the driver programs `LADDER[0]` (115200), publishes
  `DETECTING`, and arms the ladder for the GPS task. With a fixed baud it
  publishes `PRESENT` immediately and configures as before.
- The ladder runs on the GPS task ahead of the power cycle. While detecting or
  absent, `serviceCycle`, `serviceConfig` and `servicePoll` are all held off, so
  no configuration command can be sent at a rate no receiver is listening on.
  Serial is still fed while detecting so the sentence counter can climb.
- A step is `Casic::Autobaud::STEP_MS` (1200 ms) and locks on two sentences
  passing checksum in one step. Two rather than one guards against a garbled
  stream passing by chance at the wrong rate.
- On lock: publish the baud, reset acquisition, arm configuration, publish
  `PRESENT` last, then take the power lock.
- On failure after all six steps: publish baud 0, cancel configuration, force
  the cycle to `DISABLED`, drop the 5 V rail, release the power lock, arm one
  retry 60 s out, publish `ABSENT`.
- The retry starts the ladder inline on the GPS task. It used to defer through
  `m_ProbePending`, which cost a whole idle interval before the first step ran.
- `cycleWait` sleeps until the retry is due, then 60 s at a time once the single
  retry has been spent. Without that the task woke ten times a second forever on
  a board with no receiver, which is the same drain plan 98b removed for the
  degraded state.
- Surfaces: `gps` status prints `receiver` and `detected_baud`; the GPS Data
  page cycle row reports `detecting` or `absent` in place of the power cycle
  state, because an absent receiver otherwise reads as `disabled`, which is what
  a switched off GPS reads as; the Raw NMEA page counters row gained a
  `<state> <baud>` line.

### CASIC platform and MON-HW

- `GPS_PLATFORM` 1 to 4 maps to `dyModel` 0 to 3 and is applied through the same
  `CFG-NAVX` query-modify-write as the constellation mask, so the 44 byte struct
  is read, edited and written once rather than built from zeros. The mask now
  carries both bits when both settings are set.
- The NMEA fallback can now carry two sentences (`$PCAS04` and `$PCAS11`),
  newline separated, and `finishConfigCommand` sends each line separately.
- `gps monhw` polls MON-HW (class 0x0A id 0x09) with a `CFG-MSG` rate 0xFFFF
  poll. The response is decoded best effort and the raw bytes are printed. The
  56 byte layout is unconfirmed against a live unit, so both the console and the
  docs mark it hardware-tuning-pending.
- `Casic::Autobaud::baud()` returns `LADDER[0]` after `NO_RECEIVER` rather than
  a failure value. Only `onProbeLocked` reads it, so the firmware is correct
  today, but a future caller that reads it without checking `state()` would
  silently configure 115200. Left as is and recorded here.

### Ephemeris store and replay

- Tier 2 polls `MSG-GPSEPH`, `MSG-GPSION` and `MSG-GPSUTC` once a fix is stable
  and no configuration command is outstanding. The capture window is
  `EPH_POLL_MS` (1500 ms).
- **NVS budget and wear.** The store is one `gps_eph` blob, capped by
  `EphemerisCollector::MAX_BYTES` at 4096 bytes; the modelled receiver produces
  182 bytes for three frames and a real one is expected near 2.6 kB. The write
  happens once per session on the first stable fix and then at most once an
  hour (`EPH_CACHE_WRITE_MS`), and only when `GPS_ASSIST` is 2, which is not the
  default. A device left on with assist 2 therefore writes about 24 blobs a day.
  That is well inside the NVS wear budget, but it is the only periodic NVS write
  this PR adds and it is worth naming: nothing else in the GPS path writes NVS
  on a timer. The write runs on the GPS task and holds no mutex.
- Replay is armed at enable, before `configure()`, so a sentence prune cannot
  remove messages the assistance path relies on. Frames go out one per
  `servicePoll` pass, `EPH_REPLAY_GAP_MS` (30 ms) apart, so a full cache does
  not choke the receiver's navigation loop.
- **Freshness, and which clock can answer it.** The cache is refused when it is
  older than four hours. Three attempts at bounding that age were wrong before
  this one, and the first two failed for the same underlying reason: neither
  clock furble has of its own knows how long the board was off.
  - `time(nullptr)`, which the branch shipped, is the Unix epoch on a board with
    no RTC and no network, so `wall >= capture_utc` is false and every cache is
    refused. Tier 2 was dead on exactly the device it exists for.
  - `TimeKeeper` is no better. It restores as the last persisted epoch plus the
    monotonic time since boot, with a fixed one hour uncertainty and no
    knowledge of the off time, so a week unplugged reads as about two minutes
    old. It reports fresh for a cache that expired six days ago.
  - Deciding against "the receiver reported a different UTC than at arm time"
    was wrong in a subtler way, and the review caught it. That folds the date
    and the time into one number. A receiver with RMC pruned reports a ticking
    time against a date the parser kept from the last session, so the folded
    value changes every second, the commit fires, and the age is decided against
    a stale date. It passed its own scenario only because that fixture's clock
    was frozen; ticking it turned the leg into a replay of three frames.

  The receiver is the only clock that knows, and only RMC carries a date, so the
  signal is an RMC arriving after the arm. `processNmea` counts date-bearing
  sentences off the raw bytes, and the commit waits for that count to move.
  Neither the date value nor the time can stand in for it: the date does not
  change from one second to the next inside a day, and the time ticks whether or
  not a date ever arrives. The decision itself is the pure
  `Casic::Eph::freshness`, host tested over eight cases.

  A sentence split across two UART reads is missed by the counter, which costs
  one burst of delay and never a false count.

  The trade-off is explicit: a receiver that never sends RMC never gets a
  replay. That is the true cold start with the rail cut, where the cache age
  cannot be established at all and replaying blind is what this prevents. Warm
  and hot starts, which is a device that has been in a bag for an hour, are
  unaffected.

  Note for anyone touching this: TinyGPSPlus ages come from `millis()`, which is
  the same clock as `Platform::tick()` on the device but **not** in the
  simulator. Comparing an age against a `Platform::tick()` value silently makes
  every stale timestamp look fresh, which is how an even earlier version of this
  check passed its own scenario. Compare values and counts, never ages.

- A cache whose frame count, byte count, magic, version or per frame checksums
  do not agree is dropped whole rather than partially replayed.

### Satellite page (GSV/GSA)

- A direct parser, not `TinyGPSCustom`. Multi-sentence GSV sets are reassembled
  and only published when every sentence of the set has arrived, so a partial
  set never renders.
- Capture runs only while the page is open or `gps sats on` is set. Opening
  sends `$PCAS03,1,0,1,1,1,0,0,0` to un-prune GSV and GSA; closing restores the
  user's set. The extra traffic never outlives the page.
- Range checks: PRN above 65535, elevation above 90, azimuth above 359 and C/N0
  above 99 mark the set malformed, and a malformed set is refused whole rather
  than published half parsed.
- A PRN repeated inside one GSV set is now published once. It used to be
  published twice, which made the in-view count disagree with the count the GSV
  header itself declared.
- A GSA replaces the solution for its system, and a GSA whose DOP fields do not
  parse now clears the DOP rather than leaving the previous figures published as
  valid indefinitely.
- The page is an intentional-scroll page, like Raw NMEA and GPS Data.

### Malformed input hardening

- Every NMEA sentence is checksum validated before the satellite parser sees
  it, including a trailing-data check after the checksum.
- Every CASIC frame is validated for sync bytes, a payload length that is a
  multiple of four and inside the 2048 cap, a length that agrees with the
  buffer, and the id-first checksum, before it is stored or replayed.
- Null pointers and zero lengths are refused by `EphemerisCollector::feed`,
  `splitFrames` and `parseMonHw`.

### Parser ownership

`include/FurbleGPS.h` says it: TinyGPSPlus accessors clear update flags, so they
write, and every read goes through the locked `getStatusSnapshot()`. Two places
in this branch broke that rule and both raced the UI task's own snapshot.
`servicePoll()` called `m_GPS.location.FixQuality()` directly, and
`storeEphemeris()` read `m_GPS.date` and `m_GPS.time`. Both now read the
snapshot, which needs no new lock because the snapshot already takes the one
that exists. The new freshness decision reads the same snapshot.

The rule is absolute, so the five remaining unlocked reads went with them:
three `passedChecksum()` reads in the autobaud probe and two `charsProcessed()`
reads in the settle gate now take the snapshot as well. Nothing outside
`processNmea`'s locked `encode` touches `m_GPS` directly any more.

Both ephemeris scenarios hold the GPS Data page open for their whole run, so
the UI task reads the snapshot while the GPS task polls, stores and decides, and
both were added to the sim-e2e ThreadSanitizer leg alongside
`gps-concurrent-pages`.

Measured, five runs per cell, on this head:

| Cell | `gps-ephemeris-stale` | `gps-ephemeris-replay` | `gps-concurrent-pages` |
| :--- | :--- | :--- | :--- |
| as committed | 0/5 fail | 0/5 fail | 0/5 fail |
| `servicePoll` read unlocked | **4/5 fail** | 1/5 fail | 0/5 fail |
| `storeEphemeris` read unlocked | 0/5 fail | 0/5 fail | 0/5 fail |

So the TSAN leg **is** a gate for the `servicePoll` half, through
`gps-ephemeris-stale`, and a weak one through `gps-ephemeris-replay`. An earlier
revision of this plan claimed no leg caught it and used that to argue the leg was
not a gate. That was wrong, it was measured with one run rather than five, and it
would have licensed deleting a real gate. The reviewer measured 5/5 and 3/5 on
the same cells; either way the signal is strong and `halt_on_error` means one
detection fails the job.

The `storeEphemeris` half is caught by nothing, here or in the reviewer's run.
It is guarded by the rule the header states and by review, and that is the whole
of its assurance. Do not read the green leg as covering it.

### Degraded-state concurrency

- The detection state and detected baud are `std::atomic`, written only on the
  GPS task and read from the UI and the console.
- `onProbeLocked` takes `m_CycleMutex` only around the acquisition reset and
  publishes `PRESENT` after releasing it, so a reader never sees `PRESENT` with
  a stale cycle.
- `resetAcquisition` now reads the cached `m_RateMs` instead of calling
  `gpsRateInterval()`, which reads NVS. It is called under `m_CycleMutex` from
  both `enable()` and `onProbeLocked`, and master's whole reason for caching the
  rate was to keep NVS off the status paths.
- No mutex is held across a delay anywhere in the new code. `setSatelliteCapture`
  is called from the LVGL event callback and writes a `$PCAS03` sentence, which
  blocks the UI task for `TX_MS` under `m_TxMutex`; that is the same cost the
  existing `Raw NMEA` and restart buttons already pay.

### Also fixed during the rebase

- The branch had deleted `timeSourceName`, `cmdTime` and the `time` console
  command registration. Restored, with a regression guard in the host console
  suite.
- `settings set gps_baud abc` stored 0, that is Auto, because `BAUD_AUTO` is
  zero and the parse never checked `strtoul`'s end pointer. It now fails.
- `src/CLAUDE.md` had lost the `CompanionService::m_Mutex` ownership bullet.
  Restored.
- The `ui.page` identity array size and a dropped `<cstdio>` include were rebase
  casualties and would not have compiled.
- The unit status rows in `README.md` and the supported-hardware docs had been
  downgraded from "Supported" to "verification pending" for the whole AT6668
  family. Reverted: what is pending is this phase's autobaud, ephemeris and
  platform work, not the shipped receiver support.

## Simulator coverage and killing mutations

The simulator's fake UART is now a modelled receiver, not just a byte source. It
records the rate the driver programmed and answers only when its own modelled
rate matches, which is what makes the ladder and the absent state testable. The
default is a receiver that answers at any rate, so every scenario written before
this is byte-for-byte unchanged. It also answers a `CFG-MSG` rate 0xFFFF poll for
MON-HW and for the three assistance messages, counts the assistance frames
replayed back at it, and serves a selectable GSV/GSA fixture. The fixtures are
stored as sentence bodies and checksummed at runtime, so none of them can carry
a stale hand computed checksum.

Every assertion below was mutation checked: the named production change was
applied to the tree, the simulator was rebuilt, the scenario was run, and it
failed. All fifteen were killed. Three needed the coverage widening before they
were. A corrupted cache is now corrupted in its last frame rather than its
first, so a reload that stopped at the bad frame instead of refusing the whole
blob is distinguishable from one that refused it. A `partial` GSV fixture that
never sends its second sentence was added. And the fake receiver can now answer
a MON-HW poll with a truncated payload.

| Scenario | Asserts | Killing mutation |
| :--- | :--- | :--- |
| `e2e/gps-autobaud-ladder` | the ladder locks at each of the six rates and hands over to the fix path | drop one entry from `Casic::Autobaud::LADDER` |
| `e2e/gps-autobaud-ladder` | `uart.baud` matches the locked rate | do not call `uart_set_baudrate` on a ladder step |
| `e2e/gps-no-receiver` | six failed steps reach `absent` with baud 0 | make `Autobaud::service` stay `PROBING` past the last step |
| `e2e/gps-no-receiver` | the GPS Data cycle row reports `detecting` then `absent` | render `receiver.cycle_state` unconditionally |
| `e2e/gps-no-receiver` | `power.no_light_sleep` is 0 while absent | remove `releasePowerLock()` from `onProbeFailed` |
| `e2e/gps-no-receiver` | the single retry finds a receiver plugged in later | set `m_NoReceiverRetried` before the deadline check |
| `e2e/gps-ephemeris-replay` | a cache is written after a stable fix | return early from `storeEphemeris` |
| `e2e/gps-ephemeris-replay` | three frames are replayed after `restart` | skip `armEphemerisReplay()` in `serviceConfig` |
| `e2e/gps-ephemeris-invalid` | a corrupted cache is refused whole, not replayed up to the bad frame | ignore the `splitFrames` result in `loadEphemerisCache` |
| `e2e/gps-ephemeris-stale` | the four hour window against the receiver's own clock | make `Casic::Eph::freshness` always return `REPLAY` |
| `e2e/gps-ephemeris-stale` | a receiver clock behind the capture cannot bound an age | drop the backwards-clock guard from `freshness` |
| `e2e/gps-ephemeris-stale` | replay never commits without a date received this session | replace the RMC-count check with `if (false)` |
| `e2e/gps-ephemeris-stale` | the receiver's ticking time cannot stand in for its date | key the commit on `receiverUtc() > 0` instead of the RMC count |
| `e2e/gps-ephemeris-stale` (TSAN) | `servicePoll` reads the fix through the locked snapshot | read `m_GPS.location.FixQuality()` directly, 4/5 runs fail |
| `e2e/gps-satellite-fixtures` | 0, 1, 12, duplicate, partial, out of range and multi constellation populations | publish `set.building` without the complete-set check |
| `e2e/gps-satellite-fixtures` | a repeated PRN counts once | remove the in-set duplicate check |
| `e2e/gps-satellite-fixtures` | capture stops when the page closes | drop `setSatelliteCapture(false)` from `gpsSatStop` |
| `e2e/gps-malformed-satellites` | malformed NMEA, corrupt ACKs and every UART error event leave the table empty and the page alive | accept a sentence without validating its checksum |
| `e2e/gps-monhw-poll` | the MON-HW decode, and that a corrupt or truncated frame does not publish | drop the `length < 56` guard in `parseMonHw` |
| `bughunt/page-matrix` | `gps_baud`, `gps_platform` and `gps_sats` have page identity and reach both scroll ends | remove one from the `ui.page` identity array |

Host coverage is `tests/host/gps_casic_test.cpp` (381 checks, including a
deterministic mutation-checked fuzz loop over `NmeaSatellites`,
`EphemerisCollector`, `splitFrames` and `parseMonHw`) and
`tests/host/console_commands_test.cpp` (763 checks, including every new `gps`
subcommand, the four receiver states, and the `gps_baud` and `gps_plat`
settings).

### The documentation capture gate

The branch shipped a CI step that re-rendered the two GPS documentation
captures under xvfb and byte-compared them against the committed PNGs, twice,
for determinism. It was dropped for two reasons. It ran the docs script without
the optional capability environment variables the script's own header says the
harness sets, so `nav infrared` returned UNAVAILABLE and the step could never
pass. And the shape is wrong even when it runs: capture byte-equality only
holds for two runs of the same binary (sim/CLAUDE.md), so a
render-and-compare against a committed PNG is a tripwire that fires on any
unrelated LVGL, font or layout change and lands on whoever made it. No other
documentation image in the repository is gated that way. What is left is the
part that cannot rot and is easy to forget: the `docs/img` and `docs/wiki/img`
copies must match each other.

### Known coverage gaps

- The `storeEphemeris` parser read is not covered by any ThreadSanitizer leg,
  measured 0/5 with the lock removed. It rests on the header's ownership rule
  and on review. An earlier revision of this bullet claimed the whole replay
  commit condition was untestable; that was measured with one run instead of
  five and was wrong in this branch's favour. All four commit conditions are in
  the mutation table above.
- The satellite page, like the GPS Data page, carries no focusable control, so
  LVGL group navigation cannot scroll it with the physical buttons. Its content
  is unbounded where GPS Data's is not, so rows past the fold are unreachable on
  a Stick. `action scroll` reaches them, which is why the page-matrix assertions
  pass. This is a pre-existing pattern rather than something this PR introduces,
  and it needs a UI decision rather than a test.
- Everything that touches the physical receiver is unverified: the ladder
  against a real AT6668, the rail drop, the CFG-NAVX acknowledgement, the
  `$PCAS11` numbering, the MON-HW field offsets, and whether the ephemeris
  replay actually shortens time to first fix against the measured 108 s cold
  start.
- **How long after enable the AT6668 first sends a dated RMC, relative to its
  first fix.** The whole arm-and-commit two-step is built on that gap being
  small. If the unit only dates its RMC once it already has a fix, the replay
  lands after the fix it was meant to accelerate, and tier 2 is worthless in its
  current shape whatever the simulator says.
- **Whether the receiver itself rejects an expired ephemeris injection.** Every
  GNSS receiver carries `toe` and IODE and is supposed to validate before use.
  If the AT6668 does, then the four hour rule belongs to the receiver and not to
  furble, and the arm-and-commit two-step, `Casic::Eph::freshness`, the
  `gps-ephemeris-stale` scenario and the fixtures behind it can all be deleted
  in favour of replaying the cache unconditionally at arm. Measure it by
  capturing a cache, holding the unit unpowered past four hours, replaying, and
  watching whether time to first fix improves, degrades, or is unchanged against
  a no-replay control. This is the single measurement that would remove the most
  code in this plan, so take it early.

## Motivation

Three concrete failures today.

Setup is manual and the default is wrong. `Settings::GPS_BAUD` defaults to
`BAUD_9600` (`src/FurbleSettings.cpp:216-218`). The M5Stack Unit GPS v1.1 ships
at 115200 8N1. So a fresh NVS boot with the unit attached produces no fix at
all, and the only fix is for the user to find Settings, GPS, and flip a switch
labelled "GPS baud 115200" (`src/FurbleUI.cpp:1521-1546`). There is nothing on
screen that says why there is no fix. A user who does not know the module's
baud rate has no path to a working device.

Commands are fire and forget. `GPS::sendCommand` on feat/14 writes the sentence
and returns. If the receiver rejects it, ignores it, or the checksum is wrong,
nothing anywhere says so. PR14's own verification section is a list of things to
observe by eye.

Cold starts are slow and invisible. The Unit GPS v1.1 has no backup supply and
no V_BCKP pin, so every 5 V rail cut is a cold start. M5Stack quote 23 s cold
and 1 s hot for this unit. During those 23 s furble shows one small icon and
nothing else. There is no satellite count, no signal strength, no DOP, no way to
tell "indoors, no chance" from "30 more seconds".

## What is already planned

Read these first. This document does not repeat them.

| Doc | Covers | Status |
|---|---|---|
| `plans/14-gps-pcas.md` | `$PCAS02` rate, `$PCAS03` sentence pruning, `$PCAS04` constellation, NMEA checksum builder, `sendCommand`, raw NMEA ring buffer, `$PCAS10` restart button | Implemented on `feat/14-gps-pcas`, upstream PR #303 |
| `plans/15-gps-power.md` | Burst windowed light sleep, `UART_SCLK_XTAL`, `$PCAS12` standby, 5 V rail cycling | Planned |
| `plans/18-gps-motion.md` | IMU stationary detection selecting a PR15 power policy | Planned |
| `plans/21-imu-dead-reckoning.md` | Last known fix hold, timestamp advance, extrapolation | Planned |
| `plans/00-hardware-experiments.md` | Experiment B: V_BCKP presence and `$PCAS12` support | Experiment A done, B pending |
| `plans/27-usb-console.md` | USB console, `gps send` command | In progress on `feat/27-usb-console` |

---

## PR32a: receiver auto detection and autobaud

### Scope

In scope:

- A baud ladder probe that finds the receiver's actual rate.
- Module identification from the boot `$GPTXT` burst, with `$PCAS06` as a
  fallback query.
- `GPS Baud` changes from a two position switch to a roller with `Auto` as the
  default.
- A named "no receiver" state when the ladder finds nothing.

Out of scope:

- Changing the receiver's baud rate with `$PCAS01`. Considered and rejected
  below.
- Module GPS v2.1, still unsupported (`src/FurbleGPS.cpp:23`).
- Anything about what the receiver is told after it is found. That is PR32b onward.

### Files

| File | Anchor | Change |
|---|---|---|
| `include/FurbleGPS.h` | `:33-38` constants | Add the ladder table, probe timings, the detection state and the identity struct |
| `include/FurbleGPS.h` | `:40-43` private methods | Add `probe`, `identify`, `parseTxt` |
| `src/FurbleGPS.cpp` | `:19-58` `getInstance` | UART install stays. The initial baud comes from the probe, not from NVS |
| `src/FurbleGPS.cpp` | `:111-124` `enable` | Run the ladder before the feat/14 settle and configure step |
| `src/FurbleGPS.cpp` | `:126-133` `disable` | Clear the detection result |
| `src/FurbleGPS.cpp` | `:195-206` `serviceSerial` | Feed the probe's checksum counter |
| `src/FurbleUI.cpp` | `:1521-1546` baud switch | Replace the switch with a roller |
| `src/FurbleUI.cpp` | `:1548-1603` GPS Data timer | Add a detected baud line and an identity line |
| `include/FurbleSettings.h` | `:121-124` `GPS_BAUD` storage type | Keep `uint32_t`, add the sentinel value 0 for Auto |
| `src/FurbleSettings.cpp` | `:216-218` default | `BAUD_9600` becomes `BAUD_AUTO` |

### Settings

| Enum | NVS key | Type | Values | Default |
|---|---|---|---|---|
| `GPS_BAUD` | `gps_baud` (existing) | `uint32_t` | `0` auto, otherwise the literal baud rate | `0` |

`GPS_BAUD` already exists and is already a `uint32_t`, so 0 slots in as a
sentinel with no NVS migration and no new key. Existing installs keep whatever
they saved and the roller shows it. Only a fresh NVS boot gets Auto.

This is the one place in this document where the default changes. It has to.
The current default of 9600 does not work with the only GPS unit furble
supports, so "preserve current behaviour" would mean preserving a broken
default. Say this in the PR body.

### Implementation notes

Ladder order, most likely first:

| Step | Baud | Why |
|---|---|---|
| 1 | 115200 | M5Stack Unit GPS v1.1 factory default |
| 2 | 9600 | CASIC and L76K factory default, and furble's current default |
| 3 | 38400 | `$PCAS01` index 3 |
| 4 | 57600 | `$PCAS01` index 4 |
| 5 | 19200 | `$PCAS01` index 2 |
| 6 | 4800 | `$PCAS01` index 0 |

These six are the complete `$PCAS01` set. There is no seventh rate to try.

Detection criterion: one NMEA sentence with a passing checksum. Watch
`TinyGPSPlus::passedChecksum()` (`TinyGPS++.h:290`) across the step. Feeding the
live parser garbage at the wrong baud is safe: `endOfTermHandler` only commits
fields after the checksum matches (`TinyGPS++.cpp:195-227`), so a failed probe
increments `failedChecksum()` and nothing else. Reset both counters is not
possible through the public API, so record the value before each step and
compare deltas.

Timing: at the 1 Hz default a full burst arrives once per second. Allow 1200 ms
per step. Worst case the ladder is 7.2 s. Cap the whole thing at 10 s. The rail
already needs `SETTLE_MS` of 3000 after power on (feat/14 `include/FurbleGPS.h`),
so the ladder runs after that, not instead of it.

Where it runs: in the GPS task (`src/FurbleGPS.cpp:73-109`), not in `enable()`.
`enable()` is called from `reloadSetting()` which is called from an LVGL event
callback (`src/FurbleUI.cpp:733-748`). Blocking for 10 s there freezes the UI.
Follow the pattern feat/14 already established with `m_ConfigPending` and
`serviceConfig()`: `enable()` arms the probe, the task runs it.

Identification without a command. The CASIC specification says the product
information TXT block is "Output, output once at boot". So powering the rail and
listening is enough. The block looks like this, verbatim from the specification:

```
$GPTXT,01,01,02,MA=CASIC*27
$GPTXT,01,01,02,IC=ATGB03+ATGR201*71
$GPTXT,01,01,02,SW=URANUS2,V2.2.1.0*1D
$GPTXT,01,01,02,TB=2013-06-20,13:02:49*43
$GPTXT,01,01,02,MO=GB*77
$GPTXT,01,01,02,CI=00000000*7A
```

`MA` manufacturer, `IC` baseband and RF chip model, `SW` software name and
version, `TB` build timestamp, `MO` current working mode, `CI` customer number.
The unit under test should report an AT6668 part in `IC`. Record the real
strings in the PR body; they are the only firmware revision marker furble has.

`$PCAS06` is the fallback if the boot burst was missed, which happens whenever
GPS is enabled without a rail cycle. Format and values from the CASIC
specification section 1.6.7:

| Command | Query |
|---|---|
| `$PCAS06,0*1B` | firmware version, answers `SW=` |
| `$PCAS06,1*1A` | hardware model and serial number, answers `IC=` |
| `$PCAS06,2*19` | multimode working mode, answers `MO=` |
| `$PCAS06,3*18` | customer number, answers `CI=` |

Checksums above were computed with the same XOR rule feat/14 implements and
`$PCAS06,0*1B` matches the specification's own example exactly.

Parsing gotcha. The reply is a standard `$GPTXT` sentence, five comma separated
fields, and field 5 is free text that itself contains commas. `SW=URANUS2,V2.2.1.0`
splits into two terms. Do not use a term indexed parser for this. Take the whole
sentence from the feat/14 capture ring, find the fourth comma, and treat
everything up to the `*` as one string.

Fallback when nothing answers. After the ladder, set a `NO_RECEIVER` state.
Show `icon_location_disabled` and put "no receiver" on the GPS Data page. Drop
the 5 V rail so an absent unit does not hold power on. Do not loop. Retry only
when the user toggles the GPS setting, or once after 60 s, then stop. A unit
that is genuinely absent must not cost anything.

Override path. If `GPS_BAUD` is non zero, skip the ladder and use the stored
value directly. That is the escape hatch for a module the ladder cannot handle,
and it makes the change strictly additive for anyone who already has a working
setup.

### Risks

- A partially garbled stream at the wrong baud can produce a sentence that
  passes checksum by chance. Odds are 1 in 256 per sentence. Require two passing
  sentences in one step, not one.
- The ladder costs up to 10 s on every enable. Cache the result for the session
  and only re-run after a rail cut or a detection failure.
- Changing the default baud will change behaviour for a user who has never
  saved the setting. That is the point, but it must be in the release notes.
- Only one module is available for testing. The ladder is generic but the
  identity strings are one data point.

### Verification

Build matrix:

```
pio run -e m5stick-c -e m5stick-c-plus -e m5stack-core -e m5stack-core2 -e m5stick-s3
```

On the StickS3 with the unit on Port A, driven from the PR27 console:

1. Fresh NVS boot, unit attached. Enable GPS. Confirm the ladder locks at 115200
   with no user action and a fix follows. This is the headline test.
2. `gps status`. Confirm the detected baud and the six identity strings are
   reported.
3. Set `GPS_BAUD` to 9600 manually. Confirm the ladder is skipped and no fix
   arrives. Set it back to Auto. Confirm the fix returns.
4. Unplug the unit. Enable GPS. Confirm the ladder ends in `NO_RECEIVER` within
   the 10 s cap, the rail drops, and the console reports it. Confirm nothing
   loops or spins.
5. Send `$PCAS01,3*1F` by hand to move the module to 38400 without saving,
   power cycle furble only. Confirm the ladder finds 38400. Cut the rail.
   Confirm it returns to 115200 and the ladder follows.
6. `gps send PCAS06,0` and `gps send PCAS06,1` mid session. Confirm the `SW=`
   and `IC=` replies arrive and parse, including the comma inside `SW=`.
7. Fujifilm with GEOTAG. Confirm the first fix still lands inside the 30 s
   `MAX_AGE_MS` budget (`include/FurbleGPS.h:38`).

---

## PR32b: verified commands with CASIC binary ACK

### Scope

In scope:

- A CASIC binary framer, parser and checksum, verified against published frames.
- A binary demultiplexer on the receive path so binary frames and NMEA can share
  the UART.
- Replacement of the fire and forget `$PCAS` config path with acknowledged
  `CFG` class binary messages, one outstanding at a time, with retries.
- A command status list on the diagnostics page and on the console.

Out of scope:

- `AID` class messages. That is PR32d, which needs this framer.
- `CFG-CFG` save to flash. Still never sent, same reasoning as PR14's `$PCAS00`.
- `$PCAS20` online firmware upgrade. Rejected below.

### Findings that drive the design

`$PCAS` sentences are not acknowledged, and this is stated, not inferred. The
CASIC specification section 2.5 says:

> When the receiver receives a CFG type message, it needs to set whether the
> message processing is correct, and reply with an ACK-ACK or ACK-NACK message.
> Before the receiver replies to a received CFG message, the sender must not
> send a second CFG message. Other messages received by the receiver do not need
> to reply.

CFG class is 0x06 and only exists in the binary protocol. So there is no way to
verify an NMEA `$PCAS` command, and no amount of parsing will produce one. The
only path to verified configuration is the binary equivalents.

Binary framing, from the CASIC specification section 2.2 and the L76K
specification chapter 3.1:

```
0xBA 0xCE | len U2 | class U1 | id U1 | payload (len bytes, multiple of 4) | cksum U4
```

All fields little endian. `len` excludes header, class, id and checksum.

The checksum formula in the CASIC specification is wrong. It prints:

```
ckSum = (class << 24) + (id << 16) + len;
```

The L76K specification prints the operands the other way round:

```
Checksum = (ID << 24) + (Class << 16) + Len;
```

then adds each 4 byte payload word. The L76K version is correct. Verified two
ways. First, against every worked example in the L76K specification, including
`BA CE 04 00 05 01 06 00 00 00 0A 00 05 01`, which only balances with ID first.
Second, against 33 real messages pulled live from Espruino's CASIC assistance
mirror: 33 of 33 match with ID first and 0 of 33 match with class first.
Reproduce with:

```
curl -s https://www.espruino.com/agps/casic.base64 | base64 -d > casic.bin
```

then walk the frames and compare. Put the corrected formula in a comment next to
the implementation with a note that the primary specification is wrong, because
the next person will read the specification and reintroduce the bug.

The receiver really does acknowledge. An independent investigation on an
ATGM336H, the same module family as the unit, observed both `ACK-ACK` and
`ACK-NACK` frames coming back. So this is not a paper feature.

Binary output is probably already enabled. The L76K `CFG-PRT` query response
shows `ProtoMask` 0xFF on UART0, meaning binary input, text input, binary output
and text output are all on. Confirm on the unit with a `CFG-PRT` query before
assuming it.

### Files

| File | Anchor | Change |
|---|---|---|
| `include/FurbleGPS.h` | `:33-38` constants | Add the class and id constants, ACK timeout, retry count |
| `include/FurbleGPS.h` | `:40-43` private methods | Add `sendBinary`, `casicChecksum`, `serviceBinary`, `pending` state |
| `src/FurbleGPS.cpp` | `:73-109` `task` | Also call `serviceSerial` on `UART_DATA`, not only on `UART_PATTERN_DET` at `:99` |
| `src/FurbleGPS.cpp` | `:195-206` `serviceSerial` | Split the byte stream: binary frames to the new parser, everything else to `m_GPS.encode` |
| `src/FurbleGPS.cpp` | feat/14 `configure()` | Becomes a queued state machine, one outstanding CFG message |
| `src/FurbleUI.cpp` | `:1548-1603` GPS Data timer | Add a per command status list |

### Settings

None. This changes how existing settings are applied, not what they mean. Every
PR14 default of "do not send" still sends nothing, so a fresh NVS boot still
produces no transmit traffic at all.

One compile time or console-only escape: keep the NMEA `$PCAS` path as a
fallback if the ACK path fails three times. A receiver that ignores binary CFG
must not become a receiver furble cannot configure.

### Command mapping

| PR14 sentence | Binary equivalent | Class/ID | Payload |
|---|---|---|---|
| `$PCAS02,<ms>` | `CFG-RATE` | 0x06 0x04 | U2 interval ms, U2 reserved |
| `$PCAS03,...` | `CFG-MSG` | 0x06 0x01 | U1 ClsID 0x4E, U1 MsgID, U2 rate. One per sentence |
| `$PCAS04,<n>` | `CFG-NAVX` | 0x06 0x07 | mask bit B8, `navSystem` bits B0 GPS, B1 BDS, B2 GLONASS |
| `$PCAS10,<n>` | `CFG-RST` | 0x06 0x02 | U2 NavBbrMask, U1 ResetMode, U1 StartMode |
| `$PCAS01,<n>` | `CFG-PRT` | 0x06 0x00 | U1 PortID, U1 ProtoMask, U2 Mode, U4 BaudRate |
| `$PCAS00` | `CFG-CFG` | 0x06 0x05 | U2 mask, U1 mode. Never sent |

NMEA message ids for `CFG-MSG`: GGA 0x00, GLL 0x01, GSA 0x02, GSV 0x03,
RMC 0x04, VTG 0x05, ZDA 0x08, TXT 0x11, all under class 0x4E.

`CFG-MSG` rate 0xFFFF means "output once immediately", which turns any periodic
message into a poll. PR32d and PR32e both use this.

A worked `CFG-MSG` frame, third party, from an ATGM336H, enabling `NAV-DOP` at
one per fix:

```
BA CE 04 00 06 01 01 01 01 00 05 01 07 01
```

len 4, class 0x06, id 0x01, payload ClsID 0x01 MsgID 0x01 rate 0x0001, checksum
0x01070105. It balances under the ID first formula. Use it as the unit test
vector alongside the L76K examples.

### Implementation notes

Receive demux. `serviceSerial` currently hands the whole buffer to
`m_GPS.encode` (`src/FurbleGPS.cpp:203-205`). Binary frames would be fed to
TinyGPS++ as noise, which is harmless but wasteful, and the ACK would be lost.
Scan for `0xBA 0xCE`, read `len`, take `6 + len + 4` bytes as a frame, and pass
the rest through unchanged. Frames can straddle two reads, so keep a small
carry buffer, the same way feat/14 already keeps `m_Partial` for sentences.

Pattern detection is on `'\n'` (`src/FurbleGPS.cpp:46`). A binary ACK contains
no newline, so with today's task loop an ACK sits in the ring buffer until the
next NMEA sentence triggers an event. At 1 Hz that is up to a second of latency,
which is longer than a sensible ACK timeout. Handle `UART_DATA` as well as
`UART_PATTERN_DET` in `GPS::task` (`src/FurbleGPS.cpp:79-103`); the `UART_DATA`
case is currently an empty `break` at `:81-82`.

One outstanding command. The specification forbids sending a second CFG message
before the first is answered. Model the config path as a queue plus a single
in flight slot. On `ACK-ACK` for the matching class and id, pop and send the
next. On `ACK-NACK`, record the failure and move on rather than retrying the
same rejected message forever.

Retry policy: 3 attempts, 300 ms timeout each. The ACK is generated by the
receiver's command handler, not by the navigation loop, so it should be
immediate; 300 ms is generous. After 3 failures mark the command
`FAILED` and, once per config pass, fall back to the `$PCAS` sentence.

Surfacing. Keep a small fixed array of `{class, id, state, attempts}` where
state is one of `QUEUED`, `SENT`, `ACKED`, `NACKED`, `TIMEOUT`, `FALLBACK`.
Render it on the diagnostics page and dump it from the console with
`gps config`. This is the thing a user pastes into a bug report.

### Risks

- Highest complexity in this document for the least visible user benefit. If the
  ACK path proves unreliable on this firmware, ship the demux and the diagnostics
  and keep `$PCAS` as the transport. Say so rather than forcing it.
- A binary frame with a corrupt length can make the parser skip real data. Bound
  `len` at 2048, the specification's maximum payload, and resynchronise on the
  next `0xBA 0xCE` if the checksum fails.
- `CFG-NAVX` is 44 bytes and sets many fields at once. Always query first, edit
  only the masked fields, and write back. Never send a `CFG-NAVX` built from
  zeros.
- Binary output may be disabled on some firmware. Then furble sends `CFG-PRT`
  blind, which is the one unverifiable command in the set. Accept it and fall
  back after the timeout.

### Verification

1. Host side: run the checksum builder over the four L76K worked examples and
   the ATGM336H `NAV-DOP` frame. All five must match. Run it over the 33 frames
   in `casic.bin`. All 33 must match.
2. Console `gps binary 06 00` to query `CFG-PRT`. Record the `ProtoMask`. Confirm
   binary output is enabled.
3. Set the fix rate to 500 ms through the normal setting. Confirm one `CFG-RATE`
   goes out and one `ACK-ACK` with class 0x06 id 0x04 comes back, and confirm the
   sentence period actually changes.
4. Send a deliberately malformed `CFG-RATE` with an illegal interval. Confirm an
   `ACK-NACK` and that the status list shows `NACKED`.
5. Pull the unit's TX wire mid command. Confirm 3 timeouts, then `FALLBACK`, then
   the `$PCAS` sentence, then no further retries.
6. Set every PR14 setting in one pass. Confirm the queue never has two commands
   outstanding, by logging send and ACK timestamps.
7. Fresh NVS boot. Confirm zero bytes are transmitted, binary or NMEA.

---

## PR32c: dynamic platform model

### Scope

In scope:

- A `Platform` setting selecting the receiver's dynamic motion model.
- `CFG-NAVX` `dyModel` as the primary path, `$PCAS11` as the fallback.
- A hook so PR18 can drive the model from IMU stationary detection.

Out of scope:

- The stationary detector itself. That is PR18.
- The other `CFG-NAVX` fields. Rejected below.

### Findings

`$PCAS11` is not in either primary specification. It does not appear in the
CASIC protocol specification v3.6, which documents `$PCAS00` through `$PCAS06`,
`$PCAS10` and `$PCAS20` and nothing else. It does not appear in the Quectel
L76K specification, which documents `$PCAS01`, `02`, `03`, `04` and `10`. The
only attestation is third party: the millerjs ATGM336H wiki lists
`$PCAS11,1*1C` as "Stationary mode", and that checksum is arithmetically
correct, so the sentence is at least well formed.

The documented equivalent is `CFG-NAVX` (0x06 0x07), field `dyModel` at payload
offset 4, gated by mask bit B0. Full table from the CASIC specification section
2.11.8:

| Value | Mode | Use here |
|---|---|---|
| 0 | Portable | General purpose, the receiver's own default behaviour |
| 1 | Static | Tripod, copy stand, timelapse rig |
| 2 | Walking | Handheld, on foot |
| 3 | Car | In a vehicle |
| 4 | Nautical | Boat |
| 5 | Flight, acceleration under 1 g | Not offered |
| 6 | Flight, acceleration under 2 g | Not offered |
| 7 | Flight, acceleration under 4 g | Not offered |

Whether `$PCAS11` takes the same numbering is unverified. The one attested pair,
`$PCAS11,1` for stationary, matches `dyModel` 1 for static, which is suggestive
but is one data point. Treat the `$PCAS11` fallback as provisional and say so in
the menu help text and the PR body.

### Files

| File | Anchor | Change |
|---|---|---|
| `include/FurbleSettings.h` | `:16-29` `type_t` | Add `GPS_PLATFORM` |
| `include/FurbleSettings.h` | `:101-148` `storage_type` | `uint8_t` |
| `src/FurbleSettings.cpp` | `:11-24` table | One row |
| `src/FurbleSettings.cpp` | `:186-227` defaults | Joins the `uint8_t` group near `:190-198` |
| `src/FurbleGPS.cpp` | feat/14 `configure()` | Add the platform step to the config queue |
| `src/FurbleGPS.cpp` | `:136-143` `reloadSetting` | Load `GPS_PLATFORM` |
| `include/FurbleGPS.h` | `:40-43` | Add `setPlatform(uint8_t)` public, for PR18 |
| `src/FurbleUI.cpp` | `:1514-1604` `addGPSMenu` | One roller |
| `src/FurbleUI.cpp` | `:53-76` `m_Menu` grid | Register the page name |
| `include/FurbleUI.h` | `:67-75` `status_t` | Pointer for the new menu object, next to `gpsBaud` |

### Settings

| Enum | NVS key | Type | Values | Default |
|---|---|---|---|---|
| `GPS_PLATFORM` | `gps_plat` (8 chars) | `uint8_t` | 0 do not send, 1 Portable, 2 Stationary, 3 Pedestrian, 4 Vehicle | 0 |

Name string: `"Platform"`. Roller index 0 is "do not send", which sends nothing
and preserves current behaviour exactly. The roller index is deliberately not
the wire value; map it in code so the wire values for `dyModel` and `$PCAS11`
can differ if the device proves they do.

Hidden when GPS is off, using the existing show and hide list at
`src/FurbleUI.cpp:733-748`.

### Implementation notes

Send order: `CFG-NAVX` query, then a masked write with only B0 set and only
`dyModel` changed, then wait for the ACK. On `ACK-NACK` or timeout, fall back to
`$PCAS11,<n>` and mark the setting unverified on the diagnostics page.

Why stationary matters for photography. The navigation filter constrains the
solution to zero velocity, which suppresses the position random walk you get
from a receiver that assumes it might be moving. On a tripod for a long
exposure or a timelapse this should tighten the scatter of the reported
position, which is what ends up in EXIF. There is no vendor accuracy figure for
this on the AT6668. Do not claim a number. Measure it: log the fix for 10
minutes stationary in portable mode and 10 minutes in static mode and report
the standard deviation of both.

Interaction with PR18. PR18 detects stationary from the IMU and then picks
between the PR15 power policies: standby, rail cycling or a lower fix rate. This
PR gives PR18 a fourth and much cheaper option. Switching `dyModel` to static
costs nothing in fix freshness, does not risk the 30 s `MAX_AGE_MS` budget, and
does not need a wake path when a camera asks for geodata. When a camera is
connected, PR18 should prefer this over standby. Add that to PR18 when both
land.

Entry and exit asymmetry, same rule as PR18. Enter static slowly, 60 s of
continuous stationary. Leave static on the first motion sample. A receiver left
in static mode while moving will lag or freeze the position, and that lands in a
photo.

Manual override wins. If the user has picked a platform explicitly, PR18 must
not change it. Only the "do not send" and "Stationary" selections are
automatable.

### Risks

- `$PCAS11` numbering is unverified beyond one value. If `CFG-NAVX` works, the
  fallback is never exercised and stays untested. Exercise it deliberately in
  the verification steps.
- Static mode on a device that is actually moving produces a confidently wrong
  position. The default is off and the automatic path belongs to PR18's tested
  detector.
- `CFG-NAVX` writes 44 bytes. A read modify write bug here can silently change
  `minCNO`, `minElev` or the DOP limits and degrade every fix. Always query
  first, and dump the full struct to the console before and after.
- Vendor firmware may ignore `dyModel` entirely. Then this is a no-op, which is
  safe but pointless. Report it honestly if so.

### Verification

1. Fresh NVS boot. `GPS_PLATFORM` 0. Confirm no platform command is sent.
2. Set Stationary. Confirm one `CFG-NAVX` query, one masked write, one
   `ACK-ACK`. Query again and confirm `dyModel` reads back as 1 and that
   `minCNO`, `minElev`, `pDop` and `staticHoldTh` are unchanged.
3. Force the fallback by blocking the binary path. Confirm `$PCAS11,1*1C` goes
   out and the diagnostics page marks it unverified.
4. Scatter test. Tripod, clear sky, 10 minutes in Portable and 10 minutes in
   Stationary, logging lat and lon every second over the console. Report both
   standard deviations. If Stationary is not better, say so and ship the setting
   anyway as a documented no-op, or drop it.
5. Set Stationary, then walk 200 m. Confirm the position lags, then set
   Pedestrian and confirm it recovers. This is the failure mode users need
   warned about.
6. Fujifilm with GEOTAG, tripod, Stationary, 30 minutes. Confirm frames are
   still tagged and the positions agree with the phone reference.

---

## PR32d: assisted fast fix

### Scope

Three tiers, in increasing dependency and increasing payoff.

- Tier 1, `AID-INI` from NVS. Inject the last known position and an estimate of
  the current time on every GPS enable. No network, no phone, no companion app.
- Tier 2, ephemeris cache. Poll the ephemeris out of the receiver while it has a
  fix, keep it in NVS, and play it back on the next enable together with the
  tier 1 `AID-INI`. Still no external dependency. This is the tier that
  actually moves the number.
- Tier 3, phone assisted. A companion app supplies position and time. Optional
  opaque ephemeris passthrough if a source can be found.

Out of scope:

- The companion app and its GATT service. That is `plans/50-companion-app-design.md`.
- Setting the system clock for any purpose other than building `AID-INI`.

### Findings

`AID-INI` is class 0x0B id 0x01, payload 56 bytes, fully documented in the
CASIC specification section 2.15.1:

| Offset | Type | Name | Unit | Notes |
|---|---|---|---|---|
| 0 | R8 | ecefXOrLat | m or degrees | latitude when the LLA flag is set |
| 8 | R8 | ecefYOrLon | m or degrees | longitude |
| 16 | R8 | ecefZOrAlt | m | altitude |
| 24 | R8 | tow | s | GPS time of week |
| 32 | R4 | freqBias | m/s or ppm | clock frequency drift |
| 36 | R4 | pAcc | m | 3D position accuracy estimate |
| 40 | R4 | tAcc | s | time accuracy estimate |
| 44 | R4 | fAcc | m/s or ppm | drift accuracy estimate |
| 48 | U4 | res | | reserved |
| 52 | U2 | wn | | GPS week number |
| 54 | U1 | timeSource | | time source |
| 55 | U1 | flags | | see below |

Flag bits: B0 position valid, B1 time valid, B2 clock drift valid, B3 reserved,
B4 clock frequency valid, B5 position is in latitude, longitude, altitude form,
B6 altitude invalid, B7 reserved.

So for tier 1 the flags byte is B0 | B1 | B5 = 0x23, or 0x63 if the cached
altitude is not trusted. Frame header is `BA CE 38 00 0B 01`, since 56 is 0x0038.
When the time cannot be bounded, clear B1 and send 0x21 or 0x61 so the receiver
uses the cached position but ignores tow and wn. See the stale-time guard in the
phase 1 status above.

`AID` is class 0x0B, not class 0x06, so by the specification's own rule it is
not acknowledged. In practice the ATGM336H investigation saw ACK and NACK
frames for class 0x08 message injection as well, so this firmware appears to be
looser than the specification. Do not depend on either behaviour. Verify the
effect by measuring the fix, not by counting ACKs.

The honest TTFF numbers, from the vendors:

| Source | Cold | Warm | Hot |
|---|---|---|---|
| M5Stack Unit GPS v1.1, AT6668 | 23 s | not stated | 1 s |
| M5Stack Mini GPS Unit U032, AT6558 | 35 s | 32 s | 1 s |
| AT6558 chip datasheet | 32 s | not stated | 1 s |

Read the middle row carefully. Warm start saves 3 s out of 35. Warm start is
exactly what tier 1 buys: position and time known, ephemeris not. So on this
chip family, injecting position and time alone is worth roughly 10 percent, not
the order of magnitude the phrase "assisted GPS" suggests. The reason is that
cold start on a 50 channel receiver is dominated by downloading the ephemeris
off the satellites at 50 bits per second, and knowing where you are does not
speed that up. Say this in the PR body. Tier 1 is worth shipping because it is
cheap, because it is the framing work tier 2 and tier 3 need, and because
knowing which satellites are overhead helps under a weak sky. It is not worth
shipping on a TTFF claim.

Tier 2 is where the win is. The receiver publishes its own ephemeris:
`MSG-GPSEPH` 0x08 0x07, 72 byte payload, one per satellite; `MSG-GPSION`
0x08 0x06, 16 bytes; `MSG-GPSUTC` 0x08 0x05, 20 bytes. All three are listed as
periodic output messages in the CASIC specification, and `CFG-MSG` with rate
0xFFFF polls any periodic message once. So furble can read the ephemeris out
while it has a fix and write it back later. GPS ephemeris is valid for roughly
four hours, so the cache is useful across a lens change, a battery swap or a
drive to the location, which is exactly the furble use case. Size is small: 32
satellites at 72 bytes of payload plus 10 bytes of framing is about 2.6 kB,
which fits an NVS blob.

This matters more than it looks because of `plans/15-gps-power.md` and
Experiment B. The Unit GPS v1.1 has no backup battery and no V_BCKP pin, and
the AT6558 datasheet is explicit that without VDD_BK the RTC and backup RAM stop
and "the hot start function will fail". If Experiment B confirms that, every 5 V
rail cut is a cold start, and PR15's rail cycling policy is dead on arrival
because a 23 s re-fix costs more than the idle draw it saves. Tier 2 is what
brings rail cycling back into play: cache the ephemeris in NVS, cut the rail
freely, replay on wake. That turns PR15's rejected policy into a usable one.
State this dependency explicitly in both documents.

Tier 3 and the missing service. CASIC publishes no assistance download service
and no documented ephemeris file format. Espruino, who ship this chip in the
Bangle.js 2, obtain their data by mirroring `api.smawatch.cn/epo/ble_epo_offline.bin`,
a URL belonging to an unrelated watch vendor, and re-serve it at
`https://www.espruino.com/agps/casic.base64`. I fetched and decoded that mirror.
It contains a plain text header

```
AGNSS data from CASIC.
DataLength: 2650.
Limitation: 104/1000.
```

followed by exactly 32 `MSG-GPSEPH` frames and 1 `MSG-GPSION` frame, all with
valid checksums under the corrected formula. So the container format is not
mysterious: it is the documented CASIC binary protocol. What is undocumented is
where legitimate, current data comes from. The `Limitation: 104/1000` line is a
daily quota on someone else's server.

Therefore tier 3 is scoped to position and time only. The companion app supplies
what a phone actually knows, which is a good position and an accurate time, and
that is enough to fill `AID-INI` properly instead of from a stale NVS cache.
An opaque ephemeris passthrough, where the app hands furble a byte blob and
furble streams it to the receiver without interpreting it, is a five line
addition and should be included, but furble must not ship a hardcoded URL to a
third party's server. Leave the source to the app and to the user.

### Files

| File | Anchor | Change |
|---|---|---|
| `include/FurbleGPS.h` | `:33-38` | Add the `AID-INI` struct, the cache struct, tier gating |
| `include/FurbleGPS.h` | `:40-43` | Add `sendAidIni`, `pollEphemeris`, `replayEphemeris` |
| `src/FurbleGPS.cpp` | `:111-124` `enable` | After detection and settle, before configure, inject assistance |
| `src/FurbleGPS.cpp` | `:161-192` `update` | On a live fix, refresh the cached position, time and tick |
| `src/FurbleGPS.cpp` | `:195-206` `serviceSerial` | Capture polled `MSG-GPS*` frames into the cache |
| `include/FurbleSettings.h` | `:16-29`, `:101-148` | Add `GPS_ASSIST` |
| `src/FurbleSettings.cpp` | `:11-24`, `:186-227` | Row and default |
| `src/FurbleUI.cpp` | `:1514-1604` | One roller |

### Settings

| Enum | NVS key | Type | Values | Default |
|---|---|---|---|---|
| `GPS_ASSIST` | `gps_assist` (10 chars) | `uint8_t` | 0 off, 1 position and time, 2 position, time and cached ephemeris | 0 |

Default 0 sends nothing, so a fresh NVS boot is unchanged. Tier 3 gets no key:
it is the same tier 1 and tier 2 machinery fed by a better source, gated by
whether a companion app is connected.

Two NVS blobs, both in `FURBLE_STR`: `gps_fix` holding latitude, longitude,
altitude, UTC and the capture tick; `gps_eph` holding the raw polled frames and
their capture time. Write `gps_fix` at most once per 10 minutes and `gps_eph` at
most once per hour, to keep NVS wear bounded.

### Implementation notes

Time is the hard part, because furble has no clock. There is no `M5.Rtc` use
anywhere in the tree, and ESP-IDF starts at the epoch. So:

- On every live fix, store the UTC from RMC together with `Platform::tick()`.
  Also call `settimeofday` so the rest of the session has a wall clock. That is
  a small, independently useful change.
- Within one power session, elapsed time is `tick()` delta and is exact. Set
  `tAcc` to a second or two.
- Across a reboot, elapsed time is unknown. The cached UTC is a lower bound.
  Send it with a large `tAcc`, for example 3600 s, and set B1 so the receiver
  knows the value is coarse. A coarse time is still much better than none for
  choosing which satellites to search.
- On a board with a battery backed RTC, read it and shrink `tAcc` accordingly.
  Gate that behind a board check, do not assume every target has one.

Week number and time of week. GPS epoch is 1980-01-06 00:00:00 UTC. GPS time
leads UTC by the leap second count, 18 s as of this writing. `tow` is seconds
since Sunday 00:00 GPS time, `wn` is the week count. The specification does not
say whether `wn` is the full week number or the value modulo 1024, and it is a
U2 so both fit. `CFG-NAVX` has a `wnRollOver` field, which suggests the receiver
resolves rollover itself and wants the full number. Send the full number, and if
the injection has no effect, retry modulo 1024 and record which one worked.

`pAcc`. Use the great circle distance the device could plausibly have moved,
not a small number. If the cached fix is an hour old, 50 km is a defensible
`pAcc`. An optimistic `pAcc` on a stale position is worse than no position,
because the receiver will trust it.

Ephemeris polling for tier 2. Once a fix is stable, send `CFG-MSG` with
ClsID 0x08, MsgID 0x07, rate 0xFFFF, then the same for 0x06 and 0x05. Collect
whatever frames come back over the next second. Store them verbatim, checksums
and all, so replay is a straight `uart_write_bytes` with no re-framing and no
chance of a checksum bug corrupting the payload. Stamp the blob with the fix
time and refuse to replay anything older than 4 hours.

Replay pacing. The ATGM336H investigation pushed 33 frames and got only 2
ACK-ACK, the rest NACK. Espruino see the same mixed result and describe it as
"some messages fail with NACK, but at least it does not choke". The likely cause
is flow control: 2.6 kB at 115200 with no pacing is 230 ms of solid traffic into
a receiver that is also running its navigation loop. Pace the replay, one frame
at a time with a short gap, and count the ACK and NACK results. If pacing lifts
the ACK rate, that is a finding worth reporting upstream to Espruino.

Ordering on enable: detect baud (PR32a), settle, inject `AID-INI`, replay
ephemeris, then apply configuration (PR14 and PR32b). Assistance before
configuration, because a `$PCAS03` prune could remove sentences the assistance
verification needs.

### Risks

- Tier 1's payoff is small by the vendor's own numbers. Measure before claiming.
  If the measured saving is under 3 s, ship tier 1 as plumbing and say plainly
  that the TTFF benefit is in tier 2.
- A stale cached position with an optimistic `pAcc` can make the fix slower than
  no assistance. This is the main way this feature backfires. Always widen
  `pAcc` with age, and drop the cache entirely past 24 hours.
- A wrong `wn` or `tow` sends the receiver looking for satellites that are not
  there. Same mitigation: honest `tAcc`, and drop the time if it cannot be
  bounded.
- NVS wear from writing the fix cache. Rate limit and use a coarse position, two
  decimal places is plenty for assistance.
- Ephemeris replay may simply not work on this firmware, as the third party
  attempt could not confirm. Design the measurement first: time to first fix
  with and without, ten runs each, same location, cold rail every time. If the
  distributions overlap, drop tier 2 and keep tier 1.
- No verification channel. The receiver emits no NMEA sentence describing its
  ephemeris state, so the only proof is the fix time.

### Verification

1. Host side: build an `AID-INI` for a known position and time and check the
   frame byte for byte against a hand computed reference, including the
   checksum.
2. Baseline, 10 runs. Rail cut, 60 s off, enable, log time to first fix.
   `GPS_ASSIST` 0. Expect around 23 s per the M5Stack figure. Record the spread,
   not just the mean.
3. Tier 1, 10 runs, same procedure, `GPS_ASSIST` 1, cache under 10 minutes old.
4. Tier 1 with a stale cache: leave the device off overnight, then one run.
   Confirm it is no worse than baseline.
5. Tier 2, 10 runs, `GPS_ASSIST` 2, ephemeris cache under 1 hour old. This is
   the number that decides whether the tier ships.
6. Tier 2 with a 5 hour old cache. Confirm the replay is refused and the run
   falls back to tier 1 behaviour.
7. Log every ACK and NACK during replay. Report the ratio, with and without
   pacing.
8. Deliberately corrupt the cached position by 500 km and run. Confirm the fix
   still arrives and record how much slower it is. This bounds the worst case.
9. Fujifilm with GEOTAG. Confirm the first frame after enable is tagged sooner
   than on master.

---

## PR32e: satellite and signal quality page

### Scope

In scope:

- A per satellite table: id, constellation, elevation, azimuth, C/N0, used in
  fix.
- PDOP, HDOP and VDOP from GSA, and fix type and quality.
- Temporary un-pruning of GSV and GSA while the page is open.
- A signal bar rendering, because a list of numbers does not answer "will this
  ever get a fix".

Out of scope:

- Logging any of it to SD. That is `plans/24-sd-gpx-logging.md`.
- `MON-HW` interference data. Deferred, see the inventory.

### Findings

The pinned TinyGPS++ fork parses GGA and RMC only. `endOfTermHandler` classifies
every other sentence as `GPS_SENTENCE_OTHER`
(`.pio/libdeps/m5stick-s3/TinyGPSPlus/src/TinyGPS++.cpp:237-243`). So GSV and
GSA are dropped today. `hdop` comes from GGA term 8 (`TinyGPS++.cpp:298-300`);
there is no PDOP or VDOP anywhere.

`TinyGPSCustom` is present in the fork at `TinyGPS++.h:231-257`, with `begin`,
`isValid`, `isUpdated`, `age` and `value`. It works, but it is the wrong tool
here for two reasons.

First, it matches the whole first term with `strcmp`, including the talker id
(`TinyGPS++.cpp:246-252`). GSV is emitted per constellation with its own talker,
`$GPGSV`, `$BDGSV`, `$GLGSV`, and the L76K specification says explicitly that
`GN` may not be used for GSV. So one `TinyGPSCustom` per talker per term.

Second, GSV carries up to four satellites per sentence across up to nine
sentences, so a `TinyGPSCustom` on a given term only ever holds the value from
the most recent sentence in the set. Reconstructing the table would need
4 talkers times 17 terms, 68 objects at roughly 48 bytes each including two
16 byte buffers, about 3.3 kB, and still would not solve the multi-sentence
problem.

So: parse GSV and GSA directly. feat/14 already added a whole-sentence capture
path with `captureSentences` and `m_Partial`, and those sentences have already
passed a checksum. Split on commas into a fixed table. Storage is 32 entries of
`{u8 id, u8 elev, u16 az, u8 snr, u8 flags}`, 192 bytes, versus 3.3 kB for the
`TinyGPSCustom` approach.

Field layouts, from the L76K specification chapter 2.2:

```
$<T>GSV,<TotalNumSen>,<SenNum>,<TotalNumSat>,<SatID>,<SatElev>,<SatAz>,<SatCN0>[,...],<SignalID>*<CS>
$<T>GSA,<Mode>,<FixMode>,<SatID> x12,<PDOP>,<HDOP>,<VDOP>,<SystemID>*<CS>
```

GSV: elevation 00 to 90 degrees, azimuth 000 to 359 degrees true, C/N0 00 to 99
dB-Hz and null when not tracking, up to four satellites per sentence, maximum 32
in view. GSA: `FixMode` 1 no fix, 2 is 2D, 3 is 3D; up to 12 satellite ids, and
those are the ones actually used in the solution, which is where the "used" flag
comes from. Satellite numbering from the L76K appendix: GPS system 1 ids 1 to 32,
GLONASS system 2 ids 65 to 88, BeiDou system 4 ids 1 to 63, QZSS system 5 ids
193 to 197.

### Files

| File | Anchor | Change |
|---|---|---|
| `include/FurbleGPS.h` | feat/14 capture section | Add the satellite table, the DOP struct and a GSV generation counter |
| `include/FurbleGPS.h` | `:40-43` | Add `parseGSV`, `parseGSA`, `getSatellites`, `getDOP` |
| `src/FurbleGPS.cpp` | feat/14 `captureSentences` | Route GSV and GSA into the parsers |
| `src/FurbleGPS.cpp` | feat/14 `configure()` | Add the temporary un-prune and its restore |
| `src/FurbleUI.cpp` | `:1548-1603` GPS Data timer | New sibling page, same open and close timer pattern |
| `src/FurbleUI.cpp` | `:1593-1601` `gpsData.button` callback and `:1606-1611` `gpsDataStop` | Copy the pattern for the new page |
| `src/FurbleUI.cpp` | `:53-76` `m_Menu` grid | Register the page name |
| `include/FurbleUI.h` | `:67-75` `status_t`, `:178-185` name strings | Add the object pointer and the string |

### Settings

None. The page is under Diagnostics and is entirely read only.

### Implementation notes

Un-pruning while open. PR14's `GPS_NMEA` prune sends `$PCAS03,1,0,0,0,1,0,0,0`,
which sets `nGSV` and `nGSA` to zero. With that setting on, this page would show
nothing. On page open send `$PCAS03,1,0,1,1,1,0,0,0*02`, which adds GSA and GSV
and leaves everything else as it was. On page close, re-run the normal
`configure()` so the user's setting is restored. Empty fields in `$PCAS03` keep
the previous configuration, so this is a targeted change, not a full rewrite.
Once PR32b lands, prefer two `CFG-MSG` writes with class 0x4E ids 0x02 and 0x03,
because those are acknowledged and the restore is verifiable.

The un-prune is why this page must not be left open by accident. It raises the
sentence load, which is the opposite of what PR14 and PR15 are for. Close it on
inactivity, the same way the existing GPS Data timer pauses at
`src/FurbleUI.cpp:1606-1611`.

GSV assembly. Track `TotalNumSen` and `SenNum` per talker. Build into a shadow
table and swap it in when `SenNum == TotalNumSen`. Without that, a page refresh
landing mid set shows a table that is a quarter empty and looks like signal
loss. Time out an incomplete set after two fix intervals and swap in what
arrived.

Used flag. GSA lists the satellites in the solution. Clear all used flags when a
GSA arrives, then set them from that sentence's ids. Note that with multiple
constellations there is one GSA per system, distinguished by `SystemID` in the
last field, so clear only that system's flags.

Rendering. Three parts on one page. A line with fix type, satellites used over
satellites in view, and PDOP, HDOP, VDOP. A bar per satellite, height from C/N0,
filled when used and outlined when only in view. A footer with `charsProcessed`,
`passedChecksum` and `failedChecksum`, which feat/14 already puts on the raw
NMEA page and which PR15 needs. Twenty to thirty bars fit across a StickS3
screen at 1 px per bar plus a gap; on the smaller StickC use a count and a
histogram of C/N0 bands instead of one bar per satellite.

C/N0 is the number that matters. Under 30 dB-Hz on most satellites means indoors
or blocked and no amount of waiting will help. Over 40 on four or more means a
fix is imminent. Put that interpretation in the PR body so the page is
actionable, not just informative.

`ANTENNA OK` and friends arrive as `$GPTXT,01,01,01,ANTENNA OK*35` and are free
if TXT output is on. Show the last one. The unit has a built in ceramic antenna
with no external path, so the value is low, but it is one line of code.

### Risks

- The parser runs in the GPS task on every sentence. At 5 Hz with all
  constellations that is a lot of sentences. Only parse while the page is open,
  gated by the same `m_Capture` flag feat/14 already has.
- Fixed 32 entry table can overflow with GPS plus BeiDou plus GLONASS all in
  view. Cap it, sort by C/N0, and show the count that did not fit.
- Leaving the page open raises power draw and undoes PR14's pruning. Auto close.
- The multi-sentence GSV set is easy to get subtly wrong and the failure looks
  like a hardware problem. Unit test the assembler on the seven example
  sentences in the L76K specification.
- Small screens. The bar view will not fit on the StickC. Design the fallback up
  front rather than discovering it at build time.

### Verification

1. Host side: feed the assembler the seven `$GPGSV` and `$BDGSV` examples and
   the `$GNGSA` example from the L76K specification. Confirm 12 GPS and 16
   BeiDou satellites, and PDOP 2.5, HDOP 2.0, VDOP 1.5.
2. On device outdoors. Confirm the count matches `gps.satellites.value()` from
   GGA for the used count, and that in view is higher.
3. Set PR14 Sentences to pruned. Open the page. Confirm GSV and GSA reappear and
   that closing the page restores the pruned set. Check with the raw NMEA page.
4. Cover the antenna. Confirm C/N0 falls, the used count drops, DOP rises, fix
   type goes to 1, and nothing crashes or shows stale bars.
5. Set the constellation to GPS only. Confirm only `$GPGSV` arrives and the
   BeiDou rows disappear.
6. Leave the page open 10 minutes. Confirm no memory growth and no drift in the
   checksum failure rate.
7. Build and eyeball on all five environments. The StickC fallback must be
   legible.

---

## Inventory: everything else the module documents

Checked the CASIC protocol specification, the L76K protocol specification, the
AT6558 datasheet and the M5Stack Unit GPS v1.1 page for anything useful and
unplanned. This is the complete residue.

| Item | Where documented | Verdict |
|---|---|---|
| 1PPS output, `CFG-TP` 0x06 0x03 | CASIC 2.11.4, full pulse interval, width, polarity, reference and source config | Not usable. The Unit GPS v1.1 Grove connector is GND, 5 V, UART_RX, UART_TX only. No PPS pin is brought out. Configuring a pulse that reaches nothing is pointless. Would need a hardware modification. |
| SBAS | AT6558 datasheet lists WAAS, EGNOS, GAGAN and MSAS reception; the CASIC numbering table assigns SBAS PRN 33 to 51 and 120 to 138 | Nothing to do. There is no `$PCAS` field and no `CFG-NAVX` bit for SBAS. `navSystem` has only B0 GPS, B1 BDS, B2 GLONASS. SBAS is either on or it is not, and furble cannot influence it. |
| QZSS | L76K note under `$PCAS04`: "The QZSS is enabled by default, but it does not support configuration" | Nothing to do, and this is a good thing. QZSS is free extra coverage over Asia and Oceania. |
| NMEA version, `$PCAS05,ver` | CASIC 1.6.6. 2 = NMEA 4.1 and above, 5 = default, China Transport dual mode, NMEA 2.3 and 4.0 compatible, 9 = single GPS NMEA 2.2 | Diagnostics command only, not a setting. Changing the NMEA version can change the talker ids, and the TinyGPS++ fork only recognises GGA and RMC under the talkers in `strchr("PNABL", term[1])` (`TinyGPS++.cpp:238-241`). A version change that produces something else silently kills every fix. Add it to the console for experiments, keep it out of the menu. |
| Antenna status TXT | CASIC 1.5.8 part 2, `ANTENNA OPEN`, `ANTENNA OK`, `ANTENNA SHORT` | Folded into PR32e. One line, already in the stream. |
| `MON-HW` 0x0A 0x09 | CASIC 2.14.2. Noise power per IF channel, AGC counts, antenna status, and eight jamming centre frequencies | Deferred, not rejected. Genuinely answers "why is my fix slow" when the answer is interference, which nothing else in this document can detect. 56 bytes, one poll. Worth a follow up once PR32b's binary path is proven. |
| `MON-VER` 0x0A 0x04 | CASIC 2.14.1, 32 byte software version and 32 byte hardware version strings | Redundant. The boot `$GPTXT` burst and `$PCAS06` give the same information over a path PR32a already needs. Rejected. |
| `CFG-NAVX` quality fields | CASIC 2.11.8: `minSVs`, `maxSVs`, `minCNO`, `minElev`, `iniFix3D`, `fixMode`, `pDop`, `tDop`, `pAcc`, `tAcc`, `staticHoldTh`, `drLimit` | Read only. Dump them on the diagnostics page so a bug report contains them. Do not expose as settings; there is no way for a user to reason about `minCNO` and every one of them can make fixes worse. PR32c writes exactly one masked field. |
| `NAV-DOP` 0x01 0x01 | CASIC 2.7.2. pDop, hDop, vDop, nDop, eDop, tDop as R4, plus runtime since power on | Alternative to GSA for PR32e. GSA is simpler, is already NMEA, and carries the used-satellite list PR32e needs anyway. Note the verified enabling frame in PR32b as a fallback if GSA proves unreliable. |
| `AID-HUI` 0x0B 0x03 | CASIC 2.15.2. 60 bytes of UTC parameters, leap seconds and Klobuchar ionospheric coefficients for GPS and BDS | Optional extra for PR32d tier 2. The Espruino assistance blob does not include it, so it is not required for a working cache, but it is pollable from the receiver the same way the ephemeris is. Add only if tier 2 measures well. |
| `$PCAS20` online upgrade | CASIC 1.6.9 | Rejected outright. Firmware update over the UART, with no documented image format and no recovery path if it fails. Bricking a user's GPS unit from a camera remote is not an acceptable failure mode. |
| `CFG-CFG` 0x06 0x05 and `$PCAS00` | CASIC 2.11.6 and 1.6.1 | Still never sent, as PR14 decided. Adding the binary form does not change the argument: it makes furble's settings outlive furble, survives a device factory reset, and wears the module flash. |
| AT6558 automatic low power mode and ON_OFF pin | AT6558 datasheet 3.2.4 | No access. The internal RTC-timer duty cycle mode is not reachable over either documented protocol, and the ON_OFF pin is not on the Grove connector. PR15's rail cycling is the only power control furble has. |
| `$PCAS12` timed standby | Not in the CASIC specification and not in the L76K specification | Already owned by PR15 and Experiment B. This document does not touch it. Note for PR15: a search of the CASIC v3.6 specification finds no `$PCAS12` at all, so the odds of support are lower than PR15 currently assumes. |

## Considered and rejected

| Item | Reason |
|---|---|
| Raising the module baud with `$PCAS01,5` after autobaud | Doubles the state machine, and the gain is nil because PR14 and PR15 reduce traffic rather than needing more of it. |
| `$PCAS00` and `CFG-CFG` save to flash | Makes furble's configuration outlive furble and survive a factory reset, and wears the module flash. |
| `$PCAS20` firmware upgrade | Undocumented image format, no recovery path, catastrophic failure mode. |
| `$PCAS05` NMEA version as a user setting | Can change talker ids and silently break all parsing in the pinned TinyGPS++ fork. Console only. |
| `TinyGPSCustom` for the satellite table | Roughly 3.3 kB of objects, per-talker duplication, and it still cannot reassemble a multi-sentence GSV set. A 192 byte direct parser is better on every axis. |
| `MON-VER` for module identification | The boot `$GPTXT` burst gives the same strings for free and needs no transmit path. |
| `NAV-STATUS`, `NAV-SOL`, `NAV-PV` binary navigation output | Duplicates what GGA and RMC already provide, and TinyGPS++ already parses those. |
| Configuring `CFG-TP` 1PPS | The pin is not on the Grove connector. |
| Exposing `minCNO`, `minElev` and the DOP limits as settings | Unreasonable to tune without a sky view analyser, and every value can make fixes worse. Read only. |
| A hardcoded assistance download URL in furble | The only known source is a third party watch vendor's server, reached through someone else's mirror, with a stated daily quota. Not furble's to consume. |
| Accelerometer aided ephemeris prediction | Not a thing. Rejected in `plans/21-imu-dead-reckoning.md` for position; there is no sensor path to orbital elements. |
| Setting the receiver's platform automatically from GPS speed rather than the IMU | Circular. The receiver's speed estimate is an output of the model being selected. PR18's IMU is an independent source. |

## Dependencies

```
14 ──> 32a ──> 32b ──> 32c
                 └───> 32d
14 ──────────────────> 32e
```

- PR14 is a hard prerequisite for all five. `sendCommand`, the checksum builder
  and the raw sentence capture path all come from it.
- PR32a is a hard prerequisite for PR32b onward in practice, because verifying a
  command is meaningless if furble is talking at the wrong baud.
- PR32b is a hard prerequisite for PR32c's primary path and for all of PR32d.
  PR32c has an NMEA fallback and could ship without it, degraded.
- PR32e depends only on PR14 and can be developed in parallel with PR32b.
- PR05 provides the Diagnostics submenu. Soft dependency for PR32b and PR32e.
  Without it both pages hang off Settings, GPS temporarily.
- PR27 is a soft dependency for all five. Every verification section here is
  written as a console script. Without the console they are stopwatch work.
- PR15 and Experiment B. Experiment B's V_BCKP result changes PR32d's value
  directly. No backup supply means every rail cut is a cold start and PR32d
  tier 2 becomes the only way to make PR15's rail cycling policy viable. Feed
  the tier 2 measurement back into PR15's default policy choice.
- PR18 gains a fourth stationary policy from PR32c, cheaper than the three it
  currently chooses between. Update PR18 when both land.
- PR21 is unaffected. Its fix cache and PR32d's assistance cache hold different
  things for different consumers and should stay separate.
- PR50 companion app is where PR32d tier 3 lands. This document specifies the
  receiver side only.

## References

Every link below was fetched and returned HTTP 200 on 2026-08-16.

- CASIC Multimode Satellite Navigation Receiver Protocol Specification, English
  translation, v3.6. Primary source for `$PCAS05`, `$PCAS06`, the `$GPTXT`
  product information block, the CASIC binary framing, the message class and id
  table, `CFG-NAVX` `dyModel`, `CFG-TP`, `CFG-CFG`, `MON-HW`, `MON-VER`,
  `AID-INI` and `AID-HUI`. The text layer does not extract through a browser
  fetch, but it does extract with a local PDF library, which is how the values
  above were read rather than inferred. Note the checksum formula in section 2.2
  is wrong; see PR32b:
  https://www.espruino.com/files/CASIC_en.pdf
- Quectel L76K GNSS Protocol Specification v1.1. AT6558 based, same protocol
  family. Primary source for the GSV and GSA field layouts, the GNSS numbering
  appendix, the default configuration table, the correct binary checksum
  formula, the ACK-ACK and ACK-NACK payloads, and worked `CFG-PRT`, `CFG-MSG`,
  `CFG-RST` and `CFG-RATE` frames:
  https://www.waveshare.net/w/upload/d/dd/Quectel_L76K_GNSS_Protocol_Specification_V1.1.pdf
- millerjs ATGM336H wiki. Only attestation for `$PCAS11,1*1C` as stationary
  mode, and the source of the verified `NAV-DOP` enabling frame:
  https://wiki.millerjs.org/atgm336h
- Espruino Bangle.js 2 technical notes. Working `$PCAS02`, `$PCAS03` and
  `$PCAS04` against an AT6558, the CASIC binary helpers, and an `AID-INI`
  implementation:
  https://www.espruino.com/Bangle.js2+Technical
- Espruino CASIC assistance mirror. Used to verify the binary checksum formula
  against 33 real frames and to determine the container format. Contains a text
  header, 32 `MSG-GPSEPH` frames and 1 `MSG-GPSION` frame:
  https://www.espruino.com/agps/casic.base64
- Espruino discussion 7026, sending A-GNSS to the Bangle 2 AT6558. Source for
  the `api.smawatch.cn` origin, the daily quota, the roughly four hour validity
  window and the mixed ACK and NACK behaviour during ephemeris upload:
  https://github.com/orgs/espruino/discussions/7026
- Espruino discussion 5298, Bangle.js GPS improvements. Background on why there
  is no official CASIC assistance service:
  https://github.com/orgs/espruino/discussions/5298
- wardriver_rev3 issue 175, assisted GPS research on the ATGM336H specifically.
  Independent confirmation that the module emits `ACK-ACK` and `ACK-NACK`, and
  the 2 of 33 acknowledgement rate that motivates PR32d's paced replay:
  https://github.com/JosephHewitt/wardriver_rev3/issues/175
- M5Stack Unit GPS v1.1 product page. ATGM336H-6N at AT6668, 115200 8N1 default,
  5 V at 31.64 mA, cold start 23 s, hot start 1 s, accuracy under 1.5 m CEP50,
  built in ceramic antenna, Grove pinout of GND, 5 V, UART_RX, UART_TX with no
  PPS and no V_BCKP:
  https://docs.m5stack.com/en/unit/Unit-GPS%20v1.1
- AT6558 chip datasheet, mirrored by M5Stack. Cold start TTFF under 32 s, hot
  start and recapture under 1 s, sensitivity figures, and section 3.2.3 stating
  that without VDD_BK the RTC and backup RAM stop and the hot start function
  fails:
  https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/datasheet/unit/AT6558_en.pdf
- M5Stack Mini GPS Unit U032 datasheet, AT6558. The only source found that
  quotes all three start times for this chip family: cold 35 s, warm 32 s,
  hot 1 s. This is the number that sets expectations for PR32d tier 1:
  https://mm.digikey.com/Volume0/opasdata/d220001/medias/docus/202/U032_Web.pdf
- The gkoh TinyGPSPlus fork pinned at `platformio.ini:19`. `TinyGPSCustom` is
  present; GGA and RMC are the only parsed sentences:
  https://github.com/gkoh/TinyGPSPlus
- TinyGPS++ documentation, for the custom field API and the statistics
  accessors:
  http://arduiniana.org/libraries/tinygpsplus/

Not cited: the Mouser mirror of the Unit GPS SMA U190 datasheet. The URL blocks
automated fetches and could not be verified.

## Draft issue

Open one issue covering the setup automation angle before any of the five pull
requests. Motivation only, no design.

> **GPS setup is manual, cold starts are slow, and there is no way to see why**
>
> With the GPS/BDS Unit v1.1 attached to a StickS3, a fresh install gets no fix
> at all until you find Settings, GPS and flip the "GPS baud 115200" switch,
> because the stored default is 9600 and the unit ships at 115200. Nothing on
> screen suggests that is the problem, so the only way to get there is already
> knowing the module's baud rate. Once it is talking, every enable is a cold
> start, which M5Stack rate at 23 seconds, because the unit has no backup supply
> and furble cuts its 5 V rail; furble already knows the last position and could
> hand it back to the receiver instead of starting from nothing. And while you
> wait, the only feedback is one icon, so there is no way to tell "indoors, this
> will never work" from "ten more seconds", when the receiver is already
> reporting per satellite signal strength that furble discards. Would automatic
> baud detection, an assisted start from the cached position, and a satellite
> signal page be welcome?

## Simulator verification, 2026-08-24

The host simulator now drives the production GPS UART parser and configuration
state machine through a deterministic fake receiver. Scenarios cover binary
ACK, NACK, timeout with three retries and PCAS fallback, malformed checksum
frames, byte-at-a-time partial frames, short UART writes, all UART error event
types, and hot, warm and cold `$PCAS10` restart commands. The GPS profiler state
query also covers standby to wake transitions without relying on host thread
timing. Remaining acceptance checks are physical UART framing, receiver ACK
behavior, real PCAS standby and restart semantics, cold-start timing, and GPS
fix recovery with the Unit GPS v1.1 attached.
