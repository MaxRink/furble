# 61: Camera compatibility survey

Status: research complete, Lumix implementation ported in plan 72 and
UNTESTED. All cited links were fetched and content verified during this survey
(August 2026).

Update 2026-08-21: expanded from a new-vendor feasibility survey into a
full reference catalog plus a feature-expansion gap analysis for the
already-supported vendors. New sections added below the original survey:

- "Reference catalog" documents every camera BLE and BT protocol resource
  that can serve as a base, with per-entry fields (vendors and models,
  transport, what it documents, source maturity, license, activity, URL).
- "Existing vendor feature expansion" gap-analyses what Fujifilm, Canon,
  Nikon, Sony, and Ricoh expose over BLE beyond furble's current shutter,
  focus, and geotag: bulb, video, zoom, settings read and write, time sync,
  richer geotag, and telemetry.
- "Prioritized expansion backlog" ranks both new-vendor and
  existing-vendor work by value, effort, and risk, with cited sources.

## Purpose

Map BLE camera remote protocols across vendors against current lib/furble
coverage. Rank the most feasible additions. Identify which can be tested
with a FauxNY-style fake or an emulated GATT server, and which need real
hardware.

## Implementation state

Plan 72 ports the Lumix implementation from upstream PR 282 onto the fork's
current vendor-class API. `Camera::Type::PANASONIC_LUMIX` is value 10, and
`CameraList` registers the saved-camera and scan paths. The current fork does
not have the upstream `Camera::toUnixTime()` helper, so the Lumix class keeps
the equivalent conversion local and leaves the current connection and power
API unchanged.

The companion wire model has no camera-type field or vendor mapping, so it is
unchanged. No user setting is needed for Lumix support. The next free settings
wire ID remains 41.

The S5II first-pair path may still require a WiFi-assisted Lumix Sync handshake
on the camera. The port only implements the MEI0 path seen in S5II and BGH1
traces. The G9II-generation XOR login is not implemented. Lumix support is
UNTESTED because this workspace has no Lumix hardware. Verification is limited
to code review and the FauxNY-style app review.

## Current lib/furble coverage

One class per vendor mode. From the code on fork master:

| Class | Pairing | Focus | Shutter | Geotag |
|---|---|---|---|---|
| CanonEOSRemote | bond, BR-E1 style | yes | yes | no (remote mode has none) |
| CanonEOSSmart | bond + on-camera confirm | no-op | yes | yes, camera requested |
| FujifilmBasic | no bond, 4-byte adv token | yes | yes | yes, camera requested |
| FujifilmSecure | bond, serial rematch on scan | yes | yes | yes, camera requested |
| NikonRemote | 4-stage handshake, no bond | no-op | yes | no |
| NikonSmart | Blowfish handshake, broken | no-op | no-op | code present, unreachable |
| Ricoh | MITM passkey bond | unsupported (no half-press command) | yes, capture with AF | yes, rate limited |
| Sony | bond | yes | yes | yes, allow/enable gated |
| FauxNY | fake | logs | logs | logs |

Known gaps in supported vendors:

- Canon: remote mode gives focus but no GPS. Smart mode gives GPS but no
  focus. The protocol forces the choice (upstream issue 189).
- NikonSmart: full pairing swaps to Bluetooth Classic mid-handshake.
  NimBLE is LE only, so connect always fails. Upstream issue 257 has a
  working PoC using a key sniffed from SnapBridge plus a patched Bluedroid
  stack. gkoh found Bluedroid breaks Fujifilm/Sony coexistence (BLE
  privacy limits) and is considering a Nikon-only build.
- FujifilmBasic: frequent disconnects on GFX100 during shutter (upstream
  issue 221, open).

## Vendor survey

| Vendor | Protocol style | Pairing model | Shutter/focus known | Geotag over BLE | furble status | Feasibility | Best source |
|---|---|---|---|---|---|---|---|
| Canon (BR-E1) | GATT, bitfield byte to one control chr | BLE bond + identify write | yes/yes | no | supported | done | [iandouglasscott.com](https://iandouglasscott.com/2018/07/04/canon-dslr-bluetooth-remote-protocol/), [robot9706/CanonBLEIntervalometer](https://github.com/robot9706/CanonBLEIntervalometer) |
| Canon (smart) | GATT, 4 services on d8492fffa821 base | bond + camera confirm | yes/no | yes | supported | done | [gkoh/furble#189](https://github.com/gkoh/furble/issues/189) |
| Nikon (ML-L7 remote) | GATT, 4-stage handshake, mode+cmd bytes | app-layer handshake, no bond | yes/no | no | supported | done | [gkoh/furble#209](https://github.com/gkoh/furble/issues/209) |
| Nikon (SnapBridge smart) | GATT, Blowfish challenge over de00 service | BLE handshake then BT Classic swap | partial | yes, 41-byte DMS packet | broken (LE-only stack) | blocked | [skyblond.info](https://skyblond.info/archives/1115.html), [hurui200320/nsg](https://github.com/hurui200320/nsg), [gkoh/furble#257](https://github.com/gkoh/furble/issues/257) |
| Sony | GATT, FF00 control svc, 16-bit opcodes | bond, adv flags gate pairing | yes/yes | yes, DD00 svc, 95-byte packet | supported | done | [coral/freemote](https://github.com/coral/freemote), [whc2001/ILCE7M3ExternalGps](https://github.com/whc2001/ILCE7M3ExternalGps/blob/master/PROTOCOL_EN.md) |
| Fujifilm (basic + secure) | GATT, two-write shutter commands | token (basic) or bond + serial (secure) | yes/yes | yes, pull model, 23-byte packet | supported | done | [furble wiki](https://github.com/gkoh/furble/wiki/Protocol-Documentation), [gkoh/furble#208](https://github.com/gkoh/furble/issues/208) |
| Panasonic Lumix | GATT, 1-byte commands, MEI0 magic handshake | none on S5 gen (no SMP at all); XOR login on G9II gen | yes/yes | yes, 16-byte packet, official feature | missing (upstream PR 282 open, untested) | high | [tobiasbrummer/lux-lat-long-log](https://github.com/tobiasbrummer/lux-lat-long-log), [Aikhjarto/LUMIX-G9II-Remote-Control](https://github.com/Aikhjarto/LUMIX-G9II-Remote-Control), [gkoh/furble#247](https://github.com/gkoh/furble/issues/247) |
| Pentax K (via Ricoh) | same Ricoh Imaging GATT family as GR | MITM passkey bond | documented, unverified on K bodies | documented | partial (name match accepts PENTAX, protocol untested) | medium | [dm-zharov/ricoh-gr-bluetooth-api](https://github.com/dm-zharov/ricoh-gr-bluetooth-api) |
| Ricoh GR IV | GATT, ShootingFlavor + OperationRequest | MITM passkey bond | yes, single-write capture with AF; no focus-only operation | yes | supported; GR III/IIIx remain incompatible | done | [dm-zharov/ricoh-gr-bluetooth-api](https://github.com/dm-zharov/ricoh-gr-bluetooth-api), [sotashimozono/gr3sync](https://github.com/sotashimozono/gr3sync) |
| OM System / Olympus | unknown GATT, nothing public | camera shows name + passcode, QR in app | functionally confirmed, bytes unknown | yes per manual, bytes unknown | missing | low (needs original RE) | [OM-1 II manual](https://learning.omsystem.com/OM-1MarkII/zz_html_manual/en/shooting_remotely_remote_shutter_280.html) |
| Sigma (fp, fp L, BF) | none, no Bluetooth radio | n/a | n/a | n/a | impossible | none | [sigma-global fp specs](https://www.sigma-global.com/en/cameras/fp/#specifications) |
| GoPro (HERO 9+) | official Open GoPro BLE API, TLV + protobuf | standard bond, 0xFEA6 adv filter | shutter yes (no focus concept) | no location push | missing | medium | [Open GoPro BLE docs](https://gopro.github.io/OpenGoPro/docs/ble/protocol/ble_setup) |
| Insta360 (X3, RS) | reversed roles: camera connects to the remote | camera pairs to a device named "Insta360 GPS Remote" | shutter yes | partial, encoding unresolved | missing | low (needs peripheral role) | [Chwalek writeup](https://medium.com/@patrickchwalek/ble-control-of-insta360-cameras-7bf6894648a4), [pchwalek/insta360_ble_esp32](https://github.com/pchwalek/insta360_ble_esp32) |
| DJI (Osmo Action 4+) | official DJI R SDK framing, 0xAA SOF, CRC16/32 | bond to nearest camera in pairing mode | record start/stop yes | yes, official 10 Hz GPS push | missing | medium-high | [dji-sdk/Osmo-GPS-Controller-Demo](https://github.com/dji-sdk/Osmo-GPS-Controller-Demo), [protocol.md](https://github.com/dji-sdk/Osmo-GPS-Controller-Demo/blob/main/docs/protocol.md) |
| DJI (Pocket 3) | DUML frames over fff0 service, community RE | DUML pairing flow | partially mapped | unknown | missing | low | [yigitkonur/lib-osmo-ble](https://github.com/yigitkonur/lib-osmo-ble) |

Upstream demand check: issues exist for Panasonic (247, open) and Nikon
smart (257, open). Nobody has ever requested OM System, Pentax K, Sigma,
GoPro, Insta360, DJI, Hasselblad, or Leica upstream.

## Ranked feasible additions

1. Panasonic Lumix. The work is done: upstream PR 282 (by gkoh, open,
   untested) adds lib/furble/Lumix from HCI traces of the S5II and BGH1.
   Fork action: track the PR, rebase onto fork master, review against the
   two external repos. Risks: a suspected WiFi-assisted first-pair
   handshake on S5II-class bodies, and the G9II-generation XOR login that
   PR 282 does not implement. Effort: days (port and review), hardware
   needed to close.
2. Pentax K series. Possibly free. The Ricoh Imaging GATT family spec
   lists K-1, K-3 III, K-70, KF, KP alongside the GR line, and the fork's
   Ricoh::nameMatches already accepts "PENTAX". Nobody has confirmed a K
   body accepts OperationRequest. Effort: near zero code, but any support
   claim must be declared untested. A single K-3 III owner test settles it.
3. DJI Osmo Action (4 and newer). Officially documented protocol with an
   official ESP32 BLE remote demo, including 10 Hz GPS push for in-camera
   geotagging. Clean fit for furble's GPS feature. Effort: medium, a new
   framed binary protocol with CRC16/CRC32, but no reverse engineering
   risk. New Camera::Type and class per the Sony template.
4. GoPro (HERO 9 and newer). Official Open GoPro BLE API. Shutter and
   record are simple TLV commands. No location push, so no geotag story,
   which weakens the fit. Effort: medium-low for shutter only.
5. Insta360. Shutter bytes are known but the pairing model is inverted:
   the camera connects to a peripheral advertising a magic name. furble is
   a NimBLE central and the fork removed BLE advertising. Supporting this
   means adding a peripheral role alongside the central and the companion
   GATT service. Effort: high relative to value. Park it.

Not rankable today:

- Nikon smart (SnapBridge): blocked on the BT Classic swap. Watch issue
  257. A Nikon-only Bluedroid build is gkoh's current direction. Not a
  fork-side opportunity until the stack question resolves.
- OM System: requires original reverse engineering from an OM Image Share
  HCI snoop or an RM-WR1 sniff. No public protocol material exists. Only
  worth starting if an OM body is available.
- Sigma: no Bluetooth radio in fp, fp L, or BF. Wired or USB release only.
  Permanently out of scope.

## Testing: fake versus hardware

The FauxNY class fakes the app-layer Camera interface only. It validates
UI, control queue, and multiconnect behavior for any new vendor for free.
Protocol-level testing differs per vendor:

- Emulatable with a fake GATT server (plans/28-emulator direction):
  Panasonic S5 generation (no bonding, static MEI0 handshake, documented
  packets), GoPro and DJI Action (official specs make a faithful emulator
  possible). These can reach high confidence before touching hardware.
- Hardware required: Pentax K (the open question is camera-side
  acceptance, an emulator proves nothing), Panasonic final verification
  (PR 282 failed against a real S5II already), Insta360 (inverted roles),
  OM System (nothing to emulate yet).
- Repo policy still applies: only Fujifilm is hardware-testable here.
  Every other vendor ships as code review plus FauxNY plus declared
  untested, with owner testing recruited through the upstream issue.

## Dependencies on existing plans

- 28-emulator: a vendor-protocol GATT emulator is the natural extension
  and would de-risk Lumix, GoPro, and DJI work.
- 25-multiconnect-ui: new vendors join the 8-slot multiconnect matrix.
  Mixed-vendor behavior needs a row in its test plan.
- 50-companion-app-design: an Insta360 peripheral role would share the
  advertising infrastructure with the companion GATT service. Decide
  jointly if ever pursued.

## Reference catalog

Every protocol resource that can serve as a base for furble camera BLE and
BT work, grouped by vendor. Fields per entry: what it covers, transport,
what it documents, source maturity, and URL. Maturity is one of: official
(vendor spec or SDK), RE-mature (well corroborated reverse engineering),
RE-partial (incomplete or single-source), or undocumented. License and
last-activity were not individually audited during this survey and are
marked "not verified" unless already known; confirm before vendoring any
code.

### Cross-vendor and tooling

- libgphoto2 (gphoto project). Vendors: most DSLR and mirrorless via PTP.
  Transport: USB and PTP/IP over WiFi only, no BLE or GATT. Documents: PTP
  opcodes, vendor PTP extensions. Maturity: official-grade community, very
  active. License: LGPL. Not a BLE source but the canonical PTP reference
  for any future WiFi or USB control.
  https://github.com/gphoto/libgphoto2/tree/master/camlibs/ptp2 ,
  BLE gap tracked at https://github.com/gphoto/libgphoto2/issues/409
- Generic BLE HID selfie shutters ("AB Shutter 3" and clones). Vendors:
  any phone or app that triggers on volume-up. Transport: BLE HID
  peripheral (the remote is the peripheral, sends Volume Up or Enter).
  Documents: HID consumer-control usage. Maturity: RE-mature. This is an
  inverted role for furble (furble is a central); adding it needs a GATT
  server and HID service. code-martin/ABShutter3 decodes the commercial
  remote; michaelruck/ESP32_phone_camera_remote_shutter and
  tyano463/esp32_ble_hid are ESP32 peripheral implementations.
  https://github.com/code-martin/ABShutter3 ,
  https://github.com/michaelruck/ESP32_phone_camera_remote_shutter ,
  https://github.com/tyano463/esp32_ble_hid ,
  https://github.com/MinatsuT/esp32_remote_shutter
- furble emulator direction (plans/28-emulator) and camera test harness
  (plans/36-camera-test-harness). A vendor-protocol GATT emulator would
  de-risk any documented vendor before hardware.

### Fujifilm

- furble itself is the most complete public reverse engineering of the
  Fujifilm BLE remote protocol. https://github.com/gkoh/furble , protocol
  wiki https://github.com/gkoh/furble/wiki/Protocol-Documentation
- petabyt/libfuji, lib/bluetooth.c. Vendors: Fujifilm X and GFX.
  Transport: BLE GATT plus a BLE-triggered WiFi handover. Documents:
  shutter char CHR_SHUTTER_UUID 7fcf49c6, secondary CHR_SHUTTER_UUID2
  600655e6 (opcode 0x04 triggers WiFi), secure pair and status service
  UUIDs, 23-byte geotag with embedded time, serial read from DIS 0x2a26.
  Maturity: RE-mature, active. License: not verified.
  https://github.com/petabyt/libfuji/blob/master/lib/bluetooth.c
- tiredboffin/fffw. Vendors: Fujifilm (X-T4 and others). Transport: BLE
  GATT. Documents: full per-camera GATT dumps (the shutter service
  6514eb81 exposes six writable characteristics: 600655e6, fb15c357,
  7fcf49c6, b1307521 56-byte, 861442ab, 43070f6c), the 5013/5023/5033/5053/
  5063 indication family, the 15ca59fe vitals service, and standard DIS
  0x180a. Its ffbt bt1tg module emulates the TG-BT1 grip (shutter, movie,
  zoom) but the button byte payloads are not in the repo. Maturity:
  RE-mature. License: not verified.
  https://github.com/tiredboffin/fffw/blob/main/ffbt/cfg/gatt-5dcb7a.yaml ,
  https://github.com/tiredboffin/fffw
- missuo/Koko, FujifilmProtocol.swift. Vendors: Fujifilm. Transport: BLE.
  Documents: 23-byte WGS-84 location packet, matches furble and libfuji.
  Maturity: RE-partial. License: not verified.
  https://github.com/missuo/Koko/blob/main/Koko/Models/FujifilmProtocol.swift
- Fujifilm official remote-release manual. Documents behaviors only, not
  bytes: shutter, and bulb (slide up to start exposure, slide down to end).
  Confirms bulb maps onto furble's existing press then release.
  https://app.fujifilm-dsc.com/en/manual/camera_remote/usage/remote_release/
- TG-BT1 grip spec and JJC clone. Documents the button set (shutter,
  start/stop movie, zoom with power-zoom lenses only). Bytes not published.
  https://www.fujifilm-x.com/en-us/products/accessories/tg-bt1/

### Canon

- Ian Douglas Scott, Canon DSLR Bluetooth Remote Protocol. Vendors: Canon
  EOS with BR-E1 support. Transport: BLE GATT. Documents: the single
  control-byte bitfield (mode bits 2-3: 11 immediate 0x0c, 01 2s timer
  0x04, 10 movie 0x08; wide zoom 0x10; tele zoom 0x20; autofocus 0x40;
  shutter 0x80). Maturity: RE-mature, the authoritative writeup.
  https://iandouglasscott.com/2018/07/04/canon-dslr-bluetooth-remote-protocol/
- maxmacstn/ESP32-Canon-BLE-Remote. Transport: BLE GATT on ESP32.
  Documents: service 00050000 base UUIDs and the same bitfield constants,
  passkey 123456 pairing. Maturity: RE-mature. License: not verified.
  https://github.com/maxmacstn/ESP32-Canon-BLE-Remote
- ids1024/cannon-bluetooth-remote (Python/bluez) and pklaus/canoremote
  (Python/bleak, tested EOS 200D) and RReverser/eos-remote-web (Web
  Bluetooth) and robot9706/CanonBLEIntervalometer. Corroborating
  implementations of the same BR-E1 control byte.
  https://github.com/ids1024/cannon-bluetooth-remote ,
  https://github.com/pklaus/canoremote ,
  https://github.com/RReverser/eos-remote-web ,
  https://github.com/robot9706/CanonBLEIntervalometer
- Canon Smart mode (Camera Connect geotag) coverage is in furble already;
  upstream context at https://github.com/gkoh/furble/issues/189
- Canon CCAPI (official developer program) is HTTP over WiFi, not BLE.
  Useful only for a future WiFi path. https://developercommunity.usa.canon.com/

### Nikon

- skyblond.info, replacing SnapBridge with an ESP32. Vendors: Nikon
  SnapBridge bodies and ML-L7. Transport: BLE GATT plus a mandatory BR/EDR
  (Bluetooth Classic) bond for the smart path. Documents: service de00,
  the 4-stage handshake, Blowfish key FF FF AA 55 11 22 33 00 and the 8
  salts, the 41-byte geo packet, the 10-byte time packet, and the recipe
  to complete the BR/EDR bond (scan by advertised name, expose an SPP
  slave, wait ~1s, discard the classic bond after first reconnect).
  Maturity: RE-mature, matches furble byte for byte.
  https://skyblond.info/archives/1115.html
- hurui200320/nsg (Nikon SnapBridge GPS). Corroborating implementation of
  the smart-mode handshake and geo push. License: not verified.
  https://github.com/hurui200320/nsg
- Nikon ML-L7 user manual. Documents the button map (photo/video mode,
  movie record, +/- zoom, Fn1/Fn2, playback), not bytes.
  https://onlinemanual.nikonimglib.com/p1100/en/10-04.html
- furble's own Nikon classes are the working ML-L7 remote reference;
  upstream issues 209 (ML-L7) and 257 (SnapBridge BR/EDR blocker).
  https://github.com/gkoh/furble/issues/257

### Sony

- coral/freemote. Vendors: Sony Alpha and others with the BLE remote.
  Transport: BLE GATT. Documents: control service 8000FF00, write char
  FF01, notify char FF02, and the full command table (focus 0x07 down /
  0x06 up, shutter 0x09 / 0x08, AF-ON 0x15 / 0x14, C1 0x21 / 0x20, movie
  0x0E / 0x0F, zoom tele/wide 0x0244-0x0247 with a step byte, focus step
  0x026A-0x026D), plus FF02 status decodes (focus, shutter, recording).
  Maturity: RE-mature, the richest single source. License: not verified.
  https://github.com/coral/freemote
- whc2001/ILCE7M3ExternalGps, PROTOCOL_EN.md. Documents the DD00 location
  service and 95-byte GPS packet furble uses.
  https://github.com/whc2001/ILCE7M3ExternalGps/blob/master/PROTOCOL_EN.md
- HYPOXIC writeup, arsya/sony_camera_ble_remote (ESP32), Greg Leeds
  writeup. Corroborating the control bytes and framing.
  https://gethypoxic.com/blogs/technical/sony-camera-ble-control-protocol-di-remote-control ,
  https://github.com/arsya/sony_camera_ble_remote ,
  https://gregleeds.com/reverse-engineering-sony-camera-bluetooth/
- Sony Camera Remote SDK (official) is USB and WiFi (PTP-style), not the
  BLE remote channel. Not a BLE source.
  https://support.d-imaging.sony.co.jp/app/sdk/en/index.html

### Ricoh and Pentax

- dm-zharov/ricoh-gr-bluetooth-api. Vendors: RICOH GR II/III/IIIx, G900SE,
  WG-M2, PENTAX K-1, K-3 III, K-3 III Mono, K-70, KF, KP. Transport: BLE
  GATT. Documents: the Camera, Shooting, GPS and Location Control service
  characteristics furble uses, plus a Date/Time (clock set) characteristic
  furble does not yet write, plus Battery Level, Storage Info, Grad ND.
  Maturity: RE-mature. License: not verified.
  https://github.com/dm-zharov/ricoh-gr-bluetooth-api
- sotashimozono/gr3sync. Corroborating GR III geotag client.
  https://github.com/sotashimozono/gr3sync
- Ricoh Theta 360 uses a separate THETA API, not the GR API. Net-new work
  if ever pursued. Ricoh Image Sync app is the official companion.
  https://www.ricoh-imaging.co.jp/english/products/app/image-sync2/

### Panasonic Lumix

- gkoh/furble PR 282 (open, untested) adds lib/furble/Lumix from HCI traces
  of the S5II and BGH1. Fork-side prior art also exists on the local
  feat/72-lumix branch. https://github.com/gkoh/furble/pull/282 ,
  demand issue https://github.com/gkoh/furble/issues/247
- tobiasbrummer/lux-lat-long-log (S5-generation, MEI0 magic handshake,
  16-byte geo packet) and Aikhjarto/LUMIX-G9II-Remote-Control (G9II
  generation XOR login). Transport: BLE GATT for shutter and geotag,
  1-byte commands; WiFi for everything else. Maturity: RE-partial.
  https://github.com/tobiasbrummer/lux-lat-long-log ,
  https://github.com/Aikhjarto/LUMIX-G9II-Remote-Control
- LUMIX Sync official docs confirm BLE does shutter (including bulb) and
  geotag; the rest is WiFi. No official UUIDs published.
  https://av.jpn.support.panasonic.com/support/global/cs/soft/lumix_sync/en/DC-S9/adv_sht_rmt.html

### GoPro

- Open GoPro official BLE spec. Vendors: HERO 9 and newer, MAX. Transport:
  BLE GATT. Documents: advertised service FEA6, command request GP-0072
  (b5f90072-aa8d-11e3-9046-0002a5d5c51b) and response GP-0073, settings
  GP-0074/0075, query GP-0076/0077, the shutter TLV (03 01 01 01 start,
  03 01 01 00 stop), mode set, keep-alive, and the bond-with-encryption
  pairing model (camera must be put in pairing mode). GoPro has no focus
  concept and no location push. Maturity: official. License: docs open.
  https://gopro.github.io/OpenGoPro/ble/protocol/ble_setup.html ,
  https://gopro.github.io/OpenGoPro/ble/protocol/id_tables.html ,
  https://gopro.github.io/OpenGoPro/tutorials/send-ble-commands
- KonradIT/goprowifihack, Bluetooth section. Community table of the same
  UUIDs and commands, plus older HERO notes.
  https://github.com/KonradIT/goprowifihack/blob/master/Bluetooth/bluetooth-api.md

### Blackmagic Design

- Official Blackmagic Camera Control Developer manual (Aug 2025). Vendors:
  Pocket Cinema Camera, URSA and other Bluetooth-capable bodies. Transport:
  BLE GATT with SDI-format payloads. Documents: Camera service
  291d567a-6d75-11e6-8b77-86f30ca893d3, Outgoing Camera Control
  5dd3465f-1aee-4299-8493-d2eca2f8e1bb (write), Incoming b864e140-...,
  Camera Status 7fe8691d-... (write 0x01 to power on), Device Name
  ffac0c52-..., Timecode 6d8f2110-..., Protocol Version 8f1fd018-..., the
  4-byte header plus command packet padded to 32 bits, data types and
  operations, autofocus (category 0 Lens, parameter 1), focus, iris, zoom,
  and record via the transport mode. Pairing shows a six-digit passkey on
  the camera. Maturity: official. License: Blackmagic developer terms.
  https://documents.blackmagicdesign.com/DeveloperManuals/BlackmagicCameraControl.pdf
- coral/blackmagic-camera-protocol and its PROTOCOL.json. Machine-readable
  version of the command table with the same UUIDs; record via Media
  category 10, parameter 1, int8 (2 record, 0 stop) per forum use.
  Maturity: RE-mature over an official base. License: not verified.
  https://github.com/coral/blackmagic-camera-protocol ,
  https://github.com/coral/blackmagic-camera-control/blob/main/PROTOCOL.json ,
  https://forum.blackmagicdesign.com/viewtopic.php?f=12&t=100654

### DJI

- dji-sdk/Osmo-GPS-Controller-Demo (official). Vendors: Osmo Action 4 and
  newer, Osmo 360. Transport: BLE GATT on ESP32-C6, the DJI R SDK framing
  (0xAA start of frame, CRC16 and CRC32). Documents: connect to nearest
  camera in pairing mode, single-click record, mode switch, and official
  10 Hz GPS push for in-camera geotag. Maturity: official demo. License:
  see repo. https://github.com/dji-sdk/Osmo-GPS-Controller-Demo ,
  protocol notes https://github.com/dji-sdk/Osmo-GPS-Controller-Demo/blob/main/docs/protocol.md
- yigitkonur/lib-osmo-ble (Osmo Pocket 3, DUML over service fff0) and
  xaionaro/reverse-engineering-dji (djictl, DUML artifacts). Maturity:
  RE-partial. https://github.com/yigitkonur/lib-osmo-ble ,
  https://github.com/xaionaro/reverse-engineering-dji
- Fork-side prior art exists on the local feat/74-dji-osmo branch.

### Insta360

- pchwalek/insta360_ble_esp32 and the companion Medium writeup and
  Hackaday project 188975. Vendors: X3, X4, ONE RS. Transport: BLE GATT,
  service be80, write be81, notify be82; 20-byte chunked writes with a
  2-byte sequence counter; a wake-on-advertisement quirk where the camera
  powers on when it sees manufacturer data (spoofed Apple company id) from
  an advertiser named "Insta360 GPS Remote" carrying the camera serial.
  Documents concrete button byte arrays. Inverted role (camera connects to
  the remote), so furble would need a peripheral path. Maturity:
  RE-partial to mature. License: not verified.
  https://github.com/pchwalek/insta360_ble_esp32 ,
  https://medium.com/@patrickchwalek/ble-control-of-insta360-cameras-7bf6894648a4 ,
  https://hackaday.io/project/188975

### Olympus and OM System

- No public source documents the OI.Share or OM remote BLE GATT
  characteristics for shutter or power-on. Control after pairing is WiFi
  (OlympusCameraKit / OPC HTTP CGI). ura14h/PlayOPC wraps the licensed
  OLYCameraKit framework, hotchpotch/olympus-camera wraps the WiFi CGI API,
  and bullbin/xiaoyi_m1_re_liveview shows the analogous Yi M1 BLE-key then
  WiFi pattern. BLE remote bytes require original reverse engineering.
  Maturity: undocumented for BLE.
  https://github.com/ura14h/PlayOPC , https://github.com/hotchpotch/olympus-camera ,
  https://github.com/bullbin/xiaoyi_m1_re_liveview ,
  OM-1 II manual https://learning.omsystem.com/OM-1MarkII/zz_html_manual/en/shooting_remotely_remote_shutter_280.html

### Sigma

- fp, fp L, and BF have no Bluetooth radio. Wired or USB release only.
  Permanently out of scope. https://www.sigma-global.com/en/cameras/fp/#specifications

## Existing vendor feature expansion

The original survey ranked new vendors. This section gap-analyses the five
already-supported vendors for capabilities beyond furble's current shutter,
focus, and geotag. furble's base Camera API today exposes only
shutterPress/Release, focusPress/Release, and updateGeoData, so most of
these need a new library capability method (for example videoStart/Stop or
a settings interface) and, to be user-reachable, UI wiring. Adding the
library capability without UI is explicitly in scope ("expand lib/furble
even for features furble does not currently use"); UI wiring is a separate
follow-up.

Legend: Y furble has it, N missing, P partial or mode-limited.

| Feature | Fujifilm | Canon | Nikon | Sony | Ricoh | Best source and note |
|---|---|---|---|---|---|---|
| Separate focus (half-press/AF) | Y | remote Y, smart N | N (remote no-op) | Y | N (focus-only unsupported) | Sony 0x07/0x06 (freemote); Canon 0x40 (Ian Scott); Ricoh OperationRequest is capture |
| Bulb / long exposure | Y (mode + press/release) | via 2s or immediate | N | via press/release | via self-timer | Fuji manual: slide up start, down stop |
| Video record start/stop | P (movie mode + press) | N (movie mode 0x08 unused) | N (MODE_VIDEO 0x03 unused) | N (movie 0x0E/0x0F unused) | N | Canon 0x08, Nikon 0x03, Sony 0x0E/0x0F |
| Zoom (power-zoom lenses) | N (861442ab/43070f6c bytes unknown) | N (wide 0x10, tele 0x20) | N (ML-L7 has +/- , bytes unknown) | N (0x0244-0x0247) | N | Sony and Canon documented; Fuji and Nikon need a sniff |
| Self-timer / interval camera-side | N | N (2s 0x04) | N | N | Y (ShootingFlavor, SelfTimer chars) | Ricoh already; Canon 0x04 |
| Settings read/write (ISO, etc.) | N (proprietary) | N | N | N | P (power, op mode readable) | No vendor exposes full PTP-style settings over the BLE remote channel; mostly WiFi |
| Time sync (clock set) | via geotag only | via geotag timestamp | embedded in geo, no standalone 2006 write | via geotag | N (Date/Time char unused) | Ricoh has a Date/Time char; Nikon has char 2006 |
| Richer geotag (alt/heading/DOP) | alt yes, no heading | alt yes | alt + DMS + sats | alt + UTC offset | alt yes | furble geotag already carries altitude for most; heading/DOP unused everywhere |
| Telemetry (battery, rec, mode) | N (5013/5023/5033 undecoded) | N | N | N (FF02 notify undecoded) | P (subscribes, logs only) | Sony FF02 decodes exist (freemote); Fuji indications undocumented |
| Pairing / newer revisions | secure LESC handled (FujifilmSecure) | done | smart blocked on BR/EDR | done | MITM passkey done | Nikon BR/EDR recipe at skyblond |

Per-vendor notes.

- Fujifilm (only hardware testable here, X100VI and X-E5). The shutter
  service 6514eb81 exposes six writable characteristics; furble uses one
  (7fcf49c6). The secondary 600655e6 accepts opcode 0x04 to trigger a WiFi
  handover (libfuji). Bulb and movie need no new opcode: they work with the
  existing press/release once the camera is in Bulb or Movie mode (Fuji
  manual, thenowherephotographer). Cheaply testable additions: an explicit
  movie or bulb console command that verifies the existing bytes in those
  modes; reading the DIS 0x180a strings (model, firmware, serial) which
  furble ignores; and dumping the readable indication characteristics
  (5013/5023/5033/5053/5063) and the 15ca59fe vitals reads to decode
  battery, recording, and mode. The dedicated TG-BT1 movie and zoom button
  bytes and the meaning of 861442ab/43070f6c/fb15c357/b1307521 are
  undocumented and need an nRF or HCI sniff of a real grip.
- Canon. The BR-E1 control byte already carries movie (mode 0x08), 2s
  timer (mode 0x04), and power zoom (wide 0x10, tele 0x20). furble hardcodes
  immediate stills (0x0c). Adding movie and timer is a few constants on the
  existing write path; zoom needs a power-zoom lens to verify. Well sourced
  (Ian Scott).
- Nikon. ML-L7 MODE_VIDEO 0x03 is declared but never sent; sending
  {0x03, 0x02} to shutter char 2083 is the likely movie toggle (needs
  hardware or an HCI capture to confirm). The larger prize is unblocking
  SnapBridge smart mode (GPS, geotag, time sync on non-ML-L7 bodies), which
  requires a BR/EDR classic bond; skyblond documents the recipe but it adds
  Bluetooth Classic coexistence cost and interacts with furble power
  management (see plans/65-bt-coexistence).
- Sony. freemote documents movie (0x0E/0x0F), AF-ON (0x15/0x14), custom C1
  (0x21/0x20), zoom and focus stepping (0x0244-0x026D with a step byte,
  which needs the 2-byte length prefix rather than furble's fixed
  uint16_t), and FF02 status notifications (focus acquired, shutter active,
  recording). All are low risk over the existing control characteristic.
- Ricoh. Already the richest existing class. Gaps: the Date/Time (clock
  set) characteristic in the Camera service is unused (furble only writes
  geotag), and the subscribed status characteristics are logged but not
  surfaced. Pentax K-series and G900/WG-M2 share the same GATT family per
  dm-zharov and should work but are unverified.

## Prioritized expansion backlog

Combined ranking of new-vendor and existing-vendor work. Effort is rough
(hours, days, weeks). Risk covers protocol certainty and ESP32 or furble
architecture impact. Every item is untested unless it targets Fujifilm
hardware available here.

Tier 1, best sourced and self-contained (map onto the existing
shutter/focus/geotag API or a small library addition).

1. Fujifilm movie and bulb verification (testable). Add explicit console
   commands that exercise the existing press/release bytes with the camera
   in Movie and Bulb modes, and document the result. Effort: hours. Risk:
   very low. Source: Fuji manual, thenowherephotographer. This is the one
   item verifiable on the X100VI and X-E5.
2. Fujifilm Device Information readout (testable). Read DIS 0x180a model,
   firmware, and serial and expose in the About or camera detail view.
   Effort: hours. Risk: very low. Source: fffw GATT dump, libfuji.
3. Sony command expansion: movie (0x0E/0x0F), AF-ON (0x15/0x14), C1
   (0x21/0x20). Needs a videoStart/Stop (or generic command) library hook.
   Effort: days including the base API method. Risk: low protocol, untested
   hardware. Source: coral/freemote.
4. Canon BR-E1 movie mode (0x08) and 2s timer (0x04). Small constants on
   the existing control write plus a library hook for video. Effort: hours
   to days. Risk: low protocol, untested. Source: Ian Scott.
5. Nikon ML-L7 video (MODE_VIDEO 0x03). One command on the existing shutter
   char, but the exact movie action byte is unconfirmed. Effort: hours.
   Risk: medium (needs HCI or hardware confirm). Source: skyblond, ML-L7
   manual.
6. GoPro (HERO 9+) new vendor, shutter and record only. Official TLV over
   FEA6. Effort: days. Risk: medium (NimBLE bond and encryption re-key on
   ESP32 is the historically fragile part; no location story). Source: Open
   GoPro.
7. Blackmagic new vendor, record via transport mode and instantaneous
   autofocus. Officially documented UUIDs and packet format. Effort: days.
   Risk: medium (six-digit passkey pairing needs a numeric entry UI furble
   lacks; protocol itself is certain). Source: official manual, coral.

Tier 2, well sourced but higher effort or architecture impact.

8. DJI Osmo Action 4+ new vendor with official 10 Hz GPS push. Clean fit
   for furble geotag. Effort: medium, a new framed binary protocol with
   CRC. Risk: low protocol (official demo), untested. Source:
   dji-sdk/Osmo-GPS-Controller-Demo.
9. Panasonic Lumix new vendor. Track upstream PR 282, rebase, review
   against the two external repos. Effort: days. Risk: medium (S5II WiFi
   assisted first pair, G9II XOR login not in PR 282), untested. Source:
   PR 282, lux-lat-long-log, LUMIX-G9II-Remote-Control.
10. Pentax K-series via the existing Ricoh class. Likely near-zero code
    (name match already accepts PENTAX). Risk: unverified camera-side
    acceptance, must ship declared untested. Source: dm-zharov.
11. Ricoh Date/Time clock set and status surfacing. Small additions on the
    existing Ricoh class. Effort: hours to days. Risk: low, untested.
    Source: dm-zharov.
12. Sony zoom and focus stepping, and FF02 telemetry decode. Needs the
    2-byte length prefix path and a telemetry surface. Effort: days. Risk:
    low protocol, untested. Source: coral/freemote.

Tier 3, blocked, high effort, or low value.

13. Nikon SnapBridge smart mode (GPS, geotag, time sync). Requires a BR/EDR
    classic bond alongside NimBLE. Effort: weeks. Risk: high (Bluetooth
    Classic coexistence RAM and power cost, interacts with DFS traps).
    Source: skyblond, upstream 257, plans/65-bt-coexistence.
14. Generic BLE HID shutter mode (universal phone and app coverage).
    Requires furble to add a BLE peripheral and HID GATT server, distinct
    from its central-only design. Effort: weeks. Risk: architecture.
    Source: ABShutter3, esp32_ble_hid.
15. Insta360 new vendor. Inverted role (camera connects to the remote,
    wake-on-advertisement). Needs a peripheral path. Effort: weeks. Risk:
    architecture. Source: pchwalek/insta360_ble_esp32.
16. Fujifilm dedicated movie and zoom buttons, and telemetry decode. Blocked
    on an nRF or HCI sniff of a TG-BT1 grip. Effort: gated by capture.
    Risk: undocumented. Source: fffw (behavior only).
17. Olympus and OM System. No public BLE protocol. Needs original reverse
    engineering. Effort: gated by capture and hardware. Source: none for
    BLE.

Enum allocation note: any new vendor class must claim the next free
Camera::Type value in writing here before implementation to avoid the slot
10 collision that already occurred between the local feat/72-lumix and
feat/74-dji-osmo branches (both took value 10). Current highest persisted
value in use on fork master is RICOH = 9.

## Verification plan for this document

- Docs only, no code. CI must stay green.
- Every link above was fetched and checked for the claimed content during
  the original survey or the 2026-08-21 expansion. License and last-activity
  fields in the reference catalog were not individually audited and are
  marked "not verified"; confirm before vendoring third-party code.
- Follow-ups happen per vendor as separate plans with their own
  verification sections.
