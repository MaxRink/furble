# 310 - Review feedback audit and remediation gate

## Goal

Close every unresolved Copilot review finding from PRs #391, #392, #393
(superseded by #398), #395, #397, #398, #399 and #402 against the current
master tree. A finding is closed only when the code is fixed with a regression
test, already fixed on master with the commit recorded, or proven false or
outdated with evidence. Resolving a review thread without one of those is not
closure.

## Merge policy

No pull request merges while any review thread is unresolved. Every finding
must be fixed in the branch, or proven false or outdated with evidence in the
thread and in this table. The remediation pull request itself must pass the
online check before merge:

```sh
python3 tools/check_review_threads.py --repo owner/name --pr <number>
```

That tool uses the GitHub GraphQL API through `gh api graphql` and fails closed
on API errors. It is deliberately not wired into the offline pre-review or fork
CI jobs. `CONTRIBUTING.md` and `AGENTS.md` carry the policy text; this plan
carries the audit.

## Findings

| PR | Finding | Status | Test / evidence |
| --- | --- | --- | --- |
| #391 | `CompanionGatt::reidentify()` rotated the LE address only in RAM; a reboot restored the default and stranded a host that paired after the rotation. | Fixed in this PR. `CompanionIdentity` persists `companion_own_addr` and applies it in `CompanionGatt::init()` before advertising. Camera bonds are never deleted. | `device-identity`: rotate, reboot, apply, camera bond untouched, invalid identity rejected, wiped store falls back. |
| #391 | The Swift auth comment claimed a full second but the settle window is 0.3 s. | Fixed in this PR. Comment now says 0.3 s. | `FurbleBLEClient.swift` comment matches `DispatchQueue.main.asyncAfter(.now() + 0.3)`. |
| #391 | Verify the console host shim declares `reidentify()`. | Already fixed on master (`tests/host/console/FurbleCompanion.h`, since `3fe328bb`). | `console-commands` now also lists `reidentify` in the accepted subcommand contract. |
| #391 | Verify docs/wiki and README console/BLE debug sections. | Fixed in this PR. README lists `companion reidentify`; `docs/console-commands.md`, `docs/companion.md` and `docs/wiki/Console-Commands.md` describe durable identity, bond scope and the bench BLE status service. | Doc review plus `console-commands` command contract. |
| #391 | Plan 304 was said to claim BLE debug hardware validation was still pending while the PR claimed a successful run. | Already fixed/outdated on master. `plans/304-webui-stick-memory.md` now records the validated status-only service with no pending claim. | Current plan text. |
| #392 | `HTTPD_MAX_SOCKETS = 2` left no usable client slot after ESP-IDF's three reserved sockets. | Fixed in this PR. Set to 5 and updated the budget comments and docs (five `sock_db` slots, 960 bytes). | `webui-handler` reads the configured `max_open_sockets`; `webui-security` keeps the gate boundaries. |
| #392 | Verify `serverFits()` uses `SERVER_INTERNAL_LARGEST_FLOOR`. | Already fixed on master (`include/FurbleWebUIMemory.h`, since `610673fc`). | `webui-security` and `webui-handler` floor tests. |
| #392 | `CompanionGatt::reloadPassword()` was bypassed by the service's GATT password-write path, leaving WebUI `m_PasswordReady` true. | Fixed in this PR. `CompanionService::reloadPassword()` invokes a password-reload callback; `CompanionGatt` installs the WebUI reload there. | `companion-gatt` asserts the callback on failed rotation, successful rotation and failed clear; `webui-handler` asserts the password gate clears and restores. |
| #392 | Verify `WebUI::reloadSetting()` does not create the identity task while `web_ui` is off. | Already fixed on master (`src/FurbleWebUI.cpp`, since `610673fc`). | `webui-handler` now asserts a WebUI-off reload writes no identity NVS and keeps the gate semantics. |
| #392 | Add required `src/`/`include/`/`tests/host/` ownership notes for changed behavior. | Fixed in this PR. `src/CLAUDE.md`, `include/CLAUDE.md`, `lib/furble/CLAUDE.md` and `tests/host/CLAUDE.md` record the identity, password-callback, socket and virtual-clock contracts. | Doc review. |
| #393 | Simulator shim `sim/shim/mqtt_client.h` must have `task.stack_size`. | Already fixed on master (`4f7b954f`). | The simulator builds and MQTT scenarios compile against the shim. |
| #393 | Secure/plain worker coverage and a secure-specific start floor for the 6 KiB TLS worker. | Fixed in this PR. Added `START_TLS_INTERNAL_FREE_FLOOR`/`LARGEST_FLOOR` (11,264/7,168) and select them from the trimmed URI. | `mqtt-owner` secure `mqtts://` fixture asserts no start at the plain floor and the 6 KiB worker above it; `mqtt-fuzz` pins both gates and boundaries. |
| #393 | `wss://` branch must be reachable or removed. | Fixed in this PR. `validMQTTString()` accepts secure WebSocket URIs; `ws://` remains rejected. | `provision-apply-mqtt` accepts `wss://` and rejects `ws://`. |
| #393 | `m_URISetting` race: use the owner-task copy `m_URI`. | Already fixed on master (`11157cad`). | `src/FurbleMQTT.cpp` copies the settings under `m_Mutex` in `loadTopics()`; the owner uses `m_URI`. |
| #393 | Plan/CLAUDE records for the worker and gate contract. | Fixed in this PR. Plans 308/309 and `include/CLAUDE.md` record plain/TLS floors and the 3 KiB/6 KiB workers. | Doc review. |
| #395 | `src/FurbleCrashLog.cpp` `s_Printing` was an unsynchronized recursion guard. | Fixed in this PR. It is `std::atomic<bool>` with compare-exchange in `printRecord()` and a relaxed load in `appendLine()`. | `crash-log`. |
| #395 | `CrashLog::init()` left a clean `FLAG_CLEAN` record that hid an early-boot crash. | Fixed in this PR. Every init consumes the old record and stores a fresh unclean current-boot snapshot. | `crash-log` simulates an early panic after a clean record and requires recovery. |
| #395 | `crash last extra` was accepted. | Fixed in this PR. `cmdCrash()` rejects `argc > 2`. | `console-commands` checks the rejection and usage text. |
| #395 | `sim/CrashLogSim.cpp` ring buffer had no cross-task synchronization and the clean sidecar hid early crashes. | Fixed in this PR. Ring, sequence, current snapshot and persistence are guarded; init writes a fresh unclean snapshot. | Crash simulator scenarios (`crash-record-survives-restart`, reset matrix) pass. |
| #395 | `sim/webui_memory_model.cpp` kept stale failure flags and returned `c_str()` past the mutex. | Fixed in this PR. Successful admission clears the blocked/fail-closed/reason/message state; query strings are thread-local copies. | `sim-webui-memory-model` blocks, recovers, and asserts clean state and non-null copies. |
| #395 | Verify console `EXPECTED_COMMANDS` contains `crash`. | Already fixed on master (`4f7b954f`). | `console-commands` command-table test. |
| #397 | `BootScreen` marked `BOOT_SPLASH_SHOWN` before the disabled/invalid-panel returns. | Fixed in this PR. The mark is recorded only after the panel draw succeeds. | `boot-splash` asserts `boot.splash_shown yes`; `boot-splash-disabled` asserts `no`. |
| #397 | `FurbleBootTimeline` used `esp_log_timestamp()` where a cycle-counter timestamp is required. | Fixed in this PR. It reads `esp_cpu_get_cycle_count()` and converts with the ROM tick rate. | `boot-timeline` pins the conversion, including zero tick rate. |
| #397 | `WEBUI_EARLY_DONE` and `MQTT_INIT_DONE` marks were outside their feature guards. | Fixed in this PR. Both marks moved inside their `FURBLE_WEBUI`/`FURBLE_MQTT` blocks. | Release/debug firmware builds compile both profiles. |
| #397 | Resolve or prove the `mqtt-owner` host timing flake. | Fixed in this PR. The host `esp_timer_get_time()` no longer advances with wall time; only `host_mqtt_timer::advance()` moves it. | `mqtt-owner` sleeps 5 ms and requires an unchanged clock; 50/50 repeated runs pass. |
| #398 | The secure MQTT gate floor must cover the 6 KiB TLS worker. | Fixed in this PR. Secure URIs use 11,264 free / 7,168 largest. | `mqtt-owner` and `mqtt-fuzz` boundary tests. |
| #398 | Add a secure-URI `mqtts://` fixture asserting the 6 KiB worker. | Fixed in this PR. | `mqtt-owner` secure fixture. |
| #398 | Verify HA gate, cached HA and start-gate boundary coverage. | Verified in this PR, with TLS boundaries added. Existing tests cover both discovery floors and cached `MQTT_HA`; failures are per-target. | `mqtt-owner`, `mqtt-fuzz`. |
| #398 | README settings overview did not explain cached `mqtt_ha` or the fail-closed MQTT/discovery floors. | Fixed in this PR. The README MQTT paragraph now records the cache and both fail-closed gates. | Doc review against `docs/settings-and-controls.md`. |
| #398 | README WebUI figures contradicted the updated `docs/webui.md` budget table. | Fixed in this PR. README now carries the 27.4/25.6 KiB measured pair, the 34.6 KiB inference and the unchanged server floor. | Doc review against `docs/webui.md`. |
| #398 | Check the 3 KiB companion and 8 KiB main stack against fuzz/host tests. | Fixed in this PR. The memory fuzzer now asserts both stack budgets, and the simulator budget scenario already pins them. | `memory_fuzz.py`, `hw-memory-post394-budget`. |
| #399 | `getBatteryCaps()` returned a mutable reference rewritten on every call. | Fixed in this PR. The API and simulator return `battery_caps_t` by value. | Sim and firmware builds; console battery paths compile against the value API. |
| #399 | Console reset constants must use the descriptive IDF names. | Fixed in this PR with compile-time aliases: the pinned IDF 5.5.3 only defines `ESP_RST_USB`, `ESP_RST_JTAG` and `ESP_RST_EFUSE`, so the code prefers `ESP_RST_USB_UART_CHIP_RESET`, `ESP_RST_USB_JTAG_CHIP_RESET` and `ESP_RST_EFUSE_CRC` when the headers provide them. | `m5stick-s3` and `m5stick-s3-debug` target builds. |
| #399 | Fuzz readiness accepted only a top-level pass. | Fixed in this PR. Every manifest target must have exactly one result entry with `status=pass`; unknown and duplicate ids fail. | `test_flash_readiness`. |
| #399 | Permutation results must be unique and complete. | Fixed in this PR. Results are compared in manifest order with board and combination equality. | `test_flash_readiness`. |
| #399 | Memory dumps must match the exact expected scenario set. | Fixed in this PR. Missing, unexpected and duplicate scenarios fail. | `test_flash_readiness`. |
| #399 | Coverage evidence must be an executable assert, not a token in a comment or log string. | Fixed in this PR. Scenario evidence is parsed from `assert*`/`xassert` lines only; shell runners require a non-comment `grep`. All hardware rows now name executable assertions. | `test_sim_coverage`, `check_sim_coverage.py`, scenario updates. |
| #399 | `run-permutations.py` enabled the MQTT stack model for `mqtt=off` and seeded `wifi_connected` with a non-boolean value. | Fixed in this PR. MQTT seeds are conditional on `mqtt=on`; the Wi-Fi seed is `true`/`false` plus the reconnect action. | `test_run_permutations`, a 2-combination smoke run. |
| #399 | Full permutation results must match `sim/coverage/permutations.json`. | Fixed in this PR. The runner validates order, duplicates and the full count against the manifest. | `test_run_permutations` plus `check_flash_readiness` result validation. |
| #399 | CONTRIBUTING did not document the hardware-coverage, memory-profile, full-permutation and flash-readiness workflow. | Fixed in this PR. Added the sim-first release-gate command block and the per-target evidence rule. | Doc review. |
| #399 | `docs/sim-memory-profiler.md` said the crash record is 434 bytes; the layout is 436. | Fixed in this PR. Document now says 436 bytes. | `sizeof(CrashRecord::record_t)` layout and the simulator scenario assertion. |
| #402 | Add the required plan record for the host link fix. | Fixed in this PR. Recorded here and in plan 99: CrashLogSim is the single `furble_sim_crash_log_capture` definition; the duplicate stub is removed. | Host build links `sim_scheduler_test`; full CTest. |
| Policy | No merge before every review finding is fixed or proven false/outdated with evidence; the remediation PR must pass the online thread check. | Fixed in this PR. Added `tools/check_review_threads.py`, its unit tests, and CONTRIBUTING/AGENTS policy text. Not wired into offline pre-review or fork CI. | `tests/test_check_review_threads.py`; run the tool against the PR before merge. |

## Verification commands

```sh
python3 tools/check_review_threads.py --repo MaxRink/furble --pr <number>
python3 tools/pre_review.py --base origin/master
ctest --test-dir host-build --output-on-failure
python3 tools/check_flash_readiness.py --ready
FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e m5stick-s3-debug
FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e m5stick-s3
```

The flash-readiness report remains blocked only by the parallel permutation
results and boundary work, not by these review findings. The full permutation
run is a long release gate; this PR proves the generator and result validation
without claiming a completed 6,912-combination run.
