# Implemented plans

Plans whose implementation is complete and merged on master. Hardware-only
validation notes (measurement, soak, device-look) may remain inside a plan;
they are tracked by their owning hardware gate and do not represent
unimplemented software.

Research, design-only, branch-only, deferred, and partially implemented plans
stay in the parent [`plans/`](../README.md) directory.

| Plan | Title |
| --- | --- |
| [01-cpu-freq-setting.md](01-cpu-freq-setting.md) | PR01: CPU frequency setting |
| [02-battery-display.md](02-battery-display.md) | PR02: battery percent and battery info page |
| [03-settings-while-connected.md](03-settings-while-connected.md) | PR03: reach Settings while connected |
| [04-bulb-timer.md](04-bulb-timer.md) | PR04: bulb timer |
| [05-diagnostics-scaffold.md](05-diagnostics-scaffold.md) | PR05: diagnostics scaffold |
| [06-power-module.md](06-power-module.md) | PR06: FurblePower pm-lock module |
| [07-ble-sleep.md](07-ble-sleep.md) | PR07: BLE modem sleep and light sleep while connected |
| [08-scan-tuning.md](08-scan-tuning.md) | PR08: scan duty presets and scan timeout |
| [09-reconnect-backoff.md](09-reconnect-backoff.md) | PR09: exponential backoff for infinite reconnect |
| [104-focus-outline-dedup.md](104-focus-outline-dedup.md) | Focus outline dedup: single ring per selected item |
| [105-ui-fuzzing.md](105-ui-fuzzing.md) | 105 - Seeded UI fuzzer |
| [107-ui-layout-audit.md](107-ui-layout-audit.md) | UI layout audit: walkthrough screenshot review |
| [109-sim-coverage-expand.md](109-sim-coverage-expand.md) | 109 sim coverage expansion |
| [110-startup-render-watchdog.md](110-startup-render-watchdog.md) | 110 - Startup render task-watchdog stall |
| [112-connect-crash-mid-drop.md](112-connect-crash-mid-drop.md) | 112 - Connect crash on a mid-connect peer drop |
| [113-connection-state-sim-coverage.md](113-connection-state-sim-coverage.md) | Plan 113: connection-state sim coverage |
| [113-per-board-ota-slots.md](113-per-board-ota-slots.md) | 113 - Per-board OTA slot sizing for boards with spare flash |
| [115-ota-engine.md](115-ota-engine.md) | 115 - Transport-independent OTA engine |
| [117-sim-mqtt-coverage.md](117-sim-mqtt-coverage.md) | 117 - Sim MQTT coverage |
| [118-sim-ethernet-coverage.md](118-sim-ethernet-coverage.md) | 118 - Sim Ethernet coverage |
| [118-sim-mqtt-transport.md](118-sim-mqtt-transport.md) | 118 - Optional simulator MQTT transport |
| [119-companion-gatt-sim.md](119-companion-gatt-sim.md) | Plan 119: companion GATT host coverage |
| [121-sim-usage-fault-docs.md](121-sim-usage-fault-docs.md) | PR121: simulator usage and fault-injection reference |
| [122-gps-sim-stability.md](122-gps-sim-stability.md) | 122 - GPS simulator stability and coherent snapshots |
| [123-host-task-lifecycle.md](123-host-task-lifecycle.md) | 123 - Host control task lifecycle |
| [124-sim-incremental-deps.md](124-sim-incremental-deps.md) | 124 - Reliable incremental simulator builds |
| [126-settings-handle-concurrency.md](126-settings-handle-concurrency.md) | 126 - Concurrent settings handles |
| [127-dev-version-identity.md](127-dev-version-identity.md) | 127 - Development firmware revision identity |
| [129-m5-dependencies-idf55.md](129-m5-dependencies-idf55.md) | M5 dependency compatibility slice |
| [130-espressif32-613-idf553.md](130-espressif32-613-idf553.md) | Espressif32 6.13.0 and ESP-IDF 5.5.3 |
| [131-ota-replay-store.md](131-ota-replay-store.md) | 131 - durable OTA replay-store adapter |
| [132-ota-partition-sink.md](132-ota-partition-sink.md) | 132 - OTA partition sink adapter |
| [139-persistent-time.md](139-persistent-time.md) | PR139: persistent wall-clock time |
| [14-gps-pcas.md](14-gps-pcas.md) | PR14 - GPS receiver configuration with $PCAS |
| [141-ci-manual-dispatch.md](141-ci-manual-dispatch.md) | Plan 141: reliable manual firmware CI |
| [142-bt-journal-memory.md](142-bt-journal-memory.md) | Plan 142: bounded Bluetooth journal storage |
| [143-pm1-watchdog-window.md](143-pm1-watchdog-window.md) | 143 - M5PM1 watchdog recovery window |
| [144-upload-partition-offset.md](144-upload-partition-offset.md) | 144 - Keep PlatformIO uploads aligned with OTA partitions |
| [145-connect-context-initializer.md](145-connect-context-initializer.md) | 145 - Explicit connection context initialization |
| [146-setstate-sleep-lock-order.md](146-setstate-sleep-lock-order.md) | 146 - Sleep lock ordering in Control::setState |
| [147-connect-reclaim-order.md](147-connect-reclaim-order.md) | 147 - Failed-connect reclaim ordering |
| [148-teardown-connect-cancel.md](148-teardown-connect-cancel.md) | 148 - Connect cancellation token for the teardown wedge |
| [150-nimble-taskdata-race.md](150-nimble-taskdata-race.md) | 150 - NimBLE task data release race |
| [151-fujifilm-registration-followups.md](151-fujifilm-registration-followups.md) | 151: Fujifilm registration follow-ups |
| [152-menu-focus-outline-dedup.md](152-menu-focus-outline-dedup.md) | 152 - Drop the focus ring on menu rows that already carry the accent fill |
| [153-level-main-menu.md](153-level-main-menu.md) | 153 - Spirit level entry on the main menu |
| [154-host-flappy-peer-realism.md](154-host-flappy-peer-realism.md) | Plan 154: Host test harness peer realism for the multi-target flappy-camera workflow |
| [157-control-test-sync-points.md](157-control-test-sync-points.md) | 157 - Deterministic interleaving sync points for the control task |
| [160-sim-scenario-ownership.md](160-sim-scenario-ownership.md) | 160 - simulator scenario ownership |
| [162-console-host-coverage.md](162-console-host-coverage.md) | 162 - developer console host coverage and a build inventory gate |
| [163-coverage-floor.md](163-coverage-floor.md) | 163 - measured coverage on both stacks, with a ratcheting floor |
| [164-gps-status-detail.md](164-gps-status-detail.md) | 164 - receiver detail on the GPS status page |
| [169-flaky-host-tests.md](169-flaky-host-tests.md) | 169 - deterministic host tests and a coverage run that cannot lose a profile |
| [171-console-coverage-crash.md](171-console-coverage-crash.md) | 171 - the console suite exits while the control task is still running |
| [174-coverage-empty-profiles.md](174-coverage-empty-profiles.md) | 174 - two host suites that measured nothing under coverage |
| [175-coverage-crash-output.md](175-coverage-crash-output.md) | 175 - Coverage crash output |
| [176-sim-task-notifications.md](176-sim-task-notifications.md) | 176 - Simulator task notifications |
| [178-sim-startup-settings-order.md](178-sim-startup-settings-order.md) | 178 - simulator startup settings order |
| [20-imu-hw-motion.md](20-imu-hw-motion.md) | PR20 - Hardware motion detection |
| [22-ir-remote-trigger.md](22-ir-remote-trigger.md) | PR22 - Infrared shutter trigger |
| [24-sd-gpx-logging.md](24-sd-gpx-logging.md) | PR24 - SD card GPX track logging and settings backup |
| [26-pm1-watchdog.md](26-pm1-watchdog.md) | PR26: M5PM1 hardware watchdog on the StickS3 |
| [27-usb-console.md](27-usb-console.md) | PR27: USB serial command console |
| [286-dependabot-updates.md](286-dependabot-updates.md) | Dependabot updates |
| [287-firmware-compatibility-audit.md](287-firmware-compatibility-audit.md) | Camera firmware compatibility audit |
| [288-provisioning-schema.md](288-provisioning-schema.md) | 288 - provisioning schema and wire-ID ledger follow-up |
| [289-camera-parity-and-simulators.md](289-camera-parity-and-simulators.md) | 289: camera parity, virtual peers, and safe fixes |
| [290-sim-page-query-map.md](290-sim-page-query-map.md) | 290 - simulator page query map sizing |
| [298-pre-review-gate.md](298-pre-review-gate.md) | 298 - pre-review scanner gate |
| [35-exposure-presets.md](35-exposure-presets.md) | 35: Exposure time presets in 1/3 stops |
| [36-camera-test-harness.md](36-camera-test-harness.md) | 36 - camera test harness: protocol tests without cameras |
| [42-waveshare-eth-node.md](42-waveshare-eth-node.md) | 42 - Waveshare ESP32-S3-ETH wired MQTT node and display-less onboarding |
| [62-issue-140.md](62-issue-140.md) | 62: Configurable button behavior (upstream issue 140) |
| [67-bulb-ux.md](67-bulb-ux.md) | 67: Bulb exposure UX |
| [68-flasher-debug-firmware.md](68-flasher-debug-firmware.md) | Web flasher debug firmware |
| [75-false-connected.md](75-false-connected.md) | PR75: false Connected when the camera is not in pairing mode |
| [76-ui-polish.md](76-ui-polish.md) | UI polish: focus highlight and sim theme/board capture |
| [77-battery-saver-profile.md](77-battery-saver-profile.md) | 77 - Battery Saver profile |
| [80-camera-lifetime.md](80-camera-lifetime.md) | 80: Camera lifetime, own cameras by shared_ptr |
| [83-lifecycle-tests.md](83-lifecycle-tests.md) | 83 - host regression tests for the connect and disconnect lifecycle |
| [85-sim-e2e.md](85-sim-e2e.md) | 85 - simulator end-to-end scenarios |
| [87-sim-buttons.md](87-sim-buttons.md) | 87 - per-model physical buttons in the SDL sim |
| [89-reconnect-ui.md](89-reconnect-ui.md) | 89 - Reconnect UI indicator (task #54) |
| [92-ui-icons.md](92-ui-icons.md) | 92 - Better suited menu icons |
| [99-nimble-client-leak.md](99-nimble-client-leak.md) | PR99: Fix the NimBLE client leak that breaks all connects until a reboot |
