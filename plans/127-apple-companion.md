# 127 - Apple companion foundation

Status: implementation slice. Shared Swift protocol, auth gate and host tests
are present under `companion/apple`. The iOS and macOS Xcode targets consume the
same SwiftUI source and CoreBluetooth adapter.

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
- Shared SwiftUI iOS/macOS app source plus privacy usage strings.

## Follow-up gates

- Firmware must expose the Auth characteristic and capability bit 1 with the
  challenge format from plan 116 before a physical Apple app can connect.
- Firmware must expose the cameras characteristic from plan 51 before camera
  rows are shown. The current client capability-gates it.
- An Xcode project must add one signed or unsigned iOS target and one macOS
  target around the shared Swift package. CI can run `swift test` now; Xcode
  simulator builds and real iPhone/Mac Bluetooth tests require Xcode and Apple
  SDKs.
- Add the Keychain access-group entitlement in the app project. The shared
  client deliberately does not enable background location unless the user opts
  in.
