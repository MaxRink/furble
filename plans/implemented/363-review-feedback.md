# 363 - Review feedback

An independent review of `fix/pmic-irq-and-speaker` (head `cb17e193`, rebased
onto `b57f0118`) built the branch, read the vendor drivers and the M5PM1 V1.9
manual, and returned four blocking findings and ten non-blocking. Every finding
was verified against this tree before anything changed, and none was dismissed
as false. One finding cited stale line numbers; that is recorded in its row and
does not change the defect.

The blocking fixes are in the `B` rows, the non-blocking in the `N` rows. Where
a finding asked for a choice, the row gives the choice and why.

| # | Finding | Verified | Disposition | Evidence |
| ---: | :--- | :--- | :--- | :--- |
| B1 | The amplifier settle parked the first tone of every pattern, because the speaker session ended after each pattern and `M5.Speaker.end()` powered the codec down. The beep landed about a second after the LED and vibration. | Confirmed: `update()` called `stopSpeaker()` after the final tone, `beginSpeaker()` took a fresh `now + 1000 ms` deadline, and this fixture called `settleAmplifier()` after every signal. | Fixed by keeping the session open with an idle timeout, the first of the three options: the settle is a property of an amplifier enable, so a boot-one-shot would play too early after a real power-down and "enable at boot" holds the codec and rail up through display-off sleep. `AMP_IDLE_TIMEOUT_MS` (10 s) closes the session after the last tone; `update()` records `m_SpeakerIdleDeadline` instead of calling `stopSpeaker()`, keeps the timer running while the amplifier is open, and closes on the deadline. Ten seconds spans the intervalometer's one second countdown cadence in its last three seconds and the shutter that follows. The power source is now sampled once per pattern so a multi-pattern session cannot hold a stale verdict. New `boundary-feedback idle-window` mode: a pattern after a completed pattern sounds its first tone on the next timer pass, a pattern past the window still waits, and console mode plays instead of parking. | `src/FurbleFeedback.cpp:51`, `:378`, `:397`, `:563`, `:614`, `:617`; `tests/host/boundaries/feedback_driver_test.cpp:198-262`; `tests/host/CMakeLists.txt:2884` |
| B2 | A failed PMIC read concluded "mains", the unsafe direction, and suppressed its own retry: `readRegister8` is `value_or(0)`, `getGPIOInput` returns false on failure, `isCharging()` maps false to `is_charging`, and the re-read was guarded on `!charging`. | Confirmed against the vendor sources, exactly as cited. | Fixed: mains power requires a positive VBUS reading, the charge flag is no longer an input at all, and the re-read is keyed on a zero VBUS reading alone. Boundary case added: `setBatteryPower(0, true)` must still cap 255 to 191. The suggested `PWR_SRC` read through `getPowerSource()` was not adopted as the release condition, because that vendor read is also `value_or(0)`, so a failed `PWR_SRC` read reports `vin`, which is mains; the VIN measurement is the only signal that cannot fail towards mains and it is already one block read. Reasoned in the plan. | `I2C_Class.cpp:83-88`, `M5PM1_Class.cpp:161-165`, `Power_Class.cpp:2553-2558`; `include/FurbleFeedbackPolicy.h:36`; `src/FurbleFeedback.cpp:452-478`; `tests/host/boundaries/feedback_driver_test.cpp:182-190`; `tests/host/feedback_volume_policy_test.cpp:44-56` |
| B3 | `pmic-irq` cannot fail on the pre-fix code and no host test compiled the four production call sites; `FurblePlatform.cpp` appeared in zero test targets. | Confirmed: the target linked only the header and the shim, re-implemented the clear in its own `clearStatus()`, and `grep FurblePlatform.cpp tests/host/CMakeLists.txt` returned nothing. | Fixed: new `platform-wake` target in `tests/host/platform` compiles the unchanged `src/FurblePlatform.cpp` against `sim/shim/M5PM1.h` with M5Unified, ESP GPIO/sleep/timer and boot-mark stubs, arms the wake, raises GPIO4 and a 5VIN bit, and drives `motionWakeSample()`, `clearMotionWake()`, `disarmMotionWake()` and a re-arm. Proven by deletion: removing the `irqClearSysAll()` call made it print `FAIL: motionWakeSample left IRQ Status 2 latched` and exit 1. The shim gained the GPIO-configuration, power-source, LED, timed-wake and RTC RAM surface the translation unit calls. `pmic-irq` stays as header/model coverage with its scope now stated in the file. | `tests/host/platform/motion_wake_test.cpp`; `tests/host/platform/platform_test_support.cpp`; `tests/host/platform/shims/`; `tests/host/CMakeLists.txt:2822-2860`, `:2874`; `sim/shim/M5PM1.h`; `tests/host/pmic_irq_test.cpp:1-26` |
| B4 | The refused-watchdog-arm premise was false and three documents repeated it. | Confirmed: `watchdogEnable()` already logs the failed set and verify at ERROR and `m5pm1Access()` logs a WARN. The reviewer cited `:242`/`:249`; on the rebased tree those lines are `:262`/`:269`, so the numbers were stale and the claim was not. Also confirmed the added line was wrong for the normal disarm path. | Fixed: one neutral WARN parameterised on the requested state (`arm`/`disable`), no duplicate log, and the "silent" premise deleted from `src/main.cpp`, `plans/363` and `src/CLAUDE.md`. | `src/main.cpp:471-480`; `src/FurblePlatform.cpp:262`, `:269`; `include/FurblePlatform.h:401`; `plans/implemented/363-pmic-irq-speaker.md` item 5 and "Report a refused watchdog change" |
| N1 | The `rtcOnBus` guard is unreachable for every board, so the comment overstates it. | Confirmed: only `STICKC_PLUS2` sets `rtcOnBus`, and its timed wake is `RTC_ALARM`. | Fixed: the boot comment and `src/CLAUDE.md` say the compound guard is a contract, not a live branch, and `board-profile` asserts that `rtcOnBus` implies an `RTC_ALARM` timed wake so a new row fails the test. | `src/FurblePlatform.cpp:109-119`; `tests/host/board_profile_test.cpp:155-161`; `src/FurbleBoard.cpp:44-46` |
| N2 | The shared 3V3_L3B_AU claim for the amplifier and the IR contradicts the cited guide. | Confirmed. In-repo evidence: `plans/91-mic-trigger.md:40` reads the same guide as L3B carrying the ES8311 codec, the LCD backlight and the speaker, and `docs/power/board-power.md:112` puts the IR rail on the 5 V boost. The reviewer's L2/L3A detail is not corroborated in the tree (it uses L2 as a PM1 power state) and is not repeated. | Fixed: the shared-IR clause is dropped from the policy header, the `applyVolume()` comment, the root `CLAUDE.md`, `plans/95` and both places in `plans/363`, and the unsupported rail-map citation is removed. | `include/FurbleFeedbackPolicy.h:9-21`; `src/FurbleFeedback.cpp:428-440`; `CLAUDE.md:374-380`; `plans/95-engineering-lessons.md:71-80`; `plans/91-mic-trigger.md:40`; `docs/power/board-power.md:112` |
| N3 | The plan carried a stale base commit and host test count. | Confirmed: it named `92618bff`; the rebased base is `b57f0118`. | Fixed: "Implementation state" names `b57f0118`, and the counts were refreshed from this round's runs. | `plans/implemented/363-pmic-irq-speaker.md` Implementation state |
| N4 | The write-0-to-clear mechanism belongs to the vendor driver; the manual only states the hold-until-cleared rule. | Confirmed in both documents and in the shim comment. | Fixed: the citation is split in `include/FurblePmicIrq.h`, `sim/CLAUDE.md` and `plans/363`; the manual is cited for the hold rule and the driver for the zero write. | `include/FurblePmicIrq.h:12-24`; `sim/CLAUDE.md`; `plans/implemented/363-pmic-irq-speaker.md` Sources |
| N5 | "A full battery still counts as mains power" should read "still on the cable". | Confirmed, and the sentence was about to become wrong anyway. | Fixed: the policy comment now says a full battery still on the cable keeps VBUS asserted and therefore counts as mains, which is the actual rule after B2. | `include/FurbleFeedbackPolicy.h:26-34` |
| N6 | A parked tone is dropped in console mode, because `UI::task` skips `lv_task_handler`. | Confirmed at `src/FurbleUI.cpp:13445`. | Fixed: `Feedback::setTimerPathActive()` records whether the LVGL timer path runs, `UI::setDisplayMode()` sets it from the console flag, and `startTone()` plays immediately instead of parking when it is false. Covered by the `idle-window` console case. | `src/FurbleFeedback.cpp:195-200`, `:393-404`; `src/FurbleUI.cpp:13388-13402`; `include/FurbleFeedback.h:71-75`; `tests/host/boundaries/feedback_driver_test.cpp:246-262` |
| N7 | The clear order destroyed a late GPIO4 edge for no gain. | Confirmed: Status 2 was cleared first, so a GPIO4 edge arriving between the writes was latched and then cleared. | Fixed by swapping the order, not by editing the comment: `clearLatched()` now writes Status 1 then Status 2, and the comment explains that Status 1 is what the poll consumes and that nothing reads the Status 2 bits. `pmic-irq` and `platform-wake` both still pass, so the swap changed no outcome. | `include/FurblePmicIrq.h:47-63`; `tests/host/pmic_irq_test.cpp`; `tests/host/platform/motion_wake_test.cpp` |
| N8 | The IRQ clear should be labelled defensive and cite the open hardware question in plan 20. | Confirmed: plan 20 says the physical event path is uncertified and the hardware gate must pass. | Fixed: `src/CLAUDE.md`, `include/CLAUDE.md` and the plan design say the clear is defensive, cite `plans/implemented/20-imu-hw-motion.md` and scope the evidence to the shim. | `src/CLAUDE.md` PMIC bullet; `include/CLAUDE.md`; `plans/implemented/363-pmic-irq-speaker.md` |
| N9 | `armMotionWake()` now fails on either clear; that should be stated. | Confirmed: `clearLatched()` returns `gpio && system` and the arm returns false. | Fixed: stated on the template, in `src/CLAUDE.md` and in the plan design. | `include/FurblePmicIrq.h:55-63`; `src/CLAUDE.md`; `plans/implemented/363-pmic-irq-speaker.md` |
| N10 | The new fields are unsynchronised. | Confirmed: `signal()` and `startTone()` write what the LVGL timer callback reads, with no lock. | Fixed by documenting the contract in the header and the plan: whole-word stores of pattern state, a stale read can drop one retry and never corrupt a pattern, and it must not become a cross-task protocol. | `include/FurbleFeedback.h` private fields; `plans/implemented/363-pmic-irq-speaker.md` "Keep the session open across a burst" |

## Also fixed, from CI

| # | Source | Finding | Resolution | Evidence |
| ---: | :--- | :--- | :--- | :--- |
| C1 | PR 488, `cppcheck and clang-tidy` | `clang-analyzer-deadcode.DeadStores`: "Value stored to 'caps' during its initialization is never read" in `Feedback::onBatteryPower()`. `FURBLE_HAS_VBUS_SENSE` is 1 on the StickS3, so `FURBLE_HAS_VBUS_SENSE \|\| caps.charging` short-circuited and the capability read was dead there. | Already fixed once on the branch (`7c41e477`, folding the caps read into the expression), then superseded here by the B2 restructure: the charge capability is asked for only when VBUS is not readable, so the read is neither dead nor called when it cannot change the answer. Keeping both fixes would leave a call that is still short-circuited away. | `src/FurbleFeedback.cpp:452-478` |
| C2 | PR 488, local `python3 tools/check_static_analysis.py --base origin/master` on the simulator database, after the third rebase | `clang-diagnostic-error`: `src/FurbleUI.cpp:13398` and `:13405`, "no member named 'setTimerPathActive' in 'Furble::Feedback'". The simulator's include order puts `sim/shim` before `include/`, so `src/FurbleUI.cpp` compiles against `sim/shim/FurbleFeedback.h`, and the N6 fix never added the method there. The pushed head `5c596614` carried no CI run, so nothing had compiled the simulator since N6 landed. `sim-e2e` and `static-analysis` would both have failed. | Fixed: the shim declares `setTimerPathActive(bool)` as a no-op with the reason (the simulator has no speaker). Verified by compiling the four simulator translation units this branch touches or that include the shim (`src/FurbleUI.cpp`, `sim/FurblePlatformSim.cpp`, `sim/driver.cpp`, `sim/main.cpp`) with the simulator database's own command line: all exit 0, and the re-run static analysis is clean. | `sim/shim/FurbleFeedback.h:94-98`; `src/FurbleUI.cpp:13398`, `:13405` |
| C3 | PR 488, `Python tooling tests` | `tests/test_build_inventory.py`, `test_no_exemption_is_also_built` and `test_every_firmware_source_is_built_or_exempt`: "redundant exemption: src/FurblePlatform.cpp is already built". The `platform-wake` target puts the production translation unit in the host build list, so the exemption that existed only because no host target compiled it became stale, and the inventory checker fails on both the uncovered-source rule and the redundancy rule. | Fixed: the entry is deleted from `tests/build_inventory_exemptions.json`. `python3 -m unittest discover -s tests -p 'test_*.py'`, the command the job runs, passes. | `tests/build_inventory_exemptions.json`; `tests/test_build_inventory.py` |

## Gate results

All figures are from `/private/tmp/furble-pmicfix` on the tree rebased onto
`7caf0468`, plus this round. The host and sanitized builds are
`/tmp/furble-pmicfix-host-build2` and `.pio/host-sanitized`.

| Gate | Result |
| --- | --- |
| Host `ctest` (`ctest -j 8`) | 203 of 203 passed. The count rose from 198 with `platform-wake` and `boundary-feedback-idle-window` (201) and master's `control-gps-race` pair in #487 (203). |
| `boundary-feedback` all six modes, `boundary-headless-service`, `boundary-headless-service-watchdog-refused` | Passed, and the console-mode close assertion is in `idle-window`. |
| `platform-wake` | Passed. Proven to fail on the pre-fix code twice: see the B3 row and the deletion proofs below. |
| `board-profile`, `pmic-irq`, `feedback-volume-policy` | Passed, including the new `rtcOnBus` implication assertion. |
| ASan/UBSan subset (`tools/run_sanitized_host_tests.sh`, `-R` over `boundary-feedback*`, `boundary-headless-service*`, `platform-wake`, `pmic-irq`, `feedback-volume-policy`, `board-profile`) | 12 of 12 passed, exit 0. |
| `python3 tools/pre_review.py --base origin/master` | 10 gates passed, exit 0, 0 failures and 1 warning: diff hygiene cannot map the `src/FurbleUI.cpp` change to a doc page token, because the change restores the master console behaviour instead of adding a user-facing page. |
| `sh sim/scripts/check-doc-tokens.sh` | `sim documentation tokens are present`. |
| `python3 tools/check_static_analysis.py --base origin/master --build-dir <host> --build-dir <sim>` | Clean on the host and simulator databases. See below. |

## Behavioural verification

Ran on this head, not read off the code:

| Case | Command | Output |
| --- | --- | --- |
| B1, pattern after a completed pattern inside the idle window | `./boundary_feedback_test idle-window` | `feedback.idle first_pattern=settled`, `feedback.idle second_pattern=immediate`, `feedback.idle idle_close=1 recold=settled` |
| B1, console mode plays instead of parking, and the next pattern closes the session | same run | `feedback.idle console_mode=played_and_closed` |
| B2, zero VBUS with the charge flag set | `./boundary_feedback_test sound` | `feedback.sound power=zero-vbus-charging stored=255 applied=191`, next to `power=battery stored=255 applied=191` and `power=vbus stored=255 applied=255` |
| B3, deletion proof 1: Status 2 clear removed from `motionWakeSample()` | `./platform_wake_test` after the deletion | `FAIL: motionWakeSample left IRQ Status 2 latched`, exit 1 |
| B3, deletion proof 2: `PmicIrq::clearLatched()` reduced to Status 1 | `./platform_wake_test` after the deletion | `FAIL: armMotionWake left IRQ Status 2 latched`, exit 1 |
| B3, restored | `./platform_wake_test` | `platform-wake: PASS` |

Both deletions were reverted with `git checkout` and the tree is clean.

## Static analysis

The four databases CI uses are the host, the simulator and the two firmware
profiles. This host builds two of them:

- **Host.** `cmake -S tests/host -B /tmp/pmicfix-static-host
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -G Ninja`. This database now compiles
  `src/FurblePlatform.cpp` for the first time, through the `platform-wake`
  target, so the four production IRQ call sites are analyzable.
- **Simulator.** Built from the pinned dependency clones the workflow uses
  (`M5GFX` 0.2.27, `M5Unified` 0.2.20, `lvgl` v9.4.0, the pinned `TinyGPSPlus`
  commit) with `-DFURBLE_DEP_ROOT` and `-DFURBLE_LVGL_DIR`. This is the database
  the previous round could not build at all, which is how the `C2` finding
  survived: it is a `clang-diagnostic-error` only the simulator database can
  see.
- **Firmware, `m5stick-s3` and `m5stick-c`.** Not built here. They need the
  Xtensa GCC toolchain package, which this host does not have, and a full
  ESP-IDF build, which does not fit in 3.7 GiB of free disk. CI builds both with
  `tools/firmware_compile_db.py` and is the only checker for them. The `C1`
  dead-store that failed the earlier job was a firmware-database finding, so the
  firmware databases are the part of this gate that is unverified locally.

After the `C2` fix the gate reports no findings on either database, with
`clang-tidy` from Homebrew LLVM 23.1.6 rather than CI's Ubuntu package, and
`cppcheck` 2.22.0. Version skew is a real difference: a check that exists in one
release and not the other can only fail in CI.
