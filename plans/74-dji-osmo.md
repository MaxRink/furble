# DJI Osmo Action BLE remote support

## Motivation

The camera compatibility survey ranks DJI Osmo Action 4 and newer as a high-value
target because DJI publishes the protocol and an ESP32 reference implementation.
Adding the remote commands gives furble shutter and recording control for Action 4
and Action 5 Pro without adding an app-layer setting or changing the existing GPS
pipeline.

## Protocol source

The implementation follows DJI's official [Osmo-GPS-Controller-Demo](https://github.com/dji-sdk/Osmo-GPS-Controller-Demo),
which is an ESP-IDF 5.x ESP32 example. The protocol details are in the official
[protocol documentation](https://github.com/dji-sdk/Osmo-GPS-Controller-Demo/blob/main/docs/protocol.md)
and [data segment documentation](https://github.com/dji-sdk/Osmo-GPS-Controller-Demo/blob/main/docs/protocol_data_segment.md).
The official [command implementation](https://github.com/dji-sdk/Osmo-GPS-Controller-Demo/blob/main/logic/command_logic.c)
also provides the record-control device-ID representation used here.

The BLE target service is `0xFFF0`, with notifications on `0xFFF4` and writes on
`0xFFF5`. DJI R SDK frames use `0xAA`, a little-endian version/length field, command
type, sequence, CRC16, command set and command ID, payload, and CRC32. The class
implements the documented connection request (`0x00, 0x19`), camera status
subscription (`0x1D, 0x05`), camera status push (`0x1D, 0x02`), and recording
control (`0x1D, 0x03`) commands.

The protocol data documentation names Action 4 as `0xFF33` and Action 5 Pro as
`0xFF44`. The official record-control example stores Action 4 in a little-endian
`uint32_t` as `0x33FF0000`, which emits `00 00 FF 33` on the wire. The Action 5
value is handled analogously as `0x44FF0000`, emitting `00 00 FF 44`.

## Implemented

- BLE advertisement matching using DJI's documented marker bytes.
- NimBLE central connection, bonding, 517-byte MTU request, service discovery,
  and notification subscription.
- DJI R SDK frame construction and validation with the documented CRC16 and CRC32
  initial values.
- Protocol connection handshake with a stable furble device ID, the ESP32
  Bluetooth MAC, first-pair verification mode, and camera approval response.
- Action 4 and Action 5 Pro device-ID validation.
- Shutter press mapped to recording start and stop toggle through `0x1D, 0x03`.
- Camera status subscription at the documented fixed 2 Hz rate to keep the local
  recording state synchronized.
- Persisted `Camera::Type::DJI_OSMO = 11` and `CameraList` registration.

No furble setting was added. Settings wire ID `41` remains available for the next
setting that needs one.

## Deferred

The DJI GPS push path is deliberately deferred. The compatibility plan describes
its direction as camera-to-remote, opposite furble's usual geotag push, so
`updateGeoData` remains a no-op in v1. The 10 Hz GPS flow, mode switching, version
queries, wakeup behavior, and other DJI commands are also outside this change.

## Implementation state

Implemented in `lib/furble/DJIOsmo.cpp` and `lib/furble/DJIOsmo.h`, with camera type
and persistence registration in `Camera.h` and `CameraList.cpp`. The plan's GPS
scope is intentionally deferred as described above. No deviations from the
requested v1 shutter and recording scope are known.

**UNTESTED:** No DJI Osmo Action 4 or Action 5 Pro hardware is available for
validation. Firmware compilation and simulator checks are the available coverage.

Rebase and verification, 2026-08-19:

- Rebased onto fork master `734b759`. The only conflict was a doc merge in
  `lib/furble/CLAUDE.md`, resolved to keep the master Ricoh Imaging family text
  and add the DJI Osmo line. No conflict markers remain.
- Vendor type id `Camera::Type::DJI_OSMO = 11`. Id 10 was left to the sibling
  Panasonic Lumix branch to resolve the earlier Type-10 collision, so the two
  vendors no longer share a persisted id. Both are the lowest free ids above the
  existing vendors (Ricoh = 9). No settings wire id is added, so the settings
  ledger next-free stays 41.
- Protocol source verified against DJI's own reference
  `github.com/dji-sdk/Osmo-GPS-Controller-Demo`. The frame layout (SOF 0xAA,
  10-bit length, CmdType, SEQ, header CRC16, tail CRC32), both CRC seeds
  (`0x3AA3` / `0x00003AA3`) and polynomials (`0xA001` / `0xEDB88320`), the
  connection command set/id `0x00`/`0x19`, the status subscribe `0x1D`/`0x05`
  and push `0x1D`/`0x02`, the record control `0x1D`/`0x03` with the `0x00`/`0x01`
  start/stop payload byte, and the Action 4 device id `0x33FF0000` all match the
  source exactly.
- Firmware build SUCCESS: `FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e m5stick-s3`.
- Host tests green: `ctest` in `tests/host` reports 7 of 7 passing.
- Protocol goldens green and deterministic: `make -C tests/protocol generate`
  regenerated all 204 fixtures with no diff, and `make -C tests/protocol test`
  passes. This vendor addition does not perturb the golden fixtures.
- clang-format 21 clean on all changed files. No em-dashes.

The SDL simulator does not compile the real vendor registry, so DJI Osmo cannot
be added as a selectable fake alongside FauxNY without new shim wiring, which is
out of scope here.

DECLARED UNTESTED: no DJI hardware is available. Merge is held for a human nod
per project policy for other-vendor additions.
