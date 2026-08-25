# 130 - OTA-over-MQTT signed transfer contract

Status: implementation slice on fork/master `ef8aca7` (follow-up to PR #213). This is the
transport-neutral contract that can land before the WiFi and MQTT transports.
It is deliberately separate from #66 and #161 so their rebases do not make the
OTA safety policy unreviewable.

## Goal

Give the firmware, simulator, MQTT client, and future Nordic port one binary
OTA envelope and one delivery policy. The contract is host-testable and does
not open a network socket, write flash, reboot, or claim that a signature was
verified.

## Wire contract

Each MQTT payload is a little-endian `FOA1` envelope with a kind, 128-bit
session id, non-zero sequence number, and exact body length. `BEGIN` carries a
non-empty version, image length, partition capacity, non-zero SHA-256 digest,
an 8-byte key id, a monotonic rollback counter, and a 64-byte Ed25519 or
ECDSA-P256 signature. The signature is raw `r || s` for ECDSA, not DER. `CHUNK`
carries an offset, up to 4096 bytes, and CRC-32. `COMMIT` and `ABORT` have empty
bodies. Unknown kinds, truncation, length arithmetic overflow, unsigned
manifests, invalid digest/key metadata, and bad checksums are rejected before a
sink is called.

`canonicalSignedBytes()` emits a deterministic `FOM1` byte string containing
the session, image and partition sizes, rollback counter, digest, algorithm,
key id, and version. The injected sink's `begin()` must compare the requested
partition capacity with the actual inactive partition and reject a rollback
counter below its stored floor. Its `finalize()` must verify the signature over
these canonical bytes using the key-id trust anchor, verify the complete digest,
and make the image bootable. This PR never treats metadata presence as
authentication and never permits an unsigned update.

## Delivery policy

- Every message requires MQTT QoS exactly 1 or 2 and `retain == false`. Retained OTA
  commands are rejected to prevent a broker replay on reconnect.
- A duplicate `BEGIN` with the identical session, manifest, and sequence is
  idempotent. A duplicate chunk is idempotent only when its sequence, offset,
  length, checksum, and sink-backed bytes all match. A duplicate commit or abort
  is idempotent only when its terminal sequence matches. Sequence numbers are
  unique within a session, while chunks may arrive out of order. A bounded
  eight-entry terminal-session window prevents replay across recent sessions.
- Chunks may arrive out of order. Partial overlaps are rejected, so a retry
  cannot replace bytes already accepted by the sink. Image and partition bounds
  are checked with widened arithmetic. `COMMIT` requires contiguous coverage
  from byte zero through the exact image length. The in-memory ledger stores
  only up to 4096 range records and never copies image bytes. A flash-backed
  sink implements `matches()` for exact duplicate verification; otherwise a
  same-range retry fails closed.
- A sink rejection, including `begin()`, or failed final verification aborts the
  sink and makes the session terminal. A bounded eight-entry terminal window is
  deliberate: replay protection is guaranteed for the retained window without
  unbounded RAM. The later adapter can implement the sink with the landed
  `OTA::Engine`; this contract does not duplicate its state machine.

## Verification

`tests/host/ota_mqtt_protocol_test.cpp` compiles the production
`src/FurbleOTAMQTT.cpp` directly and covers codec round trips, malformed and
truncated payloads, digest/signature metadata, QoS and retained policy,
out-of-order delivery, exact duplicate delivery, overlap and bounds rejection,
incomplete commit, abort, sink failures, and terminal replay. The same source
is included by the firmware build and is dependency-free, so the simulator and
Nordic port can reuse it without ESP-IDF headers.

## Follow-up gates

The #66 MQTT client must map its event metadata without dropping QoS, retain,
or duplicate flags, and route `cmd/ota` to this session. A real transport must
provide a cryptographic verifier and flash-backed sink, then prove interrupted
download/resume, power-loss rollback, broker redelivery, TLS authorization,
and Home Assistant progress/result topics. Those are not claimed by this
contract and remain hardware or network-gated.
