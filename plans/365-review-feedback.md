# 365 - Review feedback for the Core family simulator alignment

Implementation state: reviewed at `7685a0e6` on `fix/core-sim-align`. The branch
was conflicting against `master` from the moment it was opened, had no check
runs at all, and was rebased onto `6a82c71b`. The rows below are this review's
findings; all of them are fixed on the branch. Hardware gates stay where plan
[365](365-core-sim-align.md) puts them: this review had no board, so nothing here
is a bench result.

## Scope

An independent adversarial review of PR #513 against plan 365, the pinned vendor
code (M5Unified 0.2.20, M5GFX 0.2.27), `src/FurbleBoard.cpp`, the in-tree
document register, the recent Waveshare, StickC and Core family merges
(#500, #505, #507, #521, #482) and the simulator model. The pull request had no
human or Copilot review threads, so every row comes from this review.

The review treated the description's claims as things to falsify. Four specific
questions were asked and are answered in rows 6 to 9: whether the IP5306 decode
matches the register document table and uses the written `0x6C` rather than the
32 s default, whether the AXP2101 guard really blocks `0x82` where that address
is the CPU rail, whether the model agrees with `src/FurbleBoard.cpp` on every
board row after the recent merges, and whether the new scenarios assert rather
than print.

## Why the pull request had no check runs

Not a conflicting base that had been deleted, and not a stale workflow set.

- `base_sha` was `30cc9c79`, which is `fix(hw): hold W5500 RESETn for 500 us and
  correct the IP5306 cutoff (#482)`, committed `2026-11-26 05:12:08 +0100`
  (`04:12:08Z`). The pull request was opened at `04:12:59Z`, 51 s later.
- That commit rewrote the same `docs/power/board-power.md` line and the same
  `hw-pmic-ip5306-core.txt` lines this branch rewrites, so the branch was
  conflicting from creation. `git merge-tree` reports a conflict against
  `30cc9c79` as well as against current master, and 14 paths conflicted on the
  rebase.
- `git/refs/pull/513/merge` returned 404, while `497` and `510` have one. That
  merge commit is created when the head can be merged with the base; a pull
  request that is conflicting never gets one. Every workflow here triggers on
  `pull_request`, which resolves `GITHUB_REF` and `GITHUB_SHA` from that commit,
  or on a `push` limited to `master`/`combined`. With no merge ref, no run could
  be created, so the head had zero check runs for its whole life.
  `check_review_threads.py` fails closed on "PR head has no check runs", which is
  why the pull request could not merge.
- The alternatives are excluded by evidence: `30cc9c79` is an ancestor of
  `origin/master`, so the base existed; all 17 workflow files exist at the
  branch's merge base `a5cb2e14` and the pull request touches no file under
  `.github/`, so its head does not predate a workflow change; Actions was
  enabled and busy, with 525 runs created in the 02:00 to 07:00Z window that
  contains the push, and other open pull requests had 20, 39 and 93 branch runs.
- A conflict alone is not the trigger: #497 is conflicting today and has 93
  branch runs, because it was mergeable when it was pushed and its merge ref
  exists. The distinguishing fact for #513 is that it was already conflicting at
  the `opened` event.

Fixed by rebasing onto `6a82c71b` and force-pushing, which creates the merge ref
and the runs.

## Findings

| # | Area | Finding | Verdict | Evidence |
| :-: | :--- | :--- | :--- | :--- |
| 1 | Branch state | The pull request never had a merge ref, so no workflow could schedule a run. See the section above. | Fixed: rebased onto `6a82c71b`. | `gh api .../git/refs/pull/513/merge` 404, `/pulls/513` `base.sha`, `check-runs` `total_count` 0, branch-run count 0 |
| 2 | Rebase | The rebase is a re-implementation, not a text merge. Master had replaced the model's rail owner (`rail_state_t`, `hwSetRail(rail_holder_t, bool)`), split `rtc` into `rtc` + `rtcBatteryBacked` with an `RTC_FACTS` table, added `vbus`/`poe`, added the Waveshare row and the PM1 `PWR_CFG` byte, and moved the Port A rail into the model. 14 paths conflicted. | Fixed: master's owners are kept and the branch's facts are ported onto them. The branch's `hwSetGpsRail()` was dropped in favour of the model-owned `hwSetRail()`, and the external 5 V net is now driven from `setRailLocked()` instead of a second call site. | `git log 6a82c71b..HEAD`, `sim/hardware_model.cpp` `setRailLocked`, `sim/FurblePlatformSim.cpp` `setRail` |
| 3 | Board identity | The branch's `sim/board_override.cpp` mapped `waveshare-s3-eth` to `board_unknown`. #500 gave that board its own firmware row and `plans/349-waveshare-board-row.md` records that M5Unified detects it as `board_M5StampS3`, so the override returned the `UNKNOWN` row instead: `canPowerOff` true and `powerOffPath` true. `Platform::powerOff()` would then report success on a board whose firmware refuses, and `board.sd` would read the wrong row. | Fixed: `waveshare-s3-eth` maps to `board_M5StampS3`, and `headless-s3` keeps `board_unknown`. The files themselves are gone: master now carries a weak `furble_sim_board_profile()` hook plus `Sim::setBoardProfile()`, so the mapping moved into `sim/driver.cpp` and the redundant seam and its host stub were deleted. | `src/FurbleBoard.cpp` `case m5::board_t::board_M5StampS3`, `plans/349-waveshare-board-row.md` |
| 4 | RTC columns | After master split the column, two model rows were stale against the firmware rows: `stickc-plus2.rtcBatteryBacked` was false while the row is true, and `stickc-plus.timedWake` was `"none"` while the row is `RTC_ALARM`. Both are plan 350 defects whose fix landed on the firmware side only. `board_caps_t.rtcBatteryBacked` also had no query at all, so the column could not be asserted by any scenario. | Fixed: both rows corrected, `hw.rtc_backed` added, and `hw.rtc_backed` plus `board.rtc_backed` asserted in all seven `board-row-*` scenarios. | `plans/350-stickc-family-fixes.md` defect 2 and the Plus timed wake, `sim/hardware_model.cpp` rows, `sim/scenarios/e2e/board-row-*` |
| 5 | Core IR | Master's model carried `CORE.ir = true` and `hw-pmic-ip5306-core` asserted `hw.ir yes`, while the firmware row has `irPin = -1` and `docs/power/board-power.md` lists no IR part for any Core board. The branch's correction to `no` is right, and the model side was the wrong one. | Fixed: the model, the scenario and `board.ir` all say no. | `src/FurbleBoard.cpp` CORE row, `docs/power/board-power.md` section 2 |
| 6 | IP5306 decode | The decode matches the table the branch cites: `IP5306_LIGHT_LOAD_TABLE_MS = {8000, 32000, 16000, 64000}` indexed by bits 3:2 is 00 = 8 s, 01 = 32 s, 10 = 16 s, 11 = 64 s. The window is taken from `hw.pmic.reg[0x02]`, which `pmicInitLocked()` writes with the vendor byte `0x6C` (108) through the meaning-checked path, so it runs the written 64 s and not the 32 s power-on default. `hw-ip5306-default-window` asserts the byte, its field, 64000 ms and `pmic.light_load_source reg`, so a seeded window cannot stand in for the decode. | No code change. Teeth checked: replacing the decode with a constant 32 s fails that scenario. One citation caveat, row 7. | `sim/hardware_model.cpp` `ip5306LightLoadMs`, `hw-ip5306-default-window.txt` run output, `hw-ip5306-window-decode.txt` |
| 7 | IP5306 citation | The branch attributes the bits 3:2 table to "the IP5306 register document V1.21". `docs/power/datasheet-register.md` records the in-tree IP5306 copy as V1.01 and states that the public datasheet does not tabulate the register map, and the previously recorded source for this table was M5Unified 0.2.20's bitfield comment. The values agree; the provenance did not. | Fixed in the docs: `docs/power/board-power.md` gives both sources, so no claim rests on a document this tree does not hold. | `docs/power/datasheet-register.md` IP5306 row, `docs/power/board-power.md` "IP5306 (Core)" |
| 8 | AXP2101 guard | `pmicWriteForLocked()` compares the modelled meaning at the address, so on `pmic_variant axp2101` a write carrying `axp192-adc-enable-1` is refused and counted rather than landing in DCDC1. `hw-pmic-axp2101-guard` is the negative control: it submits the AXP192 ADC block and asserts 3 violations, `0x82`/`0x83`/`0x84` unwritten, and `pmic.reg.0x82.meaning axp2101-dcdc1-voltage`. `hw-pmic-axp2101-cpu-rail` asserts the vendor block never writes `0x82` on that part at all. | No code change. Teeth checked: making `0x82`'s AXP2101 meaning equal the AXP192 one applies the write and the scenario fails with `pmic.violations expected '3' got '2'`. | `sim/hardware_model.cpp` `pmicWriteForLocked`, the `pmic-axp192-adc` action, both scenarios |
| 9 | Row agreement | For the seven supported boards the model and the firmware row now agree on `ir`, `sd`, `audio`, `rtc`, `rtcBatteryBacked`, `timedWake`, `gps_rail`, `touch`, capacity and battery caps, and the `board-row-*` scenarios assert each pair to one literal and pass. The Waveshare and headless rows are model-only and are covered by `hw-profile-waveshare` and `hw-profile-headless`. | Fixed via rows 3, 4 and 5. `board.timed_wake` was added because that column had no two-sided gate and had just been found stale in two rows. | `sim/driver.cpp` `board.*` queries, `sim/scenarios/e2e/board-row-*.txt`, run output |
| 10 | Scenarios assert | Every new `hw-*` and `board-row-*` scenario asserts rather than prints: 17 `hw-*` scenarios with 2 to 32 `assert` lines each, 7 `board-row-*` with both sides pinned, no `print` and no `xassert` in any new file. The one `print` in `bughunt/timer-overflow-no-timed-wake.txt` is pre-existing; the branch only adds `seed touch true` there. | No change. | `grep -c '^print'` and `^xassert` over the changed scenario files |
| 11 | Build | The merge left an assignment to the branch's local `displayOffRequested`, which master had replaced with the model-backed `hwDisplayOffState()`, and the `pmicSetGpsRailLocked()` move dropped the `refreshRailHoldersLocked()` signature. Both broke the simulator build. | Fixed: the local flag is gone and the helper signature is restored. | `sh sim/build.sh` |
| 11b | Host suite | `time_keeper_persistence_test` compiles `src/FurbleBoard.cpp` with `FURBLE_SIM` and does not link the simulator, so the branch's seam include had nothing to resolve and the whole host build failed at `cmake --build`. | Fixed for good by moving onto master's weak hook, which needs no header: the host stub the intermediate revision needed is deleted and the target compiles unchanged. | `tests/host/CMakeLists.txt:234-244`, `src/FurbleBoard.cpp` weak hook |
| 11c | Board power doc | The rebase of commit 2 left literal conflict markers committed in `docs/power/board-power.md`, because that commit's conflict was staged together with the one that was being resolved. `git diff --check` caught it and the Whitespace gate failed closed. | Fixed by amending the commit that introduced them; every commit in the branch is now marker-free and `git diff --check origin/master...HEAD` passes. | `git diff --check origin/master...HEAD`, per-commit scan |
| 11d | Host suite | `board_profile_test.cpp` pins all 22 columns of every row, including the three literals this branch changes, so the host suite failed on `Core battery caps`, `Tough sd slot` and `Tough feedback`. CI runs that target. | Fixed: the three expectations are updated and the table carries the evidence for the two rows that differ from the Core2 row they share an image with. | `tests/host/board_profile_test.cpp`, `ctest -R board-profile` |
| 11e | Master moved | While this review ran, master merged #497 (`535ad7d6`, plan 354), which took the Tough SD/speaker/RTC row, the Core and Core2 capacities, and the IP5306 session policy. That last one inverted this branch's premise: the firmware now holds `SYS_CTL0` bit 1 so the cut is not armed while awake, and the Core's manual Off is accepted and deferred rather than refused. Landing the branch unchanged would have shipped a model that contradicts the firmware. | Fixed by rebasing onto `535ad7d6` and modelling the merged policy: the boot holds the bit, only the manual Off releases it, the Off is accepted and leaves the state machine ON until the cut fires, `Platform::canTurnDisplayOff()` answers from the modelled bit, and the register decode now calls `include/FurbleIP5306.h` instead of keeping a second table. The Tough row is no longer this branch's change at all. | `git log 535ad7d6`, `src/FurblePlatform.cpp` `keepIp5306BoostOn`, `include/FurbleIP5306.h`, `sim/hardware_model.cpp` |
| 11f | Display gate | Wiring `Platform::canTurnDisplayOff()` to the modelled boost bit made it answer `no` on every board the IP5306 does not describe, because their `boostKeepOn` is false by default. The full suite caught it: 10 scenarios failed, from `display-dim-then-off` to `imu-gesture-wake-*`. | Fixed: the hook is `hwCanTurnDisplayOff()`, which answers yes for any other modelled part and reports the bit only on the IP5306. All 272 end-to-end scenarios pass. | `sim/hardware_model.cpp` `hwCanTurnDisplayOff`, `sh sim/scripts/run-e2e.sh` |
| 11g | CI: memory profile | `sim/coverage/memory_budget.json` was still keyed `profile-core`, while the runner emits `profile-core-4mb`, so the `sim-e2e` memory step failed closed with "no budget for scenario 'profile-core-4mb'". The plan's claim that every consumer of the profile name moved was wrong: this file was missed. | Fixed: the key is `profile-core-4mb`. `run-memory-profile.py` now reports "memory budget passed for 13 dump(s)". | `sim/coverage/memory_budget.json`, `sim/scripts/run-memory-profile.py` |
| 11h | CI: Python tooling | `tests/test_sim_profile_sdkconfig.py` maps a simulator profile to a firmware env and still named the bare `core`, so the derived-heap and draw-buffer tests raised `KeyError: 'core'` / `profile-core-4mb`. | Fixed: the map names `core-4mb`. `python3 -m unittest tests.test_sim_profile_sdkconfig` runs 7 tests, OK. | `tests/test_sim_profile_sdkconfig.py` |
| 11i | CI: static analysis | cppcheck `uselessCallsSubstr` on the new `pmic.reg.<0xNN>` parsing, which assigned a prefix of a string to itself. | Fixed: `rest.resize(dot)`. cppcheck reports no finding on the file. | `sim/hardware_model.cpp` |
| 11j | CI: host harness | `control-zombie-cancel` failed "an empty connect remains idle" on CI in 15.6 s. The target links neither `src/FurbleBoard.cpp` nor the model, and it passed 4/4 locally at 17.3 s; the assertion is a 1 s `waitFor` after a TestSync barrier, so it is timing-sensitive under a loaded runner. | Re-run rather than changed: nothing in this branch reaches it. | `tests/host/control_zombie_cancel_test.cpp:290`, `tests/host/CMakeLists.txt:1690` |
| 11k | Rebase: weak hook | Master added a weak `furble_sim_board_profile()` hook plus `Sim::setBoardProfile()`, which is a better seam than this branch's `sim/board_override.{h,cpp}` and needs no header in a firmware source. Keeping both would have left two overlapping mechanisms. | Fixed: the override files, the `profile()` edit and the host stub are deleted, and the profile-name table moved into `sim/driver.cpp`, which seeds `Sim::setBoardProfile()` from `hw_profile` with `seed board` still winning. The table reads the seeds rather than the model, because the seeds are applied after that block. | `src/FurbleBoard.cpp`, `sim/driver.cpp`, `sim/FurblePlatformSim.cpp:637` |
| 11l | CI: panic fixtures | `sim/fixtures/hardware/panic-icon-no-psram.txt` still seeded the refused bare `core`, so it exited 2 at parse instead of the panic status 3 and the `sim-e2e` panic step failed. `sim/coverage/permutations.json` and `tests/test_run_permutations.py` carried the same name, which would have broken the permutation run. | Fixed: all three name `core-4mb`. The fixture now exits 3 with the `SIM PANIC` and `abort() was called` tokens. | `sim/fixtures/hardware/panic-icon-no-psram.txt`, `sim/coverage/permutations.json`, `tests/test_run_permutations.py` |
| 11m | CI: doc tokens | The first fix for the `seed board` validator called a helper, and `sim/scripts/check-doc-tokens.sh` rejects a `validateSeed` predicate it cannot read as a literal comparison chain, so the sim-e2e doc-token gate failed. | Fixed by keeping master's literal chain. Only `seed board stamps3` exists in the tree, so the narrow list is correct and the profile names stay a `hw_profile` concern. | `sim/scripts/check-doc-tokens.sh`, `sim/driver.cpp` |
| 11n | CI: alternate boards | `sim-e2e`'s "Run certified end-to-end scenarios on alternate boards" step failed on the m5stack-core binary with `ASSERT FAILED: ir.state expected 'idle' got 'absent'` in `hw-audio-ir-sd-faults`, and `ASSERT FAILED: hw.rtc_backed expected 'no' got 'yes'` in `hw-profile-tough`. The m5stick-s3 suite cannot see either: both scenarios are certified for the other panels. | Both scenarios were wrong, not the model. The Tough part has the BT1 cell and `TimedWake::RTC_ALARM` since #497, so `hw.rtc_backed yes` is the merged fact. The Core row has `irPin -1`, so `ir.state` is `absent` by construction; the fault path now runs on the StickC Plus, whose emitter is a plain GPIO on an AXP192 board, and `hw-pmic-ip5306-core` pins the Core's `absent` state so the reason is recorded. All three panel suites pass: 274 on m5stick-s3, 66 on m5stick-c, 54 on m5stack-core. | `sim/scenarios/e2e/hw-audio-ir-sd-faults.txt`, `hw-profile-tough.txt`, `hw-pmic-ip5306-core.txt`, `sh sim/scripts/run-e2e.sh` with `FURBLE_SIM_BOARD_ID` |
| 11o | Rebase: #493 rail ownership | Rebasing onto `79df13a7` conflicted in `sim/hardware_model.cpp`, `sim/FurblePlatformSim.cpp` and `sim/CLAUDE.md`. #493 replaced `PowerBackend::Rail::GPS` with `Rail::EXT_5V` behind a reference-counted `RailAccount`, so the branch's `gps-rail-on/off` action no longer compiled, and the two `Platform::tick()` bodies had to run together. | Fixed: the action takes the `EXT_5V` rail, `tick()` polls the IR fake and then advances the IP5306 timer, and the model keeps master's `railSupply`/`irBurstMs` fields beside its own register and light-load state. `sim/CLAUDE.md` keeps master's rail row and the branch's power and board-identity rows. | `include/FurblePowerBackend.h`, `sim/driver.cpp`, `sim/FurblePlatformSim.cpp` |
| 11p | Row agreement | The row gate covered the capability columns but not `rtcOnBus`, `canPowerOff`, `ledActiveLow`, `irMarkLow` or `gpsPinSource`, so those could drift unobserved. | Fixed: the model carries all five plus the two profile GPS pins, `hw.*` and `board.*` expose each, and every `board-row-*` scenario asserts both sides. The seven rows now agree on capacity, battery caps, canPowerOff, powerOffPath, rtcBatteryBacked, rtcOnBus, timedWake, ledActiveLow, irMarkLow, gpsPinSource, the GPS pins and the rail column. | `sim/hardware_model.cpp`, `sim/driver.cpp`, `sim/scenarios/e2e/board-row-*.txt` |
| 12 | Plan number | The branch added `plans/355-core-sim-align.md`, but master already uses 355 for `355-sim-cmake-entry-point.md` and its `355-review-feedback.md`, so the branch would have added a second plan 355 and this record would have overwritten #521's. The renumbered 363 collided the same way when #488 took 363 for `363-pmic-irq-speaker.md` and its own review record. | Fixed: the plan and this record are 365. Both earlier records are untouched. | `plans/355-review-feedback.md`, `plans/363-pmic-irq-speaker.md`, `plans/363-review-feedback.md` |
| 13 | Memory budget | The `core` to `core-4mb`/`core-16mb` rename moved every consumer, as the plan says. After the rebase the Core budget row is still the 4 MB one and the 16 MB row exists, so `hw-pmic-ip5306-core` reads 532480 KiB internal and `hw-profile-core-16mb` passes. | No change. | `sim/memory_profile.cpp`, `sim/coverage/board_heap.json`, `tools/derive_sim_heap.py`, both scenarios |

## Verification

macOS, SDL dummy video and audio drivers, simulator binary from
`sh sim/build.sh` on this branch rebased onto `6a82c71b`.

| Command | Result |
| :--- | :--- |
| `python3 tools/check_sim_scenarios.py` | manifest complete |
| `python3 tools/check_sim_coverage.py` | 206/206 hardware, 20/20 crash, 11/11 fuzz, 6912 permutations |
| `sh sim/scripts/run-e2e.sh` | every certified `m5stick-s3` end-to-end scenario passes |
| `sh sim/scripts/check-doc-tokens.sh` | passes |
| `python3 tools/pre_review.py --base origin/master` | every gate passes, including the scanner unit tests |
| host suite | `ctest --test-dir ... --output-on-failure` passes every target |
