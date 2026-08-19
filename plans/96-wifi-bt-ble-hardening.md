# 96: WiFi, Bluetooth and BLE hardening

Status: analysis complete, remediation not started. Docs-only PR. This document
is the issue inventory and the sequenced fix plan. Each fix batch becomes its
own PR and updates this doc with its implementation state.

## Motivation

The fork now spans three radio workloads: BLE camera links (the core product),
the wifi-hub chain (PRs #31, #53, #66, #90), and the BLE companion service.
Plan 75 exposed the false-connected class. PR #62 fixed one deadlock that
bricked a device. Plan 65 researched coexistence. This plan audits everything
around WiFi, Bluetooth Classic, and BLE in one pass, on fork/master (9c818f8
lineage, read at a5c2ca8..9c818f8) and the open PR branches, and sequences the
remaining engineering fixes so nothing in this class ships unreviewed.

Method: four parallel read-only audits (pairing/registration, reconnect and
concurrency, WiFi/MQTT/WebUI, power/coex) against fork/master,
feat/75-false-connected (#93), feat/68-reconnect-restart (#62),
feat/25-multiconnect-ui (#38), feat/66-pairing-code (#63), and the
feat/33 chain. Every issue below carries evidence, severity, and a CONFIRMED
or SUSPECTED tag. CONFIRMED means the defect is visible in the code as read.
SUSPECTED means the mechanism is real but the trigger needs a repro or
hardware evidence. Things that are already correct are listed too, so nobody
re-fixes them.

## What is already correct

Verified sound, do not touch without cause:

- Bluetooth Classic is fully compiled out on every board. NimBLE only,
  Bluedroid off, ESP32 controllers pinned BLE-only
  (sdkconfig.m5stick-s3:619-620, sdkconfig.m5stick-c:637-639). The S3 has no
  Classic radio. Plan 65 track 2 (Classic for Nikon Smart) stays not-attempted.
- Power lock discipline: counted locks, idempotent GPS optional-lock,
  balanced backlight APB lock, documented console exception
  (src/FurblePower.cpp:49-93, src/FurbleUI.cpp:154/774/791,
  src/FurbleGPS.cpp:224-241). No leaks found.
- DFS traps hold: GPS UART on XTAL/REF_TICK (src/FurbleGPS.cpp:85-87), BT
  modem sleep sdkconfig blocks are the DFS-compatible combinations
  (sdkconfig.m5stick-s3:858-862). The wifi chain adds no APB-clocked
  peripheral.
- Coexistence is already enabled on all boards
  (CONFIG_ESP_COEX_SW_COEXIST_ENABLE=y, sdkconfig.m5stick-s3:1029). Plan 65
  rec 6 (no sdkconfig change needed) is correct.
- Reconnect backoff (plan 09): bounds, 100 ms cancel slicing, reset points
  all correct (src/FurbleControl.cpp:207-227). PR #62 adds a sound 17 s
  stale-session floor.
- Adaptive conn params (plan 10): peer renegotiation via m_PeerOverride and
  the counter-proposal mirror are correct (lib/furble/Camera.cpp:333-374,
  703-717).
- Adaptive TX power (plan 11): no death spiral across drops. State resets on
  disconnect and reconnect applies the full user cap
  (src/FurbleControl.cpp:263-265, 597-608).
- Camera::onDisconnect touches only flags, no callback-vs-list race
  (lib/furble/Camera.cpp:180-192).
- Nikon Remote is correctly gated: the 4-stage challenge handshake validates
  every response before connect returns (lib/furble/NikonBase.cpp:95-141).
- Ricoh verifies isEncrypted, isAuthenticated, isBonded plus required
  characteristics (lib/furble/Ricoh.cpp:191-199, 237-244).
- WiFi (33b): backoff with jitter, WIFI_PS_MIN_MODEM, STA-only, scan denial
  while a camera is active, BSSID caching, six-file netif sdkconfig fix
  including the historical headless mis-set (src/FurbleWiFi.cpp:263, 291-318,
  406-415).
- MQTT (33c): non-blocking enqueue everywhere, LWT, modern HA device
  discovery with stale-config retraction (src/FurbleMQTT.cpp:392-396,
  934-935, 1245-1435).
- WebUI (90): shutter goes through the Control queue, no mutex in handlers,
  bounded bodies, self-disable deferred to the supervisor
  (src/FurbleWebUI.cpp:364-386, 770-775).
- Secret redaction on console and REST (33b src/FurbleConsole.cpp:305-307,
  33d src/FurbleWebUI.cpp:209-211, 580-583).

## Issue inventory

Line numbers are from the ref named per item. Severity: critical, high,
medium, low.

### A. BLE connect, pairing, registration

- A1. FujifilmBasic discovery failures do not fail the connect. HIGH,
  CONFIRMED, survives PR #93. On fork/master a null shutter service only
  logs (lib/furble/FujifilmBasic.cpp:151-154), then
  pSvc->getCharacteristic dereferences null at :159 (crash), and a null
  m_Shutter still returns true at :160-168 (connected with an inert
  shutter, the exact false-connected symptom). Same unchecked pChr at
  :102-103. Fix: make service and characteristic discovery hard failures,
  return false on any null.
- A2. No stale-bond recovery. HIGH, CONFIRMED absence. Nothing deletes a
  bond after secureConnection fails for a previously bonded camera. Camera
  deleted the pairing plus furble kept the bond equals permanent reconnect
  failure until the user deletes and re-adds the camera. Only
  CanonEOSSmart's reject path (:120) and the bt debug console delete bonds.
  Fix: on secureConnection failure for a bonded address, delete the bond
  and retry once (mirror the M5PM1 single-retry pattern).
- A3. PR #93 gate assessment. The gate itself is correctly ordered
  (clear before subscribe, latch on arrival, bounded 25 s timeout, clean
  teardown, runs off the Control mutex). Residual issues:
  - A3a. The wait ignores cancel and disconnect. HIGH, CONFIRMED.
    waitForRegistration polls m_Configured for up to 25 s holding
    Camera::m_Mutex with m_ConnectInProgress true and checks neither an
    abort flag nor m_Connected. Cancel during the wait spins the UI in
    disconnect() and trips the PM1 10 s watchdog on the S3 (see B2). Fix:
    poll m_Connected and a cancel flag each slice, return false fast.
  - A3b. Mandatory gate for Basic bodies is unverified. MEDIUM, SUSPECTED.
    Upstream disabled the old wait, payloads differ by model (X100VI shows
    0x01 0x00, the old predicate wanted 0x02 0x00), and the plan itself owes
    hardware verification. If a Basic body never notifies, every connect
    fails after a 25 s stall. Fix: verify on hardware before merge, or scope
    the mandatory gate to Secure and log-only on Basic until verified.
  - A3c. Arrival-based gate accepts any CHR_NOT1 traffic. MEDIUM,
    SUSPECTED incompleteness, acknowledged in plan 75. Needs a camera-side
    negative capture to tighten.
  - A3d. volatile bool m_Configured crosses tasks. LOW, CONFIRMED. Use
    std::atomic<bool>.
- A4. Canon and Sony promote active without app-level confirmation. MEDIUM,
  CONFIRMED structural gap. CanonEOSRemote returns true after link,
  secureConnection, one write-without-response identify, and discovery
  (lib/furble/CanonEOSRemote.cpp:64). Sony returns true after link,
  secureConnection, and discovery (lib/furble/Sony.cpp:34-38, 84).
  CanonEOSSmart has a real 60 s pairing-indication gate for new pairing
  (:109-123) but skips it entirely when isBonded (:42-44), so a saved
  reconnect to a camera that revoked remote control is promoted anyway. All
  Canon identity writes are write-without-response (lib/furble/CanonEOS.cpp:42),
  so not even ATT ACKs are checked. Fix: for saved reconnects add a cheap
  vendor liveness probe (state characteristic read, or run the
  pair-indication subscribe on CanonEOSSmart and require the accept byte);
  switch Canon identity writes to write-with-response. Code review plus
  FauxNY only, declared untested per repo policy.
- A5. MITM is requested but nullified on fork/master. MEDIUM, CONFIRMED.
  setSecurityAuth(true, true, true) for every vendor
  (lib/furble/Camera.cpp:224) but numeric comparison auto-accepts (no
  onConfirmPasskey override; Ricoh explicitly auto-accepts,
  lib/furble/Ricoh.cpp:477-481) and passkey entry injects a fixed 123456.
  PR #63 fixes the numeric-comparison half. Residual in #63:
  - A5a. The passkey entry path has no UI. MEDIUM, CONFIRMED gap.
    onPassKeyEntry still injects 123456, so any camera that displays a
    random code and expects entry fails silently. Fix: add an entry UI or
    fail that path loudly with a distinct message.
  - A5b. PAIRING_WINDOW_MS is 2 minutes but SMP times out near 30 s. LOW,
    CONFIRMED. The dialog outlives the procedure. Fix: reduce to ~30 s.
  - A5c. The fixed display passkey 123456 provides no MITM protection.
    LOW, CONFIRMED, acceptable for a camera remote. Document it.
- A6. PR #63 concurrency. See B7 for the shared m_ConnectCamera issue.
  answerPairing captures m_Client under m_PairingMutex then uses it outside
  the lock while setSelfDelete lets NimBLE free it on a peer drop. MEDIUM,
  SUSPECTED use-after-free window. Fix: do the inject and disconnect via a
  handle-based call or hold the lock across the use. The callbacks
  themselves never block the host task and the timeout/cancel funnel is
  sound.
- A7. RPA and address rotation. CameraList matching is a raw byte compare
  with no identity resolution (lib/furble/CameraList.cpp:224-229). Specific
  defects:
  - A7a. FujifilmSecure's serial-keyed rescan finds the camera but never
    updates m_Address from the advertisement before connecting to the saved
    address (lib/furble/FujifilmSecure.cpp:69-88, 123-124). Nikon does
    update it (lib/furble/Nikon.cpp:66). MEDIUM, CONFIRMED asymmetry. Fix:
    one line, mirror Nikon, and re-serialize on successful connect.
  - A7b. Canon, Sony, Ricoh have no rescan at all and are fully exposed to
    rotation. LOW to MEDIUM, SUSPECTED (most cameras appear to use stable
    addresses, no field evidence either way).
  - A7c. CameraList::remove deletes the bond by the saved address
    (:135-137); a stale address leaks the real bond in NVS. LOW, CONFIRMED.
- A8. Small library defects. LOW, CONFIRMED unless noted.
  Camera::gattRead returns true even when the read failed
  (lib/furble/Camera.cpp:530-546). FujifilmSecure's scan-wait loop can lose
  a success queued exactly at scan end, costing one 60 s cycle
  (lib/furble/FujifilmSecure.cpp:106-114, SUSPECTED). Scan::onResult does
  an unlocked check-then-invoke of a std::function that stop() nulls from
  another task (lib/furble/Scan.cpp:44-51, 87-93, SUSPECTED).

### B. BLE reconnect, lifecycle, concurrency

- B1. Host-task deadlock through the Control-to-Camera lock chain. HIGH,
  CONFIRMED chain, SUSPECTED trigger. Camera::connect holds
  Camera::m_Mutex for the whole attempt, up to 60 s for the FujifilmSecure
  rescan (lib/furble/Camera.cpp:195-244, FujifilmSecure.cpp:97-121).
  Control::getConnectedTargetCount takes Control m_Mutex then
  camera->isConnected() which takes Camera::m_Mutex
  (src/FurbleControl.cpp:410-419, Camera.cpp:760-767). CompanionGatt::onRead
  reaches it on the NimBLE host task (src/FurbleCompanion.cpp:494-496,
  src/FurbleCompanionService.cpp:109-112). Host task blocks on the mutex,
  the connect needs the host task to finish, whole BLE stack deadlocks, UI
  follows, PM1 resets the S3. Console status hits the same mutex from the
  console task (hang only). Fix: make isConnected lock-free
  (std::atomic<bool> m_Connected, drop the client double-check), and serve
  companion status from a cached snapshot refreshed by the companion task,
  never computed on the host task.
  - B1 status. The lock-free half is landed standalone (branch
    fix/lock-free-isconnected, PR "Lock-free Camera::isConnected so the UI
    stays responsive during connect"). Camera::m_Connected and m_Active are
    now std::atomic<bool>, and Camera::isConnected() returns
    m_Connected.load() with no m_Mutex and no m_Client double-check. The UI,
    console and control tasks no longer block on Camera::m_Mutex while a
    connect holds it, so the UI keeps feeding the PM1 watchdog through a slow
    first connect. This removes the UI-starves-watchdog reset path. The
    cached companion status snapshot (host-task path) is still open and stays
    with the rest of the WiFi/BT hardening batch.
- B2. Disconnect can spin forever and CMD_DISCONNECT can be dropped. HIGH,
  CONFIRMED on fork/master. Target::sendCommand uses xQueueSend with zero
  timeout and logs on failure (src/FurbleControl.cpp:53-58, queue depth 8).
  If CMD_DISCONNECT is dropped, disconnect() spins in
  while (!target->m_Stopped) on the UI task (:360-364): UI dead, watchdog
  starves, S3 hard-resets. This is the historical brick family. The
  m_ConnectInProgress wait (:366-368) is bounded only by the connect attempt
  itself; Scan::stop in doDisconnect rescues the scan phase but not GATT
  setup and not the #93 registration wait. Fix: make CMD_DISCONNECT
  undroppable (send with timeout, or queue reset plus sendToFront), and make
  the whole disconnect path bounded with a forced completion (below).
- B3. PR #62 leaves STATE_DISCONNECTING stuck on timeout. MEDIUM, CONFIRMED
  on the branch. The timeout return skips m_Targets.clear() and
  setState(STATE_IDLE); Control::task then discards every later command
  including CMD_CONNECT (src/FurbleControl.cpp:290-291), so the device
  cannot reconnect until reboot. Fine inside prepareRestart (esp_restart
  follows), wrong for the interactive cancel path that shares the function.
  doDisconnect ignores the return value. Fix: on timeout force-complete:
  mark targets stopped, clear m_Targets under the mutex, setState(IDLE),
  log loudly. Never leave STATE_DISCONNECTING without an exit edge.
- B4. NimBLE disconnects run under Control m_Mutex. MEDIUM, CONFIRMED.
  disconnect() clears m_Targets under the mutex and each Target destructor
  performs a blocking BLE termination (src/FurbleControl.cpp:42-47,
  371-372), contradicting the "state only, no radio calls" comment at :350.
  PR #62 fixes this only for targets that stopped cleanly. Fix: move
  disconnects out from under the mutex (swap the vector out under the lock,
  destroy outside).
- B5. m_Targets is iterated without the mutex from three tasks. MEDIUM,
  CONFIRMED. updateGPS from the GPS task (src/FurbleControl.cpp:300-307 via
  src/FurbleGPS.cpp:798), allConnected and the STATE_ACTIVE fan-out on the
  control task (:272-287, 309-317), the disconnect wait loop on the UI task
  (:360-364), while mutations happen under the mutex on the UI task.
  Iterator invalidation and use-after-free windows on every disconnect.
  Fix: take the mutex or use the getTargets snapshot in all three; these are
  short state-only sections.
- B6. Commands are silently dropped during reconnect, stranding a held
  shutter. MEDIUM, CONFIRMED. A single camera drop moves the control task to
  STATE_CONNECT and any CMD_SHUTTER_RELEASE arriving then is discarded
  (src/FurbleControl.cpp:239, 251-267). With multi-connect, camera A keeps
  the shutter held while camera B reconnects. Fix: on the transition out of
  STATE_ACTIVE, synthesize release commands to still-connected targets from
  a Control-tracked held flag.
- B7. Unsynchronized shared fields in Control. LOW as-is, HIGH once #63
  lands. CONFIRMED writes: m_ConnectCamera written with no lock
  (src/FurbleControl.cpp:182, 187) and left dangling on failed connects
  until disconnect(); PR #63 adds a 250 ms UI-task reader
  (getConnectingCamera, :397-399), and a CameraList::load destroys all
  Camera objects, giving the timer a freed pointer. m_State, backoff flags,
  failcount are plain fields crossing tasks (:150, 223, 241, 330-334,
  401-403). Fix: std::atomic for m_State and m_ConnectCamera, clear
  m_ConnectCamera on the failure path, reset failcount in connectAll, and
  have the #63 timer resolve cameras through a mutex-protected accessor.
- B8. PR #38 multi-connect races. MEDIUM, SUSPECTED, partly acknowledged in
  the PR. getTargets returns raw Target* owned by Control; once PR #62 lets
  the console task clear m_Targets (cmdReboot path), a camerasUpdate tick
  can hold freed pointers. The ACTIVE-check-then-isConnected TOCTOU can
  block the LVGL task on Camera::m_Mutex for a full reconnect attempt,
  which the PM1 converts to a reset. The identical unmitigated pattern
  already exists on fork/master in the diagnostics page
  (src/FurbleUI.cpp:3758-3760). Fix: rows store camera identity, re-resolve
  each tick; B1's lock-free isConnected removes the block; pause the
  diagnostics timer with the reconnect box.
- B9. PM1 watchdog is fed only from the UI task, and legitimate long UI
  blocking exists. HIGH leverage, CONFIRMED structure. watchdogFeed runs
  from UI::task only (src/FurblePlatform.cpp:341-348, 10 s timeout).
  setConnSaver does up to 2 s of HCI per camera on the LVGL task
  (src/FurbleControl.cpp:640-645), up to ~18 s worst case at 9 targets.
  Every UI-blocking defect above becomes a hard reset. Fix: move
  setConnSaver fan-out onto the control task via a command; consider a tiny
  dedicated feeder task that feeds only while a UI heartbeat advances.
- B10. Duplicate cancel callbacks. LOW, CONFIRMED. doConnect registers a new
  cancel event callback per connect (src/FurbleUI.cpp:1952-1953), so cancel
  work multiplies over the session. Register once at UI build time.
- B11. Scan-callback lock order inversion. LOW, SUSPECTED. NimBLE scan
  callbacks take UI::m_Mutex while LVGL handlers under the same mutex can
  enter blocking NimBLE calls (src/FurbleUI.cpp:1888-1890, 2277-2286).
  Narrow window. Fix falls out of B9 (no blocking NimBLE work on the LVGL
  task).
- B12. Adaptive TX control signal is wrong-direction RSSI. LOW to MEDIUM,
  SUSPECTED design caveat. getRssi measures the camera's TX, which does not
  change when furble steps down; the loop is open and bounded only by the
  +3 dBm floor and drop recovery (src/FurbleControl.cpp:552-583). Document
  in plan 11; optionally step up on supervision-timeout disconnects.

### C. WiFi, MQTT, WebUI (feat/33 chain)

- C1. The chain does not compose as committed. HIGH, CONFIRMED integration
  defect, partially documented in plan 33. FurbleWiFi exists only on 33b;
  33c and 33d branch from 33-wifi-hub in parallel, contain zero esp_wifi
  bring-up, and poll networkReady forever (src/FurbleMQTT.cpp:334-347,
  src/FurbleWebUI.cpp:290-303 on 33d). Their sdkconfigs still carry the
  pre-33b netif state (ESP_NETIF_TCPIP_LWIP unset, LOOPBACK set), so the
  restack has real sdkconfig conflict hazard across all six files. PRs #66
  and #90 are unverifiable on hardware until restacked. Fix: rebase 33c
  onto 33b and 33d onto the result with a careful six-file sdkconfig
  reconciliation, drop the duplicated esp_netif_init once main.cpp owns it.
  Settings wire IDs were coordinated (51-55 vs 56-62), no collision.
- C2. WiFi never retries after a blocked scan. MEDIUM, CONFIRMED (33b).
  scanForNetwork sets g_ScanBlocked while a camera is active, the retry
  loop breaks, the worker parks on portMAX_DELAY, and nothing re-notifies
  on camera disconnect (src/FurbleWiFi.cpp:312-318, 385-414). Fix: notify
  the WiFi worker on the Control transition out of STATE_ACTIVE, or retry
  direct connects while blocked.
- C3. No DHCP timeout and no LOST_IP handling. MEDIUM, CONFIRMED (33b).
  Association without a lease waits forever (:423, :449-478);
  IP_EVENT_STA_LOST_IP is not registered. Fix: bounded post-associate wait
  treated as failure, register LOST_IP.
- C4. Unauthenticated HTTP control plane with CSRF exposure. HIGH within
  the LAN threat model, CONFIRMED (90). No auth, no Host check, POST
  handlers parse any Content-Type (src/FurbleWebUI.cpp:421-452), so a
  cross-origin simple request from any web page can fire the shutter or
  rewrite settings including MQTT_URI to an attacker broker; DNS rebinding
  exposes the GETs. Fix, cheap and ordered: require
  Content-Type: application/json on POSTs, validate Host against the
  device address, optional static token setting. Document the remainder.
- C5. MQTT gives up permanently after 10 failures. MEDIUM, CONFIRMED (33c).
  MAX_CONNECT_FAILURES=10 with flat 10 s retries means a ~100 s broker or
  router outage silences the hub until manual intervention
  (src/FurbleMQTT.cpp:490-501). Fix: capped exponential backoff retrying
  forever; keep the hard block only for identified auth failures.
- C6. Secrets readable over the BLE companion service. MEDIUM, CONFIRMED
  (33b, 90). settingValue returns raw WIFI_PSK and MQTT_PASS
  (src/FurbleCompanionService.cpp:316-326 on 33d), inconsistent with the
  console and REST redaction. Fix: return a set/unset indicator for secret
  types, keep writes write-only. Related LOW: MQTT_URI may embed userinfo
  and is printed plaintext.
- C7. MQTT sdkconfig symbols skip the headless env. MEDIUM, CONFIRMED
  (33c/33d). CONFIG_MQTT_PROTOCOL_311 and CONFIG_MQTT_TRANSPORT_SSL are set
  in the five release sdkconfigs only; sdkconfig.esp32-s3-headless lacks
  both, and headless is the intended hub target. Also violates the
  all-envs-consistent sdkconfig rule. Fix: add both symbols to the headless
  config.
- C8. Plan 65 coex recommendations 2 and 3 are unimplemented. MEDIUM,
  CONFIRMED deferred debt. No BLE connection-interval floor above 50 ms
  while WiFi is up (FAST profile is 30-50 ms, lib/furble/Camera.h:283-286)
  and no hub camera-count cap or doc (CONFIG_BT_NIMBLE_MAX_CONNECTIONS=9
  unchanged). Under WiFi traffic sub-50 ms BLE intervals lose coex
  arbitration (IDF issue 15833); the cost is shutter latency jitter, with a
  large supervision-timeout margin making drops unlikely. Fix: add a
  WiFi-aware conn profile at 51.25-60 ms avoiding multiples of 102.4 ms,
  selected while WiFi reports connected, through the existing
  setConnProfile machinery; document 3 cameras supported, 5 tested, in hub
  mode.
- C9. Heap headroom on non-PSRAM boards is unmeasured and the hub is not
  env-gated. MEDIUM, SUSPECTED. MQTT and WebUI init on all five boards
  (33d src/main.cpp:291-292); defaults are off so idle cost is zero, but
  enabling WiFi plus TLS plus httpd on an m5stick-c must fund roughly 45 KB
  driver plus 40 KB TLS peak from an already tight heap. Fix: run the plan
  65 min-free-heap soak before merge; consider gating to S3 envs or setting
  SPIRAM_TRY_ALLOCATE_WIFI_LWIP on S3; full cert bundle (~90 KB flash) can
  drop to the common subset.
- C10. Smaller WiFi-chain defects. LOW, per item: auth-fail
  indistinguishable from AP loss so a wrong PSK retries forever (SUSPECTED,
  src/FurbleWiFi.cpp:431-447); NVS writes and SNTP init inside the
  2304-byte sys_evt task (SUSPECTED, :449-476); esp_wifi_deinit cycling
  without detaching default-netif glue (SUSPECTED, :279-289); CameraList
  loads from the httpd and esp-mqtt tasks race a GUI scan (SUSPECTED,
  src/FurbleWebUI.cpp:507-520, src/FurbleMQTT.cpp:600-643); esp_timer
  callbacks can publish into a client being destroyed (SUSPECTED,
  src/FurbleMQTT.cpp:444-457, 929-935); unlocked failure counters
  (SUSPECTED); cross-surface shutter hold ownership between REST, MQTT and
  UI is uncoordinated but queue-serialized (SUSPECTED, document or add a
  hold owner in Control).

### D. Power, coexistence, brownout

- D1. Brownout margin under WiFi TX bursts on battery is untested. LOW to
  MEDIUM, SUSPECTED. Detector at IDF defaults, no code touches WiFi TX
  power. Fix: battery soak per plan 65 tests 5 and 6; hold
  esp_wifi_set_max_tx_power(44) in reserve for Stick boards if brownouts
  appear.
- D2. Adaptive BLE TX floor while WiFi is up. LOW, SUSPECTED. Camera-side
  RSSI does not see WiFi bursts on the shared antenna, so stepping to
  minimum shaves margin exactly when arbitration losses occur. Fix: floor
  the adaptive range one step higher, or suspend step-down, while WiFi is
  connected. Two-line condition in sampleAdaptivePower.
- D3. IR RMT clock is APB-derived. LOW, SUSPECTED. RMT_CLK_SRC_DEFAULT
  (src/FurbleIR.cpp:217) shifts carrier under DFS unless the IDF 5.x RMT
  driver's own pm lock covers it (likely, unverified). Fix if touched:
  RMT_CLK_SRC_XTAL on S3, matching the GPS UART pattern.

## Remediation plan

Sequenced batches. Each batch is one PR. Within a batch, items share files
and verification. Hardware verification means the X100VI plus a WiFi AP on
the attached M5StickS3, per repo policy; vendor code beyond Fujifilm is code
review plus FauxNY and declared untested.

### Batch 1: connect and disconnect lifecycle (highest risk, do first)

Fixes the brick-class family and unblocks the open BLE PRs.

1. B2 undroppable CMD_DISCONNECT and bounded disconnect.
2. B3 force-complete on disconnect timeout (fix inside PR #62 before it
   merges, or as an immediate follow-up).
3. B4 destroy targets outside the Control mutex.
4. A3a cancel and disconnect checks in the #93 registration wait (fix
   inside PR #93 before merge).
5. B1 lock-free isConnected plus cached companion status.

Dependencies: none. Hardware-verifiable: connect, cancel mid-connect, cancel
mid-registration-wait, disconnect storms, companion status read during
reconnect, all on the X100VI. This batch should land before or with PRs #62
and #93.

### Batch 2: Fujifilm registration and bonds

1. A1 hard-fail FujifilmBasic discovery.
2. A3d atomic m_Configured.
3. A2 stale-bond delete-and-retry on secureConnection failure.
4. A7a FujifilmSecure address update from the rescan match.
5. A3b decide the Basic gate scope after hardware verification.

Dependencies: batch 1 (shares Camera.cpp and the #93 branch).
Hardware-verifiable: delete the pairing on the X100VI body and reconnect
(A2), normal reconnect cycles (A7a), full #93 verification. A3b needs a
Basic-protocol body or the scoped fallback.

### Batch 3: Control concurrency hygiene (code-only)

1. B5 lock the three unlocked m_Targets iterations.
2. B7 atomics for m_State and m_ConnectCamera, failcount reset,
   getConnectingCamera through a locked accessor (coordinate with PR #63).
3. B6 synthesized shutter release on leaving STATE_ACTIVE.
4. B9 setConnSaver fan-out moved to the control task; watchdog feeder
   decision documented.
5. B10 single cancel callback registration.
6. A8 gattRead correctness, Scan callback race, FujifilmSecure scan-end
   window.

Dependencies: batch 1 for the isConnected change. Verification: code review,
FauxNY, TSan-style reasoning; on-device smoke on the X100VI (GPS updates
during disconnect, settings toggles while connected).

### Batch 4: pairing UX and vendor gates

1. A5a passkey entry UI or loud failure, A5b 30 s window, A5c doc (inside
   or after PR #63).
2. A6 answerPairing client race.
3. B8 PR #38 row identity model plus the fork/master diagnostics page fix.
4. A4 Canon and Sony saved-reconnect liveness probes, Canon
   write-with-response.
5. A7b, A7c address rotation notes and bond cleanup.

Dependencies: batch 1 and 3 primitives. Verification: X100VI for the dialog
flows (Fujifilm Secure numeric comparison), FauxNY plus declared-untested
for Canon, Sony.

### Batch 5: wifi-hub restack and STA robustness

1. C1 restack 33c onto 33b and 33d onto 33c, reconcile all six sdkconfigs
   (this is a fan-out-to-linear conversion; rebase each dependent with
   rebase --onto its parent, then verify the netif symbols on every file).
2. C7 headless MQTT sdkconfig symbols.
3. C2 retry after camera disconnect, C3 DHCP timeout and LOST_IP, C10
   auth-fail classification and GOT_IP work moved off the sys_evt task.

Dependencies: none on batches 1-4, but do not merge any of the chain before
this batch. Hardware-verifiable: AP loss, router reboot, wrong PSK, DHCP
blackhole (AP with DHCP disabled), all against the home AP on the S3 or the
headless env.

### Batch 6: hub services hardening

1. C5 MQTT persistent backoff.
2. C4 HTTP POST hardening (Content-Type, Host, optional token).
3. C6 companion secret redaction.
4. C10 CameraList access routing and MQTT timer teardown guard.

Dependencies: batch 5. Hardware-verifiable: broker outage over 100 s, HA
rediscovery, cross-origin POST attempt from a browser, companion read of
WIFI_PSK.

### Batch 7: coexistence and power validation

1. C8 WiFi-aware BLE conn profile and the hub camera-count doc.
2. D2 adaptive TX floor while WiFi is up.
3. C9 heap soak on a non-PSRAM board, env-gating decision.
4. D1 battery brownout soak with WiFi TX.
5. B12 plan 11 doc note, D3 RMT clock note.

Dependencies: batch 5 (needs real WiFi traffic concurrent with a camera
link). Hardware-verifiable: shutter latency A/B with WiFi idle vs MQTT plus
WebUI traffic on the X100VI, per plan 65 test 3; min-free-heap and battery
soaks per plan 65 tests 5 and 6.

## Verification summary

X100VI plus AP covers: all of batch 1 and 2, the dialog flows in batch 4,
all of batch 5 and 6, and the coex latency and soak tests in batch 7.
Code-only (FauxNY, review, declared untested): Canon, Sony, Ricoh changes,
RPA rotation behavior, most of batch 3. Nothing in this plan requires
hardware the project does not have, except a Fujifilm Basic-protocol body
for A3b, which has the scoped fallback.
