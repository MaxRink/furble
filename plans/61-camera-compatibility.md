# 61: Camera compatibility survey

Status: research, no implementation yet. All cited links were fetched and
content verified during this survey (August 2026).

## Purpose

Map BLE camera remote protocols across vendors against current lib/furble
coverage. Rank the most feasible additions. Identify which can be tested
with a FauxNY-style fake or an emulated GATT server, and which need real
hardware.

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
| Ricoh | MITM passkey bond | 2 s timer proxy | yes, single write | yes, rate limited |
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
| Ricoh GR III/IIIx/IV | GATT, ShootingFlavor + OperationRequest | MITM passkey bond | yes, single-write capture | yes | supported | done | [dm-zharov/ricoh-gr-bluetooth-api](https://github.com/dm-zharov/ricoh-gr-bluetooth-api), [sotashimozono/gr3sync](https://github.com/sotashimozono/gr3sync) |
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

## Verification plan for this document

- Docs only, no code. CI must stay green.
- Every link above was fetched and checked for the claimed content.
- Follow-ups happen per vendor as separate plans with their own
  verification sections.
