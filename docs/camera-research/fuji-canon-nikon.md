# Furble camera protocol research: Fujifilm, Canon, Nikon

Research date: 2026-09-12 (Europe/Berlin)

This is a research handoff, not a compatibility guarantee. It follows the
evidence classes and certification rules in [plan 159](https://github.com/MaxRink/furble/blob/965f299f0f43c38fe45e4680f1bfb735023533af/plans/159-camera-peer-certification.md):
official manuals establish user-visible semantics; public implementations are
common-implementation evidence; only an exact hardware capture can certify
private GATT bytes, ordering, security, timing, or physical capture.

## Source identity and provenance

The source was read from a clean snapshot at exact Git revision
`965f299f0f43c38fe45e4680f1bfb735023533af` (`Merge pull request #66 from
MaxRink/feat/33c-mqtt`, 2026-09-12). The worktree was clean when read. No
source files were edited; all current Furble and plan links below pin this
same MaxRink commit.

Public source pins used here (GitHub files and repository metadata retrieved
2026-09-12):

| Source | Commit | License / use |
| --- | --- | --- |
| [tiredboffin/fffw](https://github.com/tiredboffin/fffw/tree/bedc091e0b54a1a34aaf6929dd08e1db36d13b08) | `bedc091e0b54a1a34aaf6929dd08e1db36d13b08` | MIT (`LICENSE.md`); research and clean-room capture targeting |
| [3bl3gamer/canon-bluetooth-control](https://github.com/3bl3gamer/canon-bluetooth-control/tree/d029deac13b1ab91c813e667af0d67e3b9ed168a) | `d029deac13b1ab91c813e667af0d67e3b9ed168a` | No repository SPDX license reported; citation-only, do not copy |
| [RReverser/eos-remote-web](https://github.com/RReverser/eos-remote-web/tree/707c05d5dd19040f27782d039f07e0a46fa6febe) | `707c05d5dd19040f27782d039f07e0a46fa6febe` | MIT (`LICENSE`); independent semantic/protocol comparison |
| [robot9706/CanonBLEIntervalometer](https://github.com/robot9706/CanonBLEIntervalometer/tree/c230a7ad3b9fde569289b63272f46097bb8c2531) | `c230a7ad3b9fde569289b63272f46097bb8c2531` | No repository SPDX license reported; citation-only, do not copy |
| [attilaolah/birdcam](https://github.com/attilaolah/birdcam/tree/93ffc86a85a474dd884e4ac0168a991c1fdb1822) | `93ffc86a85a474dd884e4ac0168a991c1fdb1822` | MIT, but contains third-party-derived Nikon material; clean-room research |
| [hurui200320/nsg](https://github.com/hurui200320/nsg/tree/5a9117def8fad5b75771a52837562ae02b9c80c8) | `5a9117def8fad5b75771a52837562ae02b9c80c8` | AGPL-3.0; citation-only, no code or fixtures |

## Models Furble actually lists

The current README's tested list is the source of truth for model names. For
this lane it lists Fujifilm GFX100 II, GFX100RF, GFX100S, GFX100S II,
GFX50S II, X-E4, X-E5, X-H1, X-H2S, X-S10, X-S20, X-T200, X-T3, X-T30, X-T4,
X-T5, X100V, and X100VI; Canon EOS M6, EOS R6 Mark II, EOS RP, and PowerShot
G9 X Mark II; and Nikon COOLPIX B600 and Z6 III. The same README also lists
Ricoh GR IV HDF and Sony ZV-1F outside this Fujifilm/Canon/Nikon lane. See the
pinned [README model list](https://github.com/MaxRink/furble/blob/965f299f0f43c38fe45e4680f1bfb735023533af/README.md#L73-L106).
The pinned [supported-hardware reference](https://github.com/MaxRink/furble/blob/965f299f0f43c38fe45e4680f1bfb735023533af/docs/supported-hardware.md#L38-L54)
states that only Fujifilm cameras are available for hardware tests; Canon and
Nikon are covered by code review and the FauxNY test camera.

The `fffw` firmware map is useful profile research, not an expanded Furble
support list. It records, among others, X-E5 `01.10` as secure+, X100VI
`01.31` as secure and `01.11` as v3.1, and multiple GATT hashes for X-T5,
X-S20, X-H2/H2S, X-T4, X-T3, X100V, X-S10, X-H1, X-E3, X-T30/30 II, X-T50,
GFX100/100 II/100RF. Exact profile identity still requires model, firmware,
advertisement, and GATT-database capture.

## Fujifilm Basic and Secure

### Official semantics

Fujifilm's [X100VI manual](https://app.fujifilm-dsc.com/en-int/manual/x100vi/connections/usage_smartphone_app/)
and [X-E5 manual](https://fujifilm-dsc.com/en-int/manual/x-e5/connections/usage_smartphone_app/)
document Bluetooth pairing through the camera's pairing menu and a displayed
code, automatic reconnect after pairing, and remote control through the XApp
workflow. Their network menus expose pairing destination, Bluetooth on/off,
and smartphone location sync. Location is downloaded only while the app is
running, and the X100VI manual says stale location is indicated after 30
minutes. These manuals do not document Furble's private GATT UUIDs, token,
secure registration, or shutter bytes. Fujifilm also warns that leaving
Bluetooth enabled increases battery drain; X100VI/X-E5 support a power-off
Bluetooth connection to the smartphone app, which is an app-specific behavior,
not evidence that Furble can wake a camera.

Firmware is a material constraint. Fujifilm's [X-E5 firmware page](https://www.fujifilm-x.com/en-us/support/download/firmware/cameras/x-e5/)
shows version 1.12 when retrieved on 2026-09-12 and says version 1.10 changed wireless
security and pairing procedure. It also warns that old Camera Remote versions
cannot connect after the update. Do not collapse firmware generations into one
Fujifilm profile.

### Current Furble implementation evidence

The implementation is in the pinned [Fujifilm base](https://github.com/MaxRink/furble/blob/965f299f0f43c38fe45e4680f1bfb735023533af/lib/furble/Fujifilm.h),
[Basic](https://github.com/MaxRink/furble/blob/965f299f0f43c38fe45e4680f1bfb735023533af/lib/furble/FujifilmBasic.cpp),
[Secure](https://github.com/MaxRink/furble/blob/965f299f0f43c38fe45e4680f1bfb735023533af/lib/furble/FujifilmSecure.cpp),
and [wire protocol](https://github.com/MaxRink/furble/blob/965f299f0f43c38fe45e4680f1bfb735023533af/lib/furble/protocol/FujifilmProtocol.h).
The following details are from that clean, pinned source.

#### Basic profile

- Advertisement: manufacturer data starts with company ID `0x04d8` on the
  wire, type `0x02`, then a four-byte pairing token. A Basic match requires
  the exact 7-byte manufacturer field and either the CR service
  `af854c2e-b214-458e-97e2-912c4ecf2cb8` or XAPP service
  `117c4142-edd4-4c77-8696-dd18eebb770a`.
- Pairing: connect, write the saved four-byte token with response to
  `91f1de68-dff6-466e-8b65-ff13b0f16fb8/aba356eb-9633-4e60-b73f-f52516dbd671`,
  then write the Furble identity to characteristic
  `85b9163e-62d1-49ff-a6f5-054b4630d4a1` in the same service.
- Registration: subscribe to indications `a68e3f66-0fcc-4395-8d4c-aa980b5877fa`,
  `bd17ba04-b76b-4892-a545-b73ba1f74dae`, and `049ec406-ef75-4205-a390-08fe209c51f0`,
  plus notifications on `f9150137-5d40-4801-a8dc-f7fc5b01da50` and the
  geotag request characteristic `ad06c7b7-f41a-46f4-a29a-712055319122`.
  Furble waits up to 25 s for registration confirmation on
  `CHR_NOT1`: captured X100VI is `01 00`; legacy Basic accepts `02 00` there.
  A valid `01 00` geotag request also promotes a saved X100VI reconnect.
- Shutter: discover service
  `6514eb81-4e8f-458d-aa2a-e691336cdfac`, characteristic
  `7fcf49c6-4ff0-4777-a03d-1a79166af7a8`. Write command `{01 00}` followed by
  parameter `{02 00}` for press, `{00 00}` for release, or `{03 00}` for
  focus. Focus release maps to release.
- Geotag: write 23 bytes to
  `3b46ec2b-48ba-41fd-b1b8-ed860b60d22b/0f36ec14-29e5-411a-a1b6-64ee8383f090`:
  signed little-endian latitude and longitude in 1e-7 degrees, signed
  little-endian altitude, little-endian year, then month/day/hour/minute/second.
- Persistence: Basic stores name, BLE address/type, and token. A rotating
  token is advertisement identity, not a stable serial. No Basic firmware map
  proves every listed model follows these bytes.

#### Secure profile

- Advertisement: company ID `0x04d8` followed by five serial bytes, with exact
  8-byte manufacturer data and advertised service
  `a9d2b304-e8d6-4902-8336-352b772d7597`. The secure pairing service is
  `123d8f06-62a1-4935-9322-833c531ee225`; the primary service is
  `731893f9-744e-4899-b7e3-174106ff2b82`.
- Identity and status: after BLE link security, read four bytes from
  `f557d96b-8284-4667-8793-b971c1deca2a` and acknowledge by retaining the first
  three bytes and replacing the last with `20`. Write the Furble identity to
  `85b9163e-62d1-49ff-a6f5-054b4630d4a1`.
- Registration subscriptions: the local Secure table includes required
  indications `a68e3f66-0fcc-4395-8d4c-aa980b5877fa` and
  `bd17ba04-b76b-4892-a545-b73ba1f74dae`, then optional notifications on
  `f9150137-5d40-4801-a8dc-f7fc5b01da50`, geotag request
  `ad06c7b7-f41a-46f4-a29a-712055319122`, and additional NOT4/NOT5/NOT6/NOT7/
  NOT8/NOT9/NOT10 plus geotag-sync interval UUIDs. The required indication
  writes use ATT responses; optional CCCD writes do not.
- Secure persistence/reconnect: the five-byte serial is persisted and used to
  match a saved scan. Secure bodies can use resolvable private addresses, so
  the identity address must be read from the live connection for bond lookup and
  deletion. The local code scans for up to 60 s. It records two consecutive
  security failures on an existing bond before deleting the stale identity bond;
  a fresh pairing still needs camera-side authorization.
- Timing/profile: registration wait is 25 s with 20 ms polling in the local
  checkout. After shutter discovery, local code requests the fast connection
  profile and waits up to 1 s for confirmation. The local comments tie this
  ordering to the X100VI trace; it is not a universal Secure-camera guarantee.
- Shutter and geotag use the same UUIDs and payloads as Basic. Secure local code
  writes a 10-second geotag sync interval as a little-endian `uint16_t` to
  `c95d91ae-b247-4d6d-8661-7dd5d6a0f85b`, then reacts to a camera geotag request.

### Fujifilm peer implementation evidence

The MIT [fffw profile map](https://github.com/tiredboffin/fffw/blob/bedc091e0b54a1a34aaf6929dd08e1db36d13b08/ffbt/cfg/gatt-profiles-map.csv)
maps X-E5 `0110` to secure+ and X100VI `0131` to secure (`ad14d4` GATT hash),
while X100VI `0111` maps to an older v3.1 profile (`76ab62`). It also records
different GATT templates and handles across X-T5, X-S20, X-H2/H2S, X-T4,
X-T3, X100V, X-S10, X-H1, X-E3, X-T30/30 II, and GFX bodies. Its [X-T5 04.30 profile](https://github.com/tiredboffin/fffw/blob/bedc091e0b54a1a34aaf6929dd08e1db36d13b08/ffbt/cfg/xt5-0430.yaml)
shows a 10-second location interval and a concrete GATT template, but that is
not evidence for another body or firmware.

### Gaps and capture targets

- No official Fujifilm document found here publishes the private Furble/XApp
  UUID semantics or command bytes. Keep those fields `common-implementation`
  or `hardware-capture`, never `official-semantic`.
- Capture exact Basic and Secure advertising, GATT database/properties, SMP
  method and bond/RPA behavior, indication confirmation, status read/ack,
  registration delay, every optional CCCD result, shutter/focus physical
  outcome, geotag request cadence and EXIF result for each firmware family.
- Specifically capture X100VI `01.11` versus `01.31`, X-E5 `01.10+`, and at
  least one Basic X-E4/X100V/X-T body. X-E5 and X100VI appearing in the README
  proves model-level reports, not a firmware wildcard.
- The simulator should use role/profile-exclusive GATT templates from fffw,
  model delayed registration and 10-second geotag requests, and mark all
  cross-model fields uncertified until exact captures exist.

## Canon EOS Smart and Remote

### Official semantics

Canon's [EOS R6 Mark II BR-E1 pairing instructions](https://cam.start.canon/fr/C012/manual/html/UG-07_Network_0040.html)
require Bluetooth enabled, adding a wireless-remote device, and holding BR-E1
W and T simultaneously for at least three seconds. Canon documents remote
shooting at about 5 m, still and movie operation, and extra battery use. Its
[Bulb instructions](https://cam.start.canon/en/C012/manual/html/UG-03_CustomShooting_0070.html)
say BR-E1 starts a bulb exposure immediately or after 2 s and a second press
stops it. Canon's [GPS instructions](https://cam.start.canon/en/C012/manual/html/UG-07_Network_0160.html)
separately require Camera Connect, smartphone location services, and a
Bluetooth smartphone pairing; pairing a wireless remote ends smartphone GPS
acquisition. These official pages establish semantics and conflicts, not the
private GATT protocol.

### Current Furble implementation evidence

See pinned [Canon base](https://github.com/MaxRink/furble/blob/965f299f0f43c38fe45e4680f1bfb735023533af/lib/furble/CanonEOS.cpp),
[Smart](https://github.com/MaxRink/furble/blob/965f299f0f43c38fe45e4680f1bfb735023533af/lib/furble/CanonEOSSmart.cpp),
and [Remote](https://github.com/MaxRink/furble/blob/965f299f0f43c38fe45e4680f1bfb735023533af/lib/furble/CanonEOSRemote.cpp).

#### Smart mode

- Discovery matches the advertised primary service
  `00010000-0000-1000-0000-d8492fffa821`.
- Bonding uses NimBLE secure connection, described in source as Just Works.
  A saved local bond is treated as pair accepted. A new session subscribes to
  indication on `00010006-0000-1000-0000-d8492fffa821` (name/pair result), then
  writes to `00010006` prefix `01` plus Furble name.
- The source then writes to `0001000a-0000-1000-0000-d8492fffa821`: prefix `03`
  plus a generated 16-byte Furble UUID, prefix `04` plus name, and prefix `05`
  plus `{02}`. It waits up to 60 s for indication `02` accept or `03` reject.
  Reject deletes the local bond. The final `0001000a` write `{01}` commits the
  pairing, then mode `00030010-0000-1000-0000-d8492fffa821` gets `{02}` for
  shooting.
- Shutter writes to `00030030-0000-1000-0000-d8492fffa821`: `{00 01}` press,
  `{00 02}` release. Focus press/release are intentional no-ops. There is no
  Bulb, video, or manual-focus implementation in this Furble path.
- GPS service `00040000-0000-1000-0000-d8492fffa821`: subscribe indication
  `00040003`; request byte `03` causes `{01}` on write characteristic
  `00040002`; success byte `02` enables updates. Furble sends a packed 20-byte
  message with header `04`, N/S and E/W direction bytes, absolute float32
  coordinates/elevation, sign bytes, and a Unix timestamp. The source uses
  `mktime`, whose result depends on the host/runtime timezone, so timezone
  independence needs a focused capture/review before certification; this is a
  conditional verification item, not a proven bug.

#### Remote mode

- Discovery matches `00050000-0000-1000-0000-d8492fffa821`.
- After NimBLE secure connection, write prefix `03` plus Furble name to
  `00050002-0000-1000-0000-d8492fffa821`.
- Control characteristic `00050003-0000-1000-0000-d8492fffa821` accepts one-byte
  write-with-response commands: press `0x8c` (`0x80` shutter plus `0x0c`
  control), release `0x0c`, focus press `0x4c` (`0x40` focus plus control),
  focus release `0x0c`. GPS is intentionally unsupported. There is no local
  timeout or camera-state confirmation beyond the underlying BLE call.

### Canon peer implementation evidence

The MIT [eos-remote-web implementation](https://github.com/RReverser/eos-remote-web/blob/707c05d5dd19040f27782d039f07e0a46fa6febe/index.js)
uses the same `00050000`/`00050002`/`00050003` UUID family, sends pair command
`03` plus name, and drives immediate-release `0x8c` then `0x0c`. Its README
reports a sleep/reconnect failure that required unpairing and power-cycling on
the author's camera. Treat that as a report, not a universal Canon defect.

The [canon-bluetooth-control README](https://github.com/3bl3gamer/canon-bluetooth-control/blob/d029deac13b1ab91c813e667af0d67e3b9ed168a/README.md)
is tested on EOS M6 and says OS-level Bluetooth pairing is required before its
Web Bluetooth handshake. It documents the Smart family `00010000` service,
prefixes `01`, `03`, `04`, `05`, pairing indications `02`/`03`, shooting mode
`02`, wake `03`, suspend `05`, and shooting writes `0001`/`0002`; it warns that
other models may differ. The [CanonBLEIntervalometer implementation](https://github.com/robot9706/CanonBLEIntervalometer/blob/c230a7ad3b9fde569289b63272f46097bb8c2531/src/src/canon_ble.c)
uses a secure write to initiate bonding, then name/platform/confirm `{01 54...}`,
`{05 02}`, `{01}`, notification setup, and trigger `{00 01}` then `{00 02}`.
The first and third repositories have no confirmed permissive license, so use
their details as citation-only research.

### Gaps and capture targets

- Keep Smart and Remote as role-exclusive profiles. Canon's official docs make
  smartphone Camera Connect and BR-E1 roles mutually constraining; do not model
  a fictional union advertisement.
- Capture exact model and firmware for M6, R6 Mark II, RP, and G9 X Mark II,
  including first pair, saved reconnect, bond deletion, mode transitions,
  autofocus result, still, Bulb, two-second release, video, standby, and RF
  loss. Confirm physical outcomes, not just successful ATT writes.
- Capture Smart GPS request/enable/ack and a UTC timestamp in a non-UTC timezone.
  The local `mktime` path is a certification gap because its result depends on
  runtime timezone configuration; this is not a proven bug. Confirm whether GPS is
  available while any Canon remote role is paired.
- The simulator should reject Remote GPS and Smart focus, model the 60 s Smart
  user decision timeout, and expose camera-state/physical-outcome unknowns as
  `UNCERTIFIED`.

## Nikon Remote and Smart

### Official semantics and limitations

Nikon's [Z6 III ML-L7 manual](https://onlinemanual.nikonimglib.com/z6III/en/compatible_accessories_380.html)
documents Bluetooth pairing by entering [Save wireless remote controller] and
holding the ML-L7 power button for more than three seconds. Only one remote is
remembered, and the last paired remote wins. Nikon documents still and video
control, but says the remote cannot be used during another Bluetooth/Wi-Fi
connection, USB data exchange, or airplane mode; the remote shutter cannot be
half-pressed and cannot be held for burst capture. [Troubleshooting](https://onlinemanual.nikonimglib.com/z6III/en/problems_and_solutions_374.html)
adds battery, connection, and overheating prerequisites.

For the listed COOLPIX B600, Nikon's [official SnapBridge compatibility table](https://nikonimglib.com/snbr/onlinehelp/en/compatible_cameras_44.html)
marks SnapBridge pairing, Wi-Fi AP switching, still remote photography, clocks,
and location as supported, but marks the SnapBridge-app feature labelled
“Bluetooth remote control” as unsupported. This is an app-feature matrix, not
an accessory manual, so it cannot establish or contradict ML-L7-style physical
accessory support for B600. The follow-up [official manual index](model-manuals-other.md#nikon-coolpix-b600-accessory-clarification)
resolves accessory support: Nikon's B600 Reference Manual and ML-L7 product
page explicitly list it. Neither source establishes Furble's private BLE bytes.
The [B600 download page](https://downloadcenter.nikonimglib.com/en/products/510/COOLPIX_B600.html)
identifies firmware 1.1 (2019-12-19). Thus Furble's B600 ML-L7-style BLE path
is reverse-engineered evidence distinct from Nikon's documented SnapBridge
remote-photography feature. Official ML-L7 accessory compatibility is now
documented separately; exact Furble protocol and outcome captures remain absent.

### Current Furble implementation evidence

See pinned [Nikon](https://github.com/MaxRink/furble/blob/965f299f0f43c38fe45e4680f1bfb735023533af/lib/furble/Nikon.cpp),
[base](https://github.com/MaxRink/furble/blob/965f299f0f43c38fe45e4680f1bfb735023533af/lib/furble/NikonBase.cpp),
[Remote](https://github.com/MaxRink/furble/blob/965f299f0f43c38fe45e4680f1bfb735023533af/lib/furble/NikonRemote.cpp),
and [Smart](https://github.com/MaxRink/furble/blob/965f299f0f43c38fe45e4680f1bfb735023533af/lib/furble/NikonSmart.cpp).

#### Shared discovery and four-stage handshake

- Main service is `0000de00-3dd4-4255-8d62-6dc7b9bd5561`.
- New discovery matches no manufacturer data plus that service. Saved
  reconnect matches seven manufacturer bytes: company ID `0x0399`, persisted
  four-byte device ID, and trailing zero. The saved address is updated from a
  matching advertisement.
- The shared pair message is 17 bytes: stage byte, 64-bit timestamp, and either
  two 32-bit IDs or an eight-byte serial. The current base sends two
  request/response rounds, stages 1 -> 2 and 3 -> 4, with each response wait
  bounded at about 10 s and sliced into 250 ms polls. A stage-5 message template
  exists but is not emitted by the current base loop. Remote and Smart use
  separate pair characteristics and pairing validators.

#### Remote (ML-L7-style)

- Pair characteristic `00002087-3dd4-4255-8d62-6dc7b9bd5561`; indication
  `00002084-3dd4-4255-8d62-6dc7b9bd5561`; shutter
  `00002083-3dd4-4255-8d62-6dc7b9bd5561`.
- Remote pairing stages send stage 1, validate stage-2 response, send stage 3,
  and validate the stage-4 timestamp. The stage-5 template is not emitted by
  the current base loop. The first indication is
  subscribed before the shared handshake. Saved device and nonce are reused;
  new sessions randomize them, forcing the device ID's first wire byte to 01.
- Shutter writes require response: `{02 02}` press and `{02 00}` release to
  the remote shutter characteristic. Focus and GPS are no-ops. Furble reports
  this mode as shutter-only, with no manual exposure control.
- Shared stage timeout is about 10 s per response. There is no separate camera
  acceptance or physical-capture confirmation.

#### Smart (SnapBridge-style)

- Pair characteristic `00002000`; success notification `00002008`; identity
  `00002002`; location `00002007`; time `00002006`, all in the de00 UUID
  family.
- The local implementation uses Blowfish-derived eight-salt validation and
  builds stage 3 from the validated camera response. It then waits only 1 s
  for a final success indication and writes the controller name to `00002002`.
- The source explicitly marks Smart non-functional because Nikon requires a
  Bluetooth Classic (BR/EDR) continuation after the BLE exchange; Furble is
  LE-only. Smart shutter, focus, and GPS methods are no-ops in the final path.
  The local code contains a 41-byte GPS encoder, but the unsupported mode does
  not reach it as a successful connection.

### Nikon peer implementation evidence

The MIT [birdcam Coolpix reference](https://github.com/attilaolah/birdcam/blob/93ffc86a85a474dd884e4ac0168a991c1fdb1822/nikon/coolpix/README.md)
enumerates the de00 family UUIDs, including authentication `00002000`, client
name `00002002`, current time `00002006`, location `00002007`, control point
`00002008`, and power/control services. It derives the flow from SnapBridge
research and APK/HCI observation, so it is common-implementation evidence, not
Nikon documentation.

The [nsg protocol document](https://github.com/hurui200320/nsg/blob/5a9117def8fad5b75771a52837562ae02b9c80c8/doc/nikon-z-gps.md)
and [pairing engine](https://github.com/hurui200320/nsg/blob/5a9117def8fad5b75771a52837562ae02b9c80c8/esp32/lib/nikon-protocol/NikonPairingEngine.cpp)
describe the same eight salts and Blowfish hash, 17-byte four-stage BLE
exchange, 41-byte GPS payload, and a required BR/EDR bond after writing the
identity. They also report a separate Classic address and user-confirmed
numeric passkey, and cite Z50 II/Z8 validation. The repository is AGPL-3.0 and
must remain citation-only. Its model/firmware captures do not certify Furble's
B600 or Z6 III paths.

### Gaps and capture targets

- Capture B600 firmware 1.1 and Z6 III exact firmware separately. The official
  B600 accessory sources establish ML-L7 support, unlike the app-only feature
  table. Keep Furble's private protocol `common-implementation` until raw HCI
  and a physical result are archived for the exact profile.
- For Remote, capture advertisement rotation, device ID/nonce persistence,
  four-stage bytes and indications, SMP/bond behavior, ML-L7-compatible still,
  video, two-second/bulb behavior, standby and RF loss. Nikon's manual says
  the physical ML-L7 shutter has no half-press, so do not infer focus support.
- For Smart, capture BLE-to-BR/EDR transition, Classic address discovery,
  numeric-comparison UI, bond persistence, reconnect, and exact post-bond
  writes. Until a Classic-capable implementation exists, Smart must stay an
  explicit Furble failure, not a merely timed-out success.
- The simulator should use separate Remote and Smart peers, enforce one-remote
  semantics and connection exclusivity, and represent Classic continuation as
  an unsupported capability rather than silently accepting the BLE half.

## Cross-vendor future work

1. Store exact profile identity: model, firmware, role, raw advertisement, GATT
   hash/properties, Furble revision, board, NimBLE/ESP-IDF revisions, and
   fixture source/license metadata.
2. Build simulator peers from captured role-specific GATT templates. Do not use
   union services or family-wide wildcard acceptance.
3. Require negative cases: wrong token/serial, rejected pairing, missing or
   wrong-property characteristics, stale bond, RPA rotation, lost indication,
   camera sleep, RF loss, cancellation, and physical capture failure.
4. Measure hardware, not just ATT success: end-to-end shutter latency, focus
   outcome, capture count, GPS-to-EXIF result, standby current, reconnect
   duration, and timeout distributions. Record repeated min/median/p95/max;
   never invent distributions from a single trace.

Unknown fields remain unknown. No source or official manual reviewed here
supports a 100% model or firmware claim.
