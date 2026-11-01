# Camera parity matrix and Furble action traceability

Status: implementation and capture work list for GOAL Workstream D and E.
Owner: Furble maintainers.
Execution pins: Furble `fa9c9581e4261b6a81afe46baf588ef5d74a7609` on branch
`firmware-compat-audit`. The audit read the tree at that commit; concurrent
uncommitted simulator work in the same checkout is outside this document.
Private corpus `MaxRink/rea-camera-firmware` at `a596f50` plus uncommitted Wi-Fi
sheets that this document does not use; only the Bluetooth sheets, the two
diagram files, and `docs/research/feature-gap-matrix.md` were read.

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

Table 1 has `176` rows: `65` aligned,
`22` safe-fix-needed,
`65` capture-gated,
`12` unsupported, and
`12` out-of-scope.

Table 2 has `340` rows across 13 camera entries:
`77` implemented, `122` missing,
`3` capture-gated, `110` unavailable,
`28` out of scope; hardware `8` verified,
`236` capture-gated, `96` not applicable.

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
| S1 | Remove the LUMIX focus mapping. `focusPress` previously wrote `01` (Confirm) and `focusRelease` wrote `02` (Start). There is no focus opcode. | LX | `app-static` panasonic.md C06, C07, F02 | Fixed. Focus press and release are now no-ops. |
| S2 | Add the LUMIX session handshake: `01` Confirm, wait for `7be5fe6e` `00`, `02` Start, and `03` Finish on disconnect. | LX | `app-static` panasonic.md C06 to C08, F01, F40 | Fixed. Connect sends Confirm, waits for readiness, then Start. Disconnect sends Finish. |
| S3 | Add the LUMIX GPS stop packet (fix `0x56`, lat and lon `0x7fffffff`, altitude `0x7fff`) and wait for `37701b80` and `4cf487c0` readiness before the clock and GPS writes. | LX | `app-static` panasonic.md C14, F41, F45; `public-source` S5 report | A stale position can persist, and the shortened MEI0 to GPS sequence is reported to drop the link. |
| S4 | Write the Fujifilm 12-byte UTC and timezone packet on the `F9150137` `01 00` request, and write the client name with a trailing NUL. | FJ-S40, FJ-S41 | `firmware-static` fujifilm.md C28 and C03; `app-static` XApp and Camera Remote | Fixed. Both apps now match the NUL write and the UTC packet. Offset and DST are zero because Furble has no timezone source. |
| S5 | Send the Fujifilm `AAB609C4` location sync enable, not only the interval, and read it back once. | FJ-S40, FJ-S41 | `firmware-static` fujifilm.md C25; `app-static` XApp | The interval is written but the geotag feature may never be enabled on a body whose menu default is off. |
| S6 | Make the Canon Smart GPS timestamp an absolute UTC epoch instead of `mktime` on the runtime timezone. | CN-S | `app-static` canon.md CS-34; `furble-source` CanonEOSSmart.cpp | Fixed. `CanonProtocol::utcEpoch` has no timezone input, and `canon-time` pins the epoch. |
| S7 | Add the Sony GPS sequence: subscribe `DD01`, read `DD21` and choose 91 or 95 bytes, read `DD32` and `DD33`, fill UTC offset and DST, and release `DD30` and `DD31` when GPS stops. | SO | `app-static` sony.md C-20 to C-30, F-21, F-23 | The current write is a partial packet with no lock release, which leaves the camera locked to Furble. |
| S8 | Add the Ricoh Date Time write on connect, gated on a fresh Operation Mode `CAPTURE` read, and read it back. | RC3, RC4 | `firmware-static` ricoh.md C-19 and CM-03; `app-static` Image Sync and GR WORLD | Applied on the first valid GPS time and then once per minute. The readback and the CAPTURE gate are still capture-gated. |
| S9 | Write the Ricoh Paired Device Name (`FE3A32F8`, UTF-8) after the first connect. | RC3, RC4 | `app-static` ricoh.md C-76 and CM-15 | Fixed. Connect writes the controller name when the characteristic exists. |
| S10 | Decode Ricoh Capture Status and use a `Capturing=1` then `0` transition as the shot-confirmation signal. | RC3, RC4 | `firmware-static` ricoh.md C-56; `public-source` field table | Removes the false shot-taken implication without adding a new command. |
| S11 | Stop Nikon Smart at the BLE authentication result and report the Bluetooth Classic blocker. Do not wait for a final OK that is a time request. | NK-S | `app-static` nikon.md S20 and S22; `furble-source` NikonSmart.cpp | Turns a 10 s silent failure into a precise reason and removes a misleading success path. |
| S12 | Gate the UI controls by family capability: hide bulb and focus on Ricoh, hide focus on LUMIX, and label Nikon Remote release as a no-op. | All | `firmware-static` and `app-static` rows cited in Table 1 | Stops the product promising an outcome the camera cannot produce. |
| S13 | Write the disconnect reason `7EDE1988` before an intentional Fujifilm Secure disconnect, and read it after a failed reconnect. | FJ-S40, FJ-S41 | `firmware-static` fujifilm.md C06 and C78; `app-static` both Fujifilm apps | Gives the reconnect path a camera-side reason instead of a generic supervision timeout. |

### 1.3 Highest-value capture gates

Ranked by how much implementation work each capture unblocks. Each row is an
`unknown` in the corpus until the named artifact exists; the capture described is
the closing artifact.

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
| G10 | DJI SMP requirement, `device_id` byte order, snapshot key, wake, sleep, GPS, and mode switch | DJI | Whether requiring a bond is correct, and which documented R SDK commands the body accepts | FFF4 and FFF5 capture on Action 4 and Action 5 Pro, with physical outcomes |
| G11 | Fujifilm movie, power, wake, and still self-timer payloads | FJ-S40 | Payload, mode precondition, and physical outcome for `861442AB`, `43070F6C`, and the X-S20 movie strings | HCI plus physical capture on X100VI and X-S20 |
| G12 | Physical shutter and EXIF outcome on every family | All | Whether any ATT success corresponds to a file | Per-family still plus EXIF check, as required by the goal |

### 1.4 Applied safe fixes

| Fix | Status | Evidence |
| --- | --- | --- |
| LUMIX focus mapping removed; Confirm/Start/Finish handshake added | applied | `lib/furble/Lumix.{h,cpp}`, host `lumix-session` |
| Fujifilm client name NUL and 12-byte UTC time packet | applied | `lib/furble/Fujifilm.cpp`, `FujifilmBasic.cpp`, `FujifilmSecure.cpp`, protocol test |
| Canon Smart GPS timestamp uses an absolute UTC epoch | applied | `lib/furble/protocol/CanonTime.h`, `canon-time` |
| Ricoh paired-device name and GPS-time clock sync | applied | `lib/furble/Ricoh.{h,cpp}`, `lib/furble/protocol/RicohTime.h`, `ricoh-time`, `ricoh-focus` |

The LUMIX fix sends Confirm `01`, waits up to two seconds for control readiness
`00`, sends Start `02`, and sends Finish `03` on disconnect. Focus press and
release are no-ops. The peer defaults to refusing a bare ShutterOn, so the host
test proves the gate rather than only the byte sequence.

## 2. Table 1: Furble action traceability

Rows are grouped by action class. A row may name more than one family only when
the code path is shared and the verdict is identical. Every family-specific
behaviour has its own row.

### 2.1 Discovery, matching, and link


| Furble action | Family | Code path | Spec row | Expected camera response | Current behavior | Evidence | Verdict |
| --- | --- | --- | --- | --- | --- | --- | --- |
| L01 scan, discovery | all BLE | `Scan::start(std::function)` then `CameraList::match` | fujifilm §2.4; ricoh §2; canon §2.1 to §2.6; nikon §2.1 to §2.3; sony §2.3 to §2.4; panasonic §2; dji §2.2 | Connectable advertisement in the pairing, wake, or reconnect window | Active scan, one logical generation, dedup by address, vendor dispatch | `furble-source`; `hardware-capture` Fujifilm adv; `app-static` gates | aligned |
| L02 scan, saved camera | FJ-S40, FJ-S41 | `FujifilmSecure::_connect` scan branch, `onResult`, 60 s queue | fujifilm §7.3 and §8 | Form A3 or A4 with the saved five serial bytes, service `A9D2B304` or `123D8F06` | Queue reset, 60 s wait, serial compare, accepts either service, cancel polled | `furble-source`; `hardware-capture` 2026-08-28 | aligned |
| L03 scan, saved camera | FJ-B | `FujifilmBasic::_connect` | fujifilm §7.6 | Saved address still advertises; token is taken from NVS | No saved scan. Connects to the stored address and reuses the stored token | `furble-source`; `public-source` | aligned |
| L04 scan, saved camera | RC3, RC4, CN-S, CN-R, SO, LX, DJI | per-class `_connect` | ricoh §7.4; canon §7.5 and §7.6; sony §7.4; panasonic §7.5; dji §7.2 | Saved address still resolves and the camera is in its reconnect window | Direct connect to the stored address. No advertisement re-check and no identity resolution | `furble-source`; `public-source`; `inference` | capture-gated |
| L05 scan, saved camera | NK-R, NK-S | `Nikon::_connect` scan branch, `onResult` | nikon §2.3 and §7.2 | 7-byte manufacturer record with the saved device ID, `DE00` advertised | 60 s scan, device ID compare, then connect | `furble-source`; `firmware-static` B600 | aligned |
| L06 match | FJ-B | `FujifilmBasic::matches`, `matchesBasicAdvertisement` | fujifilm §2 form A1, §2.3 | 7 bytes, company `0x04D8`, type `0x02`, CR or XApp service | Exact length, exact type, either service | `furble-source`; `public-source`; `app-static` | aligned |
| L07 match | FJ-S40, FJ-S41 | `FujifilmSecure::matches` | fujifilm §2 forms A3 and A4 | 8 bytes, service `A9D2B304` | Exact length and service; type byte not validated | `furble-source`; `hardware-capture` | aligned |
| L08 match | RC3, RC4 | `Ricoh::matches` | ricoh §2 A1 to A8 | Ricoh service or `GR_` / `RICOH` / `PENTAX` name | Any of four services or a name prefix. Broader than Image Sync and the class comment | `furble-source`; `app-static`; `public-source` | capture-gated |
| L09 match | CN-S | `CanonEOSSmart::matches` | canon §2.1 to §2.6 | Service `00010000`, company `0x01A9`, 6 or 19 bytes | First advertised service only. No product ID or UUID form parse | `furble-source`; `app-static` | capture-gated |
| L10 match | CN-R | `CanonEOSRemote::matches` | canon §2.5 and §7.6 | Service `00050000` | First advertised service equals `00050000` | `furble-source`; `public-source` | aligned |
| L11 match | NK-R, NK-S | `Nikon::matches`, `matchesNikonDiscovery` | nikon §2.1 and §2.3 | Discovery: no manufacturer data plus `DE00`. Reconnect: company `0x0399` and device ID | Both forms implemented | `furble-source`; `firmware-static` B600 | aligned |
| L12 match | SO | `Sony::matches`, `matchesSonyAdvertisement` | sony §2.1 to §2.4 | Company `0x012D`, type `0003`, tag `0x22` pairing bits | Requires bits 7, 6, and 1. The app decodes bit 6 and does not decode bit 1 | `furble-source`; `app-static` | capture-gated |
| L13 match | LX | `Lumix::matches`, `matchesLumixAdvertisement` | panasonic §2 | Company `0x003A`, session service `054AC620`, 8 bytes | Company, service, and length. Model-blind | `furble-source`; `app-static` | aligned |
| L14 match | DJI | `DJIOsmo::matches`, `matchesDJIAdvertisement` | dji §2.1 and §2.2 | `AA 08`, model bytes, byte 4 `FA` | First five bytes only. Model bytes not gated before listing | `furble-source`; `public-source`; `unknown` device IDs | capture-gated |
| L15 match | FN | `FauxNY::matches` | none, software peer | Not applicable | Always true, simulator and test only | `furble-source` | out-of-scope |
| L16 connect, outer attempt | all | `Camera::connect`, client create, security preset, vendor `_connect`, failure reclaim | state-machines §1.2 | GAP connect then vendor handshake | Owns the client for the attempt, polls cancel, reclaims on failure | `furble-source` | aligned |
| L17 connect, vendor | FJ-S40, FJ-S41 | `FujifilmSecure::_connect` | fujifilm §7.2 and §7.3 | Secure link, status gate, subscriptions, registration, interval, shutter discovery, fast profile | All steps, then FAST exact parameters within 1 s | `furble-source`; `hardware-capture` X100VI | aligned |
| L18 connect, vendor | FJ-B | `FujifilmBasic::_connect` | fujifilm §7.6 | `91F1DE68` present, token, name, five subscriptions, registration, shutter | Looks up `91F1DE68` first. Absent from X100VI 1.31, X-S20 3.30, X-E5 1.12 | `furble-source`; `firmware-static` absence | unsupported |
| L19 connect, vendor | RC3, RC4 | `Ricoh::_connect` | ricoh §7.3 and §7.4 | Bond, model read, Camera and Shooting services, state probe | Secure and requires encrypted, authenticated, bonded; requires Shooting | `furble-source`; `hardware-capture` GR IV | aligned |
| L20 connect, vendor | CN-S | `CanonEOSSmart::_connect` | canon §7.3 | Bond, pair request and result, identity, GPS subscription, commit, mode | Identity written before any result; commit then mode `02` | `furble-source`; `app-static` order differs | capture-gated |
| L21 connect, vendor | CN-R | `CanonEOSRemote::_connect` | canon §7.6 | Bond, name `03` plus name, control characteristic | Exactly that, no pair result wait | `furble-source`; `public-source` | aligned |
| L22 connect, vendor | NK-R | `Nikon::_connect`, `NikonRemote` | nikon §7.1 | `DE00`, `2087` four-stage, then `2083` ready | Four stages, 10 s each, remote selected by `2087` | `furble-source`; `firmware-static` B600 | aligned |
| L23 connect, vendor | NK-S | `Nikon::_connect`, `NikonSmart` | nikon §7.3 and §7.7 | `2000` Blowfish exchange, then Classic bond | BLE runs, `connectFinalise` always false. Never active | `furble-source`; `app-static`; `public-source` | unsupported |
| L24 connect, vendor | SO | `Sony::_connect` | sony §7.4 and §7.9 | Bond, `FF00` and `FF01`, optional `DD00` | Secure, requires `FF00` and `FF01`; no `EE01` and no `EE03` | `furble-source`; `public-source` | capture-gated |
| L25 connect, vendor | LX | `Lumix::_connect` | panasonic §7.2 and §7.5 | Session service, MEI0, name, control, Confirm, readiness, Start, optional location and clock | MEI0 and name by `writeValue`, control readiness subscription, Confirm, Start | `furble-source`; `app-static` | aligned |
| L26 connect, vendor | DJI | `DJIOsmo::_connect` | dji §7.1 and §7.2 | FFF0, FFF4, `00 19`, verify, ACK, status | MTU 517, secure and bonded required, 33-byte request, verify 2 and 0, 30 s, ACK | `furble-source`; `public-source` | aligned |
| L27 secure, setup | all | `setSecurityAuth(true, true, true)` and per-class `securityMode()` | state-machines §1.2 | Bond, MITM, LE Secure Connections | Set before every connect. Default DisplayYesNo; Ricoh keyboard, DJI and Ricoh fallback passkey | `furble-source` | aligned |
| L28 secure, handshake | FJ-S40, FJ-S41 | `secureConnection()` plus stale-bond branch | fujifilm §7.5 and §7.8 | Encrypt with stored keys, or numeric comparison on a fresh bond | One handshake per attempt, two-failure rule, identity delete, repair prompt | `furble-source`; `hardware-capture` 2026-09-02 and 2026-09-05 | aligned |
| L29 secure, handshake | RC3, RC4 | `secureConnection`, `securityMode`, passkey callbacks | ricoh §7.1 and §7.3 | GR III passkey, GR IV numeric comparison | Keyboard display for every Ricoh, injects `123456`, always confirms | `furble-source`; `firmware-static`; `hardware-capture` | capture-gated |
| L30 secure, handshake | CN-S | `secureConnection` | canon §7.1 | Bond, method unknown | Shared base sets bond, MITM, Secure Connections. Cano class does not override IO or add passkey handling | `furble-source`; `app-static`; `unknown` | capture-gated |
| L31 secure, handshake | CN-R | `secureConnection` | canon §7.1 and §7.6 | Secure bond, method unknown | One handshake, no bond pre-check | `furble-source`; `public-source`; `unknown` | capture-gated |
| L32 secure, handshake | NK-R | none | nikon §7.8 | Application four-stage; SMP requirement unknown | No `secureConnection()` | `furble-source`; `public-source`; `unknown` | capture-gated |
| L33 secure, handshake | NK-S | none | nikon §7.7 and §7.8 | Classic bond with numeric passkey | No BLE handshake and no Classic step | `furble-source`; `public-source` | unsupported |
| L34 secure, handshake | SO | `secureConnection` | sony §7.1 and §7.5 | Bond, method unknown | One handshake. Failure fails the connect; no bond delete and no re-pair prompt | `furble-source`; `app-static` | capture-gated |
| L35 secure, handshake | LX | none | panasonic §7.3 and §7.9 | No bond in the app; application identity | No `secureConnection()`. Global preset can still trigger SMP if asked | `furble-source`; `app-static` | aligned |
| L36 secure, handshake | DJI | `secureConnection`, requires encrypted and bonded | dji §7.4 and §7.5 | Unknown whether SMP is required; the demo makes no SMP call | Requires encrypted bonded link and fails otherwise | `furble-source`; `public-source`; `unknown` | capture-gated |
| L37 register | FJ-S40 | status read, `0x20` ack, name, subscriptions, 25 s, interval, shutter | fujifilm §7.2 and §7.3 | Four-byte status, ack, registration, interval echo | All steps. Promotion requires the registration gate | `furble-source`; `firmware-static`; `hardware-capture` | aligned |
| L38 register | FJ-S41 | shared v4.0 path | fujifilm §7.7 | v4.0 plus `8B5ECF55` `80 01 01` | Shared path only. `8B5ECF55` never written | `furble-source`; `firmware-static`; `app-static` | capture-gated |
| L39 register | FJ-B | token, name, five subscriptions, wait | fujifilm §7.6 | Registration on `F9150137` | Implemented; entry service absent on all three target images | `furble-source`; `firmware-static` absence | unsupported |
| L40 register | RC3, RC4 | model read and state probe | ricoh §7.3 and §7.4 | Bond is registration; model string identifies the body | Reads model, overwrites display name, probes state, subscribes to four | `furble-source`; `hardware-capture` | aligned |
| L41 register | CN-S | `00010006` name, `02` result, identity, commit, mode | canon §7.3 | Pair result `02`, then stored identity | Identity before result, commit as a pairing step, mode `02` only | `furble-source`; `app-static` order differs | capture-gated |
| L42 register | CN-R | name `03` plus name | canon §7.6 | Unknown, no result documented | Writes the name and proceeds | `furble-source`; `public-source`; `unknown` | aligned |
| L43 register | NK-R | four-stage `2087` | nikon §7.1 | Stage 2 and stage 4 | Implemented, serial logged at stage 4 | `furble-source`; `firmware-static` B600 | aligned |
| L44 register | NK-S | four-stage `2000`, final OK | nikon §7.3 | Stage 2 and 4, then Classic | BLE stages, 1 s final OK, unconditional failure | `furble-source`; `public-source` | unsupported |
| L45 register | SO | none | sony §7.2 and §7.3 | `EE01`, `EE03`, `EE04` | None used | `furble-source`; `app-static` | capture-gated |
| L46 register | LX | MEI0, name, Confirm, Start | panasonic §7.2 and §7.6 | MEI0, name, Confirm, Start | MEI0 and name, then Confirm and Start. Fixed seed still replaces the per-install seed | `furble-source`; `app-static` | aligned |
| L47 register | DJI | `00 19`, verify 2, ACK | dji §7.1 | Verification result 0, protocol ready | Implemented, including both device-ID encodings | `furble-source`; `public-source` | aligned |
| L48 reconnect | FJ-S40, FJ-S41 | saved scan, identity address, secure, full registration | fujifilm §7.3 | New window, same bond, geotag request as confirmation | Implemented. Geotag request promotes on a saved reconnect | `furble-source`; `hardware-capture` 2026-08-28 | aligned |
| L49 reconnect | FJ-B, RC3, RC4, CN, SO, LX, DJI, NK-R | per-class saved path | ricoh §7.4; canon §7.5; sony §7.4; panasonic §7.5; dji §7.2; nikon §7.2 | Saved address or identity valid, vendor re-registration as needed | Direct connect by stored address; Nikon scans; Basic reuses the token | `furble-source`; `public-source` | capture-gated |
| L50 reconnect policy | all | `Control::connectAll`, `ReconnectBackoff::delayMs` | state-machines §1.5 | Not a camera action | Two failed cycles stop non-infinite mode. Backoff 2.5 s to 120 s, 100 ms slices | `furble-source` | aligned |
| L51 stale bond, camera deleted | FJ-S40, FJ-S41 | two failures, `deleteBond(identity)`, fresh pair, `needsRepair` | fujifilm §7.5 and §8 | `rc=13` about 30 s or `rc=520` about 5 s, no refusal | Implemented and hardware traced. Fresh pair after delete normally fails because unpair drops the link | `furble-source`; `hardware-capture` | aligned |
| L52 stale bond, camera deleted | FJ-B | token reject | fujifilm §7.6 and §7.8 | Old token refused; new advertisement token required | Connect or token write fails. No bond and no re-pair prompt | `furble-source`; `public-source` | aligned |
| L53 stale bond, camera deleted | RC3, RC4 | `Ricoh::_connect` secure branch | ricoh §7.5 | Passkey or numeric comparison fails; camera may drop or hold | Only a NEW pairing that was bonded deletes the local bond, by the advertised address. Saved reconnect keeps it | `furble-source`; `public-source` | capture-gated |
| L54 stale bond, camera deleted | CN-S | `deleteBond` on pair reject | canon §7.7 and §7.8 | Result other than `02`, or no result | Deletes on rejection or timeout. Saved reconnect has no detection and always sends the pair request | `furble-source`; `app-static`; `unknown` | capture-gated |
| L55 stale bond, camera deleted | CN-R, SO, LX, DJI | none | canon §7.7; sony §7.5; panasonic §7.6; dji §7.3 | Vendor-specific, mostly unknown | No detection or recovery. SO, LX, and DJI rely on a saved address that may rotate | `furble-source`; `public-source`; `unknown` | capture-gated |
| L56 stale bond, camera deleted | NK-R, NK-S | device ID mismatch | nikon §7.6 | New device ID after re-pairing | Scan times out when the saved ID is gone. No local bond | `furble-source`; `public-source`; `unknown` | capture-gated |
| L57 disconnect, user | all | `Control::disconnect`, `Camera::disconnect`, drain, reclaim | state-machines §1.6 | Link termination, vendor teardown if any | Cancel, DISCONNECTING, terminate, 30 s wait, drain, 2 s reclaim | `furble-source` | aligned |
| L58 disconnect, vendor teardown | FJ-S40, FJ-S41 | `Fujifilm::_disconnect` | fujifilm C06 | Optional disconnect reason before link loss | Only `m_Client->disconnect()`. Reason never written | `furble-source`; `app-static` | safe-fix-needed |
| L59 disconnect, vendor teardown | LX | `Lumix::_disconnect` | panasonic C08 | `03` Finish, then close | Clears pointers and disconnects. No Finish | `furble-source`; `app-static` | safe-fix-needed |
| L60 disconnect, vendor teardown | RC, CN, NK, SO, DJI | per-class `_disconnect` | ricoh §7.6; canon §7.6; nikon §7.2; sony §7.9; dji §7.2 | Vendor teardown, mostly none documented | Link terminate only | `furble-source`; `public-source` | aligned |

### 2.2 Capture


| Furble action | Family | Code path | Spec row | Expected camera response | Current behavior | Evidence | Verdict |
| --- | --- | --- | --- | --- | --- | --- | --- |
| C01 shutter press | FJ-B, FJ-S40, FJ-S41 | `Fujifilm::shutterPress` | fujifilm C11 to C13; §5.1 | ATT Write Response twice, then exposure | `01 00` then `02 00`, both with response, no delay | `furble-source`; `firmware-static`; `app-static`; `hardware-capture` | aligned |
| C02 shutter release | FJ-B, FJ-S40, FJ-S41 | `Fujifilm::shutterRelease` | fujifilm C11; §5.1 | ATT Write Response twice, exposure ends | `01 00` then `00 00` | `furble-source`; `app-static`; `hardware-capture` | aligned |
| C03 shutter press | RC4 | `Ricoh::shutterPress`, `captureAllowed`, `setShootingFlavor`, `writeOperation` | ricoh CM-16, CM-18, F-01 | Fresh CAPTURE, Drive Mode write, then `{01,01}` | Fresh read, refuse unless CAPTURE, Drive Mode `0`, `{01,01}` | `furble-source`; `firmware-static`; `hardware-capture` | aligned |
| C04 shutter press | RC3 | same path | ricoh F-01 and §4.5 | `{01,01}` alone; Drive Mode pre-write can swallow the first frame | Same Drive Mode pre-write as GR IV, gate refuses `OTHER` | `furble-source`; `public-source`; `hardware-capture` | capture-gated |
| C05 shutter release | RC3, RC4 | `Ricoh::shutterRelease` | ricoh F-03 | No release write for a still | Logs and returns | `furble-source`; `app-static` | aligned |
| C06 shutter press and release | CN-S | `CanonEOSSmart` | canon CS-16, CS-17 | AF plus release on, then off | `00 01` then `00 02`, both Write Command | `furble-source`; `app-static`; `public-source` | aligned |
| C07 shutter press and release | CN-R | `CanonEOSRemote` | canon CR-04, CR-05 | `0x8c` then `0x0c` | Exactly that, with response | `furble-source`; `public-source` | aligned |
| C08 shutter press | NK-R | `NikonRemote::shutterPress` | nikon R5 | `2083` `02 02` short press | Exactly that | `furble-source`; `firmware-static` B600 | aligned |
| C09 shutter release | NK-R | `NikonRemote::shutterRelease` | nikon R6b | `02 00` is a no-op, not a release | Writes `02 00`, which the firmware ignores | `furble-source`; `firmware-static` | aligned |
| C10 shutter press and release | NK-S | `NikonSmart` | nikon §9.2 | `2021` key 3 press and release | No-ops; camera cannot become active | `furble-source`; `app-static` | unsupported |
| C11 shutter press and release | SO | `Sony::shutterPress` and `shutterRelease` | sony F-03, C-32 | `01 09`, then `01 08` | Exactly that | `furble-source`; `public-source` | aligned |
| C12 shutter press and release | LX | `Lumix::writeCommand` | panasonic C09, C10 | `04`, then `05` inside the started session | Exactly that, with response, after Confirm and Start | `furble-source`; `app-static` | aligned |
| C13 shutter | DJI | `DJIOsmo::shutterPress`, `writeRecordCommand` | dji C16, F03, F08 | `1D 03` start or stop; photo outcome unknown | Toggles record from tracked state. No still photo | `furble-source`; `public-source`; `unknown` | capture-gated |
| C14 focus press and release | FJ-B, FJ-S40, FJ-S41 | `Fujifilm::focusPress` and `focusRelease` | fujifilm §5.1, C14 | Half press `01 00`, release `00 00` | Half press then release. Old `03 00` removed, parser returns -30 | `furble-source`; `firmware-static`; `public-source` | aligned |
| C15 focus | RC3, RC4 | `Ricoh::focusPress` and `focusRelease` | ricoh F-02, CM-30 | No BLE focus trigger; Focus Mode only configures | Both no-ops | `furble-source`; `firmware-static`; `app-static` | unsupported |
| C16 focus press and release | CN-S | `CanonEOSSmart` | canon CS-18, CS-19, F02 | `00 03` AF on, `00 04` AF off | Exactly that, Write Command | `furble-source`; `app-static` | aligned |
| C17 focus press and release | CN-R | `CanonEOSRemote` | canon CR-06, CR-05 | `0x4c`, then `0x0c` | Exactly that | `furble-source`; `public-source` | aligned |
| C18 focus | NK-R, NK-S, DJI | vendor classes | nikon §9.1 and §9.2; dji F02 | Remote has no focus; Smart uses key 2; DJI none | No-ops or unreachable | `furble-source`; `firmware-static`; `app-static` | unsupported |
| C19 focus press and release | SO | `Sony::focusPress` and `focusRelease` | sony F-02, C-33 | `01 07`, then `01 06` | Exactly that | `furble-source`; `public-source` | aligned |
| C20 focus press and release | LX | `Lumix::focusPress` and `focusRelease` | panasonic F02, C06, C07 | No focus command. `01` Confirm, `02` Start | No-op. Does not change session state | `furble-source`; `app-static` | aligned |
| C21 bulb | FJ, CN-S, CN-R, SO, LX | UI holds press, then release | fujifilm §5.1; canon F04; sony F-04; panasonic F04 | Held press; camera mode decides | UI bulb page holds for the configured duration | `furble-source`; `firmware-static` absence; `inference` | capture-gated |
| C22 bulb | RC3, RC4 | none | ricoh F-04 | Shooting Mode B or BT, then a stop write | Release is ignored, so a bulb countdown cannot end the exposure | `furble-source`; `app-static` | unsupported |
| C23 bulb | NK-R, NK-S, DJI | none | nikon §9.1; dji F04 | No bulb command | No-op or local hold only | `furble-source`; `public-source` | out-of-scope |
| C24 self-timer | RC3, RC4 | `TIMER_2S` declared, unused | ricoh F-05, CM-18 | Drive Mode `1` or `2`, then capture | Missing. Enum unused, `009A8E70` never written | `furble-source`; `firmware-static`; `app-static` | capture-gated |
| C25 self-timer | CN-S, CN-R, SO, LX, FJ, DJI, NK | none | canon F05; sony F-05; panasonic F05; fujifilm §9; dji F05 | Menu only, or a 2 s remote bit on Canon Remote | Missing. Canon Remote `0x04` delay documented but not implemented | `furble-source`; `app-static`; `official-semantic` | capture-gated |
| C26 interval | all except DJI, NK-S | UI intervalometer, shutter commands | fujifilm §9; ricoh F-06; canon F06; sony F-06; panasonic F06; nikon §9.1 | Repeated shutter, no camera interval opcode | Local timer, press and release per shot | `furble-source`; `app-static`; `firmware-static` absence | aligned |
| C27 interval, camera-side | RC3, RC4 | none | ricoh F-06, CM-18 | Drive Mode `10` to `15` and `37` to `40` | Missing | `furble-source`; `app-static` | capture-gated |
| C28 interval, camera-side | DJI | none | dji F06 | Timelapse `0x02` plus status interval | Local timer only. Timelapse mode never selected | `furble-source`; `public-source` | capture-gated |
| C29 movie | FJ | none | fujifilm C15, §9 | `861442AB`, Camera Remote writes 1, stop unknown | Missing | `furble-source`; `firmware-static`; `app-static` | capture-gated |
| C30 movie | RC3, RC4 | none | ricoh F-08, CM-26, CM-29 | Capture Mode `2`, config, start and stop | Missing | `furble-source`; `app-static`; `unknown` | capture-gated |
| C31 movie | CN-S, CN-R | none | canon CS-22, CS-23, F08; CR-08 | `00 10` / `00 11`; Remote `0x88` inferred | Missing | `furble-source`; `app-static`; `inference` | capture-gated |
| C32 movie | SO, LX, NK-S, NK-R, DJI | per-class | sony F-08; panasonic C11, C12; nikon §9.2; dji C16 | DiRxTx, `06`/`07`, key 4, record toggle | DJI implemented as toggle. Others missing or unreachable | `furble-source`; `app-static`; `public-source` | capture-gated |
| C33 zoom | FJ, RC, NK, SO, LX, DJI | none | fujifilm §9; ricoh F-09; nikon §9.1; sony F-09; panasonic F09; dji F09 | Fixed lens, Wi-Fi zoom, or no bytes | Missing or out of scope. No verified BLE zoom setter | `furble-source`; `firmware-static` absence; `public-source` | out-of-scope |
| C34 zoom | CN-S, CN-R | none | canon CS-24 to CS-27, CR-09, CR-10 | `00 20` to `00 23`, or tele and wide bits | Missing. Smart bytes have no app call site; Remote bytes are inference | `furble-source`; `app-static`; `inference` | capture-gated |

### 2.3 Power, wake, GPS, and clock


| Furble action | Family | Code path | Spec row | Expected camera response | Current behavior | Evidence | Verdict |
| --- | --- | --- | --- | --- | --- | --- | --- |
| P01 power off | FJ-S40, FJ-S41 | none | fujifilm C18, §9 | `43070F6C` `01 00` | Missing | `furble-source`; `firmware-static`; `app-static` | capture-gated |
| P02 power off | RC4 | none | ricoh CM-01, F-11 | `B58CE84C` `0`, avoid during POWER_OFF_TRANSFER | Missing. Unconditional power-off must not be implemented | `furble-source`; `firmware-static`; `inference` | out-of-scope |
| P03 power off | RC3 | none | ricoh F-11 | No BLE power-off write in the reviewed path | Missing | `furble-source`; `official-semantic` | out-of-scope |
| P04 power off | CN-S, SO | none | canon CS-14, F11; sony C-01, F-11 | Mode `05`; `CC02` `03 08 0D 00` | Missing | `furble-source`; `app-static` | capture-gated |
| P05 power off | NK-R, NK-S, LX, CN-R, FJ-B | none | nikon §9.1; panasonic F11; canon §9.2; fujifilm §9 | No power-off opcode documented | Missing or out of scope | `furble-source`; `public-source`; `app-static` | out-of-scope |
| P06 power on | FJ-S40, FJ-S41 | none | fujifilm C18, C19, §5.4 | `43070F6C` `00 01`; remote boot on | Missing | `furble-source`; `firmware-static`; `public-source` | capture-gated |
| P07 power on | RC3, RC4 | connect only | ricoh F-10, F-12 | GR III/IIIx wake on connect; GR IV power write or connect | Connect only. No power write and no wake confirmation | `furble-source`; `hardware-capture`; `official-semantic` | capture-gated |
| P08 power on | CN-S, SO | mode/connect only | canon CS-14, F10; sony F-10, F-54 | Connect, then `03` ANY_START; Sony GATT connect | Canon writes `02`, Sony connects only | `furble-source`; `app-static` | capture-gated |
| P09 power on | NK-S, DJI | none | nikon S15; dji F10, F12 | `2001`; WKP advertisement plus key | Missing or unreachable | `furble-source`; `app-static`; `public-source` | capture-gated |
| P10 power on | LX, NK-R, CN-R | none | panasonic F10; nikon §9.1; canon §9.2 | Panasonic states Bluetooth cannot power on | Missing | `furble-source`; `official-semantic` | out-of-scope |
| P11 wake | FJ-S40, FJ-S41 | none | fujifilm §2.5, C18, C19 | `00 01`, remote boot, startup advertisement | Missing. A wake drops the link and starts a new window | `furble-source`; `firmware-static`; `hardware-capture` | capture-gated |
| P12 wake | RC4 | connect only | ricoh F-12 | Leave BLE_STARTUP; power `1` wakes | Connects but does not wake. Capture write in BLE_STARTUP refused by design | `furble-source`; `hardware-capture` | capture-gated |
| P13 wake | CN-S | none | canon F12 | Mode `03` ANY_START | Missing | `furble-source`; `app-static` | capture-gated |
| P14 wake | NK-S, DJI, SO | none/connect | nikon §9.2; dji F12; sony F-12 | `2001`, WKP, GATT connect | Connect only for Sony. No Smart or DJI wake | `furble-source`; `public-source` | capture-gated |
| P15 GPS enable | FJ-S40, FJ-S41 | none | fujifilm C25, C26, N15 | `AAB609C4` On or Off | Missing. Only the interval is written | `furble-source`; `firmware-static`; `app-static` | safe-fix-needed |
| P16 GPS enable | RC3, RC4 | `setGeotagEnable` | ricoh CM-04, F-23 | `A36AFDCF` `01` | Written once before the first packet | `furble-source`; `firmware-static`; `app-static`; `hardware-capture` | aligned |
| P17 GPS enable | CN-S | `GEO_ENABLE` | canon CS-35, F23 | One-byte `01`; acceptance unproven | On request `03`; app uses a 9-byte form | `furble-source`; `unknown` | capture-gated |
| P18 GPS enable | SO | `locationEnabled` | sony C-20, C-21, F-23 | `DD30` lock, `DD31` enable, `DD01` availability | Reads and writes `DD30` and `DD31`, never subscribes `DD01`, never releases | `furble-source`; `app-static` | safe-fix-needed |
| P19 GPS enable | LX | none | panasonic C13, F-23, F-45 | Readiness `37701b80` `01`, then packets | Missing. Writes without waiting | `furble-source`; `app-static`; `public-source` | safe-fix-needed |
| P20 GPS enable | DJI | none | dji C07, F21, F23 | `00 17` push, no separate enable | Deliberate no-op | `furble-source`; `public-source` | capture-gated |
| P21 GPS packet | FJ-S40, FJ-S41 | `updateGeoData`, `encodeGeotag` | fujifilm C22, N03, §5.2 | One 23-byte packet per request | Answers on request, clears flag, truncates lat, lon, altitude | `furble-source`; `firmware-static`; `app-static`; `hardware-capture` | aligned |
| P22 GPS packet | RC3, RC4 | `Ricoh::updateGeoData` | ricoh CM-31, F-21 | 32 bytes, big-endian doubles, datum `0` | Movement 0.00001 deg or 1 m, or every 10 s. Enables GEO Tag on first write | `furble-source`; `firmware-static`; `app-static`; `hardware-capture` | aligned |
| P23 GPS packet | CN-S | `CanonEOSSmart::updateGeoData` | canon CS-34, F21 | 20 bytes, float32, Unix seconds | Only after `m_GeoEnabled`. Timestamp from `CanonProtocol::utcEpoch`, no runtime timezone | `furble-source`; `app-static` | aligned |
| P24 GPS packet | SO | `Sony::updateGeoData` | sony C-22, C-23, F-21 | 91 or 95 bytes from `DD21`, UTC offset and DST | Always 95, zero offset and DST, no `DD21`, no release | `furble-source`; `app-static` | safe-fix-needed |
| P25 GPS packet | LX | `Lumix::updateGeoData` | panasonic C13, F-21 | 16 bytes, GPS epoch, fix `0x41` | Every GPS tick, then clock after success | `furble-source`; `app-static`; `public-source` | safe-fix-needed |
| P26 GPS packet | NK-S, NK-R, DJI | per-class | nikon S8; dji F21 | 41 bytes; no Remote surface; deliberate no-op | Unreachable or no-op | `furble-source`; `app-static`; `public-source` | unsupported |
| P27 clock read | all | none | fujifilm §9; ricoh CM-40; canon F18; sony F-18; panasonic F18; dji F18 | Only Ricoh exposes a documented readable clock | Missing everywhere | `furble-source`; `firmware-static`; `app-static` | safe-fix-needed |
| P28 clock write | FJ-S40, FJ-S41 | none | fujifilm C28, C29, §5.3 | 12-byte UTC and timezone after `01 00`, or 7-byte fallback | Missing. Not even the fallback | `furble-source`; `firmware-static`; `app-static` | safe-fix-needed |
| P29 clock write | RC3, RC4 | none | ricoh CM-03, F-19 | 7-byte Date Time, local wall clock | Missing | `furble-source`; `firmware-static`; `app-static` | safe-fix-needed |
| P30 clock write | CN-S, SO, LX, NK-S | per-class | canon CS-34; sony C-06; panasonic C15; nikon S7 | PTP; `CC13`; 10 bytes after GPS; `2006` | Canon GPS timestamp only; Sony missing; LUMIX partial; Nikon unreachable | `furble-source`; `app-static` | capture-gated |
| P31 clock write | NK-R, CN-R, DJI | none | nikon §9.1; canon §9.2; dji F19 | No BLE clock write | Missing | `furble-source`; `public-source` | out-of-scope |
| P32 timezone and DST | FJ-S40, FJ-S41, SO, LX | none | fujifilm C28; sony C-22; panasonic F-20 | int32 hundredths; bytes 91 to 94; bytes 7 to 9 | Missing or zero | `furble-source`; `app-static` | safe-fix-needed |
| P33 timezone and DST | RC3, RC4 | none | ricoh F-20 | No timezone field; menu owns the zone | Not applicable | `firmware-static`; `app-static` | out-of-scope |
| P34 sync interval | FJ-S40, FJ-S41 | interval write | fujifilm C23, N04 | `C95D91AE` `0A 00`, echo | Written after registration. Echo subscribed but unhandled | `furble-source`; `firmware-static`; `hardware-capture` | aligned |

### 2.4 Data, settings, and maintenance


| Furble action | Family | Code path | Spec row | Expected camera response | Current behavior | Evidence | Verdict |
| --- | --- | --- | --- | --- | --- | --- | --- |
| M01 settings read | FJ-S40, FJ-S41 | status read only | fujifilm C08, C58, C59, C62, C66, C68 | Camera info, serial, protocol, card, lens, passphrase | Only status. Model, serial, card, lens, passphrase never read | `furble-source`; `firmware-static` | safe-fix-needed |
| M02 settings read | FJ-B | none | fujifilm §4 | Same table by generation | No reads | `furble-source`; `unknown` | capture-gated |
| M03 settings read | RC3, RC4 | model, state probe | ricoh F-24, CM-38 to CM-44 | Current values | Reads model and state, logs the rest. No battery or storage | `furble-source`; `hardware-capture` | aligned |
| M04 settings read | CN-S | none | canon CS-12, CS-13, CS-37, F24 | Capability, power, status | Missing | `furble-source`; `app-static` | capture-gated |
| M05 settings read | SO | `DD30`, `DD31` only | sony C-24 to C-27, F-24 | `DD21`, `DD30` to `DD33`, `EE04` | Only two location bytes | `furble-source`; `app-static` | safe-fix-needed |
| M06 settings read | LX | none | panasonic C21 to C27, F-24 | SSID, name, capability, vendor, model | Missing. Matcher model-blind | `furble-source`; `app-static` | capture-gated |
| M07 settings read | DJI | status parse | dji C15, F24 | Mode, status, record time, battery, storage | Mode, status, record time only | `furble-source`; `public-source` | aligned |
| M08 settings write | FJ-S40, FJ-S41 | interval only | fujifilm C23, C30 to C37 | Interval, SSID, resize, transfer, logging, reservation, remote boot | Only interval | `furble-source`; `firmware-static`; `app-static` | capture-gated |
| M09 settings write | RC3, RC4 | Drive Mode, GEO Tag | ricoh CM-04, CM-18, F-25 | Drive Mode, geotag, focus, exposure | Drive Mode `0` and GEO Tag `1` only | `furble-source`; `firmware-static`; `app-static` | aligned |
| M10 settings write | CN-S, SO, LX, DJI | per-class | canon CS-14; sony C-04 to C-13; panasonic C01, C02; dji C18 | Mode, Wi-Fi, identity, name, mode switch | Canon mode only; Sony missing; LUMIX identity and name; DJI missing | `furble-source`; `app-static` | capture-gated |
| M11 paired-device name | FJ-S40, FJ-S41 | `85B9163E` | fujifilm C03 | Stored client name, optional NUL | `furble-` plus five hex, NUL terminated | `furble-source`; `app-static` | aligned |
| M12 paired-device name | RC3, RC4 | write | ricoh C-76, CM-15, F-30 | Stored paired name | Writes the controller name on connect | `furble-source`; `app-static` | aligned |
| M13 paired-device name | CN-S, LX | write | canon CS-05; panasonic C02 | Stored client name | Canon before the result; LUMIX implemented | `furble-source`; `app-static` | capture-gated |
| M14 paired-device name | SO, DJI, NK, CN-R | none | sony F-30; dji F-30; nikon §9.1 | No separate name write | Missing | `furble-source`; `public-source` | out-of-scope |
| M15 pairing management | FJ-S40, FJ-S41 | status, ack, name, local delete | fujifilm F pairing | Count, reason, client name | No count read and no reason write. Local delete only | `furble-source`; `firmware-static`; `app-static` | safe-fix-needed |
| M16 pairing management | RC3, RC4 | bond only | ricoh C-75, F-29, F-54 | BLE enable, paired list, delete | No enable read and no list query. Local delete only | `furble-source`; `app-static` | capture-gated |
| M17 pairing management | CN, NK, SO, LX, DJI | bond only | canon F29; nikon §9.1; sony F-29; panasonic F-29; dji F29 | Vendor-specific | No paired-list management. Sony `EE01` and LUMIX APS missing | `furble-source`; `app-static` | capture-gated |
| M18 bond deletion | FJ-S40, FJ-S41 | `deleteBond` on two failures | fujifilm §7.5 | Local bond removed; camera needs pairing mode | Implemented | `furble-source`; `hardware-capture` | aligned |
| M19 bond deletion | RC3, RC4, CN-S | `deleteBond` on a new pairing failure | ricoh §7.5; canon §7.8 | Local bond removed | New pairing only. Saved reconnect keeps a stale bond | `furble-source`; `public-source` | capture-gated |
| M20 bond deletion | others | none | sony §7.5; panasonic §7.7; dji §7.3 | Camera menu or OS | Missing | `furble-source`; `official-semantic` | capture-gated |
| M21 image transfer, list, thumbnail | all | none | fujifilm §9; ricoh F-15 to F-17; canon F15; sony F-15; panasonic F15; dji F15 | BLE handover then Wi-Fi or PTP | Missing. No camera Wi-Fi client | `furble-source`; `firmware-static`; `app-static` | capture-gated |
| M22 live view | all | none | fujifilm §9; ricoh F-14; canon F14; sony F-14; panasonic F14; dji F14 | Wi-Fi stream | Missing. No camera Wi-Fi client | `furble-source`; `official-semantic`; `public-source` | capture-gated |
| M23 backup and restore | FJ, RC, CN, NK, SO, LX, DJI | none | fujifilm C38 to C50; ricoh F-26; canon F26; nikon §9; sony F-26; panasonic F26; dji F26 | Vendor backup, partly unknown | Missing and excluded. Fujifilm backup can overwrite settings | `furble-source`; `firmware-static`; `app-static` | out-of-scope |
| M24 firmware update | all | none | fujifilm C53 to C55; ricoh C-28 to C-30; canon F28; nikon S32; sony F-28; panasonic F28; dji F28 | Vendor-controlled, partly encrypted, partly Wi-Fi | Missing. Image transport and signature policy not recovered | `furble-source`; `firmware-static`; `app-static` | out-of-scope |
| M25 model and firmware identity | FJ-S40, FJ-S41 | advertised name and serial only | fujifilm C56 to C60 | GAP name, DIS strings, serial, protocol | Advertised model and serial only. No identity characteristic | `furble-source`; `firmware-static`; `hardware-capture` | capture-gated |
| M26 model and firmware identity | RC3, RC4 | model read | ricoh C-11, F-36 | Exact model string | Reads the model and uses it as the display name | `furble-source`; `hardware-capture` | aligned |
| M27 model and firmware identity | CN, NK, SO, LX, DJI | advertised name only | canon F36; nikon §9.1; sony F-36; panasonic F-36; dji F36 | Product ID, DIS, serial, firmware | Missing. Canon no product ID; Sony ignores advert model; LUMIX model-blind; DJI device ID only | `furble-source`; `app-static`; `public-source` | capture-gated |
| M28 error status | FJ-S40, FJ-S41 | log only | fujifilm C67, N23 | `1587B102` details, `763514E8` write | Not read or written. Other subscribed messages logged as unhandled | `furble-source`; `firmware-static` | capture-gated |
| M29 error status | RC3, RC4 | capture status log | ricoh F-35, CM-91 | Capture Status fields and result codes | Subscribed and logged as raw bytes | `furble-source`; `public-source` | aligned |
| M30 error status | CN-S, SO, LX, DJI, NK | none/partial | canon CS-37; sony F-35; panasonic F-35; dji F-35; nikon §9.1 | Error and busy notifications | Canon not subscribed; Sony ignores status; LUMIX no readiness; DJI logs only; Nikon no read | `furble-source`; `app-static`; `public-source` | capture-gated |

### 2.5 Control plane: errors, timeouts, retries, cancellation


| Furble action | Family | Code path | Spec row | Expected camera response | Current behavior | Evidence | Verdict |
| --- | --- | --- | --- | --- | --- | --- | --- |
| X01 command error handling | FJ, CN, NK, SO | vendor shutter and focus ignore the `gattWrite` result | fujifilm §5; canon §5; nikon R5; sony §5 | ATT error or write failure | Failed write invisible to the UI. Command dropped | `furble-source`; `app-static` | safe-fix-needed |
| X02 command error handling | RC, LX, DJI | vendor helpers log the result | ricoh §5; panasonic §5; dji §5 | ATT error or write failure | Logged, not surfaced. DJI does not change state on failure | `furble-source` | aligned |
| X03 read error handling | all | `Camera::gattRead` returns true unconditionally | state-machines §1 | ATT error or empty value | Failure indistinguishable from a successful empty read | `furble-source` | safe-fix-needed |
| X04 vendor rejection | FJ-S40, FJ-S41 | status or ack failure aborts; drop after ack is refused registration | fujifilm §7.4 | Refused ack drops the link with reason `0x13` about 160 ms later | Aborts and prompts. No ack loop | `furble-source`; `hardware-capture` | aligned |
| X05 vendor rejection | RC3, RC4 | secure failure with a NEW pairing deletes | ricoh §7.8 | `0x0E` before bonding, `0x80` for a refused WLAN write | Secure failure returns false. Bond delete only for a NEW bonded pairing | `furble-source`; `public-source` | aligned |
| X06 vendor rejection | CN-S | pair result other than `02` | canon §7.8 | Indication not `02`, or no indication | Deletes the bond and fails. No retry | `furble-source`; `app-static` | aligned |
| X07 vendor rejection | DJI | verify result or connection response | dji §7.1 | `verify_data` 1 rejects; ret code nonzero rejects | Fails on nonzero or reject. Not repeated in the same scan | `furble-source`; `public-source` | aligned |
| X08 vendor rejection | NK-S | final OK timeout | nikon §7.3 | No final OK on Z series | Waits 1 s, fails. Expected, not an error | `furble-source`; `public-source` | unsupported |
| X09 vendor rejection | SO, LX, NK-R, CN-R | none | sony §7.7; panasonic §7.8; nikon §7.5; canon §7.8 | Mostly unknown | Failed write or handshake fails the connect. No vendor status decode | `furble-source`; `unknown` | capture-gated |
| X10 timeout, connect | all | `TIMEOUT_DEFAULT_MS`, `TIMEOUT_INFINITE_MS` | state-machines §1.7 | GAP and vendor handshake | 30 s normal, 5 s infinite reconnect | `furble-source` | aligned |
| X11 timeout, vendor waits | FJ, CN, NK, DJI | registration, pair, stage, handshake waits | fujifilm §8; canon §8; nikon §8; dji §8 | Vendor response | Fujifilm 25 s, Canon 60 s, Nikon 10 s per stage, DJI 30 s | `furble-source`; `app-static` | aligned |
| X12 timeout, vendor writes | all | no per-write vendor timeout | fujifilm §5; ricoh §5; canon §8; sony §8; panasonic §8; dji §5 | ATT transaction and supervision only | No vendor deadline. A write can block for the transaction | `furble-source`; `app-static` | capture-gated |
| X13 retry, connect | all | `Control::connectAll`, `ReconnectBackoff` | state-machines §1.5 | Not a camera action | Two failed cycles stop non-infinite mode. Infinite backs off 2.5 s to 120 s | `furble-source` | aligned |
| X14 retry, vendor | FJ, CN, SO, RC, LX, DJI | none | fujifilm §7.4; canon §7.8; sony §7.7 | Mostly final | No write retry. The next GPS tick or user action is the retry. Fujifilm registration is one attempt | `furble-source`; `unknown` | capture-gated |
| X15 cancellation, connect | all | `cancelConnect`, `connectCancelled`, `abortBlockingConnect` | state-machines §1.2 and §1.7 | Link terminate wakes a blocked NimBLE call | Polls: Fujifilm 20 ms, Canon 1 s, Nikon 250 ms, DJI 100 ms, FauxNY 25 ms | `furble-source` | aligned |
| X16 cancellation, scan | FJ, NK | saved scan loops poll the token | fujifilm §8; nikon §8 | Scan stop | Both stop the scan and return false | `furble-source` | aligned |
| X17 cancellation, secure stall | FJ, RC | `abortBlockingConnect` terminates the client | fujifilm §7.5; ricoh §7.5 | `secureConnection` returns when the link dies | Implemented. Tests pin that the stall is aborted, not outwaited | `furble-source`; `hardware-capture` | aligned |
| X18 cancellation, disconnect drain | all | `Control::disconnect`, `reclaimClient` | state-machines §1.6 | Terminate or dead link | 30 s cap, 2 s drain reclaim, deterministic free | `furble-source` | aligned |

## 3. Table 2: per-camera parity matrix

Each family table uses the goal feature list. `Transport` names the camera
surface, not the Furble transport: a feature can be `BLE GATT` on the camera and
still be `missing` in Furble. `Simulator` names the virtual peer and the scenario
that exercises the path, or `none`. `Test` names host or camera tests at this
pin, or `none (class)` when only advertisement parsing covers the family.

Evidence convention for Table 2: the `Spec row` cell names the sheet row, and the
claim carries that row's label (`firmware-static`, `app-static`,
`hardware-capture`, `public-source`, `official-semantic`, `inference`, or
`unknown`). The `Furble state`, `Simulator`, and `Test` cells are
`furble-source`. The `Hardware state` cell carries `hardware-capture` when it
says verified, `capture-gated` when no capture covers the row, and `not
applicable` when the camera has no such surface.

The simulator fleet has two real camera peers at this pin:
`FujifilmVirtualCamera` with Basic and Secure configurations, and
`RicohVirtualCamera`. `FauxNY` is a software peer, not a camera model. The
scenarios in `sim/scenarios/manifest.json` exercise connect, cancel, reconnect,
multi-target, UI, GPS, IMU, power, and fault paths; none asserts a vendor feature
byte for Canon, Nikon, Sony, LUMIX, or DJI, because no peer exists for those
families.

### 3.1 Fujifilm Basic


| Camera | Feature | Transport | Spec row | Furble state | Simulator | Test | Hardware state |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Fujifilm Basic | Still capture | BLE GATT | fujifilm C11 to C13 | implemented | peer Basic, shutter and focus writes | `fujifilm_camera_test` | capture-gated, no Basic body capture |
| Fujifilm Basic | Half press and focus | BLE GATT | fujifilm §5.1, C14 | implemented | peer Basic, focus write | `fujifilm_camera_test` | capture-gated |
| Fujifilm Basic | Bulb | BLE GATT, local hold | fujifilm §5.1 | implemented, local hold only | peer counts press and release | none (bulb-specific) | capture-gated |
| Fujifilm Basic | Self-timer (still) | camera menu | fujifilm §9 | unavailable, menu-only | none | none | not applicable |
| Fujifilm Basic | Self-timer (movie) | camera menu or none | fujifilm §9 | unavailable | none | none | not applicable |
| Fujifilm Basic | Interval, camera-side | camera menu | fujifilm §9 | unavailable, menu-only | none | none | not applicable |
| Fujifilm Basic | Interval, local | BLE GATT repeated shutter | fujifilm §9 | implemented | scenario uses FauxNY, not this peer | none (class) | capture-gated |
| Fujifilm Basic | Burst | camera menu | fujifilm §9 | unavailable, menu-only | none | none | not applicable |
| Fujifilm Basic | Movie start and stop | BLE GATT | fujifilm C15 | missing | none | none (class) | capture-gated |
| Fujifilm Basic | Zoom | none recovered | fujifilm §9 | out of scope | none | none | not applicable on fixed-lens bodies |
| Fujifilm Basic | Power on and off | BLE GATT | fujifilm C18 | missing | none | none (class) | capture-gated |
| Fujifilm Basic | Wake | BLE advertisement and GATT | fujifilm §2.5, C19 | missing | none | none (class) | capture-gated |
| Fujifilm Basic | Sleep | BLE GATT status only | fujifilm C21 | unavailable, status only | none | none | not applicable |
| Fujifilm Basic | Live view | Wi-Fi | fujifilm §9 | missing | none | none | not applicable over BLE |
| Fujifilm Basic | Image transfer, list, thumbnail | Wi-Fi, BLE handover | fujifilm §9 | missing | none | none | capture-gated |
| Fujifilm Basic | Clock and timezone | BLE GATT | fujifilm C28, C29 | missing | none | none | capture-gated |
| Fujifilm Basic | GPS push and pull | BLE GATT push, no pull | fujifilm C22, N03 | implemented, partial: push only | peer request and packet | `fujifilm_camera_test` | capture-gated |
| Fujifilm Basic | Geotag enable and sync interval | BLE GATT | fujifilm C23, C25 | implemented, interval only, enable missing | peer does not model enable | none | capture-gated |
| Fujifilm Basic | Settings read | BLE GATT | fujifilm §4 | missing | none | none | capture-gated |
| Fujifilm Basic | Settings write | BLE GATT | fujifilm §5 | missing | none | none | capture-gated |
| Fujifilm Basic | Backup and restore | BLE GATT | fujifilm C38 to C50 | out of scope | none | none | capture-gated |
| Fujifilm Basic | Firmware update | BLE GATT | fujifilm C53 to C55 | out of scope | none | none | capture-gated |
| Fujifilm Basic | Pairing management | BLE GATT, token | fujifilm §7.6 | implemented | peer token accept | `fujifilm_camera_test` | capture-gated |
| Fujifilm Basic | Battery | none | fujifilm §5.5 | unavailable | none | none | not applicable |
| Fujifilm Basic | Storage | BLE GATT | fujifilm C62 | missing | none | none | capture-gated |
| Fujifilm Basic | Error and busy status | BLE GATT | fujifilm C67, N23 | missing | none | none | capture-gated |
| Fujifilm Basic | Model and firmware identity | BLE advertisement and GATT | fujifilm C56, C57 | missing, advertised name only | peer advertises name and token | `fujifilm_camera_test` | capture-gated |
| Fujifilm Basic | Model-specific controls | BLE GATT strings | fujifilm §9 | missing | none | none | capture-gated |

### 3.2 Fujifilm Secure v4.0


| Camera | Feature | Transport | Spec row | Furble state | Simulator | Test | Hardware state |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Fujifilm Secure v4.0 | Still capture | BLE GATT | fujifilm C11 to C13 | implemented | peer Secure, shutter writes | `fujifilm_camera_test`, `fujifilm_registration_gate_test` | capture-gated, wire traced, physical still not captured |
| Fujifilm Secure v4.0 | Half press and focus | BLE GATT | fujifilm §5.1, C14 | implemented | peer Secure, focus write | `fujifilm_camera_test` | capture-gated |
| Fujifilm Secure v4.0 | Bulb | BLE GATT, local hold | fujifilm §5.1 | implemented, local hold only | peer counts writes | none | capture-gated |
| Fujifilm Secure v4.0 | Self-timer (still) | camera menu | fujifilm §9 | unavailable, menu-only | none | none | not applicable |
| Fujifilm Secure v4.0 | Self-timer (movie) | BLE string on X-S20 | fujifilm §9 | missing | none | none | capture-gated |
| Fujifilm Secure v4.0 | Interval, camera-side | camera menu | fujifilm §9 | unavailable, menu-only | none | none | not applicable |
| Fujifilm Secure v4.0 | Interval, local | BLE GATT repeated shutter | fujifilm §9 | implemented | no Fujifilm scenario | none | capture-gated |
| Fujifilm Secure v4.0 | Burst | camera menu | fujifilm §9 | unavailable, menu-only | none | none | not applicable |
| Fujifilm Secure v4.0 | Movie start and stop | BLE GATT | fujifilm C15 | missing | none | none | capture-gated |
| Fujifilm Secure v4.0 | Zoom | none recovered | fujifilm §9 | out of scope | none | none | not applicable on X100VI, capture-gated on X-S20 |
| Fujifilm Secure v4.0 | Power on and off | BLE GATT | fujifilm C18, C19 | missing | none | none | capture-gated |
| Fujifilm Secure v4.0 | Wake | BLE advertisement and GATT | fujifilm §2.5 | missing | none | none | capture-gated |
| Fujifilm Secure v4.0 | Sleep | BLE GATT status only | fujifilm C21 | unavailable, status only | none | none | not applicable |
| Fujifilm Secure v4.0 | Live view | Wi-Fi | fujifilm §9 | missing | none | none | not applicable over BLE |
| Fujifilm Secure v4.0 | Image transfer, list, thumbnail | Wi-Fi, BLE handover | fujifilm C17, C68 | missing | none | none | capture-gated |
| Fujifilm Secure v4.0 | Clock and timezone | BLE GATT | fujifilm C28, C29 | missing | none | none | capture-gated, `F9150137` semantics disputed |
| Fujifilm Secure v4.0 | GPS push and pull | BLE GATT push, no pull | fujifilm C22, N03 | implemented, partial: push only | peer request and packet, byte exact | `fujifilm_camera_test` | verified on the request and packet, EXIF capture-gated |
| Fujifilm Secure v4.0 | Geotag enable and sync interval | BLE GATT | fujifilm C23, C25 | implemented, interval only, enable missing | peer does not model enable | none | verified for the interval echo, enable capture-gated |
| Fujifilm Secure v4.0 | Settings read | BLE GATT | fujifilm §4 | missing, pairing status only | peer status read | `fujifilm_registration_gate_test` | capture-gated |
| Fujifilm Secure v4.0 | Settings write | BLE GATT | fujifilm C23, C30 to C37 | implemented, interval only | peer interval write | none | capture-gated |
| Fujifilm Secure v4.0 | Backup and restore | BLE GATT | fujifilm C38 to C50 | out of scope | none | none | capture-gated |
| Fujifilm Secure v4.0 | Firmware update | BLE GATT | fujifilm C53 to C55 | out of scope | none | none | capture-gated |
| Fujifilm Secure v4.0 | Pairing management | BLE GATT | fujifilm C01 to C03, §7.2 | implemented | peer status, ack, name, subscriptions | `fujifilm_registration_gate_test`, `fujifilm_stale_bond_test` | verified on X100VI 1.31/1.32 and the 2026-09 bench |
| Fujifilm Secure v4.0 | Battery | none | fujifilm §5.5 | unavailable | none | none | not applicable |
| Fujifilm Secure v4.0 | Storage | BLE GATT | fujifilm C62 | missing | none | none | capture-gated |
| Fujifilm Secure v4.0 | Error and busy status | BLE GATT | fujifilm C67, N23 | missing | none | none | capture-gated |
| Fujifilm Secure v4.0 | Model and firmware identity | BLE advertisement and GATT | fujifilm C56 to C60 | missing, advertised name and serial only | peer advertises model and serial | `fujifilm_device_name_test` | verified for the advertised name, GATT identity capture-gated |
| Fujifilm Secure v4.0 | Model-specific controls | BLE GATT strings | fujifilm §9 | missing | none | none | capture-gated |

### 3.3 Fujifilm Secure v4.1


| Camera | Feature | Transport | Spec row | Furble state | Simulator | Test | Hardware state |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Fujifilm Secure v4.1 | Still capture | BLE GATT | fujifilm C11 to C13, §7.7 | implemented, shared v4.0 path | peer is v4.0 only | shared v4.0 tests | capture-gated, no X-E5 capture |
| Fujifilm Secure v4.1 | Half press and focus | BLE GATT | fujifilm §5.1 | implemented | peer is v4.0 only | shared v4.0 tests | capture-gated |
| Fujifilm Secure v4.1 | Bulb | BLE GATT, local hold | fujifilm §5.1 | implemented, local hold only | none | none | capture-gated |
| Fujifilm Secure v4.1 | Self-timer (still) | camera menu | fujifilm §9 | unavailable, menu-only | none | none | not applicable |
| Fujifilm Secure v4.1 | Self-timer (movie) | camera menu or none | fujifilm §9 | unavailable | none | none | not applicable |
| Fujifilm Secure v4.1 | Interval, camera-side | camera menu | fujifilm §9 | unavailable, menu-only | none | none | not applicable |
| Fujifilm Secure v4.1 | Interval, local | BLE GATT repeated shutter | fujifilm §9 | implemented | none | none | capture-gated |
| Fujifilm Secure v4.1 | Burst | camera menu | fujifilm §9 | unavailable, menu-only | none | none | not applicable |
| Fujifilm Secure v4.1 | Movie start and stop | BLE GATT | fujifilm C15 | missing | none | none | capture-gated |
| Fujifilm Secure v4.1 | Zoom | none recovered | fujifilm §9 | out of scope | none | none | not applicable on X-E5 |
| Fujifilm Secure v4.1 | Power on and off | BLE GATT | fujifilm C18, C19 | missing | none | none | capture-gated |
| Fujifilm Secure v4.1 | Wake | BLE advertisement and GATT | fujifilm §2.5 | missing | none | none | capture-gated |
| Fujifilm Secure v4.1 | Sleep | BLE GATT status only | fujifilm C21 | unavailable, status only | none | none | not applicable |
| Fujifilm Secure v4.1 | Live view | Wi-Fi | fujifilm §9 | missing | none | none | not applicable over BLE |
| Fujifilm Secure v4.1 | Image transfer, list, thumbnail | Wi-Fi, BLE handover | fujifilm C17, C68 | missing | none | none | capture-gated |
| Fujifilm Secure v4.1 | Clock and timezone | BLE GATT | fujifilm C28, C29 | missing | none | none | capture-gated |
| Fujifilm Secure v4.1 | GPS push and pull | BLE GATT push, no pull | fujifilm C22, N03 | implemented, push only | peer is v4.0 only | shared v4.0 tests | capture-gated |
| Fujifilm Secure v4.1 | Geotag enable and sync interval | BLE GATT | fujifilm C23, C25 | implemented, interval only, enable missing | none | none | capture-gated |
| Fujifilm Secure v4.1 | Settings read | BLE GATT | fujifilm §4 | missing | none | none | capture-gated |
| Fujifilm Secure v4.1 | Settings write | BLE GATT | fujifilm C23, C30 to C37 | implemented, interval only | none | none | capture-gated |
| Fujifilm Secure v4.1 | Backup and restore | BLE GATT | fujifilm C38 to C50 | out of scope | none | none | capture-gated |
| Fujifilm Secure v4.1 | Firmware update | BLE GATT | fujifilm C53 to C55 | out of scope | none | none | capture-gated |
| Fujifilm Secure v4.1 | Pairing management | BLE GATT | fujifilm §7.7, C05 | capture-gated, `8B5ECF55` not written | none | none | capture-gated, no X-E5 capture |
| Fujifilm Secure v4.1 | Battery | none | fujifilm §5.5 | unavailable | none | none | not applicable |
| Fujifilm Secure v4.1 | Storage | BLE GATT | fujifilm C62 | missing | none | none | capture-gated |
| Fujifilm Secure v4.1 | Error and busy status | BLE GATT | fujifilm C67, N23 | missing | none | none | capture-gated |
| Fujifilm Secure v4.1 | Model and firmware identity | BLE advertisement and GATT | fujifilm C56 to C60 | missing, advertised name and serial only | none | `fujifilm_device_name_test` | capture-gated |
| Fujifilm Secure v4.1 | Model-specific controls | BLE GATT | fujifilm §7.7 | missing | none | none | capture-gated |

### 3.4 Ricoh GR III, GR IIIx, and HDF


| Camera | Feature | Transport | Spec row | Furble state | Simulator | Test | Hardware state |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Ricoh GR III/IIIx | Still capture | BLE GATT | ricoh CM-16, F-01 | implemented, gated on a fresh CAPTURE read | peer with standby and capture writes | `ricoh_focus_test`, `ricoh_sleep_gate_test` | capture-gated, author-reported |
| Ricoh GR III/IIIx | Half press and focus | BLE none, HTTP focus only | ricoh F-02 | unavailable, no-op by design | peer accepts no focus write | `ricoh_focus_test` | not applicable over BLE |
| Ricoh GR III/IIIx | Bulb | BLE, no stop primitive | ricoh F-04 | unavailable, release is ignored | none | none | capture-gated |
| Ricoh GR III/IIIx | Self-timer (still) | BLE GATT | ricoh F-05, CM-18 | missing, `TIMER_2S` unused | none | none | capture-gated |
| Ricoh GR III/IIIx | Self-timer (movie) | camera menu or none | ricoh F-05 | unavailable | none | none | not applicable |
| Ricoh GR III/IIIx | Interval, camera-side | BLE GATT | ricoh F-06, CM-18 | missing | none | none | capture-gated |
| Ricoh GR III/IIIx | Interval, local | BLE GATT repeated capture | ricoh F-06 | implemented | none | none | capture-gated |
| Ricoh GR III/IIIx | Burst | BLE GATT Drive Mode | ricoh F-07 | missing | none | none | capture-gated |
| Ricoh GR III/IIIx | Movie start and stop | BLE GATT | ricoh F-08 | missing | none | none | capture-gated |
| Ricoh GR III/IIIx | Zoom | none, fixed lens | ricoh F-09 | out of scope | none | none | not applicable |
| Ricoh GR III/IIIx | Power on and off | BLE and connect wake | ricoh CM-01, F-10, F-11 | missing, connect wake only | none | none | capture-gated |
| Ricoh GR III/IIIx | Wake | BLE connect | ricoh F-12 | implemented, connect only | peer accepts connect | `ricoh_sleep_gate_test` covers the gate, not the wake | capture-gated |
| Ricoh GR III/IIIx | Sleep | BLE GATT read only | ricoh C-17 | unavailable, value 2 refused on write | none | none | not applicable, menu or auto sleep |
| Ricoh GR III/IIIx | Live view | Wi-Fi HTTP | ricoh F-14 | missing | none | none | capture-gated |
| Ricoh GR III/IIIx | Image transfer, list, thumbnail | Wi-Fi HTTP | ricoh F-15 to F-17 | missing | none | none | capture-gated |
| Ricoh GR III/IIIx | Clock and timezone | BLE GATT | ricoh CM-03, CM-40 | missing | none | none | capture-gated |
| Ricoh GR III/IIIx | GPS push and pull | BLE GATT push, read exists | ricoh CM-31, F-21, F-22 | implemented, partial: push only | peer accepts the packet | `ricoh_focus_test` | capture-gated, author-reported |
| Ricoh GR III/IIIx | Geotag enable and sync interval | BLE GATT | ricoh CM-04, F-23 | implemented, enable only, no camera interval | peer records enable | `ricoh_focus_test` | capture-gated |
| Ricoh GR III/IIIx | Settings read | BLE GATT | ricoh F-24 | implemented, partial: model and state probe | peer serves model and state | `ricoh_focus_test` | capture-gated |
| Ricoh GR III/IIIx | Settings write | BLE GATT | ricoh F-25 | implemented, partial: Drive Mode and geotag | peer records both | `ricoh_focus_test` | capture-gated |
| Ricoh GR III/IIIx | Backup and restore | none in BLE | ricoh F-26, F-27 | out of scope | none | none | not applicable |
| Ricoh GR III/IIIx | Firmware update | SD card only | ricoh F-28 | out of scope | none | none | not applicable |
| Ricoh GR III/IIIx | Pairing management | BLE and camera menu | ricoh F-29, F-30, F-31 | implemented, partial: bond delete on a new pairing only | none | `camera_regression_test` bond policy | capture-gated |
| Ricoh GR III/IIIx | Battery | BLE GATT | ricoh CM-39, F-32 | missing | none | none | capture-gated |
| Ricoh GR III/IIIx | Storage | BLE GATT | ricoh CM-44, F-33 | missing | none | none | capture-gated |
| Ricoh GR III/IIIx | Error and busy status | BLE GATT | ricoh F-35, CM-91 | implemented, partial: capture status logged | peer can emit notifications | `ricoh_control_flap_test` | capture-gated |
| Ricoh GR III/IIIx | Model and firmware identity | BLE GATT | ricoh C-11, F-36 | implemented, model read | peer serves model | `ricoh_focus_test` | capture-gated |
| Ricoh GR III/IIIx | Model-specific controls | BLE GATT | ricoh F-38, F-43, F-45 | missing | none | none | capture-gated |

### 3.5 Ricoh GR IV and GR IV HDF


| Camera | Feature | Transport | Spec row | Furble state | Simulator | Test | Hardware state |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Ricoh GR IV/HDF | Still capture | BLE GATT | ricoh CM-16, CM-18, F-01 | implemented | peer with standby and capture writes | `ricoh_focus_test`, `ricoh_sleep_gate_test` | verified at the write level, physical image not captured |
| Ricoh GR IV/HDF | Half press and focus | BLE none, HTTP focus only | ricoh F-02 | unavailable, no-op by design | peer accepts no focus write | `ricoh_focus_test` | not applicable over BLE |
| Ricoh GR IV/HDF | Bulb | BLE, no stop primitive | ricoh F-04 | unavailable, release is ignored | none | none | capture-gated |
| Ricoh GR IV/HDF | Self-timer (still) | BLE GATT | ricoh F-05, CM-18 | missing, `TIMER_2S` unused | none | none | capture-gated |
| Ricoh GR IV/HDF | Self-timer (movie) | camera menu or none | ricoh F-05 | unavailable | none | none | not applicable |
| Ricoh GR IV/HDF | Interval, camera-side | BLE GATT | ricoh F-06 | missing | none | none | capture-gated |
| Ricoh GR IV/HDF | Interval, local | BLE GATT repeated capture | ricoh F-06 | implemented | none | none | capture-gated |
| Ricoh GR IV/HDF | Burst | BLE GATT Drive Mode | ricoh F-07 | missing | none | none | capture-gated |
| Ricoh GR IV/HDF | Movie start and stop | BLE GATT | ricoh F-08 | missing | none | none | capture-gated |
| Ricoh GR IV/HDF | Zoom | none, fixed lens | ricoh F-09 | out of scope | none | none | not applicable |
| Ricoh GR IV/HDF | Power on and off | BLE GATT | ricoh CM-01, F-10, F-11 | missing, off is out of scope | none | none | capture-gated |
| Ricoh GR IV/HDF | Wake | BLE connect and power | ricoh F-12, F-47 | capture-gated, connect does not wake a BLE_STARTUP body | peer models standby and flap | `ricoh_sleep_gate_test`, `ricoh_control_flap_test` | verified for the standby trap |
| Ricoh GR IV/HDF | Sleep | BLE GATT read only | ricoh C-17 | unavailable, value 2 refused on write | none | none | not applicable, menu or auto sleep |
| Ricoh GR IV/HDF | Live view | Wi-Fi HTTP | ricoh F-14 | missing | none | none | capture-gated |
| Ricoh GR IV/HDF | Image transfer, list, thumbnail | Wi-Fi HTTP | ricoh F-15 to F-17 | missing | none | none | capture-gated |
| Ricoh GR IV/HDF | Clock and timezone | BLE GATT | ricoh CM-03, CM-40 | missing | none | none | capture-gated |
| Ricoh GR IV/HDF | GPS push and pull | BLE GATT push, read exists | ricoh CM-31, F-21, F-22 | implemented, partial: push only | peer accepts the packet | `ricoh_focus_test` | verified for the packet layout, EXIF capture-gated |
| Ricoh GR IV/HDF | Geotag enable and sync interval | BLE GATT | ricoh CM-04, F-23 | implemented, enable only, no camera interval | peer records enable | `ricoh_focus_test` | verified for the enable write, notification capture-gated |
| Ricoh GR IV/HDF | Settings read | BLE GATT | ricoh F-24 | implemented, partial: model and state probe | peer serves model and state | `ricoh_focus_test` | capture-gated |
| Ricoh GR IV/HDF | Settings write | BLE GATT | ricoh F-25 | implemented, partial: Drive Mode and geotag | peer records both | `ricoh_focus_test` | capture-gated |
| Ricoh GR IV/HDF | Backup and restore | none in BLE | ricoh F-26, F-27 | out of scope | none | none | not applicable |
| Ricoh GR IV/HDF | Firmware update | BLE control plus Wi-Fi | ricoh F-28 | out of scope | none | none | capture-gated |
| Ricoh GR IV/HDF | Pairing management | BLE and camera menu | ricoh F-29, F-30, F-54 | implemented, partial: bond delete only | none | `camera_regression_test` bond policy | capture-gated |
| Ricoh GR IV/HDF | Battery | BLE GATT | ricoh CM-39, F-32 | missing | none | none | capture-gated |
| Ricoh GR IV/HDF | Storage | BLE GATT | ricoh CM-44, F-33 | missing | none | none | capture-gated |
| Ricoh GR IV/HDF | Error and busy status | BLE GATT | ricoh F-35, CM-91 | implemented, partial: capture status logged | peer can emit notifications | `ricoh_control_flap_test` | capture-gated |
| Ricoh GR IV/HDF | Model and firmware identity | BLE GATT | ricoh C-11, F-36 | implemented, model read | peer serves model | `ricoh_focus_test` | capture-gated, no GR IV GATT dump |
| Ricoh GR IV/HDF | Model-specific controls | BLE GATT | ricoh F-43, F-44, F-45 | missing | none | none | capture-gated |

### 3.6 Canon EOS Smart


| Camera | Feature | Transport | Spec row | Furble state | Simulator | Test | Hardware state |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Canon EOS Smart | Still capture | BLE GATT | canon CS-16, CS-17, F01 | implemented | none | none (class) | capture-gated |
| Canon EOS Smart | Half press and focus | BLE GATT | canon CS-18, CS-19, F02 | implemented | none | none (class) | capture-gated |
| Canon EOS Smart | Bulb | BLE GATT, local hold | canon F04 | implemented, local hold only | none | none | capture-gated |
| Canon EOS Smart | Self-timer (still) | camera menu, status only | canon F05 | unavailable, menu-only | none | none | not applicable |
| Canon EOS Smart | Self-timer (movie) | camera menu or none | canon F05 | unavailable | none | none | not applicable |
| Canon EOS Smart | Interval, camera-side | camera menu | canon F05 | unavailable, menu-only | none | none | not applicable |
| Canon EOS Smart | Interval, local | BLE GATT repeated shutter | canon F06 | implemented | none | none | capture-gated |
| Canon EOS Smart | Burst | camera menu | canon F07 | unavailable, menu-only | none | none | not applicable |
| Canon EOS Smart | Movie start and stop | BLE GATT | canon CS-22, CS-23 | missing | none | none | capture-gated |
| Canon EOS Smart | Zoom | BLE GATT | canon CS-24 to CS-27 | missing | none | none | capture-gated, no app call site |
| Canon EOS Smart | Power on and off | BLE GATT | canon CS-14, F10, F11 | missing | none | none | capture-gated |
| Canon EOS Smart | Wake | BLE GATT | canon F12 | missing, writes `02` not `03` | none | none | capture-gated |
| Canon EOS Smart | Sleep | BLE advertisement and status | canon F13 | unavailable, status only | none | none | not applicable |
| Canon EOS Smart | Live view | Wi-Fi, BLE status only | canon F14 | missing | none | none | capture-gated |
| Canon EOS Smart | Image transfer, list, thumbnail | Wi-Fi and PTP | canon F15 to F17 | missing | none | none | capture-gated |
| Canon EOS Smart | Clock and timezone | PTP, GPS timestamp only | canon F18 to F20 | missing | none | none | capture-gated |
| Canon EOS Smart | GPS push and pull | BLE GATT push, no pull | canon CS-34, F21, F22 | implemented, partial: one-byte enable and 20-byte packet | none | none (class) | capture-gated |
| Canon EOS Smart | Geotag enable and sync interval | BLE GATT | canon CS-28 to CS-33 | implemented, partial: no source select, no stop, no request ack | none | none (class) | capture-gated |
| Canon EOS Smart | Settings read | BLE GATT | canon CS-12, CS-13, CS-37 | missing | none | none | capture-gated |
| Canon EOS Smart | Settings write | BLE GATT and PTP | canon CS-10 | implemented, partial: mode only | none | none | capture-gated |
| Canon EOS Smart | Backup and restore | none known | canon F26, F27 | unavailable | none | none | not applicable |
| Canon EOS Smart | Firmware update | network and card | canon F28 | out of scope | none | none | capture-gated |
| Canon EOS Smart | Pairing management | BLE GATT and OS bond | canon CS-01 to CS-06 | capture-gated, order differs | none | none (class) | capture-gated |
| Canon EOS Smart | Battery | error category only | canon F32 | unavailable | none | none | not applicable |
| Canon EOS Smart | Storage | error category only | canon F33 | unavailable | none | none | not applicable |
| Canon EOS Smart | Error and busy status | BLE GATT | canon CS-37, F35 | missing | none | none | capture-gated |
| Canon EOS Smart | Model and firmware identity | BLE advertisement and DIS | canon F36 | missing, no product ID parse | none | none (class) | capture-gated |
| Canon EOS Smart | Model-specific controls | app model gates | canon F37, F42 | missing | none | none | capture-gated |

### 3.7 Canon EOS Remote


| Camera | Feature | Transport | Spec row | Furble state | Simulator | Test | Hardware state |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Canon EOS Remote | Still capture | BLE GATT | canon CR-04, CR-05 | implemented | none | none (class) | capture-gated |
| Canon EOS Remote | Half press and focus | BLE GATT | canon CR-06 | implemented | none | none (class) | capture-gated |
| Canon EOS Remote | Bulb | BLE GATT, local hold | canon F04 | implemented, local hold only | none | none | capture-gated |
| Canon EOS Remote | Self-timer (still) | BLE GATT 2 s delay bit | canon CR-07 | missing | none | none | capture-gated, byte is inference |
| Canon EOS Remote | Self-timer (movie) | camera menu or none | canon F05 | unavailable | none | none | not applicable |
| Canon EOS Remote | Interval, camera-side | camera menu | canon F05 | unavailable, menu-only | none | none | not applicable |
| Canon EOS Remote | Interval, local | BLE GATT repeated shutter | canon F06 | implemented | none | none | capture-gated |
| Canon EOS Remote | Burst | camera menu | canon F07 | unavailable, menu-only | none | none | not applicable |
| Canon EOS Remote | Movie start and stop | BLE GATT | canon CR-08 | missing | none | none | capture-gated, byte is inference |
| Canon EOS Remote | Zoom | BLE GATT | canon CR-09, CR-10 | missing | none | none | capture-gated, byte is inference |
| Canon EOS Remote | Power on and off | none | canon F10, F11 | unavailable | none | none | not applicable |
| Canon EOS Remote | Wake | unknown | canon F12 | missing | none | none | capture-gated |
| Canon EOS Remote | Sleep | BLE advertisement and status | canon F13 | unavailable, status only | none | none | not applicable |
| Canon EOS Remote | Live view | none over BLE | canon F14 | unavailable | none | none | not applicable |
| Canon EOS Remote | Image transfer, list, thumbnail | none over BLE | canon F15 to F17 | unavailable | none | none | not applicable |
| Canon EOS Remote | Clock and timezone | none over BLE | canon F18 to F20 | unavailable | none | none | not applicable |
| Canon EOS Remote | GPS push and pull | none | canon F21, F22 | unavailable | none | none | not applicable |
| Canon EOS Remote | Geotag enable and sync interval | none | canon F23 | unavailable | none | none | not applicable |
| Canon EOS Remote | Settings read | none | canon F24 | unavailable | none | none | not applicable |
| Canon EOS Remote | Settings write | none | canon F25 | unavailable | none | none | not applicable |
| Canon EOS Remote | Backup and restore | none | canon F26, F27 | unavailable | none | none | not applicable |
| Canon EOS Remote | Firmware update | none over BLE | canon F28 | out of scope | none | none | not applicable |
| Canon EOS Remote | Pairing management | BLE GATT | canon CR-01 to CR-03 | implemented, name write only | none | none (class) | capture-gated |
| Canon EOS Remote | Battery | none | canon F32 | unavailable | none | none | not applicable |
| Canon EOS Remote | Storage | none | canon F33 | unavailable | none | none | not applicable |
| Canon EOS Remote | Error and busy status | none | canon F35 | unavailable | none | none | not applicable |
| Canon EOS Remote | Model and firmware identity | none | canon F36 | unavailable | none | none | not applicable |
| Canon EOS Remote | Model-specific controls | none | canon F37 | unavailable | none | none | not applicable |

### 3.8 Nikon Remote


| Camera | Feature | Transport | Spec row | Furble state | Simulator | Test | Hardware state |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Nikon Remote | Still capture | BLE GATT | nikon R5 | implemented | none | none (class) | capture-gated |
| Nikon Remote | Half press and focus | none | nikon §9.1 | unavailable | none | none | not applicable |
| Nikon Remote | Bulb | none, local hold | nikon §9.1 | out of scope | none | none | not applicable |
| Nikon Remote | Self-timer (still) | none | nikon §9.1 | unavailable | none | none | not applicable |
| Nikon Remote | Self-timer (movie) | camera menu or none | nikon §9.1 | unavailable | none | none | not applicable |
| Nikon Remote | Interval, camera-side | camera menu | nikon §9.1 | unavailable, menu-only | none | none | not applicable |
| Nikon Remote | Interval, local | BLE GATT repeated press | nikon §9.1 | implemented | none | none | capture-gated |
| Nikon Remote | Burst | none | nikon §9.1 | unavailable | none | none | not applicable |
| Nikon Remote | Movie start and stop | none | nikon §9.1 | unavailable, `MODE_VIDEO` never sent | none | none | not applicable |
| Nikon Remote | Zoom | none | nikon §9.1 | unavailable | none | none | not applicable |
| Nikon Remote | Power on and off | none | nikon §9.1 | unavailable | none | none | not applicable |
| Nikon Remote | Wake | none | nikon §9.1 | unavailable | none | none | not applicable |
| Nikon Remote | Sleep | none | nikon §9.1 | unavailable | none | none | not applicable |
| Nikon Remote | Live view | none | nikon §9.1 | unavailable | none | none | not applicable |
| Nikon Remote | Image transfer, list, thumbnail | none | nikon §9.1 | unavailable | none | none | not applicable |
| Nikon Remote | Clock and timezone | none | nikon §9.1 | unavailable | none | none | not applicable |
| Nikon Remote | GPS push and pull | none | nikon §9.1 | unavailable | none | none | not applicable |
| Nikon Remote | Geotag enable and sync interval | none | nikon §9.1 | unavailable | none | none | not applicable |
| Nikon Remote | Settings read | BLE GATT | nikon R7 to R13 | missing | none | none | capture-gated |
| Nikon Remote | Settings write | BLE GATT | nikon R14 | missing | none | none | capture-gated |
| Nikon Remote | Backup and restore | none | nikon §9.1 | unavailable | none | none | not applicable |
| Nikon Remote | Firmware update | none over BLE | nikon §9.1 | out of scope | none | none | not applicable |
| Nikon Remote | Pairing management | BLE GATT | nikon R1 to R4 | implemented | none | none (class) | capture-gated |
| Nikon Remote | Battery | BLE GATT | nikon §9.1 | missing | none | none | capture-gated |
| Nikon Remote | Storage | none | nikon §9.1 | unavailable | none | none | not applicable |
| Nikon Remote | Error and busy status | BLE GATT | nikon R8, R10 | missing | none | none | capture-gated |
| Nikon Remote | Model and firmware identity | BLE GATT | nikon §9.1 | missing | none | none | capture-gated |
| Nikon Remote | Model-specific controls | BLE GATT | nikon §9.1 | missing | none | none | capture-gated |

### 3.9 Nikon Smart


| Camera | Feature | Transport | Spec row | Furble state | Simulator | Test | Hardware state |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Nikon Smart | Still capture | BLE GATT | nikon §9.2 | unavailable | none | none (class) | capture-gated |
| Nikon Smart | Half press and focus | BLE GATT | nikon §9.2 | unavailable | none | none (class) | capture-gated |
| Nikon Smart | Bulb | BLE GATT | nikon §9.2 | unavailable | none | none (class) | capture-gated |
| Nikon Smart | Self-timer (still) | BLE GATT | nikon §9.2 | unavailable | none | none (class) | capture-gated |
| Nikon Smart | Self-timer (movie) | camera menu or none | nikon §9.2 | unavailable | none | none | not applicable |
| Nikon Smart | Interval, camera-side | camera menu | nikon §9.2 | unavailable, menu-only | none | none | not applicable |
| Nikon Smart | Interval, local | BLE GATT | nikon §9.2 | unavailable | none | none (class) | capture-gated |
| Nikon Smart | Burst | BLE GATT | nikon §9.2 | unavailable | none | none (class) | capture-gated |
| Nikon Smart | Movie start and stop | BLE GATT | nikon §9.2 | unavailable | none | none (class) | capture-gated |
| Nikon Smart | Zoom | BLE GATT | nikon §9.2 | unavailable | none | none (class) | capture-gated |
| Nikon Smart | Power on and off | BLE GATT | nikon §9.2 | unavailable | none | none (class) | capture-gated |
| Nikon Smart | Wake | BLE GATT | nikon §9.2 | unavailable | none | none (class) | capture-gated |
| Nikon Smart | Sleep | BLE GATT | nikon §9.2 | unavailable | none | none (class) | capture-gated |
| Nikon Smart | Live view | BLE GATT | nikon §9.2 | unavailable | none | none (class) | capture-gated |
| Nikon Smart | Image transfer, list, thumbnail | BLE GATT | nikon §9.2 | unavailable | none | none (class) | capture-gated |
| Nikon Smart | Clock and timezone | BLE GATT | nikon S7 | unavailable, code exists but is unreachable | none | none | capture-gated |
| Nikon Smart | GPS push and pull | BLE GATT push, no pull | nikon S8, S9 | unavailable, code exists but is unreachable | none | none | capture-gated |
| Nikon Smart | Geotag enable and sync interval | BLE GATT | nikon S8 | unavailable, code exists but is unreachable | none | none | capture-gated |
| Nikon Smart | Settings read | BLE GATT | nikon §9.2 | unavailable | none | none (class) | capture-gated |
| Nikon Smart | Settings write | BLE GATT | nikon §9.2 | unavailable | none | none (class) | capture-gated |
| Nikon Smart | Backup and restore | BLE GATT | nikon §9.2 | unavailable | none | none (class) | capture-gated |
| Nikon Smart | Firmware update | BLE GATT | nikon §9.2 | unavailable | none | none (class) | capture-gated |
| Nikon Smart | Pairing management | BLE GATT plus Classic | nikon S1 to S5, §7.3 | unavailable, BLE half runs and Classic is blocked | none | none (class) | capture-gated |
| Nikon Smart | Battery | BLE GATT | nikon §9.2 | unavailable | none | none (class) | capture-gated |
| Nikon Smart | Storage | BLE GATT | nikon §9.2 | unavailable | none | none (class) | capture-gated |
| Nikon Smart | Error and busy status | BLE GATT | nikon S22 | unavailable | none | none | capture-gated |
| Nikon Smart | Model and firmware identity | BLE GATT | nikon S6, S11 | unavailable | none | none | capture-gated |
| Nikon Smart | Model-specific controls | BLE GATT | nikon §9.2 | unavailable | none | none (class) | capture-gated |

### 3.10 Sony


| Camera | Feature | Transport | Spec row | Furble state | Simulator | Test | Hardware state |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Sony | Still capture | BLE GATT, PTP/IP primary | sony F-01, F-03 | implemented, full press only | none | none (class) | capture-gated |
| Sony | Half press and focus | BLE GATT, PTP/IP primary | sony F-02 | implemented | none | none (class) | capture-gated |
| Sony | Bulb | PTP/IP | sony F-04 | implemented, local hold only | none | none | capture-gated |
| Sony | Self-timer (still) | PTP/IP | sony F-05 | missing, PTP-only | none | none | capture-gated |
| Sony | Self-timer (movie) | camera menu or none | sony F-05 | unavailable | none | none | not applicable |
| Sony | Interval, camera-side | BLE GATT | sony §9 | missing | none | none (class) | capture-gated |
| Sony | Interval, local | BLE GATT repeated shutter | sony F-06 | implemented | none | none | capture-gated |
| Sony | Burst | BLE GATT | sony §9 | missing | none | none (class) | capture-gated |
| Sony | Movie start and stop | BLE DiRxTx, PTP/IP | sony F-08 | missing | none | none | capture-gated |
| Sony | Zoom | PTP/IP | sony F-09 | out of scope, BLE | none | none | capture-gated |
| Sony | Power on and off | BLE GATT | sony C-01, F-10, F-11 | missing | none | none | capture-gated |
| Sony | Wake | BLE advertisement and GATT | sony F-12 | implemented, connect only | none | none | capture-gated |
| Sony | Sleep | unknown | sony F-13 | unavailable | none | none | capture-gated |
| Sony | Live view | Wi-Fi PTP/IP | sony F-14 | missing | none | none | capture-gated |
| Sony | Image transfer, list, thumbnail | Wi-Fi PTP/IP and MTP | sony F-15 to F-17 | missing | none | none | capture-gated |
| Sony | Clock and timezone | BLE GATT | sony C-06, F-19, F-20 | missing | none | none | capture-gated |
| Sony | GPS push and pull | BLE GATT push, no pull | sony C-22, C-23 | implemented, partial: 95 bytes only, zero offset, no lock release | none | none (class) | capture-gated |
| Sony | Geotag enable and sync interval | BLE GATT | sony C-20, C-21 | implemented, partial: no `DD01` and no release | none | none (class) | capture-gated |
| Sony | Settings read | BLE GATT | sony C-14 to C-19 | implemented, partial: `DD30` and `DD31` only | none | none (class) | capture-gated |
| Sony | Settings write | BLE GATT and PTP/IP | sony C-04 to C-13 | missing | none | none | capture-gated |
| Sony | Backup and restore | Wi-Fi PTP/IP | sony F-26, F-27 | missing | none | none | capture-gated |
| Sony | Firmware update | BLE read and PTP/IP | sony F-28 | out of scope | none | none | capture-gated |
| Sony | Pairing management | BLE GATT | sony C-31, C-32 | missing, no `EE01` and no `EE03` | none | none (class) | capture-gated |
| Sony | Battery | BLE GATT | sony F-32 | missing | none | none | capture-gated |
| Sony | Storage | BLE GATT | sony F-33 | missing | none | none | capture-gated |
| Sony | Error and busy status | BLE GATT | sony F-35 | missing | none | none | capture-gated |
| Sony | Model and firmware identity | BLE GATT and advertisement | sony C-16, C-17, C-19 | missing | none | advertisement parsing only | capture-gated |
| Sony | Model-specific controls | BLE and PTP/IP | sony F-37 | missing | none | none | capture-gated |

### 3.11 Panasonic LUMIX

The MEI0 and APS generations share the control, location, and clock
characteristics. They differ in the identity block: MEI0 is a plain 16-byte write,
APS reads a per-connection key and writes a 20-byte XOR and word-swapped block.

| Camera | Feature | Transport | Spec row | Furble state | Simulator | Test | Hardware state |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Panasonic LUMIX | Still capture | BLE GATT | panasonic C09, C10, F01 | implemented, Confirm and Start then `04` and `05` | none | host `lumix-session` | capture-gated |
| Panasonic LUMIX | Half press and focus | none | panasonic F02 | unavailable, focus is a no-op | none | host `lumix-session` | not applicable |
| Panasonic LUMIX | Bulb | BLE GATT, local hold | panasonic F04 | implemented, local hold only | none | none | capture-gated |
| Panasonic LUMIX | Self-timer (still) | BLE GATT | panasonic §9 | missing | none | none (class) | capture-gated |
| Panasonic LUMIX | Self-timer (movie) | camera menu or none | panasonic F05 | unavailable | none | none | not applicable |
| Panasonic LUMIX | Interval, camera-side | BLE GATT | panasonic §9 | missing | none | none (class) | capture-gated |
| Panasonic LUMIX | Interval, local | BLE GATT repeated press | panasonic F06 | implemented | none | none | capture-gated |
| Panasonic LUMIX | Burst | BLE GATT | panasonic §9 | missing | none | none (class) | capture-gated |
| Panasonic LUMIX | Movie start and stop | BLE GATT | panasonic C11, C12 | missing | none | none | capture-gated |
| Panasonic LUMIX | Zoom | Wi-Fi live view | panasonic F09 | out of scope, BLE | none | none | capture-gated |
| Panasonic LUMIX | Power on and off | none over BLE | panasonic F10, F11 | unavailable | none | none | not applicable |
| Panasonic LUMIX | Wake | BLE advertisement, GATT, Wi-Fi | panasonic F12 | missing | none | none | capture-gated |
| Panasonic LUMIX | Sleep | BLE advertisement, camera menu | panasonic F13 | unavailable | none | none | not applicable |
| Panasonic LUMIX | Live view | Wi-Fi | panasonic F14 | missing | none | none | capture-gated |
| Panasonic LUMIX | Image transfer, list, thumbnail | BLE start, Wi-Fi DLNA | panasonic C16 to C20 | missing | none | none | capture-gated |
| Panasonic LUMIX | Clock and timezone | BLE GATT | panasonic C15 | implemented, partial: after GPS, offset and DST zero | none | none (class) | capture-gated |
| Panasonic LUMIX | GPS push and pull | BLE GATT push, no pull | panasonic C13, C14 | implemented, partial: no stop, no readiness | none | none (class) | capture-gated, author-reported S5 EXIF check |
| Panasonic LUMIX | Geotag enable and sync interval | BLE GATT readiness | panasonic C13, F45 | missing, no readiness subscription | none | none (class) | capture-gated |
| Panasonic LUMIX | Settings read | BLE GATT | panasonic C21 to C27 | missing | none | none (class) | capture-gated |
| Panasonic LUMIX | Settings write | BLE identity and name, Wi-Fi CGI | panasonic C01, C02 | implemented, partial: fixed MEI0 and plain name | none | none (class) | capture-gated |
| Panasonic LUMIX | APS identity | BLE GATT | panasonic C03 to C05 | missing, no key read and no XOR block | none | none | capture-gated |
| Panasonic LUMIX | Backup and restore | unknown | panasonic F26, F27 | out of scope | none | none | capture-gated |
| Panasonic LUMIX | Firmware update | HTTP then Wi-Fi | panasonic F28 | out of scope | none | none | capture-gated |
| Panasonic LUMIX | Pairing management | BLE identity and Wi-Fi | panasonic F29, F30 | implemented, partial: no APS and no Wi-Fi registration | none | none | capture-gated |
| Panasonic LUMIX | Battery | unknown | panasonic F32 | unavailable | none | none | not applicable |
| Panasonic LUMIX | Storage | BLE GATT on G9II | panasonic F33 | missing | none | none | capture-gated |
| Panasonic LUMIX | Error and busy status | BLE GATT notify | panasonic F35 | missing, readiness not subscribed | none | none | capture-gated |
| Panasonic LUMIX | Model and firmware identity | BLE GATT reads | panasonic C25 to C27 | missing, matcher model-blind | none | none (class) | capture-gated |
| Panasonic LUMIX | Model-specific controls | BLE GATT presence | panasonic F37, F42, F43 | missing | none | none | capture-gated |

### 3.12 DJI Osmo Action 4 and Action 5 Pro


| Camera | Feature | Transport | Spec row | Furble state | Simulator | Test | Hardware state |
| --- | --- | --- | --- | --- | --- | --- | --- |
| DJI Osmo | Still capture | BLE GATT | dji C03, F01 | missing, record toggle only | none | `advertisement_dispatch_test` handshake | capture-gated |
| DJI Osmo | Half press and focus | none | dji F02 | unavailable | none | none | not applicable |
| DJI Osmo | Bulb | none | dji F04 | unavailable | none | none | not applicable |
| DJI Osmo | Self-timer (still) | BLE status field only | dji F05 | unavailable, no setter | none | none | capture-gated |
| DJI Osmo | Self-timer (movie) | camera menu or none | dji F05 | unavailable | none | none | not applicable |
| DJI Osmo | Interval, camera-side | BLE GATT | dji C15, C18 | missing, timelapse mode not selected | none | none | capture-gated |
| DJI Osmo | Interval, local | BLE GATT repeated record | dji F06 | implemented | none | none | capture-gated |
| DJI Osmo | Burst | camera menu, status field | dji F07 | unavailable, menu-only | none | none | not applicable |
| DJI Osmo | Movie start and stop | BLE GATT | dji C16, F08 | implemented, toggle only | none | `advertisement_dispatch_test` | capture-gated |
| DJI Osmo | Zoom | Wi-Fi through Mimo | dji F09 | unavailable, BLE | none | none | capture-gated |
| DJI Osmo | Power on and off | BLE GATT | dji C13, F10, F11 | missing | none | none | capture-gated |
| DJI Osmo | Wake | BLE advertisement and GATT | dji F12 | missing | none | none | capture-gated |
| DJI Osmo | Sleep | BLE GATT | dji C13, F13 | missing | none | none | capture-gated |
| DJI Osmo | Live view | Wi-Fi, USB, or app | dji F14 | missing | none | none | capture-gated |
| DJI Osmo | Image transfer, list, thumbnail | Wi-Fi or app | dji F15 to F17 | missing | none | none | capture-gated |
| DJI Osmo | Clock and timezone | BLE GPS time, Wi-Fi SyncTime | dji F19, F20 | missing | none | none | capture-gated |
| DJI Osmo | GPS push and pull | BLE GATT push, no R SDK pull | dji C07 | missing, deliberate no-op | none | none | capture-gated |
| DJI Osmo | Geotag enable and sync interval | Wi-Fi through Mimo | dji F23 | missing | none | none | capture-gated |
| DJI Osmo | Settings read | BLE GATT | dji C15, C01 | implemented, partial: mode, status, record time | none | none | capture-gated |
| DJI Osmo | Settings write | BLE GATT | dji C18, F25 | missing | none | none | capture-gated |
| DJI Osmo | Backup and restore | none documented | dji F26, F27 | unavailable | none | none | not applicable |
| DJI Osmo | Firmware update | Wi-Fi OTA and Mimo BLE DM OTA | dji F28 | out of scope | none | none | capture-gated |
| DJI Osmo | Pairing management | BLE GATT | dji C09 to C12 | implemented | none | `advertisement_dispatch_test` | capture-gated |
| DJI Osmo | Battery | BLE status field | dji C15 | missing | none | none | capture-gated |
| DJI Osmo | Storage | BLE status fields | dji C15 | missing | none | none | capture-gated |
| DJI Osmo | Error and busy status | BLE status and return codes | dji F35 | implemented, partial: logged only | none | none | capture-gated |
| DJI Osmo | Model and firmware identity | BLE device ID and C01 | dji F36 | implemented, partial: device ID gate only | none | `advertisement_dispatch_test` | capture-gated |
| DJI Osmo | Model-specific controls | BLE mode switch | dji C18, F38 to F60 | missing | none | none | capture-gated |

### 3.13 FauxNY software peer

FauxNY has no camera transport. Its connect is a 2.5 s delay that sets
`m_Connected`, and every vendor action is a log line. It is excluded from every
camera-parity claim.

| Camera | Feature | Transport | Spec row | Furble state | Simulator | Test | Hardware state |
| --- | --- | --- | --- | --- | --- | --- | --- |
| FauxNY | Still, focus, bulb, interval | none, log only | none | out of scope, software peer | `FauxNY` | `control_e2e`, UI suites | not applicable |
| FauxNY | GPS, clock, transfer, live view, settings | none, log only | none | out of scope, software peer | `FauxNY` geo record | GPS and UI suites | not applicable |
| FauxNY | Power, wake, sleep, pairing, bond, battery, storage, errors | none, log only | none | out of scope, software peer | `FauxNY` | lifecycle suites | not applicable |

## 4. Contradictions and stale claims

These are the claims in the private corpus or in the Furble research notes that
do not match the execution pin. Each one must be corrected in the same PR that
touches the family, or the matrix row above is wrong.

| Claim | Where | What the code does at this pin | Disposition | Evidence |
| --- | --- | --- | --- | --- |
| Furble sends `03 00` for Fujifilm focus | fujifilm.md C14 and §9; state-machines §2.3, §3.3, and unknown 2; feature-gap-matrix §2.3.1 | Writes `01 00` for focus press and `00 00` for release. Commit `fdacb87e` removed `03 00` because the firmware parser returns -30 | Correct the spec and diagram to the two-value half press. The code is aligned | `firmware-static` return value; `furble-source` |
| Canon Smart focus is a no-op | canon.md F02; feature-gap-matrix §2.5 | Writes `00 03` AF_ON and `00 04` AF_OFF since `4607330d` | Correct the spec. Keep the outcome capture-gated | `app-static` canon.md CS-18, CS-19; `furble-source` |
| LUMIX clock follows the G9II public layout | panasonic.md C15 last sentence | Writes the LUMIX Sync layout: int16 standard offset at 7 to 8, DST at 9. `5a7b73ba` aligned it to the app | Correct the comparison sentence | `app-static`; `furble-source` |
| LUMIX writes a zero byte at 7 and an offset at 8 to 9 | panasonic.md F20 | Writes a zero offset at 7 to 8 and a zero DST at 9. The bytes are identical because both fields are zero | Correct the field description | `app-static`; `furble-source` |
| Furble accepts the rotated DJI device IDs and rejects the table values | dji.md U01 | Accepts `0x33FF0000`, `0x44FF0000`, `0xFF330000`, and `0xFF440000` since `c2151d46` | Correct U01. The wire byte order is still unknown | `furble-source`; `public-source` |
| LUMIX `altitude` is uint16 and the clock has a separate zero byte | state-machines §11.4 | `altitude` is `int16_t`; the clock has `tz_offset` at 7 to 8 and `dst` at 9 | Correct the diagram after `5a7b73ba` | `furble-source` |
| Fujifilm focus writes `03 00` in the traceability note | feature-gap-matrix §2.1 and §2.3.1 | Same as the first row | Correct the note | `furble-source` |
| Ricoh capture is GR IV only | ricoh.md F-01; `Ricoh.h` class comment | `Ricoh::matches` accepts GR III, GR IIIx, HDF, GR IV, and broad PENTAX names, and one class drives every match | Decide the product claim. Either narrow the matcher or document GR III as experimental | `furble-source`; `app-static` |
| LUMIX company ID is `0x0376` | pairing-security-flows §11.8 cross-reference | Code and the official app use `0x003A` | Correct the cross-reference | `app-static`; `furble-source` |

Claims deliberately not adopted from the corpus, because the evidence label is
`inference` or `unknown` and adopting them would invent support:

- Sony 91-byte GPS variant selection.
- Canon Smart Write Request for control writes.
- Canon Smart identity ordering.
- LUMIX per-install MEI0 seed and APS identity.
- Ricoh GR III Drive Mode omission.
- DJI device ID byte order, SMP requirement, snapshot key, wake, and sleep.
- Nikon Smart `2021` key events.

## 5. Coverage accounting

### 5.1 Table 1

| Verdict | Rows |
| --- | ---: |
| aligned | 65 |
| safe-fix-needed | 22 |
| capture-gated | 65 |
| unsupported | 12 |
| out-of-scope | 12 |
| Total | 176 |

| Class | Rows |
| --- | ---: |
| Discovery, matching, and link | 60 |
| Capture | 34 |
| Power, wake, GPS, and clock | 34 |
| Data, settings, and maintenance | 30 |
| Control plane | 18 |
| Total | 176 |

### 5.2 Table 2

| Furble state | Rows |
| --- | ---: |
| implemented | 77 |
| missing | 122 |
| capture-gated | 3 |
| unavailable | 110 |
| out of scope | 28 |
| Total | 340 |

| Hardware state | Rows |
| --- | ---: |
| verified | 8 |
| capture-gated | 236 |
| not applicable | 96 |
| Total | 340 |

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
| B1 LUMIX session | Remove the focus mapping; add Confirm, Start, Finish, readiness, GPS stop, clock gating | `lib/furble/Lumix.{h,cpp}` | New LUMIX host test with the new peer |
| B2 Fujifilm clock and identity | 12-byte clock, NUL name, `AAB609C4` enable, disconnect reason | `Fujifilm.cpp`, `FujifilmSecure.cpp`, `FujifilmProtocol.*` | `fujifilm_camera_test`, `fujifilm_protocol_test` |
| B3 Canon GPS | UTC epoch, `00040001`, `05`, `03`, `06` | `CanonEOSSmart.cpp` | New `canon_gps_test` |
| B4 Sony GPS | `DD01`, `DD21`, `DD32`, `DD33`, timezone, release | `Sony.cpp` | New `sony_gps_test` |
| B5 Ricoh clock and identity | Date Time gated on CAPTURE, Paired Device Name, capture status | `Ricoh.{h,cpp}` | Extend `ricoh_focus_test` |
| B6 UI capability gates | Hide bulb and focus on Ricoh, focus on LUMIX, label Nikon Remote release | `src/FurbleUI.cpp`, docs | `headless_ui_contract_test`, screenshots |
| B7 Nikon Smart fail-fast | Named Classic blocker, remove final-OK wait | `NikonSmart.cpp` | `camera_regression_test` |
| B8 Control-plane errors | Surface failed writes; make `gattRead` distinguish failure | `Camera.{h,cpp}`, vendor classes | New `camera_write_error_test` |

### 6.2 Simulator batches

Required peers, in dependency order:

1. `CanonSmartVirtualCamera` and `CanonRemoteVirtualCamera`: product-ID gates,
   `00010006`, `0001000a`, `00030010`, `00030030`, `00040000`, `00050000`,
   pairing reject, stale bond, timeout, malformed indication.
2. `NikonRemoteVirtualCamera`: `DE00`, `2087` four stages, `2083` short and
   long press, `2084`, stage timeout.
3. `NikonSmartVirtualCamera`: `2000` Blowfish, eight salts, final-OK
   distinction, Classic-blocked mode that must fail cleanly.
4. `SonyVirtualCamera`: advertisement bits, `EE01`, `EE03`, `FF01`, `FF02`,
   `DD01`, `DD11`, `DD21`, `DD30` to `DD33`, `CC02`, `CC13`.
5. `LumixVirtualCamera`: MEI0 and APS, Confirm and Start readiness,
   `7be5fe6e`, `37701b80`, `4cf487c0`, capture, movie, GPS and clock, GPS stop.
6. `DjiVirtualCamera`: `FFF0`, `FFF4`, `FFF5`, `00 19` both device-ID encodings,
   verify mode 2, status subscription, record toggle, CRC faults, wake, and an
   SMP-required and an SMP-free mode.

Each peer needs the six failure classes: success, camera-side rejection, timeout,
disconnect, stale bond, and malformed response. Add one manifest entry per peer
per suite and run `python3 tools/check_sim_scenarios.py`.

### 6.3 Test batches

- One host test per peer feature path, not per byte.
- A regression test for every safe fix in §1.2.
- A negative test for every capture gate: refuse or label, never send silently.
- Extend `tests/host/control_e2e` with a multi-vendor session once two peers exist.

### 6.4 Capture batches

Run the captures in the order in §1.3. Each capture must record the body,
firmware, controller, raw HCI or sniffer trace, camera settings, and physical
outcome. A capture without the physical outcome closes only the wire half.

### 6.5 Documentation batches

- Correct the stale claims in §4 in the same PR as the code change.
- Add per-camera "not implemented over BLE" lists to `README.md` and
  `docs/supported-hardware.md`.
- Add the LUMIX and DJI rows.
- Keep every capture-gated feature labelled in the UI and the usage guide.
- Regenerate `docs/ui-walkthrough.md` after the capability gates.

## 7. Sources read

Furble at `fa9c9581`:

- `GOAL.md` in `MaxRink/rea-camera-firmware/docs/`, Workstreams D and E.
- `lib/furble/Camera.{h,cpp}`, `CameraList.cpp`, `Scan.cpp`.
- `lib/furble/Fujifilm*.{h,cpp}`, `lib/furble/protocol/FujifilmProtocol.{h,cpp}`.
- `lib/furble/Ricoh.{h,cpp}`, `CanonEOS*.{h,cpp}`, `Nikon*.{h,cpp}`.
- `lib/furble/Sony.{h,cpp}`, `Lumix.{h,cpp}`, `DJIOsmo.{h,cpp}`, `FauxNY.{h,cpp}`.
- `lib/furble/protocol/AdvertisementProtocol.{h,cpp}`.
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
