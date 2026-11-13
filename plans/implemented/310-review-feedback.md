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
| #391 | `CompanionGatt::reidentify()` rotated the LE address only in RAM; a reboot restored the default and stranded a host that paired after the rotation. | Fixed in this PR. `CompanionIdentity` persists the 15-character `companion_bleid` key and applies it in `CompanionGatt::init()` and on every disabled->enabled transition in `CompanionGatt::enable()`, before advertising. Camera bonds are never deleted. | `device-identity`: rotate, reboot, apply, camera bond untouched, invalid identity rejected, wiped store falls back. |
| #391 | The Swift auth comment claimed a full second but the settle window is 0.3 s. | Fixed in this PR. Comment now says 0.3 s. | `FurbleBLEClient.swift` comment matches `DispatchQueue.main.asyncAfter(.now() + 0.3)`. |
| #391 | Verify the console host shim declares `reidentify()`. | Already fixed on master (`tests/host/console/FurbleCompanion.h`, since `3fe328bb`). | `console-commands` now also lists `reidentify` in the accepted subcommand contract. |
| #391 | Verify docs/wiki and README console/BLE debug sections. | Fixed in this PR. README lists `companion reidentify`; `docs/console-commands.md`, `docs/companion.md` and `docs/wiki/Console-Commands.md` describe durable identity, bond scope and the bench BLE status service. | Doc review plus `console-commands` command contract. |
| #391 | Plan 304 was said to claim BLE debug hardware validation was still pending while the PR claimed a successful run. | Already fixed/outdated on master. `plans/304-webui-stick-memory.md` now records the validated status-only service with no pending claim. | Current plan text. |
| #392 | `HTTPD_MAX_SOCKETS = 2` was said to leave no usable client slot after ESP-IDF's three reserved sockets. | Fixed after review correction: the original finding was false. `max_open_sockets` counts client sockets; two clients plus the three reserved internal sockets total five. The constant is back to 2, with corrected comments, docs and budget text. | `webui-handler` reads the configured client count; `boundary-webui-tls` derives its socket count and byte ledger from `WebUI::maxOpenSockets()`. |
| #405 | The temporary `HTTPD_MAX_SOCKETS = 5` wording described five total sockets instead of two clients. | Reverted in this PR. Header/source/docs/plans/CLAUDE now say two client sockets plus three ESP-IDF-reserved sockets. | Doc and comment audit; `boundary-webui-tls` uses the constant. |
| #392 | Verify `serverFits()` uses `SERVER_INTERNAL_LARGEST_FLOOR`. | Already fixed on master (`include/FurbleWebUIMemory.h`, since `610673fc`). | `webui-security` and `webui-handler` floor tests. |
| #392 | `CompanionGatt::reloadPassword()` was bypassed by the service's GATT password-write path, leaving WebUI `m_PasswordReady` true. | Fixed in this PR. `CompanionService::reloadPassword()` invokes a password-reload callback; `CompanionGatt` installs the WebUI reload there. | `companion-gatt` asserts the callback on failed rotation, successful rotation and failed clear; `webui-handler` asserts the password gate clears and restores. |
| #392 | Verify `WebUI::reloadSetting()` does not create the identity task while `web_ui` is off. | Already fixed on master (`src/FurbleWebUI.cpp`, since `610673fc`). | `webui-handler` now asserts a WebUI-off reload writes no identity NVS and keeps the gate semantics. |
| #392 | Add required `src/`/`include/`/`tests/host/` ownership notes for changed behavior. | Fixed in this PR. `src/CLAUDE.md`, `include/CLAUDE.md`, `lib/furble/CLAUDE.md` and `tests/host/CLAUDE.md` record the identity, password-callback, socket and virtual-clock contracts. | Doc review. |
| #393 | Simulator shim `sim/shim/mqtt_client.h` must have `task.stack_size`. | Already fixed on master (`4f7b954f`). | The simulator builds and MQTT scenarios compile against the shim. |
| #393 | Secure/plain worker coverage and a secure-specific start floor for the 6 KiB TLS worker. | Fixed in this PR. Added `START_TLS_INTERNAL_FREE_FLOOR`/`LARGEST_FLOOR` (11,264/7,168) and select them from the trimmed URI. | `mqtt-owner` secure `mqtts://` fixture asserts no start at the plain floor and the 6 KiB worker above it; `mqtt-fuzz` pins both gates and boundaries. |
| #393 | `wss://` branch must be reachable or removed. | Fixed in this PR by removing it. The validator accepts only `mqtt://` and `mqtts://`, and `MQTT::startClient()` no longer tests for WebSocket. The contract is documented as MQTT/TLS only. | `provision-apply-mqtt` rejects `wss://` and `ws://`; `test_mqtt_transport_contract` requires SSL+3.1.1 and no WebSocket transport in every MQTT sdkconfig. |
| #393 | `m_URISetting` race: use the owner-task copy `m_URI`. | Already fixed on master (`11157cad`). | `src/FurbleMQTT.cpp` copies the settings under `m_Mutex` in `loadTopics()`; the owner uses `m_URI`. |
| #393 | Plan/CLAUDE records for the worker and gate contract. | Fixed in this PR. Plans 308/309 and `include/CLAUDE.md` record plain/TLS floors and the 3 KiB/6 KiB workers. | Doc review. |
| #395 | `src/FurbleCrashLog.cpp` `s_Printing` was an unsynchronized recursion guard. | Fixed in this PR. It is `std::atomic<bool>` with compare-exchange in `printRecord()` and a relaxed load in `appendLine()`. | `crash-log`. |
| #395 | `CrashLog::init()` left a clean `FLAG_CLEAN` record that hid an early-boot crash. | Fixed in this PR. Every init consumes the old record and stores a fresh unclean current-boot snapshot. | `crash-log` simulates an early panic after a clean record and requires recovery. |
| #395 | `crash last extra` was accepted. | Fixed in this PR. `cmdCrash()` rejects `argc > 2`. | `console-commands` checks the rejection and usage text. |
| #395 | `sim/CrashLogSim.cpp` ring buffer was updated from multiple tasks without synchronization. | Fixed in this PR. The ring, sequence, current snapshot and persistence writes are guarded. | Crash simulator scenarios (`crash-record-survives-restart`, reset matrix) pass. |
| #395 | `sim/CrashLogSim.cpp` kept the clean sidecar and could suppress an early-boot crash. | Fixed in this PR. Init consumes the old record and persists a fresh unclean snapshot. | `crash-record-survives-restart` and the reset matrix pass. |
| #395 | `sim/webui_memory_model.cpp` kept stale failure flags after a later success. | Fixed in this PR. Successful admission clears blocked, fail-closed, reason and message. | `sim-webui-memory-model` blocks, recovers, and asserts clean state. |
| #395 | `sim/webui_memory_model.cpp` returned a `c_str()` pointer that outlived the mutex. | Fixed in this PR. `webuiGateReason()` and `webuiGateMessage()` return thread-local copies. | `sim-webui-memory-model` asserts non-null query copies after recovery. |
| #395 | Verify console `EXPECTED_COMMANDS` contains `crash`. | Already fixed on master (`4f7b954f`). | `console-commands` command-table test. |
| #397 | `BootScreen` marked `BOOT_SPLASH_SHOWN` before the disabled/invalid-panel returns. | Fixed in this PR. The mark is recorded only after the panel draw succeeds. | `boot-splash` asserts `boot.splash_shown yes`; `boot-splash-disabled` asserts `no`. |
| #397 | `FurbleBootTimeline` used `esp_log_timestamp()` where a cycle-counter timestamp is required. | Fixed in this PR. It reads `esp_cpu_get_cycle_count()` and converts with the ROM tick rate. | `boot-timeline` pins the conversion, including zero tick rate. |
| #397 | `WEBUI_EARLY_DONE` and `MQTT_INIT_DONE` marks were outside their feature guards. | Fixed in this PR. Both marks moved inside their `FURBLE_WEBUI`/`FURBLE_MQTT` blocks. | Release/debug firmware builds compile both profiles. |
| #397 | Resolve or prove the `mqtt-owner` host timing flake. | Fixed in this PR. The host `esp_timer_get_time()` no longer advances with wall time; only `host_mqtt_timer::advance()` moves it. | `mqtt-owner` sleeps 5 ms and requires an unchanged clock; 50/50 repeated runs pass. |
| #398 | The secure MQTT gate floor must cover the 6 KiB TLS worker. | Fixed in this PR. Secure URIs use 11,264 free / 7,168 largest. | `mqtt-owner` and `mqtt-fuzz` boundary tests. |
| #398 | Add a secure-URI `mqtts://` fixture asserting the 6 KiB worker. | Fixed in this PR. | `mqtt-owner` secure fixture. |
| #398 | Verify HA gate, cached HA and start-gate boundary coverage. | Verified in this PR, with TLS boundaries added. Existing tests cover both discovery floors and cached `MQTT_HA`; failures are per-target. | `mqtt-owner`, `mqtt-fuzz`. |
| #398 | README settings overview did not explain cached `mqtt_ha` or the fail-closed MQTT/discovery floors. | Fixed in this PR. The README MQTT paragraph now records the cache and both fail-closed gates. | Doc review against `docs/settings-and-controls.md`. |
| #398 | README WebUI figures contradicted the updated `docs/webui.md` budget table. | Fixed in this PR. README carried the 27.4/25.6 KiB measured pair, the then-current 34.6 KiB inference and the unchanged server floor. #407 later replaced that inference with 25,911 under the 12 KiB main stack. | Doc review against `docs/webui.md`, `README.md` at #407. |
| #398 | Check the 3 KiB companion and 8 KiB main stack against fuzz/host tests. | Fixed in this PR. The memory fuzzer asserts both stack budgets, and the simulator budget scenario pins them. The main stack later reverted to 12 KiB in #407 after the MQTT path overflowed the 8 KiB stack on hardware. | `memory_fuzz.py`, `hw-memory-post394-budget`, `plans/implemented/311-mqtt-queue-psram.md`. |
| #399 | `getBatteryCaps()` returned a mutable reference rewritten on every call. | Fixed in this PR. The API and simulator return `battery_caps_t` by value. | Sim and firmware builds; console battery paths compile against the value API. |
| #399 | Console reset constants must use the descriptive IDF names. | Fixed in this PR by using the pinned IDF 5.5.3 names directly (`ESP_RST_USB`, `ESP_RST_JTAG`, `ESP_RST_EFUSE`). The dead `#if defined` compatibility branch was removed; a future header change fails the target build loudly. | `m5stick-s3` and `m5stick-s3-debug` target builds. |
| #399 | Fuzz readiness accepted only a top-level pass. | Fixed in this PR. Every manifest target must have exactly one result entry with `status=pass`; unknown and duplicate ids fail. | `test_flash_readiness`. |
| #399 | Permutation results must be unique and complete. | Fixed in this PR. Results are compared in manifest order with board and combination equality. | `test_flash_readiness`. |
| #399 | Memory dumps must match the exact expected scenario set. | Fixed in this PR. Missing, unexpected and duplicate scenarios fail. | `test_flash_readiness`. |
| #399 | Coverage evidence must be an executable assert, not a token in a comment or log string. | Fixed in this PR. Scenario evidence is parsed from `assert`/`assert_min`/`assert_max`/`assert-eventually` only; `xassert` never counts. Shell and runner evidence must be a structured `assert_token "..."` marker or a grep that explicitly reads `$LOG`/`$log`/`$output`/a result file. All hardware rows now name executable assertions. | `test_sim_coverage`, `check_sim_coverage.py`, scenario updates. |
| #399 | `run-permutations.py` enabled the MQTT stack model for `mqtt=off` and seeded `wifi_connected` with a non-boolean value. | Fixed in this PR. MQTT seeds are conditional on `mqtt=on`; the Wi-Fi seed is `true`/`false` plus the reconnect action. | `test_run_permutations`, a 2-combination smoke run. |
| #399 | Full permutation results must match `sim/coverage/permutations.json`. | Fixed in this PR. The runner validates order, duplicates and the full count against the manifest. | `test_run_permutations` plus `check_flash_readiness` result validation. |
| #405 | The full permutation artifact was 9.5 MB because every result kept an `output_tail` and pretty-printed JSON. | Fixed in this PR. `--compact` keeps `id`, `board`, `combination`, `passed` and `exit`, omits `output_tail`, and writes compact JSON. The release command uses `--compact`. | `test_run_permutations` runs a one-combination compact artifact and checks the exact entry keys. |
| #399 | CONTRIBUTING did not document the hardware-coverage, memory-profile, full-permutation and flash-readiness workflow. | Fixed in this PR. Added the sim-first release-gate command block and the per-target evidence rule. | Doc review. |
| #399 | `docs/sim-memory-profiler.md` said the crash record is 434 bytes; the layout is 436. | Fixed in this PR. Document now says 436 bytes. | `sizeof(CrashRecord::record_t)` layout and the simulator scenario assertion. |
| #405 | `CompanionGatt::init()` applied the persisted identity only when the companion setting was on at boot, so a later `companion start`/settings enable advertised the default identity. | Fixed in this PR. `CompanionGatt::enable()` applies `CompanionIdentity::applyPersisted()` on every disabled->enabled transition before the pairing/reconnect advertising path. The init call remains a harmless double application. | `test_companion_identity_enable_contract` parses the production function order; `device-identity` covers persistence and restore. |
| #405 | `wss://` was accepted after the validator change, but no firmware sdkconfig enables the WebSocket transport; Waveshare also lacked the SSL transport for `mqtts://`. | Fixed in this PR by rejecting `wss://` and removing the dead secure-WebSocket branch. Enabled MQTT 3.1.1 and SSL in the Waveshare sdkconfig so the `mqtt://`/`mqtts://` contract holds on every MQTT build. | `provision-apply-mqtt` rejects `wss://`/`ws://`; `test_mqtt_transport_contract` pins SSL+3.1.1 and no WebSocket transport for all four MQTT sdkconfigs. |
| #405 | `CompanionIdentity` used the 18-character NVS key `companion_own_addr`; ESP-IDF truncates keys above 15 characters. | Fixed in this PR. Renamed to `companion_bleid` (15 characters), distinct from `companion_addr`. The host Preferences stub rejects long keys and enforces 15 characters. | `device-identity` checks every stored key length, the exact key name, no bond-key collision, and a rejected over-length write. |
| #405 | Boundary evidence for #404 was comment-only; the manifest `runner` was never executed by the readiness gate. | Fixed in this PR. `check_sim_coverage` accepts only an executable runner with structured `assert_token` markers, or executable scenario asserts/greps that read a result sink. `check_flash_readiness --ready` runs every declared runner and fails on nonzero. | `test_sim_coverage`, `test_flash_readiness`; readiness now executes `run-boundary-conformance.sh`. |
| #405 | Shell evidence could still be satisfied by `grep 'token' README.md` or an inline comment. | Fixed in this PR. Boundary assertions use `assert_token "token"` against `$LOG`; the checker accepts only structured `assert_token` markers or greps that explicitly read `$LOG`/`$log`/`$output`/a result file. README and comment matches are rejected. | `test_sim_coverage` wrong-file and inline-comment cases; every boundary token is structured. |
| #405 | `sim/CrashLogSim.cpp` shared `s_Injected` as a plain bool across driver and simulator task threads. | Fixed in this PR. The flag is `std::atomic<bool>`. | `test_crash_log_sim_contract` requires the atomic declaration; crash simulator scenarios still pass. |
| #405 | `src/FurbleConsole.cpp` used `#if defined(ESP_RST_*)` for enum constants, so the compatibility branch was dead. | Fixed in this PR. The reset names are the pinned IDF 5.5.3 `ESP_RST_USB`, `ESP_RST_JTAG`, `ESP_RST_EFUSE` directly; a future IDF that removes them fails the target build instead of silently keeping dead code. | Firmware builds for `m5stick-s3-debug`, `m5stick-s3` and `waveshare-s3-eth`. |
| #405 | The boundary runner's fail-closed behavior had no regression test. | Fixed in this PR. Added a fake-cmake/fake-ctest test where ctest prints every PASS token but exits 1; the runner must still fail. | `test_boundary_runner`: failing ctest with PASS tokens returns nonzero; successful ctest returns zero. |
| #405 | `boundary_companion_test` did not link `src/FurbleCompanionIdentity.cpp`, so the combined #404+#405 tree could not link. | Fixed in this PR. Added the source to `BOUNDARY_COMPANION_SOURCES`; PreferencesSim and the NimBLE mock already satisfy its dependencies. | `boundary-companion-gatt` builds and passes. |
| #405 | #404/#405 conflicts in MockNimBLE own-address/bond/server state, WebUI TLS ledger/`lastMaxOpenSockets`, and the host CMake target. | Resolved in this PR retaining both behaviors: one mock own-address state, the TLS allocation ledger plus `lastMaxOpenSockets`, and both boundary and review test targets. | Combined full host build and CTest. |
| #405 | `sim/webui_memory_model.cpp` kept `serverAllocFailed` after a successful retry. | Fixed in this PR. The success path clears `serverAllocFailed` along with the other stale flags. | `sim-webui-memory-model` forces one exact allocation failure, retries after the budget delay, and requires `running` with no stale fail-closed state. |
| #405 | `CompanionGatt::reidentify()` changed the own address while advertising/connected, then dropped the link and bond before persistence/apply could fail. | Fixed in this PR. It stops advertising first without touching the link/bond, then rotates; a failure restores the previous advertising state/window and keeps the link and bond. | `test_companion_identity_enable_contract` pins stop-before-rotate and rotate-before-disconnect/forget; boundary companion test exercises reidentify. |
| #405 | `CompanionIdentity::rotate()` persisted before applying, so an apply failure left NVS pointing at an address the controller never used. | Fixed in this PR. The order is generate -> apply -> persist. An apply failure leaves NVS unchanged and restores the previous/default controller identity. A persist failure re-applies the previous persisted/default address before reporting failure. | `device-identity` covers first-boot persist failure (default restore), apply failure with a previous record, and persist failure with a previous record. |
| #405 | `run-permutations.py` accepted unknown `--board` values and marked filtered subsets complete. | Fixed in this PR. Unknown boards return 2; `complete` is true only for a full, unfiltered manifest run. | `test_run_permutations` unknown-board and board-filtered cases. |
| #405 | `check_sim_coverage.py` accepted `xassert` as modeled evidence and any non-comment line containing `grep`. | Fixed in this PR. `xassert` is never evidence; shell evidence must be a structured `assert_token \"...\"` marker or a grep that explicitly reads the captured output/result file, and modeled runner entries must be executable. | `test_sim_coverage` xassert, comment/log, wrong-file grep, executable runner and non-executable runner cases. |
| #405 | Boundary NVS error values did not match IDF. | Fixed in this PR. The shim now uses `ESP_ERR_NVS_NO_FREE_PAGES=0x110a` and `ESP_ERR_NVS_NEW_VERSION_FOUND=0x110b`. | `boundary_support.cpp` static_asserts both values. |
| #405 | Headless test counters crossed threads without synchronization and `headless.no_panel touched=0` could never become nonzero. | Fixed in this PR. Counters are atomics and the boot vector is mutex-guarded; the display shim marks `used` on every panel access. | `boundary-headless-service` reports the actual probe and passes with `touched=0`. |
| #405 | Boundary log vector mutex from #404 might have been lost in the rebase. | Verified retained. `BoundaryHost::logContains()` still locks `detail::logStore().mutex`. | Boundary tests build and pass on the rebased tree. |
| #405 | End-to-end GATT password-write → WebUI gate coverage was missing. | Fixed in this PR. `boundary_companion_test` compiles production CompanionGatt/GATT with `FURBLE_WEBUI=1`, writes the password characteristic, and observes the WebUI hook without calling `reloadSetting()` directly. The WebUI TLS allocation ledger stays in its separate `boundary_webui_tls_test` target; merging the two targets is not required and remains a documented split-target limitation. | `companion.password_gate webui_reloads=1`; `boundary-companion-gatt` and `boundary-webui-tls` pass. |
| #402 | Add the required plan record for the host link fix. | Fixed in this PR. Recorded here and in plan 99: CrashLogSim is the single `furble_sim_crash_log_capture` definition; the duplicate stub is removed. | Host build links `sim_scheduler_test`; full CTest. |
| #402 | Full CTest exposed a `sim-scheduler` flake: the watchdog-style `try_lock` report can return "lock held" under parallel load, so one sample lacked `wait=mutex`. | Fixed in this PR. The test retries the report against a real deadline instead of treating one unavailable sample as a missing waiter. | `sim-scheduler` 50/50 repeated runs and a full parallel CTest. |
| Policy | No merge before every review finding is fixed or proven false/outdated with evidence; the remediation PR must pass the online thread check. | Fixed in this PR. Added `tools/check_review_threads.py`, its unit tests, and CONTRIBUTING/AGENTS policy text. Not wired into offline pre-review or fork CI. | `tests/test_check_review_threads.py`; run the tool against the PR before merge. |

## Documentation audit

The docs-only mapping for this remediation PR is in
[`plans/evidence/2026-11-05-docs-review-audit.md`](../evidence/2026-11-05-docs-review-audit.md).
It records where each #391, #392, #396, #397, #398, #399, #400, #401 and #402
documentation finding was fixed and verifies the claims against the merged tree.

## Verification commands

```sh
python3 tools/check_review_threads.py --repo MaxRink/furble --pr <number>
python3 tools/pre_review.py --base origin/master
ctest --test-dir host-build --output-on-failure
sh sim/scripts/run-boundary-conformance.sh
python3 sim/scripts/run-permutations.py --compact
python3 tools/check_flash_readiness.py --ready
FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e m5stick-s3-debug
FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e m5stick-s3
FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e waveshare-s3-eth
```

The flash-readiness report passes on master at `0883a31e`, not because of a
relaxed finding but because the evidence landed: #404 linked the boundary
paths, #405 closed these review findings, #406 recorded 6,912 of 6,912
permutations and #407 fixed the MQTT owner and queue path. The memory profile
is 13 of 13 after `python3 sim/scripts/run-memory-profile.py` generates the
ignored dumps in `.pio/sim-memory`. Hardware validation of #407 is deferred;
see [313-sim-only-status.md](../313-sim-only-status.md).
