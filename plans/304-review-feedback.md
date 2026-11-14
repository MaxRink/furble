# 304 - Review feedback for the #381 and #390 closeout

Implementation state (2026-11-14): every finding below is fixed on
`fix/381-390-review`. Hardware validation of both issues is still deferred
(plans 302, 304 and 313).

## Scope

This is the review-feedback record for the branch that closes the simulator
side of issue #381 (USB monitor recovery, plan 302) and issue #390 (WebUI
first boot on the StickS3, plan 304). The findings came from a subagent review
of commits `6d456d14` (#381) and `395bf7a1` (#390). Each is recorded as fixed,
already fixed or a false positive, with evidence.

## Findings

| # | Source | Finding | Resolution | Evidence |
| ---: | :--- | :--- | :--- | :--- |
| 1 | Subagent review | #381: on macOS a hung-up tty polls as `POLLIN\|POLLHUP` and `read()` returns 0, so the hang-up guard, which required `POLLIN` clear, never fired. | Fixed. A ready poll followed by a 0-byte read throws `readFailed("port hung up")`. | `FurbleSerialConsole.swift`; `hungUpTTYThrowsInsteadOfReadingNothing` drives the real POSIX read path through a pseudo-terminal whose master closed. |
| 2 | Subagent review | #381: the hard reset failed when the S3 reset dropped the USB node before RTS was released, so no `hard-reset` marker was written. The esptool comment named the wrong branch and dwell. A bare pulse cannot clear `RTC_CNTL_FORCE_DOWNLOAD_BOOT`. | Fixed. The pulse counts once RTS is asserted; a release that fails with `ENXIO`, `EIO` or `ENODEV` returns `pulsedPortLost` and still emits `hard-reset`. The comment now names esptool's non-OTG `HardReset` branch, RTS high for 100 ms. Plan 302 records the force-download limit as a deviation and hardware step 4. | `FurbleSerialConsole.hardReset()`; `aPortLostOnReleaseStillCountsAsAReset`, `aResetThatDropsThePortStillEmitsHardReset`; `plans/302-companion-restart-reconnect.md` Deviations. |
| 3 | Subagent review | #381: the tests drove a miniature copy of the monitor loop, not the production `USBConsoleRunner.monitor` wiring. The banner parser lacked edge cases. A status reply without `uptime` produced a `device-reset` marker. | Fixed. The per-sample step moved to `CLIUSBMonitorSession` in CLIKit, which the CLI calls, so the tests drive the production marker order and port handling. New parser cases: CRLF, noise before `rst:`, a truncated reason, two banners in one buffer and the classic `DOWNLOAD_BOOT(` field. `rebooted()` returns false when either uptime is missing. | `CLIUSBRecovery.swift`, `USBConsoleRunner.swift`; `crlfBannerParses`, `noiseBeforeTheResetLineIsDropped`, `truncatedReasonIsUnknown`, `theLastOfTwoBannersWins`, `classicDownloadBootIsDownloadMode`, `uptimeRegressionMarksADeviceResetAndMissingUptimeDoesNot`. |
| 4 | Subagent review | #381: each S3 port open resets the chip, and the stall path reopens the port. Self-inflicted stalls could escalate to a reset or a `hard-reset-needed` marker. | Fixed as a recorded hardware item. Plan 302 adds the reopen interaction to Deviations and hardware step 6 with its pass criterion. A fix without a board would be a guess. | `plans/302-companion-restart-reconnect.md` Deviations and step 6. |
| 5 | Subagent review | #390: `hw-memory-webui-first-boot` never read `heap.minimum`. The model's connect minimum was 5,599 free with a 3,836-byte largest block, below RED and below the 4 KiB target stack. | Fixed. The abort risk is real on hardware: the dip is the connected page's first draw (LVGL layer and style blocks), between two governor readings, and real fragmentation is worse than the model. Every caller of `Control::addActive()` now calls `MemoryPressure::admit(CONNECT_TARGET_BYTES)`; when the 4,480-byte target task would take the heap into AMBER or RED, that level is entered at once and the WebUI server is shed before the connect allocates. The scenario prints `heap.minimum` and `mem.high_water_largest` and asserts floors of 6,144 and 4,480. With admission the minimum is 7,475 free with a 5,712-byte largest block. A board that stays NORMAL after the target task sheds nothing. | `include/FurbleMemoryPressure.h`, `src/FurbleMemoryPressure.cpp`, `src/FurbleUI.cpp`, `src/main.cpp`; `tests/host/memory_pressure_test.cpp`; `sim/scenarios/e2e/hw-memory-webui-first-boot.txt`. |
| 6 | Subagent review | #390: the scenario seeded the post-#394 measurement, 27,351 / 25,588, not the current inferred StickS3 heap. | Fixed. The seed is 25,911 / 24,148, the inference plan 313 uses after the 12 KiB main stack. It is 1,440 bytes lower, so it is the harder case. Every heap figure in the scenario moved by that amount. | `sim/scenarios/e2e/hw-memory-webui-first-boot.txt`; `plans/304-webui-stick-memory.md` review follow-up. |
| 7 | Subagent review | #390: the identity-stored and flat-soak assertions cannot fail in the model. | Fixed in the docs. `webuiMemoryStart()` marks a seeded identity stored and the model allocates nothing per request, so both hold by construction. Plan 304 and the scenario header say so and point at hardware steps 2 and 5. | `plans/304-webui-stick-memory.md`; scenario header. |
| 8 | Subagent review | #390: HTTPS stayed paused after the disconnect because of a 4,936-byte internal residual. | Fixed. `lv_image_cache_drop(NULL)` at disconnect let HTTPS resume in the simulator, which confirmed the image cache as the main part (3,496 bytes). On PSRAM boards the UI routes LVGL's image draw buffers to PSRAM through the image handler's `buf_malloc_cb` and `buf_free_cb`. The DMA display buffers keep their own internal allocation. HTTPS now resumes after the disconnect at 12,695 / 10,932. 1,440 bytes stay allocated with every image buffer in PSRAM; the model does not attribute them, and they no longer block the exit. | `src/FurbleUI.cpp` `imageBufferMalloc()`; scenario disconnect leg; `docs/webui.md`, `docs/wiki/WebUI.md`. |

## Experiment matrix (finding 5 and 8)

Plain StickS3 simulator, the scenario's connect and disconnect legs on the
post-#394 seed, before the seed change. Temporary environment switches, since
removed, turned each fix off.

| Admission | Icons in PSRAM | Drop cache at disconnect | Connect minimum | After disconnect |
| :--- | :--- | :--- | ---: | :--- |
| no | no | no | 5,599 | 18,831 free, AMBER, HTTPS paused |
| no | no | yes | 5,599 | NORMAL, HTTPS running |
| no | yes | no | 5,599 | NORMAL, HTTPS running at 14,135 |
| yes | yes | no | 8,915 | NORMAL, HTTPS running at 14,135 |

On the new seed with both fixes the minimum is 7,475, 1,440 lower again.

## Validation

- `swift test` in `companion/apple` passes 59 tests in 9 suites, covering
  findings 1 to 3.
- `memory_pressure_test` passes, covering the admission policy of finding 5.
- `sh sim/scripts/run-e2e.sh` on the plain StickS3 binary passes 211 of 211.
  `hw-memory-webui-first-boot` is deterministic across repeated runs.
- `FURBLE_VERSION=dev FURBLE_TEST=0 pio run` succeeds for `m5stick-s3` and
  `m5stack-core2`. `tools/check_size_budget.py` passes both;
  `tools/stack_budget.py --env m5stick-s3` passes every task.
- `sh tools/run_sanitized_host_tests.sh --clean` passes 183 of 183. The
  headless service double gained the `admit()` stub the first run was missing.
- `python3 tools/pre_review.py --base origin/master` passes 8 gates and
  `sh sim/scripts/check-doc-tokens.sh` passes.
- No hardware run. The hardware steps in plans 302 and 304 stay open.
