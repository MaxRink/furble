# Furble camera research: Sony, Panasonic LUMIX, DJI Osmo

Research date: 2026-09-12 (Europe/Berlin). This is a research ledger, not a
compatibility guarantee. The repository says only Fujifilm hardware is
available for testing. A public implementation, an official semantic manual,
or an advertisement match is not proof of exact private bytes, firmware
compatibility, or a physical capture.

## Source identity and provenance

The inspected firmware snapshot matches a clean source tree at immutable commit
`965f299f0f43c38fe45e4680f1bfb735023533af` (`MaxRink/furble`, retrieved
2026-09-12). The Sony, Lumix, DJI, CameraList, advertisement, README,
supported-hardware, and plan files below were hash-checked against that exact
commit. Canonical source permalinks are [Sony.cpp](https://github.com/MaxRink/furble/blob/965f299f0f43c38fe45e4680f1bfb735023533af/lib/furble/Sony.cpp),
[Lumix.cpp](https://github.com/MaxRink/furble/blob/965f299f0f43c38fe45e4680f1bfb735023533af/lib/furble/Lumix.cpp),
[DJIOsmo.cpp](https://github.com/MaxRink/furble/blob/965f299f0f43c38fe45e4680f1bfb735023533af/lib/furble/DJIOsmo.cpp),
and [CameraList.cpp](https://github.com/MaxRink/furble/blob/965f299f0f43c38fe45e4680f1bfb735023533af/lib/furble/CameraList.cpp).
Snapshot file hashes are recorded below for future matching.

| Snapshot file | SHA-256 |
| --- | --- |
| `lib/furble/Sony.cpp` | `a4729d735f6d97b4e4952f22b4c9953945de16d0e017b1189ff7d379f445da0f` |
| `lib/furble/Sony.h` | `6d445f7f709d27eb82b4e555768100b53cb15e8fcfa1e2ba225cda3d2c3cf375` |
| `lib/furble/Lumix.cpp` | `63a7d6a4cc72146f31d5c8134246ac5558bedd73b39ff0b3e9f0de0f43192e26` |
| `lib/furble/Lumix.h` | `fc77ca8ef7349554632405b7c5797d2747896b6a815f1535711e3fbe7413d1b4` |
| `lib/furble/DJIOsmo.cpp` | `a80402c31d52090bb71de160ebb9ea5a912ab7f57989b92f3bc4069c4101cdbe` |
| `lib/furble/DJIOsmo.h` | `19b8e4c4e56cc04592ce65a10c8f6d6dc4c0b8a35a3791a606e73c8ad2781018` |
| `lib/furble/CameraList.cpp` | `ec89d63f7f114f5927db353164e82a41e5c8b001dc1790097ab8fa488752fd4b` |
| `lib/furble/protocol/AdvertisementProtocol.cpp` | `32523304c3a554fdff85cbf02e2a88eb40260ea7c59f60ad802fb41c49d88d8f` |
| `README.md` | `eccb0b6a052d665b022a058691eb83890a08fc245c6859199616335ebb0a1126` |
| `docs/supported-hardware.md` | `8933f6beb3bf98068470f64056eb9911cb49343fd92112d833bcd2636b5a041f` |
| `plans/159-camera-peer-certification.md` | `9cd56637de2417ea31774383e1eb5b8516494ba6338a30fc0df2d7cd5e1d09ea` |

Plan 159 is the governing provenance model. It distinguishes
`hardware-capture`, `official-semantic`, `common-implementation`,
`cross-model-inferred`, and `synthetic`; only the first can certify exact
private bytes, security, ordering, timing, or physical outcome. Its strict
peer leads for Sony and Lumix are missing FF02/readiness gates and
address-only persistence. The DJI lead is unproven SMP and MTU/write
assumptions.

## Complete camera-family inventory

`Camera::Type` and `CameraList` currently register these real hardware vendor
modes: Fujifilm Basic/Secure, Canon EOS Smart/Remote, Nikon, Sony, Ricoh
Imaging, Panasonic LUMIX, and DJI Osmo. The Ricoh matcher also recognizes
PENTAX-like names and reuses the Ricoh type/path, but that is broad matching,
not validated PENTAX support. There is no additional real camera family in the
inspected source.
`MOBILE_DEVICE` is a deprecated reserved enum value, not a supported camera.
`FauxNY` is a software test peer and is listed separately below.

The README's tested list names Fujifilm bodies, Canon EOS M6/R6 Mark II/RP and
PowerShot G9 X Mark II, Ricoh GR IV HDF, Nikon COOLPIX B600/Z6 III, and Sony
ZV-1F. It does not name a tested Lumix or DJI model. `docs/supported-hardware.md`
also omits Lumix and DJI even though `CameraList.cpp` includes their headers,
deserialization cases, and scan matchers. This is a documentation gap, not
evidence that the two modes are hardware-tested.

## Sony

### Furble implementation and exact source scope

Files: `lib/furble/Sony.cpp`, `Sony.h`,
`protocol/AdvertisementProtocol.cpp`, and `CameraList.cpp` in the snapshot
above. `CameraList` registers `Camera::Type::SONY`; `Sony::matches` is an
advertisement matcher, not a model registry.

The README names only **Sony ZV-1F** as tested. It says “most modern Sony
cameras,” but no exact model/firmware capture is included. Therefore the only
model-level Furble evidence is ZV-1F community testing, with no firmware
version recorded. All other Sony models remain unverified.

Advertisement matching parses at least 13 manufacturer bytes: company ID
`0x012d` (Sony), device type `0x0003` (camera), protocol version, two-byte
model code, and status tags `0x22` and `0x21`. Furble accepts a device when
`0x22` mode has bits 7, 6, and 1 set. It does not retain or gate on the model
code. The matcher is therefore broad and cannot establish model support.

GATT constants in the implementation:

| Role | UUID |
| --- | --- |
| Remote/control service | `8000ff00-ff00-ffff-ffff-ffffffffffff` |
| Control characteristic | `0000ff01-0000-1000-8000-00805f9b34fb` (`0xff01`) |
| Location service | `8000dd00-dd00-ffff-ffff-ffffffffffff` |
| Location/status characteristic | `0xdd01` |
| Location update characteristic | `0xdd11` |
| Location info/read characteristic | `0xdd21` |
| Location allow/lock characteristic | `0xdd30` |
| Location enable characteristic | `0xdd31` |

Connect flow is: create NimBLE client through `Camera::connect`, set the
requested connection timeout and connection parameters, enable authenticated
bonded security globally, call Sony `_connect`, connect to the saved address,
call `secureConnection`, require the control service and FF01, then optionally
retain the DD00 location service. The vendor path does not subscribe to FF02,
read FF21, or verify a camera-side remote-registration acknowledgement. A
failed connect/secure/service lookup returns false; the base class reports the
handshake failure and tears down the client. The base call receives a caller
timeout, but Sony has no vendor-specific timeout constant or poll loop.

Current Furble command writes are raw little-endian 16-bit values to FF01:

| API | Constant | Wire bytes | Meaning from public implementations |
| --- | ---: | --- | --- |
| `focusPress` | `0x0701` | `01 07` | half-down |
| `focusRelease` | `0x0601` | `01 06` | half-up |
| `shutterPress` | `0x0901` | `01 09` | full-down |
| `shutterRelease` | `0x0801` | `01 08` | full-up |

The current code sends only full-down/full-up for the shutter. The Apache
Freemote implementation and the GPL AlphaRemote implementation model a full
shutter action as half-down, full-down, then full-up, half-up. Freemote warns
that skipping the sequence can leave a camera inoperable. This is a
source-conflicting, exact-model capture question, not an accepted Furble bug.

Freemote reports FF02 notifications:

* `02 3f 00/20`: focus lost/acquired;
* `02 a0 00/20`: shutter ready/active;
* `02 d5 00/20`: recording stopped/started.

Furble currently neither subscribes to FF02 nor waits for those statuses. The
AlphaRemote source uses a serialized GATT operation queue, treats Android
status 144 as “remote disabled,” and waits up to 3000 ms for expected focus or
shutter status. Those are useful peer behaviors, not Furble-certified timing.

Location behavior in current Furble: read DD30 and DD31; write `01` to either
when not already enabled; then write a 95-byte fixed-format packet to DD11.
The packet starts `00 5d 08 02 fc 03 00 00 10 10 10`, encodes latitude and
longitude as big-endian signed degrees times 10^7, year as big-endian, then
UTC date/time, 65 zero bytes, a zero UTC offset, and two zero bytes. The source
does not read DD21 configuration or subscribe to DD01. It sends a 95-byte
packet with timezone/DST-related fields zero-valued, not an actual local
timezone or DST setting. The public ILCE7M3ExternalGps protocol says firmware
protocol >=65 (camera firmware 3.02+) requires DD30 then DD31, in that order,
and describes DD21 bit 2 (mask `0x02`) as controlling timezone/DST fields and a
95-byte packet. Its prose/length arithmetic is internally inconsistent,
however: omitting four bytes from 95 yields 91, not the separately claimed
older 89-byte form. Treat the bit semantics and 89/95-byte variants as
unresolved; do not hard-code either behavior until exact model/firmware traces
are captured.

GPS/time facts supported by the sources: Sony location payload carries UTC
year/month/day/hour/minute/second, and the public 95-byte form has optional
timezone and DST offsets. Furble hard-codes zero offset fields, so local-time/
DST behavior is unknown and likely model/firmware-dependent. Furble has no Sony
power-control command. The public advertisement decoder reports `0x21` bit 6
as powered-on versus networked standby and bit 7 as remote-power-on support.

Official Sony ZV-1F semantics: enable Bluetooth Function, enable Bluetooth Rmt
Ctrl, enter Bluetooth Pairing, pair the remote, then accept the camera's
confirmation. Sony documents one remote at a time, deletes pairing on camera
initialization, says the remote link is active only while operating the
remote, and disables power-saving mode while Bluetooth Rmt Ctrl is On.

### Sony primary sources and licensing

* [Sony ZV-1F Help Guide, Bluetooth Rmt Ctrl](https://helpguide.sony.net/dc/2210/v1/en/contents/TP1000931171.html), official semantic source, retrieved 2026-09-12. It does not document private GATT UUIDs or bytes.
* [coral/freemote README at commit `9dad4c257af19c9274a70f8affd74b8b3243d8a6`](https://github.com/coral/freemote/blob/9dad4c257af19c9274a70f8affd74b8b3243d8a6/README.md), and [BLECamera.cpp](https://github.com/coral/freemote/blob/9dad4c257af19c9274a70f8affd74b8b3243d8a6/src/BLECamera.cpp). Repository license is Apache-2.0. It reports A7 III testing and cross-model expectations, not a Furble-compatible certification.
* [Staacks/alpharemote CameraBLE.kt at commit `93972e53d1f8333e431762a7c25322f2f489a8ad`](https://github.com/Staacks/alpharemote/blob/93972e53d1f8333e431762a7c25322f2f489a8ad/app/src/main/java/org/staacks/alpharemote/camera/CameraBLE.kt), [CameraAction.kt](https://github.com/Staacks/alpharemote/blob/93972e53d1f8333e431762a7c25322f2f489a8ad/app/src/main/java/org/staacks/alpharemote/camera/CameraAction.kt), and [CameraActionStep.kt](https://github.com/Staacks/alpharemote/blob/93972e53d1f8333e431762a7c25322f2f489a8ad/app/src/main/java/org/staacks/alpharemote/camera/CameraActionStep.kt). Repository license is GPL-3.0. It confirms FF01/FF02 and ordered button actions, but cannot be copied into MIT Furble without a licensing decision.
* [whc2001/ILCE7M3ExternalGps PROTOCOL_EN.md at commit `219a05745bdcbbe8ede1442b9ba356d6fd8e1e29`](https://github.com/whc2001/ILCE7M3ExternalGps/blob/219a05745bdcbbe8ede1442b9ba356d6fd8e1e29/PROTOCOL_EN.md). The repository intentionally has no LICENSE and describes itself as abandoned/reference-only; citation and clean-room research only.

### Sony future capture targets

Capture exact model and firmware for ZV-1F first, then at least one Alpha body
and one RX/ZV body. Record raw manufacturer advertisements through pairing,
bonding/security, GATT handles/properties, FF02 notifications, the complete
shutter sequence, DD21 configuration, DD30/DD31 ordering, 89/95-byte GPS
variants, UTC offset/DST, camera networked-standby transitions, disconnect
reason, and physical photo/recording outcome. Repeat after firmware updates.
Use a peer that rejects malformed ordering and wrong characteristic properties;
do not promote Freemote/AlphaRemote bytes to `PASS_CERTIFIED`.

## Panasonic LUMIX

### Furble implementation and exact source scope

Files: `lib/furble/Lumix.cpp`, `Lumix.h`,
`protocol/AdvertisementProtocol.cpp`, and `CameraList.cpp`. The class comment
says its reverse engineering is based on **S5II and BGH1** HCI snoop logs and
explicitly says the **G9II-generation XOR login is not implemented**. No
Lumix model appears in Furble's tested README list or supported-hardware doc.
The defensible status is therefore an untested S5II/BGH1-derived experimental
path, not model-certified support.

Advertisement matching requires Panasonic manufacturer data with at least 8
bytes, company ID `0x0376`, and the proprietary session service advertised.
It parses a flags byte plus a five-byte address fragment but does not use a
model string. The matcher cannot distinguish S5II, BGH1, G9II, or other LUMIX
bodies.

GATT constants:

| Role | UUID |
| --- | --- |
| Session service | `054ac620-3214-11e6-ac0d-0002a5d5c51b` |
| Session-init MEI0 | `8d08a420-3213-11e6-8aca-0002a5d5c51b` |
| Device name | `cd7a71a0-3213-11e6-8f56-0002a5d5c51b` |
| Control service | `7be5faae-475b-11e7-a919-92ebcb67fe33` |
| Control command | `7be5fd56-475b-11e7-a919-92ebcb67fe33` |
| Location service/characteristic | `1d74afe0-3214-11e6-8ab4-0002a5d5c51b` / `daff1bc0-3216-11e6-91c8-0002a5d5c51b` |
| Clock service/characteristic | `34738720-3214-11e6-b66b-0002a5d5c51b` / `ead55e60-3216-11e6-a42e-0002a5d5c51b` |

The current connect flow is open-link `connect`, session service lookup, a
fixed 16-byte MEI0 write, ASCII controller device-name write, required control
service and writable control characteristic lookup, then optional location
and clock characteristic lookup. It does not call `secureConnection`, does
not bond, and does not subscribe to readiness notifications. On any missing
service/characteristic or failed write it returns false. No vendor timeout is
defined; the base caller supplies the connect timeout. The snapshot's source
comment says a shortened MEI0-to-GPS sequence can cause the camera to drop the
link, but current Furble does not wait for the two documented readiness
notifications before enabling GPS.

MEI0 is the fixed source array:
`4d 45 49 30 01 00 10 00 80 01 02 00 00 01 00 00`.
The source labels it a trace-derived session-init payload. The S5 protocol
report says captures used a changing counter/checksum but a fixed value was
accepted in repeated reconnects; this is not independently firmware-qualified
for Furble.

Control writes are one byte: focus press `01`, focus release `02`, shutter
press `04`, shutter release `05`. Furble sends no status subscription and has
no response/error-code interpretation beyond the boolean GATT write result.
The public G9II implementation sends a full still-photo sequence at handles
`0x0068`: `01`, `02`, `04`, then `05`; it also uses `06`, `07` for video
toggle. That handle-level implementation is a different generation and must
not be copied to the UUID path without an exact capture.

GPS/time writes in current Furble:

* location packet: 16 bytes, little-endian, GPS epoch seconds since
  1980-01-06, latitude/longitude as signed degrees times 10^7, altitude as
  uint16 metres, fix `0x41`, trailing zero;
* clock packet: 10 bytes, little-endian year, month/day/hour/minute/second,
  zero byte, and signed timezone offset set to zero;
* clock is written only after a successful location write.

The S5 report documents two readiness notifications (`37701b80` and
`4cf487c0`, both `01`) before time/GPS and reports a physical EXIF check, but
it is a third-party report without raw capture manifest or exact firmware
identity. It also reports RPA rotation of about 15 minutes and name anchors
such as `S5-XXXXXX`; current Furble persists an address and does not implement
name/IRK resolution. Power, sleep, explicit disconnect reasons, and focus
status are unknown in Furble's path. Panasonic's official semantic page says
Bluetooth Shutter Remote Control cannot turn the camera on and that Remote
Wakeup must be enabled with Auto Transfer off to cancel sleep.

### Panasonic primary sources and licensing

* [Panasonic S-series smartphone operation page](https://help.na.panasonic.com/answers/how-to-operate-the-camera-with-a-smartphone-lumix-s-series/), official semantic source, retrieved 2026-09-12. It documents Bluetooth shutter operation, bulb semantics, and sleep/wakeup settings, not private UUIDs.
* [Panasonic DC-S5M2 complete manual PDF](https://help.na.panasonic.com/wp-content/uploads/2023/02/DCS5M2_DVQP2839ZA_ENG.pdf), official model manual, retrieved 2026-09-12. Search result identifies LUMIX Sync shutter remote and Bluetooth operation; PDF text extraction was not relied on for private protocol bytes.
* [tobiasbrummer/lux-lat-long-log PROTOCOL.md at commit `87e51686c8496ca20fa8b14433ba548383f8da5b`](https://github.com/tobiasbrummer/lux-lat-long-log/blob/87e51686c8496ca20fa8b14433ba548383f8da5b/PROTOCOL.md). Repository license is MIT. It reports S5 captures and physical EXIF evidence, but lacks an exact raw-capture manifest and firmware identity, so use as an uncertified template.
* [Aikhjarto/LUMIX-G9II-Remote-Control Python implementation at commit `25c91ebebd5d27ecf531405ee49b6d27e2c085d8`](https://github.com/Aikhjarto/LUMIX-G9II-Remote-Control/blob/25c91ebebd5d27ecf531405ee49b6d27e2c085d8/src/LumixG9IIRemoteControl/LumixG9IIBluetoothControl.py). Repository license is GPL-3.0. It uses a G9M2 name filter, handle/register login, and XOR challenge helpers. Clean-room/citation-only; do not inherit S5 fields.

### Panasonic future capture targets

Capture S5II and BGH1 separately, recording exact model strings, firmware,
RPA rotation, advertisements, GATT UUID/handle map, properties, MEI0 counter,
readiness notifications, name write, control response, shutter/focus/video
outcomes, clock/GPS packets, EXIF result, sleep/wakeup, power cycle and
disconnect reason. Capture G9II separately with its XOR login and model
identity. Test saved reconnect without assuming the address persists. A
strict peer must fail closed when either readiness notification is missing.

## DJI Osmo

### Furble implementation and exact source scope

Files: `lib/furble/DJIOsmo.cpp`, `DJIOsmo.h`,
`protocol/AdvertisementProtocol.cpp`, and `CameraList.cpp`. The class comment
names **Osmo Action 4 and Action 5 Pro**. It accepts only camera IDs
`0x33ff0000` (Action 4) and `0x44ff0000` (Action 5 Pro) after the protocol
handshake. It has no exact firmware capture and is not in the README or
supported-hardware tested lists. The current source therefore exposes an
experimental Action 4/5 Pro-oriented mode, not a certified model list.

The matcher accepts manufacturer data of at least five bytes with bytes
`[0]=aa`, `[1]=08`, `[4]=fa`. This is intentionally broad. The official DJI
demo at the pinned commit documents the same filter and additionally names
Osmo Action 6 and Osmo 360. Current Furble rejects those newer IDs in
`supportedCameraId`, so the official demo's model list must not be applied to
Furble.

GATT is the documented 16-bit service and characteristics:

| Role | UUID |
| --- | --- |
| Target service | `0xfff0` |
| Camera-to-controller notify | `0xfff4` |
| Controller-to-camera write | `0xfff5` |

Connect sets local MTU request 517, connects, calls `secureConnection`, and
requires encrypted plus bonded security (it logs authenticated but does not
require that flag). It discovers FFF0, requires FFF4 notify and FFF5 write or
write-without-response, subscribes to FFF4, then sends the protocol connection
request. The handshake waits up to 30 seconds in 100 ms polls. It rejects a
negative camera response, unsupported camera ID, non-`verify_mode == 2`, or
nonzero verification result. A cancellation token aborts the poll. Status
subscription failure is logged but does not fail the connection, leaving a
potentially stale local recording state.

Frames are little-endian DJI R SDK frames: SOF `0xaa`, 10-bit total length in
the version/length field, CmdType, ENC/reserved bytes, 16-bit sequence, CRC16
over bytes SOF through SEQ, DATA beginning at offset 12 with CmdSet/CmdID, and
CRC32 over SOF through DATA. Current Furble uses CRC16 initial `0x3aa3` with
polynomial `0xa001` and CRC32 initial `0x00003aa3` with polynomial
`0xedb88320`. It allocates one full frame per write and sends it in one GATT
write operation.

Connection request is CmdSet `0x00`, CmdID `0x19`, command type `0x02`, with a
33-byte payload: controller device ID, value `06`, six-byte Bluetooth MAC,
zero-filled reserved area, new-pair verify mode `1` or saved mode `0`, and a
random four-digit verify value for a new pair or zero for a saved pair. The
camera response is parsed as a 33-byte request. Furble replies with CmdType
`0x20`, same sequence, controller ID and zero result, then marks the protocol
ready.

Recording uses CmdSet `0x1d`, CmdID `0x03`, type `0x01`, nine-byte payload with
camera device ID and record control `0` start / `1` stop. `shutterPress` toggles
this state; `shutterRelease`, focus press/release, and GPS/time update are
no-ops. Camera status subscription is CmdSet `0x1d`, CmdID `0x05`, payload
`03 14 00 00 00 00`. Status push is CmdSet `0x1d`, CmdID `0x02`; Furble uses
camera status `0x05` or status `0x03` outside photo mode to infer recording.
Malformed frame or CRC logs a rejection; camera reject, unsupported ID,
verification failure, failed GATT write, missing service/characteristic, and
30-second timeout return failure. No physical recording result is proven.

The official demo's pinned data structures additionally define GPS push as
CmdSet `0x00`, CmdID `0x17`, with date/time, longitude/latitude times 10^7,
height in millimetres, north/east/down speed, vertical/horizontal/speed
accuracy, and satellite count, followed by a one-byte return code. Its status
push documents camera mode, camera status, record time, remaining capacity,
battery percentage, temperature warning, and power mode (`0` normal, `3`
sleep). The demo sends GPS at 10 Hz from an LC76G GNSS source. These are
official-demo protocol targets only: current Furble sends no `0x17` frame and
does not parse the richer fields.

Official DJI documentation says the controller is the BLE master and Osmo the
slave, requires a camera connection request followed by pairing/protocol
authentication, exposes FFF4/FFF5, supports status subscription, and supports
recording, mode switching, sleep/wake, and GPS push. The official support page
lists Osmo Action 4, Action 5 Pro, Action 6, Osmo 360, and Osmo 360 II for the
GPS Bluetooth Remote Controller; it says Osmo 360 supports only recording
start/stop and mode switching in the applicable multi-product context. These
are official accessory semantics, not proof for Furble's IDs or frames.

Power/sleep and pairing caveat: current Furble assumes secure encrypted/bonded
BLE and auto-confirms or injects fallback passkey `123456` in its NimBLE
callbacks. The official demo's source has a prominent DJI EULA notice, and its
BLE sample contains TODO/reconnection limitations. The exact SMP IO method,
bond persistence, passkey behavior, MTU negotiation, camera wake state,
disconnect reason, retry behavior, and physical outcome need an HCI capture.

### DJI primary sources and licensing

* [DJI official Osmo GPS Bluetooth Remote Controller support article](https://repair.dji.com/help/content?customId=01700008289&lang=en&paperDocType=ARTICLE&re=US&spaceId=17), retrieved 2026-09-12. It lists compatible models, remote distance, multi-camera limits, wake/sleep, mode, recording, and GPS semantics.
* [DJI official Osmo-GPS-Controller-Demo README at commit `92fe23e5a749f189593f980a26a105c3bb66aa1c`](https://github.com/dji-sdk/Osmo-GPS-Controller-Demo/blob/92fe23e5a749f189593f980a26a105c3bb66aa1c/README.md), [protocol.md](https://github.com/dji-sdk/Osmo-GPS-Controller-Demo/blob/92fe23e5a749f189593f980a26a105c3bb66aa1c/docs/protocol.md), and [protocol_data_structures.h](https://github.com/dji-sdk/Osmo-GPS-Controller-Demo/blob/92fe23e5a749f189593f980a26a105c3bb66aa1c/protocol/dji_protocol_data_structures.h). The repository README says Action 4/5 Pro/6 and Osmo 360; protocol docs define frame offsets, CRC scope, connection/record/GPS/status command IDs, and status fields.
* The DJI repository `LICENSE` is mixed: it places DJI R SDK protocol documents under the DJI EULA while offering demo code under an MIT text and includes restrictive DJI source-header notices. The GitHub API reports no SPDX license. Treat protocol documents and source as citation/independent-reimplementation material until DJI licensing is reviewed. Do not copy code, diagrams, or large protocol prose into Furble.

### DJI future capture targets

Capture Action 4 and Action 5 Pro individually, with exact firmware, model ID,
advertisement, FFF0 GATT properties/CCCDs, requested/negotiated MTU, SMP
pairing/bond state, passkey/verification exchange, connection request and
response sequence, CRC/length/ATT write boundaries, status subscription and
status cadence, start/stop recording outcome, camera wake/sleep, mode switch,
power cycle, disconnect reason, and GPS/EXIF or Mimo dashboard outcome. Capture
Action 6 and Osmo 360 separately before expanding Furble's ID table. A peer
must not equate a valid CRC or ATT acknowledgement with physical recording.

## FauxNY: non-hardware test peer

`FauxNY` is registered as `Camera::Type::FAUXNY`, always matches in development,
uses a synthetic random ID/name, waits 2.5 seconds in 25 ms cancellable slices,
then marks itself connected. Shutter/focus methods only log. Under `FURBLE_SIM`
it records the last GPS coordinates/time in process memory for assertions. It
has no real BLE advertisement, GATT, SMP, camera power, or physical outcome.
It is useful for Control lifecycle and simulator liveness tests only and must
never be included in a hardware compatibility table.

## Cross-family gaps and minimum simulator-equivalent future work

The current production gap is evidence, not another vendor implementation:

1. Add exact profile identity (model, firmware, advertised identity, GATT
   digest, Furble revision, board, dependency revisions, and capture digest)
   using plan 159's fail-closed `UNCERTIFIED` rule.
2. Add Sony FF02 status, pairing-state advertisement transitions, shutter
   ordering, DD21/DD30/DD31 and 89/95-byte GPS variants.
3. Add Lumix readiness notifications, RPA/name identity, S5II/BGH1 versus G9II
   login split, and exact reconnect/power-cycle behavior.
4. Add DJI exact model IDs, SMP and MTU behavior, scheduled status pushes,
   wake/sleep and physical recording outcomes. Keep official demo GPS as a
   capture target, not a drop-in Furble feature.
5. Make mock GATT enforce real properties, CCCDs, ATT errors, security, MTU,
   asynchronous notifications, and generation-tagged callbacks. Current
   software peers must not make every characteristic readable/writable.

Required measurements are raw HCI/ATT/GAP/SMP traces plus physical outcome
checks. At minimum record timestamps and repeated min/median/p95/max for
connect, security, service discovery, readiness/registration, command-to-
status, reconnect, and disconnect. Record battery/power state, camera mode,
firmware, region, address rotation, and analyzer/controller identity. Never
turn a public implementation or official semantic document into a wildcard
model guarantee.
