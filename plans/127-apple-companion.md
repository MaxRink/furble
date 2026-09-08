# 127 - Apple companion foundation

Status: implementation slice. Shared Swift protocol, auth gate, native iOS and
macOS target definition, host tests, and the capability-gated Cameras tab are
present under `companion/apple`.
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
- Auth packets enforce plan 116's version/op framing and the begin, challenge,
  proof, result sequence; an unsolicited or out-of-order indication fails the
  connection instead of changing authentication state.
- A platform-neutral BLE state machine that requires authenticated capability
  negotiation before status, settings, cameras or trigger operations become
  available. A missing Auth characteristic is an error, never a downgrade.
- CoreBluetooth central adapter with state restoration, MTU-safe payload
  bounds, status notifications, reconnect state, and paired press/release
  trigger writes for shutter and focus.
- Opt-in Core Location bridge that emits UTC fixes with age and accuracy, and
  never starts location updates merely because a device is paired.
- Shared SwiftUI iOS/macOS app source plus platform privacy usage strings.
- Capability-gated Cameras section with stable camera IDs, selection toggles,
  connect-selected, disconnect, live state, RSSI, and firmware request errors.
- Native iOS and macOS application and unit-test targets, Keychain access-group
  entitlements, Bluetooth restoration declarations, and unsigned CI build
  definitions.
- The Apple workflow gives the macOS test an explicit DerivedData path and,
  after successful tests, uploads an unsigned Debug app with a SHA-256
  checksum and source/Xcode provenance. It is a testing artifact, not a
  signed release.
- Release tags beginning with `companion-test-` are reserved for companion
  testing and skip the firmware release workflow. Normal firmware release tags
  retain the existing build and publication path.

## Follow-up gates

- A physical Apple app connection requires firmware exposing the Auth
  characteristic with the challenge format from plan 116. Capability bit 1 is
  the cameras feature from plan 51, not an authentication flag.
- Firmware must expose the cameras characteristic from plan 51 before camera
  rows are shown. The current client capability-gates it and reports when the
  feature is unavailable.
- The generated project has one unsigned iOS target, one unsigned macOS target,
  and matching unit-test targets around the shared Swift package. CI builds and
  tests both platforms on a pinned macOS/Xcode image. Real iPhone/Mac
  Bluetooth tests still require hardware and a signed build.
- The uploaded macOS app is an unsigned CI test artifact. Its source SHA and
  pinned Xcode version are recorded beside the archive; this does not claim
  signing, notarization, installation, or release support.
- A companion testing release must use the reserved `companion-test-` prefix;
  it must not be treated as a firmware release or installer input.
- The platform targets declare the shared Keychain access-group entitlement.
  The shared client deliberately does not enable background location unless
  the user opts in.
