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
partition capacity with the actual inactive partition. The injected
`ReplayStore` loads the installed counter floor before `BEGIN`, rejects counters
less than or equal to it, and atomically reserves the strictly greater counter
after the sink accepts the manifest. The reservation is a durable, owner-bound lifecycle record:
`reserved -> staged -> consumed/cleared`. Only the owner session and counter
may advance it, and a single outstanding reservation globally blocks another
session from beginning. This prevents a lower-counter session from activating
after a higher-counter session has reserved its update. The reservation is a
durable tombstone: aborts, sink failures, and reboots cannot reuse that signed
counter, while any later greater counter stays valid. The store must implement
this as a journaled compare-and-swap, so two sessions racing to reserve a
counter cannot both succeed. A preflight verifier authenticates the complete
signed manifest metadata, including the declared digest, image and partition
sizes, rollback counter, session/transfer owner identity, key id, algorithm,
and version, before `BEGIN` can touch the sink or reserve a counter. Its
`finalize()` repeats the streamed digest equality and verifier checks, but does
not activate the image yet. On commit, the session
stages the verified image, consumes the replay reservation, and only then calls
the sink's irreversible `activate()`. If activation fails, the sink
aborts/discards the staged image and recovery retries only with a new signed
counter. If power is lost between consumption and activation, the old image
boots safely and the update is retried with a newer counter. After reboot, the
platform must discard any staged image and call the explicit once-per-boot
recovery coordinator exactly once. Session construction has no recovery side
effect, and all sessions for one boot share that coordinator. The deferred #66
MQTT composition must initialize NVS, construct the trust-store verifier and
sink, route callback metadata into one Session lifecycle, and wire this
coordinator before accepting BEGIN. A valid signing key can intentionally burn
counters by submitting an authenticated update that is later aborted; arbitrary
signatures cannot. Key custody and counter exhaustion remain operational
controls. This PR never treats metadata presence as authentication and never
permits an unsigned update.

## Delivery policy

- Every message requires MQTT QoS exactly 1 or 2 and `retain == false`. Retained OTA
  commands are rejected to prevent a broker replay on reconnect.
- A duplicate `BEGIN` with the identical session, manifest, and sequence is
  idempotent. A duplicate chunk is idempotent only when its sequence, offset,
  length, checksum, and sink-backed bytes all match. A duplicate commit or abort
  is idempotent only when its terminal sequence matches. Sequence numbers are
  unique within a session, while chunks may arrive out of order. Anti-replay is
  provided by the durable monotonic counter, not by a finite session-ID cache.
- Chunks may arrive out of order. Partial overlaps are rejected, so a retry
  cannot replace bytes already accepted by the sink. Image and partition bounds
  are checked with widened arithmetic. `COMMIT` requires contiguous coverage
  from byte zero through the exact image length. The in-memory ledger stores
  only compact range metadata and never copies image bytes. Its default 64
  entries consume at most the explicit 1024-byte ledger budget, and the total
  persistent `Session` object is compile-time limited to 2048 bytes. The count
  can be lowered at compile time with `FURBLE_OTA_LEDGER_ENTRIES`; exhausting
  the configured ledger fails closed. A flash-backed sink implements `matches()`
  for exact duplicate verification; otherwise a same-range retry fails closed.
- Failed preflight authentication is rejected before the sink or replay store
  is touched. A sink rejection, including `begin()`, failed final verification, or activation
  failure aborts the sink and makes the session terminal. Replay-store load or
 reservation failure fails closed after sink cleanup. A
  successful reservation remains consumed even when the sink later fails. The
  platform adapter must implement `ReplayStore` as an atomic journal/CAS record
  and recover the last complete floor after reboot. Callers must keep the
  coordinator and Session in persistent task/static storage and serialize all
  `onMessage` calls; a
  reentrant callback is rejected with `Busy`; this is a caller serialization
  invariant, not thread safety of the Session object. The later adapter can
  implement the sink with the landed `OTA::Engine`; this contract does not
  duplicate its state machine.

## Integration boundary

This slice intentionally does not add MQTT composition, network authentication,
NVS initialization, trust-store construction, or production callback wiring.
Those belong to #66. Until that integration lands, the OTA characteristics stay
reserved and this shared Session is not a production update path.

## Verification

`tests/host/ota_mqtt_protocol_test.cpp` compiles the production
`src/FurbleOTAMQTT.cpp` directly and covers codec round trips, malformed and
truncated payloads, digest/signature metadata, QoS and retained policy,
out-of-order delivery, exact duplicate delivery, overlap and bounds rejection,
incomplete commit, abort, sink/replay-store failures, sequence reuse, bounded
ledger exhaustion, persistent anti-rollback across more than eight sessions,
same-process and reboot replay after abort/sink/activation failure, competing
counter reservations, and the persistent object-size budget. The same source
is included by the firmware build and is dependency-free, so the simulator and
Nordic port can reuse it without ESP-IDF headers.

## Follow-up gates

The #66 MQTT client must map its event metadata without dropping QoS, retain,
or duplicate flags, and route `cmd/ota` to this session. A real transport must
provide a cryptographic verifier and flash-backed sink, then prove interrupted
download/resume, power-loss rollback, broker redelivery, TLS authorization,
and Home Assistant progress/result topics. Those are not claimed by this
contract and remain hardware or network-gated.
