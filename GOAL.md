# GOAL: complete camera parity, verified implementation, and minimum power

Status: active execution contract.
Owner: Furble maintainers.
Scope: every camera family Furble registers, every transport those cameras
expose, every action Furble performs, the simulator fleet, and the power
research needed to run the product at the lowest practical energy.

This document is the single definition of done. A feature is not complete
because a dossier describes it. It is complete only when the evidence,
specification, implementation, simulator, tests, and documentation all agree,
and when the remaining hardware-only claims are explicitly capture-gated.

## 1. Mission

Deliver a fully verified, fully documented, implementation-ready model of
every Bluetooth and Wi-Fi feature the supported cameras expose, then make
Furble implement every safe feature, simulate every camera and failure path,
and minimize power consumption without weakening correctness or security.

The result must let a competent implementer add any camera feature without
guessing, and must let a reviewer see exactly which claims are firmware-static,
app-static, hardware-captured, inferred, or unknown.

## 2. Definition of done

Every row in the feature matrix must reach one of two terminal states:

| State | Meaning |
| --- | --- |
| Implemented and verified | Specified, implemented, simulator-covered, host-tested, and either hardware-verified or explicitly marked capture-gated with a named capture. |
| Explicitly out of scope | Proven absent on the camera, unsafe, or impossible with the current hardware, with the evidence and rationale recorded. |

No feature may remain in "partially documented", "TODO", "unknown", or
"probably works" at the end. An unknown is allowed only with a named artifact
or capture that closes it, and the feature must be marked capture-gated in the
UI and docs.

The completion gate is:

1. Every camera family has a current BT API sheet and a Wi-Fi API sheet.
2. Every service, characteristic, command, response, notification, error, and
   state transition in those sheets has an evidence label.
3. Every Furble action is traced to a sheet row, and every mismatch is either
   fixed or recorded as an intentional capture gate.
4. Every implemented feature has a host test and a simulator peer path.
5. Every simulator models success, camera-side rejection, timeout, disconnect,
   stale bond, and malformed response.
6. The power PR contains ranked, tested opportunities with measured or modeled
   savings and no unverified claims.
7. Every family has a usage guide and reference sequence for every implemented
   transport.
8. Every camera feature is classified as implemented, camera-menu-only,
   transport-unavailable, unsafe, or capture-gated in a per-camera parity
   matrix.
9. Every family has a current official-app analysis and a versioned app
   register, with no unverified app claim promoted to camera behavior.
10. All validation commands pass and the two PRs are reviewable independently.

## 3. Current baseline

The starting point is not empty. It is the current private corpus and PR.

- Private corpus: `MaxRink/furble-camera-firmware-re`, head `7a7f629` or later.
- Current audit PR: `MaxRink/furble#317`, branch `firmware-compat-audit`.
- Current corpus assets: seven vendor BT API sheets, pairing and state-machine
  references, a full capability blueprint, handler-level notes for Fujifilm,
  Ricoh, and Nikon B600, container and crypto notes for Canon, Sony, and
  Panasonic, and 60 private release assets plus six Git LFS images.
- Current coverage gate: `tooling/spec_coverage.py` exits 0 with zero missing
  UUIDs for every family.
- Current tests: 120 host tests and 192 Python tooling tests pass.

Known limits that this goal must close or capture-gate:

- No physical capture exists for Ricoh, Canon, Nikon, Sony, LUMIX, or DJI.
- Canon FIR and FI2 payloads are cryptographically opaque.
- Nikon Z6 III 1.02 and 2.01 bodies are encrypted.
- Panasonic UPD code partitions are encrypted.
- Sony ZV-1F FDAT is encrypted. ILCE-7RM5 is decrypted.
- DJI publishes no Action 4 or Action 5 Pro camera image.
- Furble currently has only Fujifilm and Ricoh virtual camera peers.
- Existing power plans 96, 98, 98b, and 158 establish the starting analysis but
  do not close the power question.

## 4. Evidence rules

Every claim uses one label:

- `firmware-static`: read from a shipped image with an offset or reproducible
  command.
- `app-static`: read from an official application with a class, string, or
  offset.
- `hardware-capture`: observed on a real link, with a raw trace or a named
  author-reported capture.
- `public-source`: a vendor document, SDK, sample, or public implementation.
- `official-semantic`: a vendor documents the user-visible feature but not the
  private bytes.
- `inference`: reasoned from the other labels.
- `unknown`: unresolved, with a named closing artifact.

Non-negotiable rules:

1. Static presence is not runtime reachability.
2. A successful ATT write is not a physical capture.
3. A UUID table is not a handler map.
4. An official app is client code, not camera behavior.
5. A power model estimate is not a measurement.
6. No feature is promoted to supported hardware without a capture or an
   explicit capture gate.
7. No firmware is modified or flashed.

## 5. Workstreams

### A. Verify the current state

Goal: prove that every existing claim, hash, offset, table, and test is still
true at the execution commit.

Tasks:

1. Recompute every firmware hash and compare `firmware/SHA256SUMS`,
   `sources.json`, `release_assets.json`, and the private release assets.
2. Re-run `tooling/spec_coverage.py`, `tooling/rebuild_manifest.py`, and the
   cross-reference checks.
3. Re-run all host, Python, clang-format, and simulator checks.
4. Audit every `firmware-static` address in the handler notes against the
   current images.
5. Audit every `app-static` class, string, and offset against the retained APKs.
6. Reconcile contradictions between dossiers, specs, diagrams, and code.
7. Produce `analysis/state-verification.md` with a row per claim and a verdict:
   confirmed, corrected, stale, or unprovable.

Exit gate: zero stale claims, zero unresolved contradictions without a named
capture, and every correction applied to the corpus and to Furble.

### B. Complete the Bluetooth specification

Goal: every camera BT surface is specified to implementation quality.

Per family: Fujifilm Basic and Secure v4.0/v4.1, Ricoh GR III/IIIx/IV/HDF,
Canon EOS Smart and Remote, Nikon Remote and Smart, Sony, Panasonic MEI0 and
APS, DJI Osmo Action 4/5 Pro.

Each sheet must include:

1. GAP: every advertisement form, scan response, address type, name rule,
   company ID, manufacturer layout, service UUID list, connectable flag,
   wake form, and reconnect form.
2. SMP: pairing method, MITM, LE Secure Connections, bonding, key
   distribution, identity, stale bond, camera-side deletion, and retry policy.
3. ATT: MTU, write with and without response, notification, indication, and
   every documented error.
4. GATT: every service and characteristic, including standard services and
   unused vendor entries, with properties, maximum length, CCCD, and security.
5. Commands: every opcode, payload field, endianness, unit, range, valid state,
   expected response, failure value, timeout, and retry.
6. Notifications: every asynchronous message, length, layout, meaning, and
   required controller action.
7. Feature coverage: still, focus, bulb, self-timer, interval, burst, movie,
   zoom, power, wake, sleep, live view, transfer, clock, GPS, settings,
   backup, firmware update, pairing management, battery, storage, and model
   specific controls.
8. Unknowns: one row per unresolved item with the exact capture or artifact.

Exit gate: every sheet passes the coverage checker and a hostile review finds
no unsupported claim.

### C. Complete the Wi-Fi specification

Goal: document every Wi-Fi surface with the same rigor as Bluetooth.

Per family:

| Family | Required Wi-Fi surfaces |
| --- | --- |
| Fujifilm | EXWIFI discovery, camera AP join, HTTP and PTP/IP command plane, image list and transfer, live view, remote shooting, settings, firmware update, pairing handover. |
| Ricoh | GR II/III/IIIx/IV AP mode, station mode limits, `/v1/` endpoints, props, photos, liveview, shoot, focus, device finish, WLAN finish, image transfer, firmware update. |
| Canon | Bluetooth-to-Wi-Fi handover service `00020000`, AP configuration, Camera Connect session, PTP/IP or HTTP command plane, image transfer, live view, remote shooting, firmware update. |
| Nikon | SnapBridge Wi-Fi handover, AP join, PTP/IP command plane, image transfer, live view, remote photography, clock, firmware update. |
| Sony | PTP/IP still capture, live view, image transfer, CC00 Wi-Fi info, handover, settings, firmware update. |
| Panasonic | Wi-Fi start characteristic, AP join, HTTP/DLNA command plane, image transfer, live view, remote shooting, settings, firmware update. |
| DJI | Mimo Wi-Fi provisioning, live view, media, settings, firmware update, BLE credential exchange. |

Each Wi-Fi sheet must include:

1. Discovery and association: SSID and passphrase source, AP versus station
   role, channel, security mode, DHCP, address, and generation differences.
2. Session setup: handshake, authentication, token, nonce, HTTP headers,
   PTP/IP handshake, and teardown.
3. Every endpoint or opcode: method, path, query, request body, response body,
   status codes, error shapes, pagination, and content type.
4. Every stream: live view format, frame rate, resolution, encoding, and
   termination.
5. Transfer: image list, metadata, thumbnail, original, delete, and resume.
6. Settings: read and write schema, ranges, and persistence.
7. Firmware update: transport, manifest, signature, and failure handling.
8. Timing, retries, and power behavior.
9. Unknowns with closing artifacts.

Exit gate: a client can be implemented from the sheet without reading the
vendor app again.

### D. Verify and fix every Furble action

Goal: every action Furble performs matches the evidence.

Build a traceability table:

| Furble action | Vendor and family | Code path | Spec row | Expected camera response | Current behavior | Verdict |
| --- | --- | --- | --- | --- | --- | --- |

Build a per-camera parity matrix:

| Camera | Feature | Transport | Spec row | Furble state | Simulator | Test | Hardware state |
| --- | --- | --- | --- | --- | --- | --- | --- |

`Furble state` is one of implemented, missing, capture-gated, unavailable, or
out of scope. Camera-menu-only features are recorded as menu-only, not as
missing protocol work. `Hardware state` is verified, capture-gated, or not
applicable.

Cover at minimum:

- scan, match, connect, secure, register, reconnect, stale bond, disconnect.
- shutter, focus, bulb, self-timer, interval, movie, zoom, power, wake.
- GPS enable, GPS packet, clock, timezone, sync interval.
- image transfer, live view, settings, firmware update where implemented.
- every error path, timeout, retry, and cancellation.

Every mismatch becomes one of:

- safe fix with firmware or official-app evidence,
- capture-gated change with a UI warning and a named capture,
- explicit removal if the camera does not expose the feature.

Exit gate: no action remains unclassified, and every safe fix has a regression
test.

### E. Simulator parity and new simulators

Goal: the simulator fleet models every family and every failure class.

Current peers:

- `lib/testing/peer/FujifilmVirtualCamera.*`
- `lib/testing/peer/RicohVirtualCamera.*`

Required new peers:

- `CanonSmartVirtualCamera` and `CanonRemoteVirtualCamera`.
- `NikonRemoteVirtualCamera` and `NikonSmartVirtualCamera`.
- `SonyVirtualCamera`.
- `LumixVirtualCamera`.
- `DjiVirtualCamera`.

Each peer must model:

1. Every advertisement form and model gate.
2. Every required and optional service and characteristic.
3. Every command, response, notification, and error.
4. Pairing, bonding, SMP method, stale bond, camera-side deletion.
5. State transitions: boot, idle, capture, playback, movie, sleep, wake,
   transfer, firmware update.
6. Fault injection: timeout, disconnect, malformed frame, rejected write,
   busy, full card, low battery, wrong mode.
7. Timing and retry behavior consistent with the spec.
8. A deterministic scripted mode for end-to-end tests.

Refine existing peers with the handler findings:

- Fujifilm: shutter values `0`, `1`, `2` only; focus `01 00`, release `00 00`;
  NOT1 as time request; 20-second registration window.
- Ricoh: power write `0` or `1` only; GEO Tag `0` or `1`; GPS first 31 bytes;
  GR III passkey versus GR IV numeric comparison; drive mode table.
- Nikon: `2083` short press `02 02`, long press `02 03`, no-op `02 00`;
  `2084` key/status; Blowfish stages.
- Sony, LUMIX, DJI: model the confirmed firmware and app surfaces and mark
  the unconfirmed opcodes as rejected or capture-gated.

Exit gate: every family has a peer, every peer has success and failure tests,
and the simulator scenarios manifest is updated and validated.

### F. Power research, separate PR

Goal: find and prove the lowest practical power configuration.

This work raises its own PR, separate from camera parity. It may touch code
only after a finding is measured or modeled and reviewed.

Parallel research lanes, each run by a separate subagent and writing a separate
file before consolidation:

| Lane | Scope | Output |
| --- | --- | --- |
| BLE radio | Scan, advertise, connection parameters, PHY, reconnect, NimBLE | `docs/power/ble.md` |
| SoC and OS | DFS, light sleep, CPU frequency, tasks, locks, logging, console | `docs/power/soc.md` |
| Display and UI | Brightness, timeout, refresh, backlight, LVGL cost | `docs/power/display.md` |
| GPS and IMU | Fix rate, constellations, UBX/NMEA, UART, motion wake | `docs/power/gps-imu.md` |
| Wi-Fi and coexistence | AP and station power, transfer choice, coexistence | `docs/power/wifi-coex.md` |
| PMIC and board | Rails, charger, gauge, battery, leakage, datasheets | `docs/power/board-power.md` |
| Measurement | Bench method, instruments, scenarios, thresholds | `docs/power/measurement.md` |

The coordinator merges the lanes into `docs/power/opportunity-matrix.md` and
rejects any row without a labeled evidence source and an estimated saving.

Research areas:

1. BLE scan duty cycle, window, interval, passive versus active, duplicate
   filtering, and match-first behavior.
2. BLE connection interval, latency, supervision timeout, PHY, and data
   length per camera family.
3. Advertising and reconnect cadence, idle disconnect, and wake policy.
4. NimBLE buffers, task priorities, stack size, and modem sleep.
5. ESP32 light sleep, DFS, CPU frequency, and peripheral clock gates.
6. Display brightness, timeout, partial refresh, and backlight rails.
7. GPS duty cycle, fix rate, constellation set, SBAS, baud rate, UART wake,
   backup mode, cold versus warm start, and assisted data.
8. IMU polling, motion wake, and gesture thresholds.
9. Wi-Fi coexistence and Wi-Fi versus BLE transfer choices.
10. PMIC rails, battery chemistry, board-specific leakage, and datasheet
    limits for every supported board.
11. Logging, console, and USB-serial power cost.
12. Reconnect storms, retry backoff, and error-path residency.

Required artifacts:

- `docs/power/opportunity-matrix.md`: every opportunity, evidence, estimated
  saving, risk, and implementation cost.
- `docs/power/measurement-plan.md`: bench setup, instruments, scenarios, and
  acceptance thresholds.
- `docs/power/datasheet-register.md`: every datasheet, revision, and relevant
  limit used.
- A ranked implementation list with safe-now, needs-measurement, and rejected
  items.
- Tests for every implemented change and a simulator power scenario.

Exit gate: the power PR is independently reviewable, does not mix camera
feature work, and every number is labeled measured, modeled, or inferred.

### G. GPS and library reverse engineering for power

Goal: understand the actual GPS and support libraries, not only Furble's use.

Tasks:

1. Reverse engineer the GPS receiver protocol and configuration messages.
2. Document NMEA and UBX or proprietary message support, update rates,
   constellations, SBAS, power modes, and backup modes.
3. Audit TinyGPSPlus and FurbleGPS parsing cost, buffering, and wakeups.
4. Audit I2C, SPI, UART, and GPIO libraries used by the camera and power paths.
5. Identify the lowest-power fix mode that still satisfies Furble's geotag
   accuracy and latency requirements.
6. Document antenna, ground plane, and interference constraints from the
   datasheets and board files.

Exit gate: a recommended GPS power profile exists with evidence, and every
unverified receiver behavior is capture-gated.

### H. Datasheet and resource audit

Goal: ground the power and hardware claims in primary documents.

Collect and index:

- ESP32-S3, ESP32-C3, and any other fitted MCU datasheets and errata.
- PMIC, charger, fuel gauge, and battery datasheets.
- GPS, IMU, display, backlight, and radio module datasheets.
- Board schematics, pin maps, and FCC or certification filings.
- Vendor camera manuals and firmware release notes for power and wake behavior.
- Bluetooth Core specification sections used by GAP, SMP, and ATT.

Every claim in the power PR must cite a document, a measurement, or a labeled
inference.

### I. Usage guides and reference clients

Goal: every API sheet has a usable integration guide, not only a byte table.

Per family and transport:

1. A quick-start sequence with the minimum steps to connect, register, and take
   one picture.
2. A complete sequence for every major feature: focus, bulb, self-timer,
   interval, movie, zoom, power, wake, geotag, clock, transfer, live view,
   settings, backup, and firmware update.
3. Copy-ready request and response examples with byte-level test vectors.
4. Error handling: every status code, ATT error, timeout, disconnect, busy,
   and stale-bond path, with the recommended recovery.
5. Security notes: what must be bonded, what must be registered, what can be
   cached, and what must never be cached.
6. A minimal reference client per family, either a host test peer or a
   documented pseudocode sequence, that exercises the sheet.
7. Migration notes for generation differences and firmware cutovers.
8. A troubleshooting table mapping symptoms to the likely protocol cause.

Exit gate: a new contributor can implement a working family client from the
sheet and guide without reading vendor apps or firmware.

### J. Blocked-artifact research and external evidence

Goal: before any item is declared blocked, prove that the public record has
been searched, that public tools were tested, and that the exact missing
artifact is known.

This workstream runs in parallel with A through I. It is mandatory for every
encrypted image, every missing firmware version, every unproven runtime claim,
and every camera feature that has no capture.

#### J.1 Search protocol

For each blocker, search and record at least these source classes:

1. Code hosts: GitHub, GitLab, Codeberg, Bitbucket, SourceHut.
2. Firmware and reverse-engineering communities: CHDK, Magic Lantern, XDA,
   DPReview, PentaxForums, EOSHD, NikonGear, Sony Alpha forums, LUMIX forums,
   RCGroups, OpenRC, Hackaday, and public Discord or IRC archives.
3. Archives: Wayback Machine, archive.org, vendor support archives, and
   regional vendor sites.
4. Regulatory filings: FCC, CE, VCCI, MIC, and equivalent databases.
5. Patents and standards: Bluetooth SIG, Wi-Fi Alliance, PTP/IP, DLNA, USB,
   and vendor patent filings.
6. Service manuals, board photos, teardown reports, and repair guides.
7. Academic papers, security conference talks, and vulnerability write-ups.
8. Vendor SDKs, sample repositories, GPL or open-source releases, and
   published protocol guides.

Record the exact query, date, and result. "No result" is only valid with the
queries listed. Test public tools against the retained images before declaring
an image opaque.

#### J.2 Blocker research tasks

| ID | Family or blocker | Research task | Required output |
| --- | --- | --- | --- |
| B1 | Panasonic S5II, BGH1, G9II UPD and BLE | Search for public UPD parsing, AES-CTR keys, rootfs extraction, BLE UUIDs, MEI0 and APS documentation. Check OpenGH5, lumix-fullframe-reverse, LUMIX-G9II-Remote-Control, gpxcam, LUMIX Sync, and any newer project. Test every public key and tool. | `analysis/panasonic-external-evidence.md` with tools tried, results, and remaining key or dump. |
| B2 | Panasonic Wi-Fi and HTTP | Search for S5II, BGH1, and G9II Wi-Fi API documentation, DLNA endpoints, live view, transfer, and settings. | `analysis/panasonic-wifi-external.md` and a Wi-Fi sheet update. |
| B3 | Canon FIR v5 and v6 | Search for Digic 8 and Digic 10 extraction, EOS RP and R6 Mark II ROM dumps, `d8_oracle`, FIR decryption, and Canon service tools. Test public decryptors. | `analysis/canon-external-evidence.md` with the exact key or dump still needed. |
| B4 | Canon FI2 | Search for per-model FI2 keys, CHDK ports, M6 and G9 X Mark II dumps, and FI2 container documentation. | `analysis/canon-fi2-external.md`. |
| B5 | Nikon Z6 III 0x260 | Search for Nikon firmware decryption, Z6 III dumps, 0x260 container notes, SnapBridge protocol analysis, nsg, birdcam, and Nikon service tools. | `analysis/nikon-z6iii-external.md`. |
| B6 | Sony ZV-1F FDAT | Search for the ZV-1F model key, CXD90045 and CXD90057 keys, UFU and FDAT tools, Sony firmware extraction, and ILCE-7RM5 reproduction. | `analysis/sony-zv1f-external.md`. |
| B7 | DJI Action 4 and Action 5 Pro | Search for OTA packages, camera images, Mimo protocol analysis, DJI SDK extensions, FCC filings, teardowns, and public reverse projects. | `analysis/dji-external-evidence.md` with a feasible capture or OTA method. |
| B8 | Hardware captures, all families | Build a capture plan and a source list for HCI snoop, nRF Sniffer, Wi-Fi monitor mode, USB, and camera logs. Identify owners and hardware. | `docs/captures/capture-plan.md` with per-family traces and acceptance criteria. |
| B9 | Missing firmware versions | Search vendor archives, Wayback, regional sites, and mirrors for missing versions, especially Fujifilm pre-cutover, Ricoh GR IIIx, Canon older releases, Sony older releases, and Panasonic older releases. | `docs/research/firmware-acquisition-backlog.md`. |
| B10 | Camera-menu-only features | Search manuals, service manuals, patents, and teardown reports for features that have no transport API but matter for parity. | Feature rows marked menu-only with evidence. |
| B11 | Unproven runtime claims | For every `unknown` in the specs, search for a public capture or a reproducible test before scheduling a hardware capture. | Updated unknown row with the search result. |
| B12 | Power and GPS datasheets | Search for every fitted MCU, PMIC, GPS, IMU, display, radio module, and battery datasheet, plus errata and application notes. | `docs/power/datasheet-register.md`. |

#### J.3 Blocked-artifact register

Maintain one register at `docs/research/blocked-artifact-register.md`:

| ID | Family | Blocker | What it blocks | Sources searched | Findings | Closing artifact | Owner | Status | Date |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |

Status values: open, public-artifact-found, tool-tested-negative,
needs-hardware, needs-vendor-key, closed.

Rules:

1. A blocker is not final until the row lists the queries, the tools tried, and
   the exact artifact that would close it.
2. If a public project exists, import its evidence with hash, license, and
   provenance. Do not copy unlicensed code.
3. If a public tool fails, record the version, input hash, and error.
4. If the only path is a hardware dump or vendor key, say so explicitly and
   mark the feature capture-gated in Furble.
5. Re-run the search when a new firmware, app, SDK, or public project appears.

#### J.4 Priority order

1. Panasonic UPD and BLE, then Panasonic Wi-Fi.
2. Canon FIR and FI2, then Canon Wi-Fi.
3. Sony ZV-1F FDAT, then Sony Wi-Fi.
4. Nikon Z6 III, then Nikon Wi-Fi.
5. DJI OTA and Mimo.
6. Hardware captures for all families.
7. Missing firmware versions.
8. Menu-only and power datasheet research.

### K. Official Android app analysis and version tracking

Goal: every camera family has a current, versioned analysis of its official
Android apps, used as context and reference for the protocol, Wi-Fi, model
gates, UI semantics, and error handling.

Apps are client code. They prove what the vendor client expects and sends; they
do not prove what the camera accepts. Every app finding is labeled `app-static`
and cross-checked against firmware or a capture before it becomes a camera
claim.

#### K.1 Required app inventory

| Family | Apps | Required analysis |
| --- | --- | --- |
| Fujifilm | XApp, Camera Remote | BLE Secure and Basic, Wi-Fi EXWIFI, pairing, geotag, clock, image transfer, firmware update, model table, cutover versions |
| Ricoh | Image Sync, GR WORLD | BLE GATT definitions, GPS, GEO Tag, WLAN, image transfer, live view, settings, GR IV-only surfaces |
| Canon | Camera Connect | Smart and Remote BLE, Wi-Fi handover, PTP/IP or HTTP, GPS, livestream, model gates, firmware update |
| Nikon | SnapBridge | Smart BLE and Classic handoff, Wi-Fi, PTP/IP, pairing crypto, clock, GPS, image transfer, firmware update |
| Sony | Creators' App, Imaging Edge Mobile | CC00, DD00, EE00, BB00, FF00, PTP/IP, location, pairing, transfer, settings, firmware update |
| Panasonic | LUMIX Sync | MEI0 and APS, BLE control, Wi-Fi start, HTTP or DLNA, GPS, clock, transfer, live view, settings, firmware update |
| DJI | Mimo | BLE provisioning, R SDK context, Wi-Fi live view, media, settings, OTA, model table |

#### K.2 Required evidence per app

1. Package name, version, versionCode, APK or XAPK hash, signer certificate
   hash, split structure, and acquisition source.
2. Static and semi-dynamic decompilation: DEX, native libraries, Flutter
   assets, resources, manifests, and any packed or encrypted payload.
3. Every BLE UUID, advertisement parser, model gate, command table, payload
   builder, notification handler, pairing state, and error string.
4. Every Wi-Fi endpoint, header, token, request body, response parser, stream,
   transfer path, and firmware update flow.
5. Every settings schema, range, enum, default, and persistence rule.
6. Every model table and feature gate, with the exact constants.
7. Every UI flow that reveals a protocol sequence or a camera precondition.
8. A version comparison against the previous retained release, with changed
   constants and cutovers.
9. A list of assumptions that the app cannot prove and the firmware or capture
   needed.

#### K.3 Version tracking

Maintain `docs/research/android-app-register.md`:

| Family | App | Version | Version code | SHA-256 | Signer | Retained | Analysis file | Last reviewed |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |

Rules:

1. Retain at least the current release and one older release per app.
2. Re-run the analysis when a new version appears, and diff the BLE, Wi-Fi,
   model, and settings constants.
3. Keep app artifacts in the private corpus or an ignored workspace. Never
   commit proprietary APKs or decompiled code to Furble.
4. Cite class names, method names, string literals, offsets, and hashes.
5. Do not copy code from an app into Furble. Use it as evidence only.
6. If an app is packed, AppGuard-protected, split, or Flutter-based, record the
   exact recovery method and the payload hash.
7. If an app has no BLE or Wi-Fi surface for a family, record the absence and
   the search method.

#### K.4 Cross-checks

Every app-derived claim must be checked against at least one of:

- a firmware handler or table,
- a hardware capture,
- a public implementation with a compatible license,
- a vendor document.

If none exists, the claim stays `app-static` and the feature is capture-gated.

## 6. Execution phases

### Phase 0: verify and freeze

- Recompute hashes, rerun tests, reconcile claims, and tag the verified
  corpus commit.
- Output: `analysis/state-verification.md` and a clean coverage report.

### Phase 1: close firmware and app evidence

- Finish handler maps where code is readable.
- Record exact obstructions where it is not.
- Refresh the official Android app analysis and version register for every
  family, and diff the current release against the retained older release.
- Acquire missing firmware or app versions where legally and technically
  possible.
- Run the blocked-artifact research protocol and update the register.
- Output: complete handler, container, and app notes with no stale offsets.

### Phase 2: complete BT and Wi-Fi specs

- Finish every family sheet.
- Add Wi-Fi sheets for every family.
- Add flow and state diagrams for every new surface.
- Output: specs pass the coverage checker and hostile review.

### Phase 3: implement and fix

- Apply safe fixes from the traceability table.
- Add regression tests.
- Keep capture-gated features behind explicit UI gates.
- Output: every Furble action classified, all safe fixes merged.

### Phase 4: simulator parity

- Refine Fujifilm and Ricoh peers.
- Add Canon, Nikon, Sony, LUMIX, and DJI peers.
- Add success, rejection, timeout, disconnect, malformed, and power scenarios.
- Output: simulator manifest updated and all scenario tests pass.

### Phase 5: power research PR

- Run the opportunity matrix, datasheet audit, and measurement plan.
- Implement only measured or strongly modeled safe changes.
- Output: separate power PR with ranked results and tests.

### Phase 6: final verification

- Run the full host suite, Python suite, clang-format, simulator scenarios,
  coverage gate, and firmware hash checks.
- Reconcile both PRs and the private corpus.
- Output: mergeable PRs with no unexplained failures.

## 7. PR strategy

| PR | Scope | Must not include |
| --- | --- | --- |
| PR A, current audit `#317` | Evidence corrections, safe BT fixes, pairing and state references, camera research | Power changes, broad simulator rewrites |
| PR B, camera parity | Wi-Fi specs, remaining BT fixes, traceability, simulator peers, implementation gaps | Power research |
| PR C, power | Power opportunity matrix, datasheet register, measurement plan, safe power changes | Camera feature work |

Each PR updates its own plan document and the matching docs. Each PR states
which claims are hardware-verified and which are capture-gated.

## 8. Acceptance metrics

- BT API coverage: 100 percent of known UUIDs and short UUIDs have a row.
- Wi-Fi API coverage: every documented endpoint, opcode, and stream has a row.
- Furble action traceability: 100 percent classified.
- Per-camera parity matrix: every camera feature classified.
- Official app register: every family has a current version, hash, signer,
  analysis file, and one-version diff.
- Usage guides: every family and transport has a quick start, full sequences,
  test vectors, and an error guide.
- Simulator coverage: every family has a peer and at least one failure path.
- Tests: all host, Python, simulator, and formatting gates pass.
- Power: every opportunity has an evidence label and an estimated saving.
- Documentation: no stale claim, no broken hash, no missing index entry.
- Review: a hostile reviewer can reproduce every firmware-static address and
  every app-static offset.

## 9. Explicit non-goals

- Flashing or modifying vendor firmware.
- Claiming runtime behavior from static presence.
- Copying code from unlicensed or copyleft sources into Furble.
- Supporting a camera feature that the camera does not expose.
- Trading security for power without an explicit, reviewed decision.
- Mixing the power PR with camera parity work.

## 10. Reporting

Every workstream reports:

1. What was verified, with the command or artifact.
2. What changed, with file paths.
3. What remains unknown, with the closing capture.
4. Which tests ran and their result.
5. Which claims are now safe to state publicly.

The goal is complete only when sections 2 and 8 are satisfied, not when the
documentation merely looks complete.
