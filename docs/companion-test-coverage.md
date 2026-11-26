# Companion test coverage

What each companion app actually tests, which CI job runs it, and which layers
have no automated coverage at all. This page is the reference for claims about
companion testing; the per-directory READMEs point here instead of restating the
matrix.

Both companions are Android (`companion/android`) and Apple
(`companion/apple`) clients of the frozen BLE contract in
`include/FurbleCompanion.h`. Neither app has an end-to-end test against real
firmware in CI: everything below stops at a host JVM, a host Swift runtime, a
source scan, or a single app launch on an emulator.

## The matrix

| App | Layer | What it covers | Runs the test | Automated in CI |
| --- | --- | --- | --- | --- |
| Android | JVM unit tests, 28 tests | Wire codecs (location, status, settings TLV, camera records, trigger, capability), the HMAC auth gate, the camera catalog state machine, and the GATT property/flag contract against the firmware declarations | `./gradlew test` → `testDebugUnitTest`, `src/test` | `android.yml`, job `build`, step "Run unit tests and the UI snapshot test" |
| Android | Golden-corpus contract, 6 tests over 384 firmware fixtures | Reads `tests/protocol/golden/` and re-decodes or re-encodes every location, status, settings and trigger fixture the firmware generates, plus the shared auth fixture | `./gradlew test` → `testDebugUnitTest` | `android.yml`, job `build` |
| Android | Robolectric UI snapshots, 6 baselines | Renders the shipped composables at 720x1440: first-launch main screen, permission rationale, connected status, cameras, settings, trigger, and compares each with a committed baseline | `./gradlew test` → `testDebugUnitTest`, `app/src/test/screenshots` | `android.yml`, job `build` |
| Android | Emulator smoke, launch only | Installs the debug APK, launches it with `monkey`, greps logcat for `FATAL EXCEPTION`, checks the process is alive. No screen, no frame, no interaction | `android.yml`, job `smoke`, weekly schedule and manual dispatch | Scheduled and manual only, `continue-on-error: true`, never on a pull request |
| Android | Instrumented tests (`app/src/androidTest`) | Nothing. The source set does not exist | n/a | None |
| Apple | `FurbleCompanionCoreTests`, 46 tests | Frozen location, status, capability, settings and camera records, strict length validation, the HMAC challenge state machine, Keychain storage, connection state machine, device selection, error mapping, the status-view snapshot and the `List` row count | `swift test` and `xcodebuild test` (`FurbleCompanionCoreTests-iOS`, `-macOS`) | `apple.yml`, jobs `swift-package` and `apple-project` |
| Apple | `FurbleCompanionCLIKitTests`, 59 tests | CLI argument parsing, monitor policy and CSV, USB recovery, ROM banner parsing, password resolution | `swift test` only; the Xcode project does not build this target | `apple.yml`, job `swift-package` |
| Apple | UI snapshot and `List` row count | Renders `CompanionStatusView` at 360x168 and compares it with `Tests/Snapshots/CompanionStatusView.1x.png` (`FURBLE_APPLE_SNAPSHOT_UPDATE=1` rewrites it and refuses to run on CI), and counts the rows SwiftUI builds for the status section inside a `List`, which the pixels cannot see | `swift test` and the macOS `xcodebuild test` lane | `apple.yml`, jobs `swift-package` and `apple-project` |
| Apple | XCUITest, simulator UI automation | Nothing. No UI test target exists, so the iOS simulator lane builds and unit-tests the app but never touches it | n/a | None |
| Both | Source contracts, 2 modules | `tests/test_companion_location_contract.py` and `tests/test_companion_identity_enable_contract.py` read the Android manifest, the iOS `Info.plist`, `FurbleLocationProvider.swift`, `FurbleCompanionApp.swift` and `docs/companion.md` and assert the documented declarations are still there | `python3 -m unittest tests.test_companion_*` | `main.yml`, job `Python tooling tests` |
| Both | Shared golden corpus, C++ host suite | Generates `tests/protocol/golden/` from the firmware sources, fails on a diff, and runs `protocol_test` over the fixtures and UUID map | `make -C tests/protocol test` | `protocol-tests.yml`, job `protocol` |

Test counts are the current ones: 40 Android tests (28 JVM, 6 golden corpus, 6
UI snapshot cases) and 105 Apple tests (46 core, 59 CLI). Count them from the reports, not from
this table, if a number matters:

```sh
cd companion/android && ./gradlew test
cd companion/apple && swift test
```

## What nothing covers

These are the layers a user reaches by hand. No job in this repository exercises
them, and no claim in a plan or README should read as if one does.

- **Pairing.** Android `CompanionDeviceManager` association and the macOS/iOS
  OS pairing prompt are not automatable off-device.
- **Reconnect and presence.** `CompanionDeviceService` presence callbacks,
  `OUT_OF_RANGE` transitions, and CoreBluetooth reconnection after a link loss.
  The connection *state machine* is unit tested on Apple and in the Android
  repository layer; the radio path is not.
- **Settings sync.** The TLV codec is tested on both platforms and the settings
  editors are snapshotted on Android, but no test writes a setting to firmware
  and reads it back. The two-step confirmation for link-affecting writes is only
  covered by the JVM gate test.
- **Camera commands.** Selection, connect, disconnect and the RSSI/state
  records are decoded in tests; no test issues a camera command to hardware.
- **Auth against firmware.** The HMAC framing, the golden vector and the local
  state machine are tested. No test completes an exchange with a real device.
- **Location delivery.** The encoder and the platform declarations are tested.
  `FusedLocationProviderClient` and `CLLocationManager` are not driven.
- **Keychain and Android Keystore.** Store/read/delete are unit tested against
  fakes; the real entitlement-protected Keychain and Keystore are not.
- **Background restoration** (`bluetooth-central` state restoration) and
  background location delivery.
- **Firmware update over the companion link.** Neither client implements it.
  Apple states this explicitly; Android has no OTA code path at all. There is no
  flow to test and no test that pretends otherwise.
- **Accessibility, large font scale, dark theme, rotation.** Every snapshot is
  one size, one font scale, one theme.

## Reading the CI runs

- `android.yml` runs on pull requests, on pushes to `master`, weekly on a
  schedule and on demand, filtered to `companion/android/**`. The unit and
  snapshot comparison lines are printed by the Gradle test task, so a passing
  run shows `The main screen matches companion-main.png ... with N differing
  pixels`. A missing line means the step did not run.
- `apple.yml` runs the same way, filtered to `companion/apple/**`. The
  `apple-project` job needs a full Xcode install: `xcodegen` generates
  `FurbleCompanion.xcodeproj` from `project.yml`, and both app targets are built
  and unit tested. The `swift-package` job needs only the Swift toolchain.
- `protocol-tests.yml` runs when the firmware companion sources, the Android
  protocol package, or `tests/protocol/**` change. It is the gate for the golden
  corpus that the Android and Apple codecs are checked against.
- `main.yml` runs the Python source contracts, but its pull request filter
  ignores `companion/android/**`, so an Android-only pull request does not run
  them. `pre-review.yml` does not filter on companion paths either, so an
  Android-only change runs neither. Recorded in
  [plan 474](../plans/474-companion-coverage.md); the Android README states it
  next to the test instructions instead of implying the guards cover it.

## Regenerating the Android screenshots

The committed baselines *are* the documentation gallery. Regenerate them only
after reviewing an intended UI change, then commit the PNGs with that change:

```sh
cd companion/android
./gradlew testDebugUnitTest --tests '*Companion*Test*' -PupdateScreenshots
```

The images land in `companion/android/app/src/test/screenshots/`. A normal
`./gradlew test` compares every one of them and fails with the differing pixel
count and the path of the rendered image.
