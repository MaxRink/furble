# Strict virtual BLE boundary

This directory contains a host-only, synthetic BLE transport used for
conformance and negative-path tests. It enforces the GATT database instead of
making every characteristic readable, writable, notifiable, and indicatable.

The runtime records every operation with an adapter ID, connection generation,
timestamp, ATT status, security state, payload, and separate physical outcome.
Trace storage is intentionally unbounded. A test must fail if an operation is
not supported; it must not silently fall back to a permissive fake.

Advertising data is bounded to 31 bytes for legacy advertising and 1650 bytes
for the explicit extended-advertising profile. The shared-pointer `connect`
overload owns the peer through teardown; the reference overload is retained only
for existing stack-owned fixtures.

All results are immutably marked synthetic and ineligible for certification.
Generation-scoped delayed peer and controller events are discarded after link
teardown or reconnect. GATT tables are validated for unique ordered handles and
advertising service references. GATT, GAP, SMP, controller, peer, and physical
faults are distinct and must carry a live generation except for advertising or
connection admission faults.
Long reads and prepare writes are packet bounded. CCCD values are enforced from
0 through 3, and indications require one outstanding confirmation or timeout.
When an IRK is supplied, RPA matching uses the Bluetooth `ah` AES-128 hash and
keeps the resolved address as the on-air address. Exact current-address matching
without an IRK is limited to synthetic capture fixtures.

The `nimble_boundary` files expose only a small deterministic FIFO NPL event
queue and a separate wrap-safe callout queue.
They are not an implementation of NimBLE. Production integration is blocked
until the pinned `esp-nimble-cpp` 2.5.0 C host/controller is placed below this
boundary and receives an independent qualification record from plan 159.

`RicohPeerProfiles` provides immutable synthetic discovery profiles for GR IV,
GR IV HDF, Pentax K-3 III, Pentax K-3 III Monochrome, and an unknown Ricoh
family identity. They expose only the model information service. The UUIDs are
the existing production references, while handles, values, and addresses are
synthetic fixtures. The HDF fixture keeps the observed `GR_H264457` scan name
separate from its synthetic GATT model value. Controller operations are
explicitly unsupported and every runtime result remains synthetic and
uncertified. No profile models capture-backed control writes, pairing prompts,
timing, or a physical shutter outcome.
