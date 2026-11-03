# Plan 119: companion GATT host coverage

## Motivation

The companion service is transport-independent, but before this plan its
location, status, settings, trigger, authentication, and disconnect behavior
had no executable end-to-end host coverage. That left the GATT boundary and
the real Control handoff dependent on hardware-only checks.

## Implementation

- Add a UUID-addressed `CompanionTransport` mock central that exercises the
  production `CompanionService`.
- Replace only the NVS, battery, GPS, feedback, power, platform, and timer
  surfaces that require ESP-IDF or hardware. The production companion service,
  Control, Camera, and Fujifilm protocol remain linked.
- Route multiple virtual Fujifilm cameras by BLE address in `MockNimBLE`.
- Cover numeric pairing confirmation, status reads, companion GPS geotagging,
  authenticated and unauthenticated settings and trigger writes, and the
  two-camera shutter path.

## Deviations and residual risk

The host transport is not a substitute for the ESP NimBLE server, encrypted
pairing, or Android GATT client. Real BLE security, characteristic callbacks,
MTU negotiation, and companion app interoperability remain hardware or app
tests. Only Fujifilm is exercised by the available virtual camera.

## Verification

- Exact rebased head builds the complete host suite and passes all registered
  tests, including `companion-gatt`.
- The simulator builds and its complete default-panel end-to-end scenario set
  passes. This PR changes no simulator behavior.
- Firmware CI remains the release-build gate for all supported boards.
