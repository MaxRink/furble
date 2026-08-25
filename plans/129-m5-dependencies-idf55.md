# M5 dependency compatibility slice

## Scope

This slice updates only the two M5 display-library inputs:

- M5Unified 0.2.13 to 0.2.20 (`774d920cd6851a5231748b56ece1b073645f313f`).
- M5GFX 0.2.19 to 0.2.27 (`93b480bb349749202c8a2a953065c8ae95f58320`).

M5Unified 0.2.20 declares M5GFX 0.2.27 as its dependency. The PlatformIO
registry currently exposes M5GFX 0.2.27, but rejects the old explicit
`m5stack/M5GFX@0.2.19` requirement even though the upstream Git tag remains
available. Keeping the explicit pin at 0.2.19 therefore makes a clean build
unreproducible and lets PlatformIO select the transitive 0.2.27 package.
The firmware, native-PlatformIO simulator, and all three simulator workflows
pin the same pair. The reproducible-input check rejects drift in any one of
those files.

## Compatibility reason

M5Unified 0.2.13 passes `touch_pad_t` through its ESP32 touch helper. The
ESP-IDF 5.5 touch headers no longer define that enum, producing the build
error `touch_pad_t does not name a type`. Upstream M5Unified 0.2.20 changes
the helper and board-detection channel arrays to plain integers and uses the
new touch-sensor API safely. No Furble source compatibility shim is needed.

Official provenance:

- [M5Unified 0.2.20 release](https://github.com/m5stack/M5Unified/releases/tag/0.2.20)
- [M5Unified 0.2.20 source](https://github.com/m5stack/M5Unified/tree/0.2.20)
- [M5GFX 0.2.27 release](https://github.com/m5stack/M5GFX/releases/tag/0.2.27)
- [M5GFX 0.2.27 source](https://github.com/m5stack/M5GFX/tree/0.2.27)
- [PlatformIO M5GFX registry](https://registry.platformio.org/libraries/m5stack/M5GFX)

## Verification gate

The dependency change must remain independently valid on the existing
PlatformIO Espressif32 6.12.0 / ESP-IDF 5.4.2 baseline. Run from one
canonical macOS path with `FURBLE_VERSION=dev FURBLE_TEST=0`:

1. Build all five release boards, the S3 headless environment, and all five
   debug environments.
2. Repeat at least one affected ESP32 target on Espressif32 6.13.0 /
   ESP-IDF 5.5.3 to prove the `touch_pad_t` failure is removed.
3. Run host protocol, camera, simulator, partition, and reproducible-input
   checks required by the current CI.
4. Record warnings and binary sizes. Warnings already present in Furble or
   ESP-IDF do not become dependency fixes in this slice.

No hardware claim is made by this dependency-only change.

## Rollback

Revert this slice's two library pins and lock entries. No application or
sdkconfig migration is part of the change.
