# furble improvement plans

Roadmap for power optimization and new features. Each numbered document is one
planned pull request against upstream gkoh/furble. Each PR is independently
mergeable. All new behavior is configurable. Defaults keep current behavior.

Primary test hardware: M5StickS3 with GPS/BDS Unit v1.1 (AT6668). Only Fujifilm
cameras are available for hardware tests. Other vendors are covered by code
review and the FauxNY test camera, and are marked as untested in each PR.

A plan is a historical record once its work lands. The "Implementation state"
or "State" section inside each document is authoritative over the roadmap
summary; the index is an entry point, not a status claim. Open issues are owned
by the plans in the table below.

Fully implemented plans are archived under [implemented/](implemented/README.md).

## Open issue ownership (2026-11-02)

Every open issue maps to one owning plan. The plan records the implementation
state and the remaining gate; the issue stays open until that gate lands.

| Issue | Plan | Current state |
| --- | --- | --- |
| #280 | [288](implemented/288-provisioning-schema.md) | Ledger refreshed; no open PR claims a wire id. Only the documented rebase-time re-audit remains. |
| #281 | [291](291-ui-upstream-layout-parity.md) | Open. About, GPS, and Connected content parity with upstream, one page PR each. |
| #282 | [168](168-notouch-layout-overflows.md) | Resolved on master by the StickC text-size clamp and `bughunt/stickc-connected-large-imu.txt`. |
| #283 | [173](173-sim-scheduler-visibility.md) | Diagnostics merged; root cause of the restart SIGSEGV still unknown. Tracker stays open. |
| #288 | [288](implemented/288-provisioning-schema.md) | 43/46 schema rows and the all-keyed walk are merged. Satisfied. |
| #289 | [173](173-sim-scheduler-visibility.md) | 3 of 5 items done. Per-PID preferences sweep, Secure/reconnect load proof, and fairness/waiter dump remain. |
| #290 | [290](implemented/290-sim-page-query-map.md) | Merged with PR #273; the query-map contract test passes. Satisfied. |

## Engineering lessons

- [95-engineering-lessons.md](95-engineering-lessons.md): durable hardware,
  build, CI, git, and protocol findings. Read the matching section before you
  touch that area so you do not relearn them. The CLAUDE.md files carry the
  one-line pointers, this doc carries the detail.
- [96-wifi-bt-ble-hardening.md](96-wifi-bt-ble-hardening.md): WiFi, Bluetooth
  and BLE issue inventory with a sequenced remediation plan across fork/master
  and the open radio PRs.

## Hardware experiments

- [00-hardware-experiments.md](00-hardware-experiments.md): two cheap tests that
  decide defaults later. A: does the StickS3 have a 32.768 kHz crystal. B: does
  the GPS unit v1.1 keep ephemeris without 5 V, and does it honor $PCAS12.

## Phase 0: small improvements, all boards

| Doc | Content |
|---|---|
| [00b-dev-usb-debug.md](00b-dev-usb-debug.md) | USB debug tooling. JTAG on S3, serial elsewhere |
| [01-cpu-freq-setting.md](implemented/01-cpu-freq-setting.md) | CPU frequency setting, fix 80 vs 160 MHz mismatch |
| [02-battery-display.md](implemented/02-battery-display.md) | Battery percent, voltage, current, runtime estimate |
| [03-settings-while-connected.md](implemented/03-settings-while-connected.md) | Reach settings while connected |
| [04-bulb-timer.md](implemented/04-bulb-timer.md) | Bulb timer for long exposures |
| [05-diagnostics-scaffold.md](implemented/05-diagnostics-scaffold.md) | Expanded About page, Diagnostics submenu |

## Phase 1: power infrastructure

| Doc | Content |
|---|---|
| [06-power-module.md](implemented/06-power-module.md) | Power manager module with counted pm locks |
| [07-ble-sleep.md](implemented/07-ble-sleep.md) | BLE modem sleep and light sleep while connected |
| [08-scan-tuning.md](implemented/08-scan-tuning.md) | Scan duty cycle presets and scan timeout |
| [09-reconnect-backoff.md](implemented/09-reconnect-backoff.md) | Exponential backoff for reconnect |
| [10-adaptive-conn-params.md](10-adaptive-conn-params.md) | Idle vs shooting connection parameters |
| [11-adaptive-tx-power.md](11-adaptive-tx-power.md) | RSSI based TX power stepping |

## Phase 2: display and power UX

| Doc | Content |
|---|---|
| [12-display-off.md](implemented/12-display-off.md) | True display off, blind remote mode, longer timeouts |
| [13-auto-off-low-batt.md](implemented/13-auto-off-low-batt.md) | Auto power off, low battery actions |

## Phase 3: GPS

| Doc | Content |
|---|---|
| [14-gps-pcas.md](implemented/14-gps-pcas.md) | $PCAS receiver config: rate, sentences, constellation |
| [15-gps-power.md](implemented/15-gps-power.md) | GPS duty cycling, light sleep compatible UART |

## Phase 4: sensors

| Doc | Content |
|---|---|
| [16-imu-spirit-level.md](implemented/16-imu-spirit-level.md) | Enable IMU, spirit level page |
| [17-imu-gestures.md](implemented/17-imu-gestures.md) | Tap and shake wake, double tap shutter |
| [18-gps-motion.md](18-gps-motion.md) | Motion adaptive GPS |
| [19-interval-deep-sleep.md](implemented/19-interval-deep-sleep.md) | Deep sleep between intervalometer shots |

## Phase 5: extended features

| Doc | Content |
|---|---|
| [20-imu-hw-motion.md](implemented/20-imu-hw-motion.md) | Hardware motion detection via IMU engines |
| [21-imu-dead-reckoning.md](implemented/21-imu-dead-reckoning.md) | GPS position hold when fix is lost, bounded hold and extrapolation implemented |
| [22-ir-remote-trigger.md](implemented/22-ir-remote-trigger.md) | IR shutter trigger (Nikon, Sony, Canon protocols) |
| [23-feedback-outputs.md](implemented/23-feedback-outputs.md) | Beep, LED, vibration feedback |
| [24-sd-gpx-logging.md](implemented/24-sd-gpx-logging.md) | SD card GPX logging and settings backup |

## Phase 6: usability and robustness

| Doc | Content |
|---|---|
| [25-multiconnect-ui.md](implemented/25-multiconnect-ui.md) | Multi-connect selection, Cameras status page |
| [26-pm1-watchdog.md](implemented/26-pm1-watchdog.md) | M5PM1 hardware watchdog, StickS3 lockup recovery |
| [27-usb-console.md](implemented/27-usb-console.md) | USB serial command console for testing |
| [76-reconnect-stuck.md](implemented/76-reconnect-stuck.md) | Fast reconnect to Fujifilm Secure stalls in connecting: diagnosis and fix plan |
| [28-emulator.md](28-emulator.md) | Host SDL simulator for the furble UI |
| [29-virtual-test-rig.md](implemented/29-virtual-test-rig.md) | Sim to Android app test rig over TCP, no radio |
| [31-s3-psram.md](31-s3-psram.md) | Enable PSRAM on the M5StickS3 |
| [32-gps-advanced.md](32-gps-advanced.md) | Advanced GPS support for the GPS/BDS Unit v1.1 |
| [33-wifi-hub.md](33-wifi-hub.md) | WiFi hub: displayless build, provisioning, MQTT |
| [34-ota-partitions.md](implemented/34-ota-partitions.md) | OTA updates and the partition scheme change |
| [35-exposure-presets.md](implemented/35-exposure-presets.md) | Exposure time presets in 1/3 stops |
| [36-camera-test-harness.md](implemented/36-camera-test-harness.md) | Camera protocol test harness: host tests, mock NimBLE, virtual peer |
| [71-ui-bug-batch.md](implemented/71-ui-bug-batch.md) | UI focus recovery, left escape, connection progress, display-off LED policy |
| [68-reconnect-after-restart.md](implemented/68-reconnect-after-restart.md) | Graceful camera shutdown and patient reconnect after restart |
| [76-ui-polish.md](implemented/76-ui-polish.md) | Focus ring visible in every theme, simulator theme and board selection for the screenshot matrix |

## Framework work

| Doc | Content |
|---|---|
| [30-m5stack-framework.md](30-m5stack-framework.md) | Upstream M5Stack library gaps, fork and PR strategy |
| [40-thinknode-port.md](40-thinknode-port.md) | ThinkNode port feasibility |
| [41-alternative-hardware.md](41-alternative-hardware.md) | Alternative sidecar hardware |
| [42-waveshare-eth-node.md](implemented/42-waveshare-eth-node.md) | Waveshare ESP32-S3-ETH wired MQTT node |
| [287-mcu-portability-and-bundle-hardware.md](287-mcu-portability-and-bundle-hardware.md) | Whole-stack MCU portability, battery model, GPS quality, and buyable BLE plus GNSS hardware |

## Bug analyses

| Doc | Content |
|---|---|
| [75-false-connected.md](implemented/75-false-connected.md) | False Connected when the camera is not in pairing mode. Promotion to ACTIVE is gated on GATT plumbing, not on the camera-side registration confirmation |
| [80-camera-lifetime.md](implemented/80-camera-lifetime.md) | Use after free: CameraList owns Cameras by unique_ptr and load()/clear() free them while Control holds raw pointers across an in-flight connect. Own Cameras by shared_ptr so a connecting or active Camera outlives the list |

## Simulator stability

| Doc | Content |
|---|---|
| [122-gps-sim-stability.md](implemented/122-gps-sim-stability.md) | Serialize TinyGPSPlus ownership across the GPS task, LVGL timers, and console snapshots; exercise concurrent GPS pages in the host simulator |
| [123-host-task-lifecycle.md](implemented/123-host-task-lifecycle.md) | Join real-Control harness tasks before host static destruction |
| [126-settings-handle-concurrency.md](implemented/126-settings-handle-concurrency.md) | Use operation-local Preferences handles so concurrent settings calls cannot replace or close each other's NVS handle |

## Tooling and web installer

| Doc | Content |
|---|---|
| [68-flasher-debug-firmware.md](implemented/68-flasher-debug-firmware.md) | Web flasher debug firmware option: build *-debug envs, debug manifests, install checkbox |
| [139-persistent-time.md](implemented/139-persistent-time.md) | RTC, NVS, GPS, NTP, and companion wall-clock retention policy |
| [69-flasher-bt-dump.md](69-flasher-bt-dump.md) | Web Serial Capture BT debug dump panel, depends on 68 and PR #76 |
| [113-per-board-ota-slots.md](implemented/113-per-board-ota-slots.md) | Per-flash-size OTA slots, implemented by PR #167 |
| [114-provision-parser.md](implemented/114-provision-parser.md) | One-shot provisioning parser and console apply; staged browser transport deferred |
| [114-flasher-provisioning.md](114-flasher-provisioning.md) | Follow-up browser transport for the landed provisioning parser |
| [115-ota-engine.md](implemented/115-ota-engine.md) | Transport-independent OTA lifecycle, implemented by PR #168 |
| [115-ota-update-mqtt.md](115-ota-update-mqtt.md) | HTTPS delivery and OTA-over-MQTT follow-up |
| [130-ota-mqtt-contract.md](130-ota-mqtt-contract.md) | Signed, replay-safe OTA-over-MQTT envelope and chunk policy |
| [131-ota-replay-store.md](implemented/131-ota-replay-store.md) | Durable two-slot anti-rollback replay journal |
| [132-ota-partition-sink.md](implemented/132-ota-partition-sink.md) | Ordered, digest-verified inactive-partition sink |
| [177-mqtt-4mb-size-followup.md](177-mqtt-4mb-size-followup.md) | MQTT size follow-up and developer-only single-factory USB fallback |
| [124-sim-incremental-deps.md](implemented/124-sim-incremental-deps.md) | Compiler depfiles for reliable direct-simulator incremental builds |
| [127-dev-version-identity.md](implemented/127-dev-version-identity.md) | Append an unambiguous short Git revision to development firmware versions while preserving release tags |
| [142-bt-journal-memory.md](implemented/142-bt-journal-memory.md) | Compact, capability-aware Bluetooth journal storage and loss accounting |

## Network, companion, and simulator follow-ups

| Doc | Content |
|---|---|
| [116-companion-password.md](implemented/116-companion-password.md) | Shared-secret gate for privileged companion operations |
| [117-sim-mqtt-coverage.md](implemented/117-sim-mqtt-coverage.md) | Host MQTT client and Home Assistant discovery coverage |
| [118-sim-ethernet-coverage.md](implemented/118-sim-ethernet-coverage.md) | Host Ethernet netif coverage for the transport-neutral MQTT seam |
| [119-companion-gatt-sim.md](implemented/119-companion-gatt-sim.md) | Companion GATT host coverage, implemented by the companion harness |
| [120-sim-multiconnect-coverage.md](120-sim-multiconnect-coverage.md) | Multi-camera survivor and reconnect coverage |
| [121-sim-usage-fault-docs.md](implemented/121-sim-usage-fault-docs.md) | Simulator usage and fault-injection reference, implemented |

## Optimization program

| Doc | Content |
|---|---|
| [125-esp-optimization-program.md](125-esp-optimization-program.md) | Measurement-first ESP-IDF, radio, power, memory, UI, dependency, security, and release optimization program |
| [127-nordic-port-baseline.md](127-nordic-port-baseline.md) | Simulator-gated portability boundary and Nordic silicon port baseline |
| [143-pm1-watchdog-window.md](implemented/143-pm1-watchdog-window.md) | Safe 45 second M5PM1 watchdog window for OTA health validation |
| [144-upload-partition-offset.md](implemented/144-upload-partition-offset.md) | Keep PlatformIO no-build uploads aligned with OTA application partitions |
| [145-connect-context-initializer.md](implemented/145-connect-context-initializer.md) | Explicit initialization for the LVGL connection context |
| [146-setstate-sleep-lock-order.md](implemented/146-setstate-sleep-lock-order.md) | Acquire the sleep lock before publishing the active control state |
| [147-connect-reclaim-order.md](implemented/147-connect-reclaim-order.md) | Failed-connect reclaim ordering for the Ricoh secure-timeout use-after-free |
| [148-teardown-connect-cancel.md](implemented/148-teardown-connect-cancel.md) | Connect cancellation token for the registration-wait teardown wedge |
| [149-ricoh-sleep-shutter-gate.md](149-ricoh-sleep-shutter-gate.md) | Fresh OperationMode gate so a sleeping GR IV never receives capture writes |
| [150-nimble-taskdata-race.md](implemented/150-nimble-taskdata-race.md) | Vendored esp-nimble-cpp fix for the task data release use-after-scope race |
| [151-fujifilm-registration-followups.md](implemented/151-fujifilm-registration-followups.md) | Registration follow-ups: Secure stale-bond recovery, timeout define, geotag doc reconciliation |
| [152-menu-focus-outline-dedup.md](implemented/152-menu-focus-outline-dedup.md) | Drop the focus ring on menu rows that already carry the accent fill |
| [153-level-main-menu.md](implemented/153-level-main-menu.md) | Spirit level entry on the main menu, usable without a camera connection |
| [154-host-flappy-peer-realism.md](implemented/154-host-flappy-peer-realism.md) | Flappy standby peer realism and multi-target disconnect repros in the host harness |
| [155-sim-ui-liveness.md](155-sim-ui-liveness.md) | Continuous sim liveness invariant and link_lies false-connected coverage |
| [156-restart-restore-seam.md](implemented/156-restart-restore-seam.md) | Sim restart verb and host Control reset seam, the reboot-lockout reland gate |
| [157-control-test-sync-points.md](implemented/157-control-test-sync-points.md) | Deterministic interleaving sync points for the control task and the wedge-guard proof test |
| [158-sim-scheduler-parity.md](158-sim-scheduler-parity.md) | Deterministic unified simulator scheduler and orderly teardown, followed by production connection and calibrated hardware parity |
| [159-camera-peer-certification.md](159-camera-peer-certification.md) | Capture-backed, fail-closed virtual camera peers and exact feature-level compatibility certification |
| [160-sim-scenario-ownership.md](implemented/160-sim-scenario-ownership.md) | Complete simulator scenario ownership manifest and exact CI trigger coverage |
| [290-sim-page-query-map.md](implemented/290-sim-page-query-map.md) | Keep simulator page query maps sized from their initializers; retain alias-aware navigation coverage |
| [161-sim-real-control.md](161-sim-real-control.md) | Production Control/Camera/CameraList/Scan in the simulator over MockNimBLE and virtual peers, with transport-level faults |
| [162-console-host-coverage.md](implemented/162-console-host-coverage.md) | Developer console command suite in the host harness, plus a CI gate that no firmware source escapes both build lists |
| [163-coverage-floor.md](implemented/163-coverage-floor.md) | Measured host and simulator coverage on all three panels, unioned, published in CI and held by a ratcheting floor |
| [164-gps-status-detail.md](implemented/164-gps-status-detail.md) | Receiver fix source, sentence age and power cycle state on the GPS Data page, the whole receiver status struct in the console, on a new GPS receiver status accessor |
| [165-sim-no-touch-layout.md](165-sim-no-touch-layout.md) | Certified per-board simulator coverage of the physical-button layout all three modeled boards ship, plus an indicator-clearance query and the layout gaps it exposes |
| [166-sim-teardown-livelock.md](implemented/166-sim-teardown-livelock.md) | Simulator boot livelock and teardown disconnect timeout: the M5GFX step-exec false positive, UI-thread scheduler fairness, a host wall-clock stall watchdog with thread dumps, and wall-clock bounds on every scenario |
| [167-fujifilm-device-name.md](167-fujifilm-device-name.md) | Fujifilm Secure cameras show the advertised model plus the advertised serial, since the longer camera-menu name is never advertised |
| [168-notouch-layout-overflows.md](168-notouch-layout-overflows.md) | Close every overflow and indicator collision the physical-button layout ships, promote the recorded gaps to hard assertions, and run the certified set in that layout in CI |
| [169-flaky-host-tests.md](implemented/169-flaky-host-tests.md) | Three flaky host tests made deterministic: real scheduler waits instead of spin budgets, a registration sync point instead of sleeps, the aborted-connect republish wedge, and a coverage run that fails on a scenario that never finished |
| [170-control-zombie-connect-cancel.md](implemented/170-control-zombie-connect-cancel.md) | Vendor connect waits honour the plan 148 cancel contract, the interactive teardown cap is logged and honours its bound, and a draining camera is never handed a fresh target that clears its cancel token |
| [171-console-coverage-crash.md](implemented/171-console-coverage-crash.md) | The console suite exits while the control task is still running: stop and join every shim task before static destruction, and a coverage run that names a host test it lost |
| [172-sim-cancel-sweep.md](implemented/172-sim-cancel-sweep.md) | Simulator reproduction of the 2026-09-04 cancel wedge and a certified cancel sweep: a virtual peer that models the blocking Fujifilm Secure handshake, and cancels at fixed offsets across the connect window for every peer topology and connect entry, each checked against one settle invariant, plus the bench power-off hang and a NimBLE client-pool guard the simulator never had |
| [173-sim-scheduler-visibility.md](173-sim-scheduler-visibility.md) | Scheduler-visible host mutex so virtual time stops tracking host load and the cancel bounds come back, one preferences store per simulated device, and a fatal-fault reporter that names the scenario line |
| [174-coverage-empty-profiles.md](implemented/174-coverage-empty-profiles.md) | Two host suites that measured nothing under coverage: the control shim adopts the stop-and-join task contract so both exit through main(), a coverage run fails naming a test whose raw profile is empty or missing, the console shim refuses a task created after shutdown, and the ctest summary header is anchored so a failing test cannot fabricate a crash report |
| [175-coverage-crash-output.md](implemented/175-coverage-crash-output.md) | Failed simulator coverage scenarios retain a bounded output tail so signal diagnostics remain visible in CI |
| [176-sim-task-notifications.md](implemented/176-sim-task-notifications.md) | FreeRTOS task notification counters in the simulator wake blocked owners, preserve clear/decrement and virtual timeout semantics, and unwind on shutdown |
| [285-power-accounting.md](285-power-accounting.md) | Issue #285: opt-in synthetic power accounting and frozen comparator identity |
| [288-provisioning-schema.md](implemented/288-provisioning-schema.md) | Provisioning schema coverage for every keyed setting and current wire-ID reservation ledger |
| [291-ui-upstream-layout-parity.md](291-ui-upstream-layout-parity.md) | About, GPS, and Connected content parity with upstream, one page PR at a time with per-board captures |
| [298-pre-review-gate.md](implemented/298-pre-review-gate.md) | Pre-review scanners for diff hygiene, settings contracts, C++ safety, and a sanitized host subset, wired into pull-request CI |
| [176-android-camera-catalog.md](176-android-camera-catalog.md) | Android camera characteristic discovery, stable-ID catalog, controls, and bounded protocol decoding |
| [178-sim-startup-settings-order.md](implemented/178-sim-startup-settings-order.md) | Apply simulator settings before platform construction while preserving panel, environment, watchdog, and profiler boundaries |

## Additional plan documents

These documents exist on master but were not in the original index. Some share
a number because existing plans are never renumbered; they are distinct files.
They are listed here so no plan is orphaned. Implementation state inside each
document is authoritative over any older summary.

| Doc | Content |
| --- | --- |
| [67-fujifilm-dial-detection.md](67-fujifilm-dial-detection.md) | Fujifilm drive dial detection |
| [73-pentax-k.md](implemented/73-pentax-k.md) | Pentax K through the Ricoh Imaging BLE family |
| [74-dji-osmo.md](implemented/74-dji-osmo.md) | DJI Osmo Action BLE remote support |
| [77-battery-saver-profile.md](implemented/77-battery-saver-profile.md) | Battery Saver profile |
| [82-disconnect-liveness.md](implemented/82-disconnect-liveness.md) | Disconnect liveness |
| [83-lifecycle-tests.md](implemented/83-lifecycle-tests.md) | Host regression tests for the connect and disconnect lifecycle |
| [84-unit-coverage.md](84-unit-coverage.md) | Broader host unit test coverage |
| [85-sim-e2e.md](implemented/85-sim-e2e.md) | Simulator end-to-end scenarios |
| [86-sim-bug-hunt.md](86-sim-bug-hunt.md) | SDL simulator bug hunt |
| [87-sim-buttons.md](implemented/87-sim-buttons.md) | Per-model physical buttons in the SDL sim |
| [88-alternate-hardware.md](88-alternate-hardware.md) | Alternate hardware and Meshtastic coexistence |
| [89-reconnect-ui.md](implemented/89-reconnect-ui.md) | Reconnect UI indicator |
| [92-ui-icons.md](implemented/92-ui-icons.md) | Menu icon set |
| [97-followups.md](97-followups.md) | Consolidated follow-up backlog |
| [98-power-optimization-audit.md](98-power-optimization-audit.md) | Power optimization audit |
| [98b-power-followup.md](98b-power-followup.md) | GPS degraded lock recovery follow-up |
| [99-nimble-client-leak.md](implemented/99-nimble-client-leak.md) | NimBLE client leak fix |
| [100-console-debug.md](implemented/100-console-debug.md) | Console debug dump family |
| [100-sim-fault-injection-bughunt.md](100-sim-fault-injection-bughunt.md) | Host fault injection and BLE connect failure hunting |
| [101-boot-splash.md](implemented/101-boot-splash.md) | Power-on boot splash |
| [102-disconnect-freeze.md](102-disconnect-freeze.md) | Disconnect off the UI task |
| [103-real-control-e2e-harness.md](103-real-control-e2e-harness.md) | Real Control connect/disconnect end-to-end harness |
| [104-ble-fuzzing.md](implemented/104-ble-fuzzing.md) | Seeded BLE lifecycle fuzzing |
| [104-focus-outline-dedup.md](implemented/104-focus-outline-dedup.md) | Single focus ring per selected item |
| [105-ui-fuzzing.md](implemented/105-ui-fuzzing.md) | Seeded UI fuzzer |
| [106-flash-size-optimization.md](106-flash-size-optimization.md) | Flash size optimization |
| [107-ui-layout-audit.md](implemented/107-ui-layout-audit.md) | Walkthrough screenshot layout audit |
| [108-upstream-issue-mapping.md](108-upstream-issue-mapping.md) | Historical upstream issue mapping snapshot |
| [109-sim-coverage-expand.md](implemented/109-sim-coverage-expand.md) | Simulator coverage expansion |
| [110-startup-render-watchdog.md](implemented/110-startup-render-watchdog.md) | Startup render task-watchdog stall |
| [111-power-recommendations.md](111-power-recommendations.md) | Power optimization recommendations |
| [112-connect-crash-mid-drop.md](implemented/112-connect-crash-mid-drop.md) | Connect crash on a mid-connect peer drop |
| [113-connection-state-sim-coverage.md](implemented/113-connection-state-sim-coverage.md) | Connection-state simulator coverage |
| [118-sim-mqtt-transport.md](implemented/118-sim-mqtt-transport.md) | Optional simulator MQTT transport |
| [126-esp-optimization-audit.md](126-esp-optimization-audit.md) | ESP-IDF optimization and reproducibility audit |
| [127-apple-companion.md](127-apple-companion.md) | Apple companion foundation |
| [127-reproducible-builds.md](implemented/127-reproducible-builds.md) | Reproducible firmware builds |
| [129-m5-dependencies-idf55.md](implemented/129-m5-dependencies-idf55.md) | M5 dependency compatibility slice |
| [130-espressif32-613-idf553.md](implemented/130-espressif32-613-idf553.md) | Espressif32 6.13.0 and ESP-IDF 5.5.3 |
| [141-ci-manual-dispatch.md](implemented/141-ci-manual-dispatch.md) | Reliable manual firmware CI |

## Design documents

| Doc | Content |
|---|---|
| [50-companion-app-design.md](50-companion-app-design.md) | Smartphone companion app and GATT service |
| [51-app-feature-parity.md](51-app-feature-parity.md) | Companion app parity: settings editors, camera management |
| [60-issue-257.md](60-issue-257.md) | Nikon Z50II smart device analysis, upstream issue 257 |
| [61-camera-compatibility.md](61-camera-compatibility.md) | BLE protocol survey across camera vendors, ranked additions |
| [62-issue-140.md](implemented/62-issue-140.md) | Configurable button behavior, upstream issue 140 |
| [63-sim-power-analysis.md](63-sim-power-analysis.md) | Simulator based power analysis: profiler, sleep estimator, usage test suite, energy model, CI gate and PR reporting |
| [65-bt-coexistence.md](65-bt-coexistence.md) | Classic BT feasibility, WiFi+BLE coexistence for hub mode |
| [64-debug-tooling.md](64-debug-tooling.md) | Expanded debug tooling: power stats, performance monitoring, BT debug console |
| [67-bulb-ux.md](implemented/67-bulb-ux.md) | Bulb completion state, restart action, and camera mode hint |
| [66-pairing-code-display.md](implemented/66-pairing-code-display.md) | Camera pairing code on the furble display and console |
| [70-text-scaling.md](implemented/70-text-scaling.md) | UI text size setting per board, layout audit walker |
| [72-lumix.md](72-lumix.md) | Panasonic Lumix BLE support port, untested |
| [90-scheduled-shooting.md](90-scheduled-shooting.md) | Deferred: scheduled shooting via RTC alarm |
| [91-mic-trigger.md](91-mic-trigger.md) | Deferred: sound triggered shutter |

## Wire ids

`src/FurbleSettings.cpp` is the source of truth for shipped ids, and every
nonzero id has a row in `SETTING_SCHEMAS`. `tests/host/settings_table_test.cpp`
checks the ledger against the source and rejects duplicate owners;
`tests/host/provision_apply_test.cpp` walks every keyed setting and fails on a
missing schema row. The executable reservation table lives in
[include/CLAUDE.md](../include/CLAUDE.md).

Current master (`ea86482a`, 2026-11-02) uses 1-22, 24-35, 37-41, 43-44, 46-47,
51-55, 62, 65-69, and 72-76. Conditional ids are 23 (`WATCHDOG` on
`FURBLE_M5STICKS3`), 36 (`DISPLAY_MODE` without `FURBLE_NO_DISPLAY`), and 56-61
(`FURBLE_MQTT`). Ids 48-50, 63-64, and 70-71 are free. Ids 42 and 45 retain
historical claims and need a compatibility audit before reuse. Off-wire id 0
remains used by `BULB`, `TOUCH_CALIBRATION`, `MULTISELECT`, `GPX_PERIOD`, and
`BATTERY_SAVER`.

The three open PR heads (#317, #318, #319) claim no setting id. Recheck every
open feature PR head immediately before a rebase or merge; ids only freeze when
a PR merges, and a shipped id such as 46 (`IMU`) never moves.

## Dependencies

```
01 -> 06 -> 07 -> 15 -> 18
            07 -> 19
02 -> 05 -> 10, 16
12 -> 13, 17
16 -> 17, 18
18 -> 20, 21
03, 04, 08, 09, 11, 14, 22, 23, 24: independent
30 (framework) feeds 02, 12, 17, 20
Experiment A feeds 07. Experiment B feeds 15.
25 conflicts with 03 (Connected page grid). 26 conflicts with 19 (deep sleep).
27 needs 00b.
Fork PR #44 (screenshot CI) builds on 28 (simulator) and the 70 audit and
navigation work. The gps.png capture is not byte-reproducible and must not be
baselined as-is (see 28-emulator.md).
```

## Dependency update automation

[286: Dependabot updates](implemented/286-dependabot-updates.md) records the fork-only dependency update configuration and its manual firmware boundaries.
