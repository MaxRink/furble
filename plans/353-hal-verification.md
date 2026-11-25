# 353 - HAL verification pass 2

Status: fixed on `fix/hal-verification`, branched from master `92618bff` and
pushed as PR 479. No firmware file changes and no image changes: nothing in
firmware includes `lib/hal` yet, so this branch only touches the drivers, their
host test and the documents.

Related: [322](322-hardware-abstraction.md) (the HAL design and board gap
tables), [322-review-feedback.md](322-review-feedback.md) (the first review of
the same drivers). The rule changes that follow from this pass live in
`lib/hal/CLAUDE.md`.

## Why

An independent second pass re-read `lib/hal` against the datasheets cited in
the headers and re-read `tests/host/hal_drivers_test.cpp` against both. It
found four defects that the first review missed. Three are driver promises the
silicon cannot keep or timings the silicon needs and the driver never gave;
the fourth is a test that confirmed the driver instead of the datasheet, which
is why the first three survived review.

The pattern behind the fourth is the important one. Every defect 1 to 3 was
invisible to the suite because the fake bus answered whatever the driver asked
for: it walked the register file linearly, so an access spanning two BQ27220
commands or two XL9555 ports came back looking valid, and the assertions
compared driver constants with driver constants.

## Defects

| # | Defect | Source | Fix | Test evidence |
| ---: | :--- | :--- | :--- | :--- |
| 1 | AXP2101 power-key edge events were unreachable. `enablePowerKeyIrq()` set REG 41 bits 3:2, while `PowerKey::falling` and `PowerKey::rising` decode REG 49 bits 1:0. | AXP2101 datasheet rev 1.0 section 6.12.1 page 28: a status bit is set only when its enable bit is set. Section 6.13.2.41 page 42: bits 1:0 default to 0. Section 6.13.2.44: REG 49 bits 3:0 are the power-key status. | Decided to keep the API and enable it: REG 41 bits 3:0 are written together from one `IRQ1_KEY_MASK`, which is also the decode and clear mask, so the two cannot drift apart. The extra interrupt cost is documented on the call. | `axp2101 pek irq enables all four key bits`, `axp2101 pek irq off clears only the key bits`, `axp2101 enables every bit it decodes`, `axp2101 leaves the other REG 41 bits alone`, and one `axp2101 decodes <event>` plus one `axp2101 clears only the <event> bit` case per bit of REG 49 bits 3:0. Reverting the driver to bits 3:2 fails two of them. |
| 2 | BQ27220 had no bus pacing. `read()` issued five standard commands back to back and `HalBus.h` had no delay primitive. Failure is silent: a gauge watchdog reset returns plausible but wrong SOC. | TI SLUUBD4A (revised November 2022) section 5.3 page 63: at least 66 us of bus-free time between packets, and hosts should not read a standard command more than twice a second because the data refreshes at 1 Hz. | `RegisterBus::delayUs(uint32_t)` added as a documented pacing seam, default no-op, and the driver asks for 66 us before every packet after the first. The 2 Hz cap is documented on `read()` and stated as `BQ27220::MIN_POLL_INTERVAL_MS`. | `bq27220 leaves a gap before every packet after the first` asserts the exact interleaved log (`read 08 2`, `delay`, `read 0A 2`, and so on), `bq27220 waits between packets only` counts four waits for five packets, `bq27220 leaves the bus free at least 66 us` checks every length, `bq27220 poll interval honours the 2 Hz cap`, and `bq27220 reads the five standard commands once each` pins the packet list. Removing the pacing calls, spending all four gaps before the first packet, and dropping the gap before the last packet each fail a different assertion. |
| 3 | `SX1262::sleep()` sampled BUSY once, exactly 500 us after SetSleep, and returned that sample as the result. | Semtech DS.SX1261-2.W.APP Rev 2.2 Table 8-2 has no standby-to-sleep time, so no document bounds how soon BUSY rises. Section 9.3 only says BUSY is held high for the whole sleep. | Step 5 became a bounded poll for BUSY high (`waitBusy()` shared with step 2), budget 5,000 us, and the header states the total: 200 + 10,000 + 500 + 5,000 = 15,700 us worst case. | `sx1262 sleep tolerates a late BUSY` (BUSY enters 1 ms after the command), `sx1262 confirms BUSY high inside the bound`, `sx1262 sequence inside the stated bound`, `sx1262 budgets are the numbers the header states`, `sx1262 header total bound is the documented 15,700 us` sums the four constants, and `sx1262 stuck BUSY stays inside the documented total`. Reverting to a single sample fails the first; a 9,000 us confirm budget or a 14,000 us ready budget fails the two budget checks. |
| 4 | The test asserted driver-derived values, and the fake bus walked up addresses instead of honouring each chip's auto-increment and wrap. | SLUUBD4A section 2.1 and Table 2-1 (a standard command is a 16-bit word at an even command code), Table 2-6 (BATTPRES, DSG), section 2.11 (0xFFFF is no estimate); XL9555 datasheet Rev 2.3 section 9.2.1 and Table 6 (16-bit port pairs that wrap inside the pair); SX1262 section 8.1 (100 us NRESET) and Table 8-2 (3,500 us cold start). | Four assertions rewritten to datasheet values; the fake bus gained an `AddressMode` (linear, 16-bit command word, 16-bit port pair), a misuse log and one interleaved event log. | `bq27220 no battery, no estimate` now compares with the literal `0xFFFF`; the protected-rail mask is the literal `0x0001` instead of `railBit()`; `sx1262 reset pulse is at least 100 us` reads the reset delay on its own instead of a `100 + 500` sum. Three new model cases pin the fake's wrap behaviour, and the interleaved log replaces separate read and delay logs so a gap's position is assertable. |

## Review of the fix

The fix came back with five items. All five are folded into the rows and
decisions above rather than kept as a separate list:

- The fake bus held reads and delays in separate logs, so a pacing gap could be
  counted but not placed, and a driver that spent all four gaps before the first
  packet or dropped the one before the last still passed. The fake now keeps one
  interleaved log, as `FakeRadio` does, and the BQ27220 case asserts the exact
  read, delay, read order.
- The SX1262 total was documented as 15,700 us but not pinned, so a 9,000 us
  confirm budget or a 14,000 us ready budget passed. The test now sums the four
  header constants against that total and checks each against the step that
  names it. The stuck-path bound moved to the documented total; the 14,200 us
  bound it replaces is recorded in BUSY wait bound as looser than the 10,200 us
  it came from.
- The AXP2101 double-IRQ note understated the failure. `enablePowerKeyIrq()` now
  says that REG 49 bits are latches and the IRQ pin a level, so a level or wake
  handler re-fires until the latch is drained, and `powerOff()` records that
  nothing drains REG 49 on the power-off path.
- Plan 322's Pager gap list now carries the BQ27220 pacing clause it was said to
  have, instead of the requirement existing only in this document.
- The `check()` figure in the gates table is now the executed count the binary
  prints, not a count of call sites.

## Decisions

### AXP2101 edge events: enabled, not removed

The API could have dropped `falling` and `rising`. It keeps them because they
are the only way to learn that the key went down before the short press
completes: a short press is latched on release, so a UI that only sees short
and long presses cannot light a pressed state.

Cost, documented on `enablePowerKeyIrq()`: REG 49 bits are latches and the IRQ
pin carries their level, not an edge (sections 6.13.2.44 and 6.12.1), so one
latched bit holds the line asserted until `readPowerKey()` writes it back. A
board that wires the PMIC IRQ pin to a GPIO level interrupt or a wake input
re-enters its handler for as long as a bit stays set, and sees two assertions
per press, once as the key falls and once as it rises. Draining REG 49 is part
of every entry: a handler that returns with a bit still set runs again
immediately, and a wake-on-IRQ design counts two wake events per press.
Enabling only bits 3:2 has no edges to report, so it is not a cheaper version
of the same thing, it is a different API.

Nothing drains REG 49 on the power-off path, and `powerOff()` now says so. The
press that reaches it has latched its falling edge and its short or long press,
and the release after the rails drop latches a rising edge. If REG 49 survives
soft power off, as VRTC staying up allows, the next boot reads that release as a
phantom rising edge of a press nobody made on that boot. A board that
powers off from the key clears REG 49 before the write or ignores its first read
after boot; the driver keeps `powerOff()` to the one documented bit and leaves
that policy to the board. `axp2101 power off does not drain the key latches`
pins the behaviour rather than the intent.

### Pacing seam

`RegisterBus::delayUs()` is a virtual with a documented no-op default rather
than a pure virtual. Two reasons: the host fake and a bit-banged bus have no
timing rule to satisfy, and a pure virtual would force every future adapter to
implement a wait it may never need. The risk of a default is that a firmware
adapter forgets to override it and silently loses the pacing, so the default is
documented as insufficient for the gauge, `lib/hal/CLAUDE.md` makes pacing a
driver duty that the board row must satisfy, and plan 322's Pager gap list
records the requirement for the first board with a BQ27220. The driver
asks for the wait; whether the adapter honours it is a board-level item that no
host test can prove.

The 2 Hz cap is a caller contract, not a loop in `read()`. A full read issues
five packets, all inside the 66 us rule; blocking the caller for 500 ms inside
a battery read would stall the UI, and `read()` cannot tell a 2 Hz timer from a
busy loop. The limit is stated on `read()`, stated as a constant, and pinned by
a test against the document's 500 ms.

### BUSY wait bound

`waitBusy()` is one helper used by both waits, so the two cannot diverge: BUSY
low before SetSleep is bounded by the 10 ms ready budget, BUSY high after it by
5,000 us. The 5,000 us is a driver choice because Table 8-2 has no number; the
header states the resulting 15,700 us total so a board can budget its power-off
path, and the test checks the sequence against an independent 20 ms budget.

The total is also pinned to the header's own arithmetic:
`sx1262 header total bound is the documented 15,700 us` sums the four constants
and `sx1262 budgets are the numbers the header states` compares each with the
number its step names. Without them a 9,000 us confirm budget or a 14,000 us
ready budget passed every case, and the header's 15,700 us was not checked by
anything that ran.

The stuck-BUSY path costs 200 us of reset plus the ready budget and returns
before SetSleep, so 10,200 us is its real cost. The bound on it moved from that
10,200 us, which restated the driver's constants, to 14,200 us of four Table
8-2 cold starts plus reset, which is looser than the path it guards: a 14,000 us
ready budget fitted it exactly. The case now asserts the documented total, and
the arithmetic checks above are what keep that total at the number the header
states. A bound on a path that only spends 10,200 us cannot be the check that
holds the budget, so it no longer tries to be.

### Fake bus addressing

`AddressMode` is part of the test, not the driver. `LINEAR` walks registers,
which is what the AXP2101 does (REG 34/35 as one 16-bit value, section
6.13.2.30). `WORD` keeps a BQ27220 transfer inside one 16-bit command word and
flags an odd command code or a wider read, because a standard command is one
word at an even code (section 2.1, Table 2-1). `PORT_PAIR` wraps inside a
XL9555 port pair (section 9.2.1), so a two-byte read of 0x03 answers ports 1 and
0 rather than ports 1 and the polarity register. A transfer the part cannot
serve is recorded in `misuses`; the driver cases assert that log stays empty,
and three model cases prove the wrap exists so it cannot be removed silently.

One `events` log holds every transfer and every wait in issue order, as
`FakeRadio` already did. The earlier fake kept `reads` and `delays` apart, so
`bq27220 waits between packets only` and a per-wait minimum both passed a driver
that spent all four gaps before the first packet or none before the last: only
the position of a gap in one log separates those from a correct read.
`bq27220 leaves a gap before every packet after the first` compares the shape of
that log (`read 08 2`, `delay`, `read 0A 2`, and so on), while `bq27220 leaves
the bus free at least 66 us` keeps checking the length of each wait.

## Gates

Run in `/tmp/furble-halfix` on this branch, all from a clean checkout of the
branch, with `cmake -S tests/host -B /tmp/furble-halfix-host-build` first:

| Command | Result |
| :--- | :--- |
| `cmake -S tests/host -B /tmp/furble-halfix-host-build` | configures the host suite |
| `cmake --build /tmp/furble-halfix-host-build --target hal_drivers_test` | builds with `-Wall -Wextra -Werror` |
| `ctest --test-dir /tmp/furble-halfix-host-build -R hal-drivers --output-on-failure` | 1 of 1 passed, `hal-drivers`; the binary prints `hal-drivers: PASS (219 checks)` and exits 0, the count being the `check()` calls it executed, loops included |
| `sh tools/run_sanitized_host_tests.sh '^hal-drivers$'` | `sanitized host tests: PASS`, 1 of 1 under ASan and UBSan |
| `clang-format -i` on the eight changed C++ files | version 21.1.2, the version `tools/check_format.py` requires; `clang-format --dry-run --Werror` on the same files exits 0 |
| `python3 tools/check_portability_inventory.py` | `portable contract: clean`, 18 contract files, 27 manifest files |
| `python3 tools/pre_review.py --base origin/master` | 10 of 10 gates pass, 140 scanner unit tests OK |

The format gate itself only covers `tests/host` of the two directories this
branch changes, because `tools/check_format.py` and the matching `main.yml`
step list `src include lib/furble lib/testing sim tests/host`. It reports one
formatted file, the test. The seven driver files were formatted with the same
clang-format 21.1.2 and verified with `--dry-run --Werror` by hand.

Mutation checks, each run by reverting one driver change and rebuilding: REG 41
bits 3:2 only fails `axp2101 pek irq enables all four key bits` and `axp2101
enables every bit it decodes`; dropping the pacing call fails `bq27220 waits
between packets only`; returning `port.busy()` instead of the bounded wait
fails `sx1262 sleep tolerates a late BUSY`. The two ways a counted-but-misplaced
pace passed before are mutations too: all four gaps before the first packet and
a missing gap before the last packet each fail `bq27220 leaves a gap before
every packet after the first`, the second also failing the wait count. A 9,000
us confirm budget and a 14,000 us ready budget each fail both SX1262 budget
checks. The tree was restored and the suite re-ran green after each.

## Left open

- The 66 us pacing is only as good as the board adapter. The first board that
  carries the BQ27220 must override `RegisterBus::delayUs()` with a real wait
  and read back the gauge on hardware; no host test can prove the wait.
- `SX1262` sleep was never measured on a part. The bounded wait makes a late
  BUSY a pass instead of a failure, but the 5,000 us confirmation budget and
  the sleep current stay bench items of the board that fits the radio.
- The AXP2101 edge events are enabled for every board that calls
  `enablePowerKeyIrq()`. If a board's IRQ handler cannot serve two wake events
  per press, that board should disable the function and ignore the two fields
  rather than have the driver split the mask per caller. The handler must drain
  REG 49 on every entry, because a latched bit holds the IRQ line asserted.
- `powerOff()` leaves REG 49 alone, so a board that powers off from the key has
  to clear the latches before the write or ignore its first read after boot, or
  the release of the power-off press arrives as a phantom rising edge. The
  header records the behaviour and the test pins it; the board owns the fix.
- `lib/hal` is outside the clang-format gate's directory list, in
  `tools/check_format.py` and in `main.yml` alike. Formatting the drivers is
  manual until both lists grow `lib/hal`, which is a CI change of its own.
