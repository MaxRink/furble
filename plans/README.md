# furble improvement plans

Roadmap for power optimization and new features. Each numbered document is one
planned change, usually one pull request. Fork-only changes land on fork master.
Each PR is independently mergeable. All new behavior is configurable. Defaults
keep current behavior.

Primary test hardware: M5StickS3 with GPS/BDS Unit v1.1 (AT6668). Only Fujifilm
cameras are available for hardware tests. Other vendors are covered by code
review and the FauxNY test camera, and are marked as untested in each PR.

**Sim-only mode (2026-11-13).** Master is at `e66e8807` (#439). Plan 314
(memory resilience) merged as #411 to #439 after the #407 baseline. Hardware
validation is deferred, so no board measurement after #407 is claimed. The exact gate results, the hardware-deferred list and the resume
criteria are in [313-sim-only-status.md](313-sim-only-status.md).

A plan is a historical record once its work lands. The "Implementation state"
or "State" section inside each document is authoritative over the roadmap
summary; the index is an entry point, not a status claim. Open issues are owned
by the plans in the table below.

Fully implemented plans are archived under [implemented/](implemented/README.md).

## Plans cleanup (plan 346)

[346-plans-cleanup.md](346-plans-cleanup.md) lists every document this cleanup
moved, every status line it corrected and every plan it left alone with the
reason. A row below that links into `implemented/` is archived; the row keeps
its phase position so the roadmap order stays readable.

The sim-only paragraph above is dated 2026-11-13 and describes master at
`e66e8807` (#439). Master has moved on since: this cleanup lands on
`85b89931` (#494). Those gate results were not re-run, and
[313-sim-only-status.md](313-sim-only-status.md) still holds them unchanged.

## Open issue ownership (2026-11-06)

Every open issue maps to one owning plan. The plan records the implementation
state and the remaining gate; the issue stays open until that gate lands.

| Issue | Plan | Current state |
| --- | --- | --- |
| #283 | [173](173-sim-scheduler-visibility.md) | Root cause identified: a growing `setenv` freed `environ` under the SDL loop's `getenv`. `a57e77e7` removed the mid-loop growth. The boot `TZ` growth is ordered before the loop, and the sim now creates `TZ` first so a sim-e2e guard can forbid any growth after `SDL_Init`. Not reproduced on demand. Ready to close; reopen on a new `SIM CRASH` dump. |
| #289 | [173](173-sim-scheduler-visibility.md) | All five items done. The item 4 settle failure was `Scan::stop()` waiting on a plain condition variable for a radio-task callback, invisible to the simulator scheduler. It is now `scan_condition_t`, a scheduler-visible type under `FURBLE_SIM`. 6,000 container runs passed with no breaker parks; the last 3,600 ran at a median loadavg of 81.5. |
| #381 | [302](302-companion-restart-reconnect.md) | Open. Long USB console runs reset into the ROM USB-JTAG unit. The 5 s command timeout and monitor fixes are merged. Host-side recovery is implemented: the USB monitor reopens a lost port, reports ROM reset lines and app restarts, and hard resets only with `--usb-hard-reset`, with a fake-transport test. A UART0 panic capture and the hardware retest of the recovery remain. |
| #390 | [304](304-webui-stick-memory.md) | Open. The WebUI now fails closed on the StickS3 instead of boot-looping, and the TLS identity is generated before WiFi and the companion. Simulator work is complete: `hw-memory-webui-first-boot` covers a first boot with WiFi, BLE, the companion and a camera connect, on the inferred 25,911 / 24,148 heap. A connect now sheds the WebUI before its target task starts, the icon cache lives in PSRAM and is dropped at disconnect, so HTTPS pauses for the camera session and returns after it. Review record: [304-review-feedback.md](304-review-feedback.md). A support claim still needs the hardware memory gate, a TLS handshake, a settings write and a soak; plan 304 holds the exact procedure. |
| #455 | [332](332-sim-rtc.md) | Implemented on `feat/455-sim-rtc` in #507: the simulator models a battery-backed RTC, and the deep-sleep scenarios cover a rejected stale record, an accepted valid record, an immediate late shot and a resume after the cell dropped. Closed against #507; the hardware item stays with the plan. |
| upstream #316 | [359](359-sony-legacy-generation.md) | Fixed on `fix/316-sony-7rm4`: the 2019 advertisement generation is accepted, the ATT MTU floor is requested and verified with a graceful location skip, and a body without DD30/DD31 gets a best-effort DD11 write. Host and virtual peer only; a real ILCE-7RM4 capture remains, listed in the plan. |

Closed issues #280, #281, #282, #288, #290, #451 and #464 are retained in their
plans and in the git history. They are no longer ownership rows. #451 closed
with #453 (`139d884a`), whose message carries `Fixes #451`; plan 325 is archived
under [implemented/](implemented/README.md). #464 closed with #478 (`b57f0118`),
whose title carries it, and plan 331 is archived there with it.

## Engineering lessons

- [95-engineering-lessons.md](95-engineering-lessons.md): durable hardware,
  build, CI, git, and protocol findings. Read the matching section before you
  touch that area so you do not relearn them. The CLAUDE.md files carry the
  one-line pointers, this doc carries the detail.
- [96-wifi-bt-ble-hardening.md](96-wifi-bt-ble-hardening.md): WiFi, Bluetooth
  and BLE issue inventory with a sequenced remediation plan across fork/master
  and the open radio PRs.

## Hardware experiments

- [00-hardware-experiments.md](00-hardware-experiments.md): two measured
  answers that set later defaults. A: the StickS3 has no 32.768 kHz crystal. B:
  the GPS unit v1.1 has no backup supply, so a rail cut costs a cold start, and
  it honors `$PCAS12` timed standby.

## Phase 0: small improvements, all boards

| Doc | Content |
|---|---|
| [00b-dev-usb-debug.md](implemented/00b-dev-usb-debug.md) | USB debug tooling. JTAG on S3, serial elsewhere |
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
| [10-adaptive-conn-params.md](implemented/10-adaptive-conn-params.md) | Idle vs shooting connection parameters |
| [11-adaptive-tx-power.md](implemented/11-adaptive-tx-power.md) | RSSI based TX power stepping |

## Phase 2: display and power UX

| Doc | Content |
|---|---|

## Phase 3: GPS

| Doc | Content |
|---|---|
| [14-gps-pcas.md](implemented/14-gps-pcas.md) | $PCAS receiver config: rate, sentences, constellation |
| [15-gps-power.md](implemented/15-gps-power.md) | GPS duty cycling, light sleep compatible UART |
| [304-webui-stick-memory.md](304-webui-stick-memory.md) | WebUI internal-RAM budget and the StickS3 boot-loop investigation |
| [304-review-feedback.md](304-review-feedback.md) | Review-feedback record for the #381 USB recovery and the #390 first-boot closeout |
| [305-general-internal-ram.md](implemented/305-general-internal-ram.md) | General internal-RAM map and safe reductions across tasks, LVGL, IR, console and mbedTLS |
| [306-sim-memory-crash-repro.md](implemented/306-sim-memory-crash-repro.md) | Simulator heap-pressure reproduction, MQTT/WebUI admission scenarios and retained crash logging |
| [307-sim-first-flash-gate.md](implemented/307-sim-first-flash-gate.md) | Sim-first crash, memory, fuzz and permutation release gate before any hardware flash |
| [308-mqtt-ha-memory.md](implemented/308-mqtt-ha-memory.md) | MQTT and Home Assistant discovery memory budget, admission gates and cached HA setting |
| [309-input-api-memory-fuzzing.md](implemented/309-input-api-memory-fuzzing.md) | Seeded input, API and memory fuzz gate with replay corpus and known-finding owners |
| [310-review-feedback.md](implemented/310-review-feedback.md) | Review-thread audit, code fixes, regression evidence and the online merge gate |
| [311-mqtt-queue-psram.md](implemented/311-mqtt-queue-psram.md) | MQTT event queue in PSRAM, pre-task owner gate and the 12 KiB main-stack revert |
| [312-review-feedback.md](implemented/312-review-feedback.md) | Review-feedback record for the MQTT queue fix, including the internal fallback gate |
| [313-sim-only-status.md](313-sim-only-status.md) | Sim-only closeout: exact gate results, hardware-deferred items and resume criteria |
| [314-memory-resilience.md](314-memory-resilience.md) | Memory resilience: sim heap fidelity and abort parity, load shedding, static and dynamic memory gates |
| [335-stack-cuts.md](335-stack-cuts.md) | Control stack cut withdrawn: the walk omitted the pure virtual camera connect dispatch, so the gate now resolves it and the NimBLE pairing cycle is a known limit |
| [335-review-feedback.md](335-review-feedback.md) | Review-feedback record for the withdrawn control cut: the omitted dispatch, the 25 percent line, and the checks that held |
| [314-review-feedback.md](314-review-feedback.md) | Review-feedback record for the plan 314 memory resilience series |
| [356-sim-memory-certify.md](356-sim-memory-certify.md) | Simulator memory certification: the four MQTT memory scenarios certified, a `FURBLE_SIM_MQTT=1` coverage build, and the measured `src/FurbleMQTT.cpp` number |
| [323-plans-refresh-review-feedback.md](implemented/323-plans-refresh-review-feedback.md) | Review-feedback record for the plans refresh, PR #441 |
| [315-stickc-layout-regressions.md](implemented/315-stickc-layout-regressions.md) | M5StickC header battery anchor, Cameras page double reserve and Time page scroll fixes |
| [316-docs-capture-companion-reject.md](implemented/316-docs-capture-companion-reject.md) | Docs capture: rename the hardware-model action that shadowed the companion pairing Reject |
| [317-notouch-regressions.md](317-notouch-regressions.md) | Physical-button suite: control drain wake, stale legend pass, info page padding, memory profile constants |
| [321-ublox-m10-driver.md](implemented/321-ublox-m10-driver.md) | u-blox M10 UBX driver: codec, receiver backend seam, virtual M10 in the simulator, constellations, rate, power save, AssistNow Autonomous and warm start |
| [321-review-feedback.md](implemented/321-review-feedback.md) | Review-feedback record for the u-blox M10 stack: tick-wrap gate, non-switchable rails, time resolution, ANA default, demux fuzzing, receiver encoder payload bound, source URLs |
| [325-peer-subscription-race.md](implemented/325-peer-subscription-race.md) | Thread-safe subscription tables for the virtual camera test doubles (#451) |
| [325-review-feedback.md](implemented/325-review-feedback.md) | Review-feedback record for the virtual camera subscription race |
| [326-docs-accuracy.md](implemented/326-docs-accuracy.md) | Documentation accuracy refresh: stale plan status, wiki screenshot claims and gate numbers |
| [332-gnss-vendor-audit.md](332-gnss-vendor-audit.md) | GNSS configuration audit against the CASIC V6.3.2 and V4.2.0.3, AT6668 and u-blox M10 documents: `$PCAS03` field count, `$PCAS04` encoding, B1C rejections and write order |
| [326-review-feedback.md](implemented/326-review-feedback.md) | Review-feedback record for the documentation accuracy refresh |
| [327-control-gps-race.md](327-control-gps-race.md) | Locked `Control::m_Targets` walk for the GPS fan-out and the control task's `STATE_ACTIVE` liveness check |
| [328-reference-docs-audit.md](328-reference-docs-audit.md) | Reference documentation audit: settings and console claims re-verified against the code, with the fixes and the claims that did not hold |
| [332-imu-motion-scale-audit.md](332-imu-motion-scale-audit.md) | IMU motion wake scale audit against the BMI270 revision 1.6, MPU-6886 revision 1.1 and QMI8658A Rev D documents; one scale definition shared by the backends, the simulator double and the host tests |
| [332-imu-motion-scale-review-feedback.md](332-imu-motion-scale-review-feedback.md) | Review-feedback record for PR 504: the bytes decode to 83.0 and 70.3 mg, both durations obey the 50 Hz sample rule, the MPU-6886 count size is an assumption everywhere and no register value changed, plus the two component pages the change left contradicting the code |
| [332-sim-rtc.md](332-sim-rtc.md) | Battery-backed RTC in the simulator: seeded calendar chip, `FURBLE_SIM` TimeKeeper seam, and the stale, valid, late and lost deep-sleep scenarios (#455) |
| [332-review-feedback.md](332-review-feedback.md) | Review-feedback record for the simulator RTC model: the unasserted production epoch, the leaked `.rtc` sidecar, the completed scenario table, and the board-row duplication |
| [340-gps-uart-install.md](340-gps-uart-install.md) | GPS UART install failure: absent receiver without a task, null-queue guards and the install-failure scenario |
| [340-review-feedback.md](340-review-feedback.md) | Review-feedback record for PR 509: the publish order, the fake's error code, the wiki paragraph, the firmware compile claim, and the race the latch cannot close |
| [341-sd-shutdown-ack.md](341-sd-shutdown-ack.md) | SD power-off handshake: `SD::powerOff()` reports a refusal for a missed acknowledgement or a request the writer never took, `Platform::powerOff()` then keeps the rail up, and the automatic paths back off |
| [342-wifi-lost-ip.md](342-wifi-lost-ip.md) | Lost DHCP lease: `IP_EVENT_STA_LOST_IP` clears the WiFi cache, and the WebUI gate and status page read the live station address |
| [342-review-feedback.md](342-review-feedback.md) | Review-feedback record for the lost-lease fix: doc paragraph, deliberate-stop gate test, and five recorded follow-ups |
| [339-mqtt-retry-backoff.md](339-mqtt-retry-backoff.md) | Capped exponential MQTT retry backoff and a WiFi-style failure classifier, so an outage never blocks the hub |
| [332-vendor-document-verification.md](332-vendor-document-verification.md) | Vendor document verification: ESP32 and ESP32-S3 TRMs, S3 and PICO-1 datasheets, SX1262 Rev 2.2, W5500 1.1.0, M5PM1 V1.9 and the FCC grants, with revision, page and hash for every fact |
| [332-docs-gap-closure.md](332-docs-gap-closure.md) | Component and protocol documentation gap closure: inventory, gap register, 47 rows closed and 48 open, each open row naming the document and section it needs |
| [349-waveshare-board-row.md](349-waveshare-board-row.md) | Waveshare ESP32-S3-ETH board row: dedicated profile keyed on board_M5StampS3, power-off refusal, corrected simulator capabilities |
| [358-resilience-round3.md](358-resilience-round3.md) | Resiliency round three: the SD settings backup that never completed and destroyed the last good copy, the refused companion notification that stayed lost for the session, and the ranked findings left open |
| [358-review-feedback.md](358-review-feedback.md) | Review-feedback record for PR 516: the FatFs rename that refused the existing backup, the keepalive stamp that never advanced, the boundary case count and the peer double race |
| [354-core-family-fixes.md](354-core-family-fixes.md) | Core family hardware and power fixes: Tough SD/speaker/RTC, capacities, IP5306 keep-on, honest power off, GPS rail scope |
| [354-review-feedback.md](354-review-feedback.md) | Review-feedback record for the Core family hardware and power fixes |
| [363-pmic-irq-speaker.md](363-pmic-irq-speaker.md) | M5PM1 IRQ Status 2 latch holding the wake line, the RTC IRQ write on a board with no RTC, the StickS3 amplifier settle time and battery volume cap, and the refused watchdog arm log |
| [363-review-feedback.md](363-review-feedback.md) | Review-feedback record for the PMIC IRQ, RTC and speaker branch, including the platform-wake target that compiles the production call sites |
| [364-ext5v-rail.md](364-ext5v-rail.md) | StickS3 EXT_5V rail ownership: one writer for `PWR_CFG` 0x06, named holders with a reference count, the output-mode supply interlock and the IR hold around a burst |
| [364-review-feedback.md](364-review-feedback.md) | Review-feedback record for the EXT_5V rail ownership branch, PR #493 |
| [302-companion-restart-reconnect.md](302-companion-restart-reconnect.md) | Companion stop/start without a heap panic, and advertising restart after a rejected link |
| [337-sim-mem-shims.md](337-sim-mem-shims.md) | Simulator heap_caps calloc, realloc and get_info shims, and the PSRAM task-stack abort |
| [303-upstream-sync.md](implemented/303-upstream-sync.md) | Read-only upstream audit, open issue mapping, and full SHA PlatformIO pins for upstream issue 309 |

## Phase 4: sensors

| Doc | Content |
|---|---|
| [16-imu-spirit-level.md](implemented/16-imu-spirit-level.md) | Enable IMU, spirit level page |
| [17-imu-gestures.md](17-imu-gestures.md) | Tap and shake wake, double tap shutter |
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
| [25-multiconnect-ui.md](25-multiconnect-ui.md) | Multi-connect selection, Cameras status page |
| [26-pm1-watchdog.md](implemented/26-pm1-watchdog.md) | M5PM1 hardware watchdog, StickS3 lockup recovery |
| [27-usb-console.md](implemented/27-usb-console.md) | USB serial command console for testing |
| [76-reconnect-stuck.md](implemented/76-reconnect-stuck.md) | Fast reconnect to Fujifilm Secure stalls in connecting: diagnosis and fix, merged as #121 |
| [28-emulator.md](28-emulator.md) | Host SDL simulator for the furble UI |
| [29-virtual-test-rig.md](29-virtual-test-rig.md) | Sim to Android app test rig over TCP, no radio |
| [31-s3-psram.md](implemented/31-s3-psram.md) | Enable PSRAM on the M5StickS3 |
| [32-gps-advanced.md](implemented/32-gps-advanced.md) | Advanced GPS support for the GPS/BDS Unit v1.1 |
| [33-wifi-hub.md](33-wifi-hub.md) | WiFi hub: displayless build, provisioning, MQTT |
| [34-ota-partitions.md](34-ota-partitions.md) | OTA updates and the partition scheme change |
| [35-exposure-presets.md](implemented/35-exposure-presets.md) | Exposure time presets in 1/3 stops |
| [36-camera-test-harness.md](implemented/36-camera-test-harness.md) | Camera protocol test harness: host tests, mock NimBLE, virtual peer |
| [71-ui-bug-batch.md](implemented/71-ui-bug-batch.md) | UI focus recovery, left escape, connection progress, display-off LED policy |
| [76-ui-polish.md](implemented/76-ui-polish.md) | Focus ring visible in every theme, simulator theme and board selection for the screenshot matrix |

## Framework work

| Doc | Content |
|---|---|
| [30-m5stack-framework.md](30-m5stack-framework.md) | Upstream M5Stack library gaps, fork and PR strategy |
| [40-thinknode-port.md](40-thinknode-port.md) | ThinkNode port feasibility |
| [41-alternative-hardware.md](41-alternative-hardware.md) | Alternative sidecar hardware |
| [42-waveshare-eth-node.md](implemented/42-waveshare-eth-node.md) | Waveshare ESP32-S3-ETH wired MQTT node |
| [287-mcu-portability-and-bundle-hardware.md](287-mcu-portability-and-bundle-hardware.md) | Whole-stack MCU portability, battery model, GPS quality, and buyable BLE plus GNSS hardware |
| [322-hardware-abstraction.md](implemented/322-hardware-abstraction.md) | Hardware abstraction for the next boards: dependency audit, pure chip drivers, board profile and power backend |
| [322-review-feedback.md](implemented/322-review-feedback.md) | Review-feedback record for the plan 322 hardware abstraction branches |
| [333-datasheet-corpus.md](333-datasheet-corpus.md) | Datasheet citation corpus: one page per part, document revision and section per register fact, the citation gate, and the zero-PDF licence decision |
| [354-gnss-datasheet-verification.md](354-gnss-datasheet-verification.md) | GNSS datasheet verification: the AT6668 datasheet and both CASIC protocol revisions read against the pages, the M10 module documents opened, and the facts that stay open |
| [336-gps-pin-seam.md](336-gps-pin-seam.md) | GPS UART pin source in the board profile: M5Unified lookup behind `Board::gpsPins()`, no-GPS rows for the Core and the Waveshare ESP32-S3-ETH, M5 table evidence, first step of the plan 322 port sequence |
| [336-review-feedback.md](336-review-feedback.md) | Review-feedback record for PR #510: the Waveshare doc that still advertised a GPS port, the moved pin-lookup citations, the rotated doc citations, and the regression, stickiness, `isEnabled()` and GPIO13 checks |
| [473-review-feedback.md](473-review-feedback.md) | Review-feedback record for the #473 hardware-trap corrections and the per-panel display dwell rule |
| [350-stickc-family-fixes.md](350-stickc-family-fixes.md) | StickC family fixes: shared LED/IR pin ownership, LED and IR polarity, Plus2 RTC backing, AXP192 ADC mask reason, GPIO35 third source, StickC Plus timed wake, GPS rail already on |
| [350-review-feedback.md](350-review-feedback.md) | Review-feedback record for PR #505: the arbiter build gate, the unrecorded-level fallback, the boot-time EXTEN writes, and the polarity and RTC questions this review closed |

## Next hardware

Research and design from the 2026-11-13 hardware survey. None of it is
implemented. Plan 360 is the later addition: it was read from the slgray fork
and the Waveshare documents on 2026-11-26. Plan 364 is the port-cost ranking
added the same day; it defers to 318 on the T-Beam trim, to 360 on the first
AMOLED port, and to 362 on nRF. Plan 366 is the receiver decision from the same
reading: it ranks the levers, compares seven receivers, and defers to 318 for
the bench work.

| Doc | Content |
|---|---|
| [318-next-board-battery-gps.md](318-next-board-battery-gps.md) | Next board for battery life and GPS: 32.768 kHz crystal finding, T-Beam Supreme M10S first, UBX driver design and bench plan |
| [319-meshtastic-dual-boot.md](319-meshtastic-dual-boot.md) | Design only: furble and Meshtastic on one ESP32-S3 node, slot ownership, private NVS and switching |
| [320-rtc-use.md](320-rtc-use.md) | RTC domain use: wall clock across soft resets, timed-wake time restore, drift calibration, rejected ULP ideas |
| [324-hw-research-review-feedback.md](implemented/324-hw-research-review-feedback.md) | Review-feedback record for the hardware research branch |
| [335-display-touch-datasheet-promotions.md](335-display-touch-datasheet-promotions.md) | Display and touch datasheet promotions: what was re-read on 2026-11-26, the page each number came from, the licence limits, and the items that stay unverified |
| [360-waveshare-amoled-1-8-port.md](360-waveshare-amoled-1-8-port.md) | Port plan for the slgray AMOLED-1.8 board: parts with their documents, pin collisions, AXP2101 rails and their boot state, whether M5GFX can drive the CO5300, the profile row, and the simulator and bench work |
| [364-hardware-candidates.md](364-hardware-candidates.md) | Ranked off-the-shelf port candidates by new reusable capability per driver or profile unit: T-Beam Supreme, CoreS3 and Waveshare 1.75-G on top, the skip list, and the facts no vendor page settles |
| [366-gps-receiver-selection.md](366-gps-receiver-selection.md) | GNSS receiver decision: the lever ordering with its magnitudes, the flat -146 to -148 dBm cold-start result, seven receivers with bands, constellations, sensitivity, TTFF, update rate, current, backup and driver, three priced options, the antenna against receiver rule and its five exceptions, and three catalog corrections |

## Bug analyses

| Doc | Content |
|---|---|
| [75-false-connected.md](implemented/75-false-connected.md) | False Connected when the camera is not in pairing mode. Promotion to ACTIVE is gated on GATT plumbing, not on the camera-side registration confirmation |
| [80-camera-lifetime.md](implemented/80-camera-lifetime.md) | Use after free: CameraList owns Cameras by unique_ptr and load()/clear() free them while Control holds raw pointers across an in-flight connect. Own Cameras by shared_ptr so a connecting or active Camera outlives the list |
| [352-m5gfx-board-pin.md](352-m5gfx-board-pin.md) | M5GFX board pinning: the Waveshare pin named a board with no `autodetect` arm, so the retry fell back to `board_unknown` and M5Unified probed GPIO 4, 8, 10, 12 and 38; the no-probe patch returns the display-less family before the first probe, with a build-time check |
| [352-review-feedback.md](352-review-feedback.md) | Review-feedback record for PR 499: the gate revert, the two-patch upgrade counterfactual, the `FURBLE_HAS_INTERNAL_I2C` board audit and three gate blind spots |

## Simulator stability

| Doc | Content |
|---|---|
| [122-gps-sim-stability.md](implemented/122-gps-sim-stability.md) | Serialize TinyGPSPlus ownership across the GPS task, LVGL timers, and console snapshots; exercise concurrent GPS pages in the host simulator |
| [123-host-task-lifecycle.md](implemented/123-host-task-lifecycle.md) | Join real-Control harness tasks before host static destruction |
| [126-settings-handle-concurrency.md](implemented/126-settings-handle-concurrency.md) | Use operation-local Preferences handles so concurrent settings calls cannot replace or close each other's NVS handle |
| [301-sim-ui-buttons.md](implemented/301-sim-ui-buttons.md) | Interactive on-screen A/B/C or A/B/PWR buttons in a top-view control window that drives the same emulated GPIOs as the keyboard, without covering the panel or its overlays |
| [334-sim-hardware-alignment.md](334-sim-hardware-alignment.md) | Simulator and production alignment with verified vendor rules: W5500 RESETn 500 us, the IP5306 64 s cutoff, and the panel dwell the simulator cannot model |
| [334-review-feedback.md](334-review-feedback.md) | Review-feedback record for PR 482: the README and L01 cutoff values, the opened W5500 datasheet revision, and the boundary test hardening |
| [348-sticks3-sim-align.md](348-sticks3-sim-align.md) | Align the StickS3 simulator model with the verified board facts: RTC absence, the shared EXT 5 V output, the one PWR_CFG byte, the PMIC IRQ latches and a fuller M5PM1 shim |
| [348-review-feedback.md](348-review-feedback.md) | Review-feedback record for PR 502: the PWR_CFG reset default against the M5PM1 manual, the RTC rows against the profile table and vendor pages, and the host and scenario revert proofs |

## Tooling and web installer

| Doc | Content |
|---|---|
| [68-flasher-debug-firmware.md](implemented/68-flasher-debug-firmware.md) | Web flasher debug firmware option: build *-debug envs, debug manifests, install checkbox |
| [139-persistent-time.md](implemented/139-persistent-time.md) | RTC, NVS, GPS, NTP, and companion wall-clock retention policy |
| [139-review-feedback.md](implemented/139-review-feedback.md) | Review-feedback record for the wall clock across soft resets |
| [69-flasher-bt-dump.md](implemented/69-flasher-bt-dump.md) | Web Serial Capture BT debug dump panel, depends on 68 and PR #76 |
| [113-per-board-ota-slots.md](implemented/113-per-board-ota-slots.md) | Per-flash-size OTA slots, implemented by PR #167 |
| [114-provision-parser.md](implemented/114-provision-parser.md) | One-shot provisioning parser and console apply; staged browser transport deferred |
| [114-flasher-provisioning.md](114-flasher-provisioning.md) | Follow-up browser transport for the landed provisioning parser |
| [115-ota-engine.md](implemented/115-ota-engine.md) | Transport-independent OTA lifecycle, implemented by PR #168 |
| [115-ota-update-mqtt.md](115-ota-update-mqtt.md) | HTTPS delivery and OTA-over-MQTT follow-up |
| [130-ota-mqtt-contract.md](implemented/130-ota-mqtt-contract.md) | Signed, replay-safe OTA-over-MQTT envelope and chunk policy |
| [131-ota-replay-store.md](implemented/131-ota-replay-store.md) | Durable two-slot anti-rollback replay journal |
| [132-ota-partition-sink.md](implemented/132-ota-partition-sink.md) | Ordered, digest-verified inactive-partition sink |
| [177-mqtt-4mb-size-followup.md](implemented/177-mqtt-4mb-size-followup.md) | MQTT size follow-up and developer-only single-factory USB fallback |
| [124-sim-incremental-deps.md](implemented/124-sim-incremental-deps.md) | Compiler depfiles for reliable direct-simulator incremental builds |
| [127-dev-version-identity.md](implemented/127-dev-version-identity.md) | Append an unambiguous short Git revision to development firmware versions while preserving release tags |
| [142-bt-journal-memory.md](implemented/142-bt-journal-memory.md) | Compact, capability-aware Bluetooth journal storage and loss accounting |
| [359-sony-legacy-generation.md](359-sony-legacy-generation.md) | Sony 2019 generation (upstream issue 316): legacy advertisement accepted, a verified 158-byte ATT MTU floor with an honest location skip, and a best-effort DD11 write gated on the discovered DD00 profile |
| [359-review-feedback.md](359-review-feedback.md) | Review-feedback record for PR 508: the missing directory guides, the flash delta and its external blocker, the saved-record behaviour change, the retryable-failure latch and the mock client's charged size, plus the four pre-rebase red checks and whether they were real |
| [345-doc-citation-gate.md](345-doc-citation-gate.md) | Gate that checks `file:line` citations in every Markdown file: the path exists, the line is in range, and an annotated symbol is on that line or inside its definition. Fixes the four line drift #473 left in plans 338, 303 and the CLAUDE docs |
| [344-android-ui-snapshot.md](344-android-ui-snapshot.md) | Robolectric UI snapshot test for the Android companion main screen: pinned rendering inputs, committed baseline, runs inside `./gradlew test` |
| [511-review-feedback.md](511-review-feedback.md) | Review-feedback record for PR 511: the re-derived 86 finding count at the merge base, the gate's real detection envelope, the symbol-rule sample and twelve churn spot checks, plus the five stale citations reading found in the files the PR touched |
| [344-review-feedback.md](344-review-feedback.md) | Review-feedback record for PR 483: the gate's real detection envelope, node assertions, locale restore, exported test activity, resolved Compose UI test versions, uncacheable regeneration and the platform jar download |
| [474-companion-coverage.md](474-companion-coverage.md) | Companion coverage audit: the layer matrix both apps are documented against, five more Android baselines, visible snapshot results in CI, the golden-corpus contract test and the trigger fixture defect it found |
| [347-core-debug-flash-headroom.md](347-core-debug-flash-headroom.md) | Core dual-OTA debug headroom: keep `ESP_LOG_VERBOSE` on the lane by compiling out the 8 KiB BMI270 init blob the Core can never probe, the image-wide `ESP_LOG_INFO` cap #518 shipped and why it was reverted, and the measured candidates that recovered nothing |
| [347-review-feedback.md](347-review-feedback.md) | Review-feedback record for PR 524: the DEBUG and VERBOSE strings are back in both Core debug ELFs and absent on master, the BMI270 blob and probe are present on StickS3 and Core2 and gone on the Core family by `nm`, a 9,120-byte guard and 9,152 bytes of margin on the constrained lane, and six fixed defects in the plan, `docs/power/soc.md` and the crash-diagnostics scope paragraph |
| [475-review-feedback.md](475-review-feedback.md) | Review-feedback record for PR 494: the full-queue bypass, the unbounded automatic retry, the two false user-visible claims, the citation drift, the boundary count and the reachable board row |

## Network, companion, and simulator follow-ups

| Doc | Content |
|---|---|
| [117-sim-mqtt-coverage.md](implemented/117-sim-mqtt-coverage.md) | Host MQTT client and Home Assistant discovery coverage |
| [118-sim-ethernet-coverage.md](implemented/118-sim-ethernet-coverage.md) | Host Ethernet netif coverage for the transport-neutral MQTT seam |
| [119-companion-gatt-sim.md](implemented/119-companion-gatt-sim.md) | Companion GATT host coverage, implemented by the companion harness |
| [121-sim-usage-fault-docs.md](implemented/121-sim-usage-fault-docs.md) | Simulator usage and fault-injection reference, implemented |
| [357-review-feedback.md](357-review-feedback.md) | Retrospective review of seven merged pull requests (#471, #472, #478, #492, #495, #503, #506, #517), with what held, what was wrong and the follow-up fixes |
| [355-sim-cmake-entry-point.md](355-sim-cmake-entry-point.md) | The CMake simulator entry point's macOS abort: the SDL3-before-SDL2 link order behind it, the #506 fix, the two differences that remain against sim/build.sh, and the missing smoke step |
| [355-review-feedback.md](355-review-feedback.md) | Review-feedback record for PR 517: the CMake build and three stock scenarios, the `nm -m` binding, and the reverted link order that returns exit 134 |
| [343-apple-ui-snapshot.md](343-apple-ui-snapshot.md) | First Apple UI snapshot: the shipped Status section body compared to a committed 1x baseline in the existing `swift test` target |
| [343-review-feedback.md](343-review-feedback.md) | Review-feedback record for PR 480: baseline provenance on the `macos-15` image, the comparison the CI job reports, and the List row-flattening test |

## Optimization program

| Doc | Content |
|---|---|
| [125-esp-optimization-program.md](125-esp-optimization-program.md) | Measurement-first ESP-IDF, radio, power, memory, UI, dependency, security, and release optimization program |
| [127-nordic-port-baseline.md](127-nordic-port-baseline.md) | Simulator-gated portability boundary and Nordic silicon port baseline |
| [127-review-feedback.md](127-review-feedback.md) | Review-feedback record for the plan 127 port stack |
| [362-nrf-ble-only-port.md](362-nrf-ble-only-port.md) | nRF port for a BLE-only variant: the measured portability boundary, the nRF board and toolchain survey, the flash and RAM arithmetic, and a documented no until the five seam extraction lands |
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
| [155-sim-ui-liveness.md](implemented/155-sim-ui-liveness.md) | Continuous sim liveness invariant and false-connected coverage (link_lies later replaced by link-death-liveness) |
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
| [167-fujifilm-device-name.md](167-fujifilm-device-name.md) | Fujifilm Secure cameras show the advertised model plus the advertised serial, since the longer camera-menu name is never advertised |
| [168-notouch-layout-overflows.md](168-notouch-layout-overflows.md) | Close every overflow and indicator collision the physical-button layout ships, promote the recorded gaps to hard assertions, and run the certified set in that layout in CI |
| [169-flaky-host-tests.md](implemented/169-flaky-host-tests.md) | Three flaky host tests made deterministic: real scheduler waits instead of spin budgets, a registration sync point instead of sleeps, the aborted-connect republish wedge, and a coverage run that fails on a scenario that never finished |
| [171-console-coverage-crash.md](implemented/171-console-coverage-crash.md) | The console suite exits while the control task is still running: stop and join every shim task before static destruction, and a coverage run that names a host test it lost |
| [173-sim-scheduler-visibility.md](173-sim-scheduler-visibility.md) | Scheduler-visible host mutex so virtual time stops tracking host load and the cancel bounds come back, one preferences store per simulated device, and a fatal-fault reporter that names the scenario line |
| [173-review-feedback.md](173-review-feedback.md) | Review-feedback record for the #283 environment guard and #289 load proof branch |
| [174-coverage-empty-profiles.md](implemented/174-coverage-empty-profiles.md) | Two host suites that measured nothing under coverage: the control shim adopts the stop-and-join task contract so both exit through main(), a coverage run fails naming a test whose raw profile is empty or missing, the console shim refuses a task created after shutdown, and the ctest summary header is anchored so a failing test cannot fabricate a crash report |
| [175-coverage-crash-output.md](implemented/175-coverage-crash-output.md) | Failed simulator coverage scenarios retain a bounded output tail so signal diagnostics remain visible in CI |
| [176-sim-task-notifications.md](implemented/176-sim-task-notifications.md) | FreeRTOS task notification counters in the simulator wake blocked owners, preserve clear/decrement and virtual timeout semantics, and unwind on shutdown |
| [285-power-accounting.md](implemented/285-power-accounting.md) | Issue #285: opt-in synthetic power accounting and frozen comparator identity |
| [288-provisioning-schema.md](implemented/288-provisioning-schema.md) | Provisioning schema coverage for every keyed setting and current wire-ID reservation ledger |
| [291-ui-upstream-layout-parity.md](implemented/291-ui-upstream-layout-parity.md) | About, GPS, and Connected content parity with upstream; the rows were kept and made scrollable (`d164657a`) |
| [297-gps-improvement-roadmap.md](297-gps-improvement-roadmap.md) | GPS power/accuracy findings, the implemented fixes, and the ranked remaining GPS work with bench gates |
| [298-pre-review-gate.md](implemented/298-pre-review-gate.md) | Pre-review scanners for diff hygiene, settings contracts, C++ safety, and a sanitized host subset, wired into pull-request CI |
| [178-sim-startup-settings-order.md](implemented/178-sim-startup-settings-order.md) | Apply simulator settings before platform construction while preserving panel, environment, watchdog, and profiler boundaries |
| [338-e2e-page-coverage.md](338-e2e-page-coverage.md) | End-to-end scenarios for the 23 UI pages the certified suite never asserted |

## Additional plan documents

These documents exist on master but were not in the original index. Some share
a number because existing plans are never renumbered; they are distinct files.
They are listed here so no plan is orphaned. Implementation state inside each
document is authoritative over any older summary.

| Doc | Content |
| --- | --- |
| [67-fujifilm-dial-detection.md](67-fujifilm-dial-detection.md) | Fujifilm drive dial detection |
| [73-pentax-k.md](implemented/73-pentax-k.md) | Pentax K through the Ricoh Imaging BLE family |
| [74-dji-osmo.md](74-dji-osmo.md) | DJI Osmo Action BLE remote support |
| [77-battery-saver-profile.md](implemented/77-battery-saver-profile.md) | Battery Saver profile |
| [82-disconnect-liveness.md](implemented/82-disconnect-liveness.md) | Disconnect liveness |
| [83-lifecycle-tests.md](implemented/83-lifecycle-tests.md) | Host regression tests for the connect and disconnect lifecycle |
| [84-unit-coverage.md](implemented/84-unit-coverage.md) | Broader host unit test coverage |
| [85-sim-e2e.md](implemented/85-sim-e2e.md) | Simulator end-to-end scenarios |
| [86-sim-bug-hunt.md](86-sim-bug-hunt.md) | SDL simulator bug hunt |
| [87-sim-buttons.md](implemented/87-sim-buttons.md) | Per-model physical buttons in the SDL sim |
| [88-alternate-hardware.md](88-alternate-hardware.md) | Alternate hardware and Meshtastic coexistence |
| [89-reconnect-ui.md](implemented/89-reconnect-ui.md) | Reconnect UI indicator |
| [92-ui-icons.md](implemented/92-ui-icons.md) | Menu icon set |
| [97-followups.md](97-followups.md) | Consolidated follow-up backlog |
| [98-power-optimization-audit.md](98-power-optimization-audit.md) | Power optimization audit |
| [98b-power-followup.md](implemented/98b-power-followup.md) | GPS degraded lock recovery follow-up |
| [99-boot-time.md](implemented/99-boot-time.md) | Boot timeline instrumentation and measured boot-time reductions |
| [99-nimble-client-leak.md](implemented/99-nimble-client-leak.md) | NimBLE client leak fix |
| [100-sim-fault-injection-bughunt.md](implemented/100-sim-fault-injection-bughunt.md) | Host fault injection and BLE connect failure hunting |
| [102-disconnect-freeze.md](implemented/102-disconnect-freeze.md) | Disconnect off the UI task |
| [103-real-control-e2e-harness.md](implemented/103-real-control-e2e-harness.md) | Real Control connect/disconnect end-to-end harness |
| [104-ble-fuzzing.md](implemented/104-ble-fuzzing.md) | Seeded BLE lifecycle fuzzing |
| [104-focus-outline-dedup.md](implemented/104-focus-outline-dedup.md) | Single focus ring per selected item |
| [105-ui-fuzzing.md](implemented/105-ui-fuzzing.md) | Seeded UI fuzzer |
| [106-flash-size-optimization.md](106-flash-size-optimization.md) | Flash size optimization |
| [106-review-feedback.md](106-review-feedback.md) | Review-feedback record for the Waveshare autodetect pin and group A alignment |
| [107-ui-layout-audit.md](implemented/107-ui-layout-audit.md) | Walkthrough screenshot layout audit |
| [108-upstream-issue-mapping.md](108-upstream-issue-mapping.md) | Historical upstream issue mapping snapshot |
| [109-sim-coverage-expand.md](implemented/109-sim-coverage-expand.md) | Simulator coverage expansion |
| [110-startup-render-watchdog.md](implemented/110-startup-render-watchdog.md) | Startup render task-watchdog stall |
| [111-power-recommendations.md](111-power-recommendations.md) | Power optimization recommendations |
| [111-review-feedback.md](111-review-feedback.md) | Review-feedback record for the plan 111 idle power fixes |
| [112-connect-crash-mid-drop.md](implemented/112-connect-crash-mid-drop.md) | Connect crash on a mid-connect peer drop |
| [113-connection-state-sim-coverage.md](implemented/113-connection-state-sim-coverage.md) | Connection-state simulator coverage |
| [118-sim-mqtt-transport.md](implemented/118-sim-mqtt-transport.md) | Optional simulator MQTT transport |
| [126-esp-optimization-audit.md](126-esp-optimization-audit.md) | ESP-IDF optimization and reproducibility audit |
| [127-apple-companion.md](implemented/127-apple-companion.md) | Apple companion foundation |
| [129-m5-dependencies-idf55.md](implemented/129-m5-dependencies-idf55.md) | M5 dependency compatibility slice |
| [130-espressif32-613-idf553.md](implemented/130-espressif32-613-idf553.md) | Espressif32 6.13.0 and ESP-IDF 5.5.3 |
| [141-ci-manual-dispatch.md](implemented/141-ci-manual-dispatch.md) | Reliable manual firmware CI |
| [346-plans-cleanup.md](346-plans-cleanup.md) | Plans directory cleanup: archived plans, corrected status lines, repointed references and the documents left alone with the reason |
| [346-review-feedback.md](346-review-feedback.md) | Review-feedback record for PR 484: the dead links master added after the base, the count of documents left alone, the duplicate plan number and the plan 167 justification |

## Design documents

| Doc | Content |
|---|---|
| [50-companion-app-design.md](50-companion-app-design.md) | Smartphone companion app and GATT service |
| [51-app-feature-parity.md](51-app-feature-parity.md) | Companion app parity: settings editors, camera management |
| [60-issue-257.md](implemented/60-issue-257.md) | Nikon Z50II smart device analysis, upstream issue 257 |
| [61-camera-compatibility.md](61-camera-compatibility.md) | BLE protocol survey across camera vendors, ranked additions |
| [62-issue-140.md](implemented/62-issue-140.md) | Configurable button behavior, upstream issue 140 |
| [63-sim-power-analysis.md](63-sim-power-analysis.md) | Simulator based power analysis: profiler, sleep estimator, usage test suite, energy model and CI gate; the sticky PR report is not built |
| [65-bt-coexistence.md](65-bt-coexistence.md) | Classic BT feasibility, WiFi+BLE coexistence for hub mode |
| [64-debug-tooling.md](64-debug-tooling.md) | Expanded debug tooling: power stats, performance monitoring, BT debug console |
| [67-bulb-ux.md](implemented/67-bulb-ux.md) | Bulb completion state, restart action, and camera mode hint |
| [66-pairing-code-display.md](implemented/66-pairing-code-display.md) | Camera pairing code on the furble display and console |
| [70-text-scaling.md](implemented/70-text-scaling.md) | UI text size setting per board, layout audit walker |
| [72-lumix.md](implemented/72-lumix.md) | Panasonic Lumix BLE support port, untested |
| [90-scheduled-shooting.md](90-scheduled-shooting.md) | Deferred: scheduled shooting via RTC alarm |
| [91-mic-trigger.md](91-mic-trigger.md) | Deferred: sound triggered shutter |

## Wire ids

`src/FurbleSettings.cpp` is the source of truth for shipped ids, and every
nonzero id has a row in `SETTING_SCHEMAS`. `tests/host/settings_table_test.cpp`
checks the ledger against the source and rejects duplicate owners;
`tests/host/provision_apply_test.cpp` walks every keyed setting and fails on a
missing schema row. The executable reservation table lives in
[include/CLAUDE.md](../include/CLAUDE.md).

Current master (`e66e8807`, 2026-11-13) uses 1-22, 24-35, 37-44, 46-47,
51-55, 65-69, and 72-76. Conditional ids are 23 (`WATCHDOG` on
`FURBLE_M5STICKS3`), 36 (`DISPLAY_MODE` without `FURBLE_NO_DISPLAY`), 56-61
(`FURBLE_MQTT`) and 62 (`WEB_UI` on `FURBLE_WEBUI`). Ids 48-50, 63-64, and
70-71 are free. Id 45 retains a historical claim and needs a compatibility
audit before reuse. Id 42 is merged as `TIMEZONE`. Off-wire id 0 remains used
by `BULB`, `TOUCH_CALIBRATION`, `MULTISELECT`, `GPX_PERIOD`, and
`BATTERY_SAVER`.

No pull request claims a setting id. PRs #317, #318 and #319 merged on
2026-11-02 without one. Recheck every open feature PR head
immediately before a rebase or merge; ids only freeze when a PR merges, and a
shipped id such as 46 (`IMU`) never moves.

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
