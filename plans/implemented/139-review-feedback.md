# 139 - Review feedback for the wall clock across soft resets

## Scope

This is the review-feedback record for the plan 139 follow-up on branch
`fix/time-across-soft-reset`. Every finding is recorded as fixed, already
fixed or a false positive, with evidence.

## Branch `fix/time-across-soft-reset` (84437075)

Source: Subagent review.

| # | Finding | Resolution | Evidence |
| ---: | :--- | :--- | :--- |
| 1 | Lockout. `restoreRetained()` kept the GPS, NTP or companion source. The kept value is whatever the RC-carried system clock reads, and SNTP writes that clock without telling TimeKeeper. `shouldAccept()` rejects an equal-priority correction more than `BACKWARD_TOLERANCE_MS` (5 s) backwards whatever the uncertainty, and `validAt()` expires a sample only at the seven-day ceiling. A kept clock more than 5 s fast therefore rejected every later GPS fix, and each soft reset re-anchored it. This broke the rule in `loadPersisted()` that authority is demoted across a reboot. | Fixed. The kept sample is demoted to at most `RTC`; an NVS-seeded record stays `NVS`. GPS, NTP and companion time outrank it and correct it in either direction. The intervalometer trusted-clock check now rejects an `RTC` sample when no RTC chip is present, so the kept clock is never used for a resume deadline. After a panic the resume is also discarded, because the resume path needs a verified timed-wake marker. The header comment, plan 139, `src/CLAUDE.md` and both console docs describe the demoted source. | `src/FurbleTimeKeeperPolicy.cpp`, `src/FurbleUIIntervalometer.cpp`, `tests/host/time_keeper_policy_test.cpp` (6 s fast, then GPS and companion accepted), `tests/host/time_keeper_persistence_test.cpp` (end-to-end 6 s fast, then a GPS fix is accepted and the system clock follows). The reviewer's `lockout.cpp` repro reported the fix rejected at 0, 24 and 720 h before the change and accepted at all three after it. Keeping the GPS source as a mutation fails both tests. |
| 2 | The uncertainty model is not a bound. The system time is boot time plus an RTC offset plus esp_timer. `esp_rtc_get_time_us()` converts all RTC ticks since the last accumulation with one calibration, refreshed only at boot or light-sleep entry, and every sdkconfig uses `CONFIG_RTC_CLK_SRC_INT_RC`. Without light sleep the hand-off error is the RC drift since the previous boot, not since the record's anchor, and 100 ppm is the crystal figure. | Fixed in wording. The `SOFT_RESET_UNCERTAINTY_MS` comment and the `restoreRetained()` comment call the value an estimate, not a bound, and say the hand-off accuracy depends on RC slow-clock drift since the last calibration and is unmeasured. Plan 139 adds the StickS3 measurement (set the clock from GPS, run several hours with the display on, `esp_restart()`, compare the kept value with the next GPS fix) and notes that a separate growth constant should come from it. The demotion in item 1 makes the error self-correcting. | `include/FurbleTimeKeeper.h`, `plans/implemented/139-persistent-time.md` |
| 3 | Wording. `ESP_RST_WDT` also covers RTC and super watchdog system resets that clear the RTC domain. The code already fails closed there but the comments overclaimed. `ESP_RST_JTAG` and `ESP_RST_CPU_LOCKUP` are never produced on these chips. | Fixed. The `clockSurvivedReset()` comment says these resets usually keep the RTC timer and that an RTC watchdog system reset reads behind the record and fails closed. `JTAG` and `CPU_LOCKUP` are removed from the switch and the test expectation; IDF `esp_system/port/soc/esp32s3/reset_reason.c` and `esp32/reset_reason.c` never return them. Plan 95 no longer says only power-on loses the clock: brownout, a clock glitch and RTC or super watchdog system resets also lose it. Plan 139 and both console docs say: "Most watchdog resets keep it; an RTC watchdog system reset clears the clock and falls back to NVS." | `src/FurbleTimeKeeper.cpp`, `tests/host/time_keeper_persistence_test.cpp`, `plans/95-engineering-lessons.md`, `docs/console-commands.md`, `docs/wiki/Console-Commands.md` |
| 4 | The reviewer's `tools/pre_review.py --base origin/master` run timed out at 300 s with no failures before that. | Fixed. Re-run to completion on the follow-up commit with no time limit. | `PASS: 8 gates`: whitespace, diff hygiene, settings contracts, C++ safety (7 files, 0 errors), CI workflow contract, simulator scenario manifest, simulator hardware coverage and 109 scanner unit tests. |

Validation: `time_keeper_policy_test` and `time_keeper_persistence_test`
pass under ASan and UBSan. The reviewer's `lockout.cpp` repro, built against
each commit's policy source, prints `accepted=0` at 0, 24 and 720 h before the
fix and `accepted=1` after it. A mutation that restores `retained.source`
fails both tests at their demoted-source checks. `m5stick-s3` builds;
`tools/stack_budget.py --env m5stick-s3` passes every task and
`tools/check_size_budget.py --env m5stick-s3` passes (flash 1,849,376 of
3,145,728, DRAM 47,208 of 48,128, IRAM 132,864 of 134,144).
`sim/scripts/check-doc-tokens.sh` passes. The sanitized host suite passes 182
of 183. The failure is `control-tsan-wrapper-contract`, a shell-only test that
timed out at its 10 s CTest limit at host load average 20 to 47. This branch
does not touch its scripts. The first `--clean` run was stopped by an external
SIGTERM during the build. The rerun continued in the same build tree.
Hardware validation is deferred; see plan 139.
