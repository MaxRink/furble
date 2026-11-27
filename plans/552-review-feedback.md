# PR 552 review feedback, copied, and the answers

The review below is the independent review of PR 552 (`feat/gnss-vendor-text`),
posted as a review comment on the pull request. It is copied verbatim except for
one thing: it names the plan as `plans/367`, and the plan was renumbered to
`plans/388-gnss-vendor-text.md` by the arbiter before the review landed, so the
seven references are updated here. Nothing else is changed, including the
findings that this branch answers rather than obeys.

The answers are in section A. Every numeric claim in them was produced by a
command in this worktree at the head named there, not by reading the code.

## A. The answers

Reviewed at `106e1814`; answered at the head of `feat/gnss-vendor-text` after
this file landed. "Fixed" means the branch changed. "Answered" means the finding
is real but the change is not the one the review proposed, with the reason.

### Blockers

**Blocker 1, `$CFGDYN` exists and the code said it does not. Fixed.**
`Unicore::dynamicModel()` now builds `CFGDYN,1,0,0` for Portable and
`CFGDYN,1,1,0` for Static (UC 3.3.20, Table 3-41, page 58), `Unicore::plan()`
queues it for platform 1 and 2, and platform 3 and 4 stay refused with a reason.
The three documents are corrected: `docs/protocols/gnss-unicore-cfg.md`,
`docs/supported-hardware.md:150` and `docs/wiki/Settings-Reference.md:164`; so is
section 5.1 and the Unicore table of `plans/388-gnss-vendor-text.md`. The
simulator's Unicore model has `$CFGDYN` with the two values and a `$FAIL,0` for a
mask without bit 0, `text.dyn` exposes what it kept, `gps-unicore.txt` asserts
`text.dyn 1` for Stationary on the wire, and `gps-text` and `gps-text-sim` pin
the two bodies and the two refusals. The review is right that this was the one
finding where the code contradicted a document it cited.

**Blocker 2, a refused cut left a Quectel receiver with its GNSS engine off.
Fixed.** `m_TextStopped` carries the state `textSaveQueueBuild()` creates when it
queues `PAIR003`, `railOffOrRestart()` calls `textPowerBackOn()` when the cut is
refused, and that sends `Quectel::powerBackOn()`, which is `$PAIR002` for every
part and `$PAIR004` hot start for an LC86G or an unread model (Q76 2.3.2 note
page 34, 2.3.4 page 34). The flag is cleared when the receiver answers the frame
or when the cut succeeds. The new scenario `gps-quectel-rail-refused.txt` seeds
`gps_rail_refused`, reaches the save, and asserts `text.power_ons 1`,
`text.gnss_off 0` and a fresh fix; it fails without the change. Plan 12.6 now
says what happens instead of "until the next enable", which was wrong, and
section 10.3 is rewritten.

One honesty note: I could not re-read Q76 2.3.2 note page 34 in this session. The
copy I can fetch truncates before page 34, so the LC86G exception rests on the
review's page-by-page reading, and plan 12.6 says so. The frame is gated on the
model string, so a part that answers `$PAIR002` anyway is not affected.

**Blocker 3, a late Unicore `$OK` acked the next command. Fixed.**
`GpsText::Settle` is the window and `GpsText::windowed()` answers which lines can
carry an ambiguous acknowledgement (a bare `$OK` or `$FAIL`, and a repeated `$CFG`
name; `$PDTINFO` and every Quectel line pass). `serviceConfig()` arms
`m_ConfigSettle` for `ACK_TIMEOUT_MS` after a Unicore `FAILED` or `TIMEOUT`,
`serviceText()` drops a windowed line while it is closed, and `startConfigCommand()`
already refuses to start a command until the same gate opens, so nothing starts
inside the window either. `gps-text` reproduces the review's transcript: command A
times out at 3 s, the gate is armed, B starts, A's `$OK` is dropped, B is still
waiting, and the same bytes acked B with no gate, which is asserted as the
contrast. The window is shared with the UBX ack settle, so a u-blox receiver
gains the same rule rather than a second one.

### Should fix

**4. `BUSY` was terminal. Fixed.** `service()` handles `BUSY` above `refused()`:
it re-arms the deadline, does not spend an attempt, and after `BUSY_MAX` (3)
ends as `FAILED` with `Verdict::BUSY`, so the retry the document promises is
taken and the reason is still reported when it runs out. Pinned in `gps-text`.

**5. `PENDING` had no cap. Fixed.** `PENDING_MAX` (5) bounds it; past that the
command ends as `Step::TIMEOUT` with the new `Verdict::STALLED`, which the driver
logs as "stayed in processing, giving up" rather than as an unanswered frame.
Pinned in `gps-text`, which drives six answers.

**6. A corrupt ack for another id aborted the command in flight. Fixed.**
`match()` now compares the PAIR001 packet id before the checksum verdict, so
`$PAIR001,062,...` with a corrupted byte is `NONE` whatever its checksum. The
corrupt ack for the command's own id is still `BAD_REPLY`. Pinned in `gps-text`
with both lines side by side.

**7. `PENDING` counted as a refusal. Fixed.** `serviceText()` counts `acks` for
`ACKED`, `naks` only for `refused(verdict)`, and neither for `PENDING`. The
`FAILED` path in `serviceConfig()` still counts a real refusal.

**8. `MAX_LINE` was not enforced on a terminated line. Fixed.** The splitter now
emits a `DROP` piece for a finished vendor line longer than `GpsText::MAX_LINE`,
terminator included, so `parse()` never sees more than the cap, and the header
and `docs/protocols/gnss-quectel-pair.md` describe exactly that, including what
happens to an unterminated runaway line. `gps-text` sends a 154 byte line and
asserts that no `VENDOR` piece comes out.

**9. No refusal reached any UI. Answered, with a real surface added.**
The review is right that the flags were console and log only, and right that the
UBX backend has the same shape. The per-command outcome was already on the Raw
NMEA page (`cfg <class>/<id> <state> x<attempts>`), which is a device screen, but
a nameless `NACKED` is not a refusal a user can act on. The page now appends one
`refused: <names>` line naming the settings the active receiver refused, using
the text status on a Quectel or Unicore part and the UBX status on a u-blox one,
and a text command's row names its operation instead of a bare byte.
`ui.diag.nmea.refusals` exposes the rendered line, and
`gps-unicore-refusals.txt` asserts it reads `platform,standby`. What is not done
is a refusal on the main screen: the GPS Data page holds two detail rows by
design on 135x240 and a third would sit under the floating indicator, so the
names live on the page a user opens when a setting seems ignored. That is a
deliberate limit, not a claim that the main screen shows it.

**10. `gps text` said "sent" when nothing was sent. Fixed.** `text_status_t` now
records the `GPS_PLATFORM` value a command actually carried, and the console
prints `platform: sent`, `refused` or `not requested` from it, and the same three
states for `standby`. The console harness pins `not requested` for an untouched
setting and `sent` for one that went out.

### Nits

**11. Page numbers one early in four places. Fixed.** Q76 2.3.1 result codes are
now page 33 (`GpsText.h`, `GpsQuectel.h`, `GpsText.cpp`, the simulator and the
protocol page), Q76 2.3.44 note 1 is page 68 and 2.3.45 note 2 is page 69
(`FurbleGPSText.cpp`, the protocol page, the plan), and Q29 2.4.13 note 3 is page
62 (`GpsQuectel.cpp`, `sim/vendor_gnss.h`).

**12. `PAIR080` comment numbering. Fixed.** The comment now reads 0 normal, 1
fitness, 2 to 4 reserved, 5 drone, 6 reserved, 7 swimming, and cites pages 50 and
51. No behaviour change.

**13. The CFGNAV range citation did not support the constant. Fixed.**
`NAV_RATE_MIN_MS` is now 100, the shortest interval `RATE_MS` asks for, with a
comment that says Table 3-19 prints examples and no range and that 50 is the
DRNavRate example. A 50 ms request is refused, and `gps-text` asserts the floor
and the refusal.

**14. `TEXT_SAVE_REPLY_MS` was dead. Fixed.** Deleted; the save session uses
`GpsText::Session::ACK_TIMEOUT_MS` and the whole budget is 1.5 s.

**15. Two modelled gaps had no end-to-end coverage. Fixed.**
`gps-unicore-refusals.txt` now seeds `gps_unicore_no_flash` and reaches a refused
`$CFGSAVE` (`gps.text.save refused`, `text.saves 0`), and
`gps-quectel-refusals.txt` seeds `gps_quectel_no_search_mode`, which turns
`PAIR066` into result 3 and is asserted as `gps.config.2.state NACKED` with
`text.unsupported` above zero.

**16. Nothing asserted the refusal flags. Fixed.** The simulator now has
`gps.text.*`: `configured`, `model`, `fw`, `platform`, `acks`, `naks`,
`timeouts`, `save`, `standby`, `last` and `refused`, the last as one comma
separated token. `gps-unicore-refusals.txt` asserts `gps.text.refused
platform,standby`, `gps-quectel-refusals.txt` asserts `gps.text.refused none`
beside two receiver refusals, and the UI line is asserted separately, so a
regression that stopped publishing a flag fails two scenarios.

**17. "six scenarios". Fixed** before the review landed, in the renumber commit:
the plan and its index row say eight now, which is the count after this round.

**18. The doubles share the parser and the framer. Answered in the header.**
The risk is real and is now written down where the next reader will look: the
doubles cannot falsify the framing or the checksum, and that half is pinned by
`gps-text` against fourteen literal vendor examples; what the doubles do own is
the receiver side. `sim/vendor_gnss.h` says which half is which. No code change,
because making the doubles independent would mean a second parser, which would
be a second thing to fix when a vendor adds a form.

**19. `textSaveBlocking` reported TIMEOUT for every refusal. Fixed.** The
session's verdict is captured before it is cleared and reported, so a
`$PAIR001,513,2` reads as `nacked` and only an unanswered frame reads as
`timeout`.

**20. One bare `$OK` could be accepted by both sessions. Fixed.** `serviceText()`
returns as soon as the configuration session accepts the line, so the save
session is only offered what the configuration session did not take, and the
comment above it is now true rather than aspirational.

### DRAM

The review's correction is right and the PR text is fixed: `m_TextSaveQueue` is a
`std::vector` on the heap and does not count against `.dram0`.

The size pass after that correction went further than the review could see: the
classic ESP32 boards were 108 bytes over their 55,296 byte budget, because the
text backend's fixed state was in `.bss`. It is now one heap allocation,
`GPS::text_state_t`, created by `enable()` before the UART task is woken and
freed by `disable()` after the save path, with every text entry point null-safe
and `getTextStatus()` answering an empty record while it is absent. That removed
232 bytes of `.bss` on every board. The S3 budget raise is reverted, since the
S3 now measures 47,608 against the original 48,128 with 520 bytes of headroom,
and `tools/size_budget.json` is unchanged by this PR. Measured with
`check_size_budget.py` on this head: `m5stick-c` 55,172/55,296, `m5stick-c-plus`
55,148/55,296, `m5stick-s3` 47,608/48,128. The in-flight command stays in `.bss`;
moving it would touch the UBX and CASIC paths for about 150 bytes and is not
needed to pass.

### The CI pass on `9fd20a79`: static analysis and the Core debug slot

Both findings are fixed at the source, with no suppression:

- `bugprone-unchecked-optional-access` in `GPS::serviceConfig`: the three fields
  the branches use (`op`, `dialect`, `settle_ms`) are read from
  `m_ConfigInFlight` immediately after the `has_value()` guard and before
  `text().session.service()`, whose opaque body is what made the checker
  distrust the optional. `startConfigCommand` reads `status_index` the same way
  and passes `m_ConfigInFlight.value()` to `sendConfigFrame()`, so no pointer
  into the optional survives a call.
- `cppcoreguidelines-owning-memory`: `ensureTextState()` allocates with
  `new (std::nothrow)` into a local `std::unique_ptr<text_state_t>` and
  `release()`s it into the singleton member, and `freeTextState()` hands the
  member back to a `std::unique_ptr` whose scope end destroys it. There is no
  raw owner assignment and no `delete` expression left, and the type is not
  marked or suppressed.
- The Core debug slot at the final rebase measured 1,740,768 of 1,740,800 by
  `check_size_budget.py` (pio 1,740,273), against master's 1,740,656 at
  `8da99ca5`, so 32 bytes of headroom and 112 bytes of net cost. The capability
  now also covers `binary_command_t`'s text fields, the header's text
  declarations and members, the console backend line and usage verb, and the
  `FurbleGPSText.cpp` stubs; before that it was 928 bytes over.
- The earlier pass was still 928 bytes over with only the seam compiled out,
  because the save queue, the standby dispatch, the rail restart, the detection
  shortcut and the refusal display still compiled and ran with the backend off.
  They all follow `FURBLE_GPS_TEXT` now. On the rebased head pio reports
  1,739,605 of 1,740,800 and `check_size_budget.py` reports
  1,740,112 of 1,740,800, DRAM 56,348 of 57,344, IRAM 119,036 of 119,808, so
  688 bytes of headroom remain. Master at `d31b8f95` measures 1,740,016, so the
  feature costs 96 bytes in that profile and no budget is raised.
- The job named `success` is a dependent aggregate and needs no change.

## B. The review as posted

# PR 552 review feedback: vendor text seam for $PAIR, $PQTM and $CFG

Reviewed at `106e1814` (`feat/gnss-vendor-text`) against `origin/master` `baa4ac23`,
in a throwaway worktree under `/tmp/furble-552`. Every finding below cites the
line it is about. Vendor facts were checked against the exact documents the code
cites, downloaded during the review:

- Q76: "LC26G (AB)&LC76G&LC86G Series GNSS Protocol Specification", V1.1,
  2022-12-20, the mirror in `plans/388` section 13.
- Q29: "LC29H Series&LC79H (AL) GNSS Protocol Specification", V1.2, 2023-06-20,
  the mirror in `plans/388` section 13.
- UC: "UFirebird II Series Protocol Specification", UC-04-FB2 EN R1.5, the URL
  in `plans/388` section 13.

## What is correct and should not be churned

- Every frame `frame()` builds reproduces the checksum in the vendor's own
  example: `$PAIR050,1000*12`, `$PAIR062,0,3*3D`, `$PAIR066,1,1,1,1,0,0*3A`,
  `$PAIR080,1*2F`, `$PAIR382,1*2E`, `$PAIR410,1*22`, `$PAIR511*3F`, `$PAIR513*3D`,
  `$PAIR864,0,0,115200*1B`, `$PQTMVERNO*58`, `$PQTMSAVEPAR*5A`,
  `$PQTMCFGMSGRATE,W,PQTMEPE,1,2*1D`, `$OK*04`, `$FAIL,0*1E`. The XOR rule is
  right for both vendors.
- Field order is right where it is easiest to get wrong: `PAIR066` is GPS,
  GLONASS, Galileo, BDS, QZSS, reserved (Q76 2.3.15 page 44); `PAIR080` is 0
  normal, 1 fitness (Q76 2.3.23 page 50); `PAIR062` type ids 0 to 8 match
  (Q76 2.3.12 pages 40 and 41); `CFGMSG` ids 0 to 8 and class 0 (UC 3.3
  Table 3-12 page 36); `CFGSYS` bits 0, 4, 8, 12, 16 and 20 (UC 3.3 Table 3-21
  pages 45 and 46); `RESET,0,h85` (UC 3.2 Table 3-5 pages 29 and 30).
- The constellation rules are the documents': two of three majors refused
  (Q76 2.3.15 note 2 page 44 lists only zero, one or all three), QZSS without
  GPS refused, SBAS without GPS or BDS refused (UC 3.3 page 46), SBAS refused
  in fitness mode (Q76 2.3.34 note page 60).
- The ack model is right for the case that matters most: a valid `$PAIR001` for
  another packet id returns `Verdict::NONE` and leaves the command in flight
  (`GpsText.cpp:280`), which is exactly the "late ack for a previous command"
  defence. I verified it by driving `Session` directly.
- The demux never confuses a UBX frame with a vendor line: a UBX frame whose
  payload is a complete `$PAIR001,050,0*3E` line comes out as one `UBX` piece,
  with and without leading NMEA bytes. A `$` inside a vendor body stays inside
  one `VENDOR` piece. CR, LF and CR LF all terminate one line.
- PENDING handling does not spend an attempt (`GpsText.cpp:361`), which is the
  documented meaning of result 1 (Q76 2.3.1 page 33).
- The simulator receivers are not "acks everything" doubles: they model the
  documented variant gaps (result 3 for `PAIR050` on an LC76G (PA)/(PB), result
  3 for `PAIR066` on an LC29H (BA, CA, DA, EA), result 4 out of range, `$FAIL,1`
  for a bad checksum, silence for an illegal Unicore command), and the host test
  pins the literal vendor examples, so the framing is not only self-consistent.

## Blockers

### 1. Unicore does document a dynamic platform model: `$CFGDYN` (UC 3.3.20, Table 3-41, page 58)

`GpsUnicore.cpp:169-171` refuses every platform value, `FurbleGPSText.cpp:327-328`
logs "has no Unicore dynamic model", and three documents state the same:
`docs/protocols/gnss-unicore-cfg.md:96`, `docs/supported-hardware.md:150`,
`docs/wiki/Settings-Reference.md:164`. The cited document says otherwise:

```
CFGDYN: Configure Lock Point
Applicable to: UC6580, UM670A, UM680A, UM681A
Table 3-41 Set/Output Lock Point Configuration
 Syntax      $CFGDYN,mask,DynModel,StaticHoldThresh
 Example     $CFGDYN,1,0,0
 dynModel    UINT  0 - Portable
                   1 - Static
```

UC-04-FB2 EN R1.5, printed page 58 (section 3.3.20). furble's own setting values
(`include/FurbleUI.h:857`: Do not send, Portable, Stationary, Pedestrian,
Vehicle) map exactly: Portable to `$CFGDYN,1,0,0`, Stationary to
`$CFGDYN,1,1,0`, mask bit 0 selecting `dynModel` only. Pedestrian and Vehicle
have no documented counterpart and stay refused.

Fix: send `CFGDYN` for platform 1 and 2, keep 3 and 4 refused, correct the three
documents, and add a scenario assertion for the two accepted values. This is the
one finding where the code tells the user a document says nothing while the
document says something, so it is a blocker rather than a nit.

### 2. A refused rail cut leaves a Quectel receiver with its GNSS engine off and nothing turns it back on

`textSaveQueueBuild()` (`FurbleGPSText.cpp:383-404`) always sends `PAIR382,1`,
`PAIR003`, the saves. `PAIR003` "Powers off the GNSS system, including DSP, RF,
PE and clock" (Q76 2.3.3 page 34). Q76 2.3.44 note 2 page 68 says to send
`$PAIR002` after the save when the rate is above 1 Hz. Nothing sends it:
`railOffOrRestart()` (`FurbleGPS.cpp:1148-1157`) restarts a receiver only when
`m_UbxStopped` is set, and no text path sets it, so a refused cut
(`Platform::railRelease` returns false while the IR or a Grove device holds
EXT_5V) leaves the receiver powered with no GNSS engine.

`plans/388` 12.6 records the gap, but says it lasts "until the next enable".
`enable()` calls `resetText()` and re-identifies and reconfigures; no code path
sends `PAIR002`, `PAIR004` or a rail cycle, so the receiver stays fixless across
the whole session, not until the next enable.

Fix: carry a `m_TextStopped` flag next to `m_UbxStopped`, and on a refused cut
send `PAIR002` (LC26G, LC76G) or `PAIR004` hot start (all parts, and the only
documented way back for LC86G, Q76 2.3.2 note page 34). Correct 12.6 either
way, and add a scenario with a refused cut: no existing scenario reaches this
path, so the suite cannot see it.

### 3. A Unicore `$OK` that arrives after its session timed out acks the next command

`match()` accepts a bare `$OK` for whatever is in flight (`GpsText.cpp:256-262`,
`305-307`). After `Step::TIMEOUT`, `finishConfigCommand` clears the session and
`serviceConfig` starts the next command on the next pass (`FurbleGPS.cpp:1882`,
`startConfigCommand` has no settle gate unless `settle_ms` was set, and no text
builder sets it), so a late `$OK` from the timed-out command is credited to its
successor. Demonstrated against the real code:

```
A=CFGMSG,0,0,0 ack=CFGMSG   t=1000 SEND attempts=2
                            t=2000 SEND attempts=3
                            t=3000 TIMEOUT
B=CFGNAV,1000,1000,         accept late "$OK" -> acked
```

The same applies to named replies that repeat a shared name: `$CFGMSG,0,3,0`
acks a `CFGMSG,0,0,0` command in flight (`GpsText.cpp:316`), because only the
name is compared, never the fields. `plans/388` 12.12 records the ambiguity but
not a mitigation.

Fix: after a Unicore `FAILED` or `TIMEOUT`, arm `m_ConfigSettle` with
`ACK_TIMEOUT_MS` before the next command starts, and drop any text line that
arrives inside that window. That costs one second only on a failed command and
closes the window the plan calls "not zero".

## Should fix

### 4. `BUSY` (result 5) is treated as a final refusal instead of a retry

`GpsText.cpp:121-125` puts `Verdict::BUSY` in `refused()`, so `service()` returns
`Step::FAILED`, the driver reports "refused: busy" and moves on
(`FurbleGPS.cpp:1823-1836`). Q76 2.3.1 page 33 defines result 5 as "MNL
service is busy. You can try again soon." `GpsText.h:175` even says "the
receiver asked for a retry". A busy receiver after a `PAIR066` reboot is exactly
where this bites. Treat `BUSY` like `PENDING` (re-arm the deadline, cap it, and
fall through to a resend), or resend without spending the verdict.

### 5. `PENDING` has no cap, so the configuration queue can stall forever

`GpsText.cpp:361-367`: every `PENDING` re-arms the deadline and returns `WAIT`
without spending an attempt, and there is no total bound. A receiver that keeps
answering result 1 keeps the session active indefinitely:

```
t=    0 pending=pending service=WAIT attempts=1 active=1
t= 2000 pending=pending service=WAIT attempts=1 active=1
t= 4000 pending=pending service=WAIT attempts=1 active=1
t= 6000 pending=pending service=WAIT attempts=1 active=1
```

`plans/388` 12.10 records the unmeasured processing time, not the missing bound.
Count `PENDING` results and give up after a small number (three to five) or
after a total deadline of a few seconds.

### 6. A corrupt ack for a different packet id aborts the command in flight

`GpsText.cpp:261` checks the checksum before `GpsText.cpp:280` compares the
packet id, so `$PAIR001,062,0` with one corrupted byte, arriving while a
`PAIR050` command is in flight, returns `BAD_REPLY`, which is a refusal, and the
session ends as `FAILED` without a retry:

```
bad-xor stale id 62 -> bad_reply service=FAILED
```

A single flipped bit on the UART therefore turns into "the receiver refused your
setting". Move the packet id comparison above the checksum verdict so another
id's line is `NONE` whatever its checksum.

### 7. `PENDING` is counted as a refusal in the status

`FurbleGPSText.cpp:161-170` increments `naks` for every verdict that is not
`ACKED`, so a normal "being processed" answer increments the refusal counter that
`gps text` prints. Count `refused(verdict)` only, and leave `PENDING` out of both
counters.

### 8. `MAX_LINE` is not a bound on a terminated vendor line

`GpsDemux.cpp:104-113` applies the 128 byte test only when no terminator has
arrived, so a terminated line of any length becomes one `VENDOR` piece and
`parse()` accepts it. A 154 byte line:

```
over MAX_LINE (147 bytes)  len=154 used=154 kept=0 pieces=[VENDOR@0+154]
  parse=1 name=PAIR001 csum_ok=0 term=1
```

`GpsText.h:38-46` and `docs/protocols/gnss-quectel-pair.md:29` both claim the
128 byte cap. Either drop a `VENDOR` line longer than `MAX_LINE` the way the
header describes, or correct the two comments. The same header says a runaway
line is "dropped a byte at a time"; in practice only the `$` is dropped and the
remainder becomes one `NMEA` piece, which is harmless but not what it says.

### 9. No refusal reaches any UI

`getTextStatus()` is read only by `src/FurbleConsole.cpp:1672`. The LVGL UI shows
a fix icon only (`src/FurbleUI.cpp:2411-2414`), and neither the WebUI nor the
companion status carries a GPS text field. So `gnss_refused`, `sbas_refused`,
`platform_refused` and `psm_refused` are visible on the serial console and in
`ESP_LOGW` and nowhere else. The PR sets out to stop settings being silently
ignored, and on the device itself they still are. The UBX backend has the same
shape, so this is a design gap rather than a regression, but it is the one part
of the refusal model with no user surface and no test.

### 10. `gps text` says "sent" when nothing was sent

`src/FurbleConsole.cpp:1688` prints `platform: sent` whenever
`platform_refused` is false, including the default "Do not send" setting where
no command was queued; `1689` prints `standby: available` with the same problem.
Add a third state, or report what the plan actually queued.

## Nits

11. Note page numbers are one page early in four places, while every section
    number is right: Q76 2.3.1 result codes are on page 33, not 32
    (`GpsText.h:209`, `GpsQuectel.h:33`); Q76 2.3.44 note 1 is on page 68, not
    67 (`FurbleGPSText.cpp:398`); Q76 2.3.45 note 2 is on page 69, not 68
    (`FurbleGPSText.cpp:390`, `docs/protocols/gnss-quectel-pair.md:101`); Q29
    2.4.13 note 3 is on page 62, not 61 (`GpsQuectel.cpp:60`,
    `sim/vendor_gnss.h:56`). The quoted sentences themselves are verbatim.
12. `GpsQuectel.cpp:204` says `PAIR080` is "normal, fitness, drone and
    swimming". The table (Q76 2.3.23 pages 50 and 51) is 0 normal, 1 fitness,
    2/3/4 reserved, 5 drone, 6 reserved, 7 swimming. No behaviour change, since
    only 0 and 1 are sent, but the numbering in the comment is wrong.
13. `GpsUnicore.h:32-35` claims a documented 50 to 1000 ms `CFGNAV` range from
    Table 3-19 page 43. That table gives 1000, 200 and 100 as the `MeasRate` and
    `NavRate` examples and states no range; 50 is the `DRNavRate` 20 Hz example.
    The floor furble can actually ask for is 100 ms (`RATE_MS`,
    `include/FurbleGPS.h:440`), so nothing misbehaves today, but the citation
    does not support the constant.
14. `include/FurbleGPS.h:574` `TEXT_SAVE_REPLY_MS` is dead: the save session
    uses `GpsText::Session::ACK_TIMEOUT_MS` (1000 ms), and the whole save budget
    is 1500 ms, so one unanswered frame consumes it. Either use the constant or
    delete it.
15. `sim/scenarios/e2e/gps-unicore-refusals.txt:3` says "A flashless product
    refuses CFGSAVE", but the scenario never seeds `gps_unicore_no_flash`, and no
    scenario seeds `gps_quectel_no_search_mode`. Two modelled variant gaps have
    no end to end coverage.
16. Nothing asserts the refusal flags themselves. `text.*` exposes the virtual
    receiver, not the driver status, so a regression that stopped publishing
    `platform_refused` or `psm_refused` would pass every scenario. Either expose
    a `gps.text.*` query, or assert through `gps text` output in a scenario.
17. `plans/388` line 393 says "six scenarios"; the PR adds seven.
18. `sim/vendor_gnss.cpp` parses and frames with the same `GpsText::parse` and
    `frame` as the code under test, so the doubles cannot falsify the checksum
    rule. `tests/host/gps_text_test.cpp:70-83` pins the literal vendor examples,
    so the risk is contained; a note in `sim/vendor_gnss.h` saying which half is
    independent would save the next reader the question.
19. `textSaveBlocking` reports `last_verdict = TIMEOUT` for every refusal
    (`FurbleGPSText.cpp:544-548`), even when the receiver sent `$PAIR001,513,2`.
20. `serviceText` offers every vendor line to the configuration session and then
    to the save session (`FurbleGPSText.cpp:161` and `175`), so one bare `$OK`
    would be accepted by both and counted twice. The comment above it says a
    save acknowledgement never reaches the configuration queue; nothing enforces
    that today.

## DRAM budget

`tools/size_budget.json` raises `m5stick-s3` `dram_bytes` from 48,128 to 49,152
(+1,024). That is what the tool requires, not a choice:
`tools/check_size_budget.py` sets `HEADROOM = 512` and `STEP = 1024` and records
"the measured size plus at least HEADROOM bytes, rounded up to a KiB". For the
47,840 the PR reports, 47,840 + 512 = 48,352, which rounds up to 49,152. The old
48,128 would have left 288 bytes, below the 512 the tool enforces. So the raise
is justified, and small.

Does any of it belong on the heap? No. The new fixed state is the driver's own
lifetime: two `GpsText::Session` (about 160 bytes), one `text_status_t` (about
88 bytes) and the in-DRAM `std::optional<binary_command_t>` that now carries a
`Command` (about 64 bytes). That totals about 312 bytes on this ABI, which
matches the 288 byte gap between the old headroom and the new measurement. The
per-command payloads are already heap: `Command::body` and `Command::ack` are
`std::string`, the config queue is a `std::deque`, and `m_TextSaveQueue` is a
`std::vector`. Moving the sessions to the heap would buy nothing and cost an
allocation on the configuration path.

One correction to the PR text: the justification lists "the save queue" among the
DRAM additions, but `m_TextSaveQueue` is an empty-heap vector and does not count
against `.dram0`. The PR should quote the before and after measured numbers
instead, since "the old number left 288 bytes of headroom" describes the new
measurement against the old budget and leaves the actual growth unsaid.

## What I ran

- `tools/pre_review.py --base origin/master`: 13 gates PASS, 1 FAIL. The failure
  is `clang-format` (exit 2) only because the tool is absent here:
  "clang-format not found; install it with: python3 -m pip install -r
  .github/workflows/clang-format-requirements.txt". An earlier attempt showed
  `settings contracts` and `scanner unit tests` exiting -9; both were SIGKILL
  under load from two concurrent simulator builds and both pass when run alone
  (`check_settings_contracts.py`: PASS, 0 FAIL and 6 WARN; the scanner unittest
  set: OK, skipped=4).
- `tools/check_doc_citations.py --root .`: 0 errors of 3,123 citations in 179
  files. With `--base origin/master`: 0 errors of 520 citations in 66 files.
- `tools/check_sim_scenarios.py`: "sim scenario manifest is complete".
- Host suite at `106e1814` (`cmake -S tests/host -B /tmp/hb552`, Debug):
  `gps-text` PASS (206 checks), `gps-text-sim` PASS (61 checks),
  `console-commands` PASS, `gps-ubx` PASS, `gps-ubx-m10` PASS, `gps-casic` PASS,
  `gps-ubx-fuzz` PASS (seed 1, 300 iterations).
- The seven new e2e scenarios were not run. `sh sim/build.sh` with
  `FURBLE_DEP_ROOT` and `FURBLE_LVGL_DIR` pointed at this checkout built 42
  objects and then sat on `src/FurbleGPSUbx.cpp` for over thirty minutes while
  another session built the same tree, so no `furble-sim` binary existed when the
  review closed. This is the one named gate I could not execute.
- The firmware DRAM and flash numbers were not reproduced: PlatformIO is not
  installed here, so 47,840 / 49,152 is taken from the PR text and reconciled
  against `tools/check_size_budget.py` rather than measured.
- Vendor documents were fetched and read page by page: Q76 V1.1 (107 numbered
  pages), Q29 V1.2 (111), UC R1.5 (153). Every checksum the review relies on was
  recomputed independently in Python.
