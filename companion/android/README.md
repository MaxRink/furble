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

`./gradlew test` runs the JVM unit tests and the Robolectric UI snapshot test.
The Android CI job already invokes it before the debug APK build:

```sh
cd companion/android
./gradlew test
```

The protocol, camera catalog, auth gate and GATT contract tests are plain JVM
tests. `CompanionScreenshotTest` is the only test that starts an Android
runtime: it composes the main screen from `CompanionScreens.kt` with
Robolectric's native graphics mode, renders it at 720x1440 pixels and compares
the bitmap with the committed baseline
`app/src/test/screenshots/companion-main.png`. The comparison allows a channel
difference of 2 and 0.1 percent of differing pixels, which is 1036 of the
1,036,800 pixels in the capture.

That budget makes the test a layout gate, not a copy gate. Layout-scale changes
fail with an order of magnitude to spare: 1 dp more spacing in the status list
changed 27,667 pixels, 27 times the budget. A text change only fails once it
reaches roughly a dozen characters: an appended period changed 12 pixels and an
appended short word 654, while a one-character substitution can land on either
side of the budget, because the count follows the glyphs that move rather than
the characters edited.

The slack is insurance for an unproven runner, not a measured requirement. The
baseline was rendered on macOS arm64 and matched a linux/amd64 render with 0
differing pixels, so nothing observed needs it. It exists so the first CI run
cannot fail on glyph noise before that run shows what the runner renders. A
failure writes the rendered screen to
`app/build/outputs/screenshots/companion-main-actual.png` and names that path.

Every rendering input is pinned so the same source produces the same pixels:
API 35 and the `en-rUS` locale through `@Config`, density 2 and font scale 1
through the composition, and a 360dp x 720dp capture region. The test restores
the calling JVM locale afterwards. The screen shows no clock, no location fix
and no network value, so no wall-clock or device state reaches the bitmap. Two
known nodes, the top bar and the session state line, are asserted before the
capture, so an asynchronous repository change fails on the node that moved
instead of on a pixel count.

Regenerate the baseline only after reviewing an intended UI change, then commit
the new PNG with that change:

```sh
cd companion/android
./gradlew testDebugUnitTest --tests '*CompanionScreenshotTest*' -PupdateScreenshots
```

Update mode turns off both the up-to-date check and the build cache, because the
regenerated image is also the test task's input, and it writes through a temp
file so an interrupted run cannot leave a truncated baseline.

Robolectric downloads the Android 15 platform jar, about 197 MB, and its native
runtime on the first run and reuses them from the Maven cache afterwards. The
emulator smoke job is unaffected: it installs the debug APK and keeps its launch
and logcat checks.

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
