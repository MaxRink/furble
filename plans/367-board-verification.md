# 367 - Board implementation verification against the vendor documents

Status: implemented on `board-verification-pass`, not merged. Every fact below
is recorded in [docs/components/board-verification.md](../docs/components/board-verification.md)
with its own row and source, so the next reviewer can check the work instead of
redoing it.

Related: [322](implemented/322-hardware-abstraction.md) (the `Board::Profile` row
this pass audits), [349](349-waveshare-board-row.md) (the Waveshare row),
[350](350-stickc-family-fixes.md) (StickC family polarity and RTC backing),
[353](353-hal-verification.md), [354](354-core-family-fixes.md) (Core family
hardware), [363](363-pmic-irq-speaker.md) (the `rtcOnBus` column),
[364](364-ext5v-rail.md) (the rail manager), [318](318-next-board-battery-gps.md)
(the board candidates this pass also reads).

## Why

A verification pass, not a feature pass. Eight `Board::Profile` rows and two
`Board::Profile`-driven branches of `src/FurblePlatform.cpp` carry roughly 150
hardware facts: battery capacity, the charging path and current, the power-off
mechanism and whether a software power-off path exists, whether an RTC chip is on
the bus and whether it is backed, the display controller and its sleep-timing
class, the touch controller and its address, the IMU part and the resolution of
its wake threshold, the SD slot and card detect, the IR emitter and its polarity,
the LED pin and polarity, the speaker amplifier and its enable pin, the GPS pin
source and pins, the rail topology, and the feedback outputs. Each one was
written by a different pass, and three of them had already been corrected once
(plans 350, 354, 363). Nothing re-read the whole set together.

Two defects survived those passes. The first is a hardware fact that was set to
satisfy a test implication rather than to match the board: four boards report
that no RTC chip answers on their internal I2C bus, and each of those boards has
a BM8563 at 0x51 in its vendor pin map. The second is a documentation record
that no longer described the code, in the one page that exists to do it.

## Method

For each row, the value in `src/FurbleBoard.cpp` and the branch in
`src/FurblePlatform.cpp` was compared against, in this order:

1. The vendor product page and pin map for the board, opened for this pass.
2. The pinned board support library at the version `platformio.ini` pins
   (M5Unified 0.2.20, M5GFX 0.2.27, M5PM1 1.0.6) in `.pio/libdeps`, which is the
   code that actually writes the registers.
3. The component page under `docs/components/` for the chip, and the datasheet
   section that page names.
4. The simulator model, to see whether it would have hidden the difference.

A row is VERIFIED only when a source opened in this pass states the value. A fact
the tree already recorded from a document this pass did not reopen is REPORTED.
Anything else is UNVERIFIED with the measurement that would settle it. The four
words and their meaning are the corpus rules in
[docs/components/README.md](../docs/components/README.md).

## Findings

### 1. Four boards that have an RTC said they had none

`rtcOnBus` was true only on the StickC Plus2. The column's own documentation
says it means "an RTC chip the driver can reach answers on the internal I2C
bus", and the vendor pin maps for the M5StickC, the M5StickC Plus, the Core2 and
the Tough each list a BM8563 at 0x51 on G21/G22. M5Unified's RT`C_Class::begin()`
falls back to `PCF8563_Class` for every board and keeps the instance when its
writes at 0x00 and 0x0E are acknowledged, which both are on a BM8563.

The value was false because `board_profile_test` asserted the implication
"`rtcOnBus` implies an `RTC_ALARM` timed wake". That implication was written to
stop a reviewer's finding in plan 363 (N1: the boot IRQ clear was unreachable)
from returning, and it made the hardware fact follow the test. The implication
is not a hardware rule: the Core2 has no backup cell since the 2023.2 revision,
so its timed wake stays NONE while its RTC chip still answers.

Fix: the four rows are true; the test asserts the boot rule
(`rtcOnBus && timedWake != RTC_ALARM` never fires on an `RTC_ALARM` row) instead
of an implication between two independent facts. The boot IRQ clear is now live
on the StickC, the Core2 and the Tough, where it clears a stale BM8563 alarm flag
that nothing consumes.

### 2. Four rows described the polarity of an LED they do not have

`ledActiveLow` was true on the StickS3, Core, Core2 and Tough, and every one of
those rows has `ledPin` -1. The column is documented as "the LED lights on a low
pin", which is a property of a driven pad. The Core2's green power indicator is
not software driven at all, and the StickS3 status LED is the M5PM1 `PWR_CFG` bit
4 level rather than a GPIO. The value was inert at run time
(`FurbleFeedback.cpp` uses it only when `ledPin` is not negative), which is why
nothing failed.

Fix: false on those four rows. The unknown-board row keeps true, because nothing
is known about a board furble cannot identify.

### 3. The `EXT_5V` comment in `src/FurbleBoard.cpp` did not parse

Two merges interleaved paragraphs, so the block read "the rail is already on
before the GPS driver runs, by two" followed by an unrelated paragraph followed by
"writes during M5.begin()". The StickS3 rail paragraph was also duplicated.

Fix: four separate paragraphs, one fact each, and the duplicate removed.

### 4. The Core2 rail switch was documented as the wrong AXP2101 register

`src/FurblePlatform.cpp` said "AXP2101 LDOIO0 (Core2, Tough)". M5Unified 0.2.20
`Power_Class::setExtOutput()` calls `Axp2101.setBLDO2(enable * 3300)` for that
board. The AXP2101 has no LDOIO.

Fix: the comment names BLDO2 and the function it comes from.

### 5. `CLAUDE.md` claimed three of six display boards need the reverse sleep wait

The trap paragraph listed the ILI9342C panel on the Core, Core2 and the Core
debug profile only. M5GFX 0.2.27 maps `board_M5Tough` to the same
`Panel_M5StackCore2 : Panel_ILI9342`, and the Tough page states an ILI9342C
panel, so the both-direction rule covers four of the seven display boards.

Fix: the trap names the Tough and the count is four of seven.

### 6. The same trap claimed `M5.Rtc.isEnabled()` is false everywhere but the Plus2

Fix: the trap now says which five rows answer at 0x51 and keeps the rule that the
flag is not derived from the timed-wake kind. `src/CLAUDE.md` and
`include/CLAUDE.md` carried the same claim and are corrected.

### 7. `docs/components/board-map.md` no longer described the code

The runtime profile table was a copy of a pre-322 state: Core capacity 0 (the code
says 110), Plus2 RTC backing false (true), Plus timed wake NONE (RTC_ALARM),
Tough SD and sound false (true), no `powerOffPath`, `rtcOnBus`, `ledActiveLow`,
`irMarkLow` or GPS columns at all, and no Waveshare column. It also said the
Core2 and Tough touch addresses are not in the repository, which the vendor pin
maps settle (FT6336U 0x38 and CHSC6540 0x2E).

Fix: the table is regenerated from the shipped values with all nine columns, the
touch addresses are added to the I2C table, and the page points at the new
per-board verification table for the evidence of each cell.

### 8. The simulator boots the StickS3 port powered, and the firmware does not

The simulator already models the rail fact a document decides: on the StickS3 the
EXT_5V port feeds the Grove 5 V output, the HAT 5 V pin and the IR transmitter and
receiver, so an IR transfer holds the port and a GPS cut is refused while it is in
flight (`sim/scenarios/e2e/ext5v-rail-holders.txt`,
`sim/scenarios/e2e/hw-ir-rail-cut-in-flight.txt`).

What it does not model is furble's own boot step. `Platform::init()` passes
`output_power = false` on this board so M5Unified does not commit the port, which
leaves M5PM1 `PWR_CFG` at its documented reset byte 0x17 with the 5 V boost off.
`sim/hardware_model.cpp` applies the reset byte and then M5Unified's
`output_power` default, which sets bit 3, so the modelled port is powered from the
first instruction. That is a deviation: the firmware and the vendor page both say
the port starts unpowered.

The deviation is not a one-line fix. `hw-pmic-pwr-cfg-lost-update` depends on the
boot byte and on the byte after the GPS cut differing, which is the only way its
stale write can lose an update at all; starting at 0x17 removes the difference
and the scenario loses its subject. Correcting the model therefore means moving
the stale-write snapshot to a seeded read taken after the rail changes, and
re-running the full simulator suite. That is its own PR, not a side effect of a
verification pass.

Fix in this pass: the deviation is pinned instead of hidden.
`sim/scenarios/e2e/ext5v-rail-boot.txt` asserts the modelled byte (0x1F), the
powered port, and the transfer contract around it, and its header states why the
byte is what it is and what a corrected model must change. A reader of the suite
now sees the disagreement, and any change to it fails a scenario.

## Judged already correct

Each of these was checked against a source and left alone. The row number is the
fact number in the per-board table.

| Fact | Rows | Evidence |
| :--- | :--- | :--- |
| Battery capacities 95, 120, 200, 250, 110, 500 | StickC, Plus, Plus2, StickS3, Core, Core2 | The product pages state each figure, and each page's version table records the revision that changed it |
| Tough 390 mAh | Tough | Recorded from the M5Tough README in plans/354; this pass could not reopen that document, so it stays REPORTED |
| Plus2 `battery` caps `{level, voltage, false, false}` | Plus2 | The board has no PMIC and reads only the G38 divider, so there is no current or charge status |
| StickS3 `battery` caps `{level, voltage, false, true}` | StickS3 | The M5PM1 reports voltage and the charge state on `PYG0_CHG_STAT` and has no current register |
| Core `battery` caps `{level, false, false, true}` | Core | The IP5306's I2C interface exposes a four-level gauge and the `REG_READ0` 0x70 bit 3 charge flag that M5Unified reads (`docs/components/ip5306.md` row 4), and the part has no current register |
| `powerOffPath` true everywhere but the Waveshare | seven rows | Each board has a PMIC shutdown, the Plus2's HOLD pin, or the IP5306 keep-on clear |
| `powerOffPath` false on the Waveshare | Waveshare | No PMIC and no cell, so `M5.Power.powerOff()` would deep sleep with no wake source armed |
| Plus2 `powerHoldPin` 4 | Plus2 | The vendor page states HOLD G4 must be driven high after an RTC wake and low to cut |
| StickS3 `timedWake` PMIC_TIMER, `powerHoldPin` -1 | StickS3 | M5PM1 `timerSet`/`shutdown`; there is no separate hold pin on that board |
| Tough `timedWake` RTC_ALARM | Tough | The page states the BM8563 "supports RTC timer wake-up" |
| Core2 `timedWake` NONE | Core2 | The 2023.2 version change removed the backup cell and the firmware cannot read the revision |
| Display parts and the sleep class | seven rows | Vendor pages plus M5GFX's panel classes; the firmware applies the wider both-direction rule everywhere |
| Touch: none, none, none, none, none, FT6336U 0x38, CHSC6540 0x2E, none | all eight | Vendor pin maps and M5GFX's touch configuration |
| IMU parts MPU6886, MPU6886, MPU6886, BMI270, MPU6886, MPU6886, MPU6886 | seven rows | Vendor pages; the Core's Gray variant is `MPU6886/SH200Q` |
| BMI270 threshold 0.4883 mg per count | StickS3 | BST-BMI270-DS000-08 rev 1.6 section 5.2.40, pages 92 and 93 |
| SD CS G4 on the three Core-family boards, MISO G38 on the Core2 and Tough and G19 on the Core | Core, Core2, Tough | Vendor pin maps and M5Unified's `_pin_table_sd`, which also carries 255 for both detect columns, so no card detect exists |
| IR pins 9, 9, 19, 46 | StickC, Plus, Plus2, StickS3 | Vendor pin maps; the Plus2 and StickS3 pages also name the receiver and the shared LED pin |
| IR polarity: active low on the StickC and Plus, active high on the Plus2 | three rows | The StickC Plus schematic page (plans/350) and the Plus2's NPN stage |
| LED pins 10, 10, 19 and none | StickC, Plus, Plus2 and the rest | Vendor pin maps and M5Unified's Light_PWM configuration, `invert = true` on G10 and `false` on G19 |
| Speaker: buzzer G2 on the Plus and Plus2, DAC G25 on the Core, NS4168 with AXP192 GPIO2 on the Core2 and Tough, ES8311 and AW8737 with M5PM1 GPIO3 on the StickS3 | five rows | Vendor pages and M5Unified's speaker setup and enable callbacks |
| GPS `BOARD_SUPPORT` on the seven M5 rows, `NONE` on the Core and the Waveshare | eight rows | M5Unified's Port A table: the ESP32 boards resolve to G33/G32, the StickS3 to G10/G9; the Core's Port A is its internal I2C bus and the Waveshare's is the W5500 clock |
| `gpsRail` true on five rows and false on the Plus2, the Core and the Waveshare | eight rows | M5Unified's `setExtOutput()` has no case for the Plus2 or the Core, and the Waveshare has no rail |
| Rail feeds: Grove 5 V on the StickC family, one BUS_5V net for Port A and the M-Bus on the Core2 and Tough, Grove, HAT 5 V and IR on the StickS3 | three families | Vendor pin maps and the Tough schematic; the StickS3 page states the three outputs |
| `feedbackSound`, `feedbackLight`, `feedbackVibration` per row | eight rows | Each follows the speaker, LED and motor facts above, and the Waveshare has none of the three |
| StickS3 speaker volume ceiling 191 (75 percent) | StickS3 | The vendor page states the 75 percent rule for battery operation |

## What stays UNVERIFIED

The per-board table lists twelve open facts with the measurement each needs. The
four that are most likely to be wrong rather than merely unknown:

- The MPU6886 wake-on-motion count size, assumed 4 mg. Six rows depend on it. The
  datasheet states no scale, so a bench calibration is the only way to settle it.
- The StickC and StickS3 IR emitter polarity. Both rows are a hardware fact with
  no schematic in the tree, and a wrong polarity turns every frame inside out.
- The Core2 RTC backup cell, which depends on the board revision.
- The StickS3 speaker settle time of about 1 s, which `CLAUDE.md` states and no
  document in the tree supports.

## Test evidence

| Command | Result |
| :--- | :--- |
| `board-profile` | PASS, with the corrected `rtcOnBus` and `ledActiveLow` values and the boot-rule assertion |
| Host suite: `cmake --build` then `ctest -j 4` | PASS, 221 of 221 |
| `clang-format 21.1.2 --dry-run --Werror` on the changed C and C++ files | clean |
| `python3 tools/check_doc_citations.py` | PASS, 0 errors of 3,109 citations in 176 files |
| `python3 tools/pre_review.py --base origin/master` | PASS, 13 gates |
| `sh sim/scripts/check-doc-tokens.sh` | PASS |
| `python3 tools/check_sim_scenarios.py` | PASS with the new scenario |
| `python3 tools/check_sim_coverage.py` | PASS, 206 modeled hardware entries of 206 |
| `sim/scenarios/e2e/ext5v-rail-boot.txt` under the `sim-e2e` capability env | PASS, 13 assertions |
| `ext5v-rail-holders`, `ext5v-rail-interlock`, `hw-ir-rail-cut-in-flight`, `hw-pmic-pwr-cfg-lost-update` under the same env | PASS |

Hardware was not attached. Every electrical fact in this pass is a document or a
library reading, and the UNVERIFIED rows say so.
