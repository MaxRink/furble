# 126 - Concurrent settings handles

## Motivation

The UI, GPS, companion, and background services can load settings from separate
tasks. `Settings` previously reused one static `Preferences` object for every
operation. Its `begin()` and `end()` calls mutate the object's active handle, so
overlapping loads raced even though ESP-IDF's underlying NVS API is thread-safe.
The GPS concurrent-page ThreadSanitizer gate exposed the same race in the host
simulator.

## Implementation state

- Every settings load, save, migration, and default-existence check uses an
  operation-local `Preferences` instance.
- The shared mutable handle was removed. Calls can overlap without one task
  closing or replacing another task's handle.
- Stored values, namespaces, defaults, wire ids, and public settings APIs are
  unchanged.

## Verification

- Run the full host settings round-trip and host CTest suites.
- Run the complete simulator E2E suite.
- Run `gps-concurrent-pages.txt` under ThreadSanitizer without suppressing
  Furble settings or Preferences frames.
- Build every firmware environment. No hardware behavior changes, so there is
  no hardware-only merge gate.
