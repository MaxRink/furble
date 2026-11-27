# Camera remote protocols

furble is a BLE central to the camera. Each vendor has its own advertisement
match, service layout and command encoding. This page maps the wire contract the
firmware implements; the research trail per model and firmware is in
[docs/camera-research](../camera-research/README.md).

| Vendor mode | Class | Primary service | Focus | GPS | Hardware status |
| :--- | :--- | :--- | :--- | :--- | :--- |
| Fujifilm Basic | `FujifilmBasic` | `91f1de68-dff6-466e-8b65ff13b0f16fb8` | no | no | superseded; the pairing service is absent on current bodies |
| Fujifilm Secure | `FujifilmSecure` | `123d8f06-62a1-4935-9322-833c531ee225` | no | yes | tested on X100T, X100VI, X-S20, X-E5 |
| Sony | `Sony` | `8000ff00-ff00-ffff-ffffffffffff` | yes | yes | tested on ZV-1F and ILCE-7RM5 |
| Nikon Remote | `NikonRemote` | `0000de00-3dd4-4255-8d626dc7b9bd5561` | no | no | shutter only |
| Nikon Smart | `NikonSmart` | `0000de00-3dd4-4255-8d626dc7b9bd5561` | no | yes | appears but does not pair |
| Canon EOS Smart | `CanonEOSSmart` | `00010000-0000-1000-0000-d8492fffa821` | yes | yes | tested on M6, RP, R6 Mark II, G9 X Mark II |
| Canon EOS Remote | `CanonEOSRemote` | `00050000-0000-1000-0000-d8492fffa821` | yes | no | tested as above |
| Ricoh | `Ricoh` | Ricoh Imaging UUIDs | no | yes | tested on GR IV 1.20 |
| Panasonic LUMIX | `Lumix` | Panasonic manufacturer data | no | yes | untested |
| DJI Osmo Action | `DJIOsmo` | DJI R SDK | no | no | untested |
| FauxNY | `FauxNY` | software peer | yes | yes | test double |

Support claims and the per-model tested list are in
[supported hardware](../supported-hardware.md#cameras).

## Advertisement matching

| Vendor | Match rule | Citation |
| :--- | :--- | :--- |
| Nikon | manufacturer data present, or the service UUID advertised. Company id 0x0399 | `lib/furble/protocol/AdvertisementProtocol.h:12,42` |
| Nikon reconnect | a 32-bit device id inside the manufacturer data | `lib/furble/protocol/AdvertisementProtocol.h:19,44` |
| Sony | company id 0x012d and camera type 0x0003, plus the model and mode bytes | `lib/furble/protocol/AdvertisementProtocol.h:14-15,23-34` |
| Panasonic LUMIX | company id 0x003a with a 5-byte address field | `lib/furble/protocol/AdvertisementProtocol.h:13,36-40` |
| DJI | its own manufacturer data pattern | `lib/furble/protocol/AdvertisementProtocol.h:52` |
| Ricoh | the advertised name | `lib/furble/protocol/AdvertisementProtocol.h:53` |

The parsers are byte-exact and pure, so the host tests and the fuzzer exercise
the same code the firmware runs
(`lib/furble/protocol/AdvertisementProtocol.cpp`).

## Sony

| Item | Value | Citation |
| :--- | :--- | :--- |
| Primary service | `8000cc00-cc00-ffff-ffffffffffff` | `lib/furble/Sony.h:59` |
| Control service | `8000ff00-ff00-ffff-ffffffffffff` | `lib/furble/Sony.h:62` |
| Control characteristic | `0xff01` | `lib/furble/Sony.h:63` |
| Focus up | `0x0601` | `lib/furble/Sony.h:74` |
| Focus down | `0x0701` | `lib/furble/Sony.h:75` |
| Shutter up | `0x0801` | `lib/furble/Sony.h:76` |
| Shutter down | `0x0901` | `lib/furble/Sony.h:77` |
| Geo service | `8000dd00-dd00-ffff-ffffffffffff` | `lib/furble/Sony.h:66` |
| Geo characteristics | `0xdd01` status, `0xdd11` update, `0xdd21` info, `0xdd30` allow, `0xdd31` enable | `lib/furble/Sony.h:67-71` |
| Geo packet prefix | `00 5d 08 02 fc 03 00 00 10 10 10`, then 1e-7 latitude and longitude, time, 65 zero bytes, then UTC and DST offsets in minutes | `lib/furble/Sony.h:41-56` |

A command is written as a 16-bit value with response
(`lib/furble/Sony.cpp:110-128`). The geo handshake writes allow then enable,
reads both back, and treats a readback mismatch as a failure
(`lib/furble/Sony.cpp:137-160`).

## Nikon

| Item | Value | Citation |
| :--- | :--- | :--- |
| Service | `0000de00-3dd4-4255-8d626dc7b9bd5561` | `lib/furble/NikonBase.cpp:11` |
| Remote pair characteristic | `0x00002087` | `lib/furble/NikonRemote.cpp:11` |
| Remote indication characteristic | `0x00002084` | `lib/furble/NikonRemote.h:41` |
| Remote shutter characteristic | `0x00002083` | `lib/furble/NikonRemote.h:42` |
| Smart pair characteristic | `0x00002000` | `lib/furble/NikonSmart.cpp:27` |
| Smart NOT1 characteristic | `0x00002008` | `lib/furble/NikonSmart.h:60` |
| Smart geo characteristic | `0x00002007` | `lib/furble/NikonSmart.h:62` |
| Shutter press | `{0x02, 0x02}` | `lib/furble/NikonRemote.h:49,55`, `lib/furble/NikonRemote.cpp:89-92` |
| Shutter release | `{0x02, 0x00}` | `lib/furble/NikonRemote.h:56`, `lib/furble/NikonRemote.cpp:94-97` |

The pairing message is `{stage, timestamp U8, id or serial[8]}` and the handshake
runs four stages, 0x01 to 0x04, each with a 10 s wait polled every 250 ms
(`lib/furble/NikonBase.h:38-58`, `lib/furble/NikonBase.cpp:101-132`).

The `id_t` pair is the 32-bit device id that appears in the reconnect
manufacturer data plus a nonce. The same 8 bytes read as the body serial
(`lib/furble/NikonBase.h:36-56`).

## Canon

| Item | Value | Citation |
| :--- | :--- | :--- |
| Smart name and pair result characteristic | `00010006-0000-1000-0000-d8492fffa821` | `lib/furble/CanonEOSSmart.h:49` |
| Smart identity characteristic | `0001000a-...` | `lib/furble/CanonEOSSmart.h:51` |
| Smart mode service and characteristic | `00030000-...` and `00030010-...` | `lib/furble/CanonEOSSmart.h:53-55` |
| Smart shutter characteristic | `00030030-...` | `lib/furble/CanonEOSSmart.h:59` |
| Smart geo service | `00040000-...`, data `00040002`, indication `00040003` | `lib/furble/CanonEOSSmart.h:62-68` |
| Remote identity characteristic | `00050002-...` | `lib/furble/CanonEOSRemote.h:39` |
| Remote control characteristic | `00050003-...` | `lib/furble/CanonEOSRemote.h:42` |

The Smart pairing sequence writes `01` plus the furble name to `00010006`, then
`03` plus a generated UUID, `04` plus the name and `05` plus `{02}` to
`0001000a`, waits up to 60 s for an accept or reject indication, and commits with
`{01}`. The shutter pair is `{00 01}` press and `{00 02}` release, with
`{00 03}` and `{00 04}` for focus. The Remote path writes one byte: `0x8c` press
and `0x0c` release (`docs/camera-research/fuji-canon-nikon.md:282-330`).

## Fujifilm

The Secure path has its own page with the full service and characteristic table:
[Fujifilm Secure GATT](../camera-research/fujifilm-secure-gatt.md). The Basic
path uses the token pairing service `91f1de68-dff6-466e-8b65ff13b0f16fb8` and
shutter service `6514eb81-4e8f-458d-aa2ae691336cdfac`
(`lib/furble/Fujifilm.h:56`, `lib/furble/FujifilmBasic.h:62`).

The registration gate uses the dedicated `CHR_NOT1` characteristic only: X100VI
sends `01 00` and legacy `02 00` is accepted there for older bodies. Since
`01 00` on `GEOTAG_UPDATE` means a geotag request, the two events are never
conflated (`plans/CLAUDE.md`).

## Ricoh, LUMIX and DJI

| Vendor | Shutter | Focus | GPS | Citation |
| :--- | :--- | :--- | :--- | :--- |
| Ricoh | `OperationRequest {0x01, 0x01}` means capture with autofocus, not a focus-only command | no-op until a distinct operation is verified | yes | `plans/CLAUDE.md`, `lib/furble/Ricoh.h` |
| LUMIX | shutter and GPS with clock | no opcode exists, so focus is disabled | yes | `lib/furble/Lumix.h` |
| DJI Osmo Action | shutter toggles video recording | disabled | not implemented | `lib/furble/DJIOsmo.h` |

### Ricoh standby gate and wake

A GR IV in BLE standby keeps the link up and reports CameraPower `0x01` ON
while `OperationMode` (`1452335A-EC7F-4877-B8AB-0F72E18BB295`, Camera service)
reads `0x02` BLE_STARTUP. A capture write in that state cold boots the body and
can wedge its firmware, so phase 1 of [plan 149](../../plans/149-ricoh-sleep-shutter-gate.md)
requires a fresh `OperationMode` read of `0x00` CAPTURE before every capture.
`CameraPower` (`B58CE84C-0666-4DE9-BEC8-2D27B27B3211`) is not an authorization
source for exactly that reason.

| Step | Behaviour | Source |
| :--- | :--- | :--- |
| Gate | A fresh `OperationMode` read before every shutter press. `CAPTURE` authorizes, `BLE_STARTUP` enters the wake, every other value and a failed read refuse. | `lib/furble/Ricoh.cpp` |
| Wake sequence | Drop the link, keep it down for the settle interval, reconnect to the same bonded peer, re-run the whole connect handshake (identification, security, discovery, subscriptions, state probe). The handshake repeats the connect-path paired-device identity write and the four CCCD subscriptions before any CAPTURE read exists, which is what furble already does on every cold connect to a sleeping body. No characteristic write wakes the body and no capture write happens before the confirming read. | `lib/furble/Ricoh.cpp`, `lib/furble/Camera.cpp` |
| Wake confirmation | One fresh `OperationMode` read every 50 ms until it reports `CAPTURE` inside the probe budget. Another mode ends the attempt immediately. | `lib/furble/Ricoh.cpp` |
| Notifications | Both characteristics notify and furble subscribes to both, but a held or freshly re-established link can repeat a stale value, so a notification is a diagnostic and never authorizes a capture. | `lib/furble/Ricoh.cpp` |
| Behaviour on timeout | Nothing is written, the relinked connection stays up, and the next press retries the wake. A failed reconnect reclaims the client instead, which leaves the camera reported as disconnected. | `lib/furble/Camera.cpp` |

The budgets are design bounds, not measurements: `FURBLE_RICOH_WAKE_SETTLE_MS`
200 ms, `FURBLE_RICOH_WAKE_CONNECT_TIMEOUT_MS` 5000 ms and
`FURBLE_RICOH_WAKE_PROBE_TIMEOUT_MS` 2000 ms in `lib/furble/Ricoh.h`. They bound
different things: the settle is a minimum, the connect timeout covers the GAP
connect only, and the probe budget is checked between reads. The connect
handshake's security step is bounded by the peer and the supervision timeout,
not by the 5 s, so a body that stalls the encryption exchange keeps the shutter
task busy for longer than the three numbers suggest. Whether a reconnect wakes a
real GR IV, whether the body is still connectable after furble terminates the
link, and how long it then needs are all untested on hardware.

## Connection behaviour

| Item | Value | Citation |
| :--- | :--- | :--- |
| Supervision timeout | 5.12 s | `docs/power/wifi-coex.md:169` (`lib/furble/Camera.h:469-472`) |
| Reconnect backoff | exponential from a base delay up to a cap, and a short path when the camera advertises again | `include/FurbleReconnectBackoff.h:14-38` |
| Saved camera identity | vendor type plus address, with the advertised name as a fallback for the rotating-address vendor only | `lib/furble/protocol/CameraListProtocol.h:35-52` |
| Duplicate pairing | refused through `UI::beginPairing()` for a camera already saved | [console commands](../console-commands.md#pairing-a-camera-that-is-already-saved) |

## Open rows

| Missing | Why the repository cannot close it | Document and section needed |
| :--- | :--- | :--- |
| Camera vendor protocol specifications | Every vendor protocol here is reverse engineered or taken from a public app | The vendor's own BLE or SDK document per family, otherwise the capture targets named in `docs/camera-research/` |
| Nikon Smart pairing failure cause | The code records that Smart mode appears but does not pair | Nikon SnapBridge BLE documentation or a protocol capture of a successful Smart pairing |
| DJI R SDK revision | The implementation cites DJI's published R SDK | DJI R SDK document, revision and the command table |
| Ricoh focus operation | No distinct focus opcode has been verified | Ricoh GR BLE protocol capture |
| Sony geo packet remainder | 65 zero bytes are written without a documented meaning | Sony Camera Remote SDK or a capture of the official app |
