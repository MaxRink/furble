# 116 - Companion password authentication

Status: implemented in the firmware. The real BLE handshake and Android client
integration remain hardware and app validation work.

## Implementation state

- `Settings::COMPANION_PASSWORD` is an optional write-only string setting. It is
  stored in the existing `furble` NVS namespace and uses wire id 46. Wire id 45
  remains reserved for the IMU setting on the branch that introduced it.
- The companion AUTH characteristic issues a fresh 16-byte connection-local
  nonce. The client returns the first 16 bytes of HMAC-SHA256(password, nonce).
  Comparison is constant time. The password is never listed or read back over
  the companion protocol.
- Three failed responses drop the companion connection. Disconnect and password
  reload clear the nonce and authentication state. An empty password preserves
  the pre-authentication behavior.
- Replaced in-memory password and HMAC buffers are erased with volatile stores
  so optimized builds cannot discard the clearing writes.
- Settings writes and trigger commands require both the existing encrypted,
  bonded BLE link and successful companion-password authentication. Location
  updates remain available on the encrypted companion link because they are an
  external-fix input rather than a camera-control operation.

## Validation

- `tests/host/companion_auth_test.cpp` covers the HMAC vector, challenge state,
  wrong responses, replay, failure limit, disconnect reset, and empty-password
  fallback.
- `tests/host/companion_gatt_test.cpp` drives the AUTH characteristic through a
  mock central and verifies encrypted pre-auth denial, wrong and replayed HMAC
  responses, fresh-session reset, successful settings and trigger writes, and
  the three-failure disconnect path.
- The host settings round-trip table covers the new string setting and its NVS
  and SD serialization paths.
- `tests/protocol` covers the AUTH UUID and the wire-id 46 settings fixtures.
- The remaining gate is a real BLE handshake with an Android or equivalent GATT
  client on an M5StickS3. The client must verify that settings and trigger writes
  fail before the HMAC response, succeed after it, and fail again after
  disconnect and reconnect until a new nonce is answered.

## Compatibility and security notes

The password is a shared secret, not a replacement for BLE pairing. The AUTH
characteristic requires the existing authenticated BLE link, and the challenge
prevents a bonded peer that lacks the shared secret from using privileged
companion writes. Clients must treat the nonce as single use and must not cache
the response across connections.
