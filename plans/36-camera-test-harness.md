# 36 - camera test harness: protocol tests without cameras

## Goal

Test the camera protocol layer in CI without physical cameras. Cover
advertisement matching, pairing and connection flows, shutter and focus
writes, GPS request and geotag responses, connection failures and
disconnects, and `Control` state transitions.

Line anchors below were read at `2ea53f0` on master.

## Motivation

The current CI builds firmware but tests no camera behavior.
`.github/workflows/main.yml` has formatting, discovery, build matrix, and
success jobs. Nothing runs protocol logic.

Only Fujifilm cameras are available for hardware tests. Canon, Nikon, Sony,
and Ricoh changes ship on code review alone. A payload byte error, a
coordinate scaling bug, or a broken pairing transform in those vendors would
reach users before anyone notices.

The existing `FauxNY` test camera does not close this gap. It fakes a camera
inside furble and bypasses NimBLE entirely:

- `FauxNY::matches()` always returns true in `lib/furble/FauxNY.cpp:24`.
- `FauxNY::_connect()` simulates progress and success.
- `shutter()`, `focus()`, and `updateGeoData()` only log.

It exercises application flow, not discovery, GATT writes, notifications,
pairing, or security.

The SDL simulator plan is no substitute either.
[28-emulator.md](28-emulator.md) explicitly avoids compiling `lib/furble`.
Its mock surface estimate is also far too small for the camera layer: the
repository contains 61 receiver-qualified NimBLE method uses across
`NimBLEClient` (12), `NimBLEDevice` (14), `NimBLERemoteService` (3),
`NimBLERemoteCharacteristic` (10), `NimBLEAdvertisedDevice` (8),
`NimBLEScan` (8), `NimBLEConnInfo` (5), and `NimBLEServer` (1).

## Current state

`Camera` owns the NimBLE client lifecycle. `Camera::connect()` in
`lib/furble/Camera.cpp:27-59` creates the client, installs callbacks,
configures security, calls the vendor `_connect()`, disconnects on failure,
and enables client self-deletion. `Scan::onResult()` feeds
`CameraList::match()`, which de-duplicates addresses and tries vendor
matchers. `Control` is asynchronous: `Control::Target::task()` dispatches
shutter, focus, GPS, and disconnect commands over FreeRTOS queues, and
`Control::connectAll()` connects cameras serially with a static retry
counter.

Vendor protocol logic is spread across `lib/furble/`: Fujifilm Basic and
Secure connection sequences, Canon Remote and Smart, the four-stage Nikon
pairing handshake, Sony manufacturer data parsing and location permission,
and Ricoh's bonded-connection requirement with time and movement throttling.
Every matcher takes a `NimBLEAdvertisedDevice`, and every geotag encoder
packs its struct inline inside the NimBLE write path. On the baseline before
this slice, none of it compiled on a host. The only host-clean component was
`lib/blowfish/Blowfish.cpp`.

## Implementation state

Two host harnesses are now in the tree. They cover different tiers and share
the same production protocol module.

### Tier A pure logic and Tier B seam groundwork (merged)

Landed by the protocol conformance tests. The host target lives under
`tests/camera/` with its own `camera-tests.yml` CI job. It has no ESP-IDF or
NimBLE dependency and compiles only the extracted protocol sources plus
Blowfish.

Implemented seams:

- `FujifilmProtocol` parses the common, basic, and secure advertisement bytes,
  supplies service-flag match predicates, recognizes the two Fujifilm
  notifications, frames shutter writes, and encodes the 23-byte geotag.
- `CameraListProtocol` encodes and decodes the fixed NVS index records,
  formats the persisted address key, and provides the index upsert operation.
- The production NimBLE and Preferences methods call these helpers at their
  current transport boundaries. No connection lifecycle or application-layer
  files were changed.

The `tests/camera/` binary passes advertisement, token, service-flag,
notification, shutter, geotag, CameraList persistence, and Blowfish vector
tests. This slice carries the pure logic seam. It ships no NimBLE mock, so the
full Tier B scripted lifecycle tests remain future work.

### Tier C end-to-end virtual camera harness (this PR)

Implemented under `tests/host/` with the `host_camera` job in
`.github/workflows/main.yml`. The `FujifilmVirtualCamera` peer models the
Fujifilm Basic advertisement, token pairing write, identifier write,
configuration indication, geotag request notification, shutter writes, and
geotag write. It runs against the production `Camera`, `Device`, `Fujifilm`,
and `FujifilmBasic` sources through a dependency-free in-memory NimBLE seam
under `tests/host/nimble/`. Those production sources delegate their wire
encoding to `FujifilmProtocol`, so the harness links the same protocol module
the Tier A tests exercise. No firmware behavior was changed, and no new
settings or simulator shim is required.

`tests/host/` and `tests/camera/` deliberately stay separate. `tests/camera/`
drives the extracted protocol functions directly with no transport. The Tier C
harness drives the real client lifecycle, so it needs a NimBLE seam that
`tests/camera/` does not. The two seams are not duplicates: the merged Tier A
work ships no NimBLE mock to reuse, and the `FujifilmVirtualCamera` peer
implements the wire format independently on purpose. If the peer reused the
client encoder, a bug in that encoder would pass both sides. The peer therefore
stays the adapter boundary to align when the full Tier B mock lands.

### Tier D corpus groundwork (this PR)

Implemented as schema 1 normalized text captures, a dependency-free loader, and
`tests/corpus/x100vi/synthetic.golden`. The replay test checks the production
token and geotag byte vectors. The fixture is synthetic and is not evidence of
X100VI interoperability. Reviewed real captures remain dependent on the BT
journal work in plan 64.

The macOS clang host build passes both the `tests/camera/` and `tests/host/`
suites. Hardware validation remains untested because no BLE peer radio was
exercised.

## Design

Four tiers, ordered by cost and by what they catch.

### Tier A: pure host protocol tests

Extract protocol logic into host-compilable functions. Split raw field
extraction from transport access: pure functions take byte spans, strings,
UUID values, and flags; the vendor methods keep the NimBLE access.

- New `lib/furble/protocol/<Vendor>Protocol.{h,cpp}` per vendor.
- New `tests/host/protocol_tests.cpp` and `tests/host/CMakeLists.txt`.
- First candidates: advertisement matchers, geotag encoders (`geotag_t`,
  `sony_geo_t`, `canon_geo_t`, Nikon packed time and geotag,
  `ricoh_geo_t`), Nikon pairing transforms (`scramble()`, `hash()`,
  `processMessage()`), `NikonSmart::degreesToDMSubMin()`, and Ricoh's
  helpers (`bswapd64()`, `validTimesync()`, `nameMatches()`).
- The first PR must not touch `Camera.cpp`, `FurbleControl.cpp`, or
  `Scan.cpp`.

Catches payload byte errors, coordinate scaling and sign errors, date and
time field errors, advertisement length and identifier mistakes, and
pairing transform regressions. Cannot catch service discovery, notification
timing, pairing, or lifecycle behavior.

#### Advertisement matcher coverage slice

The host suite now also compiles `lib/furble/protocol/AdvertisementProtocol.cpp`,
which is the production parser seam for vendor discovery data. The real Sony,
Panasonic Lumix, Nikon, DJI Osmo, and Ricoh classes route their deterministic
advertisement checks through this module while NimBLE remains responsible for
transport access and service presence. The suite covers:

- Sony manufacturer records, including company and camera type, required
  pairing and remote mode bits, truncated records, and tolerated trailing data.
- Panasonic Lumix company records, service gating, address extraction,
  truncated records, and tolerated trailing data.
- Nikon service-only discovery and saved-camera reconnect records, including
  company and device identity checks, malformed lengths, and trailing data.
- DJI Osmo marker records with short, unknown, and trailing-data cases.
- Ricoh and Pentax name matching with case folding, known model forms, and
  unknown names.

The parser uses explicit byte bounds and little-endian decoding rather than
reading packed NimBLE templates. Its protocol constants are grounded in the
existing vendor references in [61-camera-compatibility.md](61-camera-compatibility.md),
including the Sony [coral/freemote implementation](https://github.com/coral/freemote),
the Panasonic [LUMIX GPS logger](https://github.com/tobiasbrummer/lux-lat-long-log),
the Nikon [furble issue 209](https://github.com/gkoh/furble/issues/209), and the
DJI [official Osmo GPS controller demo](https://github.com/dji-sdk/Osmo-GPS-Controller-Demo).

Only Fujifilm hardware is available for bench testing. Sony, Panasonic, Nikon,
Ricoh/Pentax, and DJI paths remain untested on hardware and are covered by
production-code review plus deterministic host vectors.

#### Scan and advertisement edge follow-up

The cross-vendor vectors now exercise every independent Sony mode-bit rejection,
wrong vendor/type marker, null manufacturer buffer, exact truncation boundary,
and tolerated trailing byte. The Nikon discovery predicate covers all four
manufacturer/service combinations; Lumix service gating and DJI marker bytes
have explicit negative cases; Ricoh name matching includes case folding and
near-miss names. These checks remain transport-independent because an invalid
or truncated manufacturer record must be rejected before a connection attempt.

The SDL simulator adds `camera.count` and
`sim/scenarios/e2e/scan-duplicate-result.txt`, which drives the same fake scan
result twice and asserts that the camera list remains one row. This covers the
simulated scan-result de-duplication seam while the host vectors cover the
production byte parsers. Neither is evidence of vendor radio interoperability;
real cameras still require hardware acceptance for service discovery and
advertisement behavior.

The follow-up also checks every prefix length below each supported advertisement
record length for Fujifilm, Sony, Lumix, and Nikon. DJI marker records receive
the same truncation sweep, and each vendor has an all-zero unknown-record case.
The simulator now exposes `scan.end_callbacks`. The duplicate-result scenario
asserts that each scripted scan delivers one completion callback. This caught
and fixed a simulator-only duplicate callback that could run the scan-end UI
path twice while leaving production scan behavior unchanged.

Validation on the rebased follow-up branch: the camera protocol host suite,
simulator duplicate-result scenario, full simulator scenario suite, and
formatting checks pass. Hardware testing is not applicable to parser vectors or
the deterministic FauxNY scan seam. Only the Fujifilm path remains eligible for
physical camera validation, and no Fujifilm radio was exercised by this slice.

The subsequent audit found one production dispatch gap in the newly added
Lumix path: `CameraList::match` appended a Lumix camera but did not return a
match result. The branch now reports success, and null scan callbacks are
ignored before address de-duplication. The parser host suite remains the
hardware-independent guard for truncated, unknown, vendor-specific, and
boundary-length records.

Effort: 2 to 3 days for Fujifilm, 5 to 8 days for all vendors.

### Tier B: mock NimBLE client layer

An in-memory NimBLE implementation that lets the production `Camera`,
vendor classes, `Scan`, and `Control` run on a host. Header shims under
`tests/host/nimble/` and `tests/host/freertos/`, no production changes
unless the shim proves insufficient.

A scripted peer drives each test: advertise this packet, accept or reject
the connection, send this notification after subscription, wait for this
write, drop the connection, report this security state. Key scenarios map
directly to existing code: the Fujifilm `02 00` configuration and `01 00`
geotag request notifications, the Canon Smart pairing indication, each
Nikon handshake response, Ricoh's `NimBLEConnInfo` security check, Sony
location permission reads and writes, and `Control::connectAll()` retries.

Budget the full 61-method NimBLE surface plus callback classes,
`NimBLEAttValue`, address and UUID types, and the FreeRTOS queue and task
calls `Control` uses. Expect 1,000 to 2,000 lines of test-only code.

Catches connect success and failure, pairing state transitions, shutter
bytes, GPS notification ordering, disconnect handling, and queue-driven
command behavior. Cannot catch actual NimBLE host behavior, radio timing,
or real camera interoperability.

Effort: 4 to 7 days for Fujifilm Basic plus `Control`, 8 to 15 days for all
vendors.

Main risks: `Camera::connect()` enables client self-deletion, so callback
lifetime must be modeled exactly. `Control::connectAll()` holds static
retry state, so each test needs a fresh fixture.

### Tier C: virtual camera peer

A real BLE peripheral that behaves like a camera, tested against real
furble hardware. Unlike `FauxNY`, it advertises camera data, exposes GATT
services, accepts writes, and sends notifications.

Two implementations:

- Mac, Swift plus CoreBluetooth (`CBPeripheralManager`), under `peer/mac/`.
  Limitation: CoreBluetooth may not reproduce exact vendor manufacturer
  advertisement data, so the first Mac peer should target a service-UUID
  matcher (Sony or Canon), not Fujifilm. Bleak is a GATT client and cannot
  implement the peer.
- A second ESP32 running NimBLE, under `peer/esp32-camera/`. This is the
  faithful peer: full control of advertisement bytes, GATT definitions,
  notification timing, and security. Better suited to a self-hosted
  hardware-in-the-loop CI job.

Effort: 3 to 5 days for one unencrypted Mac vendor, 5 to 10 days for an
ESP32 Fujifilm peer, 2 to 4 weeks for pairing, encryption, and all vendors.

### Tier D: protocol conformance corpus

Capture real Fujifilm X100VI traffic and turn stable messages into golden
vectors under `tests/corpus/x100vi/`, normalized by `tools/corpus/`.
`README.md` already documents the Android HCI snoop precedent. Encrypted
GATT traffic needs a sniffer with keys or logging at the NimBLE operation
boundary. Raw captures stay private; the repository holds normalized
vectors only. The X100VI is in the supported camera list; the corpus adds
repeatable protocol-conformance evidence rather than establishing support.

Effort: 1 to 2 days with a working sniffer, 3 to 5 days if key handling
must be solved.

## CI jobs

| Job | Tier | Runner | Runs |
|---|---|---|---|
| `host-protocol` | A | hosted | every PR, compiles protocol sources plus Blowfish, no ESP-IDF or NimBLE dependency |
| `host-camera-scripted` | B | hosted | every PR, compiles camera and control sources against the mock headers |
| `host-corpus-replay` | D | hosted | every PR, feeds normalized vectors into Tier A encoders and Tier B scripts |
| hardware-in-the-loop | C | self-hosted | on demand: furble board, ESP32 peer, serial control, recorded console output |

## Phases

1. Capture a small amount of real Fujifilm traffic in parallel with all
   later work. Captures feed the other tiers.
2. Tier A for Fujifilm. First PR: `FujifilmProtocol.{h,cpp}`, move the
   `geotag_t` byte construction out of `Fujifilm::sendGeoData()` into a
   pure encoder, keep the `m_GeoRequested` gate and the NimBLE write in
   place, add `tests/host/` and the `host-protocol` job. Small and
   shippable, tests a real GPS path, changes no connection behavior.
3. X100VI golden geotag and advertisement vectors from Tier D, replayed
   against the Tier A functions.
4. Tier B for Fujifilm Basic plus `Control`: pairing, connect, shutter,
   geotag request notifications, disconnects.
5. Extend Tiers A and B to Canon Smart, Sony, Ricoh, Nikon Remote, Nikon
   Smart, and Fujifilm Secure. Treat Nikon Smart separately:
   `NikonSmart::finalise()` currently returns failure and is marked not
   functional.
6. Tier C ESP32 virtual peer for hardware-in-the-loop validation of both
   the host mock and the real firmware.

The order is risk-based. Pure encoders are cheap and deterministic. The
mock exercises application behavior without radio dependencies. The peer
validates the real NimBLE stack. The corpus provides compatibility evidence
throughout.

## Risks

- The mock may model the expected protocol instead of actual NimBLE
  behavior. The corpus and the peer are the checks against that.
- Packed structs can differ between host and ESP32 layouts. Tests must
  compare bytes, not structs.
- `Fujifilm::updateGeoData()` only sends after a camera notification.
  Encoder-only tests do not cover the full GPS flow.
- Captured traffic may contain device identifiers or encryption material.
  Review before committing vectors.
- Non-Fujifilm results still need careful interpretation. No such hardware
  is available.

## Verification

- `host-protocol` passes with no ESP-IDF or NimBLE dependency.
- `host-camera-scripted` covers success, failure, drop, shutter, pairing,
  and GPS notification paths.
- Corpus replay matches captured X100VI vectors.
- The ESP32 peer passes the same scripted scenarios over real BLE.
- The normal PlatformIO build remains unchanged.
