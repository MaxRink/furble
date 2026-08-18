# 97: Consolidated follow-up backlog

Status: docs-only. This is the single deduplicated backlog for the whole
engineering program. It consolidates every outstanding follow-up from the
radio-hardening audit, the plan gap-analysis deferrals, the review-flagged
fixes still owed, and the in-tree code markers. Nothing here is a firmware
change. Each item carries a one-line description, a source reference, a
severity, and a readiness tag.

## Motivation

The program now spans dozens of merged and in-flight PRs across BLE, WiFi, the
companion service, GPS, power, and tooling. Follow-up work is scattered across
plan 96 (the radio audit), the numbered feature plans, PR review threads, and
code comments. This document pulls all of it into one prioritized list so that
no confirmed defect ships unreviewed and no deferred sub-feature is forgotten.
It also feeds the release notes and future upstream contributions through the
"Bugs also present upstream" section.

## How to read the tags

- Severity: critical, high, medium, low. Matches plan 96 where the item came
  from there.
- READY-NOW: can be started against fork/master today, no merge dependency.
- GATED-ON: needs a specific branch or PR to land first, named inline.
- Source: the plan, PR, file, or review thread the item came from.

Plan 96 (`plans/96-wifi-bt-ble-hardening.md`) is the primary source for the
BLE and WiFi items. It is analysis-complete but NOT yet merged to fork/master.
It currently lives on branch `fork/docs/96-wifi-bt-ble-hardening`. Item IDs
like A1, B2, C4 below refer to its issue inventory. Landing plan 96 as a docs
PR is itself a prerequisite for the batches it sequences.

## Group 1: BLE and WiFi hardening

Source for all items unless noted: plan 96. Plan 96 sequences these into seven
remediation batches. The batch is named in each GATED-ON tag.

### Batch 1: connect and disconnect lifecycle (do first, brick-class)

- Camera::m_Mutex split plus lock-free isConnected. High. Fixes the
  Control-to-Camera host-task deadlock (A3/B1). Make m_Connected a
  std::atomic<bool>, drop the client double-check, serve companion status from
  a cached snapshot. Source: plan 96 B1, `lib/furble/Camera.cpp:760-767`,
  `src/FurbleCompanionService.cpp`. READY-NOW.
- Non-droppable CMD_DISCONNECT plus bounded disconnect. High. Send disconnect
  with a timeout or queue-reset-then-send-to-front so a full queue cannot drop
  it and spin disconnect() forever on the UI task. Source: plan 96 B2,
  `src/FurbleControl.cpp:53-58, 360-364`. READY-NOW.
- Force-complete on disconnect timeout. High. On timeout mark targets stopped,
  clear m_Targets under the mutex, setState(IDLE), log loudly. Never leave
  STATE_DISCONNECTING without an exit edge. Source: plan 96 B3 (C1/C2 pairing
  in the task brief), `src/FurbleControl.cpp:290-291`. GATED-ON: fix inside
  feat/68-reconnect-restart (#62) before it merges, or immediate follow-up.
- Destroy targets outside the Control mutex. Medium. Swap the vector out under
  the lock, run the blocking BLE terminations outside it. Source: plan 96 B4,
  `src/FurbleControl.cpp:42-47, 371-372`. READY-NOW.
- Cancel and disconnect checks in waitForRegistration (#93). High. Poll
  m_Connected plus a cancel flag each slice, return false fast instead of
  spinning up to 25 s under Camera::m_Mutex. Source: plan 96 A3a. GATED-ON:
  fix inside feat/75-false-connected (#93) before merge.

### Batch 2: Fujifilm registration and bonds

- FujifilmBasic null-deref hardening. High. Make shutter service and
  characteristic discovery hard failures, return false on any null. Today a
  null service is dereferenced (crash) and a null shutter still returns
  connected. Source: plan 96 A1, `lib/furble/FujifilmBasic.cpp:151-168`.
  READY-NOW. Also present upstream, see the upstream section.
- m_Configured as std::atomic<bool>. Low. Replace the volatile bool that
  crosses tasks. Source: plan 96 A3d. GATED-ON: feat/75-false-connected (#93).
- Stale-bond delete-and-retry. High. On secureConnection failure for a bonded
  address, delete the bond and retry once. Nothing recovers a stale bond
  today, so a camera-side pairing delete means permanent reconnect failure.
  Source: plan 96 A2. READY-NOW.
- FujifilmSecure address update from rescan. Medium. Mirror Nikon: update
  m_Address from the advertisement before connecting, re-serialize on success.
  Source: plan 96 A7a, `lib/furble/FujifilmSecure.cpp:69-88, 123-124`.
  READY-NOW.
- Decide the Basic registration-gate scope. Medium, suspected. Verify a Basic
  body notifies on hardware, or scope the mandatory gate to Secure and
  log-only on Basic until verified. Source: plan 96 A3b. GATED-ON:
  feat/75-false-connected (#93) plus hardware.

### Batch 3: Control concurrency hygiene (code-only)

- Lock the three unlocked m_Targets iterations. Medium. updateGPS, allConnected
  plus the ACTIVE fan-out, and the disconnect wait loop all iterate m_Targets
  from different tasks while mutations happen under the mutex. Source: plan 96
  B5, `src/FurbleControl.cpp:272-317, 360-364`. READY-NOW. Also present
  upstream, see the upstream section.
- Atomics for m_State and m_ConnectCamera, clear on failure, failcount reset.
  Low now, high once #63 lands. m_ConnectCamera is written unlocked and left
  dangling on failed connects. Source: plan 96 B7, `src/FurbleControl.cpp:182,
  187, 397-399`. READY-NOW, coordinate with feat/66-pairing-code (#63).
- Synthesized shutter release on leaving STATE_ACTIVE. Medium. Track a held
  flag in Control and release still-connected targets when one camera drops,
  so a multi-connect reconnect does not strand a held shutter. Source: plan 96
  B6 (C4 in the task brief). READY-NOW.
- setConnSaver fan-out moved off the LVGL task. High leverage. Move the up-to
  18 s HCI fan-out to the control task via a command; document the watchdog
  feeder decision. Source: plan 96 B9, `src/FurbleControl.cpp:640-645`.
  READY-NOW.
- Single cancel-callback registration. Low. Register the cancel event callback
  once at UI build time instead of per connect. Source: plan 96 B10,
  `src/FurbleUI.cpp:1952-1953`. READY-NOW.
- Small library correctness. Low. gattRead returns true on failed reads;
  FujifilmSecure scan-end race loses a success; Scan::onResult unlocked
  check-then-invoke. Source: plan 96 A8, `lib/furble/Camera.cpp:530-546`,
  `lib/furble/Scan.cpp:44-93`. READY-NOW.

### Batch 4: pairing UX and vendor gates

- Passkey-entry UI or loud failure, PAIRING_WINDOW 30 s, doc the fixed display
  passkey. Medium. onPassKeyEntry still injects 123456, so a random-code camera
  fails silently; the 2 minute window outlives the ~30 s SMP timeout. Source:
  plan 96 A5a/A5b/A5c. GATED-ON: feat/66-pairing-code (#63).
- answerPairing client use-after-free window. Medium, suspected. Do the inject
  and disconnect via a handle-based call or hold m_PairingMutex across the use.
  Source: plan 96 A6. GATED-ON: feat/66-pairing-code (#63).
- PR #38 multi-connect row identity plus the fork/master diagnostics-page fix.
  Medium. Rows should store camera identity and re-resolve each tick; pause the
  diagnostics timer with the reconnect box. Source: plan 96 B8,
  `src/FurbleUI.cpp:3758-3760`. GATED-ON: feat/25-multiconnect-ui (#38).
- Canon and Sony saved-reconnect liveness promotion. Medium, structural gap.
  Add a cheap vendor liveness probe for saved reconnects and switch Canon
  identity writes to write-with-response. CanonEOSSmart skips its pairing gate
  when isBonded. Source: plan 96 A4, `lib/furble/CanonEOSRemote.cpp:64`,
  `lib/furble/Sony.cpp:34-84`, `lib/furble/CanonEOSSmart.cpp:42-44`. READY-NOW.
  Untested vendor, code review plus FauxNY only.
- RPA and address-rotation notes plus bond cleanup. Low to medium. Canon, Sony,
  Ricoh have no rescan (A7b); CameraList::remove leaks the real bond when the
  saved address is stale (A7c). Source: plan 96 A7b/A7c,
  `lib/furble/CameraList.cpp:135-137`. READY-NOW.

### Batch 5: wifi-hub restack and STA robustness

- Restack the wifi-family: rebase #66 and #90 onto #53. High, integration
  defect. 33c and 33d branch from 33-wifi-hub in parallel, carry zero
  esp_wifi bring-up, poll networkReady forever, and their sdkconfigs still
  hold pre-33b netif state across all six files. Rebase 33c onto 33b and 33d
  onto the result with a careful six-file sdkconfig reconciliation. Source:
  plan 96 C1. GATED-ON: this must precede any wifi-chain merge.
- Headless MQTT sdkconfig symbols. Medium. Add CONFIG_MQTT_PROTOCOL_311 and
  CONFIG_MQTT_TRANSPORT_SSL to sdkconfig.esp32-s3-headless; today they are in
  the five release configs only. Source: plan 96 C7. GATED-ON: wifi restack.
- WiFi retry-stall after a blocked scan. Medium. Notify the WiFi worker on the
  Control transition out of STATE_ACTIVE, or retry direct connects while
  blocked; today the worker parks on portMAX_DELAY. Source: plan 96 C2,
  `src/FurbleWiFi.cpp:312-318, 385-414`. GATED-ON: wifi restack.
- DHCP timeout and LOST_IP handling. Medium. Bounded post-associate wait
  treated as failure, register IP_EVENT_STA_LOST_IP. Source: plan 96 C3.
  GATED-ON: wifi restack.
- WiFi auth-fail classification and sys_evt task offload. Low. Distinguish a
  wrong PSK from AP loss; move NVS writes and SNTP init out of the 2304-byte
  sys_evt task. Source: plan 96 C10. GATED-ON: wifi restack.

### Batch 6: hub services hardening

- MQTT permanent-block backoff. Medium. Replace the give-up-after-10-failures
  behavior with capped exponential backoff that retries forever; keep the hard
  block only for identified auth failures. Source: plan 96 C5,
  `src/FurbleMQTT.cpp:490-501`. GATED-ON: wifi restack.
- WebUI CSRF and auth hardening. High within the LAN threat model. Require
  Content-Type application/json on POSTs, validate Host against the device
  address, add an optional static token. Source: plan 96 C4,
  `src/FurbleWebUI.cpp:421-452`. GATED-ON: wifi restack.
- Secrets-over-companion redaction. Medium. settingValue returns raw WIFI_PSK
  and MQTT_PASS over the BLE companion service; return a set/unset indicator
  and keep writes write-only, matching console and REST. Source: plan 96 C6,
  `src/FurbleCompanionService.cpp:316-326`. GATED-ON: wifi restack.
- CameraList access routing plus MQTT timer teardown guard. Low. Serialize
  CameraList loads from httpd and mqtt tasks against the GUI scan; guard
  esp_timer publishes into a client being destroyed. Source: plan 96 C10.
  GATED-ON: wifi restack.
- Hub heap headroom and env-gating. Medium, suspected. Run the min-free-heap
  soak on a non-PSRAM board before merge; consider gating the hub to S3 envs.
  Source: plan 96 C9, `src/main.cpp:291-292`. GATED-ON: wifi restack.

### Batch 7: coexistence and power validation

- Coex rec W2: BLE connection-interval floor while WiFi is up. Medium. Add a
  WiFi-aware conn profile at 51.25-60 ms avoiding multiples of 102.4 ms,
  selected while WiFi reports connected. Source: plan 96 C8 plus plan 65,
  `lib/furble/Camera.h:283-286`. GATED-ON: wifi restack.
- Coex rec W3: hub camera-count cap and doc. Medium. Document 3 cameras
  supported, 5 tested, in hub mode. Source: plan 96 C8. GATED-ON: wifi restack.
- Coex rec T1: adaptive TX floor while WiFi is up. Low. Floor the adaptive
  range one step higher, or suspend step-down, while WiFi is connected.
  Two-line condition in sampleAdaptivePower. Source: plan 96 D2. GATED-ON:
  wifi restack.
- Coex rec P1: RMT carrier on XTAL. Low, suspected. Use RMT_CLK_SRC_XTAL on S3
  for the IR carrier to survive DFS, matching the GPS UART pattern. Source:
  plan 96 D3, `src/FurbleIR.cpp:217`. READY-NOW.
- Adaptive TX wrong-direction RSSI caveat. Low to medium, design note.
  getRssi measures the camera TX, so the step-down loop is open; document in
  plan 11 and optionally step up on supervision-timeout disconnects. Source:
  plan 96 B12, `src/FurbleControl.cpp:552-583`. READY-NOW. Present upstream in
  latent form, see the upstream section.
- Battery brownout soak with WiFi TX. Low to medium, suspected. Run the plan
  65 battery soak; hold esp_wifi_set_max_tx_power(44) in reserve for Stick
  boards. Source: plan 96 D1. GATED-ON: wifi restack.
- PM1 watchdog liveness. High leverage, structural. The watchdog is fed only
  from UI::task, so any long UI block becomes a hard reset. Consider a small
  dedicated feeder that feeds only while a UI heartbeat advances. Source: plan
  96 B9, `src/FurblePlatform.cpp:341-348`. READY-NOW. Fork-only mechanism, see
  the upstream section.

## Group 2: deferred features

Deferred sub-features from the plan gap analysis. Each is a scoped feature, not
a defect. Severity is enhancement unless noted.

- 17 StickS3 deep-wake. IMU gesture wake from deep sleep on the S3. Source:
  plan 17. READY-NOW.
- 22 IR intervalometer. Interval trigger over the IR path. Source: plan 22.
  GATED-ON: IR trigger base (plan 22 core) landing.
- 32 MON-HW telemetry. Hardware monitoring telemetry surface. Source: plan 32.
  READY-NOW.
- 50 OTA-to-app. Firmware update driven from the companion app. Source: plan
  50. GATED-ON: companion GATT service (feat/50 chain).
- 51 remote-disconnect. App-initiated disconnect over the companion service.
  Source: plan 51. GATED-ON: companion service (feat/51 chain).
- 71 level quick-wins. Remaining spirit-level UI quick-wins. Source: plan 71
  (`plans/71-ui-bug-batch.md`). READY-NOW.
- 73 Ricoh geotag. Geotag push for Ricoh bodies. Source: plan 73. READY-NOW.
  Untested vendor, code review plus FauxNY.
- 29 rig phases 4, 5, 6. Later multi-device rig phases. Source: plan 29.
  GATED-ON: earlier rig phases.
- 33d done. WebUI base feature is complete; no further sub-feature. Tracked
  here only so it is not re-opened. Source: plan 33d. No action.

## Group 3: tooling and CI

- esp32-s3-headless in CI. Medium. Add the headless env to the CI build matrix
  so the hub target is built and size-tracked. Source: task brief, plan 33
  chain. GATED-ON: wifi restack (headless sdkconfig must be consistent first,
  see C7).
- Power-optimized -lowpower build profile. Enhancement. Add a low-power build
  profile per plan 77. Source: plan 77 (not yet written; note as pending).
  READY-NOW.
- #41 sd-gpx sim shim. Low. Add a simulator shim for the SD GPX logging path
  so plan 24 logic runs under the SDL sim. Source: review of feat/24,
  plan 41 area. READY-NOW.

## Group 4: vendor

- Canon and Sony saved-reconnect liveness. See Group 1 batch 4 A4. Untested
  vendors, declared per repo policy. READY-NOW.
- Ricoh geotag (plan 73). See Group 2. Untested vendor. READY-NOW.
- Vendor URL citations. Done. Kept here only to record closure. Source: review
  thread. No action.

## Group 5: review-flagged fixes still owed

- #35 low-battery warning never cleared on recovery. Low. m_LowBatteryWarned
  (or the equivalent auto-off latch) is set on the low-battery path and never
  reset when the battery recovers, so the warning latches for the session.
  Source: review of PR #35 / plan 13 auto-off-low-batt. READY-NOW. Note: the
  named flag was not located by string search on fork/master; confirm the exact
  identifier in the auto-off path (`src/FurbleUI.cpp` / `src/FurblePower.cpp`)
  when fixing.
- #41 sd-gpx sim shim. See Group 3. READY-NOW.

## Group 6: in-tree code markers

Scan of src, include, lib on fork/master.

- FurbleUI.cpp:1550 hardcoded-values cleanup. Low, cosmetic. `@todo Clean up
  the plethora of hardcoded values here`. Source: `src/FurbleUI.cpp:1550`.
  READY-NOW.
- FurbleUI.cpp:1686 cancel-button clipping. Low. `@todo cancel button bottom is
  clipped, weird`. Source: `src/FurbleUI.cpp:1686`. READY-NOW.
- FujifilmBasic.cpp:116 disabled registration wait. Informational. The old
  `#if 0` registration-wait block is the historical false-connected root that
  plan 75 and plan 96 A1 address. Source: `lib/furble/FujifilmBasic.cpp:116`.
  Covered by Group 1 batch 2. Present upstream, see the upstream section.

No other TODO, FIXME, XXX, "not implemented", or `#if 0` markers were found in
src, include, or lib.

## Priority summary

Do first, brick-class, all READY-NOW or gated only on the in-flight BLE PRs
they fix: Group 1 batch 1 and 2 (B1, B2, B3, B4, A3a, A1, A2, A7a). These are
the highest severity and unblock #62 and #93.

Do next, code-only and READY-NOW: Group 1 batch 3 (B5, B7, B6, B9, B10, A8),
plus the RMT XTAL note (P1) and the two UI @todo cleanups.

Gated on the wifi restack (#53 to #66 to #90): all of Group 1 batches 5, 6, 7
and the esp32-s3-headless CI addition. The restack (C1) is the single
prerequisite that unblocks the entire hub half of the backlog.

Feature and vendor work (Groups 2 and 4) is enhancement-severity and can be
scheduled independently of the hardening batches.

## Bugs also present upstream

Purpose: call out in the release notes which bugs the fork fixed that ALSO
exist in upstream gkoh/furble, so they can be contributed back. Each candidate
was confirmed by reading the relevant file on origin/master
(gkoh/furble, at `2b79ce8` lineage). CONFIRMED-UPSTREAM means the defect is
visible in upstream code as read. FORK-ONLY means the mechanism only exists in
the fork (a fork-added feature) and is not an upstream contribution candidate.

### CONFIRMED-UPSTREAM

- FujifilmBasic null shutter-service and shutter-characteristic deref.
  CONFIRMED-UPSTREAM. In `origin/master:lib/furble/FujifilmBasic.cpp` the
  shutter service null case only logs "Failed to get shutter service", then
  `m_Shutter = pSvc->getCharacteristic(...)` dereferences the null service
  (crash), and a null m_Shutter only logs and still `return true` (connected
  with an inert shutter, the false-connected symptom). Same class as plan 96
  A1 and the fork fix. Strong upstream-PR candidate.
- False-connected: connect returns success without confirming registration.
  CONFIRMED-UPSTREAM. Upstream FujifilmBasic._connect keeps the registration
  wait disabled behind `#if 0` and returns true after subscribing, with no
  arrival latch. This is the exact class plan 75 fixed on the fork. Upstream
  never confirms the camera accepted registration before promoting the link.
- Droppable CMD_DISCONNECT plus unbounded disconnect spin.
  CONFIRMED-UPSTREAM. In `origin/master:src/FurbleControl.cpp`
  Target::sendCommand uses `xQueueSend(m_Queue, &cmd, 0)` (zero timeout, log
  only), and Control::disconnect spins
  `do { vTaskDelay(1); } while (!target->m_Stopped);` with no bound while
  holding m_Mutex. This is the reconnect-cancel deadlock and brick-class
  family (plan 96 B2, CLAUDE.md hardware trap). The code pattern is upstream;
  the observed brick needs the fork's M5PM1 watchdog with power gestures
  disabled to turn the hang into a hard reset, so upstream sees a hang rather
  than a brick. Upstream-PR candidate as a robustness fix.
- NimBLE disconnect runs under the Control mutex. CONFIRMED-UPSTREAM.
  Upstream Control::disconnect clears m_Targets under m_Mutex and each Target
  destructor calls the blocking m_Camera->disconnect(), contradicting the
  state-only intent (plan 96 B4). Present verbatim upstream.
- m_Targets iterated without the mutex from multiple tasks. CONFIRMED-UPSTREAM.
  Upstream updateGPS, allConnected, and getTargets iterate m_Targets with no
  lock while addActive and disconnect mutate it under the lock (plan 96 B5).
  The window is narrower upstream because upstream is effectively
  single-target, but the unsynchronized access is structurally the same.
- m_ConnectCamera written unlocked and left dangling on failed connects.
  CONFIRMED-UPSTREAM. Upstream connectAll writes m_ConnectCamera with no lock
  and only nulls it on success, so a failed connect leaves a dangling pointer
  that getConnectingCamera reads unlocked (plan 96 B7). Lower impact upstream
  without the fork's periodic reader, but present.
- MITM requested then nullified. CONFIRMED-UPSTREAM. Upstream Camera.cpp calls
  `setSecurityAuth(true, true, true)` with no onConfirmPasskey override, so
  numeric comparison auto-accepts (plan 96 A5). The security level requested
  is not the security delivered. Documentation-or-fix candidate.
- Camera::isConnected takes m_Mutex and calls into NimBLE. CONFIRMED-UPSTREAM
  as the shared root of plan 96 B1. Upstream isConnected holds m_Mutex and
  calls m_Client->isConnected(). Upstream cannot deadlock through it because
  it has no companion GATT service on the host task, but the lockful
  implementation is the same starting point the fork made lock-free.

### FORK-ONLY

These were fixed or flagged on the fork but the mechanism does not exist
upstream, so they are not upstream-contribution candidates.

- Watchdog fed only from the UI task. FORK-ONLY. Upstream FurblePlatform.cpp
  has no watchdog feed at all; the PM1 watchdog is a fork feature (plan 26).
- Adaptive-TX wrong-direction RSSI caveat. FORK-ONLY. Adaptive TX power,
  setConnSaver, and reconnect backoff do not exist upstream (fork plans 09,
  10, 11). The caveat is a fork design note, not an upstream bug.
- STATE_DISCONNECTING stuck on timeout (plan 96 B3). FORK-ONLY. The timeout
  early-return is introduced by feat/68-reconnect-restart (#62); upstream has
  no disconnect timeout, it spins unbounded (the B2 defect above) instead.
- Stranded shutter on reconnect (plan 96 B6). FORK-ONLY. Requires
  multi-connect (feat/25, #38); upstream is single-target.
- #93 registration-wait cancel gap (A3a) and atomic m_Configured (A3d).
  FORK-ONLY. waitForRegistration and the arrival latch are the fork's plan 75
  additions; upstream has no such wait to harden.
- WiFi, MQTT, WebUI, companion-service defects (plan 96 group C, A6).
  FORK-ONLY. The entire wifi-hub and companion stack is fork-only.

### Not re-confirmed here

The following fork fixes were noted in the task brief but are either fork-only
by construction or need a per-vendor upstream read before claiming: Canon and
Sony saved-reconnect promotion (A4, present upstream in the same vendor files
but the fork has not changed upstream's behavior yet, so it is a shared latent
gap rather than a fork fix to contribute back), and the FujifilmSecure address
update (A7a, the Nikon-vs-Fujifilm asymmetry exists upstream too and is a valid
upstream candidate once the fork fix is proven on hardware).
