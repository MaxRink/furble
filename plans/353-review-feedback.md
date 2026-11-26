# 353 - Review feedback for the HAL verification pass 2

## Scope

This is the review-feedback record for plan 353
([353-hal-verification.md](353-hal-verification.md)), branch
`fix/hal-verification`, PR 479 against master. Every finding from every round is
recorded as fixed, already fixed or a false positive, with evidence naming the
file and line that carries the fix or the command that proves it.

The branch is one fix commit plus the merge verification commit that adds this
file and corrects one test comment, rebased onto master `afb8513b` before merge.
Line numbers below are from the merge tree, which is the one that merges.

Three rounds fed it:

1. the independent second verification pass over `lib/hal` and its test, which
   found four defects the first review missed;
2. the review of that fix, which returned five items;
3. the merge verification pass, which added this record and re-ran every gate
   and mutation the two earlier rounds claimed.

## Round 1 - second verification pass

| # | Source | Finding | Resolution | Evidence |
| ---: | :--- | :--- | :--- | :--- |
| 1 | Second verification pass | AXP2101 power-key edge events were unreachable: `enablePowerKeyIrq()` enabled REG 41 bits 3:2 while `PowerKey::falling` and `PowerKey::rising` decode REG 49 bits 1:0, whose enables default to 0 (section 6.13.2.41). | Fixed in the driver, not the test. REG 41 bits 3:0 are written together from `IRQ1_KEY_MASK`, which is also the decode and clear mask, so the enabled set and the decoded set cannot drift apart. The interrupt cost is documented on the call. | `lib/hal/AXP2101.cpp:35`, `lib/hal/AXP2101.cpp:187`, `lib/hal/AXP2101.h:143`; cases `axp2101 pek irq enables all four key bits`, `axp2101 enables every bit it decodes`, `axp2101 decodes <event>` x4 |
| 2 | Second verification pass | BQ27220 issued five standard commands back to back against section 5.3's 66 us bus-free rule and the twice-per-second poll cap, with no delay primitive in `HalBus.h`. The failure is silent: a gauge watchdog reset answers plausible but wrong data. | Fixed in the driver. `RegisterBus::delayUs()` added as a documented pacing seam with a no-op default; `BQ27220::read()` asks for 66 us before every packet after the first; the 2 Hz cap is stated as `MIN_POLL_INTERVAL_MS` and as a caller contract. | `lib/hal/HalBus.h:52`, `lib/hal/BQ27220.cpp:43`, `lib/hal/BQ27220.h:35`, `lib/hal/BQ27220.h:46`; cases `bq27220 leaves a gap before every packet after the first`, `bq27220 waits between packets only`, `bq27220 leaves the bus free at least 66 us` |
| 3 | Second verification pass | `SX1262::sleep()` sampled BUSY once, exactly 500 us after SetSleep, and returned that sample, so a healthy part that asserts BUSY later looked broken. Table 8-2 tabulates no standby-to-sleep time. | Fixed in the driver. `waitBusy()` polls for the level inside a budget and is shared by the BUSY-low and BUSY-high waits, so the two cannot diverge. Step 5 gets `SLEEP_CONFIRM_BUDGET_US = 5000` and the header states the resulting 15,700 us total. | `lib/hal/SX1262.cpp:12`, `lib/hal/SX1262.cpp:46`, `lib/hal/SX1262.h:66`, `lib/hal/SX1262.h:31`; case `sx1262 sleep tolerates a late BUSY` |
| 4 | Second verification pass | The test confirmed the driver instead of the datasheet: computed expected values from driver constants and a fake bus that walked up the register file across command words and port pairs. | Fixed in the test only, which is where the defect was. Datasheet literals replace the derived values, and the fake gains an `AddressMode`, a misuse log and one interleaved event log. | Literal `0xFFFF` at `tests/host/hal_drivers_test.cpp:466`, literal `0x0001` at `tests/host/hal_drivers_test.cpp:185`, `delayAfter()` reset check at `tests/host/hal_drivers_test.cpp:596`, fake addressing at `tests/host/hal_drivers_test.cpp:62` |

## Round 2 - review of the fix

| # | Finding | Resolution | Evidence |
| ---: | :--- | :--- | :--- |
| 1 | The fake bus kept reads and delays in separate logs, so a pacing gap could be counted but not placed: a driver that spent all four gaps before the first packet, or dropped the one before the last, still passed. | Fixed. One `events` log holds every transfer and every wait in issue order, and the BQ27220 case compares the shape against read, delay, read. | `tests/host/hal_drivers_test.cpp:128`, `tests/host/hal_drivers_test.cpp:161`, `tests/host/hal_drivers_test.cpp:446`. Both mutations fail it; see the round 3 table. |
| 2 | The 15,700 us total was documented but not pinned, so a 9,000 us confirm budget or a 14,000 us ready budget passed every case. | Fixed. The test sums the four header constants against the documented total and checks each constant against the step that names it. The stuck-BUSY bound moved to the documented total; the 14,200 us bound it replaces is recorded as looser than the 10,200 us path it guarded. | `tests/host/hal_drivers_test.cpp:575` to `587`, `tests/host/hal_drivers_test.cpp:631` |
| 3 | The AXP2101 double-interrupt note understated the failure: it said two interrupts per press without saying that REG 49 bits are latches and the IRQ pin is a level. | Fixed. `enablePowerKeyIrq()` now says the bits are latches behind a level line, that a handler re-enters until they are drained, and that a wake design counts two wake events per press; `powerOff()` records that it drains nothing. A case pins that behaviour. | `lib/hal/AXP2101.h:143`, `lib/hal/AXP2101.h:168`, `tests/host/hal_drivers_test.cpp:401` |
| 4 | Plan 322's Pager gap list did not carry the BQ27220 pacing clause it was said to have, so the requirement existed only in plan 353. | Fixed. The Pager paragraph records the `RegisterBus::delayUs()` override with its datasheet section, and the hal-3 deviation repeats it. | `plans/322-hardware-abstraction.md` "T-LoRa Pager" paragraph and the hal-3 deviation bullet |
| 5 | The `check()` figure in the gates table was a count of call sites, not the number of assertions that run. | Fixed. The figure is the executed count the binary prints, loops included. | `hal-drivers: PASS (219 checks)`; reproduced in round 3 from a clean configure of the branch |

## Round 3 - merge verification pass

| # | Finding | Resolution | Evidence |
| ---: | :--- | :--- | :--- |
| 1 | Plan 353 recorded its review rounds only as prose inside its own sections; `plans/353-review-feedback.md` did not exist, which `AGENTS.md` requires for every finding. | Fixed. This file, linked from plan 353's Related line. | `plans/353-hal-verification.md` Related; this document |
| 2 | The new SX1262 test comment said "only the 500 us comes from the datasheet", which contradicts the header: the 200 us reset pulse is twice section 8.1's 100 us minimum and the 10 ms ready budget is sized against Table 8-2's 3.5 ms cold start. | Fixed. The comment now separates the three datasheet-derived numbers from the one budget with no tabulated value. No code or assertion changed. | `tests/host/hal_drivers_test.cpp:566` |
| 3 | `SX1262.h` cites Table 8-2 for the 3.5 ms cold start, but the revision history of the cited revision (Rev 2.2) records "Modified Startup and sleep in Table 8-3". | Not verified, not changed. No copy of the datasheet may be committed (`lib/hal/CLAUDE.md`), so the table could not be re-read during this pass, and the line is unchanged from master. Recorded under "Left open" in plan 353 rather than guessed at. | `lib/hal/SX1262.h:19`, `lib/hal/SX1262.h:24`; plan 353 "Left open" |

## Mutation evidence

Each row reverts one production change or misplaces one pacing call, rebuilds
the same test file, and runs it. The parent commit is `92618bff`, the branch
point.

| Mutation | Result |
| :--- | :--- |
| All three drivers reverted to `92618bff` | 5 failures: `axp2101 pek irq enables all four key bits`, `axp2101 enables every bit it decodes`, `bq27220 leaves a gap before every packet after the first`, `bq27220 waits between packets only`, `sx1262 sleep tolerates a late BUSY` |
| `BQ27220.cpp` only reverted to `92618bff` | 2 failures: `bq27220 leaves a gap before every packet after the first`, `bq27220 waits between packets only` |
| `SX1262.cpp` only reverted to `92618bff` | 1 failure: `sx1262 sleep tolerates a late BUSY` |
| `AXP2101.cpp` only reverted to `92618bff` | 2 failures: `axp2101 pek irq enables all four key bits`, `axp2101 enables every bit it decodes` |
| All four 66 us gaps moved ahead of the first packet | 1 failure: `bq27220 leaves a gap before every packet after the first` |
| Gap before the last packet dropped | 2 failures: `bq27220 leaves a gap before every packet after the first`, `bq27220 waits between packets only` |
| Step 5 returns `port.busy()` again | 1 failure: `sx1262 sleep tolerates a late BUSY` |
| `SLEEP_CONFIRM_BUDGET_US` 5,000 to 9,000 | 2 failures: `sx1262 budgets are the numbers the header states`, `sx1262 header total bound is the documented 15,700 us` |
| `READY_BUDGET_US` 10,000 to 14,000 | 2 failures: the same two cases |
| AXP2101 rising and falling bit constants swapped | 3 failures: `axp2101 decodes rising edge`, `axp2101 decodes falling edge`, `axp2101 pek decode` |
| `AXP2101::railBit()` shifted by one | The new literal mask fails `axp2101 protected rail stays on`; the pre-fix `railBit()`-derived mask passes. This is the assertion the fourth defect was about. |

## Gates

Run on the branch head, from a clean checkout, clang-format 21.1.2 in `PATH`:

| Command | Result |
| :--- | :--- |
| `cmake -S tests/host -B <dir>` then `cmake --build <dir> --target hal_drivers_test` | builds with `-Wall -Wextra -Werror` |
| `ctest --test-dir <dir> -R hal-drivers --output-on-failure` | 1 of 1 passed; `hal-drivers: PASS (219 checks)` |
| `sh tools/run_sanitized_host_tests.sh '^hal-drivers$'` | `sanitized host tests: PASS`, 1 of 1 under ASan and UBSan |
| `clang-format --dry-run --Werror` on the eight changed C and C++ files | exit 0; `clang-format -i` leaves the tree unchanged |
| `python3 tools/check_portability_inventory.py` | `portable contract: clean`, 18 contract files, 27 manifest files |
| `python3 tools/pre_review.py --base origin/master` | `PASS: 10 gates` |
| `python3 tools/check_review_threads.py --repo MaxRink/furble --pr 479` | must pass with CI green on the merged head; see the PR |
