# 97: Consolidated follow-up backlog

Status: active backlog. This is the single deduplicated backlog for the whole
engineering program. It consolidates every outstanding follow-up from the
radio-hardening audit, the plan gap-analysis deferrals, the review-flagged
fixes still owed, and the in-tree code markers. Each item carries a one-line
description, a source reference, a severity, a readiness tag, and now a state
line. The first tranche, Group 1 batches 1 to 3, was audited item by item
against master at 253d9cbe: most of it had already landed under other PRs, two
items were still open and are fixed here, and the rest carry the reason they
stay open. The second tranche, batches 4 to 7, was audited the same way at
79df13a7. Of its twenty-two items, ten had already been fixed under their own
PRs, three were already settled by a recorded decision, three are fixed here
(the Canon identity writes, the companion secret reads and the adaptive
transmit floor), and six stay open with the reason recorded, which for most of
them is a device gate rather than missing code. The state lines are the
backlog's audit trail, so a reader can tell a settled item from an untried one
without re-reading the code.

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
BLE and WiFi items. It is analysis-complete and merged on fork/master. Item IDs
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
  State: already fixed before this tranche, ce710521 (#109). `isConnected()`
  returns the cached `std::atomic<bool> m_Connected` with no mutex and no
  client dereference (`lib/furble/Camera.cpp:1533`,
  `lib/furble/Camera.h:660`). The companion status read reaches it through
  `Control::getConnectedTargetCount()`, which takes only Control's own mutex,
  so the cached-snapshot half became unnecessary once the camera read stopped
  taking Camera::m_Mutex. No work was invented for it here.
- Non-droppable CMD_DISCONNECT plus bounded disconnect. High. Send disconnect
  with a timeout or queue-reset-then-send-to-front so a full queue cannot drop
  it and spin disconnect() forever on the UI task. Source: plan 96 B2,
  `src/FurbleControl.cpp:53-58, 360-364`. READY-NOW.
  State: already fixed before this tranche. `Target::sendCommand()` drains the
  queue and posts the disconnect at the front, so a full queue cannot drop it
  (`src/FurbleControl.cpp:104`, 332f711a, #62), and `Control::disconnect()`
  waits on the target tasks with the caller's own bound instead of spinning on
  the link (`src/FurbleControl.cpp:1056`, 633fd8f4, #128).
  `control_disconnect_test` covers the dead-camera and clean-disconnect paths.
- Force-complete on disconnect timeout. High. On timeout mark targets stopped,
  clear m_Targets under the mutex, setState(IDLE), log loudly. Never leave
  STATE_DISCONNECTING without an exit edge. Source: plan 96 B3 (C1/C2 pairing
  in the task brief), `src/FurbleControl.cpp:290-291`. GATED-ON: fix inside
  feat/68-reconnect-restart (#62) before it merges, or immediate follow-up.
  State: already fixed, in #62 itself (332f711a). The restart path
  force-completes on its timeout and moves any target whose task has not
  stopped into the drain set rather than freeing it, and both paths publish
  STATE_IDLE on every exit, so STATE_DISCONNECTING always has an exit edge.
  `control_teardown_wedge_test` and `control_disconnect_test` cover the
  wedge it prevented.
- Destroy targets outside the Control mutex. Medium. Swap the vector out under
  the lock, run the blocking BLE terminations outside it. Source: plan 96 B4,
  `src/FurbleControl.cpp:42-47, 371-372`. READY-NOW.
  State: already fixed before this tranche, 633fd8f4 (#128). `disconnect()`
  moves the targets into the drain set under the mutex without running a
  destructor, and `reapZombieTargets()` (`src/FurbleControl.cpp:1303`) frees
  only a target whose task has published m_Stopped, so the destructor skips
  its radio call while the lock is held. `control_zombie_cancel_test` and
  `control_reclaim_uaf_test` pin the drain and the reclaim.
- Cancel and disconnect checks in waitForRegistration (#93). High. Poll
  m_Connected plus a cancel flag each slice, return false fast instead of
  spinning up to 25 s under Camera::m_Mutex. Source: plan 96 A3a. GATED-ON:
  fix inside feat/75-false-connected (#93) before merge.
  State: already fixed, bca68c2a. `Fujifilm::waitForRegistration()` polls the
  link, the cancel token and the active flag on every slice
  (`lib/furble/Fujifilm.cpp:120`), and `fujifilm_registration_cancel_test`
  covers the cancel that lands mid-wait.

### Batch 2: Fujifilm registration and bonds

- FujifilmBasic null-deref hardening. High. Make shutter service and
  characteristic discovery hard failures, return false on any null. Today a
  null service is dereferenced (crash) and a null shutter still returns
  connected. Source: plan 96 A1, `lib/furble/FujifilmBasic.cpp:151-168`.
  READY-NOW. Also present upstream, see the upstream section.
  State: already fixed before this tranche, a79bdae0 (#126). A null shutter
  service and a null shutter characteristic both return false
  (`lib/furble/FujifilmBasic.cpp:170`, `lib/furble/FujifilmBasic.cpp:179`),
  with `fujifilm_missing_shutter_service_test` and
  `fujifilm_missing_shutter_char_test` as the regressions.
- m_Configured as std::atomic<bool>. Low. Replace the volatile bool that
  crosses tasks. Source: plan 96 A3d. GATED-ON: feat/75-false-connected (#93).
  State: already fixed, bca68c2a. `lib/furble/Fujifilm.h:112` holds the
  atomic, read by the registration wait and written by the notification
  callback.
- Stale-bond delete-and-retry. High. On a fresh Ricoh pairing security failure
  for a bonded address, delete only the matching local bond and return failure;
  the next bounded control retry performs numeric comparison. Saved reconnect
  failures preserve their bond. The retry stays outside the security callback
  so NimBLE client/callback lifetime ownership remains safe. Source: plan 96
  A2. READY-NOW.
  State: already fixed, 01e97e3d. A failed secure handshake on a fresh
  live-scan pairing deletes only the matching bond and returns failure
  (`lib/furble/Ricoh.cpp:202`); saved reconnects keep theirs.
  `fujifilm_stale_bond_test` covers the Fujifilm shape and
  `ricoh_control_flap_test` the Ricoh one.
- FujifilmSecure address update from rescan. Medium. Mirror Nikon: update
  m_Address from the advertisement before connecting, re-serialize on success.
  Source: plan 96 A7a, `lib/furble/FujifilmSecure.cpp:69-88, 123-124`.
  READY-NOW.
  State: fixed in this tranche, 39400b25, PR #530. The scan match carries the advertised
  address across the queue instead of a bare success flag, and the saved path
  adopts it before connecting (`lib/furble/FujifilmSecure.cpp:212`), then
  serialises the new address. `fujifilm_secure_address_test` is the red-first
  regression. Deviation from the item text: no NVS write was added to the
  connect task, because a saved reconnect re-derives the address on every
  attempt and the record is rewritten by the existing STATE_ACTIVE save path
  when the session came from the scan menu.
- Decide the Basic registration-gate scope. Medium, suspected. Verify a Basic
  body notifies on hardware, or scope the mandatory gate to Secure and
  log-only on Basic until verified. Source: plan 96 A3b. GATED-ON:
  feat/75-false-connected (#93) plus hardware.
  State: still gated. Nothing in the host harness can settle whether a Basic
  body notifies, so the decision waits for a Basic-protocol device or the
  scoped log-only fallback.

### Batch 3: Control concurrency hygiene (code-only)

- Lock the three unlocked m_Targets iterations. Medium. updateGPS, allConnected
  plus the ACTIVE fan-out, and the disconnect wait loop all iterate m_Targets
  from different tasks while mutations happen under the mutex. Source: plan 96
  B5, `src/FurbleControl.cpp:272-317, 360-364`. READY-NOW. Also present
  upstream, see the upstream section.
  State: already fixed before this tranche, bbe4d22c (#487). updateGPS, the
  ACTIVE fan-out and allConnected all take Control's mutex, and
  `control_gps_race` plus its unsanitized twin walk the GPS path against
  addActive and disconnect.
- Atomics for m_State and m_ConnectCamera, clear on failure, failcount reset.
  Low now, high once #63 lands. m_ConnectCamera is written unlocked and left
  dangling on failed connects. Source: plan 96 B7, `src/FurbleControl.cpp:182,
  187, 397-399`. READY-NOW, coordinate with feat/66-pairing-code (#63).
  State: already fixed before this tranche. m_State is atomic (cbb61ce9),
  m_ConnectCamera is published under the mutex through a generation-stamped
  setter and cleared on both connect outcomes (5e1ac1dd), and the failure
  count resets at each user cycle, on success, and when the last cancelled
  target retires.
- Synthesized shutter release on leaving STATE_ACTIVE. Medium. Track a held
  flag in Control and release still-connected targets when one camera drops,
  so a multi-connect reconnect does not strand a held shutter. Source: plan 96
  B6 (C4 in the task brief). READY-NOW.
  State: still open, not taken in this tranche. The UI releases a held bulb
  exposure at session teardown and when the bulb page is left, but a
  mid-session drop still has no Control-side release. Landing it needs the
  multi-connect session plus a device answer for what a dropped body does with
  an open bulb exposure, and guessing at that is what the hardware trap in
  CLAUDE.md warns about.
- setConnSaver fan-out moved off the LVGL task. High leverage. Move the up-to
  18 s HCI fan-out to the control task via a command; document the watchdog
  feeder decision. Source: plan 96 B9, `src/FurbleControl.cpp:640-645`.
  READY-NOW.
  State: fixed in this tranche, d08621e2, PR #530. setConnSaver() publishes the value
  and wakes the control task, which applies it before any state work
  (`src/FurbleControl.cpp:1691`, `src/FurbleControl.cpp:1708`), so the LVGL
  task never enters NimBLE for the toggle. `control_conn_saver_test` is the
  red-first regression. Deviation from the item text: a pending flag applied
  every tick replaces a new command, which keeps the request from being
  dropped when the command queue is full.
  Watchdog feeder decision: the PM1 feed stays on the UI task
  (`src/FurblePlatform.cpp:341-348`). Moving the fan-out removes the only
  multi-second UI-task block this item named, so a second feeder would hide
  the next such block rather than prevent it. PM1 liveness as its own item
  stays in batch 7, unchanged.
- Single cancel-callback registration. Low. Register the cancel event callback
  once at UI build time instead of per connect. Source: plan 96 B10,
  `src/FurbleUI.cpp:1952-1953`. READY-NOW.
  State: already fixed before this tranche, b90d5666 (#114). The handler is
  registered once when the connect message box is built
  (`src/FurbleUI.cpp:9558`), and the cancel-single-disconnect simulator
  scenario pins one disconnect per click.
- Small library correctness. Low. gattRead returns true on failed reads;
  FujifilmSecure scan-end race loses a success; Scan::onResult unlocked
  check-then-invoke. Source: plan 96 A8, `lib/furble/Camera.cpp:530-546`,
  `lib/furble/Scan.cpp:44-93`. READY-NOW.
  State: two of the three are already fixed before this tranche. gattRead
  treats an empty read as a failure (13653c24), and Scan dispatches through a
  state mutex with an in-flight count that stop() waits for
  (`lib/furble/Scan.cpp:117`). The scan-end success race is still open: the
  loss window is the few instructions between a one-second receive timeout
  returning and the scan reporting inactive, the harness has no seam there,
  and adding the two-line drain without a failing test is the kind of
  unproven fix this backlog exists to avoid.

### Batch 4: pairing UX and vendor gates

- Passkey-entry UI or loud failure, PAIRING_WINDOW 30 s, doc the fixed display
  passkey. Medium. onPassKeyEntry still injects 123456, so a random-code camera
  fails silently; the 2 minute window outlives the ~30 s SMP timeout. Source:
  plan 96 A5a/A5b/A5c. GATED-ON: feat/66-pairing-code (#63).
  State: already fixed before this tranche, 501ea2ab (#63). Camera publishes the
  request and its type instead of answering it on the host task, the LVGL modal
  renders numeric comparison and passkey display, and a request the UI cannot
  take is rejected rather than auto-accepted, so a full request queue fails the
  connect visibly. The window is 30 s (`Camera::PAIRING_WINDOW_MS`), and
  `docs/ui-walkthrough.md` records that a display request is the build's own
  fixed passkey. `camera_pairing_test`, `camera_pairing_peer_test` and
  `camera_pairing_race_test` are the regressions. The entry direction still
  injects the configured passkey and says so (`pair.entry: required` on the
  console and a WARN log), which is the loud-failure half the item allowed.
- answerPairing client use-after-free window. Medium, suspected. Do the inject
  and disconnect via a handle-based call or hold m_PairingMutex across the use.
  Source: plan 96 A6. GATED-ON: feat/66-pairing-code (#63).
  State: already fixed before this tranche, 501ea2ab (#63). The request captures
  the connection handle when it is published, and the whole answer runs under
  `m_PairingMutex`: the live client has to still report that handle before
  anything is injected, and a mismatch consumes the request without answering
  it. The mutex is recursive because the answer can synchronously reach
  `onDisconnect()` on the same thread before NimBLE self-deletes the client, so
  no client use outlives the lock. `camera_pairing_race_test` and
  `camera_pairing_peer_test` cover the race and the refusal paths.
- PR #38 multi-connect row identity plus the fork/master diagnostics-page fix.
  Medium. Rows should store camera identity and re-resolve each tick; pause the
  diagnostics timer with the reconnect box. Source: plan 96 B8,
  `src/FurbleUI.cpp:3758-3760`. GATED-ON: feat/25-multiconnect-ui (#38).
  State: already fixed before this tranche by #62 and #63. The Cameras rows are
  built from `Control::getTargetCameras()`, which hands out strong camera
  references, and the tick re-resolves them (`UI::camerasUpdate`), so a retired
  target cannot leave a freed pointer on a row. The tick self-pauses outside
  STATE_ACTIVE, the lock-free `isConnected()` from batch 1 removes the LVGL block
  the item was really about, and a mid-session reconnect never takes the screen:
  it raises the status-row icon and the page banner instead of a modal box, so
  there is no reconnect box left for the diagnostics timer to be co-resident
  with (that timer only runs on its own pages).
- Canon and Sony saved-reconnect liveness promotion. Medium, structural gap.
  Add a cheap vendor liveness probe for saved reconnects and switch Canon
  identity writes to write-with-response. CanonEOSSmart skips its pairing gate
  when isBonded. Source: plan 96 A4, `lib/furble/CanonEOSRemote.cpp:64`,
  `lib/furble/Sony.cpp:34-84`, `lib/furble/CanonEOSSmart.cpp:42-44`. READY-NOW.
  Untested vendor, code review plus FauxNY only.
  State: the write-with-response half is fixed in this tranche, 56f0d4e9. Both
  Canon roles identify through `CanonEOS::writePrefix()`, which now asks for the
  ATT response, and `CanonEOSSmart::_connect()` waits for the response on the
  final accept byte. Every one of those writes is part of the identity exchange,
  so the response is the only acknowledgement the connect sequence gets: a body
  that refused the identity step used to be promoted as paired because a write
  without response reports the PDU as sent. `canon_identity_ack_test` is the
  red-first regression and asserts the flag on the writes the peer receives.
  Still open, same item: the saved-reconnect liveness probe for both Canon roles
  and Sony, and the pairing gate `CanonEOSSmart` skips when `isBonded()`. Both
  need a protocol read and a device, and Canon and Sony are declared untested
  here, so they were not guessed at.
- RPA and address-rotation notes plus bond cleanup. Low to medium. Canon, Sony,
  Ricoh have no rescan (A7b); CameraList::remove leaks the real bond when the
  saved address is stale (A7c). Source: plan 96 A7b/A7c,
  `lib/furble/CameraList.cpp:135-137`. READY-NOW.
  State: still open, with the reason now recorded. `CameraList::remove()` deletes
  the bond at the address in the saved record, and a record stores the address the
  camera advertised. A peer that rotates its resolvable private address keeps its
  bond at the identity address, so the delete misses. Closing that means storing
  the bonded identity address in the record, which is an NVS record-format change
  with a migration and a compatibility question for records already written; it is
  not a one-line delete. A7b is worse: plan 96 marks rotation SUSPECTED with no
  field evidence for Canon, Sony or Ricoh, so a rescan path for those vendors
  would encode a guess. Both stay open until the identity address is in the record
  and there is field evidence that a supported vendor rotates.

### Batch 5: wifi-hub restack and STA robustness

- Restack the wifi-family: rebase #66 and #90 onto #53. High, integration
  defect. 33c and 33d branch from 33-wifi-hub in parallel, carry zero
  esp_wifi bring-up, poll networkReady forever, and their sdkconfigs still
  hold pre-33b netif state across all six files. Rebase 33c onto 33b and 33d
  onto the result with a careful six-file sdkconfig reconciliation. Source:
  plan 96 C1. GATED-ON: this must precede any wifi-chain merge.
  State: closed as obsolete, the restack already happened. 33a merged as #148,
  33b as #53, 33c as #66 and 33d as #90, in that order, on top of one another,
  and plan 33 records the six-file sdkconfig reconciliation as part of that
  merge. There is no branch left to rebase and the gate the item carried has
  been satisfied for a long time. Every batch 5 and 6 item below is therefore
  judged on its own code, not deferred behind a restack.
- Headless MQTT sdkconfig symbols. Medium. Add CONFIG_MQTT_PROTOCOL_311 and
  CONFIG_MQTT_TRANSPORT_SSL to sdkconfig.esp32-s3-headless; today they are in
  the five release configs only. Source: plan 96 C7. GATED-ON: wifi restack.
  State: already fixed before this tranche, a6c8e2ad (#148).
  `sdkconfig.esp32-s3-headless` sets `CONFIG_MQTT_PROTOCOL_311` and
  `CONFIG_MQTT_TRANSPORT_SSL` exactly as the release files do. No test covers a
  sdkconfig; the headless firmware build is the check, and CI builds that env.
- WiFi retry-stall after a blocked scan. Medium. Notify the WiFi worker on the
  Control transition out of STATE_ACTIVE, or retry direct connects while
  blocked; today the worker parks on portMAX_DELAY. Source: plan 96 C2,
  `src/FurbleWiFi.cpp:312-318, 385-414`. GATED-ON: wifi restack.
  State: already fixed before this tranche, fca5178b and 2f650822 (#319). A
  refusal from the coexistence gate now shortens the retry deadline to
  `COEX_RETRY_MS` instead of leaving the worker parked, so the station retries
  seconds after the camera session ends rather than after the full backoff.
  `real-wifi-lifecycle` drives the production handler and pins the shortened
  cap through `WiFi::hostCoexRetryCap()`.
- DHCP timeout and LOST_IP handling. Medium. Bounded post-associate wait
  treated as failure, register IP_EVENT_STA_LOST_IP. Source: plan 96 C3.
  GATED-ON: wifi restack.
  State: already fixed before this tranche, 7caf0468 (#485). The join budget is
  bounded and a budget that expires counts one failed attempt, and
  `IP_EVENT_STA_LOST_IP` is registered and clears the cached address so the
  WebUI stops answering on a dead one. `real-wifi-lifecycle` drives both the
  bounded join and the lost-lease path against the production handler.
- WiFi auth-fail classification and sys_evt task offload. Low. Distinguish a
  wrong PSK from AP loss; move NVS writes and SNTP init out of the 2304-byte
  sys_evt task. Source: plan 96 C10. GATED-ON: wifi restack.
  State: already fixed before this tranche, 2f650822 (#319). `classifyDisconnect()`
  separates `WIFI_REASON_AUTH_FAIL` (a permanent stop with an actionable log) from
  the AP-loss and transient classes, and the handler only records a pending
  erase or save that the worker performs, so no NVS write and no SNTP init runs
  on the sys_evt task. `real-wifi-lifecycle` drives the auth-fail stop and the
  queued storage.

### Batch 6: hub services hardening

- MQTT permanent-block backoff. Medium. Replace the give-up-after-10-failures
  behavior with capped exponential backoff that retries forever; keep the hard
  block only for identified auth failures. Source: plan 96 C5,
  `src/FurbleMQTT.cpp:490-501`. GATED-ON: wifi restack.
  State: already fixed before this tranche, b3d08489 (#498), planned and
  documented in [339-mqtt-retry-backoff.md](339-mqtt-retry-backoff.md).
  `MAX_CONNECT_FAILURES` is gone; a retryable failure schedules 10, 20, 40, then
  60 s forever, and `m_Blocked` survives only for the two classes a retry cannot
  fix. The MQTT host double covers the classifier and the schedule.
- WebUI CSRF and auth hardening. Addressed by the PR33d successor: HTTPS and
  companion-password authentication are mandatory, POSTs require JSON, and
  browser Origin must match Host. Browser, radio and hardware acceptance remain
  release gates. Source: plan 33d.
  State: landed before this tranche, 9d59b5ce (#90). A POST without
  `Content-Type: application/json` is refused with 415, and a request that
  carries `Origin` must match `Host`; both sit in the shared request gate ahead
  of every handler. The browser, radio and hardware acceptance in the item text
  remain release gates and are unchanged by this audit.
- Secrets-over-companion redaction. Medium. settingValue returns raw WIFI_PSK
  and MQTT_PASS over the BLE companion service; return a set/unset indicator
  and keep writes write-only, matching console and REST. Source: plan 96 C6,
  `src/FurbleCompanionService.cpp:316-326`. GATED-ON: wifi restack.
  State: fixed in this tranche, 2c47add0. `settingValue()` answers the
  word `set` or `unset` for `WIFI_PSK` and `MQTT_PASS`, in the setting's own
  `SETTING_STRING` type, for both the single read and the catalog, so no client
  needs a new wire type and no reader receives the credential. Writes are
  untouched, so the setting stays write-only rather than read-only.
  `companion_gatt_test` grew a red-first case that reads a stored passphrase
  through op 1 and op 0 and asserts the secret text never appears in a record.
  Deviation from the item text: the item named an indicator; this answers the
  console's words instead, because a one-byte indicator inside a string setting
  would render as a stray byte in the two companions' text editors, and the URI
  stays readable because it carries the broker address a user needs.
- CameraList access routing plus MQTT timer teardown guard. Low. Serialize
  CameraList loads from httpd and mqtt tasks against the GUI scan; guard
  esp_timer publishes into a client being destroyed. Source: plan 96 C10.
  GATED-ON: wifi restack.
  State: already fixed before this tranche, 703beb35 for the catalog routing and
  b0aca150 (kept through 65f42427, #470) for the timers. The saved catalog is read
  through `CameraList::savedSnapshot()` under the persistence mutex, and a scan
  only touches the transient lists, so an httpd or mqtt read no longer races the
  GUI scan. The MQTT timer callbacks no longer publish: they notify the owner
  task, and `releaseOwnedActuators()` stops both timers before the client is
  destroyed, so nothing can publish into a client being torn down.
- Hub heap headroom and env-gating. Medium, suspected. Run the min-free-heap
  soak on a non-PSRAM board before merge; consider gating the hub to S3 envs.
  Source: plan 96 C9, `src/main.cpp:291-292`. GATED-ON: wifi restack.
  State: still open, and it is a measurement plus a product decision, not code.
  The concern is a non-PSRAM Stick spending about 45 KB of driver plus a TLS peak
  on top of an already tight heap, and no host test or simulator run can answer
  it: the simulator seeds a heap ceiling but models neither the WiFi driver
  allocation nor the TLS handshake peak. Gating the hub to the S3 environments
  would remove the question for the 4 MB boards and remove the feature for them
  at the same time, which is the part that needs a maintainer decision.

### Batch 7: coexistence and power validation

- Coex rec W2: BLE connection-interval floor while WiFi is up. Medium. Add a
  WiFi-aware conn profile at 51.25-60 ms avoiding multiples of 102.4 ms,
  selected while WiFi reports connected. Source: plan 96 C8 plus plan 65,
  `lib/furble/Camera.h:283-286`. GATED-ON: wifi restack.
  State: still open, deliberately. The selection is not the hard part; the
  acceptance is. Flipping the fast profile to 51.25-60 ms changes the interval
  every hub user's shutter writes ride on, and plan 65's three-arm latency test
  (p50/p95/p99 plus missed events, three cameras) is what decides whether it
  ships. It also has to cover the pre-connect parameters, not only a mid-session
  request, because with the connection saver off the pre-connect interval is the
  one that governs. Landing that without the A/B would be the
  behaviour-a-user-notices change this backlog exists to make deliberate.
- Coex rec W3: hub camera-count cap and doc. Medium. Document 3 cameras
  supported, 5 tested, in hub mode. Source: plan 96 C8. GATED-ON: wifi restack.
  State: still open. The numbers come from ESPHome field data quoted second-hand
  in plan 65, and publishing "3 supported, 5 tested" in the user docs turns that
  into a Furble claim nobody here has reproduced. The cap half is a product
  decision on top of it. It stays a documentation item until a hub board runs
  the count.
- Coex rec T1: adaptive TX floor while WiFi is up. Low. Floor the adaptive
  range one step higher, or suspend step-down, while WiFi is connected.
  Two-line condition in sampleAdaptivePower. Source: plan 96 D2. GATED-ON:
  wifi restack.
  State: fixed in this tranche, a74135d1. `Control::sampleAdaptivePower()`
  reads a station probe the WiFi service installs and will not step below P6
  while it answers connected; a link already at P3 comes back up one step on the
  next sample instead of waiting for an RSSI decision that a strong signal never
  makes. The floor is clamped to the user's cap, so a +3 dBm setting still means
  +3 dBm. `control-adaptive-floor` is the red-first regression, driving the probe
  and asserting the level the radio is asked for. Deviation from the item text:
  the floor is a clamp with its own step-up path rather than one extra condition,
  because a plain condition would never lift a link that had already stepped down
  before the station came up. What a device must confirm: the shutter median and
  worst case plus the missed-event count at the floored level against the
  unfloored one, and the 24 h disconnect soak the W-18 entry asks for.
- Coex rec P1: RMT carrier on XTAL. Low, suspected. Use RMT_CLK_SRC_XTAL on S3
  for the IR carrier to survive DFS, matching the GPS UART pattern. Source:
  plan 96 D3, `src/FurbleIR.cpp:217`. READY-NOW.
  State: settled as rejected, c61eaa80. `src/CLAUDE.md` records the measurement
  and the Kconfig read behind the decision: the RMT driver takes
  `ESP_PM_CPU_FREQ_MAX` for a channel without DMA, furble raises the CPU maximum
  itself, and APB is 80 MHz there, so the 38 kHz carrier holds. `RMT_CLK_SRC_XTAL`
  does not exist on plain ESP32, so the change would also split the IR driver per
  chip for no measured gain. `src/FurbleIR.cpp:252` deliberately keeps
  `RMT_CLK_SRC_DEFAULT`.
- Adaptive TX wrong-direction RSSI caveat. Low to medium, design note.
  getRssi measures the camera TX, so the step-down loop is open; document in
  plan 11 and optionally step up on supervision-timeout disconnects. Source:
  plan 96 B12, `src/FurbleControl.cpp:552-583`. READY-NOW. Present upstream in
  latent form, see the upstream section.
  State: the documentation half is already done, in
  [implemented/11-adaptive-tx-power.md](implemented/11-adaptive-tx-power.md),
  which states that RSSI is measured on the camera's transmissions so the loop is
  open with respect to its own actuator and the hysteresis does not damp actuator
  feedback. The optional supervision-timeout step-up was not taken: it needs a
  device run to show that the drop was a power problem rather than a camera one.
- Battery brownout soak with WiFi TX. Low to medium, suspected. Run the plan
  65 battery soak; hold esp_wifi_set_max_tx_power(44) in reserve for Stick
  boards. Source: plan 96 D1. GATED-ON: wifi restack.
  State: still open. It is a soak, and nothing in the host harness or the
  simulator models a brownout on battery: the simulator WiFi service is a stub
  and the StickS3 has no current channel at all. The reserve lever
  (`esp_wifi_set_max_tx_power(44)`) stays unused until the soak says it is
  needed, which is the right order.
- PM1 watchdog liveness. High leverage, structural. The watchdog is fed only
  from UI::task, so any long UI block becomes a hard reset. Consider a small
  dedicated feeder that feeds only while a UI heartbeat advances. Source: plan
  96 B9, `src/FurblePlatform.cpp:341-348`. READY-NOW. Fork-only mechanism, see
  the upstream section.
  State: still open, and the reason is unchanged from the batch 3 tranche. The
  feed is rate limited in `Platform::watchdogFeed()` and called from the UI task
  plus the two bounded Control waits, so a hung UI still becomes a reset, which
  is what the watchdog is for. A second feeder that runs ahead of a heartbeat
  only helps if the heartbeat itself cannot be starved, and no host test can
  prove that; it needs a device soak with the flash, OTA and SD paths in the
  loop. Moving the connection-saver fan-out off the UI task removed the one known
  multi-second UI block, so there is less left for a feeder to protect.

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
  (`plans/implemented/71-ui-bug-batch.md`). READY-NOW.
- 73 Ricoh geotag. Geotag push for Ricoh bodies. Source: plan 73. READY-NOW.
  Untested vendor, code review plus FauxNY.
- 29 rig phases 4, 5, 6. Later multi-device rig phases. Source: plan 29.
  GATED-ON: earlier rig phases.
- 33d implemented. The authenticated HTTPS successor is pending serialized
  host, firmware, browser and hardware validation. Source: plan 33d.

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

That first tranche is now settled. Every batch 1 item, A1, A2, A3a and A3d
landed under the PRs cited above, and A7a landed with this tranche's plan
update. Reading the code again before fixing was the whole point: the plan
described the state of master when it was written, not today's.

Do next, code-only and READY-NOW: Group 1 batch 3 (B5, B7, B6, B9, B10, A8),
plus the RMT XTAL note (P1) and the two UI @todo cleanups. Five of the six
batch 3 items are settled. What remains is the Control-side shutter release
(B6), which needs the multi-connect session and a device answer, and the
FujifilmSecure scan-end drain (part of A8), which needs a queue seam before it
can carry a failing test.

The wifi restack gate is gone. 33a to 33d all merged, so batches 4 to 7 were
judged one item at a time instead of as a chain, and two thirds of them needed
no code from this tranche at all. What it did add: the
Canon identity writes now wait for the ATT response (A4, half of it), the
companion service answers a secret with set or unset instead of the credential
(C6), and the adaptive transmit policy will not step to the bottom of the range
while the station is connected (T1).

What is left is device work, not code waiting for time. Two items are
behaviour-visible radio changes whose acceptance is a measurement (W2 interval
floor, W3 camera count), one is a soak (D1 brownout), one needs a device answer
about a dropped body's open bulb exposure (B6, batch 3), one needs a hardware
sweep before the watchdog feed can be called live (PM1 liveness), and the
vendor liveness probes (A4 remainder) plus the bond-identity record (A7c) need
a protocol read and an NVS format decision first.

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
