# 72: Panasonic Lumix BLE support

## Motivation

Panasonic Lumix cameras are the highest-ranked missing vendor in plan 61.
Owners need the same native BLE shutter, focus, and GPS sidecar support that
furble provides for the other camera vendors. Upstream PR 282 contains a
small implementation based on S5II and BGH1 HCI traces, but it is untested.
This change ports that work onto the fork so the protocol can be reviewed and
tested by a Lumix owner without changing the existing camera API.

## Scope and design

`Lumix` is a new `Camera` subclass in `lib/furble/`. It:

- matches Panasonic manufacturer data with the advertised Lumix session
  service;
- connects without SMP pairing and writes the 16-byte MEI0 session-init
  payload;
- identifies furble through the session device-name characteristic;
- writes one-byte focus and shutter commands to the remote-control
  characteristic;
- writes the 16-byte GPS payload and optional 10-byte clock payload when those
  services are available; and
- serialises the camera name, address, and address type for saved connections.

`Camera::Type::PANASONIC_LUMIX` is assigned value 10. This is a new persisted
enum value and does not renumber any existing camera type. `CameraList` handles
both discovery and deserialisation.

The fork's Camera base has the connection power and adaptive-parameter API
that differs from the upstream PR base. The port keeps that API intact. The
upstream `Camera::toUnixTime()` helper is not present here, so the equivalent
GPS epoch conversion is local to `Lumix.cpp`.

The companion wire model remains unchanged. Its frozen status and location
packets do not carry a camera type, and this branch has no companion camera
type mapping to extend. Lumix support also needs no app setting, so there is
no new `FurbleSettings` enum, NVS key, default, or wire ID. The next free
settings wire ID remains 41.

## Known risks and exclusions

The S5II first-pair handshake is a risk. Plan 61 records reports that the
camera may activate WiFi or require a Lumix Sync-assisted first-pair flow even
though the BLE trace exposes no SMP exchange. The implementation starts with
the direct MEI0 session path and cannot prove that a fresh S5II body will
accept it.

The G9II generation uses an XOR login variant. PR 282 does not implement that
login, and this port deliberately does not claim G9II support. A camera that
advertises the shared session service may be discovered but still fail during
session setup.

## Implementation state

Ported from upstream PR 282 and adapted to fork master. Code review covers the
advertisement matcher, session setup, command bytes, GPS and clock payload
layout, persistence, and registration. No Lumix hardware is available here.
Status: UNTESTED. This is a FauxNY-style review-only port for the app layer,
not a protocol-level hardware validation.

Firmware build verification was attempted with
`FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e m5stick-s3` and one retry. Both
attempts stopped before compilation because the environment could not resolve
`github.com` while installing the pinned TinyGPSPlus dependency. The simulator
build was not run because this change adds no simulator-facing firmware module
and therefore requires no shim or source-list change.

## Verification

- Run `FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e m5stick-s3`.
- Retry the same command once if the first attempt hits the TinyGPSPlus
  fsmonitor install quirk.
- Run `sim/build.sh` only if a simulator-facing firmware module is added. This
  port adds only a library vendor class, so no simulator shim is required.
- Grep changed files for conflict markers after any merge resolution.

The build result and any deviation from this plan are recorded in the
implementation-state section before handoff.
