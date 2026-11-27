# 380 - Review feedback

An independent, adversarial review of the simulator's battery-backed RTC model
([380-sim-rtc.md](380-sim-rtc.md)), the `FURBLE_SIM` TimeKeeper seam and the
eight deep-sleep scenarios.

PR #507 landed as `c644f39e` with no review thread and no review-feedback
record: CI was green and that was the only gate it passed. This record and the
fixes it names land in the follow-up pull request that carries them, so the
audit exists even though the merge did not wait for it. The review rebuilt the
simulator from `origin/master`, ran the new scenarios, and attacked the model
where a simulated clock can lie: the board rows it duplicates, the epoch the
firmware actually accepts, the sidecar it persists, and whether any of it
reaches a firmware build.

| # | Severity | Finding | Resolution | Evidence |
| ---: | :--- | :--- | :--- | :--- |
| 1 | Blocking | No scenario asserted the epoch the firmware accepted. The driver exposes `time.epoch_s` (`sim/driver.cpp:1879-1881`), the plan lists it as a query, and nothing used it: every RTC scenario asserted `time.valid`, `time.source` and `hw.rtc_epoch`, which is the model's own number. A firmware that accepted the chip's sample at the wrong instant, or converted it wrongly, passed every scenario. | Fixed. Every RTC scenario now asserts `time.epoch_s` at the boot, after the waits and across the restart, so the number production holds is checked against the seeded calendar. | `grep -rn "time.epoch_s" sim/scenarios` was empty before this change; the scenario diffs add it. |
| 2 | Non-blocking | The `<FURBLE_SIM_PREFS>.rtc` sidecar was never removed. `removePreferences()` removed the store and its marker, and `sweepStalePreferences()` removed a dead store with its `.tmp.<pid>` sidecars, but neither touched `.rtc`. Every scripted RTC run left a file behind, a crashed run left one for good, and `check-preferences-lifecycle.sh` cannot see them because it globs `furble-sim-preferences-*.bin`. | Fixed in both places, with the sweep's unit test extended to assert that a dead store's `.rtc` goes and a live store's stays. | `sim/driver.cpp` `removePreferences()`; `sim/preferences_sweep.cpp`; `tests/host/preferences_sweep_test.cpp`. |
| 3 | Non-blocking | Plan 380 listed four of the eight scenarios the pull request added. `rtc-persist`, `rtc-cold-boot`, `rtc-start` and `rtc-resume` appeared only under Evidence, so the plan's own scenario table did not describe the implementation. | Fixed. The table lists all eight with their assertions. | [380-sim-rtc.md](380-sim-rtc.md) "Scenarios". |

Verified, no change needed:

| Check | Result | Evidence |
| :--- | :--- | :--- |
| The model agrees with the board profile rows it duplicates | Holds, row for row. `rtcBatteryBacked` and `timedWake` in `sim/hardware_model.cpp` match `src/FurbleBoard.cpp` for all nine profiles, including the two `UNKNOWN` rows (headless and Waveshare) and the `rtc-alarm` StickC Plus2 row. | Table below; the comparison is a scripted extraction of both sources. |
| The `FURBLE_SIM` branch is compiled out of firmware | Holds. `FURBLE_SIM` is defined only by the simulator build; no firmware compile line contains it and no `furble_sim` symbol is in a firmware image. The four weak hooks and both `readRtc`/`writeRtc` branches sit inside `#if defined(FURBLE_SIM)`, and `setSystemTime`/`systemTimeUs` keep the `settimeofday`/`gettimeofday` path otherwise. | `grep -c FURBLE_SIM .pio/build/m5stick-c/build.ninja` = 0; `xtensa-esp32-elf-nm firmware.elf \| grep -c furble_sim` = 0 against 20 `TimeKeeper` symbols. |
| The sidecar does not leak between scenarios | Holds by construction. Every scripted run gets a per-pid generated store (`sim/driver.cpp:2360-2378`), so the sidecar name is unique per run; a scenario that seeds no model calls `hwSetRtcModel("off")`, which discards the sidecar; and the deep-sleep runner deletes `$PREFS.rtc` before its RTC legs (`sim/scripts/run-deep-sleep.sh`). | Code paths above; the runner's `rm -f "$EVIDENCE" "$PREFS.rtc"`. |
| The two clocks a scenario can read are the same clock | Holds. Under `FURBLE_SIM`, `setSystemTime` and `systemTimeUs` call the `furble_sim_*` hooks, and `sim/wall_clock.cpp` implements them and `std::time()` on the one model, so the intervalometer's `std::time()` and `TimeKeeper`'s sample cannot disagree while a chip is installed. | `src/FurbleTimeKeeper.cpp:92-112`; `sim/wall_clock.cpp`. |
| The default path is unchanged | Holds. With no `rtc_model` the model is `OFF`, `hwRtcEpochSeconds()` returns 0, which is what the weak hook returned before the change, and `std::time()` falls back to the host clock. | `sim/hardware_model.cpp` `hwRtcEpochSeconds`; `sim/wall_clock.cpp` `time`. |

Board-row comparison, firmware row against the simulator row:

| Firmware `Profile` | Simulator `board_caps_t` | `rtcBatteryBacked` | `timedWake` | Result |
| :--- | :--- | :--- | :--- | :--- |
| `STICKC` | `STICKC` | true / true | none / none | ok |
| `STICKC_PLUS` | `STICKC_PLUS` | true / true | none / none | ok |
| `STICKC_PLUS2` | `STICKC_PLUS2` | false / false | rtc_alarm / rtc_alarm | ok |
| `STICKS3` | `STICKS3` | false / false | pmic_timer / pmic_timer | ok |
| `CORE` | `CORE` | false / false | none / none | ok |
| `CORE2` | `CORE2` | true / true | none / none | ok |
| `TOUGH` | `TOUGH` | false / false | none / none | ok |
| `UNKNOWN` | `HEADLESS` | false / false | none / none | ok |
| `UNKNOWN` | `WAVESHARE` | false / false | none / none | ok |

The two rows agree today, but nothing compares them: they are a hand copy of
two columns of `src/FurbleBoard.cpp` into `sim/hardware_model.cpp`, and a
firmware row that changes without the simulator row drifts silently. This is
recorded rather than fixed: the simulator links the firmware sources, so the
follow-up is to read `Board::profileFor()` for those two columns and keep the
literals only for the profiles the firmware switch does not carry (headless and
Waveshare). That needs its own change and its own scenario run.

Judged false:

| Hypothesis | Why it does not hold | Evidence |
| :--- | :--- | :--- |
| `rtc_model unbacked` makes `std::time()` disagree with the `TimeKeeper` sample | It does after a power-off: the chip loses its reading, so `hwRtcEpochSeconds()` returns 1970 plus uptime while `TimeKeeper` refuses the sample. Nothing acts on that number, because both intervalometer branches are gated on `trustedWallClock()`, which is false there. The mode has no positive scenario, which is recorded below, but the divergence does not change a decision. | `src/FurbleUIIntervalometer.cpp:28`; `sim/hardware_model.cpp` `hwRtcPowerOff`. |
| A scenario can pass while the firmware reads a clock the chip does not hold | Only through finding 1, which is fixed: with `time.epoch_s` asserted, the accepted epoch has to equal the seeded calendar plus the elapsed virtual time. | The scenario diffs. |

## Evidence

Simulator built from `origin/master` (`81139818`) with
`FURBLE_LVGL_DIR=/tmp/rvw507-lvgl sh sim/build.sh`. Every RTC scenario was run
headless (`SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy`) and exits 0 with the
new epoch assertion ok:

| Run | New assertion | Result |
| :--- | :--- | :--- |
| `intervalometer-deep-sleep-rtc-stale.txt` | `time.epoch_s 1767225600` | ok |
| `intervalometer-deep-sleep-rtc-valid.txt` | `time.epoch_s 1767225600` | ok |
| `intervalometer-deep-sleep-rtc-late.txt` | `time.epoch_s 1767225600` | ok |
| `intervalometer-deep-sleep-rtc-lost.txt` | `time.epoch_s 0` | ok, no sample accepted |
| `intervalometer-deep-sleep-rtc-persist.txt` | 1767225600, 1767225603, 1767225603 after the restart, 1767225604 | ok on all four |
| `intervalometer-deep-sleep-rtc-cold-boot.txt` | 1767225600, then 0 after the cold boot | ok on both |
| `sh sim/scripts/run-deep-sleep.sh` | `time.epoch_s 1767225606` in the resume leg | exit 0, the leg logs `rtc backed restored from the chip: registers set at 1767225606 s` |

The sidecar leak was reproduced before the fix: six `*.rtc` files were left in
`.pio/` after the runs above, five of them belonging to generated stores whose
`.bin` had already been removed, which is the leak finding 2 describes.

Gates run on the changed tree: `sh sim/scripts/run-invalid.sh` (exit 0, `CLI and
environment validation passed`), `sh sim/scripts/check-hardware-quirks.sh`
(`Hardware quirk gate passed`), `sh sim/scripts/check-preferences-lifecycle.sh`
(`preference ownership lifecycle check passed`), `sh sim/scripts/check-doc-tokens.sh`
(`sim documentation tokens are present`) and `python3 tools/check_sim_scenarios.py`
(`sim scenario manifest is complete`).

Recorded for follow-up, not fixed here:

- `rtc_model unbacked` has no positive scenario. Only the three invalid-seed
  fixtures name the mode. The behaviour it models (a chip that counts while the
  rail is up and comes back unset) is worth one deep-sleep scenario, and it
  needs its own manifest and coverage entries.
- The board-row duplication above has no gate.
