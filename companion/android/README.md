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

The app targets furble companion protocol version 1 from
`plans/50-companion-app-design.md`. It uses the UUIDs listed in that document,
negotiates an MTU of at least 45 before location writes, and uses little-endian
wire integers.

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
Responses use `status`, `id`, `type`, `len`, `value`. The design says list
records also gain a flags byte but does not place it in the response table, so
the decoder treats one byte after the value as optional list flags. The design
does not provide the firmware wire-id/name table in this repository; until
that table is frozen, the UI renders names as `Setting <wire id>` and never
invent ids for writes.

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
