# 304 - Review feedback for the #381 and #390 closeout

Implementation state (2026-11-14): every finding below is fixed on
`fix/381-390-review`. Hardware validation of both issues is still deferred
(plans 302, 304 and 313).

## Scope

This is the review-feedback record for the branch that closes the simulator
side of issue #381 (USB monitor recovery, plan 302) and issue #390 (WebUI
first boot on the StickS3, plan 304). The findings came from a subagent review
of commits `6d456d14` (#381) and `395bf7a1` (#390), rows 1 to 8, and an
independent second review of the fixes, rows 9 to 16. Row 17 is the
regression the master merge of #446 exposed. Each is recorded as fixed,
already fixed or a false positive, with evidence.

## Findings

| # | Source | Finding | Resolution | Evidence |
| ---: | :--- | :--- | :--- | :--- |
| 1 | Subagent review | #381: on macOS a hung-up tty polls as `POLLIN\|POLLHUP` and `read()` returns 0, so the hang-up guard, which required `POLLIN` clear, never fired. | Fixed. A ready poll followed by a 0-byte read throws `readFailed("port hung up")`. | `FurbleSerialConsole.swift`; `hungUpTTYThrowsInsteadOfReadingNothing` drives the real POSIX read path through a pseudo-terminal whose master closed. |
| 2 | Subagent review | #381: the hard reset failed when the S3 reset dropped the USB node before RTS was released, so no `hard-reset` marker was written. The esptool comment named the wrong branch and dwell. A bare pulse cannot clear `RTC_CNTL_FORCE_DOWNLOAD_BOOT`. | Fixed. The pulse counts once RTS is asserted; a release that fails with `ENXIO`, `EIO` or `ENODEV` returns `pulsedPortLost` and still emits `hard-reset`. The comment now names esptool's non-OTG `HardReset` branch, RTS high for 100 ms. Plan 302 records the force-download limit as a deviation and hardware step 4. | `FurbleSerialConsole.hardReset()`; `aPortLostOnReleaseStillCountsAsAReset`, `aResetThatDropsThePortStillEmitsHardReset`; `plans/302-companion-restart-reconnect.md` Deviations. |
| 3 | Subagent review | #381: the tests drove a miniature copy of the monitor loop, not the production `USBConsoleRunner.monitor` wiring. The banner parser lacked edge cases. A status reply without `uptime` produced a `device-reset` marker. | Fixed. The per-sample step moved to `CLIUSBMonitorSession` in CLIKit, which the CLI calls, so the tests drive the production marker order and port handling. New parser cases: CRLF, noise before `rst:`, a truncated reason, two banners in one buffer and the classic `DOWNLOAD_BOOT(` field. `rebooted()` returns false when either uptime is missing. | `CLIUSBRecovery.swift`, `USBConsoleRunner.swift`; `crlfBannerParses`, `noiseBeforeTheResetLineIsDropped`, `truncatedReasonIsUnknown`, `theLastOfTwoBannersWins`, `classicDownloadBootIsDownloadMode`, `uptimeRegressionMarksADeviceResetAndMissingUptimeDoesNot`. |
| 4 | Subagent review | #381: each S3 port open resets the chip, and the stall path reopens the port. Self-inflicted stalls could escalate to a reset or a `hard-reset-needed` marker. | Fixed as a recorded hardware item. Plan 302 adds the reopen interaction to Deviations and hardware step 6 with its pass criterion. A fix without a board would be a guess. | `plans/302-companion-restart-reconnect.md` Deviations and step 6. |
| 5 | Subagent review | #390: `hw-memory-webui-first-boot` never read `heap.minimum`. The model's connect minimum was 5,599 free with a 3,836-byte largest block, below RED and below the 4 KiB target stack. | Fixed. The abort risk is real on hardware: the dip is the connected page's first draw (LVGL layer and style blocks), between two governor readings, and real fragmentation is worse than the model. Every caller of `Control::addActive()` now calls `MemoryPressure::admit(CONNECT_TARGET_BYTES)`; when the 4,480-byte target task would take the heap into AMBER or RED, that level is entered at once. Round 2 (row 9) added the wake that makes the WebUI act on it: the server stops shortly after the target task is created, not before it. The scenario prints `heap.minimum` and `mem.high_water_largest` and asserts floors of 6,144 and 4,480. With admission the minimum is 7,475 free with a 5,712-byte largest block. A board that stays NORMAL after the target task sheds nothing. | `include/FurbleMemoryPressure.h`, `src/FurbleMemoryPressure.cpp`, `src/FurbleUI.cpp`, `src/main.cpp`; `tests/host/memory_pressure_test.cpp`; `sim/scenarios/e2e/hw-memory-webui-first-boot.txt`. |
| 6 | Subagent review | #390: the scenario seeded the post-#394 measurement, 27,351 / 25,588, not the current inferred StickS3 heap. | Fixed. The seed is 25,911 / 24,148, the inference plan 313 uses after the 12 KiB main stack. It is 1,440 bytes lower, so it is the harder case. Every heap figure in the scenario moved by that amount. | `sim/scenarios/e2e/hw-memory-webui-first-boot.txt`; `plans/304-webui-stick-memory.md` review follow-up. |
| 7 | Subagent review | #390: the identity-stored and flat-soak assertions cannot fail in the model. | Fixed in the docs. `webuiMemoryStart()` marks a seeded identity stored and the model allocates nothing per request, so both hold by construction. Plan 304 and the scenario header say so and point at hardware steps 2 and 5. | `plans/304-webui-stick-memory.md`; scenario header. |
| 8 | Subagent review | #390: HTTPS stayed paused after the disconnect because of a 4,936-byte internal residual. | Fixed. `lv_image_cache_drop(NULL)` at disconnect let HTTPS resume in the simulator, which confirmed the image cache as the main part (3,496 bytes). On PSRAM boards the UI routes LVGL's image draw buffers to PSRAM through the image handler's `buf_malloc_cb` and `buf_free_cb`. The DMA display buffers keep their own internal allocation. HTTPS now resumes after the disconnect at 12,695 / 10,932. 1,440 bytes stay allocated after the disconnect; row 12 identifies them as the cache's per-entry metadata. They no longer block the exit. | `src/FurbleUI.cpp` `imageBufferMalloc()`; scenario disconnect leg; `docs/webui.md`, `docs/wiki/WebUI.md`. |
| 9 | Subagent review, round 2 | `admit()` only stored the level. The WebUI supervisor sees it on its next `ulTaskNotifyTake` wake, up to 500 ms later, and `addActive()` creates the target task synchronously, so the server stopped after it. The docs claimed it stopped before the connect allocated. | Fixed. `MemoryPressure::setEscalateHook()` takes one hook; `admit()` calls it after releasing the governor mutex, only when the level rose. `WebUI::init()` registers `wakeForPressure()`, which gives the supervisor a task notification. MQTT keeps its own level read. The wording now says the server stops shortly after the target task is created, and that nothing orders that stop against the NimBLE client or the first connected-page draw. | `include/FurbleMemoryPressure.h`, `src/FurbleMemoryPressure.cpp`, `src/FurbleWebUI.cpp`; `tests/host/sim_webui_memory_model_test.cpp` (hook silent with headroom, fires on NORMAL to AMBER, silent on a repeat, fires on AMBER to RED); `docs/webui.md`, wiki, `src/CLAUDE.md`, scenario, plan 304. |
| 10 | Subagent review, round 2 | `imageBufferMalloc()` probed the PSRAM largest block, a full TLSF walk under the heap lock, on every image buffer, and the gap between the probe and the allocation could still abort. | Fixed. It calls `heap_caps_malloc_prefer(bytes, 2, MALLOC_CAP_SPIRAM \| MALLOC_CAP_8BIT, MALLOC_CAP_DEFAULT)`, which tries each caps set without aborting and aborts only when both fail. The probe and the `lv_malloc()` fallback are gone. The simulator gained a `heap_caps_malloc_prefer` shim that checks each pool through `furble_sim_heap_fits()` before charging it. | `src/FurbleUI.cpp`; `sim/shim/esp_heap_caps.h`, `sim/heap_model.cpp`; matrix row 4 reproduces 14,135 with the shim. |
| 11 | Subagent review, round 2 | The PSRAM image handlers also ran on the Core2, which has no persistent image cache, so it gained nothing for #390 and paid PSRAM reads on every icon draw. The change was not tied to plan 314's deferred PSRAM allocator. | Fixed. The handlers need `CONFIG_SPIRAM` and `LV_CACHE_DEF_SIZE` above 0, which excludes the Core2. Plan 304 records the scope (pixel buffers only, effectively those under the 4,096-byte malloc threshold) as a deviation from plan 314's deferred allocator, and adds hardware step 7, a `CONFIG_LV_USE_REFR_DEBUG` frame-time comparison against `a3ceef92`. | `src/FurbleUI.cpp`; `plans/304-webui-stick-memory.md`. |
| 12 | Subagent review, round 2 | The 1,440-byte residual was called "not the image cache". Only pixel buffers moved; the cache's per-entry metadata stays internal through `lv_malloc()`. Matrix row 2 had no free figure. | Fixed. With the PSRAM handlers on, `lv_image_cache_drop(NULL)` at disconnect returns 1,488 bytes (14,135 to 15,623 on the post-#394 seed; 12,695 to 14,183 on the inferred seed). The residual is that metadata, charged at 64-bit host sizes. Row 2 now reads 19,119 and a fifth row records the drop with both fixes. The scenario, plan 304 and `docs/webui.md` say so. | Experiment matrix; scenario disconnect comment. |
| 13 | Subagent review, round 2 | The `admit()` log printed only the reading before the allocation. | Fixed. It reads `memory pressure normal -> amber before a 4480-byte allocation: free=13895 largest=12132`. | `src/FurbleMemoryPressure.cpp`; `docs/webui.md`. |
| 14 | Subagent review, round 2 | The headless console `connect` admitted a target task for a camera that was already a target, which `addActive()` then ignores. | Fixed. `connectCamera()` skips `admit()` when the camera's address is already among `Control::getTargetCameras()`. The headless doubles gained `getTargetCameras()` and `Camera::getAddress()`. | `src/main.cpp`; `tests/host/headless/`. |
| 15 | Subagent review, round 2 | `CONNECT_TARGET_BYTES` repeated the 4,096 literal from the `xTaskCreate` in `FurbleControl.cpp`. | Fixed. `TaskMemory::CAMERA_TARGET_STACK_BYTES` is the one value: `addActive()` passes it to `xTaskCreate`, the governor derives `CONNECT_TARGET_BYTES` from it, and `tools/stack_budget.json` reads it from `include/FurbleTaskMemory.h`. | `include/FurbleTaskMemory.h`, `src/FurbleControl.cpp`, `tools/stack_budget.json`. |
| 16 | Subagent review, round 2 | The changes move allocations on PSRAM boards, but the memory scenarios, panic fixtures and memory profile were not rerun. | Fixed. All three ran on plain builds; see Validation. The full permutation run is left to CI and the readiness gate. | Validation below. |
| 17 | Master merge, #446 | #446 raised the WebUI supervisor stack from 3,584 to 4,096 bytes. `hw-memory-webui-first-boot` then stopped at 20,375 free with the server paused after the disconnect, under the 20,480 AMBER exit, so the governor stayed AMBER and HTTPS never returned after a camera session. | Fixed without touching the exit or the scenario's checks. The 1,440 to 1,488 bytes the session left behind were all image cache metadata (row 12): 11 entries at the disconnect, their pixels already in PSRAM. `UI::doDisconnect()` and the connect timer's idle teardown now end with `dropSessionImages()`, `lv_image_cache_drop(NULL)` on boards with a persistent cache. With it the pool reaches 21,863 with the server paused, 1,383 over the exit, and HTTPS starts again at 13,671 / 11,908, 48 above the boot figure, so the model keeps nothing from the session. `lv_cache_drop_all` frees even a referenced entry, so the drop relies on `LV_OS_NONE` with one draw unit: entries are referenced only inside a refresh, and both call sites run on the UI task outside one. The Main page decodes its icons once again; the scenario bounds `ui.invalidate_count` at 3 over 10 s after the disconnect. The boot and connect figures follow #446 by 512 bytes (13,623 / 11,860; minimum 6,963 with a 5,200-byte block). The scenario now also asserts AMBER, a paused server and 21,863 free 1 s after the disconnect, before it asserts NORMAL and a running server. | `src/FurbleUI.cpp` `dropSessionImages()`; `sim/scenarios/e2e/hw-memory-webui-first-boot.txt`; plan 304 review follow-up; `docs/webui.md`; `src/CLAUDE.md`. |
| 18 | CI on merge 105d2dd8 (#454, #458), then master merges of #457 and #460 | `sim-e2e` and `sim-sanitizers` failed `hw-memory-webui-first-boot`: `heap.free expected '21863' got '23079'` 1 s after the disconnect. | Fixed, three causes measured on the plain StickS3 build. (1) +1,240: the 60 s soak sleeps the panel at the default Screen off, so the whole camera session ran with the panel off. #454 disables invalidation while the panel sleeps, so the connected page and the Main page after the drop no longer drew and the cache the Main page redraw refills stayed empty. A `btn a` wake before the connect, as a user's press does, restores every figure exactly (16,595 / 13,840 connected, 6,963 / 5,200 minimum, 21,863 after the disconnect, 13,671 at the end). (2) -20: #460 made the NimBLE mock record every `setSecurityIOCap()` call in a vector, charged to the device heap and grown by two entries per connect. The record now runs in the mock's existing host scope. #457 and #458 moved nothing in this scenario. (3) -24 on Linux only: the exact figures were measured on macOS and never ran in CI. The NimBLE mock client the connect keeps is 216 bytes with libc++ and 240 with libstdc++, so Linux reads 21,839 and 13,647, measured in an Ubuntu 24.04 container with the CI's pinned M5GFX, M5Unified and LVGL. CI's 23,079 is (1) minus (3). After the connect the scenario accepts the range between the Linux and macOS figures, so a macOS regression under 24 bytes is no longer caught there; the boot figures, the 6,144 / 4,480 floors, the AMBER and paused-server check, the NORMAL and running-server return and the redraw bound are unchanged. | `sim/scenarios/e2e/hw-memory-webui-first-boot.txt`; `lib/testing/nimble/MockNimBLE.cpp`; `sim/CLAUDE.md`; plan 304. |

## Copilot review

| # | Source | Finding | Resolution | Evidence |
| ---: | :--- | :--- | :--- | :--- |
| C1 | Copilot, `include/FurbleMemoryPressure.h:109` | `CONNECT_TARGET_BYTES` covered only the target stack and TCB, but `addActive()` first heap-allocates the `Target` and its 8-entry command queue. | Fixed. `addActive()` admits `sizeof(Target)`, the queue items and one `MQTTMemory::BLOCK_OVERHEAD_BYTES` per block (which covers the queue's 84-byte control block) on top of `CONNECT_TARGET_BYTES`, before it allocates any of them. | `src/FurbleControl.cpp`; `control-e2e-admit-hook-once-per-target` checks the admitted bytes cover the `Target` as well as the task. |
| C2 | Copilot, `include/FurbleMemoryPressure.h:200` | `admit()` released its mutex before `addActive()` took `Control::m_Mutex`, so the console and the UI could both admit on one heap reading before either target existed. | Fixed. Admission moved into `Control::addActive()`, under `m_Mutex`, through a hook boot code registers (`Control::setAdmitHook(&MemoryPressure::admit)` in `app_main()` and the simulator `main()`). Two connects are now admitted one after the other, each against a reading that includes the target before it. The callers in `UI::doConnect()`, `connectCamera()` and `connectSavedCamera()` no longer admit. The host Control harnesses register no hook, so they still need no governor. | `include/FurbleControl.h`, `src/FurbleControl.cpp`, `src/main.cpp`, `src/FurbleUI.cpp`, `sim/main.cpp`; `control-e2e-admit-hook-once-per-target` adds one camera from two threads and gets one target and one admission. |
| C3 | Copilot, `src/FurbleUI.cpp:8614` | `doConnect()` admitted before the duplicate check, so a repeated `Request::CONNECT` could shed the WebUI with no new target. | Fixed by C2: admission runs after `addActive()`'s own address-based duplicate check, so a duplicate admits nothing on every path. This supersedes finding 14, whose caller-side check in `connectCamera()` and the headless `getTargetCameras()` and `Camera::getAddress()` doubles are removed. | `control-e2e-admit-hook-once-per-target`: a repeated `addActive()` leaves the admission count unchanged. |
| C4 | Copilot, `include/FurbleMemoryPressure.h:210` | The escalate hook changed a synchronization contract without an `include/CLAUDE.md` entry. | Fixed. `include/CLAUDE.md` records both hooks: registered once at boot, never cleared on device, acquire/release pointers, and the lock order `Control::m_Mutex`, then the governor mutex (released), then the hook's own locks. A hook must not block or call Control; `WebUI::m_Mutex` qualifies because no WebUI path calls Control while holding it (every `lock_guard` scope in `FurbleWebUI.cpp` was checked). The header comments say the same. | `include/CLAUDE.md`, `include/FurbleMemoryPressure.h`, `include/FurbleControl.h`, `src/CLAUDE.md`. |

## Experiment matrix (finding 5 and 8)

Plain StickS3 simulator, the scenario's connect and disconnect legs on the
post-#394 seed, before the seed change. Temporary environment switches, since
removed, turned each fix off. The last row ran in round 2, with
`heap_caps_malloc_prefer()`; the row above it gives the same figures with the
round 2 code. Dropping the cache with every pixel buffer already in PSRAM
still returns 1,488 bytes: the cache's internal metadata.

| Admission | Icons in PSRAM | Drop cache at disconnect | Connect minimum | After disconnect |
| :--- | :--- | :--- | ---: | :--- |
| no | no | no | 5,599 | 18,831 free, AMBER, HTTPS paused |
| no | no | yes | 5,599 | NORMAL, HTTPS running at 19,119 |
| no | yes | no | 5,599 | NORMAL, HTTPS running at 14,135 |
| yes | yes | no | 8,915 | NORMAL, HTTPS running at 14,135 |
| yes | yes | yes | 8,915 | NORMAL, HTTPS running at 15,623 |

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
- Round 2, after rows 9 to 16, on plain builds:
  - `sh sim/scripts/run-e2e.sh` on the StickS3 binary passes 211 of 211.
  - `sh sim/scripts/run-memory.sh` on the `FURBLE_SIM_MQTT=1` binary passes
    4 of 4.
  - `sh sim/scripts/run-panic-fixtures.sh` with both binaries passes the
    allocation abort gate.
  - `python3 sim/scripts/run-memory-profile.py` passes the budget for 13
    dumps.
  - `m5stick-s3` and `m5stack-core2` rebuild. `check_size_budget.py` passes
    both (s3 flash 1,848,064 bytes, dram 47,216 of 48,128; core2 dram 56,504
    of 57,344). `stack_budget.py --env m5stick-s3` passes 15 of 15 tasks,
    with `camera_target` read from the new constant.
  - `run_sanitized_host_tests.sh --clean` passes 183 of 183, including
    `sim-webui-memory-model` with the hook cases.
  - The full `run-permutations.py` run is left to CI and the readiness gate.
- Row 17, after the master merge, on plain builds:
  - `sh sim/scripts/run-e2e.sh` on the StickS3 binary passes 212 of 212.
  - The ten power-gate scenarios stay within `compare.py`'s threshold. None
    of them disconnects a camera, so none runs the drop.
  - `sh sim/scripts/run-memory.sh` on the `FURBLE_SIM_MQTT=1` binary passes
    4 of 4.
  - `run_sanitized_host_tests.sh --clean` passes 184 of 184.
  - `m5stick-s3` builds. `check_size_budget.py` passes (flash 1,850,288,
    dram 47,232 of 48,128) and `stack_budget.py --env m5stick-s3` passes 16
    of 16 tasks.
  - `pre_review.py --base origin/master` passes 9 gates and
    `check-doc-tokens.sh` passes.
- No hardware run. The hardware steps in plans 302 and 304 stay open.
