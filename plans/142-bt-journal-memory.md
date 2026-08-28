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

- Focused host checks: 5/5 passed, covering the journal and camera event
  paths, scan-start failure and logical-state unwind, plus the S3+PSRAM and
  S3-without-PSRAM capacity profiles.
- Fresh full host build and suite after rebasing onto `46d0fe9a`: 69/69
  tests passed. The journal checks cover storage budget, address and UUID
  round trips, text bounds, binary-advertisement hex output, payload
  truncation, sequence order, loss accounting, and disconnect identity.
- `clang-format --dry-run --Werror` and `git diff --check` passed.
- `pio run -e waveshare-s3-eth-debug` identified the checked-in 8 MB PSRAM
  profile but stalled in ESP-IDF `get_cmake_code_model` during CMake
  configuration, before project compilation; no firmware artifact was
  produced.

## Hardware status

The attached M5StickS3 was not flashed: cameras were powered off, and flashing
was intentionally prohibited because this master still carries the unsafe
10-second PM1 watchdog path. Use the separate watchdog fix before hardware
validation. Its generic ESP32-S3-DevKitC-1 PlatformIO profile advertises no
PSRAM even though the checked-in sdkconfig requests SPIRAM, so runtime
allocation must use the 32-record internal fallback. The checked-in
`waveshare-s3-eth` environment declares `BOARD_HAS_PSRAM`, 8 MB octal PSRAM,
and `CONFIG_SPIRAM=y`; that is the S3+PSRAM build boundary for the 128-record
allocation. Ricoh is supported and available for hardware validation when
powered; no camera was powered for this check. The journal is RAM-only and
never changes bonds or NVS.
