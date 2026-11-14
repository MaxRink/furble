# 127 - Apple companion foundation

Status: delivered (#354, #358, `54e3d1fa`, `2e814905`). Shared Swift protocol, auth gate, native iOS and
macOS target definition, host tests, the capability-gated Cameras tab, and a
headless macOS terminal CLI are present under `companion/apple`.
The Xcode project is generated reproducibly from `project.yml` with the pinned
XcodeGen release documented in the companion README. The Swift package also
builds `furble-companion-cli` without an Xcode project or signed app.

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
- `furble-companion-cli` is a Swift Package executable over the same
  `FurbleCompanionCore`: scan/select by advertised name or macOS peripheral
  UUID, explicit device selection, Auth begin/challenge/proof/result, status,
  capability and camera-catalog printing, phone-GPS fix injection, and paired
  shutter/focus press/release. Password sources are argument, environment,
  file, stdin or Keychain and are never printed.
- `monitor` is a read-only long-run status observer for battery measurements.
  It samples status notifications into CSV with host time/elapsed, uptime,
  battery percent/mV/mA, GPS source/satellites, control state, connected-link
  RSSI and a phase label. BLE drops become marker rows and reconnect through
  the shared client with exponential backoff; duration, `--until-empty`,
  notification timeout and a bounded retry window stop the run cleanly. It
  never writes settings or issues camera/trigger/location commands.
- The host test suite uses Swift Testing and covers the existing codecs/auth
  state machine plus device selection, CLI parsing, monitor stop policy, CSV
  rows/markers and the SVG plot fixture. The native app remains an XcodeGen
  target; the package no longer builds the SwiftUI app itself, so a
  Command-Line-Tools-only Mac can build and test the shared core and CLI.
- `tools/plot_companion_power.py` renders a self-contained SVG with battery
  percent and millivolt panels, phase-label changes and reconnect markers using
  only the Python standard library. PNG output is optional when matplotlib is
  installed.

## Pairing window for headless testing

`companion start` opens the 60 s pairing window from the console and enables the
Companion setting if it is off, so a Mac or phone can discover the device
without navigating the Settings page. It checks the live service after the
reload and rolls the persisted setting back if the service did not start.
`companion stop` persists the setting off and calls the synchronous
`disableNow()`, so an immediate `companion status` agrees with the stop.
`companion status` prints `companion.setting` (persisted) and
`companion.enabled` (live). The settings/companion-write disable path keeps a
one-second grace so the BLE write response can leave; the console stop path
does not. Boot-time enable still waits for the UI row because advertising a
pairing window at every boot would be wrong.

## Terminal and transport state

`furble-companion-cli` maps ATT/CB insufficient-encryption errors to a link
security failure with a pairing-prompt message, while notification-state and
write failures use the same mapping. The native app renders
`CompanionFailure.localizedDescription`, so both Apple clients share the
actionable text. A camera catalog timeout is reported after the BLE client
resets its state, and the status report snapshots authentication/capability
first so the printed report does not claim the link was never authenticated.

The monitor treats `battery_ma=0` and `battery_mv=0xffff` as unavailable
sentinels: CSV keeps the raw fields, human/summary output says `unknown`, and
the plotter drops only the voltage point while keeping the percentage sample.
`--transport usb` uses the StickS3 CDC console for scan, status, cameras,
monitor and password status; location and trigger remain BLE-only.

## Companion pairing dialog

The companion confirmation prompt now uses the compact camera-pairing layout:
a panel-width-capped msgbox with a symmetric margin, a wrapped instruction, a
large centered six-digit code, and `Yes`/`No` footer buttons on narrow panels.
The old content-sized `lv_msgbox` grew wider than the 135 px Stick display and
clipped the title, number, and both footer pills. The three companion modal
scenarios assert `ui.modal_overflow no`, so a future content or font change that
pushes the dialog off the panel fails in the simulator.

## BLE pairing and memory state

The companion GATT service now initiates link security from `onConnect()` for
an accepted companion link. Without that, a macOS central sees an ATT
insufficient-encryption error on the capability read and never starts pairing.
The IO capability is `NoInputNoOutput`, producing an encrypted and bonded LE
Just Works link because macOS did not present a usable numeric-comparison
prompt to a terminal/background client. The companion HMAC password remains the
application authentication layer; a device with no password reports the
explicit not-required result.

A stale companion bond is deleted when the pairing window is open and the peer
presents a different LE address. macOS rotates its LE address between sessions,
and without this clear the next connection tried to encrypt with keys the peer
no longer had and silently stayed unauthenticated. The accepted link logs
`encrypted`, `authenticated`, and `bonded` state so this is diagnosable.

On the StickS3, `sdkconfig.m5stick-s3` moves mbedTLS allocations to PSRAM
(`CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC`). Secure Connections key generation was
allocating a 68-byte block from a nearly exhausted internal heap and panicking;
the external allocation keeps the internal heap for the BLE host and tasks.
Hardware verification: status, monitor, password-required auth, password-clear
not-required auth, and three reconnect cycles after the stale-bond clear. No
camera was attached, so trigger/camera success remains a hardware gate.

## Follow-up gates

- A physical Apple app connection requires firmware exposing the Auth
  characteristic with the challenge format from plan 116. Capability bit 1 is
  the cameras feature from plan 51, not an authentication flag.
- Firmware exposes the cameras characteristic from plan 51
  (`include/FurbleCompanion.h` `CAMERAS_UUID`) and the Auth characteristic
  (`AUTH_UUID`). The current client capability-gates it and reports when the
  feature is unavailable.
- The generated project has one unsigned iOS target, one unsigned macOS target,
  and matching unit-test targets around the shared Swift package. CI builds and
  tests both platforms on a pinned macOS/Xcode image. Real iPhone/Mac
  Bluetooth tests still require hardware and a signed build.
- The pinned image is macos-15 with Xcode 16.4. GitHub retired macos-14 on
  2026-11-02 and cancels jobs on that label before any step runs. Xcode 16.2
  is still on macos-15, but its SDK predates the image's iOS 18.5 simulators,
  so the pin moved to the image default. The local `swift test` with the
  Command Line Tools passes; the Xcode project jobs are verified only in CI.
- The uploaded macOS app is an unsigned CI test artifact. Its source SHA and
  pinned Xcode version are recorded beside the archive; this does not claim
  signing, notarization, installation, or release support.
- A companion testing release must use the reserved `companion-test-` prefix;
  it must not be treated as a firmware release or installer input.
- The platform targets declare the shared Keychain access-group entitlement.
  The shared client deliberately does not enable background location unless
  the user opts in.
- A physical macOS CLI run needs Bluetooth permission for the launching
  terminal and a bonded/encrypted companion link before the firmware Auth
  characteristic will answer. The unsigned macOS app and the CLI were not
  exercised against hardware in this worktree.
- No camera is attached on the bench used for this slice. Camera list/state
  decoding is host-tested, but camera connect/select/disconnect success is not
  certified and remains a hardware gate.
- Battery observations must treat `battery_ma == 0` as an absent current
  backend, not a measurement. A full-drain comparison also needs the same
  screen, GPS policy, BLE interval and radio distance across runs; the
  documented procedure is in `docs/companion.md`.
