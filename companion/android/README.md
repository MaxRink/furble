# furble companion for Android

This is the first Android companion implementation for furble. It uses one
application module, Kotlin, Gradle Kotlin DSL, and Jetpack Compose.

## Build

Install a JDK 17, the Android SDK platform and build tools for API 37, then
run:

```sh
cd companion/android
./gradlew assembleDebug
```

The app supports Android API 26 and higher and targets Android API 37. The
Gradle wrapper is configured for Gradle 9.4.1 and Android Gradle Plugin 9.2.1.

## Tests

`./gradlew test` runs the JVM unit tests, the golden-corpus contract test and the
Robolectric UI snapshot tests. The Android CI job already invokes it before the
debug APK build:

```sh
cd companion/android
./gradlew test
```

The protocol, camera catalog, auth gate and GATT contract tests are plain JVM
tests. Three test classes start an Android runtime or read the shared firmware
corpus:

- `FurbleGoldenCorpusTest` reads `tests/protocol/golden/` from the checkout and
  re-decodes or re-encodes every location, status, settings and trigger fixture
  the firmware generates, plus `companion_auth.json`. That corpus is produced by
  `make -C tests/protocol generate` from the firmware sources and diffed by the
  Companion protocol tests workflow, so a wire change on the firmware side
  cannot land without failing this module. Run
  `make -C tests/protocol generate` after changing a firmware packet and commit
  the regenerated fixtures with the firmware change.
- `CompanionScreenshotTest` and `CompanionScreenGalleryTest` compose the shipped
  screens with Robolectric's native graphics mode and compare each 720x1440
  bitmap with a committed baseline in `app/src/test/screenshots`.

A normal run prints one line per comparison, for example `The main screen
matches companion-main.png at 720x1440 with 0 differing pixels.`, and both CI
and a local run show it. Absence of those lines means the test task did not run,
not that the screens matched.

### Screenshot gallery

These are the committed baselines, rendered by the tests above at 360x720 dp,
density 2. They are the app's only screenshots; there is no hand-captured set.

| Screen | Baseline | What it shows |
| --- | --- | --- |
| Main, first launch | `app/src/test/screenshots/companion-main.png` | The default repository state: no association, no capability, so the Settings and Cameras tabs stay hidden |
| Permission rationale | `app/src/test/screenshots/companion-rationale.png` | The gate before the shell renders, after a denied request |
| Connected status | `app/src/test/screenshots/companion-status.png` | Association, authenticated password card, phone GPS with fixes sent |
| Cameras | `app/src/test/screenshots/companion-cameras.png` | Two saved cameras with selection, connection state and RSSI |
| Settings | `app/src/test/screenshots/companion-settings.png` | Metadata-driven rows grouped by section, with the restart and immediate flags |
| Trigger | `app/src/test/screenshots/companion-trigger.png` | Shutter, focus and timed shutter controls |

The screenshots render the shipped composables, so they cannot drift from the
source without failing the comparison. They are not evidence of Bluetooth
behavior: see [the companion coverage matrix](../../docs/companion-test-coverage.md)
for the layers that no automated test reaches, including pairing, reconnect,
settings sync against firmware, camera commands and the hold/release path.

### What the pixel comparison detects

The comparison allows a channel difference of 2 and 0.1 percent of differing
pixels, which is 1036 of the 1,036,800 pixels in a capture.

That budget makes each test a layout gate, not a copy gate. Layout-scale changes
fail with an order of magnitude to spare: 1 dp more spacing in the status list
changed 27,667 pixels, 27 times the budget. A text change only fails once it
reaches roughly a dozen characters: an appended period changed 12 pixels and an
appended short word 654, while a one-character substitution can land on either
side of the budget, because the count follows the glyphs that move rather than
the characters edited.

The slack is insurance for a runner the baselines were not produced on, not a
measured requirement: the committed baselines were rendered on macOS arm64 and
matched a linux/amd64 render with 0 differing pixels, and the CI logs print the
same 0 for the runner. A failure writes the rendered screen to
`app/build/outputs/screenshots/<name>-actual.png` and names that path.

Every rendering input is pinned so the same source produces the same pixels:
API 35 and the `en-rUS` locale through `@Config`, density 2 and font scale 1
through the composition, and a 360dp x 720dp capture region. The tests restore
the calling JVM locale afterwards. The captured screens show no clock, no
location fix and no network value. Each capture asserts known nodes first, so an
asynchronous repository change fails on the node that moved instead of on a
pixel count.

Regenerate the baselines only after reviewing an intended UI change, then commit
the new PNGs with that change:

```sh
cd companion/android
./gradlew testDebugUnitTest --tests '*Companion*Test*' -PupdateScreenshots
```

Update mode turns off both the up-to-date check and the build cache, because a
regenerated image is also the test task's input, and it writes through a temp
file so an interrupted run cannot leave a truncated baseline.

Robolectric downloads the Android 15 platform jar, about 197 MB, and its native
runtime on the first run and reuses them from the Maven cache afterwards. The
emulator smoke job is unaffected: it installs the debug APK and keeps its launch
and logcat checks.

### What the emulator job does and does not prove

`android.yml` has a second job, `smoke`, that installs the debug APK in an API 35
emulator, launches it with `monkey`, greps logcat for `FATAL EXCEPTION` and
checks that the process is still alive. It runs weekly and on manual dispatch,
never on a pull request, and it is marked `continue-on-error`, so it cannot fail
a change. It proves the APK starts. It does not prove any screen renders
correctly, and it does not touch Bluetooth, `CompanionDeviceManager`, the
Keystore or location.

There is no `app/src/androidTest` source set, so no instrumented test exists.
Nothing in CI runs the app against a device or against firmware.

### Source contracts outside this module

Two Python modules, `tests/test_companion_location_contract.py` and
`tests/test_companion_identity_enable_contract.py`, read this app's manifest and
the Apple `Info.plist` and assert that the documented permissions and providers
are still there. `main.yml` ignores `companion/android/**` on pull requests and
`pre-review.yml` does not filter on companion paths, so an Android-only pull
request runs neither workflow and those two guards run only on `master` pushes
and on pull requests that touch `tests/` or `docs/`. Run them directly before
touching the manifest:

```sh
python3 -m unittest tests.test_companion_location_contract \
  tests.test_companion_identity_enable_contract
```

## Protocol

The app targets the additive companion capability contract from
`plans/51-app-feature-parity.md`. It uses the UUIDs listed in that document,
negotiates an MTU of at least 45 before location writes, and uses little-endian
wire integers. The Settings tab is shown only when the optional capability
characteristic (`b57f4f64-087b-4740-b71d-8262cf26ebbc`) reports capability
version 1, wire version 2 and feature bit 0. Older firmware keeps Settings
hidden.

The AUTH characteristic is optional during discovery. Legacy companion
firmware without AUTH remains usable for existing unprotected operations;
the password UI and password-gated operations appear only when AUTH is
present. Password protection therefore requires the PR166 firmware service
implementation, while this app remains backward-compatible with older
services.

When the optional Cameras characteristic
(`b57f4f63-087b-4740-b71d-8262cf26ebbc`) is present and capability feature bit 1
is advertised, the app subscribes to its indications and notifications and
shows a Cameras tab after authentication. Camera rows use the stable
`camera_id`, display live connection state, and support selection, per-camera
connect, refresh, and all-camera disconnect. The tab stays hidden when either
the characteristic or capability is absent.

The location encoder keeps the named fields in the document's order. The
declared `companion_fix_t` size is 42 bytes, but those packed fields add up to
41 bytes. To preserve the declared size without moving `age_ms`, the app
writes one zero trailing compatibility byte. Its offsets are version 0, flags
1, satellites 2, accuracy 3, latitude 4, longitude 12, altitude 20, year 28,
month 30, day 31, hour 32, minute 33, second 34, centisecond 35, reserved 36,
`age_ms` 37 through 40, and the compatibility byte at 41.

The same arithmetic issue exists in the declared 20-byte status record. The
named fields occupy 19 bytes, so the decoder consumes one optional trailing
byte after `uptime_s`. The app does not expose that byte as a field.

The settings request is the exact `op`, `id`, `len`, `value` TLV from the design.
List responses use `status`, `id`, `type`, `flags`, `len`, `value`; the parser
also accepts the early app prototype's trailing-flags form. The app compiles
the frozen firmware metadata table in
`app/src/main/java/com/furble/companion/protocol/FurbleSettingMetadata.kt`,
keyed only by wire ID. Unknown IDs render as read-only hexadecimal rows.
Bool, enum, range, uint32 stepper, theme and packed interval editors all write
typed values through the existing settings characteristic. The interval blob
is four packed little-endian `{uint16 value, uint8 unit}` parts.

IDs 43 and 46 are current firmware metadata; ID 65 is the PR273 Legend setting
and should be edited only when paired firmware advertises that row. ID 42 is the
Time zone enum: 0 is UTC, 1 through 38 are curated zones, and 39 is Auto (GPS).
IDs 48, 49 and 50 are the u-blox M10 settings: the GPS Signals mask (0 to 63,
0 follows GPS Constellation), GPS Power Save (0 Off, 1 Cyclic tracking,
2 On/off) and the GPS AssistNow switch.
The app does not invent rows for settings absent from a device list.

Settings list flag bit 0 is interpreted as restart required. Bit 1 marks the
five link-affecting settings and opens a two-step confirmation before a write:
COMPANION, TX_POWER, TX_ADAPTIVE, SLEEP_CONN and CPU_FREQ.

The password gate uses the firmware AUTH characteristic
`b57f4f6f-087b-4740-b71d-8262cf26ebbc`. The firmware stores the write-only
password at settings wire ID 47, but the app never lists or writes that setting.
The app subscribes to indications before
declaring the GATT session ready. It writes `01 00`, receives `01 00` plus a
16-byte nonce, then writes `01 01` plus the first 16 bytes of
HMAC-SHA256(password UTF-8, nonce). A result indication is `01 02` plus one
byte: `01` authenticated, `02` rejected, `03` dropped after
three failures, or `04` not required. With no saved app credential, the app
still sends `01 00` so empty-password firmware can return `04` not required.
If that firmware instead returns a challenge, the app reports that a password
is required and returns to the retryable unauthenticated state.
A successful password is stored as
AES-GCM ciphertext under a non-exportable Android Keystore key. App backup is
disabled, and the password is never logged or sent as a setting value.

## Runtime behavior

Pairing uses `CompanionDeviceManager` filters for the companion service UUID
and the `furble-` device-name prefix. A bonded association is required before
GATT connection. Status and camera notifications plus settings and camera
indications wake the app through `BluetoothGatt` callbacks. Presence changes arrive through
`CompanionDeviceService` and are the reconnect trigger.

Phone GPS is disabled by default. When enabled, it runs only while an
association exists, the device is present and connected, and precise location
permission is granted. `FusedLocationProviderClient` uses balanced-power
accuracy, a configurable interval that defaults to 10 seconds, and a maximum
batch delay of three intervals. Each delivered fix is encoded separately with
its own `age_ms`.

The app has no `WAKE_LOCK` permission, no permanent polling loop, and no
foreground service. The only delayed work is the temporary one-second trigger
keep-alive while a manual shutter or focus hold is active. The protocol does
not define a separate heartbeat opcode or a numeric timeout, so the app repeats
the held press operation as its keep-alive and releases on button-up, Activity
stop, or link loss. Firmware's documented dead-man behavior remains the final
safety release if the process stops before the app can send the explicit
release.
