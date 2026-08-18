# furble companion for Android

This is the first Android companion implementation for furble. It uses one
application module, Kotlin, Gradle Kotlin DSL, and Jetpack Compose.

## Build

Install a JDK 17, the Android SDK platform and build tools for API 36, then
run:

```sh
cd companion/android
./gradlew assembleDebug
```

The app supports Android API 26 and higher and targets Android API 36. The
Gradle wrapper is configured for Gradle 8.11.1.

## Protocol

The app targets the additive companion capability contract from
`plans/51-app-feature-parity.md`. It uses the UUIDs listed in that document,
negotiates an MTU of at least 45 before location writes, and uses little-endian
wire integers. The Settings tab is shown only when the optional capability
characteristic (`b57f4f64-087b-4740-b71d-8262cf26ebbc`) reports capability
version 1, wire version 2 and feature bit 0. Older firmware keeps Settings
hidden.

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

Settings list flag bit 0 is interpreted as restart required. Bit 1 marks the
four link-affecting settings and opens a two-step confirmation before a write:
COMPANION, TX_POWER, SLEEP_CONN and CPU_FREQ.

## Runtime behavior

Pairing uses `CompanionDeviceManager` filters for the companion service UUID
and the `furble-` device-name prefix. A bonded association is required before
GATT connection. Status notifications and settings indications wake the app
through `BluetoothGatt` callbacks. Presence changes arrive through
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
