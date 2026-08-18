# furble improvement plans

Roadmap for power optimization and new features. Each numbered document is one
planned pull request against upstream gkoh/furble. Each PR is independently
mergeable. All new behavior is configurable. Defaults keep current behavior.

Primary test hardware: M5StickS3 with GPS/BDS Unit v1.1 (AT6668). Only Fujifilm
cameras are available for hardware tests. Other vendors are covered by code
review and the FauxNY test camera, and are marked as untested in each PR.

## Engineering lessons

- [95-engineering-lessons.md](95-engineering-lessons.md): durable hardware,
  build, CI, git, and protocol findings. Read the matching section before you
  touch that area so you do not relearn them. The CLAUDE.md files carry the
  one-line pointers, this doc carries the detail.

## Hardware experiments

- [00-hardware-experiments.md](00-hardware-experiments.md): two cheap tests that
  decide defaults later. A: does the StickS3 have a 32.768 kHz crystal. B: does
  the GPS unit v1.1 keep ephemeris without 5 V, and does it honor $PCAS12.

## Phase 0: small improvements, all boards

| Doc | Content |
|---|---|
| [00b-dev-usb-debug.md](00b-dev-usb-debug.md) | USB debug tooling. JTAG on S3, serial elsewhere |
| [01-cpu-freq-setting.md](01-cpu-freq-setting.md) | CPU frequency setting, fix 80 vs 160 MHz mismatch |
| [02-battery-display.md](02-battery-display.md) | Battery percent, voltage, current, runtime estimate |
| [03-settings-while-connected.md](03-settings-while-connected.md) | Reach settings while connected |
| [04-bulb-timer.md](04-bulb-timer.md) | Bulb timer for long exposures |
| [05-diagnostics-scaffold.md](05-diagnostics-scaffold.md) | Expanded About page, Diagnostics submenu |

## Phase 1: power infrastructure

| Doc | Content |
|---|---|
| [06-power-module.md](06-power-module.md) | Power manager module with counted pm locks |
| [07-ble-sleep.md](07-ble-sleep.md) | BLE modem sleep and light sleep while connected |
| [08-scan-tuning.md](08-scan-tuning.md) | Scan duty cycle presets and scan timeout |
| [09-reconnect-backoff.md](09-reconnect-backoff.md) | Exponential backoff for reconnect |
| [10-adaptive-conn-params.md](10-adaptive-conn-params.md) | Idle vs shooting connection parameters |
| [11-adaptive-tx-power.md](11-adaptive-tx-power.md) | RSSI based TX power stepping |

## Phase 2: display and power UX

| Doc | Content |
|---|---|
| [12-display-off.md](12-display-off.md) | True display off, blind remote mode, longer timeouts |
| [13-auto-off-low-batt.md](13-auto-off-low-batt.md) | Auto power off, low battery actions |

## Phase 3: GPS

| Doc | Content |
|---|---|
| [14-gps-pcas.md](14-gps-pcas.md) | $PCAS receiver config: rate, sentences, constellation |
| [15-gps-power.md](15-gps-power.md) | GPS duty cycling, light sleep compatible UART |

## Phase 4: sensors

| Doc | Content |
|---|---|
| [16-imu-spirit-level.md](16-imu-spirit-level.md) | Enable IMU, spirit level page |
| [17-imu-gestures.md](17-imu-gestures.md) | Tap and shake wake, double tap shutter |
| [18-gps-motion.md](18-gps-motion.md) | Motion adaptive GPS |
| [19-interval-deep-sleep.md](19-interval-deep-sleep.md) | Deep sleep between intervalometer shots |

## Phase 5: extended features

| Doc | Content |
|---|---|
| [20-imu-hw-motion.md](20-imu-hw-motion.md) | Hardware motion detection via IMU engines |
| [21-imu-dead-reckoning.md](21-imu-dead-reckoning.md) | GPS position hold when fix is lost |
| [22-ir-remote-trigger.md](22-ir-remote-trigger.md) | IR shutter trigger (Nikon, Sony, Canon protocols) |
| [23-feedback-outputs.md](23-feedback-outputs.md) | Beep, LED, vibration feedback |
| [24-sd-gpx-logging.md](24-sd-gpx-logging.md) | SD card GPX logging and settings backup |

## Phase 6: usability and robustness

| Doc | Content |
|---|---|
| [25-multiconnect-ui.md](25-multiconnect-ui.md) | Multi-connect selection, Cameras status page |
| [26-pm1-watchdog.md](26-pm1-watchdog.md) | M5PM1 hardware watchdog, StickS3 lockup recovery |
| [27-usb-console.md](27-usb-console.md) | USB serial command console for testing |
| [28-emulator.md](28-emulator.md) | Host SDL simulator for the furble UI |
| [29-virtual-test-rig.md](29-virtual-test-rig.md) | Sim to Android app test rig over TCP, no radio |
| [31-s3-psram.md](31-s3-psram.md) | Enable PSRAM on the M5StickS3 |
| [32-gps-advanced.md](32-gps-advanced.md) | Advanced GPS support for the GPS/BDS Unit v1.1 |
| [33-wifi-hub.md](33-wifi-hub.md) | WiFi hub: displayless build, provisioning, MQTT |
| [34-ota-partitions.md](34-ota-partitions.md) | OTA updates and the partition scheme change |
| [35-exposure-presets.md](35-exposure-presets.md) | Exposure time presets in 1/3 stops |
| [36-camera-test-harness.md](36-camera-test-harness.md) | Camera protocol test harness: host tests, mock NimBLE, virtual peer |
| [71-ui-bug-batch.md](71-ui-bug-batch.md) | UI focus recovery, left escape, connection progress, display-off LED policy |

## Framework work

| Doc | Content |
|---|---|
| [30-m5stack-framework.md](30-m5stack-framework.md) | Upstream M5Stack library gaps, fork and PR strategy |
| [40-thinknode-port.md](40-thinknode-port.md) | ThinkNode port feasibility |
| [41-alternative-hardware.md](41-alternative-hardware.md) | Alternative sidecar hardware |

## Bug analyses

| Doc | Content |
|---|---|
| [75-false-connected.md](75-false-connected.md) | False Connected when the camera is not in pairing mode. Promotion to ACTIVE is gated on GATT plumbing, not on the camera-side registration confirmation |

## Tooling and web installer

| Doc | Content |
|---|---|
| [68-flasher-debug-firmware.md](68-flasher-debug-firmware.md) | Web flasher debug firmware option: build *-debug envs, debug manifests, install checkbox |
| [69-flasher-bt-dump.md](69-flasher-bt-dump.md) | Web Serial Capture BT debug dump panel, depends on 68 and PR #76 |

## Design documents

| Doc | Content |
|---|---|
| [50-companion-app-design.md](50-companion-app-design.md) | Smartphone companion app and GATT service |
| [51-app-feature-parity.md](51-app-feature-parity.md) | Companion app parity: settings editors, camera management |
| [60-issue-257.md](60-issue-257.md) | Nikon Z50II smart device analysis, upstream issue 257 |
| [61-camera-compatibility.md](61-camera-compatibility.md) | BLE protocol survey across camera vendors, ranked additions |
| [62-issue-140.md](62-issue-140.md) | Configurable button behavior, upstream issue 140 |
| [63-sim-power-analysis.md](63-sim-power-analysis.md) | Simulator based power analysis: profiler, sleep estimator, usage test suite, energy model, CI gate and PR reporting |
| [65-bt-coexistence.md](65-bt-coexistence.md) | Classic BT feasibility, WiFi+BLE coexistence for hub mode |
| [64-debug-tooling.md](64-debug-tooling.md) | Expanded debug tooling: power stats, performance monitoring, BT debug console |
| [90-scheduled-shooting.md](90-scheduled-shooting.md) | Deferred: scheduled shooting via RTC alarm |
| [91-mic-trigger.md](91-mic-trigger.md) | Deferred: sound triggered shutter |

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
Fork PR #44 (screenshot CI) builds on 28 (simulator). The gps.png capture is
not byte-reproducible and must not be baselined as-is (see 28-emulator.md).
```
