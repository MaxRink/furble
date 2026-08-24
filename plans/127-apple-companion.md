# 127 - Apple companion foundation

Status: implementation slice. Shared Swift protocol, auth gate, native iOS and
macOS target definition, and host tests are present under `companion/apple`.
The Xcode project is generated reproducibly from `project.yml` with the pinned
XcodeGen release documented in the companion README.

## Delivered

- Frozen UUIDs from plans 50 and 51, including the cameras and reserved Auth
  characteristic used by the password follow-up.
- Strict little-endian codecs for location, status, capability, settings and
  camera records. Short, overlong, invalid-range and invalid-UTF-8 records are
  rejected.
- HMAC-SHA256 challenge proof truncated to 16 bytes. Nonces are single-use,
  comparison is constant time, failures lock the session, and credentials are
  stored only through Keychain on Apple platforms.
- A platform-neutral BLE state machine that requires authenticated capability
  negotiation before status, settings, cameras or trigger operations become
  available. A missing Auth characteristic is an error, never a downgrade.
- CoreBluetooth central adapter with state restoration, MTU-safe payload
  bounds, status notifications, reconnect state, and explicit trigger writes.
- Opt-in Core Location bridge that emits UTC fixes with age and accuracy, and
  never starts location updates merely because a device is paired.
- Shared SwiftUI iOS/macOS app source plus platform privacy usage strings.
- Native iOS and macOS application and unit-test targets, Keychain access-group
  entitlements, Bluetooth restoration declarations, and unsigned CI build
  definitions.

## Follow-up gates

- Firmware must expose the Auth characteristic with the challenge format from
  plan 116 before a physical Apple app can connect. Capability bit 1 is the
  cameras feature from plan 51, not an authentication flag.
- Firmware must expose the cameras characteristic from plan 51 before camera
  rows are shown. The current client capability-gates it.
- The generated project has one unsigned iOS target, one unsigned macOS target,
  and matching unit-test targets around the shared Swift package. CI builds and
  tests both platforms on a pinned macOS/Xcode image. Real iPhone/Mac
  Bluetooth tests still require hardware and a signed build.
- The platform targets declare the shared Keychain access-group entitlement.
  The shared client deliberately does not enable background location unless
  the user opts in.
