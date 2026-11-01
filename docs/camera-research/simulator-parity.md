# Simulator parity: common peer contract, family state models, and fault matrix

Status: design plus first implementation. Applies to GOAL Workstream E.
Scope: every camera family Furble registers, the two existing virtual peers, and
the seven peers Workstream E still requires.

This document defines what a virtual camera peer must model, how each family's
broader state machine maps onto the peer surface, which faults every peer must
inject, and which host and simulator scenarios prove the result. It also records
the handler-level refinements that were applied to
`lib/testing/peer/FujifilmVirtualCamera.*` and
`lib/testing/peer/RicohVirtualCamera.*`, and the ones that remain capture-gated.

Two rules govern every claim here:

1. A peer models only what a spec row, a handler note, or a capture establishes.
   Anything else is a refused path or an explicit capture gate.
2. A successful ATT write is not a physical outcome. Peer success proves wire
   compatibility, not that the camera acted. See
   `docs/camera-research/README.md` and
   `plans/159-camera-peer-certification.md`.

## 0. Source key and evidence labels

Evidence labels follow `docs/diagrams/state-machines.md` section 0.1:
`firmware-static`, `app-static`, `hardware-capture`, `public-source`,
`official-semantic`, `inference`, `unknown`, plus `furble-source` for this tree.

| Key | Path | What it settles |
| --- | --- | --- |
| `fuji-spec` | private corpus `spec/fujifilm.md` | Advertisement, GATT, commands, notifications, registration, timing |
| `fuji-handlers` | private corpus `analysis/fujifilm-handlers.md` | Which function parses which handle, accepted values, step gates |
| `ricoh-spec` | private corpus `spec/ricoh.md` | Advertisement, GATT, commands, SMP matrix, timing, feature table |
| `ricoh-handlers` | private corpus `analysis/ricoh-handlers.md` | Characteristic classes, accepted values, drive table, GPS parser, pairing policy |
| `nikon-spec` | private corpus `spec/nikon.md` | DE00 layout, Remote and Smart commands, crypto, timing |
| `nikon-handlers` | private corpus `analysis/nikon-b600-handlers.md` | `2083` key gate, `2084` payload length, `2087`/`2000` stage handling |
| `canon-spec` | private corpus `spec/canon.md` | Advertisement forms, services, commands, security |
| `sony-spec` | private corpus `spec/sony.md` | Manufacturer record, services, commands |
| `lumix-spec` | private corpus `spec/panasonic.md` | Advertisement form, session service, commands |
| `dji-spec` | private corpus `spec/dji.md` | Advertisement forms, wake advertisement, commands |
| `sm` | `docs/diagrams/state-machines.md` | Furble-side state machines, timeouts, and known unknowns |
| `fuji-gatt` | `docs/camera-research/fujifilm-secure-gatt.md` | 70-entry UUID table and handle layout |
| `ricoh-notes` | `docs/camera-research/ricoh.md` | Generation split and GPS/datum history |
| `bt-surface` | `docs/camera-research/bt-api-surface.md` | Per-family service and command index |
| `plan159` | `plans/159-camera-peer-certification.md` | Evidence classes, profile identity, certification verdicts |

## 1. Common peer contract

Every peer sits below production `Control`, `Camera`, `CameraList`, and `Scan`.
It must not replace production policy (`plan159` section "Simulator
architecture"). A peer implements `NimBLEMockPeer` from
`lib/testing/nimble/MockNimBLE.h` and answers, at minimum:
`acceptConnection`, `disconnect`, `hasService`, `hasCharacteristic`,
`discoverCharacteristic`, `canWrite`, `write`, `read`, `subscribe`,
`secureConnection`, `updateConnectionParams`, `getRssi`.

### 1.1 Advertisement

| Requirement | Contract | Evidence |
| --- | --- | --- |
| Exact form | Every family publishes each advertisement form it really uses, as a distinct `Config` or state. A peer must not invent a merged "universal" form. | `plan159` "Simulator architecture"; per-family rows below |
| Model gate | Matching rules are the production parser's, not the peer's. The peer supplies bytes; `Scan` and the family matcher decide. | `sm` section 1.2; `lib/furble/protocol/AdvertisementProtocol.cpp` |
| Address | Public, static random, and resolvable private forms are separate configurations. A saved reconnect test must be able to present a different address for the same identity. | `fuji-spec` §2.2; `ricoh-spec` §2; `sm` §3.1 |
| Rotation | A registered Fujifilm Secure body switches advertised service from `A9D2B304` to `123D8F06`. A Ricoh body switches from Camera Information to Bluetooth Control in sleep. Both are camera state, not a token counter. | `fuji-handlers` §3.1; `ricoh-handlers` §10.1 |
| Manufacturer data | Company ID, form byte, and typed fields are reproduced byte for byte. Truncation is a fault, not a convenience. | `fuji-spec` §2.1; `ricoh-spec` §2; `nikon-spec` §2.2; `canon-spec` §2.1; `sony-spec` §2.1; `lumix-spec` §2; `dji-spec` §2 |
| Name | Local name is the camera's own rule. Fujifilm Secure advertises the bare model before registration; Ricoh uses `GR_` plus six hex digits. A peer must expose both the bare and augmented forms so the name tests can choose. | `fuji-spec` §2.3; `ricoh-spec` §2 notes; `fuji-handlers` §7 |
| Duration | Advertising stops on connect and, for Fujifilm Secure, stops about 30 s after a link loss. A peer exposes `startAdvertising()` and `stopAdvertising()` rather than advertising forever. | `fuji-spec` §2.4; `ricoh-handlers` §10.3; `sm` §3.1 |

Implemented today: `FujifilmVirtualCamera::advertisement()` reproduces the
Basic `D8 04 02` token form and the Secure `D8 04 01` serial form.
`RicohVirtualCamera::advertisement()` now reproduces the `5F 06 DA` typed
record and advertises one service only. Neither peer schedules advertising
windows yet; both advertise whenever asked. That is a contract gap, not a
camera behaviour.

### 1.2 GATT

| Requirement | Contract | Evidence |
| --- | --- | --- |
| Services | The peer answers exactly the services its profile declares, including standard GAP, GATT, and Device Information where the family has them. An absent service returns null, never an empty object. | `fuji-spec` §3; `ricoh-spec` §3; `nikon-spec` §3.1; `canon-spec` §3; `sony-spec` §3; `lumix-spec` §3; `dji-spec` §3 |
| Characteristics | Handles, UUIDs, properties, maximum lengths, and CCCD presence come from the table, not from a convenience list. Read-only must not be writable. | `fuji-handlers` §2.5; `ricoh-handlers` §3 and §4; `nikon-spec` §4 |
| Write type | Write with response and write without response are distinct. A characteristic with only `0x08` refuses a no-response write. | `ricoh-handlers` §2 (no characteristic is write-without-response); `nikon-spec` §4 preamble |
| Length gate | Exact-length gates are enforced. Fujifilm shutter is exactly 2 bytes; Ricoh GPS is 32; Nikon `2083` is 2 and `2087` is 17. | `fuji-handlers` §4; `ricoh-handlers` §6; `nikon-handlers` §4.1 and §4.3 |
| Read values | Reads return the profile's current value, including failure substitutes. Ricoh `CameraPower` substitutes `0xFF` on property failure; `GEOTag` substitutes `1`. | `ricoh-handlers` §5.1 and §5.5 |
| Discovery order | The peer must tolerate the production discovery order and must not require a private one. | `sm` §3.1 registration sequence |

Implemented today: both peers enforce their current service and characteristic
sets, and the Ricoh peer now refuses writes to characteristics that the image
declares read-only. The Fujifilm peer still answers `discoverCharacteristic()`
with `true` unconditionally; the production client resolves the rest, so this
is a known simplification, not a modelled camera rule.

### 1.3 Security and SMP

| Requirement | Contract | Evidence |
| --- | --- | --- |
| Method | The peer declares its association model and refuses a bond that did not use it. Fujifilm Secure uses LE Secure Connections with MITM and numeric comparison or passkey; Ricoh GR III/IIIx use passkey display, GR IV/HDF numeric comparison; Nikon Remote has no LE bond in the recovered path; Nikon Smart has no LE bond. | `fuji-spec` §7.8; `ricoh-spec` §7.1; `ricoh-handlers` §9.1 and §9.2; `nikon-spec` §7.8 |
| IO capability | The host-side IO capability is production policy. The peer records what it was asked for and fails when the capability cannot satisfy the association model. | `sm` §1.2; `fuji-spec` §7.2 step 4 |
| Bond state | The peer keeps a camera-side bond store separate from `NimBLEDevice`'s central store. A mismatch in either direction is a distinct fault. | `plan159` "Simulator architecture"; `ricoh-handlers` §9.3 |
| Stale bond | A camera that deleted its key fails every protected operation, and may fail the encryption handshake before the first read. Both shapes exist. | `sm` §3.6; `fuji-spec` §7.5; `ricoh-spec` §7.5 |
| Camera-side deletion | The camera menu deletes a bond without a GATT surface. The peer must expose `removeCameraBond()` and must not require a protocol message. | `ricoh-handlers` §11; `ricoh-spec` §7.7 |
| Re-pair while bonded | Ricoh denies, deletes its entry, and disconnects when a paired device asks to pair again. | `ricoh-handlers` §9.4 |
| Failure shape | A refusal that keeps the link and a timeout that takes the link are different faults. | `sm` §3.6 (rc 13 versus rc 520); `fuji-spec` §8 |

Implemented today: both peers keep a camera-side bond flag and a central bond
check. `RicohVirtualCamera` now separates the passkey path from the numeric
comparison path and can model a repair request. Fujifilm still models its
refusal and timeout shapes through `setSecureConnectionResult`,
`setSecureTimeouts`, `setSecureConnectionDropsLink`, and
`setRefuseWhileBonded`.

### 1.4 Registration

| Family | Registration contract | Evidence |
| --- | --- | --- |
| Fujifilm Basic | Four-byte token echo, then client name, then subscriptions, then wait for NOT1 `01 00` or `02 00`. No SMP bond. | `fuji-spec` §7.6; `sm` §2.1 |
| Fujifilm Secure | Status read of `F557D96B`, four-byte ack with byte 3 `0x20`, client name on `85B9163E`, two acknowledged indications and four notifications, then NOT1 or a geotag request, then `C95D91AE = 0A 00`, then shutter discovery and the FAST profile. | `fuji-spec` §7.1 and §7.2; `fuji-handlers` §3 |
| Ricoh | SMP bond, then model read, then pointer caching. There is no application registration handshake. `PairedDeviceName` is optional and write-only. | `ricoh-handlers` §3.1 and §5.15; `sm` §4.1 |
| Nikon Remote | Indication on `2084`, stage 1 on `2087`, stage 2 indication, stage 3, stage 4 indication. Stage 2 has no validation beyond the stage byte in the camera handler. | `nikon-handlers` §4.1; `nikon-spec` §5.1 |
| Nikon Smart | Four-stage LsSec/Blowfish on `2000`, then name on `2002`, then time and location on `2006`/`2007`. No final `01 00` on `2008` exists in the camera flow. | `nikon-handlers` §5.1 to §5.4; `nikon-spec` §5.2 and §7.8 |
| Canon Smart and Remote | Role-specific application pairing exchange, described by advertisement form and command rows. | `canon-spec` §2.1 and §7 |
| Sony | Generation-specific login sequence. | `sony-spec` §7 |
| LUMIX | Session service then command plane selection. | `lumix-spec` §7 |
| DJI | Mimo credential exchange; the BLE control path is not a bond. | `dji-spec` §7 |

A peer must expose the registration result as state, not only as a log line:
`configured()`, `tokenAccepted()`, `statusAcknowledged()`, `clientClass()`,
`partnerRegistered()`. Tests assert on the peer state and on the host state,
never on a sleep.

### 1.5 Command handling

Every command row in the family sheet becomes one of three peer behaviours:

| Behaviour | Meaning | Example |
| --- | --- | --- |
| Accept | Enforce length, value, and state gate, then return true. | Fujifilm shutter `01 00` (`fuji-handlers` §4) |
| Refuse | Enforce length or value and return the camera's failure, without touching state. | Ricoh `CameraPower` value `2` (`ricoh-handlers` §5.1) |
| Capture-gated | The camera surface exists but the effect is unknown, so the peer accepts the write and exposes it as unverified, or refuses it if the sheet says the parser rejects it. | Nikon `2008` clear control point (`nikon-handlers` §0 item 7) |

A peer must record every accepted write with service, characteristic, payload,
and response flag. Rejected writes are counted separately where a test needs to
prove a refusal.

### 1.6 Notifications and indications

| Requirement | Contract | Evidence |
| --- | --- | --- |
| Subscription store | `subscribe()` retains the client, remote characteristic, callback, and notification/indication flag, and fails when there is no live link. | `lib/testing/nimble/MockNimBLE.h`; both peers today |
| Payload length | The peer emits the declared length, including the family's exact byte count. Nikon `2084` is six bytes. | `nikon-handlers` §4.2 |
| Trigger | A notification is triggered by a modelled camera event, never by a test-only call with no state change. `requestGeotag()` is the one explicit exception because the camera request is otherwise unpredictable. | `fuji-spec` §6; `sm` §3.4 |
| Scheduling | Certified peers schedule notifications off the write path. Plan 159 forbids synchronous notification from `write()`. | `plan159` "Simulator architecture" |
| Indication confirmation | An indication subscription is acknowledged where the camera requires it. Fujifilm requires the two configuration CCCD writes with response. | `fuji-spec` §6.1 and §7.2 step 8 |
| Stale callback | A callback retained from a previous connection must not confirm the current session. | `fuji-spec` §7; host test `fujifilm_registration_gate_test.cpp` |

Implemented today: both peers retain subscriptions and can emit through the
stored callback. Fujifilm emits NOT1 at subscribe and can replay a stale
callback. Both violate the scheduling rule; the migration is in section 7.

### 1.7 Errors

| Error class | Peer contract | Evidence |
| --- | --- | --- |
| ATT write refused | Return false from `write()` so the production wrapper sees a failed operation. Ricoh's application error `0x80` is the only application-level status. | `ricoh-handlers` §2.1 |
| ATT read refused | Return an empty `NimBLEAttValue`; do not return a fabricated success value. | `ricoh-handlers` §2.1 read path |
| Service missing | `hasService()` and `getService()` return null. | `fuji-spec` §7.2 error list; `sm` §3.6 |
| Characteristic missing | `hasCharacteristic()` false and `getCharacteristic()` null. | `sm` §3.6 |
| Busy | The camera accepts the link and the write but refuses the operation, or holds the operation until the state clears. The peer exposes both shapes. | `ricoh-handlers` §8.5; `fuji-spec` §5 C17 |
| Wrong mode | The camera refuses capture based on its own mode. Ricoh exposes the mode byte and the host refuses; Fujifilm has no mode read and the camera decides. | `sm` §4.2 and §5.2; `fuji-spec` §5.4 |
| Malformed | A frame with the wrong length or an out-of-range field is refused at the parser, not silently truncated. | `fuji-handlers` §4 and §5; `ricoh-handlers` §5; `nikon-handlers` §4.3 |
| Timeout | The peer holds an operation past the host bound without answering, on the peer clock. | `sm` §1.7; `PeerStall.h` |
| Disconnect | The peer severs the link and optionally delivers `onDisconnect` inline or deferred. Both are distinct faults. | `lib/testing/nimble/MockNimBLE.h` (`mockDropLink`, `mockDropLinkSelfDelete`) |

### 1.8 Timing

| Timer | Value | Owner | Evidence |
| --- | --- | --- | --- |
| Fujifilm status ack window | 20 000 ms from the status read | Camera | `fuji-handlers` §3.2 |
| Fujifilm registration wait | 25 000 ms, 20 ms poll | Host | `sm` §3.1 |
| Fujifilm advertising after power-on | About 6 to 10 s | Camera | `fuji-spec` §8 |
| Fujifilm advertising after link loss | About 30 s | Camera | `fuji-spec` §8 |
| Fujifilm stale-bond handshake | About 30 s (rc 13) or about 5 s (rc 520) | Camera | `fuji-spec` §8 |
| Fujifilm registration lockout after a bad fresh bond | About 4.5 minutes | Camera | `fuji-spec` §8 |
| Ricoh pairing ceremony | Abandons after about 60 s on GR IIIx 1.60 | Camera | `ricoh-spec` §8 |
| Ricoh reconnect cadence | 8 s safe, 2 s latches BLE_STARTUP and fails with `0x216` | Camera | `ricoh-spec` §8 |
| Ricoh WLAN AP readiness | 500 ms on GR III 2.00, about 2 s on GR IIIx 1.50 | Camera | `ricoh-spec` §8 |
| Ricoh capture settle | 1 s, 3 s for continuous | App | `ricoh-spec` §8 |
| Nikon stage response | 10 s per stage, 250 ms slices | Host | `nikon-spec` §8 |
| Nikon Smart final OK | 1 s, and no known body sends one | Host | `nikon-spec` §8 |
| Host connect timeout | 30 s normal, 5 s infinite reconnect | Host | `sm` §1.7 |
| Host drain reclaim | 2 s | Host | `sm` §1.7 |

Rules:

1. Camera-side timers run on the peer clock. The host harness uses wall time;
   the simulator installs `Host::setPeerStallFunction()` and runs virtual time
   (`PeerStall.h`).
2. A peer must never encode a host timeout as its own behaviour. The Fujifilm
   20 s window and the Ricoh 60 s pairing ceremony are camera bounds; the 25 s
   registration wait and the 30 s connect timeout are host bounds.
3. A timer that has not been measured is `unknown`, and the peer must not
   invent one. Ricoh firmware update timeouts are `unknown` (`ricoh-spec` §8).

### 1.9 Fault injection

Every peer exposes the same eight fault classes with family-specific triggers
(section 3):

| Class | API shape | Semantics |
| --- | --- | --- |
| Reject | `failWrite(service, characteristic)` | ATT write returns an error; link stays up |
| Busy | `setBusy(bool)` | The command is refused or held while the link stays up |
| Wrong mode | `setOperationMode()` or `setCameraMode()` | The camera state that gates capture |
| Timeout | `setSecureTimeouts(n)` / scheduled stall | No answer inside the host bound |
| Disconnect | `dropLinkOnWrite`, `dropLinkDuringConnect`, `mockDropLink` | Link loss, inline or deferred |
| Malformed | `setMalformedResponse(...)` / truncated read or notification | Wrong length or out-of-range payload |
| Stale bond | `setBondMismatch()` / camera bond cleared, central bond kept | Protected op or handshake fails |
| Camera-side deletion | `removeCameraBond()` | Camera forgot the bond with no GATT message |

`clearFaults()` restores a healthy baseline so the fuzz harness can reuse one
peer across operations (already present on `FujifilmVirtualCamera`; added to
`RicohVirtualCamera`).

### 1.10 Scripted mode

The deterministic scripted mode is a sequence of peer events with no host
threads:

| Step kind | Fields | Rule |
| --- | --- | --- |
| `advertise` | form, address, service set | Emitted through `Scan` at the scenario's scan tick |
| `accept` | address | Only one link per peer |
| `expect_write` | service, characteristic, payload, max wait | The scenario fails if the write does not arrive |
| `emit` | service, characteristic, payload, indication | Runs on the peer clock |
| `drop` | reason, callback mode | `inline`, `self_delete`, or `deferred` |
| `expire` | timer name | Advances the peer clock to the camera deadline |
| `assert` | peer state, host state | Checked at the scenario end |

A scenario file names the family profile, the fault set, and the expected host
verdict. No scenario may assert a physical outcome; certification of a still
image still needs a capture or EXIF check (`plan159`; `fuji-spec` §0.4).

## 2. Per-family state model

The peer models a subset of the camera's full state machine: the states that
change a GATT answer, a notification, or the link. A state the sheet marks
unknown is modelled as a refused command, not as an invented success.

### 2.1 Fujifilm Basic

| State | Peer-visible model | Evidence |
| --- | --- | --- |
| Boot | Advertising `D8 04 02` plus token on `AF854C2E` or `117C4142`. | `fuji-spec` §2.1 |
| Idle | Link up, token and identity written, subscriptions live, no mode read exists. | `fuji-spec` §7.6; `sm` §2.1 |
| Capture | `01 00` then `02 00`; `01 00` then `00 00` for release. Camera mode decides the physical outcome. | `fuji-spec` §5.1; `sm` §2.3 |
| Playback | No mode read or write. A shutter write is accepted at ATT; effect unknown. | `sm` §2.2 |
| Movie | No Basic movie command; `861442AB` is a Secure-profile row and is not proven on Basic. | `fuji-spec` §5 C15; `sm` §2.3 |
| Sleep | No Basic sleep surface. | `sm` §2.2 |
| Wake | A Basic body is reached by its advertisement only; no wake command. | `fuji-spec` §2.5 |
| Transfer | No Basic transfer path. `AF854C2E` is also the Secure backup service, but Basic does not use it. | `sm` §2.5 |
| Firmware update | No Basic path. | `sm` §2.5 |

### 2.2 Fujifilm Secure v4.0 and v4.1

| State | Peer-visible model | Evidence |
| --- | --- | --- |
| Boot | Advertising `D8 04 01` plus five serial bytes on `A9D2B304` (unregistered) or `123D8F06` (registered). | `fuji-spec` §2.2; `fuji-handlers` §3.1 |
| Idle | Encrypted bonded link, registration complete, sync interval written, shutter discovered. | `fuji-spec` §7.1; `sm` §3.1 |
| Capture | `01 00` then `02 00`, or `00 00` for release. Only 0, 1, 2 are legal. | `fuji-handlers` §4 |
| Playback | The mode is not readable over BLE. The WiFi trigger `600655E6 = 04 00` is the only command with a captured playback success, and IND1 stayed busy in shooting mode. | `fuji-spec` §5 C17; `sm` §3.2 |
| Movie | `861442AB` carries a movie record request; the stop value is undocumented. The 92-byte `B1307521` is not a movie trigger. | `fuji-spec` §5 C15; `fuji-handlers` §9.4 |
| Sleep | `43070F6C = 01 00` powers off; the camera accepts no sleep value. Power key state on `F90F7D3A` reports the transition. | `fuji-handlers` §9.1 and §9.3; `fuji-spec` §5.4 |
| Wake | `43070F6C = 00 01` is a normal boot or wake and requires the remote-boot setting. `7170FD5A = 01 00` enables that setting. | `fuji-handlers` §9.1 and §9.2 |
| Transfer | `0x9001` backup/file service, `98934B2C` transfer setting ex, `BD17BA04` transfer state. No image bytes over GATT. | `fuji-spec` §5 C34 to C48; `sm` §3.5 |
| Firmware update | `049EC406` is a firmware-update state characteristic; `B1307521` carries the update request blob. No payload parser is reachable from the write dispatcher. | `fuji-spec` §5 C53 and C54; `fuji-handlers` §9.7 |

### 2.3 Ricoh GR III and GR IIIx

| State | Peer-visible model | Evidence |
| --- | --- | --- |
| Boot | Advertising the Camera Information service with the `5F 06 DA` record. | `ricoh-handlers` §10.1 and §10.2 |
| Idle | Encrypted, authenticated, bonded. `OperationMode` reads `0x00` CAPTURE. Model read replaces the advertised name. | `ricoh-handlers` §3.1; `sm` §4.1 |
| Capture | Drive Mode `00`, then Operation Request `{01 01}`. Stop is `{00 01}`; `{01 02}` is refused. | `ricoh-handlers` §8.1; `ricoh-spec` §5.1 |
| Playback | `OperationMode` reads `0x01`. Furble refuses capture in this mode. | `sm` §4.2; `ricoh-spec` §9 F-01 |
| Movie | No movie opcode; movie uses the same release path when the camera is already in movie capture mode. Movie status is Capture Status byte 3. | `ricoh-handlers` §8.4 and §5.10 |
| Sleep | Passkey bond persists; `CameraPower` write `2` is refused, so only the camera menu or idle timer can sleep. | `ricoh-handlers` §5.1 and §11 |
| Wake | A GATT connect wakes a GR III or IIIx advertising while off. The advertisement stays on the Camera Information service. | `ricoh-handlers` §11; `ricoh-spec` §9 F-12 |
| Transfer | `FileTransferList` and `PowerOffDuringFileTransfer` are BLE control only; bytes transfer over Wi-Fi HTTP. | `ricoh-handlers` §11; `ricoh-spec` §9 F-14 to F-17 |
| Firmware update | GR III has no app update flow; update is SD card only. | `ricoh-spec` §9 F-28 |

### 2.4 Ricoh GR IV and GR IV HDF

| State | Peer-visible model | Evidence |
| --- | --- | --- |
| Boot | Advertising with the sleep form `0F291746` when in BLE standby. | `ricoh-handlers` §10.1 |
| Idle | `CameraPower` reads `01`, `OperationMode` reads `00` CAPTURE. | `sm` §5.1 |
| Capture | Same two writes as GR III. GR IV is the body those writes were verified on. | `ricoh-spec` §9 F-01; `sm` §5.3 |
| Playback | `OperationMode` `0x01`. Capture refused by host policy. | `sm` §5.2 |
| Movie | Capture Mode `2`, Movie Configuration, then the same `{01 01}` release; stop encoding unknown. | `ricoh-spec` §9 F-08 |
| Sleep | BLE standby reports `CameraPower 01` with `OperationMode 02`. Capture in that state can wedge firmware. | `sm` §5.1 and §5.2 |
| Wake | A connect plus camera-side `Connection While Power Off` wakes the body; `CameraPower` write `1` is accepted but emits no command. | `ricoh-handlers` §5.1 and §11; `ricoh-spec` §9 F-12 |
| Transfer | Same BLE control surface plus Wi-Fi HTTP. | `ricoh-handlers` §11 |
| Firmware update | `FirmwareUpdateStart` accepts one byte below 2; `FirmwareUpdateResult` reports the result. The image itself is not carried over BLE. | `ricoh-handlers` §5.16; `ricoh-spec` §9 F-28 |

### 2.5 Nikon Remote

| State | Peer-visible model | Evidence |
| --- | --- | --- |
| Boot | Advertisement with no manufacturer data plus DE00, or the seven-byte reconnect form `99 03 <client_id[4]> <capability>`. | `nikon-spec` §2.1 and §2.2; `nikon-handlers` §6 |
| Idle | Paired; `2083` accepts key events. | `nikon-handlers` §4.3 |
| Capture | `{02 02}` short press, `{02 03}` long press. `{02 00}` is a no-op, not a release. | `nikon-handlers` §4.4; `nikon-spec` §5.1 |
| Playback | Camera mode `0x05` exists as a constant; no command is proven to work in it. | `sm` §8.2 |
| Movie | Camera mode `0x03` exists; no movie command is proven. | `sm` §8.2; `nikon-spec` §5.1 |
| Sleep | No power or sleep characteristic in the Remote profile. | `nikon-spec` §4.1; `sm` §8.5 |
| Wake | No wake surface; the physical remote has none. | `sm` §8.5 |
| Transfer | No image transfer over BLE. | `nikon-spec` §9.1 |
| Firmware update | No BLE update path in the Remote profile. | `nikon-spec` §9.1 |

### 2.6 Nikon Smart

| State | Peer-visible model | Evidence |
| --- | --- | --- |
| Boot | Advertisement with the client ID in manufacturer data. | `nikon-spec` §2.1 |
| Idle | Not reachable in Furble: the flow needs a Bluetooth Classic continuation. | `sm` §9.1 |
| Capture | `2021` opcode `0x12` key events exist in SnapBridge, not in the Remote profile. | `nikon-spec` §5.2 |
| Playback | `2020` main status `PLAY` exists. | `nikon-spec` §5.2 S22 |
| Movie | `2020` shooting sub-status `MOVIE_SHOOTING` exists. | `nikon-spec` §5.2 S22 |
| Sleep | No sleep command; Classic continuation carries the session. | `nikon-spec` §7.8 |
| Wake | No wake command. | `nikon-spec` §9.2 |
| Transfer | No BLE transfer; Wi-Fi or Classic PTP. | `nikon-spec` §5.2 S31 |
| Firmware update | No BLE update; `2009` bit 12 advertises capability only. | `nikon-spec` §5.2 S32 |

### 2.7 Canon EOS Smart

| State | Peer-visible model | Evidence |
| --- | --- | --- |
| Boot | `A9 01` with 19 bytes, all-zero client UUID means unregistered. | `canon-spec` §2.1 ADV-01 |
| Idle | Registered client; 6-byte reconnect form. | `canon-spec` §2.1 ADV-02 |
| Capture | Smart role command rows, shutter and release. | `canon-spec` §5 |
| Playback | No documented BLE mode switch. | `canon-spec` §9 |
| Movie | Command row exists; effect capture-gated. | `canon-spec` §5 |
| Sleep | Power state lives in the advertisement status bits, not in a sleep write. | `canon-spec` §2.3 |
| Wake | Pairing or reconnect advertisement; no wake command. | `canon-spec` §2.1 |
| Transfer | Wi-Fi handover service `00020000`, not GATT image bytes. | `canon-spec` §3 |
| Firmware update | Not a BLE surface. | `canon-spec` §9 |

### 2.8 Canon EOS Remote

| State | Peer-visible model | Evidence |
| --- | --- | --- |
| Boot | `00050000` service in the advertisement. | `canon-spec` §2.1 ADV-04 |
| Idle | Remote role paired; command set is the `0x0005` service. | `canon-spec` §3 |
| Capture | Remote command rows. | `canon-spec` §5 |
| Playback | No documented BLE mode switch. | `canon-spec` §9 |
| Movie | Command row exists; effect capture-gated. | `canon-spec` §5 |
| Sleep | No sleep command. | `canon-spec` §9 |
| Wake | No wake command. | `canon-spec` §9 |
| Transfer | No BLE transfer. | `canon-spec` §9 |
| Firmware update | Not a BLE surface. | `canon-spec` §9 |

### 2.9 Sony

| State | Peer-visible model | Evidence |
| --- | --- | --- |
| Boot | Manufacturer ID `0x012D`, device type `0x0003`, protocol version, model code, tag records. | `sony-spec` §2.1 |
| Idle | Paired link after the login sequence. | `sony-spec` §7 |
| Capture | Shutter command rows; button order conflicts remain capture-gated. | `sony-spec` §5; `docs/camera-research/sony-lumix-dji.md` |
| Playback | No documented mode surface. | `sony-spec` §9 |
| Movie | Command row exists; effect capture-gated. | `sony-spec` §5 |
| Sleep | Advertisement tag `0x21` bit 6 carries the camera-on bit. | `sony-spec` §2.2 |
| Wake | No wake command; reconnect only. | `sony-spec` §2.2 |
| Transfer | PTP/IP over Wi-Fi, not BLE. | `sony-spec` §9 |
| Firmware update | Not a BLE surface. | `sony-spec` §9 |

### 2.10 Panasonic LUMIX

| State | Peer-visible model | Evidence |
| --- | --- | --- |
| Boot | Manufacturer `3A 00` plus the session service `054ac620`. | `lumix-spec` §2 A01 and A02 |
| Idle | Paired after the session handshake. | `lumix-spec` §7 |
| Capture | Shutter command rows. | `lumix-spec` §5 |
| Playback | No documented mode surface. | `lumix-spec` §9 |
| Movie | Command row exists; effect capture-gated. | `lumix-spec` §5 |
| Sleep | Sleep advertising states are app enums only; bytes unknown. | `lumix-spec` §2 A05 to A08 |
| Wake | Wakeup advertising state exists; bytes unknown. | `lumix-spec` §2 A04 |
| Transfer | Wi-Fi HTTP/DLNA, not BLE. | `lumix-spec` §9 |
| Firmware update | Not a BLE surface. | `lumix-spec` §9 |

### 2.11 DJI Osmo Action 4 and Action 5 Pro

| State | Peer-visible model | Evidence |
| --- | --- | --- |
| Boot | `AA 08 TT 00 FA` manufacturer form. | `dji-spec` §2 A01-P |
| Idle | Paired after Mimo credential exchange. | `dji-spec` §7 |
| Capture | Shutter command rows. | `dji-spec` §5 |
| Playback | No documented mode surface. | `dji-spec` §9 |
| Movie | Command row exists; effect capture-gated. | `dji-spec` §5 |
| Sleep | Camera sleep is advertised and woken by a controller `WKP` advertisement. | `dji-spec` §2 A03-D |
| Wake | `WKP` advertisement with the reversed camera MAC, 2 s, only after a recent connection. | `dji-spec` §2 A03-D |
| Transfer | Mimo Wi-Fi media, not BLE. | `dji-spec` §9 |
| Firmware update | Mimo Wi-Fi, not BLE. | `dji-spec` §9 |

## 3. Fault classes every peer must support

The eight classes are mandatory. The table fixes the trigger per family and the
host verdict a test can assert.

| Class | Fujifilm Basic | Fujifilm Secure | Ricoh | Nikon Remote | Nikon Smart |
| --- | --- | --- | --- | --- | --- |
| Reject | Token mismatch fails `write()`; no ATT error code is decoded | Refused status ack drops the link with reason `0x13` about 160 ms later | Handler returns false, application error `0x80` | `2083` write failure | `2000` stage rejection |
| Busy | None: no busy surface | IND1 `A68E3F66` first byte 0 means busy, used by the WiFi trigger | Capture Status byte 0, or a refused Operation Request | No busy surface | `2020` restriction flags |
| Wrong mode | No mode read; camera decides | No mode read; camera decides | `OperationMode` `02` BLE_STARTUP, `01` playback, `03` other | `2083` handler checks camera mode | `2020` main and sub status |
| Timeout | Token or registration timeout | `secureConnection` rc 13 or rc 520 | Pairing ceremony timeout, supervision `0x208` | Stage 2 or 4 timeout | Salt match or final OK timeout |
| Disconnect | Link loss after token refusal | Inline or deferred drop, mid-connect or mid-subscribe | Standby drop, re-pair denial, supervision loss | Link loss during a stage | Link loss during auth |
| Malformed | 1-byte or 3-byte shutter write | Wrong-length status or time packet | Wrong-length GPS, drive index 16 and above, `{01 02}` | Wrong stage byte or 16-byte frame | Wrong 17-byte frame |
| Stale bond | Not applicable: no SMP | Camera deleted its key; central keeps its bond | Camera deleted its key; central keeps its bond | No LE bond handling | Classic bond only |
| Camera-side deletion | Not applicable | Menu deletion; next connect times out | Menu deletion; next connect fails or drops | Remote remembered list eviction | Classic bond removal |

Evidence by column: `fuji-spec` §7.6 and §8; `fuji-spec` §7.4 and §8;
`ricoh-handlers` §2.1, §5.1, §9.4, `ricoh-spec` §7.5; `nikon-handlers` §4.1,
§4.3, `nikon-spec` §7.5; `nikon-handlers` §5.1, `nikon-spec` §7.8.

Canon, Sony, LUMIX, and DJI each reuse the same eight classes with their own
command rows: `canon-spec` §5 and §7, `sony-spec` §5 and §7, `lumix-spec` §5
and §7, `dji-spec` §5 and §7. Where the sheet marks a command unknown, the
peer refuses it rather than inventing a success (`plan159` evidence rules).

## 4. Scenario matrix

Every row is a host test target, a simulator scenario, or both. Host tests run
under `tests/host/CMakeLists.txt`; simulator scenarios are files owned by
`sim/scenarios/manifest.json` and validated by
`python3 tools/check_sim_scenarios.py`.

### 4.1 Host matrix

| ID | Scenario | Peer setup | Assertion | Evidence |
| --- | --- | --- | --- | --- |
| H-FJ-01 | Basic token accept | Basic peer, default token | `tokenAccepted()`, identity written, registration confirmed | `fuji-spec` §7.6 |
| H-FJ-02 | Basic token mismatch | `Config::token` changed | `connect()` false, link reclaimed, no identifier write accepted | `fuji-spec` §7.6 |
| H-FJ-03 | Secure status ack gate | Secure peer, production connect | `statusAcknowledged()`, `clientClass() == 2` | `fuji-handlers` §3.3 |
| H-FJ-04 | Secure ack before read | Write the ack directly through the peer | Refused, `statusAcknowledged()` false | `fuji-handlers` §3.3 |
| H-FJ-05 | Secure ack after window | `setRegistrationWindowMs(0)` after the read | Refused, `registrationWindowExpired()` | `fuji-handlers` §3.2 |
| H-FJ-06 | Secure wrong identifier | Ack with a different low 28 bits | Refused, no class change | `fuji-handlers` §3.3 |
| H-FJ-07 | Shutter accept set | 2-byte writes 00, 01, 02 | All accepted, `lastShutterValue()` tracks | `fuji-handlers` §4 |
| H-FJ-08 | Shutter reject set | `03 00`, `01 01`, 1 byte, 3 bytes | All refused, `shutterRejected()` increments | `fuji-handlers` §4 |
| H-FJ-09 | Focus path | Production `focusPress()` / `focusRelease()` | Writes `01 00` then `00 00`, both accepted | `fuji-handlers` §4 |
| H-FJ-10 | Geotag length | 22 and 24 byte writes after a request | Refused, no accepted geotag | `fuji-handlers` §6.1 |
| H-FJ-11 | Geotag timestamp | Month 13, hour 24, second 60 | Refused | `fuji-handlers` §6.1 |
| H-FJ-12 | Sync interval set | 10, 15, 20, 30, 60, 120, 240, 480 | Accepted and stored | `fuji-handlers` §6.2 |
| H-FJ-13 | Sync interval reject | 5, 45, 300, 600 | Refused | `fuji-handlers` §6.2 |
| H-FJ-14 | Local time packet | 7 valid bytes, then month 13 | First accepted, second refused | `fuji-handlers` §5.1 |
| H-FJ-15 | UTC time packet | 12 valid bytes, offset 2400 and 2401, DST 1 and 2 | 2400 and DST 1 accepted, the rest refused | `fuji-handlers` §5.2 |
| H-FJ-16 | Power request | `01 00`, `00 01`, `00 00`, `02 00` | First two accepted, rest refused | `fuji-handlers` §9.1 |
| H-FJ-17 | Stale bond refusal | `setRefuseWhileBonded(true)` | Bond kept, connect fails, no repair verdict | `fuji-spec` §7.5 |
| H-FJ-18 | Security timeout | `setSecureTimeouts(kSecureTimeoutAlways)` | Link down, failure shape is timeout not refusal | `fuji-spec` §8 |
| R-01 | Power write values | Write `00`, `01`, `02` | First two accepted, `02` refused | `ricoh-handlers` §5.1 |
| R-02 | GEO Tag values | Write `00`, `01`, `02` | First two accepted, `02` refused | `ricoh-handlers` §5.5 |
| R-03 | GPS length | 31 and 33 byte writes | Refused | `ricoh-handlers` §6 |
| R-04 | GPS decode | Production 32-byte packet | `lastGps()` latitude, longitude, altitude, year match | `ricoh-handlers` §6 |
| R-05 | GPS trailing byte | Byte 31 set nonzero | Accepted, `trailing` recorded, no datum rejection | `ricoh-handlers` §6 |
| R-06 | Drive mode set | Indices 0, 1, 2, 15, 16 | 0 to 15 accepted, 16 refused | `ricoh-handlers` §5.9.1 |
| R-07 | Drive table mapping | Index 1 and 2 | Drive 0, timer 10 s and 2 s | `ricoh-handlers` §5.9.1 |
| R-08 | Operation request matrix | `{0,1}`, `{1,0}`, `{1,1}`, `{2,0}`, `{0,0}`, `{1,2}` | First four accepted, last two refused | `ricoh-handlers` §8.1 |
| R-09 | Self timer | Write `00`, `01`, `02` | All accepted, only `01` sets `selfTimerTriggered()` | `ricoh-handlers` §5.12 |
| R-10 | Busy capture | `setBusy(true)`, then Operation Request | Refused, link up | `ricoh-handlers` §2.1 |
| R-11 | Reject write | `failWrite` on Operation Request | Refused, link up | `ricoh-handlers` §2.1 |
| R-12 | Passkey method | GR III config with `setPasskeyAccepted(false)` | `secureConnection()` false | `ricoh-handlers` §9.1 |
| R-13 | Numeric comparison | GR IV config with `accept_numeric_comparison = false` | `secureConnection()` false, bond preserved | `ricoh-handlers` §9.2 |
| R-14 | Re-pair while bonded | `setRepairRequest(true)` | Denied, camera bond cleared, central bond kept | `ricoh-handlers` §9.4 |
| R-15 | Pairing menu closed | `in_pairing_mode = false`, no camera bond | Denied and disconnected | `ricoh-handlers` §9.4 |
| R-16 | Sleep advertisement | `sleep_advertising = true` | Advertisement carries only `0F291746` | `ricoh-handlers` §10.1 |
| N-01 | Remote short press | `{02 02}` on `2083` | Accepted, recorded | `nikon-handlers` §4.4 |
| N-02 | Remote long press | `{02 03}` on `2083` | Accepted, recorded | `nikon-handlers` §4.4 |
| N-03 | Remote no-op | `{02 00}` on `2083` | Accepted by GATT, no action | `nikon-handlers` §4.4 |
| N-04 | Remote wrong length | 1 and 3 byte writes | Refused | `nikon-handlers` §4.3 |
| N-05 | Smart control point | `{01 00}` on `2008` | Refused with the camera's "do not allow" path | `nikon-handlers` §0 item 7 |

Rows R-01 to R-16 and H-FJ-03 to H-FJ-16 describe the new peer surface; the
existing host tests already cover the production side for several of them
(`ricoh_focus_test.cpp`, `ricoh_sleep_gate_test.cpp`,
`fujifilm_camera_test.cpp`, `fujifilm_registration_gate_test.cpp`,
`camera_regression_test.cpp`).

### 4.2 Simulator matrix

| ID | Scenario file | Peer | Fault or state | Expected exit | Evidence |
| --- | --- | --- | --- | --- | --- |
| S-FJ-01 | `sim/scenarios/e2e/fujifilm-secure-register.txt` | Secure | Healthy first pairing | 0 | `fuji-spec` §7.2 |
| S-FJ-02 | `sim/scenarios/bughunt/fujifilm-status-window.txt` | Secure | Ack after the 20 s window | 0, host reports failure | `fuji-handlers` §3.2 |
| S-FJ-03 | `sim/scenarios/bughunt/fujifilm-shutter-03.txt` | Secure | Write `03 00` | 0, no capture reported | `fuji-handlers` §4 |
| S-FJ-04 | `sim/scenarios/bughunt/fujifilm-stale-bond.txt` | Secure | Camera bond deleted | 0, repair prompt | `fuji-spec` §7.5 |
| S-FJ-05 | `sim/scenarios/e2e/fujifilm-basic-token.txt` | Basic | Token mismatch then match | 0 | `fuji-spec` §7.6 |
| S-R-01 | `sim/scenarios/e2e/ricoh-capture.txt` | Ricoh | Drive 0 then `{01 01}` | 0 | `ricoh-handlers` §8.1 |
| S-R-02 | `sim/scenarios/bughunt/ricoh-power-sleep-refused.txt` | Ricoh | Write `02` | 0, no sleep | `ricoh-handlers` §5.1 |
| S-R-03 | `sim/scenarios/bughunt/ricoh-ble-startup.txt` | Ricoh | Mode `02` | 0, capture refused | `sm` §5.2 |
| S-R-04 | `sim/scenarios/bughunt/ricoh-standby-drop.txt` | Ricoh | Flappy standby | 0 | `ricoh-spec` §8 |
| S-R-05 | `sim/scenarios/e2e/ricoh-gps.txt` | Ricoh | 32-byte fix | 0, packet accepted | `ricoh-handlers` §6 |
| S-N-01 | `sim/scenarios/e2e/nikon-remote-pair.txt` | Nikon Remote | Stages 1 to 4 | 0 | `nikon-handlers` §4.1 |
| S-N-02 | `sim/scenarios/bughunt/nikon-remote-stage-timeout.txt` | Nikon Remote | No stage 2 | 0, bounded failure | `nikon-spec` §8 |
| S-N-03 | `sim/scenarios/bughunt/nikon-smart-no-classic.txt` | Nikon Smart | Final OK absent | 0, unsupported verdict | `sm` §9.1 |

Scenario IDs are newly proposed here. Their files and manifest rows are follow-up
work; `sim/scenarios/manifest.json` must be updated in the same change and
validated with `python3 tools/check_sim_scenarios.py` (`AGENTS.md`).

## 5. Fujifilm refinements

### 5.1 Applied in this change

| Refinement | Before | After | Evidence |
| --- | --- | --- | --- |
| Shutter accept set | Any 2-byte write accepted | Only `00 00`, `01 00`, `02 00`; high byte must be 0; `03 00` refused and counted | `fuji-handlers` §4 |
| Focus bytes | Shutter value space was not modelled, so `03 00` would have passed | The peer now models `01 00` half press and `00 00` release; production sends exactly those | `fuji-handlers` §4; `lib/furble/protocol/FujifilmProtocol.h` |
| Secure status ack | Any 4-byte write accepted | Low 28 bits must match the value published on the read, value must be nonzero, and the read must have happened | `fuji-handlers` §3.3 |
| Client class | Not modelled | `clientClass()` is 1 when bit 28 is set and 2 when clear; Furble's `0x20` top nibble yields 2 | `fuji-handlers` §3.3 |
| 20 s registration window | Not modelled | Default 20 000 ms from the status read; `setRegistrationWindowMs()` and `expireRegistrationWindow()` drive tests | `fuji-handlers` §3.2 and §3.7 |
| NOT1 time request | Only a registration payload | `timeServiceUUID()`, `localTimeCharacteristicUUID()`, and `utcTimeCharacteristicUUID()` added; both packets are parsed with the recovered predicates | `fuji-handlers` §5 |
| Local time packet | Absent | 7 bytes, exact timestamp ranges, sets `localTimeWritten()` | `fuji-handlers` §5.1 |
| UTC time packet | Absent | 12 bytes, offset in `-2400..2400` hundredths of an hour, DST flag 0 or 1 | `fuji-handlers` §5.2 |
| Power request | Absent | `43070F6C` accepts exactly `01 00` and `00 01` | `fuji-handlers` §9.1 |
| Sync interval | Any 2-byte value accepted | Only 10, 15, 20, 30, 60, 120, 240, 480 | `fuji-handlers` §6.2 |
| Geotag packet | Request flag only | 23 bytes, same timestamp predicate as the time packets | `fuji-handlers` §6.1 |
| Secure advertisement type byte | `D8 04 02` plus serial | `D8 04 01` plus serial | `fuji-spec` §2.1 A3; `fuji-handlers` §7 |

The focus refinement is a modelling correction, not a production change.
Production already sends `01 00` and `00 00`
(`lib/furble/Fujifilm.cpp focusPress`), and `FujifilmProtocol.h` already states
the accepted set. The peer now fails a `03 00` write, which is what the camera
parser does.

### 5.2 Deliberately not applied

| Finding | Why not applied now | Closing artifact |
| --- | --- | --- |
| Registration accepted is signalled by IND1 and IND2 after the ack, not by NOT1 | The handler note does not bind the three candidate values to the two characteristics, so emitting them would invent a mapping | Capture of an accepted status ack on X100VI 1.31 |
| NOT1 `01 00` is a time request, not a registration flag | Production treats it as registration, and the whole host suite depends on that. Changing the peer would change a host policy, not a camera model | Capture that varies the time request independently of registration |
| Registered advertisement rotates to `123D8F06` | The peer can already be configured with `advertised_services`, but no automatic rotation is implemented | Capture of a saved reconnect advertisement |
| Status ack refusal drops the link with reason `0x13` after about 160 ms | Needs a scheduled drop on the peer clock; the write path must not disconnect synchronously | Capture of the refusal timing |
| `B1307521` 92-byte blob and `861442AB` movie stop value | Field semantics are unknown | Handler decode or capture |

## 6. Ricoh refinements

### 6.1 Applied in this change

| Refinement | Before | After | Evidence |
| --- | --- | --- | --- |
| Camera Power write | Not writable at all | `00` powers off and `01` is accepted; `02` and above refused | `ricoh-handlers` §5.1 |
| GEO Tag write | Any payload accepted | Exactly one byte, value 0 or 1, otherwise refused | `ricoh-handlers` §5.5 |
| GPS Information write | Any payload accepted | Exactly 32 bytes; three byte-swapped `float64`, native `uint16` year, month to second, byte 31 recorded and unused | `ricoh-handlers` §6 |
| Passkey versus numeric comparison | One boolean for both generations | `Config::pairing` selects `kPasskeyDisplay` or `kNumericComparison`; `setPasskeyAccepted()` gates the passkey path | `ricoh-handlers` §9.1 and §9.2 |
| Drive mode table | Any single byte accepted | Indices 0 to 15 accepted, 16 and above refused; `driveMode()` returns the decoded `(drive, self-timer)` row | `ricoh-handlers` §5.9.1 |
| Operation Request | Any payload accepted | `{0,1}`, `{1,0}`, `{1,1}`, `{2,*}` accepted; `{0,0}` and `{1,2}` refused | `ricoh-handlers` §8.1 |
| Self timer | Any payload accepted | Any 1-byte write succeeds; only `01` sets `selfTimerTriggered()` | `ricoh-handlers` §5.12 |
| Advertisement record | No manufacturer data and four services | `5F 06 DA` typed record; Camera Information normally and Bluetooth Control in sleep | `ricoh-handlers` §10.1 and §10.2 |
| Pairing menu gate | Not modelled | A pairing attempt with the menu closed is denied and disconnected | `ricoh-handlers` §9.4 |
| Re-pair while bonded | Not modelled | `setRepairRequest(true)` models deny, camera-side deletion, and disconnect | `ricoh-handlers` §9.4 |
| Fault surface | Timeout and disconnect only | Added `failWrite()`, `setBusy()`, `setRepairRequest()`, `clearFaults()` | `ricoh-handlers` §2.1; `plan159` |

### 6.2 Deliberately not applied

| Finding | Why not applied now | Closing artifact |
| --- | --- | --- |
| Power write `0` notifies `CameraPower 00` and drops the link | Both are scheduled events and plan 159 forbids notifying from `write()`. `triggerStandbyDrop()` already owns the notify plus drop shape | Capture of the power-off timing |
| `DateTimeZone` 7-byte write and the ISO-8601 read asymmetry | Furble does not use the clock surface, so the peer would only serve a capture-gated feature | Round-trip capture on GR III and GR IV |
| Storage, battery, and CamSrvNotification payloads | Field semantics are `unknown` or unproven (`ricoh-handlers` §5.6, §5.2, §13) | Capture of each notification |
| GR IV-only 14 characteristics | The peer currently answers only the characteristics Furble uses. Adding all 65 rows is a separate peer-expansion task | GR IV 1.20 capture and the handler tables |
| WLAN Control and firmware update | Furble does not open the WLAN or update path, and both are HTTP-heavy | Wi-Fi sheet and a GR WORLD capture |

## 7. Shared test harness plan

### 7.1 One peer fixture, two families

Add `lib/testing/peer/PeerFixture.h` and `PeerFixture.cpp` (follow-up; not part
of this change) with:

| Piece | Responsibility |
| --- | --- |
| `PeerFixture` | Owns a `NimBLEDevice` reset, one peer, and the host camera object |
| `PeerScenario` | Declarative list of steps from section 1.10 |
| `PeerJournal` | Ordered record of advertisement, connect, write, read, subscribe, notification, and drop events with timestamps |
| `PeerAssert` | Helpers for "write was refused", "no write after drop", "state is X" |
| `PeerClock` | Host wall clock or simulator virtual clock, installed through `PeerStall.h` |

The fixture must not change production code. It wraps the existing peer classes
and `MockNimBLE`.

### 7.2 Common conformance test

`tests/host/peer_contract_test.cpp` runs the same checklist against every peer:

1. `advertisement()` round-trips through the family matcher.
2. Every declared service answers `hasService()`.
3. Every declared characteristic answers `hasCharacteristic()` and the right
   `canWrite()`.
4. A read-only characteristic refuses `write()`.
5. A wrong-length write is refused for every exact-length gate.
6. `subscribe()` fails without a link and succeeds with one.
7. `emitNotification()` reaches the callback with the same bytes.
8. `clearFaults()` restores the healthy path.

The checklist is the same for all peers; only the profile rows differ.

### 7.3 Family suites

| File | Covers |
| --- | --- |
| `tests/host/peer_fujifilm_test.cpp` | Section 5.1 rows, status window, time packets, power, sync interval, advertisement form |
| `tests/host/peer_ricoh_test.cpp` | Section 6.1 rows, drive table, GPS decode, pairing method, repair request, sleep advertisement |
| `tests/host/peer_nikon_test.cpp` | `2083` key values, `2084` six-byte indication, `2087` stages, Smart `2008` refusal |
| `tests/host/peer_canon_test.cpp` | Smart and Remote advertisement forms and command gates |
| `tests/host/peer_sony_test.cpp` | Sony manufacturer record and command rows |
| `tests/host/peer_lumix_test.cpp` | LUMIX manufacturer record and session service |
| `tests/host/peer_dji_test.cpp` | DJI `AA 08` form and `WKP` wake advertisement |

Each suite links the same production sources as the matching family test. No
suite may call a peer method that production cannot reach; the peer is only
allowed to expose state for assertions.

### 7.4 Certification hook

`plan159` requires a feature-scoped verdict. The harness adds a
`PeerCertification` record with:

```text
profile: camera, model string, firmware, role
evidence: label per field, hardware-capture or synthetic
scenario: id, peer config, fault set
result: PASS_CERTIFIED, FAIL_CERTIFIED, UNCERTIFIED
```

A run that uses only synthetic faults or cross-model inference is always
`UNCERTIFIED`, per `plan159` "Exact profile identity".

## 8. CMake integration plan

### 8.1 Host harness

`tests/host/CMakeLists.txt` already compiles both peers per test target. The
follow-up adds:

| Target | Sources | Notes |
| --- | --- | --- |
| `peer_contract_test` | `peer_contract_test.cpp`, both peers, `MockNimBLE.cpp` | One binary, all peers |
| `peer_fujifilm_test` | `peer_fujifilm_test.cpp`, `FujifilmVirtualCamera.cpp`, production Fujifilm sources | New |
| `peer_ricoh_test` | `peer_ricoh_test.cpp`, `RicohVirtualCamera.cpp`, production Ricoh sources | New |
| `peer_nikon_test` | `peer_nikon_test.cpp`, new Nikon peers | New |
| `peer_canon_test`, `peer_sony_test`, `peer_lumix_test`, `peer_dji_test` | One per family | New |

Rules:

1. Add each target to `furble_host_camera`'s source list only if it is a shared
   library. The current pattern compiles the peer source directly per target,
   which keeps the sanitizer builds isolated.
2. Register every target with `add_test()` and a timeout, as the existing
   camera tests do.
3. Keep a sanitizer variant for the fault tests, mirroring
   `control_reclaim_uaf_test` and `ricoh_secure_timeout_uaf_test`.
4. Add the new targets to the coverage profile mapping at the end of
   `tests/host/CMakeLists.txt` so `tools/coverage.py` can name a lost profile.

### 8.2 Simulator

`sim/CMakeLists.txt` and `sim/build.sh` list
`lib/testing/peer/RicohVirtualCamera.cpp`. The follow-up adds every new peer to
both lists and to `sim/BleSim.cpp` peer construction.

| Step | Change |
| --- | --- |
| 1 | Add a peer factory in `sim/BleSim.cpp` keyed by family, so a scenario can name a camera type |
| 2 | Map scenario fault names to peer setters in `sim/scenario_action.cpp` |
| 3 | Add the scenario files from section 4.2 |
| 4 | Register each file in `sim/scenarios/manifest.json` with a suite and owner |
| 5 | Run `python3 tools/check_sim_scenarios.py` |

### 8.3 Validation commands

```sh
cmake -S tests/host -B /tmp/furble-host
cmake --build /tmp/furble-host -j
ctest --test-dir /tmp/furble-host --output-on-failure
python3 tools/check_sim_scenarios.py
python3 tools/coverage.py
```

The exact coverage command and floor live in `tools/coverage.py` and
`tests/coverage_floor.json`; lowering a floor requires a stated reason
(`AGENTS.md`).

## 9. Contract gaps and capture gates

| Gap | Impact | Closing artifact |
| --- | --- | --- |
| Notifications emit synchronously from `subscribe()` and `write()` in the current peers | Violates `plan159` scheduling rule; a stacked CCCD operation can observe ordering that hardware never produces | Scheduled-notify migration on the peer clock |
| Advertising windows are not scheduled | A test can see an advertisement after the camera should have stopped (Fujifilm about 30 s, Ricoh sleep form indefinite) | Advertising window scenario plus a capture |
| Ricoh peer answers only the Furble-used characteristic subset | A GR IV full-table test would see missing characteristics | Full GR IV 1.20 GATT table import |
| Fujifilm peer does not emit IND1 and IND2 on registration | The real accepted-registration surface is untested | Capture of an accepted ack |
| Nikon peers do not exist yet | Workstream E item for both roles is open | New `NikonRemoteVirtualCamera` and `NikonSmartVirtualCamera` |
| Canon, Sony, LUMIX, DJI peers do not exist yet | Workstream E items are open | One peer per family |
| Ricoh power-off notify and link drop are not schedule-driven | The power-off scenario needs the standby drop path | Capture of power-off timing |
| Fujifilm movie stop value and 92-byte blob semantics | The movie and firmware-update states cannot be certified | Handler decode or capture |

## 10. Change log

| File | Change |
| --- | --- |
| `lib/testing/peer/FujifilmVirtualCamera.h` | Added shutter constants and counters, status window and class accessors, time, power, and sync-interval state, and new UUID accessors |
| `lib/testing/peer/FujifilmVirtualCamera.cpp` | Enforced the shutter accept set, the status ack gate with a 20 s window, the time packet predicates, the power values, and the sync-interval set; corrected the Secure advertisement type byte |
| `lib/testing/peer/RicohVirtualCamera.h` | Added the pairing method, GPS and drive-mode structs, handler-derived accessors, and the fault surface |
| `lib/testing/peer/RicohVirtualCamera.cpp` | Enforced power, GEO Tag, GPS, drive mode, operation request, and self-timer rules; added the GR III/GR IV pairing split, the repair request, and the typed advertisement record |

No production source, test, scenario manifest, or other document was changed.
