# 161 - Ricoh peer safety and discovery fidelity

## Objective

Keep the Ricoh path fail closed until the peer proves a capture-backed
advertisement identity, GATT model identity, and live OperationMode. The
production route does not treat a model-name advertisement as proof of the
GATT model. Keep GR III, GR IV HDF, Pentax, generic Ricoh, and unknown
Ricoh-family advertisements discovery-only in the host peer.

## Implemented

- Keep advertisement identity and GATT model separate. The observed upstream
  HDF scan identity `GR_H264457` is inert discovery evidence only; production
  does not discover it because no capture-backed production matcher exists.
  A readable model characteristic is still required, and unsupported or
  mismatched values fail connection before application writes.
- Require a readable, exactly one-byte OperationMode characteristic both at
  connect and before every shutter. Missing, unreadable, and multi-byte mode
  values refuse capture instead of treating the state as ready.
- Remove the unverified Location Control write. GPS updates only write the
  documented GPS Information characteristic, and never touch WLAN state.
- Retain the existing 32-byte GPS payload as an explicitly uncertified
  compatibility path. Do not promote the older-family public datum and time
  interpretation to GR IV until an exact GR IV or HDF raw capture and EXIF
  result establish it.
- Remove fixed passkey injection and unconditional numeric-comparison
  acceptance. New pairing is observably unsupported until a lifetime-safe
  application-owned association flow exists; absent input fails closed.
- Replace the host peer's universal service union with an explicit GR IV
  control profile. GR IV HDF, Pentax K-3 III, Pentax K-3 III Monochrome, and
  other names expose only model discovery and cannot receive control writes.
- Add immutable runtime descriptors for GR IV, GR IV HDF, Pentax K-3 III,
  Pentax K-3 III Monochrome, and unknown Ricoh-family identities. Each is a
  synthetic model-discovery profile only: handles, timing, security prompts,
  and control outcomes remain unproven and are not represented as success.
- Add negative coverage for unsupported advertisement identities, mismatched
  advertisement and GATT model values, missing model and mode, malformed
  per-shutter mode reads, explicit association rejection, untouched local and
  camera bond stores, and the synthetic incident path. The host peer drives
  the production numeric-comparison callback rather than auto-bonding.

## Evidence and limits

The on-air name `GR_H264457` and the GR IV HDF identification are tracked in
the upstream issue and pull requests
([#267](https://github.com/dm-zharov/ricoh-gr-bluetooth-api/issues/267),
[#268](https://github.com/dm-zharov/ricoh-gr-bluetooth-api/pull/268),
[#270](https://github.com/dm-zharov/ricoh-gr-bluetooth-api/pull/270)). This
evidence is discovery-only and does not establish a production control route.
The Ricoh API reference is the pinned public repository
`dm-zharov/ricoh-gr-bluetooth-api` at commit
`8c55b79928295f4c0f9a8b0f6f4e1015aeb3d016`, licensed under the Unlicense. Its
GPS Information document describes an older-family layout. It is
reverse-engineered reference material, not GR IV hardware capture, so its
datum, timestamp, and range semantics are not imported into production.
Therefore this plan does not certify GR IV, GR IV HDF, or Pentax behavior.
Exact private handles, timing, association prompts, and physical outcomes
remain `UNCERTIFIED` until captures exist.

The host peer records attempted ATT writes, but the current production Ricoh
path has no accepted, applied, or physical-outcome callback. The runtime
descriptors use only the existing UUID references and synthetic handles. No
test in this plan claims that an ATT write proves a photograph.

## Validation

- `clang-format --dry-run -Werror` on changed C++ files.
- `ricoh-virtual-ble-profile` validates each immutable discovery descriptor
  through the strict runtime, including advertising limits, model reads,
  write permissions, unsupported controller operations, and clean teardown.
- Static source and documentation checks only in this implementation slice.
- Host and simulator builds remain a serial coordinator gate and are not run
  by the implementing agent.

Hardware status: untested. Exact GR IV and GR IV HDF captures, including raw
advertising and GATT evidence, are still required.
