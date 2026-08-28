# Plan 142: bounded Bluetooth journal storage

## Goal

Keep the opt-in Bluetooth diagnostic journal useful on every board without
holding an oversized event ring in internal DRAM.

## Implementation

- Keep `BtDebugEvent` as a decoded callback view. The ring stores a compact
  216-byte record with binary six-byte addresses, binary UUIDs, packed flags,
  bounded text, and a 24-byte payload prefix. The decoded event uses the same
  payload bound, so console hex output cannot expand beyond the retained data.
- Non-S3 boards allocate 32 records, for 6912 bytes of journal storage on the
  host and at most 6912 bytes on 32-bit targets. ESP32-S3 builds with
  `CONFIG_SPIRAM` request 128 records from PSRAM, for at most 27648 bytes, and
  fall back to 32 internal records when PSRAM is unavailable.
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

No hardware run was available for this isolated worktree. The `m5stick-s3`
PlatformIO environment uses the generic ESP32-S3-DevKitC-1 board profile,
which advertises no PSRAM. Its checked-in sdkconfig requests SPIRAM support,
but that cannot add memory to the no-PSRAM hardware, so allocation failure
selects the 32-record internal fallback. The `waveshare-s3-eth` environment is
the checked-in S3+PSRAM profile (`BOARD_HAS_PSRAM`, 8 MB octal PSRAM, and
`CONFIG_SPIRAM=y`) where the 128-record PSRAM allocation can be built. Only
the Fujifilm vendor path is hardware-testable under the repository policy.
