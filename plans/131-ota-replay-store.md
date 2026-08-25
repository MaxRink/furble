# 131 — durable OTA replay-store adapter

This slice supplies the durable adapter seam for the `Furble::OTA::MQTT::ReplayStore`
contract. It deliberately does not subscribe to MQTT, write OTA partitions, or
activate an image. Those integrations remain separate hardware-gated slices.

## Journal contract

`JournalReplayStore` uses two fixed 64-byte records. A record contains magic
`FRJ1`, format version, byte length, a serial-number generation, the monotonic
rollback floor, reservation state, owner token, counter, reserved zero bytes,
and CRC-32. The backend has exactly three operations:

1. `read(slot)` reads committed bytes only;
2. `write(slot)` stages a complete inactive slot;
3. `commit()` publishes the staged slot atomically.

The inactive-slot write plus commit-last ordering means a torn write cannot
replace the last complete record. On boot, the newest valid generation wins;
invalid records are ignored when the other slot is valid. An erased pair is
the initial floor-zero state. If both slots contain bytes but neither validates,
the adapter fails closed instead of resetting the anti-rollback floor.

Generation comparison is wrap-safe. Rollback counters are not wrap-safe: a
counter must strictly increase and is never wrapped or lowered. `UINT32_MAX`
is valid terminal exhaustion; no later counter can be represented.

## Reservation state machine

`reserveFloor` is a single global owner-and-counter CAS from idle to reserved
and consumes the counter immediately. `markStaged` requires the exact owner and
counter. `completeReservation` and `abandonReservation` clear the owner while
preserving the consumed floor. `recoverAbandonedReservation` is the reboot
recovery operation; it clears an active owner without lowering the floor and is
idempotent when no reservation exists.

The caller must serialize all operations, including calls made from callbacks.
The adapter intentionally has no mutex or heap-backed state so firmware and
host/sim use the same deterministic logic under the small-target budget.

## Verification

`ota_replay_store_test` exercises owner/CAS rejection, single reservation,
commit/write/read faults, corrupt and truncated records, both-slot failure,
generation rollover, reboot recovery, and 100,000 deterministic randomized
state-machine steps. The same production journal source is compiled into the
host test and simulator. `NvsReplayJournalBackend` maps the seam to ESP-IDF
NVS blobs and `nvs_commit`; the application initializes NVS before `begin()`.
