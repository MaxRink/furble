# furble companion for Apple platforms

This directory contains the shared Swift protocol and security layer plus one
SwiftUI source set that is built as two app targets in Xcode:

- `furble companion iOS`, deployment target iOS 16, with `bluetooth-central`
  state restoration and optional user-enabled location tracking.
- `furble companion macOS`, deployment target macOS 13, using the same
  CoreBluetooth client and SwiftUI screens.

The package target `FurbleCompanionCore` is the host-testable part. It covers
the frozen little-endian location, status, capability, settings and camera
records, strict length validation, the HMAC challenge state machine, Keychain
credential storage, and a deterministic BLE connection state machine.

The app refuses to enter `ready` when the Auth characteristic or the advertised
HMAC capability is missing. There is no insecure password or BLE-security
downgrade. The firmware Auth characteristic from the follow-up companion
security slice must land before a physical Apple link can be used.

## Host tests

```sh
cd companion/apple
swift test
```

The command needs the Swift toolchain and CryptoKit. It does not need an Apple
account, code signing, an iPhone, or a Bluetooth peripheral.

## Xcode setup

Create an iOS App and a macOS App in one Xcode project, add this directory as a
local Swift package, and add the shared `FurbleCompanionApp/main.swift` source
to both targets. Add `AppResources/Info.plist` to the iOS target and enable
App Sandbox Bluetooth access for the macOS target. Keep signing disabled for
CI simulator builds.

The iOS target must declare `bluetooth-central` background mode. Location
background tracking is opt-in and must not be enabled merely by pairing. The
privacy manifest and usage strings explain the purpose of every protected API.

## Physical gates

An iPhone or Mac with Bluetooth, a password-enabled furble firmware build, and
an authenticated BLE bond are required for end-to-end testing. Verify pairing,
auth success and failure, reconnect, status notifications, settings writes,
camera commands, dead-man release, and background restoration on both Apple
targets. OTA remains intentionally absent from this client foundation until
the firmware OTA trust and signed-image policy are complete.
