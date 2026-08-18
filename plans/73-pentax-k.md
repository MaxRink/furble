# 73: Pentax K through the Ricoh Imaging BLE family

Status: implementation complete for code review. Hardware validation is
UNTESTED and needs a PENTAX K-3 III owner. FauxNY and code review are the only
validation available in this repository.

## Motivation

Pentax K bodies are a useful missing target for furble. The public Ricoh
Imaging BLE reference lists PENTAX K1, K-3 Mark III, K-3 Mark III Monochrome,
K70, KF, and KP in the same family as the RICOH GR cameras. The existing
`Ricoh` implementation already recognizes `PENTAX` advertisements, but the
support was only described as partial in plan 61. This plan turns that latent
support into an explicit compatibility claim while keeping the protocol path
shared and the hardware claim conservative.

## Protocol investigation

The Ricoh Imaging reference documents the following shared pieces:

- The Camera Information, Camera, Shooting, GPS, and Bluetooth Control GATT
  services used by `lib/furble/Ricoh.cpp`.
- `OperationRequest` service UUID `9F00F387-8345-4BBC-8B92-B87B52E3091A` and
  characteristic UUID `559644B8-E0BC-4011-929B-5CF9199851E7`.
- A two-byte operation request. Byte zero is the operation code and byte one
  is the parameter. `START` is `1`, `STOP` is `2`, and `AF` is parameter `1`.
- MITM authenticated Bluetooth pairing and GPS location transfer as family
  features. Individual models can still omit optional characteristics or
  expose different availability conditions.

The source reference is:

- https://github.com/dm-zharov/ricoh-gr-bluetooth-api
- https://www.ricoh-imaging.co.jp/english/products/app/image-sync2/connect.html

The reference does not identify a Pentax-only `OperationRequest` format. No
K-3 III owner or HCI capture is available here to prove camera-side acceptance.

## Shared versus new

### Shared with RICOH GR

- Advertisement matching keeps the existing `Ricoh::matches()` service UUID
  checks and its existing case-insensitive `PENTAX` name match.
- Pairing keeps `Ricoh::securityMode()` and the existing authenticated,
  bonded-link checks in `_connect()`.
- Persistence keeps `Camera::Type::RICOH = 9`, the existing `CameraList` load
  and match paths, and the existing serialized address/name record. A Pentax
  target is intentionally not assigned a new persisted type.
- Shutter uses the existing `ShootingFlavor::IMMEDIATE` write followed by
  `OperationRequest { START, AF }`.
- Focus keeps the existing Ricoh behavior, which selects the two-second
  shooting flavor and sends `OperationRequest { START, AF }`. This is not a
  half-press implementation.
- GPS keeps the existing location-control enable write, rate and movement
  gates, byte-swapped doubles, and packed time payload.
- All service and characteristic UUIDs, write-with-response choices,
  subscriptions, and disconnect cleanup remain shared.

### New for Pentax K

There are no new protocol bytes, GATT UUIDs, camera types, settings, NVS keys,
wire IDs, firmware modules, or simulator shims in this change. The production
code change is documentation in the shared `Ricoh` class and its matcher
comment. The existing `PENTAX` matcher is the discovery addition that plan 61
identified, and it already covered the documented K model name format.

No setting was added, so wire ID 41 remains free. There is no settings enum,
table row, default, console switch case, or companion switch case to add.

## Scope and non-claims

This change claims that a supported Pentax K advertisement can enter the
existing Ricoh Imaging protocol path. It does not claim that every listed K
body accepts every shared optional characteristic, that the fallback pairing
code works on every firmware version, or that the two-second focus behavior
matches a physical K-series half press.

The K-3 III owner test must cover discovery, first bond, reconnect, immediate
shutter, the focus/timer command, and GPS injection. A failure should result
in a Pentax-specific command or capability delta in a follow-up rather than a
new type added speculatively here.

## Verification

- FauxNY remains the available non-hardware control-path check. It does not
  prove Pentax GATT acceptance.
- Review checks that the Ricoh/GR command bytes and UUIDs are unchanged.
- Firmware build: `FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e m5stick-s3`.
- No new firmware module was added, so no `sim/shim/<Module>.h` is needed.
  The simulator build is still run as a regression check.
- Hardware status: **UNTESTED**. A K-3 III owner is required before this can
  be called hardware verified.

## Implementation state

Implemented on `feat/73-pentax-k`:

- Broadened the `Ricoh` class documentation to name the Ricoh Imaging family,
  including RICOH GR and PENTAX K bodies.
- Documented the existing `PENTAX` name match in `Ricoh.cpp`.
- Updated the lib/furble guidance to record that PENTAX K uses the shared
  `Camera::Type::RICOH` path pending hardware evidence.
- Added no settings and allocated no wire ID. The next free wire ID remains
  41.
- Added no module and no simulator shim.

Verification results:

- `clang-format --dry-run --Werror lib/furble/Ricoh.cpp lib/furble/Ricoh.h`:
  passed.
- `git diff --check` and changed-file conflict-marker checks: passed.
- `FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e m5stick-s3`: blocked before
  compilation. The first run could not create PlatformIO's global lock in the
  sandbox. With task-local PlatformIO state and the already-installed platform
  and ESP-IDF packages reused read-only, the build reached TinyGPSPlus and
  failed because DNS could not resolve `github.com`. The required retry gave
  the same DNS failure. This was not the TinyGPSPlus fsmonitor quirk.
- `./sim/build.sh`: blocked before compilation because
  `.pio/libdeps/m5stick-s3/M5GFX` is not present in this worktree. No new
  simulator module or shim caused this failure.
- Hardware: UNTESTED. Needs a K-3 III owner. FauxNY and review only.
