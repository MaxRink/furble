# 349 - The Waveshare ESP32-S3-ETH board row

Status: implemented on `fix/waveshare-board-row` (code, host tests, simulator
model and docs). No hardware run: no Waveshare ESP32-S3-ETH was on the bench,
so the firmware build and host gates are the only evidence here.

Related: [322](implemented/322-hardware-abstraction.md) (the board profile table and the
`canPowerOff` / `powerOffPath` split), [42](implemented/42-waveshare-eth-node.md)
(the board, its pin map and the M5GFX pinning), [320](320-rtc-use.md) (why an
RTC part matters for the wall clock), [321](implemented/321-ublox-m10-driver.md) (the GPS
receiver work).

## Why

An independent board-level verification of the Waveshare ESP32-S3-ETH found the
board identity and its simulator model wrong in three ways.

1. **The board resolved to the unknown-board row.** `Board::profileFor()` had no
   case for `m5::board_t::board_M5StampS3`, so the board fell through to
   `UNKNOWN`. `include/FurbleBoard.h` describes that as "a no-hardware row",
   which is the right answer for a board nobody has identified and the wrong one
   for a shipped board. That row also reports `canPowerOff` true, and
   `Platform::powerOff()` called `M5.Power.powerOff()` and returned true
   unconditionally on every board without the M5PM1.
2. **Power off cannot work on this board.** There is no PMIC, no battery and no
   switchable rail. In the pinned M5Unified 0.2.20,
   `Power_Class::_powerOff()` reaches neither an AXP192, an AXP2101 nor an M5PM1
   branch (`src/utility/Power_Class.cpp:1260`, `_pmic` stays `pmic_unknown` at
   `src/utility/Power_Class.cpp:111`), so `use_deepsleep` stays true and the call
   ends at `esp_deep_sleep_start()` (`src/utility/Power_Class.cpp:1431`). No wake
   source is armed either: the default branch arms ext0 only when
   `_wakeupPin < GPIO_NUM_MAX` (`src/utility/Power_Class.cpp:1365`) and
   `board_M5StampS3` never sets it, so it keeps the 255 default
   (`src/utility/Power_Class.hpp:286`). M5Unified's `pin_name_t::power_hold` entry is
   255 for the board too (`M5Unified.cpp:274`, the table's `board_unknown`
   entry), so the pin-pulse path cannot run. Firmware therefore reports success
   for a chip that stays asleep until a power cycle. This is the worst of the
   three: the device looks off, is still powered, and nothing short of a power
   cycle brings it back.
   Reachability, stated because it limits what this change can fix: the shipped
   `waveshare-s3-eth` image is `FURBLE_NO_DISPLAY`, so `UI::doPowerOff()` is not
   compiled in and the console `power off` command refuses on that build
   (`src/FurbleConsole.cpp:1920`). The refusal below is a profile invariant, not
   the fix for a failure a user meets on today's image: it keeps any later
   caller, and any UI image flashed onto this board, from latching a half-off
   state.
3. **The simulator model described a different board.** `sim/hardware_model.cpp`
   claimed `wifi false`, `rtc true` and `sd true` for the
   `waveshare-s3-eth` profile, and `sim/scenarios/e2e/hw-profile-waveshare.txt`
   asserted `hw.wifi no`, `hw.rtc yes` and `hw.sd yes`. The board has Wi-Fi and
   BLE 5 on the SoC, no RTC part on any schematic sheet, and a microSD slot that
   firmware does not mount.

A dedicated row is possible because M5Unified already detects this board:
`M5Unified.cpp` classifies an unknown QFN56 ESP32-S3 as `board_M5StampS3` (plan
42, section "M5GFX autodetect pin"), and `platformio.ini` pins
`M5GFX_BOARD=board_t::board_M5StampS3` for the env, so the identity does not
depend on a probe.

## Board facts

| Property | Fact | Source |
| :--- | :--- | :--- |
| SoC and radios | ESP32-S3R8, Wi-Fi and BLE 5 both stay available | plan 42, section "Waveshare ESP32-S3-ETH" |
| Wired network | WIZnet W5500 over SPI, 10/100 | plan 42, sections "W5500 SPI pins" and "esp_eth W5500 bring-up" |
| PMIC, battery, rail | none. No PMIC, no cell, no switchable rail | plan 42 board list ("No display, no IMU, no RTC, no battery") |
| Power off | `M5.Power.powerOff()` ends at `esp_deep_sleep_start()`. `POWER_HOLD` is 255 for `board_M5StampS3` | M5Unified 0.2.20 `src/utility/Power_Class.cpp` `_powerOff()`, `M5Unified.cpp` `_pin_table_other1` |
| RTC | no RTC part and no backup cell | plan 42 board list; no RTC part on the schematic sheets |
| IR, IMU, speaker, vibration | none | plan 42 board list |
| microSD | 1-line mode on GPIO 4 to 7. No card-select pin is mapped, so nothing mounts it | pin range from the vendor schematic; plan 42 board list ("microSD in 1-line mode") only records the slot. `M5Unified.cpp:202` `_pin_table_sd` has no `board_M5StampS3` row and its `board_unknown` row is 255, so `getPin(sd_spi_cs)` returns -1 (`M5Unified.hpp:259`, `int8_t`) and `src/FurbleSD.cpp:751` already reports the slot unsupported |
| GPS rail | no Grove port, no switched Port A rail. GPS is optional over the Pico header | plan 42, section "Subsystems off or stubbed" |
| Supplies | the optional PoE module feeds `POE_5V`, the same net as USB VBUS. No sense or negotiation signal | plan 42, section "Power-source observability" |

## The row

`src/FurbleBoard.cpp` gains `WAVESHARE`, returned for
`m5::board_t::board_M5StampS3`:

| Column | Value | Why |
| :--- | :--- | :--- |
| name | `Waveshare` | boot info block; this image has no display, so it reaches logs only |
| battery caps, capacity | `{false, false, false, false}`, 0 | same as the no-hardware row: no PMIC and no gauge, so no fake percentage |
| RTC backed | false | no RTC part |
| IR pin | -1 | no emitter |
| motion wake | `NONE`, -1 | no IMU |
| timed wake | `NONE` | nothing to wake it: no PMIC timer, no RTC alarm |
| hold pin | -1 | no switchable rail |
| PMIC click streak | false | no PMIC key |
| SD slot | false | no card select mapped; the image keeps `FURBLE_HAS_SD` |
| power policies | false | nothing to power off, so no auto-off or low-battery policy |
| power-off path | false | no PMIC, no cell and no rail to cut |
| modem sleep | false | a mains node has no battery life to save |
| brightness floor | 32 | same default as the other non-StickC rows, a multiple of the slider step |
| sound, light, vibration, LED pin | false, false, false, -1 | no speaker, no vibration motor. The WS2812 on GPIO 21 is not driven by `Feedback`, which only pulses a plain GPIO |
| GPS rail | false | no switched Port A rail |

The `UNKNOWN` row is unchanged. Its comment no longer claims the Waveshare
board, and its `canPowerOff` and `powerOffPath` columns stay true, so an
unrecognised board keeps the generic attempt it made before.

## Platform refusal

`Platform::powerOff()` now asks `PlatformPolicy::powerOffAction()` first
(`include/FurblePlatformPolicy.h`). A row without a power-off path logs

    Power off refused, the <name> profile has no power-off path

and returns false before `TimeKeeper::flush()` and the SD unmount. The order
matters: the SD unmount is one way, so a caller that must stay alive would be
left without its card. Both callers log the failure as well, so the refusal is
visible in the UI log and in the console.

Callers, all checked:

- `UI::doPowerOff()` (`src/FurbleUI.cpp:13412-13465`) already treated a false return
  as "stay alive": it logs `Power off failed, resuming.`, restores the PM1
  watchdog and clears `m_PoweringOff`. No change was needed.
- `Platform::powerOffUntil()` forwards a `seconds == 0` request to `powerOff()`
  and propagates false. Its timed path already fails closed for a row with
  `TimedWake::NONE` (`Timed wake is unsupported on this board`,
  `src/FurblePlatform.cpp:833`), so the Waveshare row cannot reach a timed
  sleep either.
- `UI::processAutoOff()` and `UI::processLowBattery()` both return early when
  the row's `canPowerOff` is false, so the timer cannot reach a refusal on the
  Waveshare row.
- The console `power off` command refuses outright on `FURBLE_NO_DISPLAY`
  builds (`src/FurbleConsole.cpp:1920`), which is every Waveshare image.
- The simulator `Platform::powerOff()` mirrors the gate, so `platform.power_off`
  stays `no` after a refused request instead of claiming success.

### Size: the Core debug OTA slot

`m5stack-core-debug` keeps two OTA slots, so its app partition is 1,740,800
bytes, and before `#518` it ran within a few hundred bytes of that ceiling: the
first head of this branch pushed `firmware.bin` to 1,740,928 and CI failed
`build (m5stack-core-debug)` with "flash 1,740,928/1,740,800". This branch's own
addition is the Waveshare row, its `profileFor()` case and one log line.

`#518` ("ci(flash): compile the Core OTA debug lane at ESP_LOG_INFO") landed
while this branch was open and gives that lane about 25 KB of program size by
compiling furble's own translation units at `ESP_LOG_INFO`, so the log line now
fits and nothing here is compacted to pay for it. On the rebased head the lane
is measured in the table below. No partition, budget entry or gate was changed.

### Deviation: a new `powerOffPath` column

The refusal reads a new `powerOffPath` column, not `canPowerOff`. The two
columns answer different questions, and the difference matters on the Core as
much as on this board.

`canPowerOff` has one meaning in the code. `src/FurbleUI.cpp` reads it in
`processAutoOff()` and `processLowBattery()` to stop the automatic policies, and
on the settings page to hide the Auto off, Low battery and auto-off-on-charger
controls. It is false on the Core because that row reports a coarse level and no
charging state (`{true, false, false, false}`), so an automatic policy has
nothing to act on. The flag was first set false there out of doubt about the
IP5306 power-off path (`1e73eca2`, "Add auto power off and low battery policy",
"its IP5306 has no reliable software power-off path"), and the comment above the
gate said so until this change. That doubt is what `powerOffPath` now carries.

The Core keeps a path. In the pinned M5Unified 0.2.20, `board_M5Stack` sets
`_pmic = pmic_ip5306` (`src/utility/Power_Class.cpp:587`) and `_powerOff()` calls
`Ip5306.setPowerBoostKeepOn(false)` (`src/utility/Power_Class.cpp:1288`), which
clears `BOOST_OUT_BIT` in `REG_SYS_CTL0` (`src/utility/power/IP5306_Class.cpp:112`).
`Platform::powerOff()` is the manual Off path on that board, and both
`m5stack-core` power-off scenarios (`sim/scenarios/bughunt/power-off-state-matrix.txt`
and `power-off-during-connect-hang.txt`, both listed for `m5stack-core` in
`sim/scenarios/manifest.json`) assert `platform.power_off yes` after
`action nav power_off`.

Gating the refusal on `canPowerOff` would have changed both in one step: the Core
would refuse a manual Off, `UI::doPowerOff()` would log `Power off failed,
resuming.` and clear `m_PoweringOff`, and the two scenarios would fail with
`platform.power_off no`. That is a behavior change on a shipped board made to
serve a new one.

What those scenarios do not prove is that the IP5306 cuts the rail:
`platform.power_off` is the simulator seam, and it reported success for every
non-refused row before this change as well. They pin the intended behavior and
the regression surface, not the silicon. If bench evidence ever shows the IP5306
cut failing, the fix is to set `powerOffPath` false on the Core row and update
those two scenarios in the same change, with that evidence attached.

`powerOffPath` is true for every existing row, including the unknown-board row,
and false only for the Waveshare row. `tests/host/platform_power_off_test.cpp`
asserts both facts: the Core reports no policies and still has a path, and
exactly one row refuses.

### Reconciliation required in #497 (`fix/core-family`)

`fix/core-family` (#497) is open on another branch and asserts the opposite
answer for the same board. It adds `Platform::canPowerOff()` returning the
`canPowerOff` column, refuses at the top of `Platform::powerOff()` when it is
false, hides the manual Off menu entry, refuses the console `power off` command
and adds a second `M5.Power.getType() == pmic_ip5306` refusal inside the
non-M5PM1 branch. Its `src/FurbleBoard.cpp` comment states the position:
"the IP5306 has no shutdown command, so the Core cannot cut its own power".

The first clause is true and the second is not. The Core has no shutdown
command, but it does have a cut path, and this plan's verdict is that the row
keeps `powerOffPath` true:

1. `Power_Class::powerOff()` calls `_powerOff(false)`
   (`src/utility/Power_Class.cpp:1728`). On the ESP32, `case pmic_ip5306` runs
   `Ip5306.setPowerBoostKeepOn(withTimer)`, so `withTimer` false clears the bit
   (`:1288`). `IP5306_Class::setPowerBoostKeepOn(false)` reads `REG_SYS_CTL0`
   and clears `BOOST_OUT_BIT`, `0x02` (`src/utility/power/IP5306_Class.cpp:112`).
   `_powerOff` then reaches `esp_deep_sleep_start()` (`:1437`).
2. M5Unified's own comment on that bit says to set it for DeepSleep and the RTC
   timer "because the power supply is stopped when the load is light". Clearing
   it while the ESP32 sleeps is the vendor's cut mechanism, not a wake-less
   sleep with the boost up.
3. The light-load window is `REG_SYS_CTL2` `0x02 = 0x6C`, bits 3:2 `11`, which
   is 64 s (`src/utility/Power_Class.cpp`, and #482 corrected every live document to
   that figure). `SYS_CTL1` `0x01 = 0x1D` sets short-press boost (`1`) and
   leaves boost-after-VIN-removal `0`, so the power button restarts the board
   and unplugging does not.
4. `docs/power/board-power.md` already says it for the Core: "The manual `Off`
   path clears the keep-on bit and enters ESP deep sleep. If the load then falls
   below 45 mA, the IP5306 removes the boost after its 64 s low-load timeout. It
   is not an immediate PMIC off". PM-07 and PM-12 in the same file call it "its
   off path". `plans/98-power-optimization-audit.md` says the converter
   "hard-powers-off". `src/FurbleUI.cpp` already tells the user "Power off is
   final. Press the power button to wake."
5. `docs/power/board-power.md` also records the delay honestly, which is the
   only real difference from an AXP192 `powerOff()`: no immediate command, a
   64 s window, and a residual-load condition that is still unmeasured.
   `plans/implemented/13-auto-off-low-batt.md:227` ("the boost converter may
   keep running") is the hedge that #497 hardened into a refusal, and it omits
   the `setPowerBoostKeepOn(false)` write directly above the sleep.
6. #497's own verification table says "The shipped Core therefore cuts at
   64 s", which contradicts its gate.

What #497 must change when it is rebased onto this:

- `include/FurbleBoard.h`: keep the two columns. `canPowerOff` stays the UI
  policy gate and `powerOffPath` answers the platform question; do not restore
  the "a software path actually removes power" meaning for `canPowerOff`.
- `src/FurbleBoard.cpp`: the `CORE` row needs a `powerOffPath` value inserted
  at the new column position (true), and every row that branch edits is
  positional, so re-read each one. Replace the "cannot cut its own power"
  comment with the cut description above.
- `src/FurblePlatform.cpp`: gate the refusal on
  `PlatformPolicy::powerOffAction(Board::profile().powerOffPath)`. Drop
  `Platform::canPowerOff()`, or reduce it to the `powerOffPath` column, and drop
  the extra `M5.Power.getType() == pmic_ip5306` refusal in the non-M5PM1
  branch.
- `include/FurblePlatform.h`: the `canPowerOff()` doc comment and the
  `powerOff()` doc comment both state that the Core cannot remove power.
- `sim/FurblePlatformSim.cpp`: that branch's `Platform::canPowerOff()` returns
  true unconditionally, which is what kept `platform.power_off yes` passing on
  `m5stack-core` in `power-off-state-matrix.txt` and
  `power-off-during-connect-hang.txt` while the firmware side refused. Mirror
  `powerOffPath` as this plan does, so the model and the firmware agree.
- `src/FurbleUI.cpp` and `src/FurbleConsole.cpp`: hiding the Off entry and
  refusing the console command removes a working manual Off from a shipped
  board. Keep the `canPowerOff` policy gate only.
- `plans/354-core-family-fixes.md`: the "Honest power off" section and the
  64 s verification row have to be made to agree.

`keepIp5306BoostOn()` and the `canTurnDisplayOff()` fallback in that branch are
unaffected: they hold the boost bit while the device is running, which is the
opposite end of the same register from the power-off clear.

This plan does not modify #497 or its branch. The note exists so the owner has
the evidence and the exact list before rebasing.

## Simulator model

- `sim/hardware_model.cpp`: the `waveshare-s3-eth` profile now reports
  `wifi true`, `rtc false` and `sd false`. The new `vbus` and `poe` capability
  columns carry `hw.vbus` and `hw.poe`. `vbus` mirrors
  `FURBLE_HAS_VBUS_SENSE`, which is 1 on every image except this one
  (`include/FurbleBoardCaps.h:52`); `poe` is false on every board today because
  no image has a PoE sense line, and it exists so the Waveshare row can assert
  that none was invented. Both keys are documented in `docs/sim.md`.
- `hw.sd` follows the firmware profile rather than the physical slot. The
  comment on the profile and on the scenario line record the divergence, so a
  later change that mounts the card has to update both.
- `sim/scenarios/e2e/hw-profile-waveshare.txt` asserts `hw.wifi yes`,
  `hw.rtc no`, `hw.sd no`, `hw.vbus no` and `hw.poe no`, each with a comment
  that gives the reason.

`rtc` is an ambiguous field name. On some boards it means the SoC RTC domain,
which every ESP32-S3 has and which furble does use for `RTC_NOINIT` crash state
(plan 320). In this profile and in the `hw.rtc` key it means a separate RTC part
with a backup cell, the BM8563 or PCF8563 class of chip, and this board has
none. The scenario comment says so.

## Console `eth status`: not implemented

Plan 42's file list asks for an `eth status` command that prints link, IP, speed
and duplex, mirroring `wifi status`. It does not exist: `src/FurbleConsole.cpp`
has no `eth` command, and no doc claims one. `docs/console-commands.md` and the
README describe only the `status` battery behaviour for this board. That line in
plan 42 now carries a note that it is unimplemented, and this plan records it as
outstanding work with no owner.

## Not in this change

The Waveshare pinning and Ethernet behaviour are untouched. Three nearby defects
belong to separate changes, so a reviewer should not expect them here:

- **W5500 reset timing.** The PHY reset is the `esp_eth` `reset_gpio_num = 9`
  line (`src/FurbleEthernet.cpp:192`, plan 42 section "esp_eth W5500
  bring-up"). Nothing in this change alters the reset pulse or its timing.
- **M5.begin probe order.** Plan 42 keeps "M5.begin() on a bare S3-ETH is the
  one unproven assumption" open and asks hardware gate 2 to confirm which board
  M5Unified settles on. This change trusts the resolved identity but does not
  change the probe order or skip `M5.begin()`.
- **M5GFX board pinning.** `platformio.ini:88` keeps
  `-DM5GFX_BOARD=board_t::board_M5StampS3` and `post:patches/m5gfx_board.py` is
  unchanged. Plan 42 section "M5GFX autodetect pin" remains the reference.
- **GPS pin overlap.** Plan 42 records that M5Unified maps Ex_I2C to GPIO 15/13
  on a StampS3 match and that GPIO 13 is the W5500 SCLK. This change does not
  move either pin.

## Verification

`fix/waveshare-board-row`, rebased onto `origin/master` `a8082cc1` before the
gates below ran. Seven rebase passes were needed because master moved through
files this branch touches. The first two conflicted in plans documents only:
`plans/README.md`, where this plan's row and the new 328 row sit next to each
other, and `plans/implemented/322-hardware-abstraction.md`, where this plan's follow-up
section and the T-Beam verification pass both append to the end. Both documents
keep both sides: the rows stay in number order and the later T-Beam section
precedes this plan's follow-up.

The third pass, onto `3284b617`, conflicted in five files, and every resolution
keeps both sides:

- `docs/supported-hardware.md`: this branch rewrites the `waveshare-s3-eth` table
  row and #499 appends its pinning clause to the same row. The merged row is
  this branch's row with "pinned to the display-less StampS3, so no M5 probe
  touches the W5500 pins" folded into the first sentence. This branch's
  paragraph below the table is unchanged by #499 and stays.
- `docs/sim.md`: #502 expanded the same `hw.*` and `pmic.*` key paragraph. The
  merged paragraph keeps #502's `hw.rtc_capability`, `hw.fail_closed`,
  `pmic.pwr_cfg`, `pmic.pwr_cfg_lost`, `pmic.watchdog_cycles`, `rail.*` and
  `ir.*` text and adds this branch's `hw.vbus` and `hw.poe` with their
  explanation.
- `sim/hardware_model.cpp`: #502 added `RTC_FACTS` and pinned `.rtc = false` on
  the StickS3, Core, headless and Waveshare rows; this branch adds the `.vbus`
  and `.poe` columns. Every row keeps #502's `.rtc` value and gains
  `.vbus`/`.poe`, and the merged key dispatch answers `rtc_capability`,
  `fail_closed`, `vbus` and `poe`.
- `sim/scenarios/e2e/hw-profile-waveshare.txt`: both sides assert on `hw.rtc`.
  The merged block keeps #502's `assert hw.rtc_capability match` and this
  branch's `assert hw.vbus no` and `assert hw.poe no`.
- `plans/README.md`: both sides add a row to the same table. Both rows stay.

The fourth pass, onto `c644f39e`, met `#507` (the modelled battery-backed RTC,
formerly `feat/455-sim-rtc`) plus `#512` and `#496`. It conflicted in three
files:

- `sim/hardware_model.cpp`: `#507` adds `rtcBatteryBacked` and `timedWake` to
  `board_caps_t` after `.rtc` on all nine rows, in the same place this branch
  adds `.vbus` and `.poe`. The merged declaration order is
  `rtc, rtcBatteryBacked, timedWake, vbus, poe`, and every row keeps `#507`'s
  RTC values and gains this branch's two columns in that order, so the
  designated initializers stay in declaration order.
- `docs/sim.md`: `#507` extends the same `hw.*` key list with `hw.rtc_model`,
  `hw.rtc_battery`, `hw.rtc_clock`, `hw.rtc_volt_low`, `hw.rtc_epoch` and
  `hw.timed_wake`. The merged paragraph keeps all of those and this branch's
  `hw.vbus` and `hw.poe` explanation.
- `plans/README.md`: `#507` and `#512` add rows to the same table as this
  branch's 349 row. All rows stay.

The fifth pass, onto `81139818`, merged cleanly: `#511`, `#518` and `#519`
landed in files this branch does not edit, though `#511` added the citation gate
and `#518` gave the Core debug lane its room back (see "Size: the Core debug OTA
slot" above). The citation gate then failed on this tree for two reasons, both
fixed here:

- This branch's edits move lines, and the gate pins `file:line` citations, so
  five citations in other documents were re-pointed: `docs/components/board-map.md`
  (`include/FurbleBoard.h`), `plans/implemented/34-ota-partitions.md`,
  `plans/implemented/11-adaptive-tx-power.md` (`src/FurbleUI.cpp`) and
  `plans/implemented/13-auto-off-low-batt.md` plus two in
  `plans/implemented/26-pm1-watchdog.md` (`src/FurblePlatform.cpp`,
  `src/FurbleUI.cpp`).
- This plan's own M5Unified citations now use the `src/utility/` external prefix
  that `tools/doc_citations.json` declares, instead of a bare `utility/` path
  that resolves to nothing in the checkout.

The sixth pass, onto `b56acc88`, conflicted in `src/FurbleBoard.cpp`,
`tests/host/board_profile_test.cpp` and `include/CLAUDE.md` plus the citation
documents. `#505` added the driven-stage columns (`ledActiveLow`, `irMarkLow`)
and the `FurblePinShare.h` ownership contract, which reshaped every profile row
and the expected table. The merge keeps those columns and inserts `powerOffPath`
after `canPowerOff` in every row, so the two positional initialisers stay in
step, and `include/CLAUDE.md` keeps #505's polarity and pin-share paragraph and
adds the `canPowerOff` / `powerOffPath` explanation. The citation numbers in
`docs/components/board-map.md` and `plans/implemented/26-pm1-watchdog.md` were
re-derived for the merged line numbers.

The seventh pass, onto `a8082cc1`, was a clean rebase: `#484` archived eleven
completed plans, and git followed the renames, so this branch's edits to plans
322 and 42 landed at their `plans/implemented/` paths. The two links in the
"Related" line above were re-pointed to the archived paths.

`tools/check_m5gfx_pinning.py` failed on the first `pre_review` run in this
worktree because `.pio/libdeps/waveshare-s3-eth` still carried only the older
`m5gfx_board.patch` marker: the tree was installed before #499 added
`m5gfx_board_noprobe.patch`. Applying both patches through
`patches/ble_gap_patch.apply_patch`, which is the call the build's
`post:patches/m5gfx_board.py` makes, brought the second marker in and the gate
passes. No repository file changed for it.

The machine ran several worktree builds at once, so durations are not
representative.

| Gate | Command | Result |
| :--- | :--- | :--- |
| Host build | `cmake -S tests/host -B /tmp/furble-500-host -DBUILD_TESTING=ON` then `cmake --build /tmp/furble-500-host --parallel 8` | builds clean |
| Host tests | `ctest --test-dir /tmp/furble-500-host2 --output-on-failure -j 4` | 201 of 201 pass in 152 s, including `board-profile` (#183) and `platform-power-off` (#184) |
| Format | `clang-format --dry-run --Werror` on every changed C or C++ file (clang-format 21.1.2) | clean |
| Firmware | `FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e waveshare-s3-eth` | SUCCESS in 9:48. RAM 46,904 of 327,680 bytes, flash 1,529,081 of 6,291,456 |
| Size budget | `python3 tools/check_size_budget.py --env waveshare-s3-eth` | PASS: flash 1,529,488/6,291,456, dram 46,904/48,128, iram 108,544/109,568 |
| Size budget, tightest env | `FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e m5stack-core-debug` then `python3 tools/check_size_budget.py --env m5stack-core-debug` | PASS: flash 1,715,824/1,740,800, dram 55,948/57,344, iram 118,496/119,808 |
| Stack budget | `python3 tools/stack_budget.py --env waveshare-s3-eth` | 15 tasks PASS, 0 FAIL |
| Simulator build | `FURBLE_DEP_ROOT=.pio/libdeps/waveshare-s3-eth FURBLE_LVGL_DIR=managed_components/lvgl__lvgl sh sim/build.sh` | built, exit 0 |
| Profile scenario | `SDL_VIDEODRIVER=dummy ./sim/build/furble-sim --script sim/scenarios/e2e/hw-profile-waveshare.txt` | exit 0, 32 assertions, no failure, including `hw.wifi yes`, `hw.rtc no`, `hw.rtc_capability match`, `hw.sd no`, `hw.vbus no`, `hw.poe no` |
| Other profiles | the five other `e2e/hw-profile-*.txt` scenarios on the same binary | all exit 0, 21 to 26 assertions each, no failed assertion |
| Core power off | `sim/scenarios/bughunt/power-off-state-matrix.txt` and `power-off-during-connect-hang.txt` on the same binary | both exit 0, four `platform.power_off yes` assertions pass, so the merged row keeps the Core manual Off |
| Scenario manifest | `python3 tools/check_sim_scenarios.py` | complete, exit 0 |
| Hardware coverage | `python3 tools/check_sim_coverage.py --require-ready` | complete, exit 0 (206 hardware, 20 crash, 11 fuzz, 6,912 permutations) |
| Doc tokens | `sh sim/scripts/check-doc-tokens.sh` | tokens present, exit 0 |
| M5GFX pinning | `python3 tools/check_m5gfx_pinning.py` | PASS after the libdeps refresh described above |
| Reviewer gates | `python3 tools/pre_review.py --base origin/master` | PASS: 12 gates |

Only the Waveshare environment was rebuilt. The other five release images and
their debug variants were not compiled, and no other board was flashed.

The rows this pass re-ran on `3284b617` are the host build and suite, the
simulator build, the six profile scenarios, the two Core power-off scenarios,
the manifest check, the coverage check, the doc tokens, the M5GFX pinning gate
and `pre_review`. The firmware, size budget, stack budget and format rows are
from the earlier head on base `b711a44a`; the only edits since are prose in this
plan, which no build consumes, and CI is the authority for the firmware matrix
on this head. CI also builds the 320x240 core simulator
(`FURBLE_SIM_M5GFX_BOARD=board_M5Stack`) and runs the two power-off scenarios
there, which is the Core row rather than the default StickS3 row used locally.

### Coherence with the RTC model (`#507`, formerly `feat/455-sim-rtc`)

The battery-backed RTC model this plan anticipated from `feat/455-sim-rtc`
landed as `#507`, and the fourth rebase pass merged it exactly as predicted
here before it landed: `#507` adds `rtcBatteryBacked` and `timedWake` to
`board_caps_t` and pins `.rtc` on every row, and its `WAVESHARE` entry is
`.rtc = false, .rtcBatteryBacked = false, .timedWake = "none"`, which already
agreed with this branch's `Profile` row. The merged row adds this branch's
`.vbus = false` and `.poe = false`, so the initializer list reads `rtc,
rtcBatteryBacked, timedWake, vbus, poe` in declaration order.

`#507`'s per-board `rtcBatteryBacked` values match `Profile::rtcBatteryBacked`
in `src/FurbleBoard.cpp` for all nine rows, and its eight new scenarios seed
`hw_profile stickc-plus2` and never the Waveshare profile, so this branch's
`.rtc = false` cannot break them. `#507`'s `hwRtcCapability()` pair check also
passes for the Waveshare row, which the profile scenario asserts as
`hw.rtc_capability match`.

## Still unverified

- No Waveshare ESP32-S3-ETH was on the bench. Nothing here is a hardware
  result, and the row values are read from plan 42, the pinned M5Unified and
  M5GFX sources, and the vendor schematic facts recorded there.
- The simulator does not exercise the firmware row. `M5.getBoard()` in a
  simulator build returns the built-in panel board (`M5Unified.hpp:361`
  `_check_boardtype(Display.getBoard())`, `M5Unified.hpp:364` `_board = board;`,
  and the sim pins `M5GFX_BOARD=board_M5StickS3`), so `Board::profile()` is the
  StickS3 row even when a scenario seeds `hw_profile waveshare-s3-eth`; the seed
  drives `hw.*` only. The Waveshare row and the refusal are covered by
  `tests/host/board_profile_test.cpp` and
  `tests/host/platform_power_off_test.cpp`, not by a scenario.
- The `Platform::powerOff()` refusal has no reachable caller on the shipped
  image: `waveshare-s3-eth` is `FURBLE_NO_DISPLAY`, the console `power off`
  refuses on that build, and `UI::processAutoOff()`/`processLowBattery()` return
  early on the row. It is an invariant for future callers, not a user-visible
  fix on today's image.
- Wi-Fi association on this board is untested here. The profile reports Wi-Fi as
  present; the image's own Wi-Fi stack and its memory budget are unchanged.
- The microSD slot cannot be exercised. The row reports it disabled, which is
  the current firmware behaviour, not a hardware measurement.
- PoE is unmeasured and undetectable in firmware by design.
- The IP5306 cut itself is not measured. The 64 s light-load window and the
  claim that a deep-sleep Core stays under the 45 mA threshold are a register
  read plus the M5Unified comment, not a bench result. A 30 minute panel-off
  run on battery is still owed, and the recorded fallback if it fails is to
  set `powerOffPath` false on the Core row and change the two scenarios with
  the evidence attached.
