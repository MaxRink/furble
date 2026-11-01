# Camera parity matrix and Furble action traceability

Status: implementation and capture work list for GOAL Workstream D and E.
Owner: Furble maintainers.
Execution pins: Furble `fa9c9581e4261b6a81afe46baf588ef5d74a7609` on branch
`firmware-compat-audit`, working tree clean. Private corpus
`MaxRink/rea-camera-firmware` at `a596f50` plus uncommitted Wi-Fi sheets that
this document does not use; only the Bluetooth sheets, the two diagram files,
and `docs/research/feature-gap-matrix.md` were read.

This document answers two questions.

1. For every action Furble performs, does the code match a spec row, and if not,
   is the mismatch fixable now, blocked on a capture, or a feature the camera
   does not expose?
2. For every camera family and every feature named in the goal, what is the
   Furble state, the simulator state, the test state, and the hardware state?

No claim here promotes a static artifact to runtime support. An ATT write
response is not a photograph. A UUID table is not a handler map. A successful
`secureConnection()` is not proof that the camera stored a registration.

## 0. How to read this matrix

### 0.1 Evidence labels

| Label | Meaning in this document |
| --- | --- |
| `firmware-static` | Read from a shipped vendor image with a named offset, table, or symbol by the private corpus. Not runtime observation. |
| `app-static` | Read from an official Android application by class, constant, or string. Shows what the client expects, not what the camera accepts. |
| `hardware-capture` | Observed on a real link and published by a cited party, or recorded in the Furble bench traces named in the corpus. No new capture was made for this document. |
| `public-source` | A vendor document, SDK, sample, or public implementation pinned in the corpus. |
| `official-semantic` | The vendor documents the user-visible feature but not the private bytes. |
| `inference` | Reasoned from the labels above, not directly observed. |
| `unknown` | Unresolved, with the named capture or artifact that would close it. |
| `furble-source` | Read from this repository's C++ at the execution pin. A sub-label of `public-source` used so code claims are never mistaken for camera claims. |

### 0.2 Verdict values for Table 1

| Verdict | Meaning |
| --- | --- |
| aligned | The code path matches the cited spec row and its evidence. No change required. |
| safe-fix-needed | A protocol-level mismatch with firmware-static or app-static evidence, fixable without a new physical capture, and requiring a regression test. |
| capture-gated | The code exists or a change is justified, but ordering, reachability, acceptance, or physical outcome cannot be settled without the named capture. The feature must be labelled capture-gated in the UI and docs. |
| unsupported | The camera does not expose the feature, or the code sends a value the evidence shows is a different command. The disposition is removal or a hard refusal, not a new implementation. |
| out-of-scope | Deliberately excluded: unsafe, impossible with the current transport, or a vendor-controlled surface that must not be reproduced. |

### 0.3 State values for Table 2

`Furble state`:

| State | Meaning |
| --- | --- |
| implemented | A code path exists and is reachable through the UI or the documented control path. |
| missing | The camera surface is specified well enough to code, and no code exists. |
| capture-gated | Code exists, or is deliberately withheld, because a named capture is required before the behaviour can be claimed. |
| unavailable | The camera exposes no usable surface, or the route to it is blocked by a missing transport (for example Nikon Smart's Bluetooth Classic continuation), so no Furble code can complete the action. |
| out of scope | Proven absent, unsafe, or impossible with this hardware. |

`Hardware state`:

| State | Meaning |
| --- | --- |
| verified | A named capture in the corpus covers the wire exchange for this row. Where the physical outcome (a photograph, an EXIF tag, a lens movement) is not captured, the cell says so explicitly. |
| capture-gated | No capture covers this row, or the capture covers a different body or firmware. |
| not applicable | The camera has no such surface, or the feature is software-only. |

Camera-menu-only features are recorded as `unavailable` with `menu-only` in the
feature cell, never as missing protocol work.

### 0.4 Family abbreviations

| Code | Family |
| --- | --- |
| `FJ-B` | Fujifilm Basic (pre-July-2025 bodies) |
| `FJ-S40` | Fujifilm Secure v4.0 (X100VI 1.31, X-S20 3.30, X-T5, X-T50) |
| `FJ-S41` | Fujifilm Secure v4.1 (X-E5 1.12, GFX100RF) |
| `RC3` | Ricoh GR III / GR IIIx / HDF |
| `RC4` | Ricoh GR IV / GR IV HDF |
| `CN-S` | Canon EOS Smart (Camera Connect role) |
| `CN-R` | Canon EOS Remote (BR-E1 role) |
| `NK-R` | Nikon Remote (ML-L7 style) |
| `NK-S` | Nikon Smart (SnapBridge LSS style) |
| `SO` | Sony Camera Remote and Remote Commander |
| `LX` | Panasonic LUMIX MEI0 and APS |
| `DJI` | DJI Osmo Action 4 and Action 5 Pro |
| `FN` | FauxNY software peer |

## 1. Summary

### 1.1 Counts

Table 1 has `208` rows: `74` aligned,
`25` safe-fix-needed, `77` capture-gated,
`18` unsupported, and `14` out-of-scope.

Table 2 has `316` rows across 13 camera entries:
`77` implemented, `122` missing,
`3` capture-gated, `83` unavailable,
`31` out of scope; hardware `8` verified,
`230` capture-gated, `78` not applicable.

The action list required by the goal is fully classified. The largest single
class of gaps is not a missing byte sequence. It is that Furble either hides a
feature the camera exposes (movie, interval, self-timer, clock, power) or shows a
control the camera cannot honour (bulb and focus on Ricoh, focus on LUMIX,
release on Nikon Remote, focus on Nikon Smart). The second largest class is
save/reconnect identity: Ricoh on a saved reconnect, Sony, LUMIX, and DJI have
no camera-side bond deletion detection, and LUMIX and DJI also save an address
that can rotate.

### 1.2 Highest-value safe fixes

Ranked by user value first, then evidence strength. Each row is implementable
from `firmware-static` or `app-static` evidence without a new physical capture,
and each needs a host or simulator regression test.

| Rank | Fix | Family | Evidence | Why it is high value |
| --- | --- | --- | --- | --- |
| S1 | Remove the LUMIX focus mapping. `focusPress` currently writes `01` (Confirm) and `focusRelease` writes `02` (Start). There is no focus opcode. | LX | `app-static` panasonic.md C06, C07, F02 | A focus gesture currently changes the camera session state instead of focusing. |
| S2 | Add the LUMIX session handshake: `01` Confirm, wait for `7be5fe6e` `00`, `02` Start, and `03` Finish on disconnect. | LX | `app-static` panasonic.md C06 to C08, F01, F40 | Without it every Furble session starts without the app's documented gate, and the shutter is a bare `04`. |
| S3 | Add the LUMIX GPS stop packet (`daff1bc0`, fix `0x56`, lat and lon `0x7fffffff`, altitude `0x7fff`) and wait for the `37701b80` / `4cf487c0` readiness notifications before the clock and GPS writes. | LX | `app-static` panasonic.md C14, F41, F45; `public-source` S5 report | A stale position can persist, and the shortened MEI0 to GPS sequence is reported to drop the link. |
| S4 | Write the Fujifilm 12-byte UTC and timezone packet on the `F9150137` `01 00` request, and write the client name with a trailing NUL. | FJ-S40, FJ-S41 | `firmware-static` fujifilm.md C28 and C03; `app-static` XApp and Camera Remote | Clock and timezone are a user-visible parity gap; the NUL matches both official clients. |
| S5 | Send the Fujifilm `AAB609C4` location sync enable, not only the interval, and read it back once. | FJ-S40, FJ-S41 | `firmware-static` fujifilm.md C25; `app-static` XApp | The interval is written but the geotag feature may never be enabled on a body whose menu default is off. |
| S6 | Make the Canon Smart GPS timestamp an absolute UTC epoch instead of `mktime` on the runtime timezone. | CN-S | `app-static` canon.md CS-34; `furble-source` CanonEOSSmart.cpp | The same fix removes a whole class of non-UTC field failures, and the rest of the 20-byte packet already matches. |
| S7 | Add the Sony GPS sequence: subscribe `DD01`, read `DD21` and choose 91 or 95 bytes, read `DD32` and `DD33`, fill UTC offset and DST, and release `DD30` and `DD31` when GPS stops. | SO | `app-static` sony.md C-20 to C-30, F-21, F-23 | The current write is a partial packet with no lock release, which leaves the camera locked to Furble. |
| S8 | Add the Ricoh Date Time write on connect, gated on a fresh Operation Mode `CAPTURE` read, and read it back. | RC3, RC4 | `firmware-static` ricoh.md C-19 and CM-03; `app-static` Image Sync and GR WORLD | Clocks are the most visible parity gap on a camera Furble already drives well. |
| S9 | Write the Ricoh Paired Device Name (`FE3A32F8`, UTF-8) after the first connect. | RC3, RC4 | `app-static` ricoh.md C-76 and CM-15 | The camera's paired-device list currently shows an unnamed entry. |
| S10 | Decode Ricoh Capture Status and use a `Capturing=1` then `0` transition as the shot-confirmation signal. | RC3, RC4 | `firmware-static` ricoh.md C-56; `public-source` field table | Removes the false "shot taken" implication without adding a new command. |
| S11 | Stop Nikon Smart at the BLE authentication result and report the Bluetooth Classic blocker. Do not wait for a final OK that is a time request. | NK-S | `app-static` nikon.md S20 and S22; `furble-source` NikonSmart.cpp | Turns a 10 s silent failure into a precise user-facing reason and removes a misleading success path. |
| S12 | Gate the UI controls by family capability: hide bulb and focus on Ricoh, hide focus on LUMIX, and label Nikon Remote release as a no-op. | All | `firmware-static` and `app-static` rows cited in Table 1 | Stops the product promising an outcome the camera cannot produce. This is the single largest documentation-to-code mismatch in the tree. |
| S13 | Write the disconnect reason `7EDE1988` before an intentional Fujifilm Secure disconnect, and read it after a failed reconnect. | FJ-S40, FJ-S41 | `firmware-static` fujifilm.md C06 and C78; `app-static` both Fujifilm apps | Gives the reconnect path a camera-side reason instead of a generic supervision timeout. |

### 1.3 Highest-value capture gates

Ranked by how much implementation work each capture unblocks.

| Rank | Capture | Family | What it settles | Artifact that closes it |
| --- | --- | --- | --- | --- |
| G1 | X-E5 1.12 registration with `8B5ECF55` present, and without it | FJ-S41 | Whether the v4.1 connected-application write is required, optional, or rejected, and the post-insert handle map | Raw HCI plus GATT dump on X-E5 1.12 and one v4.0 body |
| G2 | Sony `FF02` status plus the full half-down, full-down, full-up, half-up sequence and one physical still | SO | Whether the four-write sequence is required, and whether `FF01` alone leaves the body in a bad state | Timestamped HCI plus a physical still and EXIF check on ZV-1F and one Alpha body |
| G3 | Sony advertisement `0x22` bits on a pairing and a non-pairing body | SO | Whether bit 1 is a real gate or an over-restriction; the app decodes bit 6 only | Raw advertisement and scan response |
| G4 | Canon Smart pairing order and write types, first pairing and saved reconnect | CN-S | Whether identity written before the `00010006` result persists, and whether Write Request is required | HCI SMP and ATT capture on EOS RP and R6 Mark II |
| G5 | Canon Smart GPS request, source select, stop, and write type | CN-S | Whether `00040001`, `03`, `05`, and `06` are required, and whether the payload is Write Command | HCI capture with a physical EXIF check |
| G6 | LUMIX APS key read and XOR identity, per-install seed, and first-pairing Wi-Fi registration | LX | Whether the APS path covers G9II without a separate login, and whether a fixed seed works on an unregistered body | HCI and Wi-Fi capture on S5II, BGH1, G9II |
| G7 | Ricoh GR III versus GR IV drive mode, self-timer `1` and `2`, and capture gate | RC3, RC4 | Whether the Drive Mode pre-write swallows the first GR III frame, and whether `OTHER` should be allowed | HCI plus physical stills on GR III 2.20, GR IIIx 1.60, GR IV 1.20 |
| G8 | Ricoh RPA identity and camera-side bond deletion on a saved reconnect | RC3, RC4 | Whether the saved address survives rotation and how deletion presents | HCI over a reboot and a camera-side delete |
| G9 | Nikon Smart BLE plus Bluetooth Classic continuation and `2021` key events | NK-S | Whether the BLE half can ever complete without Classic, and whether `2021` exists on B600 | HCI BR/EDR plus BLE capture on B600 and Z6 III |
| G10 | DJI SMP requirement, `device_id` byte order, snapshot key, wake, sleep, GPS, and mode switch | DJI | Whether requiring a bond is correct, and which of the documented R SDK commands the body accepts | FFF4 and FFF5 capture on Action 4 and Action 5 Pro, with physical outcomes |
| G11 | Fujifilm movie, power, wake, and still self-timer payloads | FJ-S40 | Payload, mode precondition, and physical outcome for `861442AB`, `43070F6C`, and the X-S20 movie strings | HCI plus physical capture on X100VI and X-S20 |
| G12 | Physical shutter and EXIF outcome on every family | All | Whether any ATT success corresponds to a file | Per-family still plus EXIF check, as required by the goal |

## 2. Table 1: Furble action traceability

Rows are grouped by action class. A row may name more than one family only when
the code path is the shared `Camera` base and the verdict is identical. Every
family-specific behaviour has its own row.

### 2.1 Discovery, matching, and link

| Furble action | Family | Code path | Spec row | Expected camera response | Current behavior | Evidence | Verdict |
| --- | --- | --- | --- | --- | --- | --- | --- |
| L01 scan, discovery | all BLE | `Scan::start(std::function)` then `CameraList::match` | fujifilm §2.4; ricoh §2; canon §2.1 to §2.6; nikon §2.1 to §2.3; sony §2.3 to §2.4; panasonic §2; dji §2.2 | Connectable undirected advertisement in the pairing, wake, or reconnect window | Active scan, one logical generation, results deduplicated by address, vendor dispatch in `CameraList::match` order | `furble-source`; `hardware-capture` Fujifilm adv; `app-static` vendor gates | aligned |
| L02 scan, saved camera | FJ-S40, FJ-S41 | `FujifilmSecure::_connect` scan branch, `onResult`, 60 s queue wait | fujifilm §7.3 and §8 | Form A3 or A4 with the saved five serial bytes, service `A9D2B304` or `123D8F06` | Queue reset before the scan, 60 s wait, serial compare, accepts either service, cancel polled | `furble-source`; `hardware-capture` 2026-08-28 | aligned |
| L03 scan, saved camera | FJ-B | `FujifilmBasic::_connect` | fujifilm §7.6 | Saved address still advertises; token is taken from NVS | No saved scan. Connects directly to the stored address and reuses the stored token | `furble-source`; `public-source` | aligned |
| L04 scan, saved camera | RC3, RC4, CN-S, CN-R, SO, LX, DJI | per-class `_connect` | ricoh §7.4; canon §7.5 and §7.6; sony §7.4; panasonic §7.5; dji §7.2 | Saved address still resolves and the camera is in its reconnect window | Direct connect to the stored address. No advertisement re-check and no identity resolution | `furble-source`; `public-source`; `inference` | capture-gated |
| L05 scan, saved camera | NK-R, NK-S | `Nikon::_connect` scan branch, `onResult` | nikon §2.3 and §7.2 | 7-byte manufacturer record with the saved device ID, `DE00` advertised | 60 s scan, device ID compare, then connect | `furble-source`; `firmware-static` B600 | aligned |
| L06 match | FJ-B | `FujifilmBasic::matches`, `FujifilmProtocol::matchesBasicAdvertisement` | fujifilm §2 form A1, §2.3 | 7 manufacturer bytes, company `0x04D8`, type `0x02`, CR or XApp service | Exact 7-byte length, exact type, either service | `furble-source`; `public-source`; `app-static` | aligned |
| L07 match | FJ-S40, FJ-S41 | `FujifilmSecure::matches`, `matchesSecureAdvertisement` | fujifilm §2 form A3 and A4 | 8 manufacturer bytes, service `A9D2B304` | Exact 8-byte length and service; the type byte is not validated | `furble-source`; `hardware-capture` (all captures are `0x01`) | aligned |
| L08 match | RC3, RC4 | `Ricoh::matches` | ricoh §2 A1 to A8 | Ricoh service or `GR_` / `RICOH` / `PENTAX` name | Any of four services or a name prefix. Broader than Image Sync and broader than the class documentation | `furble-source`; `app-static`; `public-source` | capture-gated |
| L09 match | CN-S | `CanonEOSSmart::matches` | canon §2.1 to §2.6 | Service `00010000`, company `0x01A9`, type `0x01`, 6 or 19 manufacturer bytes | First advertised service equals `00010000`. Manufacturer data, product ID, and UUID form are not parsed | `furble-source`; `app-static` | capture-gated |
| L10 match | CN-R | `CanonEOSRemote::matches` | canon §2.5 and §7.6 | Service `00050000` | First advertised service equals `00050000` | `furble-source`; `public-source` | aligned |
| L11 match | NK-R, NK-S | `Nikon::matches`, `AdvertisementProtocol::matchesNikonDiscovery` | nikon §2.1 and §2.3 | Discovery: no manufacturer data plus `DE00`. Reconnect: 7-byte company `0x0399` and device ID | Matches no manufacturer data plus `DE00`; reconnect compares the saved device ID | `furble-source`; `firmware-static` B600 | aligned |
| L12 match | SO | `Sony::matches`, `matchesSonyAdvertisement` | sony §2.1 to §2.4 | Company `0x012D`, type `0003`, tag `0x22` pairing bits | Requires tag `0x22` bits 7, 6, and 1. The app decodes bit 6 as pairing and does not decode bit 1 | `furble-source`; `app-static` | capture-gated |
| L13 match | LX | `Lumix::matches`, `matchesLumixAdvertisement` | panasonic §2 | Company `0x003A`, session service `054AC620`, 8 manufacturer bytes | Company ID, session service, and length. Matcher is model-blind | `furble-source`; `app-static` | aligned |
| L14 match | DJI | `DJIOsmo::matches`, `matchesDJIAdvertisement` | dji §2.1 and §2.2 | `AA 08`, model bytes, byte 4 `FA` | First five bytes only. Model bytes are not gated before listing, so Action 6 and Osmo 360 can be listed and rejected later | `furble-source`; `public-source`; `unknown` device IDs | capture-gated |
| L15 match | FN | `FauxNY::matches` | none, software peer | Not applicable | Always true, simulator and test only | `furble-source` | out-of-scope |
| L16 connect, outer attempt | all | `Camera::connect`, client create, security preset, vendor `_connect`, failure reclaim | state-machines §1.2 | GAP connect then vendor handshake | Owns the client for the whole attempt, polls cancel, reclaims on failure, restores self-delete on success | `furble-source` | aligned |
| L17 connect, vendor | FJ-S40, FJ-S41 | `FujifilmSecure::_connect` | fujifilm §7.2 and §7.3 | Secure link, status gate, subscriptions, registration, interval, shutter discovery, fast profile | Implements every step; after confirm, asks for FAST and requires exact live parameters within 1 s | `furble-source`; `hardware-capture` X100VI | aligned |
| L18 connect, vendor | FJ-B | `FujifilmBasic::_connect` | fujifilm §7.6 | `91F1DE68` present, token write, name, five subscriptions, registration, shutter | Looks up `91F1DE68` first. That service is absent from X100VI 1.31, X-S20 3.30, and X-E5 1.12, so the path cannot complete on the targets | `furble-source`; `firmware-static` absence | unsupported |
| L19 connect, vendor | RC3, RC4 | `Ricoh::_connect` | ricoh §7.3 and §7.4 | Bond, model read, Camera and Shooting services, state probe | Secure, requires encrypted, authenticated, bonded, requires the Shooting service, probes and subscribes | `furble-source`; `hardware-capture` GR IV | aligned |
| L20 connect, vendor | CN-S | `CanonEOSSmart::_connect` | canon §7.3 | Bond, pair request and result, identity, GPS subscription, commit, mode | Sends pair request, then identity before any result, then commit, then mode `02` | `furble-source`; `app-static` order differs | capture-gated |
| L21 connect, vendor | CN-R | `CanonEOSRemote::_connect` | canon §7.6 | Bond, name write `03` plus name, control characteristic | Exactly that, with no pair result wait | `furble-source`; `public-source` | aligned |
| L22 connect, vendor | NK-R | `Nikon::_connect`, `NikonRemote` | nikon §7.1 | `DE00`, `2087` four-stage, then `2083` ready | Four stages with 10 s per stage, remote selected by the `2087` pair characteristic | `furble-source`; `firmware-static` B600 | aligned |
| L23 connect, vendor | NK-S | `Nikon::_connect`, `NikonSmart` | nikon §7.3 and §7.7 | `2000` Blowfish exchange, then a Bluetooth Classic bond | Runs the BLE exchange, then `connectFinalise` always returns false. The camera never becomes active | `furble-source`; `app-static`; `public-source` | unsupported |
| L24 connect, vendor | SO | `Sony::_connect` | sony §7.4 and §7.9 | Bond, `8000FF00` and `FF01`, optional `8000DD00` | Secure, requires `FF00` and `FF01`, keeps `DD00` when present. No `EE01` and no `EE03` | `furble-source`; `public-source` | capture-gated |
| L25 connect, vendor | LX | `Lumix::_connect` | panasonic §7.2 and §7.5 | Session service, MEI0, name, control service, optional location and clock | Writes MEI0 and name directly with `writeValue`, no Confirm or Start, no readiness subscription | `furble-source`; `app-static` | safe-fix-needed |
| L26 connect, vendor | DJI | `DJIOsmo::_connect` | dji §7.1 and §7.2 | FFF0, FFF4 subscribe, `00 19` connection request, camera verification, ACK, status subscription | MTU 517 request, secure and bonded required, 33-byte request, verify mode 2 and data 0, 30 s wait, ACK, non-fatal status subscription | `furble-source`; `public-source` | aligned |
| L27 secure, security setup | all | `NimBLEDevice::setSecurityAuth(true, true, true)` and per-class `securityMode()` | state-machines §1.2 | Bond, MITM, LE Secure Connections | Set before every connect. Default IO capability is DisplayYesNo; Ricoh overrides to keyboard display; DJI and Ricoh also inject a fallback passkey | `furble-source` | aligned |
| L28 secure, handshake | FJ-S40, FJ-S41 | `m_Client->secureConnection()` plus stale-bond branch | fujifilm §7.5 and §7.8 | Encrypt with the stored keys, or a numeric comparison on a fresh bond | One handshake per attempt, two-failure stale-bond rule, delete by identity address, repair prompt | `furble-source`; `hardware-capture` 2026-09-02 and 2026-09-05 | aligned |
| L29 secure, handshake | RC3, RC4 | `secureConnection`, `Ricoh::securityMode`, passkey callbacks | ricoh §7.1 and §7.3 | GR III passkey entry, GR IV numeric comparison, authenticated bond | Requests keyboard display for every Ricoh, injects `123456`, and always confirms the numeric comparison | `furble-source`; `firmware-static` GR IV; `hardware-capture` GR III | capture-gated |
| L30 secure, handshake | CN-S | `secureConnection` | canon §7.1 | Bond, method unknown | The shared base sets bond, MITM, and Secure Connections for every camera. The Canon class does not override the IO capability or add a passkey handler, and the source comment calls the result Just Works | `furble-source`; `app-static`; `unknown` | capture-gated |
| L31 secure, handshake | CN-R | `secureConnection` | canon §7.1 and §7.6 | Secure bond, method unknown | One handshake, no bond pre-check | `furble-source`; `public-source`; `unknown` | capture-gated |
| L32 secure, handshake | NK-R | none | nikon §7.8 | Pairing is an application-level four-stage exchange; SMP requirement unknown | No `secureConnection()` call at all | `furble-source`; `public-source`; `unknown` | capture-gated |
| L33 secure, handshake | NK-S | none | nikon §7.7 and §7.8 | Classic bond with numeric passkey after the BLE exchange | No BLE `secureConnection()`. The Classic step is absent and blocked | `furble-source`; `public-source` | unsupported |
| L34 secure, handshake | SO | `secureConnection` | sony §7.1 and §7.5 | Bond, method unknown | One handshake. A failure fails the connect, with no bond deletion and no re-pair prompt | `furble-source`; `app-static` | capture-gated |
| L35 secure, handshake | LX | none | panasonic §7.3 and §7.9 | No bond in the official app; application identity instead | No `secureConnection()`. The global security preset can still trigger SMP if the camera asks | `furble-source`; `app-static` | aligned |
| L36 secure, handshake | DJI | `secureConnection`, requires encrypted and bonded | dji §7.4 and §7.5 | Unknown whether the camera needs SMP; the demo makes no SMP call | Requires an encrypted bonded link and fails otherwise | `furble-source`; `public-source`; `unknown` | capture-gated |
| L37 register | FJ-S40 | status read, `0x20` ack, client name, subscriptions, 25 s wait, interval, shutter | fujifilm §7.2 and §7.3 | Four-byte status, ack, registration notification, interval echo | All steps implemented. Promotion requires the registration gate, not a bare secure link | `furble-source`; `firmware-static`; `hardware-capture` | aligned |
| L38 register | FJ-S41 | shared v4.0 path | fujifilm §7.7 | v4.0 path plus `8B5ECF55` `80 01 01` | Shared path only. `8B5ECF55` is never written | `furble-source`; `firmware-static` presence; `app-static` | capture-gated |
| L39 register | FJ-B | token, name, five subscriptions, registration wait | fujifilm §7.6 | Registration notification on `F9150137` | Implemented; the entry service is absent on all three target images | `furble-source`; `firmware-static` absence | unsupported |
| L40 register | RC3, RC4 | model read and state probe | ricoh §7.3 and §7.4 | Bond is the registration; model string identifies the body | Reads Model Number, overwrites the display name, probes state, subscribes to four characteristics | `furble-source`; `hardware-capture` | aligned |
| L41 register | CN-S | `00010006` name, `02` result, identity, commit, mode | canon §7.3 | Pair result indication `02`, then stored identity | Identity is written before the result; commit `01` is sent as a pairing step; mode `02` only | `furble-source`; `app-static` order differs | capture-gated |
| L42 register | CN-R | name write `03` plus name | canon §7.6 | Unknown, no result documented | Writes the name and proceeds without a result | `furble-source`; `public-source`; `unknown` | aligned |
| L43 register | NK-R | four-stage `2087` | nikon §7.1 | Stage 2 and stage 4 indications | Implemented, with serial logged at stage 4 | `furble-source`; `firmware-static` B600 | aligned |
| L44 register | NK-S | four-stage `2000`, then final OK | nikon §7.3 | Stage 2 and stage 4, then Classic | BLE stages run, then the 1 s final OK wait, then an unconditional failure | `furble-source`; `public-source` | unsupported |
| L45 register | SO | none | sony §7.2 and §7.3 | `EE01` `06 08 01 00 00 00 00`, `EE03` state and result, `EE04` info | None of the three is used | `furble-source`; `app-static` | capture-gated |
| L46 register | LX | MEI0 and name only | panasonic §7.2 and §7.6 | MEI0 identity, name, Confirm, Start | MEI0 and name are written; Confirm and Start are absent. The fixed seed `02 00 00 01 00 00` replaces the per-install seed | `furble-source`; `app-static` | safe-fix-needed |
| L47 register | DJI | `00 19` request, verify mode 2, ACK | dji §7.1 | Camera verification result 0, then protocol ready | Implemented, including the dual device-ID encoding | `furble-source`; `public-source` | aligned |
| L48 reconnect | FJ-S40, FJ-S41 | saved scan, identity address, secure, full registration | fujifilm §7.3 | New advertising window after link loss, same bond, geotag request as confirmation | Implemented. Accepts a geotag request as registration on a saved reconnect | `furble-source`; `hardware-capture` 2026-08-28 | aligned |
| L49 reconnect | FJ-B, RC3, RC4, CN, SO, LX, DJI, NK-R | per-class saved path | ricoh §7.4; canon §7.5; sony §7.4; panasonic §7.5; dji §7.2; nikon §7.2 | Saved address or identity still valid, vendor re-registration as needed | Direct connect by stored address; Nikon scans for the device ID; Fujifilm Basic reuses the stored token | `furble-source`; `public-source` | capture-gated |
| L50 reconnect policy | all | `Control::connectAll`, `ReconnectBackoff::delayMs` | state-machines §1.5 | Not a camera action | Retries while infinite or fewer than two consecutive cycles failed. Backoff 2.5 s to 120 s, sliced at 100 ms | `furble-source` | aligned |
| L51 stale bond, camera deleted | FJ-S40, FJ-S41 | two security failures, `deleteBond(identity)`, fresh pair, `needsRepair` | fujifilm §7.5 and §8 | Handshake failure shapes `rc=13` about 30 s or `rc=520` about 5 s; no explicit refusal | Implemented and hardware traced. Fresh pair after delete normally fails because unpair drops the link | `furble-source`; `hardware-capture` 2026-09-02 and 2026-09-05 | aligned |
| L52 stale bond, camera deleted | FJ-B | token reject | fujifilm §7.6 and §7.8 | Old token refused; a new advertisement token is required | Connect or token write fails. No local bond exists and no re-pair prompt is raised | `furble-source`; `public-source` | aligned |
| L53 stale bond, camera deleted | RC3, RC4 | `Ricoh::_connect` secure branch | ricoh §7.5 | Passkey or numeric comparison fails; camera may drop or hold the link | Only a NEW pairing attempt that was bonded deletes the local bond, and it deletes by the advertised address. A saved reconnect keeps the stale bond forever | `furble-source`; `public-source` | capture-gated |
| L54 stale bond, camera deleted | CN-S | `deleteBond` on pair reject | canon §7.7 and §7.8 | Pair result other than `02`, or no result | Deletes the bond on rejection or timeout. A saved reconnect has no deletion detection and always sends the pair request | `furble-source`; `app-static`; `unknown` | capture-gated |
| L55 stale bond, camera deleted | CN-R, SO, LX, DJI | none | canon §7.7; sony §7.5; panasonic §7.6; dji §7.3 | Vendor-specific, mostly unknown | No detection and no recovery. SO, LX, and DJI rely on a saved address that may not resolve | `furble-source`; `public-source`; `unknown` | capture-gated |
| L56 stale bond, camera deleted | NK-R, NK-S | device ID mismatch | nikon §7.6 | A new device ID is advertised after re-pairing | Reconnect scan times out when the saved ID no longer appears. No local bond exists to delete | `furble-source`; `public-source`; `unknown` | capture-gated |
| L57 disconnect, user | all | `Control::disconnect`, `Camera::disconnect`, drain, reclaim | state-machines §1.6 | Link termination, vendor teardown if any | Arms cancel, publishes DISCONNECTING, terminates, waits up to 30 s, drains, reclaims at 2 s | `furble-source` | aligned |
| L58 disconnect, vendor teardown | FJ-S40, FJ-S41 | `FujifilmSecure` inherits `Fujifilm::_disconnect` | fujifilm §5 C06 | Optional disconnect reason write before link loss | Only `m_Client->disconnect()`. The disconnect reason characteristic is never written | `furble-source`; `app-static` | safe-fix-needed |
| L59 disconnect, vendor teardown | LX | `Lumix::_disconnect` | panasonic §5 C08 | `03` Finish, then link close | Clears pointers and disconnects. No Finish write | `furble-source`; `app-static` | safe-fix-needed |
| L60 disconnect, vendor teardown | RC3, RC4, CN, NK, SO, DJI | per-class `_disconnect` | ricoh §7.6; canon §7.6; nikon §7.2; sony §7.9; dji §7.2 | Vendor-specific teardown, mostly none documented | Link terminate only. No vendor state write | `furble-source`; `public-source` | aligned |

### 2.2 Capture

| Furble action | Family | Code path | Spec row | Expected camera response | Current behavior | Evidence | Verdict |
| --- | --- | --- | --- | --- | --- | --- | --- |
| C01 shutter press | FJ-B, FJ-S40, FJ-S41 | `Fujifilm::shutterPress`, `makeShutterFrame` | fujifilm C11, C12, C13; §5.1 | ATT Write Response twice, then physical exposure | Writes `01 00` then `02 00`, both with response, no delay | `furble-source`; `firmware-static`; `app-static`; `hardware-capture` S1/S2 only | aligned |
| C02 shutter release | FJ-B, FJ-S40, FJ-S41 | `Fujifilm::shutterRelease` | fujifilm C11; §5.1 | ATT Write Response twice, exposure ends | Writes `01 00` then `00 00` | `furble-source`; `app-static`; `hardware-capture` | aligned |
| C03 shutter press | RC4 | `Ricoh::shutterPress`, `captureAllowed`, `setShootingFlavor`, `writeOperation` | ricoh CM-16, CM-18, F-01, F-37 | Fresh Operation Mode CAPTURE, Drive Mode write, then `{01,01}`; saved image | Fresh Operation Mode read, refuse unless CAPTURE, Drive Mode `0`, then `{01,01}` | `furble-source`; `firmware-static`; `hardware-capture` author-reported | aligned |
| C04 shutter press | RC3 | same path | ricoh F-01 and §4.5 | `{01,01}` alone; a Drive Mode pre-write can swallow the first frame | Same Drive Mode `0` pre-write as GR IV, and the gate refuses `OTHER` | `furble-source`; `public-source`; `hardware-capture` author-reported | capture-gated |
| C05 shutter release | RC3, RC4 | `Ricoh::shutterRelease` | ricoh F-03 | No release write for a still | Logs and returns; no write | `furble-source`; `app-static` | aligned |
| C06 shutter press and release | CN-S | `CanonEOSSmart::shutterPress` and `shutterRelease` | canon CS-16, CS-17 | AF plus release on, then AF plus release off | `00 01` then `00 02`, both as ATT Write Command | `furble-source`; `app-static`; `public-source` | aligned |
| C07 shutter press and release | CN-R | `CanonEOSRemote` | canon CR-04, CR-05 | `0x8c` then `0x0c` | Exactly that, both with response | `furble-source`; `public-source` | aligned |
| C08 shutter press | NK-R | `NikonRemote::shutterPress` | nikon R5 | `2083` `02 02` short press | Exactly that | `furble-source`; `firmware-static` B600 | aligned |
| C09 shutter release | NK-R | `NikonRemote::shutterRelease` | nikon R6b | `2083` `02 00` is a no-op, not a release | Writes `02 00`, which the firmware ignores | `furble-source`; `firmware-static` B600 | aligned |
| C10 shutter press and release | NK-S | `NikonSmart::shutterPress` and `shutterRelease` | nikon §9.2 | `2021` key 3 press and release | Both are no-ops, and the camera cannot become active | `furble-source`; `app-static` | unsupported |
| C11 shutter press and release | SO | `Sony::shutterPress` and `shutterRelease` | sony F-03, C-32 | `01 09`, then `01 08` | Exactly that, both with response | `furble-source`; `public-source` | aligned |
| C12 shutter press and release | LX | `Lumix::writeCommand` | panasonic C09, C10 | `04`, then `05` | Exactly that, with response, and no Confirm or Start before it | `furble-source`; `app-static` | safe-fix-needed |
| C13 shutter | DJI | `DJIOsmo::shutterPress`, `writeRecordCommand` | dji C16, F03, F08 | `1D 03` start or stop; photo mode outcome unknown | Toggles record from the tracked state. A still photo is not attempted | `furble-source`; `public-source`; `unknown` | capture-gated |
| C14 focus press and release | FJ-B, FJ-S40, FJ-S41 | `Fujifilm::focusPress` and `focusRelease` | fujifilm §5.1 focus row, C14 | Half press `01 00`, then release `00 00`; AF outcome not captured | Half press then release. The old `03 00` was removed because the firmware parser returns -30 | `furble-source`; `firmware-static`; `public-source` | aligned |
| C15 focus | RC3, RC4 | `Ricoh::focusPress` and `focusRelease` | ricoh F-02, CM-30 | No BLE focus trigger; Focus Mode only configures a mode | Both are no-ops with a log line | `furble-source`; `firmware-static`; `app-static` | unsupported |
| C16 focus press and release | CN-S | `CanonEOSSmart` | canon CS-18, CS-19, F02 | `00 03` AF on, then `00 04` AF off | Exactly that, both as Write Command | `furble-source`; `app-static` | aligned |
| C17 focus press and release | CN-R | `CanonEOSRemote` | canon CR-06, CR-05 | `0x4c`, then `0x0c` | Exactly that, with response | `furble-source`; `public-source` | aligned |
| C18 focus | NK-R, NK-S | `NikonRemote` and `NikonSmart` | nikon §9.1 and §9.2 | Remote has no focus surface; Smart uses key 2 | Remote is a no-op. Smart is a no-op and unreachable | `furble-source`; `firmware-static`; `app-static` | unsupported |
| C19 focus press and release | SO | `Sony::focusPress` and `focusRelease` | sony F-02, C-33 | `01 07`, then `01 06` | Exactly that, with response | `furble-source`; `public-source` | aligned |
| C20 focus press and release | LX | `Lumix::focusPress` and `focusRelease` | panasonic F02, C06, C07 | No focus command exists. `01` is Confirm and `02` is Start | Sends `01` on focus press and `02` on focus release. This changes session state and never focuses | `furble-source`; `app-static` | unsupported |
| C21 focus | DJI | `DJIOsmo` | dji F02 | No focus command documented | No-op | `furble-source`; `public-source` | unsupported |
| C22 bulb | FJ, CN-S, CN-R, SO, LX | UI holds `shutterPress`, then `shutterRelease` | fujifilm §5.1 bulb; canon F04; sony F-04; panasonic F04 | Held press; camera exposure mode decides | UI bulb page holds the press for the configured duration, then releases | `furble-source`; `firmware-static` absence; `inference` | capture-gated |
| C23 bulb | RC3, RC4 | none | ricoh F-04 | Shooting Mode B or BT, then a stop write | `shutterRelease` is ignored, so a bulb countdown runs but cannot end the exposure | `furble-source`; `app-static` | unsupported |
| C24 bulb | NK-R, NK-S, DJI | none | nikon §9.1; dji F04 | No bulb command documented | No-op or local hold only | `furble-source`; `public-source` | out-of-scope |
| C25 self-timer | RC3, RC4 | `ShootingFlavor::TIMER_2S` declared, unused | ricoh F-05, CM-18 | Drive Mode `1` or `2`, then the capture | Missing. The enum exists and is never written; `009A8E70` is never used | `furble-source`; `firmware-static`; `app-static` | capture-gated |
| C26 self-timer | CN-S, CN-R, SO, LX, FJ, DJI, NK | none | canon F05; sony F-05; panasonic F05; fujifilm §9 self-timer; dji F05 | Camera menu only, or a 2 s remote bit on Canon Remote | Missing. For Canon Remote the `0x04` delay bit is documented but not implemented | `furble-source`; `app-static`; `official-semantic` | capture-gated |
| C27 interval | all except DJI, NK-S | UI intervalometer, `CMD_SHUTTER_PRESS` and `CMD_SHUTTER_RELEASE` | fujifilm §9 interval; ricoh F-06; canon F06; sony F-06; panasonic F06; nikon §9.1 | Repeated shutter commands, no camera interval opcode | Local timer sends a normal press and release per shot. No camera interval mode is selected | `furble-source`; `app-static`; `firmware-static` absence | aligned |
| C28 interval, camera-side | RC3, RC4 | none | ricoh F-06, CM-18 | Drive Mode `10` to `15` and `37` to `40` | Missing | `furble-source`; `app-static` | capture-gated |
| C29 interval, camera-side | DJI | none | dji F06 | Timelapse mode `0x02` plus a status interval field | Local timer only. Timelapse mode is never selected | `furble-source`; `public-source` | capture-gated |
| C30 movie | FJ | none | fujifilm C15, §9 movie | `861442AB` uint16, Camera Remote writes 1, stop value unknown | Missing | `furble-source`; `firmware-static`; `app-static` | capture-gated |
| C31 movie | RC3, RC4 | none | ricoh F-08, CM-26, CM-29 | Capture Mode `2`, Movie Configuration, Operation Request start and stop | Missing | `furble-source`; `app-static`; `unknown` stop encoding | capture-gated |
| C32 movie | CN-S | none | canon CS-22, CS-23, F08 | `00 10` start, `00 11` stop | Missing | `furble-source`; `app-static`; `public-source` | capture-gated |
| C33 movie | CN-R | none | canon CR-08, F08 | `0x88` inferred | Missing. The byte is inference, not an observed frame | `furble-source`; `inference` | capture-gated |
| C34 movie | SO | none | sony F-08, F-60 | DiRxTx `01` start, `02` stop | Missing. The shutter command in movie mode is not the same thing | `furble-source`; `app-static` | capture-gated |
| C35 movie | LX | none | panasonic C11, C12, F08 | `06` RecOn, `07` RecOff | Missing | `furble-source`; `app-static` | capture-gated |
| C36 movie | DJI | `DJIOsmo::writeRecordCommand` | dji C16, F08 | `1D 03` start and stop | Implemented as a toggle, with recording state inferred from status pushes | `furble-source`; `public-source` | aligned |
| C37 movie | NK-R, NK-S | `MODE_VIDEO 0x03` declared, never sent | nikon §9.1 and §9.2 | No Remote movie opcode; Smart uses key 4 | No-op or unreachable | `furble-source`; `firmware-static` absence | unsupported |
| C38 zoom | FJ, RC3, RC4, NK, SO, LX, DJI | none | fujifilm §9 zoom; ricoh F-09; nikon §9.1; sony F-09; panasonic F09; dji F09 | Fixed lens, Wi-Fi zoom, or no BLE payload | Missing or out of scope. No family has a verified BLE zoom setter in Furble | `furble-source`; `firmware-static` absence; `public-source` | out-of-scope |
| C39 zoom | CN-S, CN-R | none | canon CS-24 to CS-27, CR-09, CR-10, F09 | `00 20` to `00 23`, or tele and wide bits | Missing. The Smart bytes have no app call site; the Remote bytes are inference | `furble-source`; `app-static`; `inference` | capture-gated |

### 2.3 Power, wake, GPS, and clock

| Furble action | Family | Code path | Spec row | Expected camera response | Current behavior | Evidence | Verdict |
| --- | --- | --- | --- | --- | --- | --- | --- |
| P01 power off | FJ-S40, FJ-S41 | none | fujifilm C18, §9 power off | `43070F6C` `01 00`; no GATT acknowledgement | Missing | `furble-source`; `firmware-static`; `app-static` | capture-gated |
| P02 power off | RC4 | none | ricoh CM-01, F-11 | `B58CE84C` `0`; avoid during POWER_OFF_TRANSFER | Missing. The corpus says an unconditional power-off must not be implemented | `furble-source`; `firmware-static`; `inference` | out-of-scope |
| P03 power off | RC3 | none | ricoh F-11 | No BLE power-off write in the reviewed path | Missing | `furble-source`; `official-semantic` | out-of-scope |
| P04 power off | CN-S | none | canon CS-14, F11 | Mode `05` PW_OFF_END | Missing | `furble-source`; `app-static` | capture-gated |
| P05 power off | SO | none | sony C-01, F-11 | `CC02` `03 08 0D 00` | Missing | `furble-source`; `app-static` | capture-gated |
| P06 power off | NK-S | none | nikon S15, §9.2 | `2001` write, values unknown | Missing and unreachable | `furble-source`; `app-static`; `unknown` | unsupported |
| P07 power off | DJI | none | dji C13, F11 | `1A` power mode `3` for sleep, no cold off | Missing | `furble-source`; `public-source` | capture-gated |
| P08 power off | LX | none | panasonic F11 | Wi-Fi only, no BLE opcode | Missing and out of scope for the BLE remote | `furble-source`; `app-static` | out-of-scope |
| P09 power off | NK-R, CN-R, FJ-B | none | nikon §9.1; canon §9.2; fujifilm §9 | No opcode documented | Missing | `furble-source`; `public-source`; `official-semantic` | out-of-scope |
| P10 power on | FJ-S40, FJ-S41 | none | fujifilm C18, C19, §5.4 | `43070F6C` `00 01`; remote boot must be on | Missing | `furble-source`; `firmware-static`; `public-source` | capture-gated |
| P11 power on | RC3, RC4 | connect only | ricoh F-10, F-12 | GR III and IIIx wake on connect; GR IV needs the app power write or an accepted connect | Connect only. No power write and no wake confirmation | `furble-source`; `hardware-capture` author-reported; `official-semantic` | capture-gated |
| P12 power on | CN-S | mode `02` only | canon CS-14, F10 | Connect to a body advertising power off, then mode `03` ANY_START | Connects and writes `02` RECORD_START, not `03` | `furble-source`; `app-static` | capture-gated |
| P13 power on | SO | connect only | sony F-10, F-54 | A GATT connect is the wake action; tag `0x21` bit 7 advertises support | Connect only, no wake confirmation | `furble-source`; `app-static` | capture-gated |
| P14 power on | NK-S | none | nikon S15, §9.2 | `2001`, key 1 | Missing and unreachable | `furble-source`; `app-static` | unsupported |
| P15 power on | DJI | none | dji F10, F12 | No cold power-on; WKP advertisement plus key | Missing | `furble-source`; `public-source` | capture-gated |
| P16 power on | LX, NK-R, CN-R | none | panasonic F10; nikon §9.1; canon §9.2 | Panasonic states Bluetooth cannot power the camera on | Missing | `furble-source`; `official-semantic` | out-of-scope |
| P17 wake from standby | FJ-S40, FJ-S41 | none | fujifilm §2.5, C18, C19 | `43070F6C` `00 01`, remote boot setting, startup advertisement | Missing. A wake drops an existing link and starts a new advertising window | `furble-source`; `firmware-static`; `hardware-capture` | capture-gated |
| P18 wake from standby | RC4 | connect only | ricoh F-12 | Leave BLE_STARTUP; power `1` wakes | Connects but does not wake. A capture write in BLE_STARTUP is refused by design | `furble-source`; `hardware-capture` author-reported | capture-gated |
| P19 wake from standby | CN-S | none | canon F12 | Mode `03` ANY_START | Missing | `furble-source`; `app-static` | capture-gated |
| P20 wake from standby | NK-S, DJI, SO | none | nikon §9.2; dji F12; sony F-12 | `2001`, WKP advertisement, or GATT connect | Connect only for Sony. No Nikon Smart or DJI wake path | `furble-source`; `public-source` | capture-gated |
| P21 GPS enable | FJ-S40, FJ-S41 | none | fujifilm C25, C26, N15 | `AAB609C4` uint16 On or Off | Missing. Only the sync interval `C95D91AE` is written | `furble-source`; `firmware-static`; `app-static` | safe-fix-needed |
| P22 GPS enable | RC3, RC4 | `Ricoh::setGeotagEnable` | ricoh CM-04, F-23 | `A36AFDCF` `01` | Written once before the first packet | `furble-source`; `firmware-static`; `app-static`; `hardware-capture` author-reported | aligned |
| P23 GPS enable | CN-S | `GEO_ENABLE` on `00040002` | canon CS-35, F23 | One-byte `01`; acceptance unproven | Written on camera request `00040003` value `03`; the app uses a 9-byte `01` plus zeros | `furble-source`; `unknown` | capture-gated |
| P24 GPS enable | SO | `Sony::locationEnabled` | sony C-20, C-21, F-23 | `DD30` lock `01`, `DD31` enable `01`, `DD01` availability | Reads and writes `DD30` and `DD31`, reads them back, never subscribes `DD01`, never releases either | `furble-source`; `app-static` | safe-fix-needed |
| P25 GPS enable | LX | none | panasonic C13, F-23, F-45 | Readiness `37701b80` `01`, then packets | Missing. Writes the packet without waiting for readiness | `furble-source`; `app-static`; `public-source` | safe-fix-needed |
| P26 GPS enable | DJI | none | dji C07, F21, F23 | `00 17` GPS push, no separate enable | Deliberate no-op in `updateGeoData` | `furble-source`; `public-source` | capture-gated |
| P27 GPS packet | FJ-S40, FJ-S41 | `Fujifilm::updateGeoData`, `sendGeoData`, `encodeGeotag` | fujifilm C22, N03, §5.2 | One 23-byte packet per `AD06C7B7` `01 00`; no per-packet ack | Answers only when requested, clears the flag, truncates lat, lon, and altitude instead of rounding | `furble-source`; `firmware-static`; `app-static`; `hardware-capture` | aligned |
| P28 GPS packet | RC3, RC4 | `Ricoh::updateGeoData` | ricoh CM-31, F-21 | 32-byte packet, big-endian doubles, datum `0` | Writes on movement of 0.00001 degrees or 1 m, or every 10 s. Enables GEO Tag on the first write | `furble-source`; `firmware-static`; `app-static`; `hardware-capture` author-reported | aligned |
| P29 GPS packet | CN-S | `CanonEOSSmart::updateGeoData` | canon CS-34, F21 | 20-byte packet, absolute float32, Unix seconds | Writes only after `m_GeoEnabled`. Timestamp is `mktime` of the tuple, so it depends on the runtime timezone | `furble-source`; `app-static` | safe-fix-needed |
| P30 GPS packet | SO | `Sony::updateGeoData` | sony C-22, C-23, F-21 | 91 or 95 bytes chosen from `DD21`, UTC offset and DST | Always 95 bytes, zero offset and DST, no `DD21` read, no lock release | `furble-source`; `app-static` | safe-fix-needed |
| P31 GPS packet | LX | `Lumix::updateGeoData` | panasonic C13, F-21 | 16-byte packet, GPS epoch seconds, fix `0x41` | Writes on every GPS tick, then writes the clock after a successful location write | `furble-source`; `app-static`; `public-source` | safe-fix-needed |
| P32 GPS packet | NK-S | `NikonSmart::updateGeoData` | nikon S8, §9.2 | 41-byte packet with flags and WGS-84 trailer | Code exists, but the camera cannot become active, so it is unreachable | `furble-source`; `app-static` | unsupported |
| P33 GPS packet | NK-R, DJI | explicit no-op | nikon §9.1; dji F21 | No GPS surface on Nikon Remote; DJI C07 documented | No-op | `furble-source`; `firmware-static`; `public-source` | out-of-scope |
| P34 clock read | all | none | fujifilm §9 clock read; ricoh CM-40; canon F18; sony F-18; panasonic F18; dji F18 | Only Ricoh exposes a documented readable clock. Sony `CC13` is write only | Missing everywhere. No family reads the camera clock | `furble-source`; `firmware-static`; `app-static` | safe-fix-needed |
| P35 clock write | FJ-S40, FJ-S41 | none | fujifilm C28, C29, §5.3 | 12-byte UTC and timezone packet after `F9150137` `01 00`, or 7-byte local fallback | Missing. Not even the 7-byte fallback is written | `furble-source`; `firmware-static`; `app-static` | safe-fix-needed |
| P36 clock write | RC3, RC4 | none | ricoh CM-03, F-19 | 7-byte Date Time write, local wall clock | Missing | `furble-source`; `firmware-static`; `app-static` | safe-fix-needed |
| P37 clock write | CN-S | GPS timestamp only | canon CS-34, F19 | PTP `SetDateTime`; the GPS timestamp effect is unknown | No clock write. The GPS packet carries a timestamp only | `furble-source`; `app-static`; `unknown` | capture-gated |
| P38 clock write | SO | none | sony C-06, F-19 | `CC13` 13 bytes with DST and offset | Missing | `furble-source`; `app-static` | capture-gated |
| P39 clock write | LX | `Lumix::updateGeoData` | panasonic C15, F-19, F-20 | 10-byte clock on `4cf487c0` in the app | Writes after a successful GPS write, with offset and DST zero. No clock request subscription | `furble-source`; `app-static` | safe-fix-needed |
| P40 clock write | NK-S | `NikonSmart::updateGeoData` only | nikon S7, §9.2 | `2006` 10 bytes with DST and timezone | No clock write. Only the 41-byte location packet carries time, and it is unreachable | `furble-source`; `app-static` | unsupported |
| P41 clock write | NK-R, CN-R, DJI | none | nikon §9.1; canon §9.2; dji F19 | No BLE clock write | Missing | `furble-source`; `public-source` | out-of-scope |
| P42 timezone and DST | FJ-S40, FJ-S41 | none | fujifilm C28, §5.3 | int32 hundredths of an hour excluding DST, plus a DST flag | Missing because the clock write is missing | `furble-source`; `app-static` | safe-fix-needed |
| P43 timezone and DST | SO | none | sony C-22, F-20 | Bytes 91 to 94 in the 95-byte form | Always zero | `furble-source`; `app-static` | safe-fix-needed |
| P44 timezone and DST | LX | none | panasonic F-20 | App offset at bytes 7 to 8, DST at 9 | Both zero. The LUMIX Sync layout is now used since `5a7b73ba` | `furble-source`; `app-static` | capture-gated |
| P45 timezone and DST | RC3, RC4 | none | ricoh F-20 | No timezone field in the 7-byte packet | Not applicable, the camera menu owns the zone | `firmware-static`; `app-static` | out-of-scope |
| P46 sync interval | FJ-S40, FJ-S41 | `FujifilmSecure::_connect` interval write | fujifilm C23, N04, §8 | `C95D91AE` `0A 00`, echo on the same characteristic | Writes `0A 00` after registration. The echo is subscribed but logged as unhandled | `furble-source`; `firmware-static`; `hardware-capture` | aligned |

### 2.4 Data, settings, and maintenance

| Furble action | Family | Code path | Spec row | Expected camera response | Current behavior | Evidence | Verdict |
| --- | --- | --- | --- | --- | --- | --- | --- |
| M01 settings read | FJ-S40, FJ-S41 | status read only | fujifilm C08, C58, C59, C62, C66, C68 | Camera info, serial, protocol version, card state, lens, passphrase | Only the pairing status is read. Model, serial, card state, lens, and passphrase are never read | `furble-source`; `firmware-static` | safe-fix-needed |
| M02 settings read | FJ-B | none | fujifilm §4 | Same table by generation | No reads | `furble-source`; `unknown` for a Basic body | capture-gated |
| M03 settings read | RC3, RC4 | model, power, mode, GPS, capture status, self timer | ricoh F-24, CM-38 to CM-44, CM-91 | Current values | Reads model and the state probe, logs the rest. No battery or storage read | `furble-source`; `hardware-capture` author-reported | aligned |
| M04 settings read | CN-S | none | canon CS-13, CS-12, CS-37, F24 | Capability mask, power state, status block | Missing | `furble-source`; `app-static` | capture-gated |
| M05 settings read | SO | `DD30`, `DD31` only | sony C-24 to C-27, F-24 | `DD21`, `DD30`, `DD31`, `DD32`, `DD33`, `EE04` | Reads only the two location bytes | `furble-source`; `app-static` | safe-fix-needed |
| M06 settings read | LX | none | panasonic C21 to C27, F-24 | SSID, name, capability, vendor, model | Missing. The matcher is model-blind | `furble-source`; `app-static` | capture-gated |
| M07 settings read | DJI | status push parse | dji C15, F24 | Mode, status, record time, battery, storage | Parses mode, status, and record time only | `furble-source`; `public-source` | aligned |
| M08 settings write | FJ-S40, FJ-S41 | implemented, interval only | fujifilm C23, C30 to C37, F settings write | Interval, SSID, resize, transfer, logging, reservation, remote boot | Only the interval is written | `furble-source`; `firmware-static`; `app-static` | capture-gated |
| M09 settings write | RC3, RC4 | Drive Mode and GEO Tag | ricoh CM-04, CM-18, F-25 | Drive Mode, geotag, focus mode, exposure | Writes Drive Mode `0` and GEO Tag `1` only | `furble-source`; `firmware-static`; `app-static` | aligned |
| M10 settings write | CN-S | mode only | canon CS-14, CS-10, F25 | Mode, auto power off inhibit, Wi-Fi | Writes mode `02` only | `furble-source`; `app-static` | capture-gated |
| M11 settings write | SO | none | sony C-04 to C-13, F-25 | Friendly name, date format, clock, APO, Wi-Fi band | Missing | `furble-source`; `app-static` | capture-gated |
| M12 settings write | LX | MEI0 and name | panasonic C01, C02, F-25 | Identity, name, Wi-Fi start | Writes a fixed MEI0 and a plain name | `furble-source`; `app-static` | safe-fix-needed |
| M13 settings write | DJI | none | dji C18, F25, F37 | Mode switch | Missing | `furble-source`; `public-source` | capture-gated |
| M14 paired-device name | FJ-S40, FJ-S41 | `85B9163E` name write | fujifilm C03 | Stored client name, optional trailing NUL | Writes `furble-` plus five hex, no NUL | `furble-source`; `app-static` | safe-fix-needed |
| M15 paired-device name | RC3, RC4 | `m_PairedDeviceName` read only | ricoh C-76, CM-15, F-30 | Stored paired name | Reads and logs the characteristic. Never writes a name | `furble-source`; `app-static` | safe-fix-needed |
| M16 paired-device name | CN-S | identity name write | canon CS-05, F30 | Stored client name | Implemented, but before the pair result | `furble-source`; `app-static` | capture-gated |
| M17 paired-device name | LX | name write | panasonic C02, F-30 | Stored controller name | Implemented | `furble-source`; `app-static` | aligned |
| M18 paired-device name | SO, DJI, NK, CN-R | none | sony F-30; dji F-30; nikon §9.1 | No separate name write, or a different command | Missing | `furble-source`; `public-source` | out-of-scope |
| M19 pairing management | FJ-S40, FJ-S41 | status, ack, name, local delete | fujifilm F pairing management | Paired-device count, disconnect reason, client name | No count read and no disconnect-reason write. Local bond deletion only | `furble-source`; `firmware-static`; `app-static` | safe-fix-needed |
| M20 pairing management | RC3, RC4 | bond only | ricoh C-75, F-29, F-54 | BLE enable condition, paired list, delete | No BLE enable condition read and no paired-list query. Local delete only | `furble-source`; `app-static` | capture-gated |
| M21 pairing management | CN-S, CN-R, NK, SO, LX, DJI | bond only | canon F29; nikon §9.1; sony F-29; panasonic F-29; dji F29 | Vendor-specific | No paired-list management. Sony `EE01` and LUMIX APS are missing | `furble-source`; `app-static` | capture-gated |
| M22 bond deletion | FJ-S40, FJ-S41 | `deleteBond` on two failures | fujifilm §7.5 | Local bond removed; camera still needs pairing mode | Implemented | `furble-source`; `hardware-capture` | aligned |
| M23 bond deletion | RC3, RC4, CN-S | `deleteBond` on a new pairing failure | ricoh §7.5; canon §7.8 | Local bond removed | Implemented only for a new pairing. Saved reconnects keep a stale bond | `furble-source`; `public-source` | capture-gated |
| M24 bond deletion | others | none | sony §7.5; panasonic §7.7; dji §7.3 | Camera menu or OS | Missing | `furble-source`; `official-semantic` | capture-gated |
| M25 image transfer | all | none | fujifilm §9 image transfer; ricoh F-15; canon F15; sony F-15; panasonic F15; dji F15 | BLE handover then Wi-Fi or PTP | Missing. Furble has no camera Wi-Fi client | `furble-source`; `firmware-static`; `app-static` | capture-gated |
| M26 image list and thumbnail | all | none | fujifilm §9; ricoh F-16 and F-17; canon F16; sony F-16; panasonic F16; dji F16 | Wi-Fi or PTP enumeration | Missing. No transport exists in Furble | `furble-source`; `app-static`; `unknown` | capture-gated |
| M27 live view | all | none | fujifilm §9; ricoh F-14; canon F14; sony F-14; panasonic F14; dji F14 | Wi-Fi stream | Missing. No camera Wi-Fi client | `furble-source`; `official-semantic`; `public-source` | capture-gated |
| M28 backup and restore | FJ, RC, CN, NK, SO, LX, DJI | none | fujifilm C38 to C50; ricoh F-26 and F-27; canon F26; nikon §9; sony F-26; panasonic F26; dji F26 | Vendor backup surfaces, partly unknown | Missing and excluded. Fujifilm backup and restore can overwrite camera settings | `furble-source`; `firmware-static`; `app-static` | out-of-scope |
| M29 firmware update | all | none | fujifilm C53 to C55; ricoh C-28 to C-30; canon F28; nikon S32; sony F-28; panasonic F28; dji F28 | Vendor-controlled, partly encrypted, partly Wi-Fi | Missing. Excluded because the image transport and signature policy are not recovered | `furble-source`; `firmware-static`; `app-static` | out-of-scope |
| M30 model and firmware identity | FJ-S40, FJ-S41 | advertised name and serial only | fujifilm C56 to C60 | GAP name, DIS strings, serial, protocol version | Uses the advertised model and serial. Reads no identity characteristic | `furble-source`; `firmware-static`; `hardware-capture` | capture-gated |
| M31 model and firmware identity | RC3, RC4 | model read | ricoh C-11, F-36 | Exact model string | Reads the model and uses it as the display name | `furble-source`; `hardware-capture` | aligned |
| M32 model and firmware identity | CN-S, CN-R, NK, SO, LX, DJI | advertised name only | canon F36; nikon §9.1; sony F-36; panasonic F-36; dji F36 | Product ID, DIS, serial, firmware | Missing. Canon does not parse the product ID; Sony ignores the advertisement model; LUMIX is model-blind; DJI gates on the device ID only | `furble-source`; `app-static`; `public-source` | capture-gated |
| M33 error status | FJ-S40, FJ-S41 | log only | fujifilm C67, N23 | `1587B102` details, `763514E8` write | Not read and not written. Subscribed state messages other than NOT1 and geotag are logged as unhandled | `furble-source`; `firmware-static` | capture-gated |
| M34 error status | RC3, RC4 | capture status log | ricoh F-35, CM-91 | Capture Status fields and result codes | Subscribed and logged as raw bytes, not decoded | `furble-source`; `public-source` | aligned |
| M35 error status | CN-S | none | canon CS-37, F35 | `00030002` error and busy notification | Not subscribed | `furble-source`; `app-static`; `unknown` | capture-gated |
| M36 error status | SO, LX, DJI | partial log | sony F-35; panasonic F-35; dji F-35 | `CC09`, `7be5fe6e`, status pushes | Sony ignores vendor status. LUMIX does not subscribe readiness. DJI logs mode and status only | `furble-source`; `app-static`; `public-source` | capture-gated |
| M37 error status | NK | none | nikon §9.1 | `2084` key and status | Not read | `furble-source`; `firmware-static` | capture-gated |

### 2.5 Control plane: errors, timeouts, retries, cancellation

| Furble action | Family | Code path | Spec row | Expected camera response | Current behavior | Evidence | Verdict |
| --- | --- | --- | --- | --- | --- | --- | --- |
| X01 command error handling | FJ, CN-S, CN-R, NK-R, SO | vendor `shutterPress` and `focusPress` call `gattWrite` and ignore the result | fujifilm §5; canon §5; nikon R5; sony §5 | ATT error or write failure | A failed write is invisible to the UI. The command is dropped silently | `furble-source`; `app-static` | safe-fix-needed |
| X02 command error handling | RC3, RC4, LX, DJI | vendor write helpers log the result | ricoh §5; panasonic §5; dji §5 | ATT error or write failure | Logged, not surfaced. DJI record failure is logged and the state is not changed | `furble-source` | aligned |
| X03 read error handling | all | `Camera::gattRead` returns true unconditionally | state-machines §1 | ATT error or empty value | A failed read is indistinguishable from a successful empty read at the return value. Callers must check length | `furble-source` | safe-fix-needed |
| X04 vendor rejection | FJ-S40, FJ-S41 | status read or ack failure aborts; link drop after ack is a refused registration | fujifilm §7.4 | A refused ack drops the link with reason `0x13` about 160 ms later | Aborts and prompts for pairing mode. No ack loop | `furble-source`; `hardware-capture` | aligned |
| X05 vendor rejection | RC3, RC4 | secure failure with a NEW pairing deletes the bond | ricoh §7.8 | ATT `0x0E` before bonding, `0x80` for a refused WLAN write | Secure failure returns false. Bond deletion only for a NEW pairing that was bonded | `furble-source`; `public-source` | aligned |
| X06 vendor rejection | CN-S | pair result other than `02` | canon §7.8 | Indication not `02`, or no indication | Deletes the bond and returns false. No retry | `furble-source`; `app-static` | aligned |
| X07 vendor rejection | DJI | verify result or connection response | dji §7.1 | `verify_data` 1 rejects; ret code nonzero rejects | Returns false on a nonzero result or a rejected connection. The request is not repeated in the same pairing scan | `furble-source`; `public-source` | aligned |
| X08 vendor rejection | NK-S | final OK timeout | nikon §7.3 | No final OK on Z series | Waits 1 s, then returns false. The wait is expected, not an error | `furble-source`; `public-source` | unsupported |
| X09 vendor rejection | SO, LX, NK-R, CN-R | none | sony §7.7; panasonic §7.8; nikon §7.5; canon §7.8 | Vendor-specific and mostly unknown | A failed write or handshake fails the connect. No vendor status decode | `furble-source`; `unknown` | capture-gated |
| X10 timeout, connect | all | `Control::TIMEOUT_DEFAULT_MS`, `TIMEOUT_INFINITE_MS` | state-machines §1.7 | GAP and vendor handshake | 30 s normal, 5 s in infinite reconnect mode | `furble-source` | aligned |
| X11 timeout, Fujifilm registration | FJ-S40, FJ-S41, FJ-B | `waitForRegistration` | fujifilm §8 | Registration notification | 25 s default, 20 ms poll, build-time override | `furble-source`; `hardware-capture` | aligned |
| X12 timeout, Fujifilm fast profile | FJ-S40, FJ-S41 | `confirmFujifilmSecureFastProfile` | fujifilm §8 | Exact live FAST parameters | 1 s, 10 ms poll. A mismatch aborts the connect | `furble-source` | aligned |
| X13 timeout, Canon pairing | CN-S | 60 s pair result loop | canon §8 | Pair result indication | 60 s, 1 s poll, cancel polled | `furble-source`; `app-static` | aligned |
| X14 timeout, Nikon stage | NK-R, NK-S | `handshake4Stage` | nikon §8 | Stage indication | 10 s per stage, 250 ms slices, cancel polled | `furble-source` | aligned |
| X15 timeout, Nikon final OK | NK-S | `connectFinalise` | nikon §8 | Optional final notification | 1 s, then failure | `furble-source`; `public-source` | unsupported |
| X16 timeout, DJI handshake | DJI | `finishProtocolConnection` | dji §8 | Connection request response, then camera request | 30 s, 100 ms poll, cancel polled | `furble-source`; `public-source` | aligned |
| X17 timeout, vendor writes | all | no per-write vendor timeout | fujifilm §5; ricoh §5; canon §8; sony §8; panasonic §8; dji §5 | ATT transaction and link supervision only | No vendor deadline. A write can block for the ATT transaction, and a connect can block for the whole caller timeout | `furble-source`; `app-static` | capture-gated |
| X18 retry, connect | all | `Control::connectAll`, `ReconnectBackoff` | state-machines §1.5 | Not a camera action | Two consecutive failed cycles stop the non-infinite cycle. Infinite mode backs off 2.5 s to 120 s | `furble-source` | aligned |
| X19 retry, registration | FJ-S40, FJ-S41 | none | fujifilm §7.4 | Camera needs time or pairing mode | One attempt. A timeout returns failure and prompts | `furble-source`; `hardware-capture` | aligned |
| X20 retry, stale bond | FJ-S40, FJ-S41 | two failures then delete | fujifilm §7.5 | No refusal signature | Two consecutive failures trigger deletion. Success resets the run | `furble-source`; `hardware-capture` | aligned |
| X21 retry, Canon pair | CN-S | none | canon §7.8 | Rejection is final | One attempt, then bond delete and failure | `furble-source`; `app-static` | aligned |
| X22 retry, vendor writes | all | none | vendor command tables | ATT retry is not defined by the spec | A failed write is not retried. The next GPS tick or the next user action is the retry | `furble-source`; `unknown` | capture-gated |
| X23 cancellation, connect | all | `cancelConnect`, `connectCancelled`, `abortBlockingConnect` | state-machines §1.2 and §1.7 | Link terminate wakes a blocked NimBLE call | Long vendor waits poll the token: Fujifilm 20 ms, Canon 1 s, Nikon 250 ms, DJI 100 ms, FauxNY 25 ms | `furble-source` | aligned |
| X24 cancellation, scan | all | saved scan loops poll `connectCancelled` | fujifilm §8; nikon §8 | Scan stop | Fujifilm and Nikon stop the scan and return false on cancel | `furble-source` | aligned |
| X25 cancellation, secure stall | FJ, RC | `abortBlockingConnect` terminates the client | fujifilm §7.5; ricoh §7.5 | `secureConnection` returns when the link dies | Implemented. The host tests pin that the stall is aborted, not merely outwaited | `furble-source`; `hardware-capture` | aligned |
| X26 cancellation, disconnect drain | all | `Control::disconnect`, `reclaimClient` | state-machines §1.6 | Link terminate, or a dead link | 30 s interactive cap, 2 s drain reclaim, client freed deterministically | `furble-source` | aligned |

## 3. Table 2: per-camera parity matrix

Each family table uses the goal feature list. `Transport` names the camera
surface, not the Furble transport: a feature can be `BLE GATT` on the camera and
still be `missing` in Furble. `Simulator` names the virtual peer and the scenario
that exercises the path, or `none`. `Test` names host or camera tests at this
pin, or `none (class)` when only advertisement parsing covers the family.

The simulator fleet has two real camera peers at this pin:
`FujifilmVirtualCamera` with Basic and Secure configurations, and
`RicohVirtualCamera`. `FauxNY` is a software peer, not a camera model. The 215
scenarios in `sim/scenarios/manifest.json` exercise connect, cancel, reconnect,
multi-target, UI, GPS, IMU, power, and fault paths; none asserts a vendor feature
byte for Canon, Nikon, Sony, LUMIX, or DJI, because no peer exists for those
families.

### 3.1 Fujifilm Basic

| Camera | Feature | Transport | Spec row | Furble state | Simulator | Test | Hardware state |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Fujifilm Basic | Still capture | BLE GATT | fujifilm C11 to C13, §9 still | implemented | peer Basic, shutter and focus writes; scenario connect/cancel | `fujifilm_camera_test` | capture-gated, no Basic body capture |
| Fujifilm Basic | Half press and focus | BLE GATT | fujifilm §5.1 focus, C14 | implemented | peer Basic, focus write asserted | `fujifilm_camera_test` | capture-gated, AF outcome not captured |
| Fujifilm Basic | Bulb | BLE GATT, local hold | fujifilm §5.1 bulb | implemented, local hold only | peer counts press and release only | none (bulb-specific) | capture-gated |
| Fujifilm Basic | Self-timer (still menu-only) | camera menu | fujifilm §9 self-timer | unavailable | none | none (class) | not applicable, no UUID or payload recovered |
| Fujifilm Basic | Interval, camera-side | none recovered | fujifilm §9 interval | missing | none | none (class) | capture-gated |
| Fujifilm Basic | Interval, local | BLE GATT repeated shutter | fujifilm §9 interval | implemented | `intervalometer-5-frames` uses FauxNY, not this peer | none (class) | capture-gated |
| Fujifilm Basic | Burst (menu-only) | camera menu | fujifilm §9 burst | unavailable | none | none (class) | not applicable, no BLE opcode |
| Fujifilm Basic | Movie start and stop | BLE GATT | fujifilm C15 | missing | none | none (class) | capture-gated |
| Fujifilm Basic | Zoom | none recovered | fujifilm §9 zoom | out of scope | none | none (class) | not applicable on fixed-lens bodies |
| Fujifilm Basic | Power on and off | BLE GATT | fujifilm C18, §5.4 | missing | none | none (class) | capture-gated |
| Fujifilm Basic | Wake | BLE advertisement and GATT | fujifilm §2.5, C19 | missing | none | none (class) | capture-gated |
| Fujifilm Basic | Sleep | BLE GATT status only | fujifilm §9 sleep, C21 | unavailable, status only | none | none (class) | not applicable, no sleep command |
| Fujifilm Basic | Live view | Wi-Fi | fujifilm §9 live view | missing | none | none (class) | not applicable over BLE |
| Fujifilm Basic | Image transfer, list, thumbnail | Wi-Fi, BLE handover | fujifilm §9 transfer | missing | none | none (class) | capture-gated |
| Fujifilm Basic | Clock and timezone | BLE GATT | fujifilm C28, C29, §5.3 | missing | none | none (class) | capture-gated |
| Fujifilm Basic | GPS push and pull | BLE GATT push, no pull | fujifilm C22, N03, §9 | implemented, partial: push only, pull unavailable | peer geotag request and packet | `fujifilm_camera_test` | capture-gated |
| Fujifilm Basic | Geotag enable and sync interval | BLE GATT | fujifilm C23, C25 | implemented, interval only, enable missing | peer does not model the enable | none (class) | capture-gated |
| Fujifilm Basic | Settings read | BLE GATT | fujifilm §4 | missing | none | none (class) | capture-gated |
| Fujifilm Basic | Settings write | BLE GATT | fujifilm §5 | missing | none | none (class) | capture-gated |
| Fujifilm Basic | Backup and restore | BLE GATT | fujifilm C38 to C50 | out of scope, unsafe | none | none | capture-gated |
| Fujifilm Basic | Firmware update | BLE GATT | fujifilm C53 to C55 | out of scope, unsafe | none | none | capture-gated |
| Fujifilm Basic | Pairing management | BLE GATT, token | fujifilm §7.6 | implemented | peer token accept | `fujifilm_camera_test` | capture-gated |
| Fujifilm Basic | Battery | none | fujifilm §5.5 | unavailable | none | none | not applicable |
| Fujifilm Basic | Storage | BLE GATT | fujifilm C62 | missing | none | none (class) | capture-gated |
| Fujifilm Basic | Error and busy status | BLE GATT | fujifilm C67, N23 | missing | none | none (class) | capture-gated |
| Fujifilm Basic | Model and firmware identity | BLE advertisement and GATT | fujifilm C56, C57 | missing, advertised name only | peer advertises name and token | `fujifilm_camera_test` | capture-gated |
| Fujifilm Basic | Model-specific controls | BLE GATT strings | fujifilm §9 model-specific | missing | none | none (class) | capture-gated |

### 3.2 Fujifilm Secure v4.0

| Camera | Feature | Transport | Spec row | Furble state | Simulator | Test | Hardware state |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Fujifilm Secure v4.0 | Still capture | BLE GATT | fujifilm C11 to C13, §9 still | implemented | peer Secure, shutter writes; scenario connect/cancel/reconnect | `fujifilm_camera_test`, `fujifilm_registration_gate_test` | capture-gated, wire traced, physical still not captured |
| Fujifilm Secure v4.0 | Half press and focus | BLE GATT | fujifilm §5.1 focus, C14 | implemented | peer Secure, focus write | `fujifilm_camera_test` | capture-gated, AF outcome not captured |
| Fujifilm Secure v4.0 | Bulb | BLE GATT, local hold | fujifilm §5.1 bulb | implemented, local hold only | peer counts the writes | none (bulb-specific) | capture-gated |
| Fujifilm Secure v4.0 | Self-timer (still menu-only) | camera menu; X-S20 movie BLE string | fujifilm §9 self-timer | unavailable, still menu-only, movie string capture-gated | none | none (class) | not applicable for stills, movie string capture-gated |
| Fujifilm Secure v4.0 | Interval, camera-side | none recovered | fujifilm §9 interval | missing | none | none (class) | capture-gated |
| Fujifilm Secure v4.0 | Interval, local | BLE GATT repeated shutter | fujifilm §9 interval | implemented | no Fujifilm scenario | none (class) | capture-gated |
| Fujifilm Secure v4.0 | Burst (menu-only) | camera menu | fujifilm §9 burst | unavailable | none | none (class) | not applicable, no BLE opcode |
| Fujifilm Secure v4.0 | Movie start and stop | BLE GATT | fujifilm C15 | missing | none | none (class) | capture-gated |
| Fujifilm Secure v4.0 | Zoom | none recovered | fujifilm §9 zoom | out of scope | none | none (class) | not applicable on X100VI, capture-gated on X-S20 |
| Fujifilm Secure v4.0 | Power on and off | BLE GATT | fujifilm C18, C19, §5.4 | missing | none | none (class) | capture-gated, transition not captured |
| Fujifilm Secure v4.0 | Wake | BLE advertisement and GATT | fujifilm §2.5, C19 | missing | none | none (class) | capture-gated, advertising form captured, wake not |
| Fujifilm Secure v4.0 | Sleep | BLE GATT status only | fujifilm §9 sleep, C21 | unavailable, status only | none | none (class) | not applicable, no sleep command |
| Fujifilm Secure v4.0 | Live view | Wi-Fi | fujifilm §9 live view | missing | none | none (class) | not applicable over BLE |
| Fujifilm Secure v4.0 | Image transfer, list, thumbnail | Wi-Fi, BLE handover | fujifilm C17, C68, §9 transfer | missing | none | none (class) | capture-gated |
| Fujifilm Secure v4.0 | Clock and timezone | BLE GATT | fujifilm C28, C29, §5.3 | missing | none | none (class) | capture-gated, `F9150137` semantics disputed |
| Fujifilm Secure v4.0 | GPS push and pull | BLE GATT push, no pull | fujifilm C22, N03, §5.2 | implemented, partial: push only, pull unavailable | peer request and packet, byte exact | `fujifilm_camera_test` | verified on the request and packet, EXIF placement capture-gated |
| Fujifilm Secure v4.0 | Geotag enable and sync interval | BLE GATT | fujifilm C23, C25 | implemented, interval only, enable missing | peer does not model the enable | none (class) | verified for the interval echo, enable capture-gated |
| Fujifilm Secure v4.0 | Settings read | BLE GATT | fujifilm §4 | missing, pairing status only | peer status read | `fujifilm_registration_gate_test` | capture-gated |
| Fujifilm Secure v4.0 | Settings write | BLE GATT | fujifilm C23, C30 to C37 | implemented, interval only | peer interval write | none (class) | capture-gated |
| Fujifilm Secure v4.0 | Backup and restore | BLE GATT | fujifilm C38 to C50 | out of scope, unsafe | none | none | capture-gated |
| Fujifilm Secure v4.0 | Firmware update | BLE GATT | fujifilm C53 to C55 | out of scope, unsafe | none | none | capture-gated |
| Fujifilm Secure v4.0 | Pairing management | BLE GATT | fujifilm C01, C02, C03, §7.2 | implemented | peer status, ack, name, subscriptions | `fujifilm_registration_gate_test`, `fujifilm_stale_bond_test` | verified on X100VI 1.31/1.32 and 2026-09 bench |
| Fujifilm Secure v4.0 | Battery | none | fujifilm §5.5 | unavailable | none | none | not applicable |
| Fujifilm Secure v4.0 | Storage | BLE GATT | fujifilm C62 | missing | none | none (class) | capture-gated |
| Fujifilm Secure v4.0 | Error and busy status | BLE GATT | fujifilm C67, N23 | missing | none | none (class) | capture-gated |
| Fujifilm Secure v4.0 | Model and firmware identity | BLE advertisement and GATT | fujifilm C56 to C60 | missing, advertised name and serial only | peer advertises model and serial | `fujifilm_device_name_test` | verified for the advertised name, GATT identity capture-gated |
| Fujifilm Secure v4.0 | Model-specific controls | BLE GATT strings | fujifilm §9 model-specific | missing | none | none (class) | capture-gated |

### 3.3 Fujifilm Secure v4.1

| Camera | Feature | Transport | Spec row | Furble state | Simulator | Test | Hardware state |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Fujifilm Secure v4.1 | Still capture | BLE GATT | fujifilm C11 to C13, §7.7 | implemented, shared v4.0 path | peer is v4.0 only | shared v4.0 tests | capture-gated, no X-E5 capture |
| Fujifilm Secure v4.1 | Half press and focus | BLE GATT | fujifilm §5.1, C14 | implemented | peer is v4.0 only | shared v4.0 tests | capture-gated |
| Fujifilm Secure v4.1 | Bulb | BLE GATT, local hold | fujifilm §5.1 | implemented, local hold only | none | none | capture-gated |
| Fujifilm Secure v4.1 | Self-timer (menu-only) | camera menu | fujifilm §9 | unavailable | none | none | not applicable |
| Fujifilm Secure v4.1 | Interval, camera-side | none recovered | fujifilm §9 | missing | none | none | capture-gated |
| Fujifilm Secure v4.1 | Interval, local | BLE GATT repeated shutter | fujifilm §9 | implemented | none | none | capture-gated |
| Fujifilm Secure v4.1 | Burst (menu-only) | camera menu | fujifilm §9 | unavailable | none | none | not applicable, no BLE opcode |
| Fujifilm Secure v4.1 | Movie start and stop | BLE GATT | fujifilm C15 | missing | none | none | capture-gated |
| Fujifilm Secure v4.1 | Zoom | none recovered | fujifilm §9 | out of scope | none | none | not applicable on X-E5, unknown on GFX100RF |
| Fujifilm Secure v4.1 | Power on and off | BLE GATT | fujifilm C18, C19 | missing | none | none | capture-gated |
| Fujifilm Secure v4.1 | Wake | BLE advertisement and GATT | fujifilm §2.5 | missing | none | none | capture-gated |
| Fujifilm Secure v4.1 | Sleep | BLE GATT status only | fujifilm §9 sleep, C21 | unavailable, status only | none | none | not applicable, no sleep command |
| Fujifilm Secure v4.1 | Live view | Wi-Fi | fujifilm §9 | missing | none | none | not applicable over BLE |
| Fujifilm Secure v4.1 | Image transfer, list, thumbnail | Wi-Fi, BLE handover | fujifilm C17, C68 | missing | none | none | capture-gated |
| Fujifilm Secure v4.1 | Clock and timezone | BLE GATT | fujifilm C28, C29 | missing | none | none | capture-gated |
| Fujifilm Secure v4.1 | GPS push and pull | BLE GATT push, no pull | fujifilm C22, N03 | implemented, push only | peer is v4.0 only | shared v4.0 tests | capture-gated, issue 208 logs corroborate `01 00` |
| Fujifilm Secure v4.1 | Geotag enable and sync interval | BLE GATT | fujifilm C23, C25 | implemented, interval only, enable missing | none | none | capture-gated |
| Fujifilm Secure v4.1 | Settings read | BLE GATT | fujifilm §4 | missing | none | none | capture-gated |
| Fujifilm Secure v4.1 | Settings write | BLE GATT | fujifilm C23, C30 to C37 | implemented, interval only | none | none | capture-gated |
| Fujifilm Secure v4.1 | Backup and restore | BLE GATT | fujifilm C38 to C50 | out of scope, unsafe | none | none | capture-gated |
| Fujifilm Secure v4.1 | Firmware update | BLE GATT | fujifilm C53 to C55 | out of scope, unsafe | none | none | capture-gated |
| Fujifilm Secure v4.1 | Pairing management | BLE GATT | fujifilm §7.7, C05 | capture-gated, `8B5ECF55` not written | none | none | capture-gated, no X-E5 capture |
| Fujifilm Secure v4.1 | Battery | none | fujifilm §5.5 | unavailable | none | none | not applicable |
| Fujifilm Secure v4.1 | Storage | BLE GATT | fujifilm C62 | missing | none | none | capture-gated |
| Fujifilm Secure v4.1 | Error and busy status | BLE GATT | fujifilm C67, N23 | missing | none | none | capture-gated |
| Fujifilm Secure v4.1 | Model and firmware identity | BLE advertisement and GATT | fujifilm C56 to C60 | missing, advertised name and serial only | none | `fujifilm_device_name_test` covers the name rule | capture-gated, no X-E5 image |
| Fujifilm Secure v4.1 | Model-specific controls | BLE GATT | fujifilm §7.7, C05 | missing | none | none | capture-gated |

### 3.4 Ricoh GR III, GR IIIx, and HDF

| Camera | Feature | Transport | Spec row | Furble state | Simulator | Test | Hardware state |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Ricoh GR III/IIIx | Still capture | BLE GATT | ricoh CM-16, F-01 | implemented, gated on a fresh Operation Mode CAPTURE read | peer with standby mode and capture writes | `ricoh_focus_test`, `ricoh_sleep_gate_test` | capture-gated, author-reported capture on GR III/IIIx |
| Ricoh GR III/IIIx | Half press and focus | BLE none, HTTP focus only | ricoh F-02, CM-30 | unavailable, no-op by design | peer accepts no focus write | `ricoh_focus_test` | not applicable over BLE |
| Ricoh GR III/IIIx | Bulb | BLE, no stop primitive | ricoh F-04 | unavailable, release is ignored | none | none | capture-gated |
| Ricoh GR III/IIIx | Self-timer | BLE GATT | ricoh F-05, CM-17, CM-18 | missing, `TIMER_2S` unused | none | none | capture-gated, value table app-static |
| Ricoh GR III/IIIx | Interval, camera-side | BLE GATT | ricoh F-06, CM-18 | missing | none | none | capture-gated |
| Ricoh GR III/IIIx | Interval, local | BLE GATT repeated capture | ricoh F-06 | implemented | none | none | capture-gated |
| Ricoh GR III/IIIx | Burst | BLE GATT Drive Mode | ricoh F-07, CM-18 | missing | none | none | capture-gated |
| Ricoh GR III/IIIx | Movie start and stop | BLE GATT | ricoh F-08, CM-26, CM-29 | missing | none | none | capture-gated |
| Ricoh GR III/IIIx | Zoom | none, fixed lens | ricoh F-09 | out of scope | none | none | not applicable |
| Ricoh GR III/IIIx | Power on and off | BLE and connect wake | ricoh CM-01, F-10, F-11 | missing, connect wake only | none | none | capture-gated |
| Ricoh GR III/IIIx | Wake | BLE connect | ricoh F-12 | implemented, connect only | peer accepts connect | `ricoh_sleep_gate_test` covers the capture gate, not the wake | capture-gated |
| Ricoh GR III/IIIx | Sleep | BLE GATT read only | ricoh C-17, F-13 | unavailable, value 2 is refused on write | none | none | not applicable, menu or auto sleep only |
| Ricoh GR III/IIIx | Live view | Wi-Fi HTTP | ricoh F-14 | missing | none | none | capture-gated |
| Ricoh GR III/IIIx | Image transfer, list, thumbnail | Wi-Fi HTTP | ricoh F-15 to F-17 | missing | none | none | capture-gated |
| Ricoh GR III/IIIx | Clock and timezone | BLE GATT | ricoh CM-03, CM-40, F-18 to F-20 | missing | none | none | capture-gated |
| Ricoh GR III/IIIx | GPS push and pull | BLE GATT push, read exists | ricoh CM-31, F-21, F-22 | implemented, partial: push only, pull missing | peer accepts the 32-byte packet | `ricoh_focus_test` | capture-gated, author-reported GR IIIx capture |
| Ricoh GR III/IIIx | Geotag enable and sync interval | BLE GATT | ricoh CM-04, F-23 | implemented, enable only, no camera interval | peer records the enable write | `ricoh_focus_test` | capture-gated |
| Ricoh GR III/IIIx | Settings read | BLE GATT | ricoh F-24 | implemented, partial: model and state probe | peer serves model and state | `ricoh_focus_test` | capture-gated |
| Ricoh GR III/IIIx | Settings write | BLE GATT | ricoh F-25 | implemented, partial: Drive Mode and geotag | peer records both | `ricoh_focus_test` | capture-gated |
| Ricoh GR III/IIIx | Backup and restore | none in BLE | ricoh F-26, F-27 | out of scope | none | none | not applicable |
| Ricoh GR III/IIIx | Firmware update | SD card only | ricoh F-28 | out of scope | none | none | not applicable |
| Ricoh GR III/IIIx | Pairing management | BLE and camera menu | ricoh F-29, F-30, F-31 | implemented, partial: bond delete on a new pairing only | none | `camera_regression_test` bond policy | capture-gated |
| Ricoh GR III/IIIx | Battery | BLE GATT | ricoh CM-39, F-32 | missing | none | none | capture-gated |
| Ricoh GR III/IIIx | Storage | BLE GATT | ricoh CM-44, F-33 | missing | none | none | capture-gated |
| Ricoh GR III/IIIx | Error and busy status | BLE GATT | ricoh F-35, CM-91 | implemented, partial: capture status logged | peer can emit notifications | `ricoh_control_flap_test` exercises disconnect, not status | capture-gated |
| Ricoh GR III/IIIx | Model and firmware identity | BLE GATT | ricoh C-11, F-36 | implemented, model read | peer serves the model | `ricoh_focus_test` | capture-gated |
| Ricoh GR III/IIIx | Model-specific controls | BLE GATT | ricoh F-38, F-43, F-45 | missing | none | none | capture-gated |

### 3.5 Ricoh GR IV and GR IV HDF

| Camera | Feature | Transport | Spec row | Furble state | Simulator | Test | Hardware state |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Ricoh GR IV/HDF | Still capture | BLE GATT | ricoh CM-16, CM-18, F-01, F-37 | implemented | peer with standby mode and capture writes | `ricoh_focus_test`, `ricoh_sleep_gate_test` | verified at the write level by author-reported GR IV capture, physical image not captured |
| Ricoh GR IV/HDF | Half press and focus | BLE none, HTTP focus only | ricoh F-02, CM-30 | unavailable, no-op by design | peer accepts no focus write | `ricoh_focus_test` | not applicable over BLE |
| Ricoh GR IV/HDF | Bulb | BLE, no stop primitive | ricoh F-04 | unavailable, release is ignored | none | none | capture-gated |
| Ricoh GR IV/HDF | Self-timer | BLE GATT | ricoh F-05, CM-17, CM-18 | missing, `TIMER_2S` unused | none | none | capture-gated |
| Ricoh GR IV/HDF | Interval, camera-side | BLE GATT | ricoh F-06, CM-18 | missing | none | none | capture-gated |
| Ricoh GR IV/HDF | Interval, local | BLE GATT repeated capture | ricoh F-06 | implemented | none | none | capture-gated |
| Ricoh GR IV/HDF | Burst | BLE GATT Drive Mode | ricoh F-07, CM-18 | missing | none | none | capture-gated |
| Ricoh GR IV/HDF | Movie start and stop | BLE GATT | ricoh F-08, CM-26, CM-29 | missing | none | none | capture-gated |
| Ricoh GR IV/HDF | Zoom | none, fixed lens | ricoh F-09 | out of scope | none | none | not applicable |
| Ricoh GR IV/HDF | Power on and off | BLE GATT | ricoh CM-01, F-10, F-11 | missing, off is out of scope | none | none | capture-gated |
| Ricoh GR IV/HDF | Wake | BLE connect and power | ricoh F-12, F-47 | capture-gated, connect does not wake a BLE_STARTUP body | peer models standby and flap | `ricoh_sleep_gate_test`, `ricoh_control_flap_test` | verified for the standby trap by author-reported capture |
| Ricoh GR IV/HDF | Sleep | BLE GATT read only | ricoh C-17, F-13 | unavailable, value 2 is refused on write | none | none | not applicable, menu or auto sleep only |
| Ricoh GR IV/HDF | Live view | Wi-Fi HTTP | ricoh F-14 | missing | none | none | capture-gated |
| Ricoh GR IV/HDF | Image transfer, list, thumbnail | Wi-Fi HTTP | ricoh F-15 to F-17 | missing | none | none | capture-gated |
| Ricoh GR IV/HDF | Clock and timezone | BLE GATT | ricoh CM-03, CM-40, F-18 to F-20 | missing | none | none | capture-gated |
| Ricoh GR IV/HDF | GPS push and pull | BLE GATT push, read exists | ricoh CM-31, F-21, F-22 | implemented, partial: push only, pull missing | peer accepts the packet | `ricoh_focus_test` | verified for the packet layout, EXIF capture-gated |
| Ricoh GR IV/HDF | Geotag enable and sync interval | BLE GATT | ricoh CM-04, F-23 | implemented, enable only, no camera interval | peer records the enable write | `ricoh_focus_test` | verified for the enable write, notification capture-gated |
| Ricoh GR IV/HDF | Settings read | BLE GATT | ricoh F-24 | implemented, partial: model and state probe | peer serves model and state | `ricoh_focus_test` | capture-gated |
| Ricoh GR IV/HDF | Settings write | BLE GATT | ricoh F-25 | implemented, partial: Drive Mode and geotag | peer records both | `ricoh_focus_test` | capture-gated |
| Ricoh GR IV/HDF | Backup and restore | none in BLE | ricoh F-26, F-27 | out of scope | none | none | not applicable |
| Ricoh GR IV/HDF | Firmware update | BLE control plus Wi-Fi HTTP | ricoh F-28 | out of scope, unsafe | none | none | capture-gated |
| Ricoh GR IV/HDF | Pairing management | BLE and camera menu | ricoh F-29, F-30, F-54 | implemented, partial: bond delete only | none | `camera_regression_test` bond policy | capture-gated |
| Ricoh GR IV/HDF | Battery | BLE GATT | ricoh CM-39, F-32 | missing | none | none | capture-gated |
| Ricoh GR IV/HDF | Storage | BLE GATT | ricoh CM-44, F-33 | missing | none | none | capture-gated |
| Ricoh GR IV/HDF | Error and busy status | BLE GATT | ricoh F-35, CM-91 | implemented, partial: capture status logged | peer can emit notifications | `ricoh_control_flap_test` | capture-gated |
| Ricoh GR IV/HDF | Model and firmware identity | BLE GATT | ricoh C-11, F-36 | implemented, model read | peer serves the model | `ricoh_focus_test` | capture-gated, no GR IV GATT dump |
| Ricoh GR IV/HDF | Model-specific controls | BLE GATT | ricoh F-43, F-44, F-45 | missing | none | none | capture-gated |

### 3.6 Canon EOS Smart

| Camera | Feature | Transport | Spec row | Furble state | Simulator | Test | Hardware state |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Canon EOS Smart | Still capture | BLE GATT | canon CS-16, CS-17, F01 | implemented | none | none (class) | capture-gated |
| Canon EOS Smart | Half press and focus | BLE GATT | canon CS-18, CS-19, F02 | implemented | none | none (class) | capture-gated, AF outcome not captured |
| Canon EOS Smart | Bulb | BLE GATT, local hold | canon F04 | implemented, local hold only | none | none | capture-gated |
| Canon EOS Smart | Self-timer (menu-only) | camera menu, status only | canon F05 | unavailable | none | none | not applicable |
| Canon EOS Smart | Interval, camera-side | none | canon F06 | out of scope, no camera API | none | none | not applicable |
| Canon EOS Smart | Interval, local | BLE GATT repeated shutter | canon F06 | implemented | none | none | capture-gated |
| Canon EOS Smart | Burst (menu-only) | camera menu | canon F07 | unavailable | none | none | not applicable |
| Canon EOS Smart | Movie start and stop | BLE GATT | canon CS-22, CS-23, F08 | missing | none | none | capture-gated |
| Canon EOS Smart | Zoom | BLE GATT | canon CS-24 to CS-27, F09 | missing | none | none | capture-gated, no app call site |
| Canon EOS Smart | Power on and off | BLE GATT | canon CS-14, F10, F11 | missing | none | none | capture-gated |
| Canon EOS Smart | Wake | BLE GATT | canon F12 | missing, writes mode `02` not `03` | none | none | capture-gated |
| Canon EOS Smart | Sleep | BLE advertisement and status | canon F13 | unavailable, status only | none | none | not applicable, no sleep command |
| Canon EOS Smart | Live view | Wi-Fi, BLE status only | canon F14 | missing | none | none | capture-gated |
| Canon EOS Smart | Image transfer, list, thumbnail | Wi-Fi and PTP | canon F15 to F17 | missing | none | none | capture-gated |
| Canon EOS Smart | Clock and timezone | PTP, GPS timestamp only | canon F18 to F20 | missing | none | none | capture-gated |
| Canon EOS Smart | GPS push and pull | BLE GATT push, no pull | canon CS-34, F21, F22 | implemented, partial: one-byte enable and 20-byte packet | none | none (class) | capture-gated |
| Canon EOS Smart | Geotag enable and sync interval | BLE GATT | canon CS-28 to CS-33, F23 | implemented, partial: no source select, no stop, no request ack | none | none (class) | capture-gated |
| Canon EOS Smart | Settings read | BLE GATT | canon CS-12, CS-13, CS-37, F24 | missing | none | none | capture-gated |
| Canon EOS Smart | Settings write | BLE GATT and PTP | canon CS-10, F25 | implemented, partial: mode only | none | none | capture-gated |
| Canon EOS Smart | Backup and restore | none known | canon F26, F27 | unavailable | none | none | not applicable |
| Canon EOS Smart | Firmware update | network and card | canon F28 | out of scope | none | none | capture-gated |
| Canon EOS Smart | Pairing management | BLE GATT and OS bond | canon CS-01 to CS-06, F29 | capture-gated, order differs from the app | none | none (class) | capture-gated |
| Canon EOS Smart | Battery | error category only | canon F32 | unavailable, no level characteristic | none | none | not applicable |
| Canon EOS Smart | Storage | error category only | canon F33 | unavailable, no free-space surface | none | none | not applicable |
| Canon EOS Smart | Error and busy status | BLE GATT | canon CS-37, F35 | missing | none | none | capture-gated |
| Canon EOS Smart | Model and firmware identity | BLE advertisement and DIS | canon F36 | missing, no product ID parse | none | none (class) | capture-gated |
| Canon EOS Smart | Model-specific controls | app model gates | canon F37, F42 | missing | none | none | capture-gated |

### 3.7 Canon EOS Remote

| Camera | Feature | Transport | Spec row | Furble state | Simulator | Test | Hardware state |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Canon EOS Remote | Still capture | BLE GATT | canon CR-04, CR-05, F01 | implemented | none | none (class) | capture-gated |
| Canon EOS Remote | Half press and focus | BLE GATT | canon CR-06, F02 | implemented | none | none (class) | capture-gated |
| Canon EOS Remote | Bulb | BLE GATT, local hold | canon F04 | implemented, local hold only | none | none | capture-gated |
| Canon EOS Remote | Self-timer | BLE GATT 2 s delay bit | canon CR-07, F05 | missing | none | none | capture-gated, byte is inference |
| Canon EOS Remote | Interval, camera-side | none | canon F06 | out of scope | none | none | not applicable |
| Canon EOS Remote | Interval, local | BLE GATT repeated shutter | canon F06 | implemented | none | none | capture-gated |
| Canon EOS Remote | Burst (menu-only) | camera menu | canon F07 | unavailable | none | none | not applicable |
| Canon EOS Remote | Movie start and stop | BLE GATT | canon CR-08, F08 | missing | none | none | capture-gated, byte is inference |
| Canon EOS Remote | Zoom | BLE GATT | canon CR-09, CR-10, F09 | missing | none | none | capture-gated, byte is inference |
| Canon EOS Remote | Power on and off | none | canon F10, F11 | unavailable | none | none | not applicable |
| Canon EOS Remote | Wake | unknown | canon F12 | missing | none | none | capture-gated |
| Canon EOS Remote | Sleep | BLE advertisement and status | canon F13 | unavailable, status only | none | none | not applicable, no sleep command |
| Canon EOS Remote | Live view | none over BLE | canon F14 | unavailable | none | none | not applicable |
| Canon EOS Remote | Image transfer, list, thumbnail | none over BLE | canon F15 to F17 | unavailable | none | none | not applicable |
| Canon EOS Remote | Clock and timezone | none over BLE | canon F18 to F20 | unavailable | none | none | not applicable |
| Canon EOS Remote | GPS push and pull | none | canon F21, F22 | unavailable | none | none | not applicable |
| Canon EOS Remote | Geotag enable and sync interval | none | canon F23 | unavailable | none | none | not applicable |
| Canon EOS Remote | Settings read | none | canon F24 | unavailable | none | none | not applicable |
| Canon EOS Remote | Settings write | none | canon F25 | unavailable | none | none | not applicable |
| Canon EOS Remote | Backup and restore | none | canon F26, F27 | unavailable | none | none | not applicable |
| Canon EOS Remote | Firmware update | none over BLE | canon F28 | out of scope | none | none | not applicable |
| Canon EOS Remote | Pairing management | BLE GATT | canon CR-01 to CR-03, F29 | implemented, name write only | none | none (class) | capture-gated |
| Canon EOS Remote | Battery | none | canon F32 | unavailable | none | none | not applicable |
| Canon EOS Remote | Storage | none | canon F33 | unavailable | none | none | not applicable |
| Canon EOS Remote | Error and busy status | none | canon F35 | unavailable | none | none | not applicable |
| Canon EOS Remote | Model and firmware identity | none | canon F36 | unavailable | none | none | not applicable |
| Canon EOS Remote | Model-specific controls | none | canon F37 | unavailable | none | none | not applicable |

### 3.8 Nikon Remote

| Camera | Feature | Transport | Spec row | Furble state | Simulator | Test | Hardware state |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Nikon Remote | Still capture | BLE GATT | nikon R5, §9.1 | implemented | none | none (class), `camera_regression_test` null-safety only | capture-gated |
| Nikon Remote | Half press and focus | none | nikon §9.1 | unavailable, no focus characteristic | none | none | not applicable |
| Nikon Remote | Bulb | none, local hold | nikon §9.1 | out of scope, no bulb opcode | none | none | not applicable |
| Nikon Remote | Self-timer | none | nikon §9.1 | unavailable | none | none | not applicable |
| Nikon Remote | Interval, camera-side | none | nikon §9.1 | out of scope | none | none | not applicable |
| Nikon Remote | Interval, local | BLE GATT repeated press | nikon §9.1 | implemented | none | none | capture-gated |
| Nikon Remote | Burst | none | nikon §9.1 | unavailable | none | none | not applicable |
| Nikon Remote | Movie start and stop | none | nikon §9.1 | unavailable, `MODE_VIDEO` never sent | none | none | not applicable |
| Nikon Remote | Zoom | none | nikon §9.1 | unavailable | none | none | not applicable |
| Nikon Remote | Power on and off | none | nikon §9.1 | unavailable | none | none | not applicable |
| Nikon Remote | Wake | none | nikon §9.1 | unavailable | none | none | not applicable |
| Nikon Remote | Sleep | BLE advertisement capability bit | nikon §9.1 | unavailable, capability bit only | none | none | not applicable |
| Nikon Remote | Live view | none over BLE | nikon §9.1 | unavailable | none | none | not applicable |
| Nikon Remote | Image transfer, list, thumbnail | none over BLE | nikon §9.1 | unavailable | none | none | not applicable |
| Nikon Remote | Clock and timezone | none | nikon §9.1 | unavailable | none | none | not applicable |
| Nikon Remote | GPS push and pull | none | nikon §9.1 | unavailable | none | none | not applicable |
| Nikon Remote | Geotag enable and sync interval | none | nikon §9.1 | unavailable | none | none | not applicable |
| Nikon Remote | Settings read | BLE GATT | nikon R7 to R13, §9.1 | missing | none | none | capture-gated |
| Nikon Remote | Settings write | BLE GATT | nikon R14, §9.1 | missing | none | none | capture-gated |
| Nikon Remote | Backup and restore | none known | nikon §9.1 | unavailable | none | none | not applicable |
| Nikon Remote | Firmware update | none over BLE | nikon §9.1 | out of scope | none | none | not applicable |
| Nikon Remote | Pairing management | BLE GATT | nikon R1 to R4, §7.1 | implemented | none | none (class) | capture-gated, B600 firmware-static only |
| Nikon Remote | Battery | BLE GATT | nikon §9.1 | missing | none | none | capture-gated |
| Nikon Remote | Storage | none | nikon §9.1 | unavailable | none | none | not applicable |
| Nikon Remote | Error and busy status | BLE GATT | nikon R8, R10, §9.1 | missing | none | none | capture-gated |
| Nikon Remote | Model and firmware identity | BLE GATT | nikon §9.1 | missing | none | none | capture-gated |
| Nikon Remote | Model-specific controls | BLE GATT | nikon §9.1 | missing | none | none | capture-gated, full profile unknown |

### 3.9 Nikon Smart

| Camera | Feature | Transport | Spec row | Furble state | Simulator | Test | Hardware state |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Nikon Smart | Still capture | BLE GATT | nikon S26, §9.2 | unavailable, no key event and the camera cannot become active | none | none (class) | capture-gated |
| Nikon Smart | Half press and focus | BLE GATT | nikon S26 key 2, §9.2 | unavailable | none | none | capture-gated |
| Nikon Smart | Bulb | PTP/IP | nikon §9.2 | unavailable | none | none | capture-gated |
| Nikon Smart | Self-timer | none | nikon §9.2 | unavailable | none | none | not applicable |
| Nikon Smart | Interval, camera-side | BLE GATT | nikon S28, §9.2 | unavailable | none | none | capture-gated |
| Nikon Smart | Interval, local | none reachable | nikon §9.2 | unavailable | none | none | not applicable |
| Nikon Smart | Burst | none | nikon §9.2 | unavailable | none | none | not applicable |
| Nikon Smart | Movie start and stop | BLE GATT | nikon S26 key 4, §9.2 | unavailable | none | none | capture-gated |
| Nikon Smart | Zoom | BLE and PTP | nikon §9.2 | unavailable | none | none | capture-gated |
| Nikon Smart | Power on and off | BLE GATT | nikon S14, S15, §9.2 | unavailable | none | none | capture-gated |
| Nikon Smart | Wake | BLE advertisement and GATT | nikon S14, §9.2 | unavailable | none | none | capture-gated |
| Nikon Smart | Sleep | BLE advertisement and `2001` | nikon §9.2 | unavailable, values unknown | none | none | capture-gated |
| Nikon Smart | Live view | PTP/IP | nikon §9.2 | unavailable | none | none | not applicable over BLE |
| Nikon Smart | Image transfer, list, thumbnail | Wi-Fi or Classic PTP | nikon S31, §9.2 | unavailable | none | none | capture-gated |
| Nikon Smart | Clock and timezone | BLE GATT | nikon S7, §9.2 | unavailable, code exists but is unreachable | none | none | capture-gated |
| Nikon Smart | GPS push and pull | BLE GATT push, no pull | nikon S8, S9, §9.2 | unavailable, code exists but is unreachable | none | none | capture-gated |
| Nikon Smart | Geotag enable and sync interval | BLE GATT | nikon S8, §9.2 | unavailable | none | none | capture-gated |
| Nikon Smart | Settings read | BLE GATT | nikon S10, S13, §9.2 | unavailable | none | none | capture-gated |
| Nikon Smart | Settings write | BLE GATT | nikon S16, S17, S19, §9.2 | unavailable | none | none | capture-gated |
| Nikon Smart | Backup and restore | PTP, unconfirmed | nikon §9.2 | unavailable | none | none | capture-gated |
| Nikon Smart | Firmware update | BLE feature bit and network | nikon S32, §9.2 | unavailable | none | none | capture-gated |
| Nikon Smart | Pairing management | BLE GATT plus Classic | nikon S1 to S5, §7.3, §7.7 | unavailable, BLE half runs and Classic is blocked | none | none (class) | capture-gated, B600 image and SnapBridge app only |
| Nikon Smart | Battery | BLE GATT | nikon §9.2 | unavailable | none | none | capture-gated |
| Nikon Smart | Storage | none | nikon §9.2 | unavailable | none | none | not applicable |
| Nikon Smart | Error and busy status | BLE GATT | nikon S22, §9.2 | unavailable | none | none | capture-gated |
| Nikon Smart | Model and firmware identity | BLE GATT | nikon S6, S11, §9.2 | unavailable | none | none | capture-gated |
| Nikon Smart | Model-specific controls | BLE GATT | nikon S23 to S30, §9.2 | unavailable | none | none | capture-gated |

### 3.10 Sony

| Camera | Feature | Transport | Spec row | Furble state | Simulator | Test | Hardware state |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Sony | Still capture | BLE GATT, PTP/IP primary | sony F-01, F-03, C-32 | implemented, full press only | none | none (class), advertisement parsing only | capture-gated |
| Sony | Half press and focus | BLE GATT, PTP/IP primary | sony F-02, C-33 | implemented | none | none (class) | capture-gated, outcome unproven |
| Sony | Bulb | PTP/IP | sony F-04 | implemented, local hold only | none | none | capture-gated |
| Sony | Self-timer | PTP/IP | sony F-05 | missing, PTP-only | none | none | capture-gated |
| Sony | Interval, camera-side | PTP/IP | sony F-06 | missing | none | none | capture-gated |
| Sony | Interval, local | BLE GATT repeated shutter | sony F-06 | implemented | none | none | capture-gated |
| Sony | Burst | unknown | sony F-07 | missing | none | none | capture-gated |
| Sony | Movie start and stop | BLE DiRxTx, PTP/IP | sony F-08, F-60 | missing | none | none | capture-gated |
| Sony | Zoom | PTP/IP | sony F-09 | out of scope, BLE | none | none | capture-gated |
| Sony | Power on and off | BLE GATT | sony C-01, F-10, F-11 | missing | none | none | capture-gated |
| Sony | Wake | BLE advertisement and GATT | sony F-12, F-54 | implemented, connect only | none | none | capture-gated |
| Sony | Sleep | unknown | sony F-13 | unavailable | none | none | capture-gated |
| Sony | Live view | Wi-Fi PTP/IP | sony F-14 | missing | none | none | capture-gated |
| Sony | Image transfer, list, thumbnail | Wi-Fi PTP/IP and MTP | sony F-15 to F-17 | missing | none | none | capture-gated |
| Sony | Clock and timezone | BLE GATT | sony C-06, F-19, F-20 | missing | none | none | capture-gated |
| Sony | GPS push and pull | BLE GATT push, no pull | sony C-22, C-23, F-21, F-22 | implemented, partial: 95 bytes only, zero offset, no lock release | none | none (class) | capture-gated |
| Sony | Geotag enable and sync interval | BLE GATT | sony C-20, C-21, C-24, F-23 | implemented, partial: no `DD01` and no release | none | none (class) | capture-gated |
| Sony | Settings read | BLE GATT | sony C-14 to C-19, F-24 | implemented, partial: `DD30` and `DD31` only | none | none (class) | capture-gated |
| Sony | Settings write | BLE GATT and PTP/IP | sony C-04 to C-13, F-25 | missing | none | none | capture-gated |
| Sony | Backup and restore | Wi-Fi PTP/IP | sony F-26, F-27 | missing | none | none | capture-gated |
| Sony | Firmware update | BLE read and Wi-Fi PTP/IP upload | sony F-28 | out of scope, unsafe | none | none | capture-gated |
| Sony | Pairing management | BLE GATT | sony C-31, C-32, F-29 | missing, no `EE01` and no `EE03` | none | none (class) | capture-gated |
| Sony | Battery | BLE GATT | sony F-32, `CC10` | missing | none | none | capture-gated |
| Sony | Storage | BLE GATT | sony F-33, `CC0F` | missing | none | none | capture-gated |
| Sony | Error and busy status | BLE GATT | sony F-35, `CC09`, `EE03` | missing | none | none | capture-gated |
| Sony | Model and firmware identity | BLE GATT and advertisement | sony C-16, C-17, C-19, F-36 | missing | none | advertisement parsing only | capture-gated |
| Sony | Model-specific controls | BLE and PTP/IP | sony F-37 | missing | none | none | capture-gated |

### 3.11 Panasonic LUMIX

The MEI0 and APS generations share the control, location, and clock
characteristics. They differ in the identity block: MEI0 is a plain 16-byte write,
APS reads a per-connection key and writes a 20-byte XOR and word-swapped block.
The rows below apply to both unless the feature cell says APS.

| Camera | Feature | Transport | Spec row | Furble state | Simulator | Test | Hardware state |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Panasonic LUMIX | Still capture | BLE GATT | panasonic C09, C10, F01 | implemented, bare `04` and `05` with no session handshake | none | none (class), advertisement parsing only | capture-gated |
| Panasonic LUMIX | Half press and focus | none | panasonic F02, C06, C07 | unavailable, the code sends Confirm and Start instead | none | none (class) | not applicable |
| Panasonic LUMIX | Bulb | BLE GATT, local hold | panasonic F04 | implemented, local hold only | none | none | capture-gated |
| Panasonic LUMIX | Self-timer | none over BLE | panasonic F05 | unavailable | none | none | not applicable |
| Panasonic LUMIX | Interval, camera-side | Wi-Fi CGI | panasonic F06 | missing | none | none | capture-gated |
| Panasonic LUMIX | Interval, local | BLE GATT repeated press | panasonic F06 | implemented | none | none | capture-gated |
| Panasonic LUMIX | Burst | unknown | panasonic F07 | missing | none | none | capture-gated |
| Panasonic LUMIX | Movie start and stop | BLE GATT | panasonic C11, C12, F08 | missing | none | none | capture-gated |
| Panasonic LUMIX | Zoom | Wi-Fi live view | panasonic F09 | out of scope, BLE | none | none | capture-gated |
| Panasonic LUMIX | Power on and off | none over BLE | panasonic F10, F11 | unavailable | none | none | not applicable |
| Panasonic LUMIX | Wake | BLE advertisement, GATT, Wi-Fi | panasonic F12 | missing | none | none | capture-gated |
| Panasonic LUMIX | Sleep | BLE advertisement, camera menu | panasonic F13 | unavailable | none | none | not applicable |
| Panasonic LUMIX | Live view | Wi-Fi | panasonic F14 | missing | none | none | capture-gated |
| Panasonic LUMIX | Image transfer, list, thumbnail | BLE start, Wi-Fi DLNA | panasonic C16 to C20, F15 to F17 | missing | none | none | capture-gated |
| Panasonic LUMIX | Clock and timezone | BLE GATT | panasonic C15, F18 to F20 | implemented, partial: written only after GPS, offset and DST zero | none | none (class) | capture-gated |
| Panasonic LUMIX | GPS push and pull | BLE GATT push, no pull | panasonic C13, C14, F21, F22 | implemented, partial: no stop packet, no readiness | none | none (class) | capture-gated, author-reported S5 EXIF check |
| Panasonic LUMIX | Geotag enable and sync interval | BLE GATT readiness | panasonic C13, F23, F45 | missing, no readiness subscription | none | none (class) | capture-gated |
| Panasonic LUMIX | Settings read | BLE GATT | panasonic C21 to C27, F24 | missing | none | none (class) | capture-gated |
| Panasonic LUMIX | Settings write | BLE identity and name, Wi-Fi CGI | panasonic C01, C02, F25 | implemented, partial: fixed MEI0 and plain name only | none | none (class) | capture-gated |
| Panasonic LUMIX | APS identity | BLE GATT | panasonic C03 to C05, H03 to H05 | missing, no key read and no XOR block | none | none | capture-gated |
| Panasonic LUMIX | Backup and restore | unknown | panasonic F26, F27 | out of scope | none | none | capture-gated |
| Panasonic LUMIX | Firmware update | HTTP then Wi-Fi | panasonic F28 | out of scope, unsafe | none | none | capture-gated |
| Panasonic LUMIX | Pairing management | BLE identity and Wi-Fi registration | panasonic F29, F30 | implemented, partial: no APS and no Wi-Fi registration | none | none | capture-gated |
| Panasonic LUMIX | Battery | unknown | panasonic F32 | unavailable | none | none | not applicable |
| Panasonic LUMIX | Storage | BLE GATT on G9II | panasonic F33 | missing | none | none | capture-gated |
| Panasonic LUMIX | Error and busy status | BLE GATT notify | panasonic F35, C06, C07 | missing, readiness not subscribed | none | none | capture-gated |
| Panasonic LUMIX | Model and firmware identity | BLE GATT reads | panasonic C25 to C27, F36 | missing, matcher is model-blind | none | none (class) | capture-gated |
| Panasonic LUMIX | Model-specific controls | BLE GATT presence | panasonic F37, F42, F43 | missing | none | none | capture-gated |

### 3.12 DJI Osmo Action 4 and Action 5 Pro

| Camera | Feature | Transport | Spec row | Furble state | Simulator | Test | Hardware state |
| --- | --- | --- | --- | --- | --- | --- | --- |
| DJI Osmo | Still capture | BLE GATT | dji C03, F01 | missing, record toggle only | none | `advertisement_dispatch_test` covers the handshake only | capture-gated |
| DJI Osmo | Half press and focus | none | dji F02 | unavailable | none | none | not applicable |
| DJI Osmo | Bulb | none | dji F04 | unavailable | none | none | not applicable |
| DJI Osmo | Self-timer | BLE status field only | dji F05 | unavailable, no setter | none | none | capture-gated |
| DJI Osmo | Interval, camera-side | BLE GATT | dji C15, C18, F06 | missing, timelapse mode not selected | none | none | capture-gated |
| DJI Osmo | Interval, local | BLE GATT repeated record | dji F06 | implemented | none | none | capture-gated |
| DJI Osmo | Burst (menu-only) | camera menu, status field only | dji F07 | unavailable | none | none | not applicable, the status field is readable |
| DJI Osmo | Movie start and stop | BLE GATT | dji C16, F08 | implemented, toggle only | none | `advertisement_dispatch_test` handshake only | capture-gated |
| DJI Osmo | Zoom | Wi-Fi through Mimo | dji F09 | unavailable, BLE | none | none | capture-gated |
| DJI Osmo | Power on and off | BLE GATT | dji C13, F10, F11 | missing | none | none | capture-gated |
| DJI Osmo | Wake | BLE advertisement and GATT | dji F12 | missing | none | none | capture-gated |
| DJI Osmo | Sleep | BLE GATT | dji C13, F13 | missing | none | none | capture-gated |
| DJI Osmo | Live view | Wi-Fi, USB, or app | dji F14 | missing | none | none | capture-gated |
| DJI Osmo | Image transfer, list, thumbnail | Wi-Fi or app | dji F15 to F17 | missing | none | none | capture-gated |
| DJI Osmo | Clock and timezone | BLE GPS time field, Wi-Fi SyncTime | dji F19, F20 | missing | none | none | capture-gated |
| DJI Osmo | GPS push and pull | BLE GATT push, no R SDK pull | dji C07, F21, F22 | missing, deliberate no-op | none | none | capture-gated |
| DJI Osmo | Geotag enable and sync interval | Wi-Fi through Mimo | dji F23 | missing | none | none | capture-gated |
| DJI Osmo | Settings read | BLE GATT | dji C15, C01, F24 | implemented, partial: mode, status, and record time only | none | none | capture-gated |
| DJI Osmo | Settings write | BLE GATT | dji C18, F25, F37 | missing | none | none | capture-gated |
| DJI Osmo | Backup and restore | none documented | dji F26, F27 | unavailable | none | none | not applicable |
| DJI Osmo | Firmware update | Wi-Fi OTA and Mimo BLE DM OTA | dji C09, F28 | out of scope, unsafe | none | none | capture-gated |
| DJI Osmo | Pairing management | BLE GATT | dji C09 to C12, F29 | implemented, application-level only | none | `advertisement_dispatch_test` handshake | capture-gated |
| DJI Osmo | Battery | BLE status field | dji C15 offset 37, F32 | missing | none | none | capture-gated |
| DJI Osmo | Storage | BLE status fields | dji C15 offsets 15, 19, 23, F33 | missing | none | none | capture-gated |
| DJI Osmo | Error and busy status | BLE status and return codes | dji F35 | implemented, partial: logged only | none | none | capture-gated |
| DJI Osmo | Model and firmware identity | BLE device ID and C01/C02 | dji F36 | implemented, partial: device ID gate only | none | `advertisement_dispatch_test` device ID | capture-gated |
| DJI Osmo | Model-specific controls | BLE mode switch | dji C18, F38 to F60 | missing | none | none | capture-gated |

### 3.13 FauxNY software peer

FauxNY has no camera transport. Its connect is a 2.5 s delay that sets
`m_Connected`, and every vendor action is a log line. It is excluded from every
camera-parity claim.

| Camera | Feature | Transport | Spec row | Furble state | Simulator | Test | Hardware state |
| --- | --- | --- | --- | --- | --- | --- | --- |
| FauxNY | Still, focus, bulb, interval | none, log only | none | out of scope, software peer | `FauxNY`, 71 scenario uses | `control_e2e`, UI and lifecycle suites | not applicable |
| FauxNY | GPS, clock, transfer, live view, settings, firmware | none, log only | none | out of scope, software peer | `FauxNY` geo record in the simulator | GPS and UI suites | not applicable |
| FauxNY | Power, wake, pairing, bond, battery, storage, errors | none, log only | none | out of scope, software peer | `FauxNY` | lifecycle suites | not applicable |

## 4. Contradictions and stale claims

These are the claims in the private corpus or in the Furble research notes that
do not match the execution pin. Each one must be corrected in the same PR that
touches the family, or the matrix row above is wrong.

| Claim | Where | What the code does at this pin | Disposition | Evidence |
| --- | --- | --- | --- | --- |
| Furble sends `03 00` for Fujifilm focus | fujifilm.md C14 and §9 half press; state-machines §2.3, §3.3, and unknown 2; feature-gap-matrix §2.3.1 | Writes `01 00` for focus press and `00 00` for release. Commit `fdacb87e` removed the `03 00` value because the firmware parser returns -30 | Correct the spec and diagram to the two-value half press. The code is aligned | `firmware-static` return value; `furble-source` `FujifilmProtocol.h` |
| Canon Smart focus is a no-op | canon.md F02; feature-gap-matrix §2.5 | Writes `00 03` AF_ON and `00 04` AF_OFF since commit `4607330d` | Correct the spec. Keep the hardware outcome capture-gated | `app-static` canon.md CS-18, CS-19; `furble-source` |
| LUMIX clock follows the G9II public layout | panasonic.md C15 last sentence | Writes the LUMIX Sync layout: int16 standard offset at bytes 7 to 8, DST at byte 9. Commit `5a7b73ba` aligned it to the app | Correct the comparison sentence. Both layouts are all-zero in Furble until a timezone source exists | `app-static`; `furble-source` `Lumix.h` |
| LUMIX writes a zero byte at 7 and an offset at 8 to 9 | panasonic.md F20 | Writes a zero offset at 7 to 8 and a zero DST at 9. The bytes are identical because both fields are zero, but the field description is the G9II shape | Correct the field description | `app-static`; `furble-source` |
| Furble accepts the rotated DJI device IDs and rejects the table values | dji.md U01 | Accepts `0x33FF0000`, `0x44FF0000`, `0xFF330000`, and `0xFF440000` after commit `c2151d46` | Correct U01. The wire byte order is still unknown | `furble-source` `DJIOsmo.h`; `public-source` |
| LUMIX `altitude` is uint16 and the clock has a separate zero byte | state-machines §11.4 | `altitude` is `int16_t`; the clock has `tz_offset` at 7 to 8 and `dst` at 9 | Correct the diagram after commit `5a7b73ba` | `furble-source` `Lumix.h` |
| Fujifilm focus writes `03 00` in the traceability note | feature-gap-matrix §2.1 note and §2.3.1 | Same as the first row | Correct the note | `furble-source` |
| Ricoh capture is GR IV only | ricoh.md F-01 implementation column; `Ricoh.h` class comment | `Ricoh::matches` accepts GR III, GR IIIx, HDF, GR IV, and broad PENTAX names, and the same class drives every match | Decide the product claim. Either narrow the matcher or document GR III as experimental. Do not leave the class comment and the matcher disagreeing | `furble-source` `Ricoh.cpp`, `CameraList.cpp`; `app-static` |
| LUMIX company ID is `0x0376` | pairing-security-flows §11.8 cross-reference | Code and the official app use `0x003A` | Correct the cross-reference. The matrix already records `0x003A` as app-static | `app-static`; `furble-source` |

Claims deliberately not adopted from the corpus, because the evidence label is
`inference` or `unknown` and adopting them would invent support:

- Sony 91-byte GPS variant selection. The `DD21` bit rule is app-static but the
  camera response to the shorter form is not captured.
- Canon Smart Write Request for control writes. The app uses it, but no capture
  shows the camera rejects Write Command. This is a capture gate, not a safe fix.
- Canon Smart identity ordering. The app's order is app-static, but no capture
  shows the camera ignores or rejects the Furble order.
- LUMIX per-install MEI0 seed and APS identity. The app-static transform is
  understood, but the camera response to a new seed or key is not captured.
- Ricoh GR III Drive Mode omission. The public report is author-reported, not a
  controlled comparison.
- DJI device ID byte order, SMP requirement, snapshot key, wake, and sleep.
- Nikon Smart `2021` key events. The class targets the wrong characteristic and
  the Classic continuation is blocked.

## 5. Coverage accounting

### 5.1 Table 1

| Class | Rows |
| --- | ---: |
| Discovery, matching, and link | 60 |
| Capture | 39 |
| Power, wake, GPS, and clock | 46 |
| Data, settings, and maintenance | 37 |
| Control plane | 26 |
| Total | 208 |

| Verdict | Rows |
| --- | ---: |
| aligned | 74 |
| safe-fix-needed | 25 |
| capture-gated | 77 |
| unsupported | 18 |
| out-of-scope | 14 |

### 5.2 Table 2

| Furble state | Rows |
| --- | ---: |
| implemented | 77 |
| missing | 122 |
| capture-gated | 3 |
| unavailable | 83 |
| out of scope | 31 |

| Hardware state | Rows |
| --- | ---: |
| verified | 8 |
| capture-gated | 230 |
| not applicable | 78 |

### 5.3 What the counts mean

- A `capture-gated` verdict in Table 1 is not a TODO. It is a decision that the
  current code or the proposed change cannot be claimed until a named capture
  exists, and that the UI and docs must say so.
- A `capture-gated` Furble state in Table 2 means the code is present but the
  feature is withheld or must be labelled. `missing` means the protocol is known
  and the only work is code and tests.
- `unavailable` is a camera fact, not a Furble defect. Nikon Smart and every
  Canon Remote feature that has no BLE surface are recorded that way on purpose.
- The only family with a hardware-verified registration and stale-bond path is
  Fujifilm Secure v4.0 on X100VI. Ricoh GR IV has author-reported captures for
  the standby trap and the capture writes. Everything else is capture-gated.

## 6. What this drives next

### 6.1 Implementation batches, safe fixes only

| Batch | Scope | Files | Tests |
| --- | --- | --- | --- |
| B1 LUMIX session | Remove the focus mapping, add Confirm, Start, Finish, readiness subscriptions, GPS stop packet, clock gating | `lib/furble/Lumix.{h,cpp}` | New `lumix_session_test` with a `LumixVirtualCamera` once the peer exists; until then a mock-peer host test |
| B2 Fujifilm clock and identity | 12-byte clock on `F9150137`, NUL-terminated name, `AAB609C4` enable, disconnect reason | `lib/furble/Fujifilm.cpp`, `FujifilmSecure.cpp`, `FujifilmProtocol.*` | `fujifilm_camera_test`, `fujifilm_protocol_test`, new clock and enable cases |
| B3 Canon GPS | UTC epoch, `00040001` read, `05` ack, `03` stop, `06` source select | `lib/furble/CanonEOSSmart.cpp` | New `canon_gps_test` with a Smart peer |
| B4 Sony GPS | `DD01` subscribe, `DD21` variant, `DD32` and `DD33`, timezone fields, lock release | `lib/furble/Sony.cpp` | New `sony_gps_test` with a Sony peer |
| B5 Ricoh clock and identity | Date Time write gated on CAPTURE, Paired Device Name write, capture-status decode | `lib/furble/Ricoh.{h,cpp}` | Extend `ricoh_focus_test`, new capture-status case |
| B6 UI capability gates | Hide bulb and focus on Ricoh, focus on LUMIX, release on Nikon Remote, and unverified features on every capture-gated family | `src/FurbleUI.cpp`, `docs/settings-and-controls.md` | `headless_ui_contract_test`, `text_size_policy_test`, screenshot regeneration |
| B7 Nikon Smart fail-fast | Return the Classic blocker as a named error, remove the final-OK wait | `lib/furble/NikonSmart.cpp` | Extend `camera_regression_test` |
| B8 Control-plane errors | Surface a failed shutter or focus write, and make `gattRead` distinguish failure from empty | `lib/furble/Camera.{h,cpp}`, vendor classes | New `camera_write_error_test`; extend `camera_regression_test` |

### 6.2 Simulator batches

Required peers, in dependency order:

1. `CanonSmartVirtualCamera` and `CanonRemoteVirtualCamera`: product-ID gates,
   `00010006` result, `0001000a` identity, `00030010` mode, `00030030` shutter
   and focus, `00040000` GPS, `00050000` control, pairing reject, stale bond,
   timeout, malformed indication.
2. `NikonRemoteVirtualCamera`: `DE00` discovery and reconnect, `2087` four
   stages, `2083` short and long press, `2084` key and status, stage timeout.
3. `NikonSmartVirtualCamera`: `2000` Blowfish stages, the eight salts, the
   final-OK distinction, and a Classic-blocked mode that must fail cleanly.
4. `SonyVirtualCamera`: advertisement bits, `EE01` and `EE03`, `FF01` and
   `FF02`, `DD01`, `DD11`, `DD21`, `DD30`, `DD31`, `DD32`, `DD33`, `CC02`,
   `CC13`, and the advertisement-gate fault.
5. `LumixVirtualCamera`: MEI0 and APS identity, Confirm and Start readiness,
   `7be5fe6e`, `37701b80`, `4cf487c0`, `04` and `05`, `06` and `07`, GPS and
   clock, GPS stop, and the wrong-seed reject.
6. `DjiVirtualCamera`: `FFF0`, `FFF4`, `FFF5`, `00 19` with both device-ID
   encodings, verify mode 2, status subscription, record toggle, status push,
   CRC faults, wake advertisement, and the SMP-required and SMP-free modes.

Each peer needs the six failure classes from the goal: success, camera-side
rejection, timeout, disconnect, stale bond, and malformed response. Add one
manifest entry per peer per suite, and run `python3 tools/check_sim_scenarios.py`.

Refine the existing peers:

- Fujifilm: model the 12-byte clock write and the NUL name, and add a scenario
  that asserts the clock bytes after a NOT1 request.
- Ricoh: model the Date Time write, Paired Device Name, capture status, and the
  Drive Mode 1 and 2 self-timer values.
- Both: add a scenario that exercises a failed shutter write and asserts the UI
  reports it, once B8 lands.

### 6.3 Test batches

- One host test per peer feature path, not per byte. The byte-level vectors stay
  in `camera_protocol_tests` and the peer assertions.
- A regression test for every safe fix in §1.2. The GOAL requires it.
- A negative test for every capture gate: the code must refuse or label, not
  silently send an unverified value.
- Extend `tests/host/control_e2e` with a multi-vendor session once two new peers
  exist, so the shared control plane is exercised against non-Fujifilm families.

### 6.4 Capture batches

Run the captures in the order in §1.3. Each capture must record the body, the
firmware, the phone or controller, the raw HCI or sniffer trace, the camera
settings, and the physical outcome. A capture that does not include the physical
outcome closes only the wire half of the row.

### 6.5 Documentation batches

- Correct the stale claims in §4 in the same PR as the code change.
- Add per-camera "not implemented over BLE" lists to `README.md` and
  `docs/supported-hardware.md`, as the private gap matrix D7 and D8 require.
- Add the LUMIX and DJI rows that D1 and D2 require.
- Keep every capture-gated feature labelled in the UI and in the usage guide.
- Regenerate `docs/ui-walkthrough.md` after the capability gates in B6.

## 7. Sources read

Furble at `fa9c9581`:

- `GOAL.md` in `MaxRink/rea-camera-firmware/docs/`, Workstreams D and E.
- `lib/furble/Camera.{h,cpp}`, `CameraList.cpp`, `Scan.cpp`.
- `lib/furble/Fujifilm*.{h,cpp}`, `lib/furble/protocol/FujifilmProtocol.{h,cpp}`.
- `lib/furble/Ricoh.{h,cpp}`.
- `lib/furble/CanonEOS.{h,cpp}`, `CanonEOSSmart.{h,cpp}`, `CanonEOSRemote.{h,cpp}`.
- `lib/furble/Nikon.{h,cpp}`, `NikonBase.{h,cpp}`, `NikonRemote.{h,cpp}`, `NikonSmart.{h,cpp}`.
- `lib/furble/Sony.{h,cpp}`, `lib/furble/Lumix.{h,cpp}`, `lib/furble/DJIOsmo.{h,cpp}`.
- `lib/furble/FauxNY.{h,cpp}`, `lib/furble/protocol/AdvertisementProtocol.{h,cpp}`.
- `lib/testing/peer/FujifilmVirtualCamera.{h,cpp}`, `RicohVirtualCamera.{h,cpp}`.
- `tests/host/` camera, registration, stale-bond, Ricoh, and regression tests.
- `tests/camera/camera_protocol_tests.cpp`.
- `sim/BleSim.cpp`, `sim/scenarios/manifest.json`.
- `docs/diagrams/pairing-security-flows.md`, `docs/diagrams/state-machines.md`.
- `docs/camera-research/*.md`, especially `bt-api-surface.md`.

Private corpus at `a596f50` plus the working tree state noted above:

- `spec/INDEX.md`, `spec/TEMPLATE.md`, `spec/coverage.md`.
- `spec/fujifilm.md`, `spec/ricoh.md`, `spec/canon.md`, `spec/nikon.md`,
  `spec/sony.md`, `spec/panasonic.md`, `spec/dji.md`.
- `docs/diagrams/pairing-security-flows.md`, `docs/diagrams/state-machines.md`.
- `docs/research/feature-gap-matrix.md`.

No vendor firmware, APK, or decompiled code was copied into this repository.
The corpus claims keep their labels. Every Furble source claim is
`furble-source` and every camera claim keeps the label of the artifact that
supports it.
