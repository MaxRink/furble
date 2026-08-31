# furble companion for Apple platforms

This directory contains the shared Swift protocol and security layer plus one
SwiftUI source set that is generated into two native app targets:

- `furble companion iOS`, deployment target iOS 16, with `bluetooth-central`
  state restoration and optional user-enabled location tracking.
- `furble companion macOS`, deployment target macOS 13, using the same
  CoreBluetooth client and SwiftUI screens.

The package target `FurbleCompanionCore` is the host-testable part. It covers
the frozen little-endian location, status, capability, settings and camera
records, strict length validation, the HMAC challenge state machine, Keychain
credential storage, deterministic BLE connection state, and paired trigger
press/release state.

The app refuses to enter `ready` when the Auth characteristic is missing. There
is no insecure password or BLE-security downgrade. The firmware Auth
characteristic from the follow-up companion security slice must land before a
physical Apple link can be used. Capability bit 1 is reserved for the cameras
characteristic, matching plans/51 and the firmware cameras slice.

Shutter and focus are hold controls. A touch sends one press packet and its
matching release packet when the touch ends or the view disappears. The
host-tested hold state suppresses duplicate presses and restores state after a
failed write so a transient Bluetooth error can be retried safely.

On first launch, or after choosing **Change password**, the shared iOS/macOS
SwiftUI screen accepts a password and confirmation through secure fields. It
validates the UTF-8 byte count against the firmware's 63-byte limit, writes the
value to the platform Keychain, and clears both entry fields after save, cancel,
or failure. A saved password is represented only as a presence state; it is
never rendered, printed, or copied into app preferences. **Delete password**
removes the Keychain item and disconnects the BLE client. It reconnects only
after a new password is saved.

## Host tests

```sh
cd companion/apple
swift test
```

The command needs the Swift toolchain and CryptoKit. It does not need an Apple
account, code signing, an iPhone, or a Bluetooth peripheral.

## Native Xcode project

The checked-in `project.yml` is the source of truth for the native iOS and
macOS application and unit-test targets. Generate the project with the pinned
XcodeGen release:

```sh
cd companion/apple
scripts/generate-xcode-project.sh
```

The generated `FurbleCompanion.xcodeproj` is intentionally ignored. CI uses
the same script before building, so local and CI projects cannot silently
drift. The two targets use bundle IDs `com.furble.companion.ios` and
`com.furble.companion.macos`; replace those IDs and the team settings in an
unsigned local copy before distribution.

Open the generated project in Xcode after running the script. For a signed
build, set a real `DEVELOPMENT_TEAM`, distribution or development
`CODE_SIGN_IDENTITY`, provisioning profiles, and `CODE_SIGNING_ALLOWED=YES` in
your local Xcode configuration. Those values are deliberately absent from the
repository and CI.

Unsigned simulator builds and tests are available with:

```sh
xcodebuild -project FurbleCompanion.xcodeproj -scheme FurbleCompanion-iOS \
  -sdk iphonesimulator -configuration Debug CODE_SIGNING_ALLOWED=NO \
  build-for-testing
xcodebuild -project FurbleCompanion.xcodeproj -scheme FurbleCompanion-macOS \
  -sdk macosx -configuration Debug CODE_SIGNING_ALLOWED=NO \
  build-for-testing
```

The iOS and macOS entitlements declare the shared Keychain access group
`$(AppIdentifierPrefix)com.furble.companion.shared`. A signed distribution must
use a real Apple team whose provisioning profiles contain that group. The app
reads the expanded group from its platform Info.plist and passes it to the
Keychain store. No signing identity, certificate, or provisioning profile is
checked in.

The iOS target must declare `bluetooth-central` background mode. Location
background tracking is opt-in and must not be enabled merely by pairing. The
privacy manifest and usage strings explain the purpose of every protected API.

## Physical gates

An iPhone or Mac with Bluetooth, a password-enabled furble firmware build, and
an authenticated BLE bond are required for end-to-end testing. Verify pairing,
auth success and failure, reconnect, status notifications, settings writes,
camera commands, dead-man release, and background restoration on both Apple
targets. Simulator builds do not exercise CoreBluetooth, Keychain entitlements,
pairing, or real location delivery. OTA remains intentionally absent from this
client foundation until the firmware OTA trust and signed-image policy are
complete.
