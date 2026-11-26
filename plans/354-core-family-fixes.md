# 354 - Core family hardware and power fixes

Status: implemented on `fix/core-family`, not merged. No hardware was attached:
the Core, Core2 and Tough gates below are unverified. Every register and board
fact quoted here comes from the cited vendor document, not from the firmware.

Related: [322](322-hardware-abstraction.md) (the board profile and power
backend this changes), [13](implemented/13-auto-off-low-batt.md) (power-off
policy), [19](implemented/19-interval-deep-sleep.md) (timed deep sleep),
[321](321-ublox-m10-driver.md) (GPS rail cycling),
[98](98-power-optimization-audit.md) and
[board-power.md](../docs/power/board-power.md) (PM-06, PL-07).

## Why

The Core, Core2 and Tough rows were copied from the pre-322 switches. Those
switches encoded several assumptions that the vendor hardware contradicts, and
two of them turn into data loss or a dead unit rather than a wrong label:

1. The M5Tough row says no SD slot and no speaker, and `FurbleFeedback` says the
   Tough has no speaker. The hardware has both, so the Tough loses SD logging
   and audio feedback for no reason.
2. The Tough can be woken by its battery-backed BM8563, but its row says no RTC
   backup and no timed wake. A timed intervalometer sleep therefore cannot use
   the one wake source the hardware has.
3. The Core's IP5306 clears its "BOOST output normally open" bit at boot. A
   panel-off Core falls below 45 mA, the PMIC removes the 5 V boost, and the
   unit dies with the panel already dark. Nothing in furble set that bit, so the
   Core-only Dim display-off default existed only to dodge that cut.
4. `Platform::powerOff()` returned true on the Core without ever reaching a
   result: on every non-M5PM1 board the call goes straight into
   `M5.Power.powerOff()`, which does not return. The claim was unreachable, not
   wrong, and the cut underneath it is real (see section 5).
5. The GPS rail column claimed a private Port A switch. On the Core2 and Tough
   the AXP192 GPIO0 enable drives one BUS_5V boost that feeds Port A and the
   M-Bus, so a "GPS" power cycle browns out every external 5 V consumer.
6. The battery capacities were wrong: Core 0 (unknown) and Core2 390 mAh. The
   vendor pages give 110 mAh and 500 mAh. The Core2 page also gives a 0.219 A
   charging current, while M5Unified's `setChargeCurrent(390)` selects 360 mA.
7. The Core's IP5306 charge current (150 mA) is below its own end-of-charge
   detection threshold (200 mA), so the current-based termination can never
   trip. The register document's lowest threshold is 200 mA.

## Sources

Vendor documents opened for this plan:

| Fact | Source |
| :--- | :--- |
| Tough microSD, NS4168 1 W speaker, BM8563 "supports RTC timer wake-up" | M5Stack Tough product page, https://docs.m5stack.com/en/core/tough and https://docs.m5stack.com/en/products/sku/K034 |
| Tough microSD G38/G23/G18/G4, NS4168 G12/G0/G2 with SPK_EN on AXP_IO2, BM8563 G21/G22 with INT on AXP_PWR | same Tough page, PinMap; https://github.com/m5stack/M5Tough README |
| Tough 390 mAh, "retains the SD card slot and speakers", I2S power amplifier | M5Tough README |
| Tough BT1 BATTERY_RTC on RTC_BAT; BM8563 INT to the PWR_KEY net into AXP192 PWRON; NS4168 SPK_EN from AXP192 GPIO2; SY7088 U8 output BUS_5V into J4 Port A pin 2 and J7 M-Bus pin 1 and M5_BUS pin 28 | M5Stack Tough schematic, https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/647/Tough-Schematics-PDF.pdf |
| Core 110 mAh @ 3.7 V, changed from 150 mAh in 2020.3 | M5Stack Basic page, https://docs.m5stack.com/en/core/basic |
| Core2 500 mAh @ 3.7 V, charging current 0.219 A, capacity changed from 390 mAh in 2023.10, RTC button battery removed in 2023.2 | M5Stack Core2 page, https://docs.m5stack.com/en/core/Core2 |
| IP5306 SYS_CTL0 bit 1 "BOOST output normally open"; SYS_CTL2 bits 3:2 light-load shutdown 8/32/16/64 s; Charger_CTL1 0x21 bits 7:6 battery-side stop-charge 200/400/500/600 mA; CHG_DIG_CTL0 0x24 charge current | Injoinic IP5306 register document V1.21 (document revision V1.4), https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/datasheet/core/IIC_IP5306_REG_V1.4_cn.pdf |
| IP5306 45 mA light-load threshold, 50 uA standby, 100 uA input quiescent | Injoinic IP5306 datasheet V1.01, cited by the Board lane in `docs/power/` |
| Boot register values `0x00 = 0x31`, `0x02 = 0x6C`, `0x21 = 0x09`, `0x24 = 0xC1`; `setPowerBoostKeepOn()` uses bit 1 | M5Unified 0.2.20 in `.pio/libdeps`, `Power_Class.cpp` and `IP5306_Class.cpp` |

## Defects, fixes and evidence

### 1. Tough capabilities

**Defect.** `src/FurbleBoard.cpp` set `sdSlot = false` and `feedbackSound =
false` for the Tough, and `src/FurbleFeedback.cpp` claimed the Tough has no
speaker. The vendor page lists an onboard microSD slot and a built-in 1 W
speaker driven by an NS4168 I2S amplifier; the pinmap puts the card on
G38/G23/G18/G4 and the amplifier on G12/G0/G2 with SPK_EN on AXP192 GPIO2.
M5Unified configures exactly those pins and enables AXP192 GPIO2 through
`_speaker_enabled_cb_core2()` for `board_M5Tough`.

**Fix.** Tough row: `sdSlot = true`, `feedbackSound = true`. Feedback comment
now names the NS4168/GPIO2 enable and only denies the LED and vibration motor.
`board_profile_test` asserts `sdSlot`, `feedbackSound`, `rtcBatteryBacked` and
`TimedWake::RTC_ALARM` by name, in addition to the full row comparison.

**Evidence.** Host `board-profile` test. No Tough hardware.

### 2. Tough RTC backup and timed wake

**Defect.** The Tough row had `rtcBatteryBacked = false` and
`TimedWake::NONE`, so the intervalometer sleep row stayed hidden and
`Platform::canTimedWake()` was false. The Tough schematic fits BT1
BATTERY_RTC on the RTC supply and wires BM8563 INT to the AXP192 PWRON net, so
the BM8563 timer can pull the power key and restart the board after a full PMIC
cut.

**Fix.** Tough row: `rtcBatteryBacked = true`,
`TimedWake::RTC_ALARM`. The BM8563 timer code in `Platform::powerOffUntil()` is
now board-agnostic (`clearBm8563Timer()`, `verifyBm8563Timer()`); the Plus2
keeps its hold-pin release, and a board with no hold pin arms the same timer and
then cuts PMIC power with `M5.Power.powerOff()`. The SD writer is unmounted
before the cut, as the manual path already did, because the Tough can have a
card inserted. The `TIMED_WAKE_MAX_EARLY_S` comment and log lines no longer say
"Plus2".

**Deviation.** A board with no hold pin refuses the timed cut while VBUS is
present. M5Unified's AXP192 path falls back to `esp_deep_sleep_start()` when the
rail cannot be cut, and the BM8563 INT reaches the PMIC power key, not an ESP32
wake pin, so a USB-powered timed sleep would strand the unit until reset. The
function returns false, clears the timer and leaves the device awake.

**Why the Core2 stays `TimedWake::NONE`.** The Core2 has the same BM8563 INT
net, but M5Stack's Core2 version-change table records "2023.2 Removed RTC button
battery". Without a backup cell the BM8563 does not keep counting through an
AXP192 cut, and the firmware cannot detect the board revision. This is recorded
as an open item, not silently enabled.

**Evidence.** Host `board-profile` test. The BM8563 timer verification itself is
firmware code, not host-tested; a hardware timed-wake run is required. The AXP192
PWRON long-press is 4 s (M5Unified writes `REG 36 = 0x0C`), so
`consumeTimedWake()` must clear the flag before that window; it runs in the UI
constructor through `Intervalometer::loadResume()`. This timing is unverified.

### 3. Battery capacities

**Defect.** Core was 0 (unknown) and Core2 was 390 mAh.

**Fix.** Core 110 mAh, Core2 500 mAh, Tough 390 mAh. The board comment records
the vendor pages and the two revision changes. Documentation tables in
`docs/power/board-power.md`, `docs/power/datasheet-register.md`,
`docs/power/measurement.md` and `docs/power/measurement-plan.md` were corrected,
including the 5.8 h Core2 discharge arithmetic (500 mAh / 67.8 mA = 7.4 h).

**Evidence.** `board-profile` test checks the constants. The cell capacity is
still the vendor nominal value, not a measured discharge.

### 4. IP5306 boost keep-on

**Defect.** M5Unified 0.2.20 writes `SYS_CTL0 = 0b00110001` at `M5.begin()` for
`board_M5Stack`, which clears bit 1, the IP5306 "BOOST output normally open"
bit. It writes `SYS_CTL2 = 0b01101100`, which selects the 64 s light-load window
(bits 3:2 = 11), not the 32 s the old docs quoted. A panel-off Core draws less
than 45 mA, so the boost was removed after that window. No furble code ever set
bit 1.

**Fix.** `include/FurbleIP5306.h` holds the register constants, the bit helpers
and `holdBoostOn()`, a template over the M5Unified `IP5306_Class` surface:
`setPowerBoostKeepOn(true)` followed by a `SYS_CTL0` readback. It is the same
code the firmware runs, not a parallel implementation.
`Platform::keepIp5306BoostOn()` calls it at boot on `pmic_ip5306`, logs the raw
`SYS_CTL2` value, and logs an error if the readback lacks bit 1. The bit stays
set for the running session. The one path that releases it is the manual Off:
`Power_Class::powerOff()` sleeps the panel, calls `_powerOff(false)`, which
clears the bit through `setPowerBoostKeepOn(false)`, and deep sleeps. The Core
display-off default is now Off like every other board; the old Dim default
existed only to dodge the accidental cut while the unit was awake.

If the write cannot be made (a non-I2C IP5306 Core, or a bus fault), the boot
logs the failure, `Platform::canTurnDisplayOff()` returns false, and
`processInactivity()` forces the dim-only mode for that boot instead of
sleeping the panel. That is the "gate display-off instead" branch, and it only
runs when the readback failed.

**Why not SYS_CTL2.** M5Unified already selects the longest window (64 s), so
the firmware does not rewrite it. The register document, not the M5Unified
comment, is the authority for the field encoding. The keep-on bit makes the
window irrelevant for the cut, so the boot log prints the raw register value
and the 8/32/16/64 s decode stays pinned by the host test.

**Size.** The first CI run failed this change on the `m5stack-core-debug`
profile, which shares the 1,740,800-byte OTA slot:

`Error: The program size (1741009 bytes) is greater than maximum allowed
(1740800 bytes)`, 209 bytes over.

Some of the strings that paid for the fix were the power-off refusal paths,
which finding 2 of [354-review-feedback.md](354-review-feedback.md) then
removed, so the committed image sits further under the slot than the
measurement recorded here. That profile had 400 bytes of headroom before this
change, and the boot log cost part of it. The log
lines in `Platform::keepIp5306BoostOn()`, `Platform::powerOff()`,
`Platform::powerOffUntil()` and `UI::processInactivity()` were shortened. The
profile measured 1,740,629 bytes on that head, 171 bytes under the slot, and
`tools/size_budget.json` keeps its 1,740,800 entry. That number belongs to the
head it was measured on: the refusal strings are gone with the review finding
below, and #524 restores verbose logging on the debug lanes, so the size of the
head being merged is whatever `build (m5stack-core-debug)` reports for it. The
squeeze is why the boot log prints the raw `SYS_CTL2` value rather than the
decoded window, and why a 30 minute soak run has to read the decoded window from
the host test rather than the log.

**Evidence.** New host `ip5306-policy` test: all 256 SYS_CTL0 values preserve
every other bit, `0x31` maps to `0x33`, the 8/32/16/64 s decode is pinned, and
the write/readback policy fails on an I2C failure, a write that does not stick
and an unreadable register. `settings-default-policy-core` now expects Off.
Hardware soak below 45 mA is still required, because the boost quiescent current
is not in the public datasheet.

### 5. Power off: a deferred cut, and the refusal that was withdrawn

What stays is the merged refusal, keyed on `powerOffPath`: it is true on the
Core, which therefore does not refuse, and false only on the Waveshare, which
has no PMIC to cut. The refusal described below is the one this branch added and
then removed.

**Defect.** `Platform::powerOff()` returned true on the Core without a result:
`M5.Power.powerOff()` does not return on any of these boards. What the call does
on an IP5306 board is the vendor's cut, in three steps: `Power_Class::powerOff()`
sleeps the panel and waits for it (`.pio/libdeps/.../M5Unified/src/utility/
Power_Class.cpp`, `powerOff()`), `_powerOff(false)` clears `REG_SYS_CTL0` bit 1
through `IP5306_Class::setPowerBoostKeepOn(false)` (`IP5306_Class.cpp`,
`BOOST_OUT_BIT = 0x02`), and `_powerOff` then reaches `esp_deep_sleep_start()`.
M5Unified's own comment on that bit says to set it for deep sleep and the RTC
timer "because the power supply is stopped when the load is light", and the
register document's `SYS_CTL2` bits 3:2 (M5Unified writes `0x6C`, so `11`) give
that light-load window as 64 s. With the panel asleep the load is far below the
45 mA threshold, so the charger removes the boost and the unit is off. It is a
deferred Off, not an absent one.

**First attempt, withdrawn.** This branch first read the `canPowerOff` column as
a platform fact: `Platform::canPowerOff()` returned it, `Platform::powerOff()`
refused on the Core before the TimeKeeper flush and SD unmount, the main menu
entry was hidden and its callback guarded, the console `power off` command
refused, `Request::POWER_OFF` reported `unsupported`, and `UI::doPowerOff()`
logged a refusal. On the Core that removes a manual Off that works, for a
premise the vendor library contradicts, and it changes the meaning of a column
the UI already uses for the automatic policies. The refusal, the hide, the
console gate and the second `M5.Power.getType() == pmic_ip5306` check are all
gone; `Board::profile().canPowerOff` keeps its policy meaning, which is what
`processAutoOff()`, `processLowBattery()` and the Power page rollers read.
Recorded as finding 2 in [354-review-feedback.md](354-review-feedback.md).

**What the branch keeps.** `Platform::powerOff()` no longer returns true for a
PMIC that refused: it documents that the call does not return and returns
`false` when it does, which is what `UI::doPowerOff()` needs to resume. The
comment states the deferred cut, so the next reader does not re-derive it.

**Evidence.** `docs/power/board-power.md` already recorded the deferred cut for
PM-07 and the `m5stack-core` row; this branch had made the documents and the
code disagree in the other direction. The Core is 64 s away from off, and that
window is a measurement item (below), not a reason to refuse.

### 6. GPS rail scope and advisory enable

**Defect.** The `gpsRail` comment and docs said the PMIC switches "Port A 5 V
for the GPS unit". The Tough schematic shows one BUS_5V net from the SY7088 U8
output into Port A J4 pin 2, the M-Bus J7 pin 1 and M5_BUS pin 28, enabled by
AXP192 GPIO0. Cutting "the GPS rail" therefore cuts every external 5 V consumer.
The request can also be refused: M5Unified cancels an enable when there is no
ACIN, VBUS current is present and the battery is at or below 8 percent.

**Fix.** The profile column, `Platform::canCutGpsRail()`, `Platform::setRail()`,
`FurbleGPS::setRailPower()` and `src/CLAUDE.md` now say `Rail::GPS` is the whole
external 5 V net on the Core2 and Tough, and that the enable is advisory and
reported through the final readback. `docs/power/board-power.md` has the same
statement in the rails table and the GPS/IMU paragraph. The existing
`railOffOrRestart()` best-effort handling is unchanged; it already restarts a
receiver whose cut did not take.

**Evidence.** Documentation and comment change only. No code path changed.

### 7. Charge-current consistency

These are recorded, not changed, because no existing API can make either one
consistent without a deliberate charge-profile change:

- **Core IP5306.** `SYS_CTL0 = 0x31` enables the charger; `0x24 = 0xC1` selects
  150 mA (`I = 0.05 + b0 * 0.1 + ...`, b0 = 1); `0x21` bits 7:6 = 00 selects
  the 200 mA battery-side stop-charge threshold. The current-based full
  detection can never trip below the 150 mA charge current. The register
  document's lowest threshold is 200 mA and the next step is 400 mA, so there
  is no consistent setting at 150 mA. Raising the charge current to 250 mA
  (the next representable step) would exceed 1C for a 110 mAh cell and was not
  done. Follow-up: measure termination on hardware; if it holds at the voltage
  edge, accept voltage-only termination and record the cell temperature, or add
  a deliberate 250 mA profile after a thermal test.
- **Core2 AXP192.** M5Unified calls `Axp192.setChargeCurrent(390)` with the
  comment "Core2 battery = 390mAh", treating capacity as current. The AXP192
  table in M5Unified maps 39 (tens of mA) to index 3, register `0x33` low nibble
  3, which is 360 mA. The vendor page gives charging current 0.219 A. The
  AXP192's discrete steps are 100, 190, 280, 360, ... mA (datasheet REG 33H
  bits 3:0), so no step equals 219 mA. `setChargeCurrent(219)` selects 190 mA
  through the same off-by-one M5Unified table, not 219 mA. This is a behaviour change for charging time and
  was not made. Follow-up: choose 190, 280 or keep 360 mA after a Core2
  charge-temperature and termination check, and correct the M5Unified call
  argument upstream.

## Files

- `include/FurbleBoard.h`, `src/FurbleBoard.cpp`: Tough row, capacities,
  comments for timed wake, GPS rail and power off.
- `src/FurbleFeedback.cpp`: Tough speaker comment.
- `include/FurbleIP5306.h` (new), `include/FurblePlatform.h`,
  `src/FurblePlatform.cpp`: keep-on policy, `powerOff()` refusal withdrawn but
  its return documented, BM8563 timer path with the alarm disarmed on a refused
  PMIC cut, SD quiesce, VBUS guard, GPS rail comment.
- `include/FurbleSettingsDefaults.h`: display-off default Off on every board.
- `src/FurbleUI.cpp`, `src/FurbleConsole.cpp`: power-off gates and refusal.
- `sim/FurblePlatformSim.cpp`: simulator `canTurnDisplayOff()` seam.
- `include/FurblePowerBackend.h`, `include/FurbleGPS.h`, `src/CLAUDE.md`:
  rail contract and advisory wording.
- `tests/host/board_profile_test.cpp`,
  `tests/host/ip5306_policy_test.cpp` (new),
  `tests/host/console_commands_test.cpp`,
  `tests/host/settings_default_policy_test.cpp`,
  `tests/host/CMakeLists.txt`,
  `tests/host/console/FurblePlatform.h`, `tests/host/console/console_shim.cpp`.
- `docs/power/board-power.md`, `docs/power/datasheet-register.md`,
  `docs/power/measurement.md`, `docs/power/measurement-plan.md`,
  `docs/settings-and-controls.md`, `docs/wiki/Settings-Reference.md`,
  `README.md`.
- `plans/README.md`, this plan.

## Verification

- Host suite: `cmake -S tests/host -B /tmp/furble-core-host-build`, then
  `cmake --build /tmp/furble-core-host-build --parallel` and
  `ctest --output-on-failure -j 3`: 197/197 tests passed in 197.29 s. The
  count is one higher than this plan first recorded because master added
  `sim-heap-caps` (plan 314) after the plan was written.
- Targeted host tests: `board-profile`, `ip5306-policy`, `console-commands`,
  `settings-default-policy`, `settings-default-policy-sticks3` and
  `settings-default-policy-core`: 6/6 passed.
- `python3 tools/check_settings_contracts.py --base origin/master --head HEAD`:
  PASS, 0 FAIL, 6 WARN (4 unresolved non-scalar defaults and 2 dynamic test
  definitions, all pre-existing).
- `python3 tools/pre_review.py --base origin/master`: PASS, 10 gates, exit 0.
- `sh sim/scripts/check-doc-tokens.sh`: PASS.
- Firmware, both affected envs, on a second attempt after the component-manager
  workaround below:
  - `pio run -e m5stack-core`: SUCCESS, 856 s. `firmware.bin` 1,628,288 bytes,
    Flash 93.5 percent of the 1,740,800-byte OTA slot, 112,512 bytes margin,
    RAM 16.8 percent.
  - `pio run -e m5stack-core2`: SUCCESS, 648 s. `firmware.bin` 1,913,312 bytes,
    Flash 30.4 percent of the 6,291,456-byte slot, RAM 1.3 percent.
  Both compile `src/FurblePlatform.cpp` (the ESP32-only IP5306 branch),
  `src/FurbleUI.cpp`, `src/FurbleConsole.cpp` and `src/FurbleBoard.cpp`, which
  the host suite does not.

### Why the firmware build needed a workaround

`idf_component_manager.prepare_components` did not finish on this host. It sat
in "Reading CMake configuration..." holding a TCP connection to the CloudFront
distribution behind `components-file.espressif.com`
(`d2i44g8zi8ev3y.cloudfront.net`, edge `18.245.60.63`) while another worktree's
manager was still parked in `CLOSE_WAIT` against the same edge. The host and
the CDN were not at fault: `curl` fetched the pinned
`lvgl__lvgl-v9.4.0.zip` (95,370,762 bytes) from the same host at 14 MB/s in
6.8 s at the same time, and `components.espressif.com` answered in 0.36 s.

The workaround keeps every compiled source and every dependency version
identical and only changes how the pinned LVGL component reaches the build:

- `IDF_COMPONENT_MANAGER=0`, so nothing waits on `prepare_components`.
- The pinned `managed_components/lvgl__lvgl` tree (LVGL 9.4.0, the version in
  `dependencies.lock`) restored from the registry zip and exposed to the build
  as a component directory.
- Two local `REQUIRES lvgl` entries, in `src/CMakeLists.txt` and
  `components/icons/CMakeLists.txt`, to stand in for the requirement the
  component manager normally injects from `src/idf_component.yml`.

Both edits and the component directory were reverted before this commit, so the
committed tree holds no part of the workaround, and CI still builds the envs
through the component manager as normal. One local-build artifact is worth
recording: an early attempt reconfigured with the component absent, which
rewrote `sdkconfig.m5stack-core` and dropped the `CONFIG_LV_FONT_MONTSERRAT_*`
symbols, so `src/FurbleUI.cpp` failed to find `lv_font_montserrat_16`. The
committed sdkconfig was restored from git and the build then passed; no
sdkconfig change is part of this PR.

## Open items and unverified claims

1. No Core, Core2 or Tough hardware was attached. Every hardware claim here is
   from the cited documents.
2. Core: 30 minutes of panel-off on battery with the keep-on readback in the
   boot log, plus pack current and board temperature. The boost quiescent
   current is not in the public datasheet.
3. Core: does charge terminate at the 4.2 V edge with a 150 mA current limit and
   a 200 mA current threshold? Measure cell temperature and termination time.
4. Core2: vendor 0.219 A versus the 360 mA register step; choose a step and
   record the charge profile change.
5. Tough: SD mount, speaker output and RTC timed wake on hardware. The timed
   wake needs the BM8563 INT to release the AXP192 PWRON net within the 4 s
   long-press window, and `consumeTimedWake()` must run inside it.
6. Tough: the RTC alarm wake path is a cold boot, not a resume. The intervalometer
   resume record and wall-clock restore depend on `TimeKeeper` and are not
   hardware-verified.
7. Core2: `rtcBatteryBacked = true` is left unchanged. M5Stack removed the RTC
   button cell in 2023.2; older units still have it, and the firmware cannot
   detect the revision. This is a separate decision.
8. The Core's deferred Off window is not measured. The claim is that a sleeping
   Core stays under 45 mA for the 64 s `SYS_CTL2` window, which comes from the
   M5Unified register write and the register document, not from a bench run. A
   90 s deep-sleep soak on battery, with the pack current logged across the cut,
   closes it.
9. The display-off default change is user-visible on a fresh Core or after a
   settings reset. It is deliberate: the keep-on bit removes the cut that the
   Dim default avoided.
10. The debug console display mode calls `M5.Display.sleep()` directly and is
    not gated by `Platform::canTurnDisplayOff()`. It is a developer path, not
    the user display-off setting; gate it too if a failed keep-on Core must
    never sleep the panel.
11. `Platform::canTurnDisplayOff()` has no host test because it reads
    `M5.Power.getType()`; the register policy beneath it is covered.
12. The two firmware envs now build locally through the component-manager
    workaround recorded under Verification, and CI builds the whole matrix
    through the component manager as normal. The workaround itself is not
    committed, so CI remains the authority for a clean component-manager build.
    The local build is what proved the ESP32-only IP5306 branch compiles; the
    host suite does not compile `src/FurblePlatform.cpp` or `src/FurbleUI.cpp`
    at all, which is why CI is the authority for the code this review changed
    there.
