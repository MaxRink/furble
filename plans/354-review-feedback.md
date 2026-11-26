# 354 - Review feedback for the Core family hardware and power fixes

## Scope

This is the review-feedback record for #497 (`fix/core-family`), which corrects
the Core family capabilities, capacities, power-off paths, IP5306 boost policy
and GPS rail scope. Every finding is recorded as fixed, already fixed or a false
positive, with evidence. Line references are positions in the tree this PR
produces on master.

## PR #497

Sources: CI on the first pushed head, and an independent review of the rebased
head against the vendor documents the plan cites.

| # | Finding | Resolution | Evidence |
| ---: | :--- | :--- | :--- |
| 1 | The `m5stack-core-debug` profile no longer fits its app slot: `Error: The program size (1741009 bytes) is greater than maximum allowed (1740800 bytes)`, 209 bytes over. The profile is the dual-OTA Core debug image, so the slot cannot grow without a partition change, and CI builds it as a mandatory lane. | Fixed. The new boot-log strings were shortened. The refusal strings this finding originally paid for are gone with finding 2, which removes more. No safety gate was removed. | `tools/size_budget.json` keeps its 1,740,800 `flash_slot` entry. The lane is measured in the table under Gates below. |
| 2 | **The "honest power off" premise is false, and acting on it removes a working manual Off from a shipped board.** The branch reads `canPowerOff` as a platform fact: `Platform::canPowerOff()` returns the column, `Platform::powerOff()` refuses on the Core before the TimeKeeper flush and SD unmount, the main menu `Power off` entry is hidden and its callback guarded, the console `power off` command refuses, `Request::POWER_OFF` reports `unsupported`, `UI::doPowerOff()` logs a refusal, and a second `M5.Power.getType() == pmic_ip5306` check refuses inside the non-M5PM1 branch. The premise ("the boost stays up, so it is not a power off") is contradicted by the pinned M5Unified, by the register document, and by this branch's own 64 s decode. | Fixed. The refusal, the menu hide, the callback guard, the console gate, the request-path gate, the `doPowerOff()` gate and the `getType()` defence are removed; `Platform::canPowerOff()` is gone. `Board::profile().canPowerOff` keeps the meaning it already had at master: the gate for the automatic policies (`processAutoOff()`, `processLowBattery()`), which is false on the Core because that row reports a coarse level and no charge state. `Platform::powerOff()` keeps #500's refusal, keyed on `powerOffPath`, which is true on the Core and false only on the Waveshare, plus #494's SD-handshake refusal. | See "Finding 2 in detail" below. |
| 3 | The new PMIC cut in `Platform::powerOffUntil()` returned from a refused `M5.Power.powerOff()` **with the BM8563 countdown still armed** and its IRQ enabled. On the Tough and StickC Plus the BM8563 INT is wired to the AXP192 power key, so a stale alarm presses that key minutes after the user asked for nothing. | Fixed. The refusal branch now calls `clearBm8563Timer()` and `M5.Rtc.disableIRQ()` before returning false. | `src/FurblePlatform.cpp`, the `RTC_ALARM` branch tail. The VBUS refusal above it already did this; the new PMIC path did not. |
| 4 | **The branch's SD unmount in `powerOffUntil()` ignored the handshake result, which is the data-loss defect #494 had just fixed.** #494 made `SD::powerOff()` return bool and made `Platform::powerOff()` refuse the cut when the writer does not acknowledge; its own plan records that the timed shutdown path skips the handshake. This branch added `SD::getInstance().powerOff()` to both timed branches with the void return of the old API, so after the rebase that call discarded the result and the rail would still drop on a wedged writer. | Fixed. Both timed branches now test the result and refuse: the M5PM1 branch clears the wake timer and the RTC RAM marker and restores the watchdog before returning false, and the BM8563 branch clears the countdown and the alarm IRQ. A refused timed cut leaves the card mounted and the device awake. | `src/FurblePlatform.cpp`, the two `if (!SD::getInstance().powerOff())` blocks in `powerOffUntil()`. `SD::powerOff()` returns bool at master (`src/FurbleSD.cpp`, #494). |

## Reconciliation with the merged power-off split (#500, plan 349)

#500 merged as `5c3b85bc` while this review was running and split the question
into two columns: `canPowerOff` is the UI policy gate and `powerOffPath` is
whether the platform can remove power at all. Plan 349 left a "Reconciliation
required in #497" list; this is that list, against the merged code:

| Merged behaviour | This branch after the rebase |
| :--- | :--- |
| `include/FurbleBoard.h` carries `canPowerOff` (the UI offers the Auto off and Low battery policies) and `powerOffPath` (the platform can cut power) | Unchanged, both merged comments kept. This branch's replacement wording for `canPowerOff` is dropped. |
| The `CORE` row keeps `canPowerOff` false and `powerOffPath` true | Kept, with this branch's three row corrections applied on top: Core capacity 110, Core2 500, and the Tough SD slot, speaker, BT1-backed RTC and RTC alarm wake. No row changes either power-off column. |
| `Platform::powerOff()` refuses on `PlatformPolicy::powerOffAction(Board::profile().powerOffPath)`, so only the Waveshare refuses, and #494's SD-handshake refusal follows it | Both refusals are the merged ones, untouched. `Platform::canPowerOff()` and the `M5.Power.getType() == pmic_ip5306` defence this branch had added are gone, so there is no second predicate. |
| The manual Off works on every board with a path: menu entry, callback, `Request::POWER_OFF`, `doPowerOff()` and the console command | All restored to the merged behaviour. `addPowerMenu()`, `processAutoOff()` and `processLowBattery()` read `Board::profile().canPowerOff`, and `board-profile` pins both columns apart by name: the Core offers no automatic policy and keeps its deferred path. |
| `include/FurblePlatform.h` states the real refusal and the real cut | `powerOff()` names the `powerOffPath` refusal, the SD-handshake refusal and the deferred IP5306 cut; `powerOffUntil()` states what a refused handshake unwinds. No comment claims the Core cannot remove power. |
| The simulator mirrors `powerOffPath` rather than an unconditional `canPowerOff()` | The unconditional seam this branch added is gone; the merged mirror is what remains, so the model and the firmware agree. |

The Core-only `esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER)` in
`UI::doPowerOff()` is reachable again instead of sitting behind an unreachable
gate, which is the concrete form of "the manual Off works".

## Finding 2 in detail

The claim under test is the branch's three-word summary: "the IP5306 has no
shutdown command, so the Core cannot cut its own power". The first clause is
true. The second does not follow, and the branch's own evidence contradicts it.

The manual Off on the Core runs `Platform::powerOff()` -> `M5.Power.powerOff()`,
which in the pinned M5Unified 0.2.20 (`/.pio/libdeps/m5stack-core-debug/
M5Unified`, `library.json` version 0.2.20) does three things:

1. `Power_Class::powerOff()` calls `M5.Display.sleep()` and `waitDisplay()`
   before anything else, so the panel and its backlight are off for the rest of
   the call (`src/utility/Power_Class.cpp`, `powerOff()`).
2. `_powerOff(false)` takes the `pmic_ip5306` case and calls
   `Ip5306.setPowerBoostKeepOn(false)` (`Power_Class.cpp:1288`), which clears
   `BOOST_OUT_BIT = 0x02` (`REG_SYS_CTL0` bit 1) in
   `src/utility/power/IP5306_Class.cpp`.
3. `_powerOff` then reaches `esp_deep_sleep_start()` (`Power_Class.cpp:1437`).

Bit 1 is the "BOOST output normally open" bit. M5Unified's own comment on the
`board_M5Stack` register table (`Power_Class.cpp`, the `0x00` entry) says to set
it for deep sleep and the RTC timer **"because the power supply is stopped when
the load is light"**, and `Power_Class::deepSleep()` and `lightSleep()` set it
themselves for exactly that reason (`Power_Class.cpp:1517`, `:1649`). Clearing
it and sleeping is the vendor's cut mechanism, not a wake-less sleep with the
boost up. The window is `SYS_CTL2` bits 3:2, which the same table writes as
`0x6C` (`11`), the 64 s setting in the register document's encoding
(`00` 8 s, `01` 32 s, `10` 16 s, `11` 64 s). With the panel asleep the load is
far below the 45 mA light-load threshold, so the boost is removed and the unit is
off.

That is also what this branch says elsewhere. Its verification table reads "The
shipped Core therefore cuts at 64 s", its `keepIp5306BoostOn()` justification
rests on the same cut, and `docs/power/board-power.md` recorded the deferred cut
at master for PM-07 before this branch made the code disagree with it.

**Verdict on the #497 / #500 contradiction.** #500 (`fix/waveshare-board-row`,
plan 349) stated the opposite answer for the same board, kept a `powerOffPath`
column true for the Core, and listed what #497 must change when it is rebased
onto that work. It merged as `5c3b85bc` while this review was running, so the
rebase implements that list rather than anticipating it. **#500 is right and #497's premise is wrong.** The Core can power
off: it is a deferred cut with a 64 s window instead of an immediate PMIC
command, which is the only real difference from an AXP192 `powerOff()`. It is
not "a wake-less deep sleep with the boost up".

The `canPowerOff` column is the wrong place for the question anyway. It is false
on the Core because that row has a four-level gauge and no charging state, so an
automatic off policy has nothing to act on; that is why `addPowerMenu()`,
`processAutoOff()` and `processLowBattery()` read it. #500 splits the two
questions into `canPowerOff` and `powerOffPath`, and this branch now leaves
`canPowerOff` alone, so the rebase onto #500 only has to add the new column.

## Finding 4 in detail

#494 (`fix/sd-writer-ack`) landed while this review was in progress and changed
the contract this branch had just started to use: `SD::powerOff()` queues the
unmount, waits for the writer task, and now reports whether the writer
acknowledged. `Platform::powerOff()` refuses before the PMIC when it did not.
#494's plan also states the scope plainly: "the timed shutdown and restart paths
skip it".

This branch adds the unmount to both timed branches, so leaving the old void
call in place would have shipped the exact failure #494 removed, on the path
#494 left alone: a mount or FAT flush that outlasts the wait, the timer or the
PMIC cut, and a GPX track left open. Both call sites now check the result. The
M5PM1 branch unwinds the wake timer and the RTC RAM marker and restores the
watchdog, so a refused cut does not leave an armed timer behind; the BM8563
branch clears the countdown and the alarm IRQ for the same reason.

## Checked and not findings

- **IP5306 light-load decode.** `include/FurbleIP5306.h` and the host
  `ip5306-policy` test map `SYS_CTL2` bits 3:2 the way the register document
  V1.21 states them: `00` 8 s, `01` 32 s, `10` 16 s, `11` 64 s. The
  non-monotonic order is the document's, not a transcription slip.
- **M5Unified writes `0x6C`, so the shipped Core cuts at 64 s.** `0x6C` is
  `0b01101100`; bits 3:2 are `11`. The same table's comment repeats the same
  four-way table. True.
- **`SYS_CTL0 = 0x31` clears the keep-on bit.** `0x31` is `0b00110001`, bit 1 is
  clear, and `0x31 | 0x02 == 0x33`, which the test pins. True.
- **`M5.Power.Ip5306` and `readRegister8`.** The member exists only in the
  `#else` arm of the `Power_Class.hpp` member block, which is the classic-ESP32
  arm (S3, C3, C6, C61 and P4 all have their own arm above it), so the
  `#if defined(CONFIG_IDF_TARGET_ESP32)` guard is required and correct.
  `IP5306_Class` inherits the public `const std::uint8_t readRegister8(uint8_t)`
  from `I2C_Device` (`I2C_Class.hpp`), so `holdBoostOn()` is well formed and
  reads back through the same address the write used.
- **Tough capabilities.** The vendor page lists an onboard microSD slot
  (MISO G38, MOSI G23, SCK G18, CS G4), a `NS4168 16-bit I2S amplifier + 1W
  speaker` on BCLK G12 / LRCK G0 / DATA G2 with `SPK_EN` on `AXP_IO2`, and
  `BM8563-RTC clock, supports RTC timer wake-up` with `INT` on `AXP_PWR`. The
  Tough schematic text carries `BT1 / BATTERY_RTC / RTC_BAT`, `U8 / SY7088` with
  its output on `BUS_5V`, and `BUS_5V` into `J4 PORT-A` pin 2 and the M-Bus
  `J7`. M5Unified configures the same speaker pins for `board_M5Tough` through
  `_speaker_enabled_cb_core2()`, which drives `AXP192 GPIO2`. Every Tough claim
  in the row is confirmed by at least two independent vendor sources.
- **Capacities.** `docs.m5stack.com/en/core/basic` gives `Battery 110 mAh @
  3.7 V` and its revision table gives `2020.3 Battery capacity changed from
  150mAh to 110mAh`. `docs.m5stack.com/en/core/Core2` gives `Lithium Battery
  500mAh@3.7V` and `2023.10 Lithium battery capacity changed from 390mAh to
  500mAh`. The M5Tough README gives `The battery capacity has been upgraded to
  390mAh`. All three rows are right, and the Tough row is unchanged.
- **Core2 stays `TimedWake::NONE`.** The same Core2 revision table says `2023.2
  Removed RTC button battery`, and there is no revision register to read. The
  conservative answer is correct, and `rtcBatteryBacked` staying true is safe
  because the BM8563 voltage-low flag is the guard TimeKeeper applies on a
  backed row.
- **GPS rail scope.** The Tough schematic ties the SY7088 output to one
  `BUS_5V` net feeding Grove Port A and the M-Bus, so `Rail::GPS` really is the
  whole external 5 V net on the Core2 and Tough. Comment and documentation only,
  no code path.
- **Charge-current follow-ups.** `0x24 = 0xC1` is 150 mA
  (`I = 0.05 + b0*0.1 + ...`, b0 = 1) against `0x21 = 0x09`, whose bits 7:6 are
  `00`, the 200 mA battery-side stop threshold, so current-based termination
  cannot trip. The AXP192 step for `setChargeCurrent(390)` is the 360 mA step.
  Both stay recorded rather than changed, as the plan says. No charge profile
  changed in this PR.
- **Refusal ordering.** While the refusal existed it did come first: it was the
  first statement of `Platform::powerOff()`, before `TimeKeeper::getInstance()
  .flush()` and `SD::getInstance().powerOff()`, and the console refused before
  queueing. The ordering claim was true; the refusal itself was the problem
  (finding 2). The timed-wake path keeps its own ordering: every validation
  refusal returns before the SD unmount, so a refused timed cut leaves the card
  mounted.
- **`m_ConsoleResult` has no reader.** Pre-existing; the branch documented it
  rather than extending the pattern.
- **StickS3 SD unmount in the `PMIC_TIMER` branch.** New in this branch and
  outside its stated Core-family scope, but it is placed after every failure
  return and before `m_M5PM1.shutdown()`, so a refused shutdown now leaves the
  card unmounted with the device awake. Accepted: `SD::handlePoint()` remounts
  lazily and opens a new track, which costs a track boundary, while the
  alternative loses the filesystem when the rail does drop.
- **No em-dashes were added.** `CONTRIBUTING.md` forbids them; `tools/
  pre_review.py` passes.

## Gates

Every gate below is re-run on the rebased head that carries findings 2 and 3.

| Gate | Command | Result |
| :--- | :--- | :--- |
| Host suite | `ctest --test-dir /tmp/rv497-build --output-on-failure -j 6` | `100% tests passed out of 208`, 88 s |
| Pre-review | `python3 tools/pre_review.py --base origin/master` | `PASS: 13 gates` |
| Doc tokens | `sh sim/scripts/check-doc-tokens.sh` | `sim documentation tokens are present` |
| Settings contract | `python3 tools/check_settings_contracts.py --base origin/master --head HEAD` | `PASS`, 0 FAIL, 6 WARN (pre-existing) |
| Doc citations | `python3 tools/check_doc_citations.py` | 0 errors of 2863 citations in 157 files |
| Format | `python3 tools/check_format.py --base origin/master` | 14 changed files formatted |
| Simulator | `sh sim/build.sh` on the branch tree | builds, so `src/FurbleUI.cpp` and `sim/FurblePlatformSim.cpp` compile after the refusal removal |
| Firmware | `build (m5stack-core-debug)` and the release matrix in CI | CI is the authority |
