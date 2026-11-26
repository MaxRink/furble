# 364 - Review feedback for the EXT_5V rail ownership branch

Review-feedback record for PR #493 (`fix/ext5v-rail-ownership`), branch head
`e0bca1b2`. An adversarial review raised five blocking findings (B1 to B5) and
seven non-blocking ones (N1 to N7). Each is recorded here with a disposition:
fixed, already fixed, or a false positive with the evidence.

The review was reproduced before any change. Two of its claims did not survive
that: one sub-claim in B2 about the M5Tough profile, recorded under B2, and no
other. Everything else matched the code at `e0bca1b2`.

## Blocking

### B1 - the interlock was a denylist, so undefined supplies allowed output mode

Verified. `PlatformPolicy::railEnableVerdict()` refused only `UNKNOWN` and
`PORT_5V` and returned `ALLOW` for anything else, and
`Platform::railOutputPermitted()` cast the raw `m5pm1_pwr_src_t` from the vendor
`getPowerSource()`, which returns `PWR_SRC` 0x04 bits 2:0 unclamped
(`M5PM1.cpp`, `*src = (m5pm1_pwr_src_t)(val & 0x07)`). Values 4 to 7 therefore
reached `ALLOW`. The first run of the new `boundary-platform-rail` target failed
on exactly those four values, which is the review's finding reproduced.

Fixed:

- `railEnableVerdict()` is an allowlist: `USB_5V` and `BATTERY` allow,
  `PORT_5V` refuses as an external source, `UNKNOWN` refuses, and a value the
  enumeration does not name falls out of the switch into
  `REFUSE_UNKNOWN_SUPPLY`.
- `railOutputPermitted()` clamps anything above `RailSupply::BATTERY` to
  `UNKNOWN` before the cast, so an undefined register value never enters the
  policy enumeration at all.
- `tests/host/rail_account_test.cpp` (`rail-account`) now walks raw values 0 to
  7 with the expected verdict for each, and asserts that an unreadable supply
  refuses whatever value came with it.
- `tests/host/boundaries/platform_rail_test.cpp` drives the production
  `railAcquire()` for values 3 to 7 and for an unreadable register, and asserts
  no write.

Proof: with the denylist restored, `boundary_platform_rail_test` reports
`FAIL: a refused enable reports false` and `FAIL: no PWR_CFG 0x06 write was
issued` for values 4, 5, 6 and 7, and `rail_account_test` fails its four new
cases. Both pass with the allowlist in place.

### B2 - `cfg.output_power = false` left the Grove 5 V output unpowered at boot

Verified for the AXP boards, with one correction.

`M5Unified::begin()` calls `Power.setExtOutput(cfg.output_power)`, and the
default is `true`. Clearing it took `Axp192.setEXTEN(false)` on the StickC and
StickC Plus, `Axp192.writeRegister8(0x90, 0x07)` on the Core2 (GPIO0 float, LDO
off) and `M5pm1.setExtOutput(false)` on the StickS3, so the Grove 5 V pin came
up unpowered on all four. `RailHolder::GROVE` has no firmware client, so nothing
could switch it back on. On the AXP boards this was a regression against
`e0bca1b2`'s base, which is blocking.

**Correction:** the review's sub-claim that the M5Tough cannot acquire the rail
because `Profile::gpsRail` is false is a false positive. The `TOUGH` row ends
with `true` (`src/FurbleBoard.cpp`), `Board::profileFor(board_M5Tough)` returns
it, and `tests/host/board_profile_test.cpp` asserts the same value. The Tough
could acquire the rail; the defect was the boot state, not the profile.

Fixed with the review's second option, scoping the flag:

- `cfg.output_power = false` is inside `#if FURBLE_HAS_M5PM1`, so only the
  StickS3 image clears it. The AXP boards keep M5Unified's own boot-time port
  control and, with it, the low-battery cancel in
  `Power_Class::setExtOutput()`.
- `docs/power/board-power.md` section 3.2 names the boot state on every board
  row and adds a "Grove 5 V at boot, board by board" table: StickS3 off until a
  holder acquires, StickC and StickC Plus powered, Core2 and Tough powered,
  Plus2, Core and Waveshare with no firmware control at all.
- `plans/364-ext5v-rail.md` and `src/CLAUDE.md` record the guard and why.

Consequence accepted and documented: on the StickS3 the port is off at boot and
on while a holder needs it, so a Grove device that is not the receiver sees the
port only across a holder's window. That is the direction the review endorsed
(the alternative was a persisted GROVE capability with a setting and a console
command, which this branch does not take). `RailHolder::GROVE` remains an
available hold for a future client, which the plan already states.

### B3 - `PWR_SRC` was documented as 0x07, which is `HOLD_CFG`

Verified against the vendor header: `M5PM1_REG_PWR_SRC` is `0x04` and
`M5PM1_REG_HOLD_CFG` is `0x07` (M5PM1 1.0.6, `src/M5PM1.h`). The code was right
because it calls `getPowerSource()`, which reads 0x04.

Fixed in all five places:

| Place | Change |
| :--- | :--- |
| `include/FurblePlatformPolicy.h` | `PWR_SRC` 0x04 in the `RailSupply` comment, plus a sentence naming 0x07 as `HOLD_CFG` |
| `docs/power/board-power.md` | the supply paragraph now says 0x04; it no longer contradicts the register table, which already listed `HOLD_CFG` 0x07 |
| `plans/364-ext5v-rail.md` | the interlock section and the open-gate section both say 0x04 |
| `sim/hardware_model.cpp` | the `railSupply` field comment says 0x04 |
| `src/FurblePlatform.cpp` | one line next to the four `static_assert`s names both registers and says what a wrong constant would do: read `HOLD_CFG`, always see `USB_5V`, and silently disable the interlock while every test stayed green |

### B4 - the IR hold could be released mid-burst, and `m_RailHeld` was unsynchronised

Verified. `IR::fire()` took the hold on the calling (UI) task, and the queue is
depth 1 with a zero-tick send, so a third quick trigger took the queue-full
branch and called `releaseRailHold()` while a burst from the first trigger was
still in `rmt_tx_wait_all_done()`. `m_RailHeld` was a plain `bool` written from
the UI task, the console task and the IR task. `fire()` also took the rail mutex
and up to six I2C transactions, against the non-blocking promise in
`include/FurbleIR.h`.

Fixed: both halves of the hold moved into the IR task. It acquires
`RailHolder::IR` at the top of the dequeued work, calls `transmit()`, and
releases the hold after that returns, and only when the acquire succeeded.
`m_RailHeld` and `releaseRailHold()` are deleted. `fire()` is now a queue send
with a zero-tick timeout and nothing else, so the queue-full path touches no
rail state, `fire()` cannot block the UI task, and the hold covers the burst and
nothing else.

### B5 - no target compiled the changed production file

Verified. `grep FurblePlatform.cpp tests/host/CMakeLists.txt sim/CMakeLists.txt`
returned nothing, the e2e scenarios drive `sim/FurblePlatformSim.cpp`, and
`rail-account` links no furble source, so the cut decision in
`src/FurblePlatform.cpp` was unobserved. Reproduced: with the cut decision
reverted to `false`, `rail-account` still printed PASS.

Fixed with a new linked boundary target, `boundary-platform-rail`
(`add_test(NAME boundary-platform-rail ...)`), which compiles the production
`src/FurblePlatform.cpp` and drives the real `railAcquire()`, `railRelease()`,
`setRail()` and `setDisplayOff()`:

- `tests/host/boundaries/platform_shims/M5PM1.h` is a register-bus fake in the
  style of `lib/hal/AXP2101.h` and `tests/host/hal_drivers_test.cpp`: it models
  `PWR_SRC` 0x04 and `PWR_CFG` 0x06, records every write, and can make the
  supply read fail. `setPowerConfig()` keeps the vendor's read-then-write shape
  and exposes an interleave hook.
- `tests/host/boundaries/platform_rail_support.cpp` doubles only the ESP-IDF
  services and the four furble subsystems the rail path never reaches. The rail
  accounting, the serialized `PWR_CFG` writer and the interlock are the
  production code.
- `tests/host/boundaries/platform_rail_test.cpp` asserts the three behaviours
  the review named: a refused enable performs no write (values 1, 3, 4, 5, 6, 7
  and an unreadable register), a refused cut performs no write, and an
  interleaved LED and rail update both survive. It also asserts the opposite
  directions, so an always-refuse cut and a missing serialization fail:
  battery and USB enable the rail, the last release cuts it, the exact byte
  sequence keeps the neighbouring bits, and a second writer cannot enter
  `PWR_CFG` while one update is between its read and its write.

Mutation checks against the pre-fix behaviour, all run on this branch:

| Mutation | Result |
| :--- | :--- |
| the whole pre-fix interlock: denylist `railEnableVerdict()` plus the raw cast in `railOutputPermitted()` | `boundary_platform_rail_test` reports 28 failed checks, including "a refused enable reports false" and "no PWR_CFG 0x06 write was issued" for values 4 to 7; `rail_account_test` fails its four raw cases |
| only the denylist restored, clamping kept | `rail-account` fails "raw 4 to 7 is undefined and refuses", and `boundary-platform-rail` passes, because the clamp already maps 4 to 7 to UNKNOWN before the policy sees them. Each half of the B1 fix closes the hole on its own; the two together are what the branch ships |
| the cut decision always `false` (the review's own revert) | `boundary-platform-rail` reports 7 failed checks: "the last holder release cuts the rail", "the cut is one PWR_CFG write", "the rail bit is clear", "the cut kept the neighbouring bits", "the refused cut is counted once" and "each PWR_CFG write kept the other owner's bit". `rail-account` still prints PASS, which is exactly why this target exists |
| the cut decision always `true` while a holder remains | `boundary-platform-rail` fails "a cut is refused while the infrared circuit holds the rail", "a refused release writes no register", "the rail stays powered" and "a refused setRail writes no register" |
| the interlock result ignored in `switchRailLocked()` | `boundary-platform-rail` fails "a refused enable reports false" and "no PWR_CFG 0x06 write was issued" for every refusal case |
| the serialization removed from `updatePowerConfig()` | `boundary-platform-rail` fails "a second PWR_CFG writer cannot enter the critical section" and "the LED policy cleared the LED bit" |
| a whole-byte write from a stale read in the LED path | `boundary-platform-rail` fails "the LED write kept the rail bit", "both bits survive the LED restore" and "each PWR_CFG write kept the other owner's bit" |

## Non-blocking

| # | Finding | Disposition | Evidence |
| :--- | :--- | :--- | :--- |
| N1 | The header claimed the leaf lock is never held across a delay, but the vendor driver retries with `vTaskDelay(50)` inside it. | Fixed by stating the real bound. The vendor driver retries each transaction twice with a 50 ms delay and adds 10 ms when it has to wake the PMIC; one `updatePowerConfig()` is four transactions, so the bound is about 240 ms with a failing PMIC. The header now states that and keeps the promise that matters: no event wait, no `Control` call, no lock inversion. Moving the vendor retries out is not this branch's change and would mean patching a pinned dependency. | `include/FurblePlatform.h`, `M5PM1.cpp` `_readReg`/`_writeReg`/`_checkAutoWake` |
| N2 | `fire()` blocked. | Fixed by B4. `fire()` is a zero-tick queue send with no rail mutex, no I2C and no wait. | `src/FurbleIR.cpp`, `include/FurbleIR.h` |
| N3 | Refusals logged at WARN only for the first refusal of a boot, and reached no console surface. | Fixed. A refusal now logs at WARN when the verdict changes and at DEBUG for a repeat. New read-only console command `rail status` prints the holder mask and one name per holder, the last verdict and both refusal counters. | `src/FurblePlatform.cpp`, `src/FurbleConsole.cpp`, `docs/console-commands.md`, `docs/wiki/Console-Commands.md`, `tests/host/console_commands_test.cpp` |
| N4 | `Platform::setRail()` was public and bypassed the accounting. | Fixed by requiring a holder: a switch-on is refused when no counted holder owns the rail, so it cannot leave the port powered with nobody accounting for it. It cannot be made private, because `meetsPowerBackend<Platform>()` takes `&P::setRail` from outside the class, so the requirement is enforced in the accounting instead. The simulator Platform mirrors it. | `include/FurblePlatform.h`, `src/FurblePlatform.cpp`, `sim/FurblePlatformSim.cpp`, `include/FurblePowerBackend.h` |
| N5 | `railAcquire()` could drop a pre-existing hold when the switch failed. | Fixed. It records whether this call added the bit and releases only then; a hold that was already counted belongs to a caller that still wants the rail. The simulator Platform mirrors it. | `src/FurblePlatform.cpp`, `sim/FurblePlatformSim.cpp` |
| N6 | Stale "GPS rail" naming in the board profile. | Fixed where the finding points: the `src/FurbleBoard.cpp` header comment and the `include/FurbleBoard.h` field comment now name the Port A 5 V output the column switches and point at `include/FurbleRail.h`, and the column list says "switchable EXT_5V rail". The identifier stays `gpsRail`: it is referenced by three merged plans (321, 322, 326) as the column name, and renaming it would rewrite those records for no behavioural gain. A later rename should move the plans with it. | `src/FurbleBoard.cpp`, `include/FurbleBoard.h` |
| N7 | `hwRailSupplyName()` returned `c_str()` after unlocking. | Fixed. It returns `std::string` by value and both call sites compare strings. | `sim/hardware_model.h`, `sim/hardware_model.cpp`, `sim/FurblePlatformSim.cpp` |

## False positives

| Finding | Evidence |
| :--- | :--- |
| B2 sub-claim: "On the Tough it cannot be acquired at all because `railSwitchable()` requires `Profile::gpsRail`, which is false there." | The `TOUGH` row in `src/FurbleBoard.cpp` ends with `true`, so `Board::profileFor(board_M5Tough).gpsRail` is true and `railSwitchable(Rail::EXT_5V)` returns true on a Tough. `tests/host/board_profile_test.cpp` asserts the same value and passes. The defect the review names for the AXP boards, the boot state, was real and is fixed; this part of the reasoning is not. |

## Gates

Re-run on the rebase onto master `bbe4d22c`, which is the head this record
accompanies. The counts below are this run, not the fix round at `966249d9`.

| Gate | Result |
| :--- | :--- |
| `boundary-platform-rail` (new target) | `platform-rail: PASS` |
| `rail-account` | `rail-account: PASS` |
| `console-commands` | pass, including the new `rail status` case |
| host suite (`ctest`, macOS, Apple clang 21) | `100% tests passed out of 200` |
| e2e scenarios (`sh sim/scripts/run-e2e.sh`) | `All 243 end-to-end scenarios passed.`, with `PASS ext5v-rail-holders` and `PASS ext5v-rail-interlock` |
| `python3 tools/pre_review.py --base origin/master` | `PASS: 10 gates`, 38 changed C or C++ files formatted and safety-checked |
| `sh sim/scripts/check-doc-tokens.sh` | `sim documentation tokens are present` |
| `python3 tools/check_sim_scenarios.py` | `sim scenario manifest is complete` |

The host suite was 197 tests at `e0bca1b2` and 198 at the fix round; it is 200
on the rebased head because master added two targets after the branch was cut.
No test was removed, and this branch adds `boundary-platform-rail`.

The reviewer-revert mutation was re-run after the rebase: with the cut
decision in `Platform::railRelease()` replaced by an unconditional `true`,
`boundary-platform-rail` reports 4 failed checks, naming the `PWR_CFG` reads
and writes the cut must perform, while `rail-account` still prints PASS. The
mutation table above is the fix-round run; the exact failure count moves with
where the mutation is applied, and the target fails in every form tried.

## Files changed

`include/FurblePlatformPolicy.h`, `include/FurblePlatform.h`,
`include/FurbleBoard.h`, `include/FurbleIR.h`, `src/FurblePlatform.cpp`,
`src/FurbleBoard.cpp`, `src/FurbleIR.cpp`, `src/FurbleConsole.cpp`,
`sim/FurblePlatformSim.cpp`, `sim/hardware_model.h`, `sim/hardware_model.cpp`,
`sim/shim/FurbleIR.h`, `tests/host/CMakeLists.txt`,
`tests/host/rail_account_test.cpp`, `tests/host/console_commands_test.cpp`,
`tests/host/console/console_shim.cpp`, `tests/host/boundaries/platform_rail_test.cpp`,
`tests/host/boundaries/platform_rail_support.cpp`,
`tests/host/boundaries/platform_shims/`, `tests/host/boundaries/shims/M5Unified.h`,
`tests/host/boundaries/shims/driver/gpio.h`,
`docs/power/board-power.md`, `docs/console-commands.md`,
`docs/wiki/Console-Commands.md`, `plans/364-ext5v-rail.md`, `plans/README.md`,
`src/CLAUDE.md`, `include/CLAUDE.md`, `tests/host/CLAUDE.md`, and this file.
