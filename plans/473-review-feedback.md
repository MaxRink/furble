# 473 - Review feedback for the hardware-trap corrections

## Scope

This is the review-feedback record for #473 (`docs/hardware-traps`), which
corrects six hardware-trap claims and carries the per-panel display dwell rule.
It updates plan 95 as a `## Follow-up:` section, plan 287, plan 322,
`plans/implemented/12-display-off.md`,
`plans/implemented/42-waveshare-eth-node.md`, the root `CLAUDE.md`,
`src/CLAUDE.md`, `docs/power/` and two source comments. Every finding is
recorded as fixed, already fixed or a false positive, with evidence.

## PR #473 (8ff406ea)

Source: review of the branch before 8ff406ea.

| # | Finding | Resolution | Evidence |
| ---: | :--- | :--- | :--- |
| 1 | The panel dwell rule was stated wrong twice over: first as a two-way dwell for every panel, then as a one-way rule for every panel. The rule is per panel. ILI9342C needs 120 ms in both directions, Sleep Out to the next Sleep In (V101 section 8.2.11, page 100) and Sleep In to the next Sleep Out (section 8.2.12, page 101), and the ILI9341 copy carries the same pair. ST7789V sections 9.1.11 and 9.1.12 and ST7796S V1.0 sections 9.2.12 and 9.2.13 need 120 ms from Sleep Out to the next Sleep In only, plus 5 ms after SLPIN and after SLPOUT before any other command. ILI9342C is the panel on `m5stack-core`, `m5stack-core2` and `m5stack-core-usb-debug`, so the reverse wait is required and must stay. | Fixed in 8ff406ea. Every passage that states the rule now states the per-panel version with its document sections and the panel build it applies to, and the deferral that proposed dropping the wake wait is withdrawn in plan 95 item 4. | The passages listed below; `git grep` evidence below. |

The passages that state the rule at 8ff406ea, all per panel:

- root `CLAUDE.md:357-377`, the display sleep trap.
- `src/CLAUDE.md:402-408`, the display off state machine.
- `include/FurbleUI.h:1135`, the comment on `DISPLAY_SLEEP_DWELL_MS`.
- `src/FurbleUI.cpp:1841`, the Sleep Out to next Sleep In gate, and
  `src/FurbleUI.cpp:1887-1893`, the Sleep In to next Sleep Out wait.
- `docs/power/display.md:103-120`.
- `docs/power/board-power.md:799`, `:1079`, `:1481`.
- `docs/power/soc.md:799-801`.
- `docs/power/datasheet-register.md:36-38`, the new ST7796S row and the revised
  ILI9342C and ILI9341 rows.
- `plans/287-mcu-portability-and-bundle-hardware.md:338`.
- `plans/322-hardware-abstraction.md:75`, `:172-178`.
- `plans/95-engineering-lessons.md:61-79` and follow-up item 4 at `:397-417`.
- `plans/implemented/12-display-off.md:30-32`.

Evidence: at 8ff406ea `git grep -iE "between (the )?(SLPIN|SLPOUT|Sleep In|Sleep
Out)"` returns nothing, and every remaining `dwell` hit that names a direction
names a panel class. The code already enforced both intervals:
`UI::sleepDisplay()` refuses a sleep within 120 ms of the last wake
(`src/FurbleUI.cpp:1843-1845`) and `UI::wakeDisplay()` waits the same interval
before `M5.Display.wakeup()` (`src/FurbleUI.cpp:1894-1897`), against
`DISPLAY_SLEEP_DWELL_MS = 120` at `include/FurbleUI.h:1136`. M5GFX 0.2.27
writes the bare SLPIN or SLPOUT with no delay
(`src/lgfx/v1/panel/Panel_LCD.cpp:101-107`) and its `sleep()` and `wakeup()`
add only the brightness write (`src/lgfx/v1/LGFXBase.hpp:1436-1437`), so both
waits are furble's. The 5 ms needs no delay of its own: after SLPIN the next
panel command is the SLPOUT the wake gate holds back 120 ms, and after SLPOUT
the wake path issues only `M5.Display.setBrightness()` (`src/FurbleUI.cpp:1901`,
`:1905`), which drives the backlight, not the panel.

Section numbers: the ST7789V table of contents numbers 9.1.11 SLPIN (10h) and
9.1.12 SLPOUT (11h); the ST7796S table of contents numbers 9.2.12 SLPIN (10h)
page 159 and 9.2.13 SLPOUT (11h) page 160. The ILI9342C V101 copy on the
M5Stack host blocks automated access, so its table of contents was not read
here. The ILI9341 copy numbers 8.2.11 Enter Sleep Mode (10h) page 100 and
8.2.12 Sleep Out (11h) page 101, the same pair and the same pages the PR
cites, which supports the pairing used in plan 95 item 4.

## Re-verification of the head commit (this record's commit)

Source: an independent check of every line citation the PR touches against the
8ff406ea tree, opening each file rather than trusting the grep hit.

| # | Finding | Resolution | Evidence |
| ---: | :--- | :--- | :--- |
| 2 | `docs/power/soc.md` "IR and SD tasks" row. The PR recomputed the IR half to `src/FurbleIR.cpp:219` and left the SD half at `src/FurbleSD.cpp:744-751`, which at HEAD is `SD::isSupported()` and the head of `SD::init()`, not a blocking queue read. | Fixed. Cites `src/FurbleSD.cpp:818`, the `xQueueReceive(..., portMAX_DELAY)` in `SD::taskLoop()`. | `docs/power/soc.md:999`; `src/FurbleSD.cpp:815-822` |
| 3 | `docs/power/soc.md` cited `src/FurbleUI.cpp:371-374`, `1316-1318`, `1333-1335` for the display `APB_FREQ_MAX` lock in the lock table, in the LOCK-03 current behaviour and in the LOCK-03 citation. At HEAD those lines are the roller helper, `UI::m_Mutex` and the connect-error dialog text; the bundle predates the move of the display state machine. | Fixed. All three cite `src/FurbleUI.cpp:511` (the `UI::UI()` acquire, before `lv_init()` at `:513`), `:1879` (the release in `sleepDisplay()`) and `:1900` (the reacquire in `wakeDisplay()`). | `docs/power/soc.md:93`, `:774`, `:805`; `src/FurbleUI.cpp:476-513`, `:1879`, `:1900` |
| 4 | The PR description still stated the one-way rule for every panel, and that "every sdkconfig caps the CPU at 80 MHz, which pins APB". The head commit says the opposite in both cases: the rule is per panel, and the 160 MHz runtime CPU maximum is what holds APB at 80 MHz, not the boot default. | Fixed in the description. | The "What" section of #473 |
| 5 | `plans/95-engineering-lessons.md:359-361` said every path without a `docs/`, `plans/`, `src/`, `include/` or `tests/` prefix is inside the ESP-IDF or esptool package, but the M5GFX citations the same commit adds (`src/lgfx/v1/panel/Panel_LCD.cpp:101-107`, `src/lgfx/v1/LGFXBase.hpp:1436-1437`) carry the library's own `src/` prefix and are in no tree path. | Fixed. The preamble now names the M5GFX `src/lgfx/` paths and says they live in the pinned `libdeps` copy. The two citations resolve in the M5GFX 0.2.27 package: `setSleep()` writes the bare SLPIN or SLPOUT at `Panel_LCD.cpp:101-107`, and `sleep()` and `wakeup()` add only the brightness write at `LGFXBase.hpp:1436-1437`. | `plans/95-engineering-lessons.md:358-362`; `.pio/libdeps/m5stick-s3/M5GFX/library.json` (0.2.27) |

Accepted as is: the "three of the six display boards" count at `CLAUDE.md:367`
and `plans/95-engineering-lessons.md:69` and `:406` counts display builds.
Six builds carry a panel (StickC, StickC Plus, StickS3, Core, Core2 and the
Core USB debug profile) and three of them are ILI9342C. `waveshare-s3-eth` is
one of the six board environments and has no panel
(`docs/power/board-power.md:115`). No change: the sentence is about the builds
that drive a panel and no rule depends on it.

Validation for this commit: `python3 tools/pre_review.py --base origin/master`
passes 10 of 10 gates and `sh sim/scripts/check-doc-tokens.sh` passes.
`python3 tools/check_review_threads.py --repo MaxRink/furble --pr 473` reports
no review threads on the PR and every check on the head passed.
