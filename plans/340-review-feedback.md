# 340 - Review feedback for the GPS UART install failure

## Scope

This is the review-feedback record for #509 (`fix/gps-uart-install`). The head
under review is `0a0273f4`; this remediation rebases it onto fork master at
`81139818` and fixes what the review found. The author's self-verification in the
PR body is not the review: every claim below was re-derived from the source, the
pinned ESP-IDF tree and a fresh simulator build.

Simulator evidence only. No hardware run is claimed here or in the plan.

## Findings

| # | Finding | Resolution | Evidence |
| ---: | :--- | :--- | :--- |
| 1 | The failure branch publishes `m_UartInstallFailed` before `m_Queue = NULL`, so a reader that sees the flag can still hold the old handle. The pinned driver writes the event queue into `*uart_queue` and then, when its interrupt allocation fails, calls `uart_driver_delete()`, which frees that queue. A failed install can therefore leave a dangling handle. Another task that observed the flag first could reach `reset()` and call `xQueueReset()` on a queue that no longer exists. | Fixed. The handle is cleared before the flag is published. The clear itself is load bearing, not defensive: it is the only thing that makes the null guard in `reset()` correct on that path. | `src/FurbleGPS.cpp:207` clears and `src/FurbleGPS.cpp:208` publishes. The dangling handle is what `components/esp_driver_uart/src/uart.c` in framework-espidf 3.50402.0 leaves: `uart_driver_install()` writes `*uart_queue` after allocating the driver object, and its `err:` label calls `uart_driver_delete()` and returns the error. |
| 2 | Nine `file:line` citations into `src/FurbleGPS.cpp` in six other docs were stale after the rebase. This PR adds 45 net lines to that file and master's citation gate, which #511 merged, re-checks every citation into a changed file. `pre_review` failed on all nine. | Fixed. Eight were shifted by the hunk map and one, `Control::updateGPS`, was already three lines stale and now points at the call. | `python3 tools/check_doc_citations.py --base origin/master`: `0 errors of 285 citations in 42 files`. Files: `docs/components/at6668.md`, `plans/00-hardware-experiments.md` (two), `plans/50-companion-app-design.md`, `plans/implemented/14-gps-pcas.md` (three), `plans/implemented/15-gps-power.md`, `plans/implemented/21-imu-dead-reckoning.md`. |
| 3 | The fake returned `ESP_ERR_INVALID_STATE`. The pinned driver never returns that from `uart_driver_install()`: its own checks and its allocation path return `ESP_FAIL`, and the interrupt allocation it can fail on returns only `ESP_ERR_INVALID_ARG` or `ESP_ERR_NOT_FOUND`. The plan's documented cause, internal RAM exhaustion, is the allocation path, so the scenario exercised a code the firmware will not log. | Fixed. The fake returns `ESP_FAIL`, and `docs/sim.md`, `sim/CLAUDE.md`, the plan and the PR body say so. The decision path only branches on `!= ESP_OK`, so this is fidelity, not coverage. | `sim/fake_uart.cpp:518`. `components/esp_driver_uart/src/uart.c` returns `ESP_FAIL` for the port, buffer and allocation checks and for an already installed driver; `esp_intr_alloc_intrstatus` documents `ESP_ERR_INVALID_ARG` and `ESP_ERR_NOT_FOUND`. The fixed run logs `Failed to install gps uart: ESP_FAIL (0xffffffff)`. |
| 4 | `docs/wiki/Console-Commands.md` mirrors `docs/console-commands.md` and was not updated, so the two copies of the `gps` status reference disagreed. `CLAUDE.md` maps a console reference change to the console doc, the README serial console block and `docs/wiki/`. | Fixed. The same paragraph is in both copies. The in-repo wiki copy needs a manual push after merge, as `CLAUDE.md` requires. | The two paragraphs are byte identical; `sh sim/scripts/check-doc-tokens.sh` passes. |
| 5 | The PR body's "Not verified" bullet said the firmware build never compiles `src/FurbleGPS.cpp`. That is false. Every firmware lane compiles it, and the only red firmware lane at this head is the Core OTA debug app slot, which grew by the code this PR adds. | Fixed in the PR body: the firmware compile is covered by CI on all twelve envs, the size lane is repo-wide, and the local gap is the hardware run only. | `src/CMakeLists.txt:17` lists `FurbleGPS.cpp` unconditionally in `furble_sources`. CI run 45773425629 built eleven firmware envs green and failed `build (m5stack-core-debug)` on size. |
| 6 | `reset()` returned before `m_RxBuffer.clear()`, so the parser buffer stayed untouched for a reason that has nothing to do with a null queue. Nothing fills that buffer without a driver today, but the contract is now conditional for no benefit. | Fixed. The clear runs first and only `uart_flush()` and `xQueueReset()` are guarded. | `src/FurbleGPS.cpp:241` and the clear at `src/FurbleGPS.cpp:243`. |
| 7 | The plan's motivation said the driver allocates from "internal DMA capability memory". It allocates with `MALLOC_CAP_INTERNAL \| MALLOC_CAP_8BIT` because every board sets `CONFIG_UART_ISR_IN_IRAM`, so the claim is internal RAM, not a DMA capability request. | Fixed in the plan, both in the motivation and in the remaining gate. | `components/esp_driver_uart/src/uart.c` `UART_MALLOC_CAPS`; `CONFIG_UART_ISR_IN_IRAM=y` in `sdkconfig.m5stick-s3` and `sdkconfig.m5stack-core`. |
| 8 | `getInstance()` has no mutual exclusion. Two tasks can both enter the install body; the loser gets `ESP_FAIL` "UART driver already installed" and now latches the session-wide absence, so a driver and task installed by the winner read `absent` and `enable()` never arms. This is the only path by which a healthy receiver can be flagged absent, and it is what the "absent state sticks" attack finds. | Accepted, not fixed here. The race pre-dates this PR and its pre-fix outcome was worse: the loser went on to reconfigure the port and created a second GPS task on the same queue. Closing it needs a mutex or `std::call_once` around the install, which is firmware size in the lane that is already over its app slot. Recorded as the follow-up below. | Every `GPS::getInstance()` caller and the boot order are in `src/main.cpp`: the companion task once the companion is enabled, the console task before `vUITask`, and the UI constructor. A first-touch overlap is rare and needs a companion client or a typed console command during the boot window. |
| 9 | The disabled plus failed-install combination has no scenario: `disable()` stores `UNKNOWN` and the derive reports `absent`, but only the enabled path is certified. | Not a defect. `disable()` is safe with no driver and the derive is the same code the certified scenario exercises. Recorded so a later GPS change can add the pair if it touches `disable()`. | `GPS::beginSaveOnShutdown()` returns before any UART read when the backend is not UBX (`src/FurbleGPSUbx.cpp:244`), and `railOffOrRestart()` only cuts the rail because `m_UbxStopped` can only be set on the UBX path. |

## Attack list

1. **Can the absent state stick across a later successful install?** Not across a
   success, because no install is attempted after the latch: the flag gates the
   whole body of `getInstance()` and nothing clears it. Recovery is a reboot,
   which the plan and `docs/console-commands.md` state. The absent path's 60 s
   retry in `serviceProbe()` cannot apply because the task that would run it is
   never created. The only way to a set flag plus a working driver is finding 8.
   That is a pre-existing race, and its pre-fix outcome was two GPS tasks.
2. **Does any other path take the power lock or touch a null UART handle?**
   `acquirePowerLock()` is called from `enable()` after the early return, and
   from `onProbeLocked()`, `enterDegraded()`, `beginWindow()`, `beginBurst()` and
   the cycle service, all of which run on the GPS task that is never created. The
   `xQueue` calls on `m_Queue` are the two receives in `task()` (no task), the
   reset in `reset()` (guarded) and `wakeGPSService()`, which returns on a null
   queue by itself. `disable()` is safe on the no-driver boot: the shutdown save
   returns on the UBX gate and the rail cut needs no UART. Console transmit
   commands still reach `uart_write_bytes()` because `m_Enabled` follows the
   setting, and the driver returns `-1` with `uart driver error` instead of
   asserting; `sendRaw()` and `sendBinaryFrame()` report the failure to the
   caller.
3. **Is the atomic needed and correctly ordered?** Needed. The flag is written on
   whichever task wins the install and read from the UI timer, the console, the
   companion service and the simulator query, so a plain `bool` would be a data
   race. Ordered wrong in the original: finding 1. Correct now.
4. **Do the console, the UI and the simulator agree?** Yes, through one funnel.
   The console row, the GPS Data page receiver row, the raw NMEA page and
   `sim/driver.cpp`'s `gps.receiver` all call `getReceiverState()`, and the
   endpoint scenario asserts `ui.gps_cycle`, which the simulator reads from the
   rendered label text rather than from the state behind it.
5. **Does anything read `isEnabled()` in a way that changes behaviour?** No. The
   readers are the GPS widget and remote-GPS menu visibility, the timezone
   service (which also requires `Fix::LIVE`), the console print and the
   simulator's `gps.enabled` query. None of them start work, allocate or touch the
   UART. The setting keeps deciding `m_Enabled`, which is the same split the
   fixed-baud no-receiver path already had, and the scenario pins it as
   `gps.enabled 1` with `gps.receiver absent`.
6. **Does the new scenario fail on the pre-fix code?** Yes, and it still does
   after this remediation. See Validation.

## Judged false, or not defects

- The extra `m_ReceiverState.store(absent)` in `getInstance()` is not dead code:
  the task-side readers of the raw word are the only other readers, and keeping
  the word coherent costs nothing. The derive is what the UI, the console and the
  simulator read.
- `reset()` is public but has no caller outside this class, so the new early
  return cannot hide a caller's expectation.
- An absent receiver with `enabled: yes` is the intended split. The console
  prints `enabled` for the setting and `receiver` for the hardware, and the GPS
  Data page shows `absent`.
- The scenario asserts the receiver before the sleep lock, so the red run stops
  at the receiver line and never shows the lock assertion failing. The same red
  log carries `'gps' acquired 'no_light_sleep' power lock, held 1 times.` before
  the first assertion and releases it only at teardown, so both halves of the
  defect are visible in one run.
- `sim/CLAUDE.md`'s GPS/UART row was rewritten by master for the shared EXT 5 V
  rail. The conflict was resolved to master's text plus the seed sentence, so the
  line-join churn in the original diff is gone.

## Validation

Host simulator builds with clang 21.0.0, `sim/build.sh`, for both certified
panels, StickS3 135x240 and M5Stack Core 320x240, from this tree at the rebased
head with the fixes applied.

`e2e/gps-uart-install-failure.txt`, exit 0 on both panels, every assertion
passing, one `Failed to install gps uart: ESP_FAIL (0xffffffff)` line per run and
no `gps` `no_light_sleep` acquire line at all.

The same scenario on the pre-fix source, with only `src/FurbleGPS.cpp` and
`include/FurbleGPS.h` taken from the base and the scenario and seed unchanged:
exit 1 with `ASSERT FAILED: gps.receiver expected 'absent' got 'present'`. The
pre-fix code discards the `esp_err_t` and logs nothing, so the fake's return code
cannot change this run. The same run logs `'gps' acquired 'no_light_sleep' power
lock, held 1 times.` before the assertion and releases it at teardown.

Neighbouring GPS scenarios on both panels, all exit 0: `gps-no-receiver`,
`gps-fixed-baud-absent`, `gps-disabled-idle`, `gps-uart-ack`,
`gps-autobaud-ladder`, `gps-duty-no-fix`, `gps-receiver-detail`,
`gps-ubx-detect`.

The full certified e2e suite on the StickS3 binary,
`sh sim/scripts/run-e2e.sh`: `All 247 end-to-end scenarios passed`. The only
behavioural change outside the failed-install path is the parser-buffer clear
moving above the null-queue guard in `reset()`, which is a no-op whenever a queue
exists.

`python3 tools/pre_review.py --base origin/master`: `PASS: 13 gates`, including
the scanner unit tests and the citation gate #511 added.

`python3 tools/check_sim_scenarios.py`: `sim scenario manifest is complete`.
`sh sim/scripts/check-doc-tokens.sh`: `sim documentation tokens are present`.
`python3 tools/check_doc_citations.py`: `0 errors`.

## Online gate and merge

`python3 tools/check_review_threads.py --repo MaxRink/furble --pr 509` must pass
with the head it is run against. The PR has no review threads, so the gate is
decided by the check runs.

At `0a0273f4` the only failing lanes were `build (m5stack-core-debug)`, which
reported `flash 1,740,960, exceeds flash_slot 1,740,800 by 160`, and the
aggregate `success`. That lane was over its slot on master since #501 for every
branch, and the 144 bytes this PR adds sat on top of it. #518 then moved the
Core OTA debug lane to `ESP_LOG_INFO` and landed 24,944 bytes of headroom, so the
same code measures
`PASS m5stack-core-debug: flash 1,715,856/1,740,800` at `563971d6`. All 37 checks
pass at that head, including `build (m5stack-core-debug)`, the aggregate
`success`, `sim-e2e` with the new scenario, both sanitizer jobs, the coverage
floor and the size report.

## Follow-up

- Serialise `GPS::getInstance()`'s install. A `static std::mutex` or
  `std::call_once` around the install and task creation closes finding 8 and the
  older duplicate-task race in one change. It needs its own size line, which is
  why it is not in this PR.
