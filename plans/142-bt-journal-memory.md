# Plan 142: bounded Bluetooth journal storage

## Goal

Keep the opt-in Bluetooth diagnostic journal useful on every board without
holding an oversized event ring in internal DRAM.

## Implementation

- Keep `BtDebugEvent` as a decoded callback view. The ring stores a compact
  216-byte record with binary six-byte addresses, binary UUIDs, packed flags,
  bounded text, and a 24-byte payload prefix. The decoded event uses the same
  payload bound, so console hex output cannot expand beyond the retained data.
- Non-S3 boards allocate 32 records, for exactly 6912 bytes of journal
  storage. StickS3 requests 128 records from PSRAM, for exactly 27648 bytes,
  and falls back to 32 internal records when PSRAM is unavailable.
- Allocate only while the journal is enabled. `dump` reports capacity, storage
  bytes, and overwritten-record count. Sequence, session, and attempt IDs keep
  lifecycle events correlated after decoding.
- Restore the pre-142 empty-read return behavior. A zero-length read remains a
  successful transport operation until vendor tests establish a semantic
  failure rule.

## Verification

- Host journal test checks storage budget, address and UUID round trips, text
  bounds, payload truncation, sequence order, and loss accounting.
- Host camera journal test checks connect, write, and subscribe events.
- Full host test suite remains required before merge.

## Hardware status

No hardware run was available for this isolated worktree. Only the Fujifilm
vendor path is hardware-testable under the repository policy.
