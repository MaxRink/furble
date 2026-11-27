# 543 - Board verification pass, review feedback

Status: every finding below is fixed on `board-verification-pass` in the
follow-up commit, and each row of the resolution table names the change and the
evidence. The review text is kept verbatim after it so a reader can compare.

Review of PR 543 (`board-verification-pass`, head e6f13645) against
`18bbcb5a`. Verdict: request changes. The five numbered findings block, the
section "Other things to fix" should be handled in the same pass or split out
with a reason. Posted as a PR review comment, not committed.

Request changes. Posted as a review comment because the authenticated account is the PR author and GitHub refuses a formal request-changes from the author. The pass is mostly good work: I re-checked more than 30 rows against the vendor pages and the pinned libraries, and every one I sampled matched except the items below. Five findings block.


## Resolution

| ID | Review finding | State | Change and evidence |
| :--- | :--- | :--- | :--- |
| F1 | The replacement assertion cannot fail (blocker 1) | Fixed | The rule is now a production function, `Board::clearsStaleRtcAlarm()` in `include/FurbleBoard.h`, called by `src/FurblePlatform.cpp:176`. `tests/host/board_profile_test.cpp` pins its answer per row as a literal in `BOOT_CLEAR`. Mutation proof: replacing the body with `return row.rtcOnBus;` fails `StickC+`, `StickC+2` and `Tough`; restored, `board-profile: PASS`. The comment, `src/CLAUDE.md` and `plans/386` now name the predicate |
| F2 | Six certified scenarios fail on the PR head (blocker 2) | Fixed | `sim/hardware_model.cpp` rows corrected at STICKC, STICKC_PLUS, CORE, CORE_16MB, CORE2 and TOUGH, and the eight literals in `board-row-{stickc,stickc-plus,sticks3,core,core2,tough}.txt`. `sh sim/scripts/run-e2e.sh` on the M5StickS3 binary passes every certified scenario, including all seven `board-row-*` rows |
| F3 | The Plus2 IR polarity was promoted to VERIFIED (blocker 3) | Fixed | Row 9 of `docs/components/board-verification.md` is `VERIFIED (pin), UNVERIFIED (polarity)` and cites `plans/350:196`. The polarity is now U13 with the scope on G19 as the settling measurement |
| F4 | U6 is contradicted by its own page (blocker 4) | Fixed | The M5StickS3 page states `Display: Model ST7789P3, Resolution 135x240` and carries an ST7789P3 pin table. Row 5 is VERIFIED and U6 is closed |
| F5 | The new certified scenario pins a known-wrong boot state (blocker 5) | Fixed | `sim/hardware_model.cpp` now starts `PWR_CFG` at `PWR_CFG_RESET` (0x17), drops `applyBootOutputPowerLocked`, latches the stale-read snapshot when the rail manager powers the port, and starts `gpsWants` false. `ext5v-rail-boot.txt` asserts the reset byte, an unowned port and a refused transfer until a holder powers it; `hw-pmic-pwr-cfg-lost-update.txt` keeps its lost-update demonstration on the new snapshot |
| F6 | Eight per-board anchors are six lines short | Fixed | Anchors are 138, 142, 146, 150, 154, 158, 162 and 176 in `board-verification.md`; `bm8563.md` cites `:146` and `:21-27`; `pcf8563.md` cites `:146` |
| F7 | The Plus2 HOLD citation points at the StickS3 IRQ poll | Fixed | The row cites `src/FurblePlatform.cpp:164-170` for the boot hold and `:1019-1024` for the timed-wake release |
| F8 | `CLAUDE.md:439-441` mixes board rows with build environments | Fixed | The trap says ILI9342C is on three of the seven board rows, names the Core image as also `m5stack-core-usb-debug`, and says the Core2 image also serves the Tough. No `M5Tough` environment is claimed |
| F9 | `include/FurbleBoard.h:62-64` still says the Plus2 is the only reachable RTC | Fixed | The field comment names all five rows, and the new predicate sits beside it |
| F10 | The board-map Unknown `sdSlot` cell contradicts the code | Fixed | The cell is `no`, matching `src/FurbleBoard.cpp:183-186` |
| F11 | The StickC row verifies "no backup cell" by absence and omits the shipped `rtcBatteryBacked` | Fixed | The row records the shipped `rtcOnBus` and `rtcBatteryBacked` true with the mechanism (the main cell keeps the BM8563 counting across an AXP192 cut), and the state is `VERIFIED (chip and pins), REPORTED (retention across a full cell removal)` |
| F12 | The Plus row cites a pin-map RTC row that does not exist | Fixed | The row cites the specification table `RTC: BM8563` and the IMU and PMU bus table instead |
| F13 | `src/FurbleBoard.cpp:45-47` repeats the vendor claim about M5Unified | Fixed | The comment says the pinned 0.2.20 does not leave the port disabled, that `output_power` defaults to true, and that `cfg.output_power = false` is load bearing |
| F14 | U7 is not genuinely unverified, and U2 is partly decidable | Fixed | U7 is closed: row 1 cites the M5Tough README, "The battery capacity has been upgraded to 390mAh", state VERIFIED. U2 is narrowed to the Core2 and the Tough and records M5Unified's `0x33 = 0b11000000` ("Charge 4.2V, 100mA", `Power_Class.cpp:695`) as what settles the StickC and Plus |
| F15 | Pre-existing stale wake citations in `board-map.md` | Fixed | The arm and disarm citations are `src/FurblePlatform.cpp:666` and `:741` |

### Found while fixing F5, not certified

With the GPS setting off, the model reports `rail.holders gps` and
`rail.state off` at the same time from the first millisecond: a holder exists
whose port is not powered. The cause is the boot sequence, not the scenario
work: the receiver's enable takes the hold, the setting lands as off, and the
disable path cuts the byte without dropping the account. `ext5v-rail-boot` is
therefore certified on the GPS-enabled case only, and the disabled case is
recorded here as a defect with its own fix, because a scenario must not require
a state that cannot exist. The boot byte itself is asserted by
`hw-pmic-pwr-cfg-lost-update` (0x1F after the enable, 0x17 after the cut), and
the model now starts it at the documented 0x17 reset value.

Items the review recorded as checked and correct were not reworked. The counts
it confirmed (143 rows, 16 per board, 12 U items) changed only by the corrections
above: the U list is now eleven items after U6 and U7 closed and U13 was added.

## 1. The replacement assertion cannot fail (tests/host/board_profile_test.cpp:217-219)

```cpp
const bool bootClearsRtcIrq = p.rtcOnBus && (p.timedWake != TimedWake::RTC_ALARM);
check(!(bootClearsRtcIrq && (p.timedWake == TimedWake::RTC_ALARM)), ...);
```

`bootClearsRtcIrq` is true only when `timedWake != RTC_ALARM`, so the second conjunct is always false and the check is always true. Truth table over all four combinations: `False/False -> true`, `False/True -> true`, `True/False -> true`, `True/True -> true`. The old assertion (`check(!p.rtcOnBus || p.timedWake == RTC_ALARM)`) could fail and did guard something. The new one asserts nothing, while the comment at :210-216, `src/CLAUDE.md:71-80` and plans/386 (Test evidence) all say the test asserts the boot rule. It does not.

The table pins at :209 and :222 still hold the values, so a wrong profile value is caught. It is specifically the claimed boot-rule check that is vacuous. Make it falsifiable: assert a cross-column rule that a row with `rtcOnBus` true has a modelled chip, exercise the `Platform` guard through a seam, or check the guard site itself. Do not describe it as asserting the boot rule until it can fail.

## 2. Six certified simulator scenarios fail on the PR head (sim/scenarios/e2e/board-row-*.txt)

Built the simulator from `e6f13645` with the pinned deps and ran the board-row scenarios. Result:

```
board-row-stickc       rc=1  ASSERT FAILED: board.rtc_on_bus expected 'no' got 'yes'
board-row-stickc-plus  rc=1  ASSERT FAILED: board.rtc_on_bus expected 'no' got 'yes'
board-row-stickc-plus2 rc=0
board-row-sticks3      rc=1  ASSERT FAILED: board.led_active_low expected 'yes' got 'no'
board-row-core         rc=1  ASSERT FAILED: board.led_active_low expected 'yes' got 'no'
board-row-core2        rc=1  ASSERT FAILED: board.rtc_on_bus expected 'no' got 'yes'
board-row-tough        rc=1  ASSERT FAILED: board.rtc_on_bus expected 'no' got 'yes'
```

Failing lines: `board-row-stickc.txt:69`, `board-row-stickc-plus.txt:50`, `board-row-core2.txt:57`, `board-row-tough.txt:53` (`board.rtc_on_bus no`, now yes), and `board-row-core.txt:71`, `board-row-core2.txt:59`, `board-row-sticks3.txt:57`, `board-row-tough.txt:55` (`board.led_active_low yes`, now no). That is eight assertions in six scenarios, all marked `"certified": true` in `sim/scenarios/manifest.json`, and all in the list `check_sim_scenarios.py --list-certified --suite e2e --board m5stick-s3` (275 entries), which is what `sim/scripts/run-e2e.sh` runs and `.github/workflows/sim-e2e.yml:249` invokes. The sim e2e job goes red on this PR.

The scenario header at `board-row-stickc.txt:5-8` says both sides are pinned to the same literal exactly so a row that drifts fails. The PR moved one side only. The model side is now stale too: `sim/hardware_model.cpp:139`, `:141` (STICKC), `:173` (STICKC_PLUS), `:244` (STICKS3), `:276` (CORE), `:311` (CORE_16MB), `:341`, `:343` (CORE2), `:373`, `:375` (TOUGH). The comment at `:97-99` says those fields mirror `src/FurbleBoard.cpp`. Fix the model rows, then the scenario literals.

`check_sim_scenarios.py` passes because it only validates the manifest. The PR body and plans/386 read as if the sim gate is green; on the certified suite it is not.

## 3. The Plus2 IR polarity was promoted from an unconfirmed inference to VERIFIED (docs/components/board-verification.md:129)

Row 9 says `G19, shared with the red LED, NPN stage so active high`, state `VERIFIED`. The cited M5StickC Plus2 page says only that G19 is shared between the IR emitter and the red LED. It does not say NPN and gives no polarity. plans/350-stickc-family-fixes.md:196 records the NPN stage as unconfirmed: "the Plus2 NPN stage from the same pass. Not confirmed against a vendor PDF in this tree". Under the corpus rule at `docs/components/README.md:44` that is UNVERIFIED, or REPORTED at best.

This is a behaviour row: `irMarkLow` decides whether the RMT output is inverted (`src/FurbleIR.cpp:161`), so a wrong polarity turns every optical frame inside out. The StickC row (`:83`) and the StickS3 row (`:152`) split the pin from the polarity correctly. Do the same here and add it to the U list with a scope on G19.

## 4. U6 is contradicted by the page it cites, so row 5 should be VERIFIED (docs/components/board-verification.md:148, :309)

The M5StickS3 spec table states, in the text of the page cited throughout this section: `| Display | Model: ST7789P3 | Resolution: 135x240 |` (fetched 2026-11-27, same URL as the M5S3 key). U6 says "The vendor page text read this pass does not name it" and row 5 keeps `UNVERIFIED (part)`. Both are wrong, and `docs/power/board-power.md` already recorded ST7789P3 as REPORTED. Fix row 5 to VERIFIED and drop U6, or state precisely what is missing if something else is meant.

## 5. The new certified scenario pins a boot state the firmware contradicts (sim/scenarios/e2e/ext5v-rail-boot.txt:28-29)

Verified in the sources:
- `src/FurblePlatform.cpp:141` sets `cfg.output_power = false` under `FURBLE_HAS_M5PM1`, and the comment says why: the rail manager owns the port.
- `sim/FurblePlatformSim.cpp:59-65` builds its own M5 config and never passes that line, so the modelled boot keeps M5Unified's default.
- `sim/hardware_model.cpp:928-931` applies `BOOST_EN` unconditionally in `applyBootOutputPowerLocked`, and `:1798-1805` takes the stale snapshot after it.

So the claim in the PR body is true: the model boots 0x1F while the firmware asks for 0x17. The scenario is manifest-certified and passes (rc 0, 13 assertions), asserting `pmic.pwr_cfg 31` and `rail.state on` with `seed gps false`. Its own header at `:22-23` says "a model that starts at 0x17 fails the first two asserts", so it is written to fail when the defect is fixed.

I judge that acceptable as a record and not acceptable as a certification. It converts a known model defect into a green requirement, will force the correcting PR to edit this file, and makes a real regression (firmware or model cutting Port A at boot) invisible because the test expects the wrong state. `hw-pmic-pwr-cfg-lost-update.txt:27` already asserted 31, so this is not the first pin, but it is the first one labelled as a verification of boot behaviour.

Recommendation, in order: fix the model in this PR (start the byte at 0x17 for the M5PM1 board, set the snapshot after the firmware's own enable, update both scenarios), or leave the new scenario `"certified": false` in the manifest and describe defect 8 as recorded rather than fixed. Then the title and body should say seven defects fixed, one recorded. A second, smaller issue in the same file: `seed gps false` at `:25` while the model starts `gpsWants = true` (`sim/hardware_model.cpp:556`), so `rail.holders gps+ir` at `:40` is a model default, not a firmware holder.

## Other things to fix

1. Every per-board anchor in the new page is six lines short. `board-verification.md:71` cites `src/FurbleBoard.cpp:132`, which is a comment line; STICKC is at 138. Same for `:94` (cites 136, PLUS 142), `:117` (140, PLUS2 146), `:140` (144, STICKS3 150), `:163` (148, CORE 154), `:186` (152, CORE2 158), `:209` (156, TOUGH 162), `:232` (170, WAVESHARE 176). The same drift is in `docs/components/bm8563.md:41` (`:140` should be 146), `pcf8563.md:36` (`:140`), and `bm8563.md:121` still says `:16` while the rest of that file was moved to `:21-27`. The anchors moved when this PR added six lines to the comment block above the table. `check_doc_citations.py` does not check that the cited line holds the cited thing.
2. `board-verification.md:120` cites `src/FurblePlatform.cpp:820` to `:824` for the Plus2 HOLD pin. Those lines are the StickS3 PMIC GPIO4 poll. The HOLD drive is at `src/FurblePlatform.cpp:164-170`.
3. `CLAUDE.md:439-441` says the reverse wait is required on "four of the seven display boards", listing `m5stack-core`, `m5stack-core2`, `m5stack-core-usb-debug` and `M5Tough`. ILI9342C is on three of the seven board rows (Core, Core2, Tough). The list mixes board rows with build environments, and there is no `M5Tough` environment: `platformio.ini:68` says the Core2 image also serves a M5Tough. The Tough belongs in the list, the ratio does not.
4. `include/FurbleBoard.h:62-64` still says "the Plus2 is the only row with a chip M5Unified drives, so a write to its calendar is the only one that reaches hardware". That is the claim this PR corrects in `CLAUDE.md`, `src/CLAUDE.md` and `include/CLAUDE.md`, and it is now false in the header that defines the field.
5. `docs/components/board-map.md:32` gives the Unknown row `sdSlot` yes. The code has `false` (`src/FurbleBoard.cpp:183-186`). Every other cell in the regenerated table matches the structs; this one does not.
6. `board-verification.md:78` (StickC row 4) says "no backup cell" with state VERIFIED, on evidence "No cell is shown on that page". The page does not state it, so under `docs/components/README.md:44` it is not VERIFIED. The row also never records the shipped `rtcBatteryBacked` value, which is `true` at `src/FurbleBoard.cpp:138`. The Core2 row (`:193`) shows the honest treatment of the same fact.
7. `board-verification.md:101` (Plus row 4) cites "M5CP pin map `RTC BM8563`". The Plus page pin map has no RTC row; only the spec table names BM8563. The pin wiring claim needs the spec table plus M5Unified's bus, not a pin map row that does not exist.
8. `src/FurbleBoard.cpp:45-47` says the M5StickS3 page states "that M5Unified leaves the port disabled at init". The page does say that, but the pinned library does not: 0.2.20 has `output_power = true` (`M5Unified.hpp:133`) and `_begin()` calls `setExtOutput(cfg.output_power)` (`M5Unified.cpp:2015`), which sets BOOST_EN (`M5PM1_Class.cpp:72-75`). The `cfg.output_power = false` line is load bearing, not belt and braces. Say that, because a reader could delete it on the strength of this sentence.
9. `board-verification.md:310` (U7) is not genuinely unverified. The M5Tough README that plans/354:52 already names states "The battery capacity has been upgraded to 390mAh" at the public URL, and row 1 is already REPORTED (`:213`). A REPORTED fact should not also appear in "What remains UNVERIFIED". U2 (`:305`) is partly decidable too: M5Unified's AXP192 init writes `0x33 = 0b11000000`, commented "Charge 4.2V, 100mA" (`Power_Class.cpp:695`), and the Core2 row already states 360 mA from `setChargeCurrent(390)`.
10. Pre-existing but in a section this PR edits: `docs/components/board-map.md:83-84` cites `src/FurblePlatform.cpp:526-536` and `:573-597` for arming and clearing the GPIO4 wake. Those ranges are the IP5306 keep-on and the power-off path; the real ones are `:666` (`armMotionWake`) and `:741` (`disarmMotionWake`).

## What I checked and found correct

- Counts: 143 fact rows exactly, 16 per shipped board, 10 CoreS3 and 5 LilyGO, 12 U items.
- `rtcOnBus` true on the StickC, Plus, Core2 and Tough: each page lists a BM8563, and M5Unified 0.2.20 `PCF8563_Class::begin` writes 0x00 twice and 0x0E and keeps the instance only when acknowledged (`PCF8563_Class.cpp:29-30`, `RTC_Class.cpp:81-87`). M5Unified probes on every board regardless of the profile, so the `CLAUDE.md` correction is right and the only runtime change is the boot IRQ clear at `src/FurblePlatform.cpp:176`.
- Core2 rail: `Power_Class.cpp:958` calls `Axp2101.setBLDO2(enable * 3300)`; the AXP192 path at `:967` writes 0x90. The old comment was wrong twice: the AXP2101 has no LDOIO, and the Tough has no AXP2101.
- `ledActiveLow` false on the four rows is inert and defensible: `FurbleFeedback.cpp:150` uses it only when `feedbackLight` is set, and `FurblePinShare.cpp:71` is compiled only for the shared-pin boards, whose `ledPin` is not -1.
- Tough ILI9342C: `M5GFX.cpp:246` (`Panel_M5StackCore2 : Panel_ILI9342`) is the panel used for the Tough at `:1467`, and the page states ILI9342C.
- Vendor facts sampled and matched: capacities 95, 120, 200, 250, 110 and 500, the 2020.3 and 2023.10 capacity changes, IP5306 0x75 with `0x70` bit 3, TF pins 23/19/18/4 and 23/38/18/4, GROVE G22/G21, FT6336U 0x38, CHSC6540 0x2E, BM8563 0x51, NS4168 enable on AXP192 GPIO2 or ALDO3, StickS3 ES8311 and AW8737 with `bitOn(0x6E, 0x11, 0b00001000)`, BMI270, IR G46/G42 with the EXT_5V note, and the Tough 390 mAh in the M5Tough README.
- The comment de-interleaving at `src/FurbleBoard.cpp:30-58` reads cleanly and the duplicate paragraph is gone.

## Gates I ran

| Command | Result |
| :--- | :--- |
| `python3 tools/check_doc_citations.py --root .` | PASS, 0 errors of 3152 citations in 179 files (the PR body says 3109, stale count) |
| `python3 tools/check_sim_scenarios.py` | PASS (manifest only, does not run the scenarios) |
| `python3 tools/check_sim_coverage.py --require-ready` | PASS, 206 of 206 modelled |
| `sim/build/furble-sim --script sim/scenarios/e2e/board-row-*.txt` | 6 of 7 FAIL, see finding 2 |
| `sim/build/furble-sim --script sim/scenarios/e2e/ext5v-rail-boot.txt` | PASS, 13 assertions, pins the wrong boot byte |
| `git diff --check`, `check_pr_hygiene`, `check_cpp_safety`, `check_ci_workflows`, `check_coupling_ratchet`, `check_datasheet_citations`, `check_m5gfx_pinning` | PASS |
| `python3 -m unittest` scanner suite | PASS |
| `python3 tools/pre_review.py --base origin/master` | could not complete in my environment: clang-format is not installed, and the settings-contracts gate was killed inside pre_review (it passes standalone with 0 FAIL, 6 WARN). Not a claim that the PR fails it. |

## Limits

No hardware was attached. Vendor facts come from the same M5Stack pages the pass cites, opened 2026-11-27. Library facts come from `.pio/libdeps/m5stick-s3` and `m5stick-c-plus` at M5Unified 0.2.20, M5GFX 0.2.27 and M5PM1 1.0.6. The simulator was built from the PR head in a scratch worktree with those deps.

Review feedback is also recorded at `plans/543-review-feedback.md`.
