# Ricoh GR wireless research

Retrieval date: 2026-09-12

This is research for a future furble camera peer and host/simulator tests. It
does not certify hardware compatibility. Private GATT bytes, ATT status codes,
ordering, timing, and physical shutter/GPS outcomes require a capture from the
exact camera model and firmware. Community implementations are useful leads,
not wildcard compatibility claims.

Related furble design: `plans/159-camera-peer-certification.md`. The inspected
Ricoh implementation matches furble commit
`965f299f0f43c38fe45e4680f1bfb735023533af`. That plan's 2026-08-30 reviews are
design audits, not captures. In particular, it already records the Ricoh
older-family source pin, the GR IV/HDF issue/PR evidence, and the unresolved
GPS UUID/final-byte conflict.

Evidence labels used below: `official-semantic` means Ricoh documents the
user-visible feature but not private bytes; `common-implementation` means a
public implementation or reverse-engineered document reports the behavior;
`current-source` means the inspected furble implementation does it;
`inference/unknown` means it is a test-design conclusion or remains unresolved.
Only exact `hardware-capture` evidence can certify private bytes, ATT status,
timing, or physical outcomes.

## Executive findings

| Model | Official transport and operations | Research status for a peer |
| --- | --- | --- |
| GR II | Wi-Fi AP and GR Remote/Image Sync. No Bluetooth in the official GR II feature matrix. Remote shooting is Wi-Fi HTTP, with power-on unavailable from the app. | Model as Wi-Fi-only until an exact contrary capture exists. |
| GR III | Bluetooth LE 4.2 for pairing, low-bandwidth control, remote shutter, and location transmission; Wi-Fi AP for image transfer/live view and the HTTP API. | BLE shutter is independently implemented, but GR III firmware 1.92/2.10 reportedly rejects the documented BLE Wi-Fi-wake write. |
| GR IIIx | Same official app family and BLE 4.2. Public implementations report the shooting UUIDs are shared with GR III, but WLAN/power GATT layout differs from GR IV and exact GR IIIx captures are implementation-specific. | Start as a separate profile from GR III. Do not inherit handles or timing. |
| GR IV | Bluetooth LE 5.3 and Wi-Fi. GR WORLD documents remote shooting, location, power, transfer, and firmware update. | Keep separate from GR III/IIIx. Furble's current class is intentionally GR IV-only, with exact capture work tracked separately. |

The official GR World feature table says GR IV and GR III/IIIx support
Bluetooth, WLAN, remote shooting, and sending location information, while GR II
supports WLAN and image import but not Bluetooth, remote shooting, or location
transmission. See [Ricoh's GR World page](https://www.ricoh-imaging.co.jp/english/products/app/gr-world/)
and [GR World FAQ](https://www.ricoh-imaging.co.jp/english/support/qa/gr-world/).
That user-visible table does not specify private GATT UUIDs or packet bytes.

## Official Ricoh evidence

### GR III and GR IIIx

The [GR III Operating Manual](https://www.ricoh-imaging.co.jp/english/support/man-pdf/gr-3.pdf)
and [GR IIIx Operating Manual](https://www.ricoh-imaging.co.jp/english/support/man-pdf/gr-3x.pdf)
describe Bluetooth action modes `On anytime`, `On when power is on`, and
`Disable`. Pairing is started in the camera menu; the camera displays its
device name and password/authentication code, and the communication device
enters that code. The GR III manual says up to six communication devices may
be paired. These are official UX/security semantics, not proof of any specific
SMP association method.

The GR III manual's communication-device section documents Image Sync remote
capture, image view/import, and time synchronization. Ricoh's current GR World
page explicitly says remote shutter and self-timer are available over a
Bluetooth connection for GR III/IIIx, only for single-frame drive mode, and
that location information is sent to the camera while connected via Bluetooth.
Ricoh also documents that image transfer uses Wi-Fi. Therefore “Bluetooth
supports remote shutter” does not imply “Bluetooth carries live view or image
files.”

Ricoh's [GR III feature page](https://www.ricoh-imaging.co.jp/english/products/gr-3/feature/04.html)
lists Bluetooth 4.2 BLE and says live-view remote shooting is available with
Image Sync. This is a product-level statement; it does not expose the GATT
profile.

### GR II

The [GR II product/feature page](https://www.ricoh-imaging.co.jp/english/products/gr-2/feature/04.html)
describes GR Remote as a browser application over built-in Wi-Fi that controls
all camera buttons and dials except power-on and internal-flash pop-up. The
[GR II Operating Manual](https://www.ricoh-imaging.co.jp/english/support/man-pdf/gr-2.pdf)
describes Wi-Fi and NFC setup, not Bluetooth pairing. Ricoh's current GR World
FAQ/feature matrix marks GR II Bluetooth, remote shooting, and location
transmission unsupported, while WLAN and image import remain supported.

There is no contradiction between “GR Remote can shoot” and “GR World remote
shooting is unsupported”: these are different official applications and
transport paths. GR Remote is the older GR II Wi-Fi HTTP surface; GR World is
Ricoh's newer cross-generation app and does not expose its Bluetooth remote
feature for GR II.

This means a GR II peer should expose a camera AP and HTTP server, not pretend
that the GR III BLE GATT service exists. GR Remote is a distinct Wi-Fi control
surface. The official material does not specify the HTTP bytes; use a capture
or the open-source HTTP implementations below.

### GR IV

The [GR IV Operating Manual](https://www.ricoh-imaging.co.jp/resources/english/support/man-pdf/gr-4_en.pdf)
and [GR IV product page](https://us.ricoh-imaging.com/product/gr-iv/) document
Bluetooth remote shooting, image view/import, time synchronization, power
control, and GR WORLD integration. The specifications page lists Bluetooth
5.3 BLE and Wi-Fi 2.4/5.2/5.8 GHz variants. Ricoh's GR IV FAQ says that with
wireless communication enabled, the camera can continue transmitting a
Bluetooth signal while powered off and can be connected from GR WORLD to
perform remote shooting.

The official manuals expose no GR IV UUIDs, handles, authentication bytes,
write payloads, GPS structure, or timeout bounds. Those remain capture-only
facts.

## Community BLE specification

Primary community source: [`dm-zharov/ricoh-gr-bluetooth-api` at
8c55b79928295f4c0f9a8b0f6f4e1015aeb3d016](https://github.com/dm-zharov/ricoh-gr-bluetooth-api/tree/8c55b79928295f4c0f9a8b0f6f4e1015aeb3d016),
retrieved 2026-09-12. The repository describes itself as an unofficial,
reverse-engineered list for GR II/III/IIIx, G900SE/WG-M2, and several Pentax
bodies. It is licensed under the Unlicense. It is not model/firmware-qualified
and does not provide capture manifests.

Exact files and useful fields:

- `characteristics_list.md`: service/characteristic read/write/notify matrix.
- `camera_information/*.md`: the custom Camera Information service
  `9A5ED1C5-74CC-4C50-B5B6-66A48E7CCFF1`, including the readable model
  characteristic `35FE6272-6AA5-44D9-88E1-F09427F51A71` and firmware revision
  characteristic `B4EB8905-7411-40A6-A367-2834C2157EA7`. These are useful
  identity gates, but the document does not bind them to one firmware.
- `camera/camera_power.md`: Camera service
  `4B445988-CAA0-4DD3-941D-37B4F52ACA86`, power characteristic
  `B58CE84C-0666-4DE9-BEC8-2D27B27B3211`, values `0=Off`, `1=On`, `2=Sleep`,
  read/write/notify.
- `camera/operation_mode.md`: same Camera service, operation mode
  `1452335A-EC7F-4877-B8AB-0F72E18BB295`, values `0=Capture`, `1=Playback`,
  `2=BLE Startup`, `3=Other`, `4=PowerOffTransfer`, read/write/notify.
- `shooting/operation_request.md`: Shooting service
  `9F00F387-8345-4BBC-8B92-B87B52E3091A`, write-only operation characteristic
  `559644B8-E0BC-4011-929B-5CF9199851E7`. Two bytes are
  `{OperationCode, Parameter}`: `0/1/2` = NOP/start/stop, and `0/1/2` = no
  AF/AF/green-button function.
- `shooting/capture_status.md`: notify/read capture status
  `B5589C08-B5FD-46F5-BE7D-AB1B8C074CAA`, with capturing/countdown/still/movie
  fields and Type 3 pre-capture result fields. This is a state signal, not a
  guarantee that a physical file was recorded.
- `shooting/self_timer.md`: write-only self-timer
  `009A8E70-B306-4451-B943-7F54392EB971`; values include countdown cancel and
  remote-control setting.
- `gps_control_command/gps_information.md`: service
  `84A0DD62-E8AA-4D0F-91DB-819B6724C69E`, characteristic
  `28F59D60-8B8E-4FCD-A81F-61BDB46595A9`. The document describes three float64
  coordinates, a little-endian int16 year, month/day/hour/minute/second, and a
  final WGS84 datum byte `0`. It says all fields except year are big-endian and
  the timestamp is UTC acquisition time. The page labels altitude/year ranges
  as “TBC”.
- `camera/geo_tag.md`: Camera service GEO-tag enable characteristic
  `A36AFDCF-6B67-4046-9BE7-28FB67DBC071`, sint8 `0/1`, read/write/notify.
- `wlan_control_command/network_type.md`: WLAN service
  `F37F568F-9071-445D-A938-5441F2E82399`, network type characteristic
  `9111CDD0-9F01-45C4-A2D4-E09E8FB0424D`, sint8 `0=OFF`, `1=AP mode`,
  read/write/notify.
- `wlan_control_command/ssid.md` and `passphrase.md`: UTF-8 read/write
  characteristics `90638E5A-E77D-409D-B550-78F7E1CA5AB4` and
  `0F38279C-FE9E-461B-8596-81287E8C9A81`.
- `wlan_control_command/channel.md`: `51DE6EBC-0F22-4357-87E4-B1FA1D385AB8`,
  `0=Auto`, `1..11` channel.
- `bluetooth_control_command/ble_enable_condition.md`: service
  `0F291746-0C80-4726-87A7-3C501FD3B4B6`, characteristic
  `D8676C92-DC4E-4D9E-ACCE-B9E251DDCC0C`, values `0=Disable`, `1=On anytime`,
  `2=On when power is on`, read/write/notify.

The operation write `{01,01}` is the most useful cross-source hypothesis. It
is not a GR III/IIIx/GR IV universal guarantee. The source's “supported models”
header is a family claim, while property availability is explicitly allowed to
differ by model.

## GR III-specific implementation evidence

[`Nielk74/ricoh-gr3-android` at
2da1a822e2945da507ead5dfd2d4ed95dee26bda](https://github.com/Nielk74/ricoh-gr3-android/tree/2da1a822e2945da507ead5dfd2d4ed95dee26bda)
was retrieved 2026-09-12. It has no repository license, so it is citation-only
and must not be copied into furble. Exact files:

- `app/src/main/java/com/ricohgr3/app/ble/RicohGattProfile.kt` repeats the
  Shooting UUID and `{01, AF/no-AF}` encoder and lists GR III/IIIx WLAN UUIDs.
- `app/src/main/java/com/ricohgr3/app/ble/CameraBleManager.kt` serializes GATT
  operations in one queue, scans by camera name, discovers services, reads
  device information, writes the operation request, and uses an 8-second
  no-callback watchdog. This is client robustness behavior, not camera timing.
- `research/FEASIBILITY.md` says the author considers BLE shutter feasible and
  records the complementary BLE/Wi-Fi model.
- `research/BLE_WIFI_WAKE_INVESTIGATION.md` reports direct tests on a GR III
  with firmware 1.92 and 2.10. The report says BLE pairing, identity reads,
  Wi-Fi credential reads, and `{01,01}` shutter writes worked, while Network
  Type `1` was rejected with application-specific GATT error `0x80`; a two-byte
  Network Type value was rejected for invalid length. It also reports the
  camera UI message “Can't turn it on in this mode” for the same attempted
  Wi-Fi activation and says SSID/passphrase writes were accepted. This is a
  self-reported hardware investigation without raw capture files, so treat it
  as strong common-implementation evidence, not certification.

The same investigation reports that the GR III camera displays a six-digit
passkey and that a bond is mandatory for useful GATT operations. This conflicts
with any fixed-passkey assumption. The exact SMP association, key material, and
whether every firmware revision behaves identically remain unknown.

## Other implementations and Wi-Fi evidence

### M5Stack firmware

[`ndreij/RICOH-GR-Live-View-Shooting` at
0384176cbd3420a9e630b0264239e26387933333](https://github.com/ndreij/RICOH-GR-Live-View-Shooting/tree/0384176cbd3420a9e630b0264239e26387933333)
is GPL-3.0, retrieved 2026-09-12. The exact protocol document is
`docs/ricoh_ble_protocol.md`. It reports GR IIIx real-device validation and
states that GR III (non-x) remained unverified at that revision. It also
documents a GR IV legacy fixed-handle profile and a separate GR IIIx layout:
the GR IIIx WLAN/power handles differ, while the Shooting service UUIDs and
operation write are reported as shared. The document says GR II uses manual
Wi-Fi, HTTP, and MJPEG only. This is useful for profile partitioning, but its
GR IIIx/GR IV claims are not a substitute for furble's exact capture corpus.

The upstream [`sky18Dragon/RICOH-GR-Live-View-Shooting` at
4d1aa81b92409d91399331d2b4a776c009ab7a8b](https://github.com/sky18Dragon/RICOH-GR-Live-View-Shooting/tree/4d1aa81b92409d91399331d2b4a776c009ab7a8b)
is also GPL-3.0. Its README explicitly separates GR II Wi-Fi-only from GR
III/IV BLE paths and says GR III/IIIx/GR IV/HDF require different generation
profiles. It is a useful independent implementation lead, not permissively
importable source.

### Wi-Fi HTTP

[`CursedHardware/ricoh-wireless-protocol` at
2dc435f73ed127aa6a7f4b991f38f1d1a74b73d7](https://github.com/CursedHardware/ricoh-wireless-protocol/tree/2dc435f73ed127aa6a7f4b991f38f1d1a74b73d7)
was retrieved 2026-09-12. The repository has no declared license. It says its
OpenAPI and definitions were extracted/reverse-engineered from Image Sync
2.1.17. Exact files include:

- `openapi.yaml`: HTTP endpoints rooted at `/v1/`, including `/photos`,
  `/photos/{folder}/{file}`, `/changes`, `/liveview`, `/props`,
  `/camera/shoot`, `/lens/focus`, `/params/camera`, `/ping`,
  `/device/wlan/finish`, and `/device/finish`.
- `definitions/camera_ricoh_gr_iii_ble.yaml` and
  `definitions/camera_ricoh_gr_iiix_ble.yaml`: BLE semantic settings such as
  date/time, SSID/passphrase/channel, GEO enable, GPS information, BLE enable
  condition, and operation mode.
- `definitions/camera_ricoh_gr_iii.yaml` and
  `definitions/capture_ricoh_gr_iii.yaml`: per-model HTTP properties and
  capture enums. The GR III/IIIx HTTP definitions are distinct from BLE
  definitions.

[`clyang/GRsync` at
8e2a17484e49f512f99e3fd6ffa493a4fd0685a3](https://github.com/clyang/GRsync/tree/8e2a17484e49f512f99e3fd6ffa493a4fd0685a3)
is MIT, retrieved 2026-09-12. `GRsync.py` uses `http://192.168.0.1/`,
`v1/photos`, `v1/props`, and `v1/device/finish` to sync GR II/III photos. Its
README says GR II and GR III/IIIx use Wi-Fi and that GR II is limited to 20 MHz
802.11n. This independently supports the AP/HTTP plane, but it does not prove
BLE behavior or remote shutter semantics.

## Discrepancies with the current furble Ricoh class

The inspected source is [Ricoh.cpp](https://github.com/MaxRink/furble/blob/965f299f0f43c38fe45e4680f1bfb735023533af/lib/furble/Ricoh.cpp)
and [Ricoh.h](https://github.com/MaxRink/furble/blob/965f299f0f43c38fe45e4680f1bfb735023533af/lib/furble/Ricoh.h)
at that exact commit; no implementation files were modified.

1. The class documentation says GR IV-only and `docs/supported-hardware.md`
   says GR III/GR II do not work. The matcher nevertheless accepts any device
   advertising the Info, Camera, Shooting, or Bluetooth Control service, or a
   Ricoh-like name. It then requires the Shooting service and uses the shared
   UUID set. That broad matcher is not an older-model compatibility proof. A
   future peer should select an exact model/firmware profile after identity and
   GATT discovery, rather than treat a service-name match as GR IV.
2. Pairing callbacks return/inject fixed passkey `123456` and automatically
   confirm the displayed value. Official GR III/IIIx UX displays a camera-side
   authentication code; the GR III hardware report says the code is dynamic.
   Keep this as a known compatibility risk until an exact furble/NimBLE SMP
   capture proves the method.
3. The current shutter path writes Shooting Flavor `0` and then
   Operation Request `{1,1}`. That matches the community operation-request
   hypothesis and the independent GR III implementation. The implementation
   targets GR IV; this research establishes no capture-backed certification.
   `captureAllowed()` gates on a fresh Operation Mode read and
   refuses BLE Startup/other modes. This is a sensible safety policy, not an
   official byte-level rule for every GR generation.
4. The current GPS packet is 32 bytes: three byte-swapped float64 values, a
   little-endian year, month/day/hour/minute/second, and centiseconds. The
   pinned older-family document describes the same 32-byte footprint but calls
   the final byte WGS84 datum `0`, with no centisecond. The field meaning and
   location-enable characteristic are unresolved. Specifically, furble uses
   `F37F568F-9071-445D-A938-5441F2E82399` / `9111CDD0-9F01-45C4-A2D4-E09E8FB0424D`
   for location enable, while the community document names that pair WLAN
   Network Type; its separate GEO-tag enable characteristic is
   `A36AFDCF-6B67-4046-9BE7-28FB67DBC071`. The GPS Information UUID itself
   matches the pinned document. Do not call current GPS parity certified.
5. Current source enables location control before its first GPS write and
   throttles subsequent writes to 10 seconds plus movement thresholds. Those
   are furble policy choices. The official documents only establish that
   location transmission exists, not this cadence or payload.
6. Current source treats power `0/1/2` and operation mode `0..4` as state
   values and subscribes to power/mode/capture notifications. The values align
   with the community document, but notification ordering, CCCD requirements,
   and errors remain model/firmware-specific.

## Future simulator and host-test targets

Use `plans/159-camera-peer-certification.md`'s provenance classes and feature
scoping. The minimum useful peers are separate, exact profiles:

### GR II Wi-Fi peer

- Advertise no Ricoh BLE control service.
- Emulate the camera AP at `192.168.0.1` and only the HTTP endpoints backed by
  a GR II capture or explicitly marked common-implementation data.
- Model manual Wi-Fi activation, HTTP remote capture/transfer, and the fact that
  app power-on is unavailable. Keep GR Remote HTTP and GR III BLE operations in
  different transports.

### GR III and GR IIIx BLE peers

- Keep model profiles separate even if Shooting UUIDs are initially equal.
  Include exact firmware, GATT digest, characteristic properties, and
  advertisement identity when captured.
- Model camera-initiated pairing with dynamic passkey input and a camera-side
  bond store. Saved reconnect must not be treated as first pairing.
- Exercise discovery, identity reads, credential reads, subscriptions, and
  operation write as serialized events. Test `{01,00}` no-AF and `{01,01}` AF
  separately, then model capture-status notification and physical outcome as
  separate evidence fields.
- Keep BLE shutter and Wi-Fi AP/HTTP as separate sessions. For GR III firmware
  1.92/2.10, represent Network Type `1` rejection `0x80` only as a
  source-labelled/capture-labelled behavior, not as a universal GR III rule.
  A synthetic accepted-wake variant must remain uncertified.
- Add negative cases for unbonded GATT operations, invalid one-byte lengths,
  missing characteristics, stale operation mode, and link loss. Exact ATT
  status and disconnect timing require captures.

### GR IV peer

- Do not inherit GR III/IIIx WLAN handles, wake behavior, or GPS field meaning.
- Keep GR IV and GR IV HDF as exact profiles until a capture demonstrates the
  same firmware/GATT/physical behavior.
- Exercise the current fresh-operation-mode safety gate, power/standby
  notifications, single-write shutter, location-enable ordering, and GPS to
  EXIF result only once raw captures and physical outcomes are available.

### Timing, errors, and certification

- The Android implementation's 8-second watchdog is a host-client timeout,
  not camera protocol timing. Use it to test “no callback cannot spin forever,”
  not to certify a camera response deadline.
- Record ATT errors (including the reported `0x80` application error and
  invalid length) with model, firmware, precondition, write type, and response
  mode. Do not convert one implementation's error into every camera peer.
- A successful ATT write is not a successful exposure. Require a capture-status
  change and physical image/EXIF outcome for `PASS_CERTIFIED`; otherwise return
  `UNCERTIFIED`.
- Do not copy source code from the no-license Nielk or CursedHardware repos, or
  GPL M5Stack implementations. Clean-room, original reimplementation may cite
  their behavior. The Unlicense `dm-zharov` documentation and MIT GRsync are
  permissive, but source provenance and model scope still belong in fixtures.

## Open questions to resolve with captures

- Exact GR III and GR IIIx GATT databases per firmware, including whether all
  service UUIDs/properties are shared and which WLAN/power handles differ.
- Exact SMP association: passkey entry/display direction, numeric comparison,
  bond persistence, key size, and reconnect behavior on camera-side bond reset.
- Whether GR III's Network Type rejection is universal, state-dependent, or an
  official-app session handshake requirement.
- Operation Mode write permissions and shutter behavior in Capture, Playback,
  BLE Startup, Other, and PowerOffTransfer for each model.
- Capture Status notification bytes, CCCD requirements, and physical image
  outcome timing.
- GPS characteristic identity and the final payload byte: centisecond versus
  WGS84 datum, plus coordinate byte order and timestamp semantics.
- BLE-to-Wi-Fi coexistence, AP activation ownership, and exact HTTP behavior
  for GR III, GR IIIx, GR II, GR IV, and HDF variants.
- Firmware/region differences and official-app behavior after future firmware
  updates.

## Source index

- [GR III Operating Manual](https://www.ricoh-imaging.co.jp/english/support/man-pdf/gr-3.pdf)
- [GR IIIx Operating Manual](https://www.ricoh-imaging.co.jp/english/support/man-pdf/gr-3x.pdf)
- [GR II Operating Manual](https://www.ricoh-imaging.co.jp/english/support/man-pdf/gr-2.pdf)
- [GR IV Operating Manual](https://www.ricoh-imaging.co.jp/resources/english/support/man-pdf/gr-4_en.pdf)
- [Ricoh GR World](https://www.ricoh-imaging.co.jp/english/products/app/gr-world/)
- [Ricoh GR World FAQ](https://www.ricoh-imaging.co.jp/english/support/qa/gr-world/)
- [dm-zharov/ricoh-gr-bluetooth-api, Unlicense, pinned commit](https://github.com/dm-zharov/ricoh-gr-bluetooth-api/tree/8c55b79928295f4c0f9a8b0f6f4e1015aeb3d016)
- [Nielk74/ricoh-gr3-android, no declared license, pinned commit](https://github.com/Nielk74/ricoh-gr3-android/tree/2da1a822e2945da507ead5dfd2d4ed95dee26bda)
- [ndreij/RICOH-GR-Live-View-Shooting, GPL-3.0, pinned commit](https://github.com/ndreij/RICOH-GR-Live-View-Shooting/tree/0384176cbd3420a9e630b0264239e26387933333)
- [sky18Dragon/RICOH-GR-Live-View-Shooting, GPL-3.0, pinned commit](https://github.com/sky18Dragon/RICOH-GR-Live-View-Shooting/tree/4d1aa81b92409d91399331d2b4a776c009ab7a8b)
- [CursedHardware/ricoh-wireless-protocol, no declared license, pinned commit](https://github.com/CursedHardware/ricoh-wireless-protocol/tree/2dc435f73ed127aa6a7f4b991f38f1d1a74b73d7)
- [clyang/GRsync, MIT, pinned commit](https://github.com/clyang/GRsync/tree/8e2a17484e49f512f99e3fd6ffa493a4fd0685a3)
