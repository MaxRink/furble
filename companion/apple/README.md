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

The Auth characteristic uses the plan 116 framing end to end: begin is
`[1,0]`, the challenge is `[1,0,<16-byte nonce>]`, proof is
`[1,1,<16-byte HMAC-SHA256 prefix>]`, and the result is
`[1,2,<status>]`. The client accepts a challenge or result only in response to
the begin/proof exchange it sent.

The app refuses to enter `ready` when the Auth characteristic is missing. There
is no insecure password or BLE-security downgrade. The firmware Auth
characteristic and plan 116 framing are required before a physical Apple link
can be used. Capability bit 1 is reserved for the cameras characteristic,
matching plans/51 and the firmware cameras slice.

After authentication, the Cameras section is shown only when capability bit 1
and the Cameras characteristic are present. It lists the saved camera catalog
by stable ID, shows live connection state and RSSI, supports selection,
connect-selected and disconnect, and displays firmware request errors. Simulator
and unsigned builds do not prove that camera BLE operations work on hardware.

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

## macOS terminal CLI

`FurbleCompanionCLI` is a Swift Package executable target. It reuses
`FurbleCompanionCore` for the packet codecs, HMAC auth exchange,
`CompanionStateMachine`, CoreBluetooth client, trigger hold state and Keychain
storage; the CLI does not contain a second packet parser. It runs without
Xcode, an iPhone, a signed app, or an AppKit window.

Build and run it from this directory:

```sh
cd companion/apple
swift build -c release --product furble-companion-cli
.build/release/furble-companion-cli help
```

The debug `swift run furble-companion-cli ...` form also works during
development. `swift build -c release` is preferable for an hour-long battery
run because the release binary has no debug-build pause/symbol overhead.

Common commands:

```sh
# List advertising furble devices by advertised name, macOS UUID and RSSI.
.build/release/furble-companion-cli scan --scan-timeout 10

# Connect, run the Auth begin/challenge/proof/result flow, then print status.
.build/release/furble-companion-cli status --device Furble

# One-shot phone-GPS fallback test.
.build/release/furble-companion-cli location --device Furble \
  --latitude 52.5 --longitude 13.4 --altitude 34 --accuracy 5 --age-ms 100

# Press and release shutter/focus on the same authenticated link.
.build/release/furble-companion-cli trigger shutter --device Furble --hold-ms 500
.build/release/furble-companion-cli trigger focus --device Furble --hold-ms 500

# Read-only long-run power observation. Rows are flushed as they are written.
.build/release/furble-companion-cli monitor --device Furble \
  --interval 5 --duration 3600 --csv --label "GPS off" > gps-off.csv

# Stop at 1% battery or when the retry window expires after power-off.
.build/release/furble-companion-cli monitor --device Furble \
  --interval 5 --until-empty --csv --label "always-on" > always-on.csv
```

`--device` accepts a case-insensitive name substring or the macOS peripheral
UUID printed by `scan`. macOS exposes a per-host peripheral UUID rather than
the BLE MAC address. Without `--device`, the CLI scans and requires exactly one
matching furble device.

### Monitor CSV

`monitor --csv` writes only rows to stdout. Progress, warnings and the final
summary go to stderr, so `> file.csv` remains clean. The header is:

```text
host_time_iso,host_elapsed_s,uptime_s,battery_percent,battery_mv,battery_ma,gps_source,gps_satellites,control_state,rssi,label
```

Example row:

```text
2026-11-04T12:34:56Z,300.2,1234,87,4012,0,2,0,0,-61,GPS off
```

`battery_ma` is the raw signed firmware field. Some firmware has no PMIC
current backend and reports 0; the CLI does not derive or invent a current. It
prints a one-time stderr warning if the first sample is 0 mA. Use
`battery_percent`, `battery_mv` and their slope over time. `rssi` is the
connected-link RSSI requested every sample and may be empty until the first
reply.

A BLE drop does not end the run. The CLI emits a marker row with empty
measurement fields and a `label` of `link-lost`, then retries with exponential
backoff (1, 2, 4 ... 30 s) through the shared client's scan/auth path. A
successful reconnect adds a `reconnected` marker. The run stops cleanly with `retry-exhausted` after
`--retry-window` seconds (default 180) without a reachable device, with
`status-timeout` if notifications stop, or with `battery-empty` when
`--until-empty` sees 1% or less. A duration/until-empty/device-power-off stop
exits 0; usage, Bluetooth and authentication failures are non-zero.

Graph a CSV without matplotlib:

```sh
python3 ../../tools/plot_companion_power.py gps-off.csv -o gps-off.svg
# Optional PNG when matplotlib is installed; the SVG is always written.
python3 ../../tools/plot_companion_power.py gps-off.csv \
  -o gps-off.svg --png gps-off.png
```

The SVG has battery percent and millivolts panels, phase-label changes and
reconnect markers. `tests/test_plot_companion_power.py` covers CSV parsing and
SVG generation with a synthetic fixture. The full charge-to-empty procedure
(GPS off, receiver standby 5 s, always-on, USB disconnected, constant screen
and BLE state) is in `docs/companion.md`.

### Password handling

The CLI never prints, echoes or logs the companion password. Sources, in
precedence order:

```text
--password VALUE       visible in the process list; avoid on shared machines
--password-file PATH   first line of the file
--password-env VAR     named environment variable
--password-stdin       standard input
FURBLE_COMPANION_PASSWORD (used when no explicit source is given)
login Keychain item    service com.furble.companion, account furble-password
```

Use `password set` to prompt without echo and store it in the login Keychain,
`password status` to print only `set`/`unset`, and `password delete` to remove
the local item. The CLI's unsandboxed Keychain item is separate from the
signed app's shared access-group item.

When the firmware companion password is unset, the Auth exchange returns the
explicit `not required` result and the CLI authenticates without a local
password. When the firmware requires a password and none is supplied, the CLI
exits non-zero with instructions instead of downgrading the link.

### macOS permissions

The terminal application that launches the CLI needs Bluetooth permission in
**System Settings > Privacy & Security > Bluetooth**. The first CoreBluetooth
use prompts for it. A rebuilt binary started through a different terminal path
can cause a new TCC prompt; the stable installed path is easier to manage.
If permission is denied, `scan` exits with the Bluetooth-unavailable code and a
pointing error. The CLI is read-only while monitoring but still performs the
normal BLE link encryption and pairing required by the firmware Auth
characteristic.

## Host tests

```sh
cd companion/apple
swift test
```

The command needs the Swift toolchain and CryptoKit. It does not need an Apple
account, code signing, an iPhone, or a Bluetooth peripheral. The Swift tests
use Swift Testing and cover the frozen codecs, HMAC auth state machine, Keychain
policy state, device selection, CLI parsing, monitor stop policy, CSV rows and
markers. `tests/test_plot_companion_power.py` separately covers CSV parsing and
SVG generation for the plotter.

On a Command Line Tools-only host the package manifest loads that toolchain's
matching Swift Testing macro plugin explicitly, so `swift test` works without a
full Xcode install. A full Xcode install keeps SwiftPM's normal plugin
discovery. The iOS/macOS SwiftUI app still needs Xcode; only the package and CLI
are available from Command Line Tools.

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

Run the unsigned macOS app locally after `xcodegen`:

```sh
cd companion/apple
scripts/generate-xcode-project.sh
xcodebuild -project FurbleCompanion.xcodeproj -scheme FurbleCompanion-macOS \
  -configuration Debug -derivedDataPath build \
  CODE_SIGNING_ALLOWED=NO CODE_SIGNING_REQUIRED=NO build
open build/Build/Products/Debug/FurbleCompanion-macOS.app
```

This needs a full Xcode installation; Command Line Tools alone cannot compile
the SwiftUI macros or run `xcodebuild`. The app is unsigned in that command.
macOS may still ask for Bluetooth permission the first time, and a rebuilt
unsigned bundle can be treated as a different client by TCC. If the app cannot
read its shared Keychain group unsigned, use the terminal CLI for password and
protocol testing; the CLI deliberately uses an unsandboxed login Keychain item.
No signing identity, certificate, or provisioning profile is added.

The Apple workflow also uploads `furble-companion-macos-debug-unsigned` after
the macOS tests pass. It contains a zip of the unsigned Debug app, a SHA-256
checksum, and source/Xcode provenance. This artifact is for CI companion
testing only. It is not signed, notarized, or suitable for release.

The iOS and macOS entitlements declare the shared Keychain access group
`$(AppIdentifierPrefix)com.furble.companion.shared`. A signed distribution must
use a real Apple team whose provisioning profiles contain that group. The app
reads the expanded group from its platform Info.plist and passes it to the
Keychain store. No signing identity, certificate, or provisioning profile is
checked in.

The iOS target must declare `bluetooth-central` and `location` background
modes. Location background tracking is opt-in and must not be enabled merely
by pairing. When the phone GPS toggle is on, the provider sets
`allowsBackgroundLocationUpdates` and iOS shows the system location indicator.
When In Use authorization is enough for a user-started session. The privacy
manifest and usage strings explain the purpose of every protected API.

## Hardware status (2026-11-04)

The CLI builds and scans a flashed StickS3 successfully (for example
`furble-38a70`, RSSI -48 dBm). Authenticated `status` currently stops at the
first protected capability read with `Encryption is insufficient.` The
characteristic is `READ_ENC`, and a bare command-line executable has no app
bundle/TCC Bluetooth prompt to complete the BLE pairing. Use the native macOS
app target for authenticated hardware testing, or wrap the CLI in a minimal
signed app bundle with the Bluetooth usage description. Set
`FURBLE_COMPANION_DEBUG=1` to print characteristic UUIDs, hex values, and read
errors. This is a transport pairing gate, not a protocol decode failure.

## Physical gates

An iPhone or Mac with Bluetooth, a password-enabled furble firmware build, and
an authenticated BLE bond are required for end-to-end testing. Verify pairing,
auth success and failure, reconnect, status notifications, settings writes,
camera commands, dead-man release, and background restoration on both Apple
targets. Simulator builds do not exercise CoreBluetooth, Keychain entitlements,
pairing, or real location delivery. OTA remains intentionally absent from this
client foundation until the firmware OTA trust and signed-image policy are
complete.

The macOS CLI is host-tested but was not exercised against the StickS3 in this
worktree. The firmware Auth characteristic requires an encrypted/bonded link,
so the Mac must complete the OS pairing prompt before `status` or `monitor` can
authenticate. The terminal application needs Bluetooth permission. The bench
has no camera attached: camera list/state decoding is tested, but camera
command success cannot be certified here. Battery runs also require USB to be
disconnected; the CSV's `battery_ma` may be 0 because the firmware has no PMIC
current backend. The full procedure is in `docs/companion.md`.
