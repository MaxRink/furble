# 116 - Companion password authentication

Status: implemented in the firmware. The real BLE handshake remains a hardware
gate. Android and Apple clients use the same versioned packet contract in their
stacked PRs.

## Implementation state

- `Settings::COMPANION_PASSWORD` is an optional write-only string setting. It is
  stored in the existing `furble` NVS namespace and uses wire id 46. Wire id 45
  remains reserved for the IMU setting on the branch that introduced it.
- The companion AUTH characteristic is
  `b57f4f6f-087b-4740-b71d-8262cf26ebbc`. Derived value `0x63` is reserved for
  the Cameras characteristic from plans 50/51. AUTH packets are versioned:
  begin write `[version=1, op=0]`, challenge indication `[1, 0, nonce16]`,
  proof write `[1, 1, hmac16]`, and result indication `[1, 2, result]`.
  The result values are 1 authenticated, 2 rejected, 3 dropped, and 4 not
  required. The client returns the first 16 bytes of HMAC-SHA256(password,
  nonce). Comparison is constant time. The password is never listed or read
  back over the companion protocol.
- Three failed responses drop the companion connection. Disconnect and password
  reload clear the nonce and authentication state. An empty password preserves
  the pre-authentication behavior.
- A successful password write revokes the current session before its settings
  acknowledgement, and the new password requires a fresh challenge.
- Replaced in-memory password and HMAC buffers are erased with volatile stores
  so optimized builds cannot discard the clearing writes. The password is held
  in a fixed 64-byte buffer, and the 63-byte limit is measured over UTF-8
  bytes, not Unicode scalar count or display characters.
- Settings writes and trigger commands require both the existing encrypted,
  bonded BLE link and successful companion-password authentication. Location
  updates remain available on the encrypted companion link because they are an
  external-fix input rather than a camera-control operation.
- Provisioning applies the dedicated `companionPassword` field to the same NVS
  setting as wire id 46. A bundle must not supply both forms, and malformed or
  duplicate password records are rejected before any setting is written.

## Validation

- `tests/host/companion_auth_test.cpp` covers the HMAC vector, challenge state,
  wrong responses, replay, failure limit, disconnect reset, and empty-password
  fallback.
- `tests/protocol/golden/companion_auth.json` is the canonical begin,
  challenge, nonce, proof, and result fixture consumed by firmware, Android,
  and Apple conformance tests.
- `tests/host/companion_gatt_test.cpp` drives the AUTH characteristic through a
  mock central and verifies encrypted pre-auth denial, wrong and replayed HMAC
  responses, fresh-session reset, successful settings and trigger writes, and
  the three-failure disconnect path.
- The host settings round-trip table covers the new string setting and its NVS
  and SD serialization paths.
- `tests/protocol` covers the AUTH UUID and the wire-id 46 settings fixtures.
- Host provisioning tests cover wire-id 46 schema acceptance, malformed
  lengths, dedicated-field persistence, duplicate-source rejection, and
  preflight rollback of the password when another setting is invalid.
- The remaining hardware gate is a real BLE handshake with an equivalent GATT
  client on an M5StickS3. The client must verify that settings and trigger
  writes fail before the HMAC response, succeed after it, and fail again after
  disconnect and reconnect until a new nonce is answered.
- Android and Apple must reject unknown versions, operations, lengths, or result
  values. Both subscribe for indications before sending begin, and only mark a
  session ready after the framed result indication accepts the proof.

## Compatibility and security notes

The password is a shared secret, not a replacement for BLE pairing. The AUTH
characteristic requires the existing authenticated BLE link, and the challenge
prevents a bonded peer that lacks the shared secret from using privileged
companion writes. Clients must treat the nonce as single use and must not cache
the response across connections.
