# lib/furble/ (BLE camera protocol library)

NimBLE-based camera protocol implementations. This is the future portable
protocol core.

- Must stay free of app-layer includes: no FurbleSettings.h, FurbleUI.h, or
  anything else from include/ or src/. Allowed deps: NimBLE, ESP-IDF,
  lib/blowfish, lib/preferences.
- `Camera` is the abstract base (also NimBLEClientCallbacks). Each vendor mode
  is one subclass pair (.h/.cpp): Fujifilm Basic/Secure, CanonEOS
  Smart/Remote, Nikon Smart/Remote, Sony, Ricoh, and FauxNY (software test
  camera).
- New camera types get a new `Camera::Type` enum value. Values are persisted
  in NVS: never renumber or reuse existing ones (MOBILE_DEVICE is deprecated
  but its value stays reserved).
- `CameraList` handles persistence of paired cameras, `Scan` handles
  advertisement matching and discovery.
- `Camera` exposes connection RSSI and keeps the configured power cap separate
  from the runtime connection level. NimBLE transmit power calls take dBm, so
  map the supported P3, P6 and P9 enum levels explicitly.
- Vendor protocol files are per-camera. Any change here needs the
  hardware-tested-vendors statement in the PR: only Fujifilm is testable on
  real hardware, all other vendors must be declared untested.
