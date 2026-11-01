# 289: camera parity, virtual peers, and safe fixes

## State

This PR is stacked on `firmware-compat-audit` (`#317`). It carries the camera
parity matrix, the nine-peer virtual camera fleet, and the safe fixes that are
implemented from firmware-static or app-static evidence.

Completed in this branch:

- `docs/camera-research/parity-matrix.md` classifies 176 Furble actions and 340
  camera feature rows, with safe fixes, capture gates, simulator state, test
  state, and hardware state.
- New peers: Canon Smart, Canon Remote, Nikon Remote, Nikon Smart, Sony, LUMIX,
  DJI. Existing Fujifilm and Ricoh peers are refined with the handler findings.
- The SDL simulator advertises all nine peers and persists their CameraList
  entries.
- `peer-contract` covers every peer.
- LUMIX safe fix: focus is a no-op; connect sends Confirm `01`, waits for
  control readiness `00`, then Start `02`; disconnect sends Finish `03`.
  `lumix-session` proves the gate against the peer.
- Fujifilm safe fix: Basic and Secure client names are NUL terminated. The
  `F9150137` `01 00` request now writes the 12-byte UTC time packet, with the
  7-byte local form as a fallback. Offset and DST are zero.
- Canon Smart safe fix: the GPS timestamp uses `CanonProtocol::utcEpoch`, not
  `mktime`, so it no longer depends on the runtime timezone. `canon-time` pins
  the epoch and leap-day rules.
- Ricoh safe fixes: connect writes the Paired Device Name. The Date Time Zone
  packet is written on the first valid GPS time and once per minute. The
  readback and the CAPTURE gate remain capture-gated.
- Sony safe fixes: subscribe DD01, read DD21, select the 91 or 95 byte form,
  and release DD30 and DD31 on disconnect. Non-zero timezone and DST remain
  capture-gated.
- Nikon Smart now stops at the BLE authentication result, reports the
  Bluetooth Classic blocker, and no longer waits for a final OK that is a time
  request.
- Fujifilm location sync is enabled with `AAB609C4` before the first geotag
  packet and read back once.
- LUMIX sends the GPS stop packet (fix `0x56`, sentinel coordinates) on
  disconnect before Finish.

## Evidence

- LUMIX Confirm, Start, Finish, and readiness: `app-static` LUMIX Sync 2.0.17,
  private `spec/panasonic.md` C06 to C08, F01, F40.
- Fujifilm and Ricoh peer refinements: `firmware-static` handler notes.
- Nikon `2083` and `2084`: `firmware-static` B600 handler note.
- Sony, LUMIX, DJI peer models: the private specs, with unproven paths rejected
  or behind explicit config flags.

## Deviations and limits

- No hardware capture was made. Runtime claims remain capture-gated.
- The remaining safe fixes are tracked in the parity matrix. S13 is
  reclassified as not representable because the Fujifilm disconnect-reason
  handle is inert on the audited images. The remaining code work is UI
  capability gating, Ricoh capture-status confirmation, and the LUMIX
  location and clock readiness gates.
- The peers model the documented protocol; they do not certify a physical
  camera outcome.

## Validation

- `cmake --build` on the host harness: pass.
- `ctest`: 125/125 pass, including `peer-contract`, `lumix-session`,
  `canon-time`, `ricoh-time`, `sony-location`, and the Fujifilm protocol time
  vector.
- `python3 -m unittest`: 192 tests pass.
- `./sim/build/furble-sim --script sim/scenarios/e2e/ble-peer-topology-smoke.txt`:
  pass, asserts 9 peers.
- `python3 tools/check_sim_scenarios.py`: manifest complete.
- clang-format clean for the changed files.
