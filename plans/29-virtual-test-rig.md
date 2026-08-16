# 29 - virtual test rig: SDL simulator plus Android emulator

## Goal

Run the furble companion feature end to end with no hardware in the room. The
firmware side is the SDL host simulator from
[28-emulator.md](28-emulator.md). The phone side is the Android companion app
from `companion/android`, running in an Android emulator. The two talk over a
local TCP socket that carries the same application payloads the GATT service
carries over the air.

The rig is a developer tool. It tests the companion protocol and both user
interfaces. It tests nothing about Bluetooth and nothing about security.

Line anchors below were read at `6306be4` on `feat/50-gatt-service`, at
`85eeedd` on `feat/50-companion-android`, and at `2b79ce8` on `master`.

## Motivation

The companion feature has two halves that have never met. The firmware GATT
service lives on `feat/50-gatt-service`. The Android app lives on
`feat/50-companion-android`. Neither branch contains the other. Every payload
struct, every enum, every UUID is duplicated by hand across a C++ header and a
Kotlin object, and nothing checks that the two copies agree.

They already do not agree. `FurbleProtocol.SERVICE_UUID` in the app is
`00000001-6675-7262-6c65-e0d1c2b3a495`, which is the placeholder from
[50-companion-app-design.md](50-companion-app-design.md) section 3.2. The
firmware froze a real base with `uuidgen`, and
`Companion::SERVICE_UUID` is `b57f4f5e-087b-4740-b71d-8262cf26ebbc`. Over the
air the app would scan, filter on a service UUID that no furble advertises, and
find nothing. The design document told the implementer to replace the
placeholder. One side did. The other did not. That bug has been sitting in two
branches for as long as both have existed, because there is no place where the
two are ever built together.

There are three more of the same class, all documented as open questions in
comments the app author left in `FurbleProtocol.kt`. The location struct sums to
41 named bytes but is declared 42. The status struct sums to 19 named bytes but
is declared 20. The settings response table in the design has no flags field but
section 3.5 says list records gain one. The firmware picked an answer for each.
The app guessed the same answer for each, by reasoning rather than by testing.
Three guesses landed. One did not.

Testing this today needs an M5StickS3, a physical Android phone with developer
mode, a Bluetooth pairing session with a numeric comparison on a 1.14 inch
screen, and a flash cycle per firmware change. That is a five minute loop for a
one byte question.

Neither the SDL simulator nor the Android emulator has Bluetooth. The Android
emulator gained a virtual Bluetooth controller through RootCanal, but that
models an HCI controller for a real Android Bluetooth stack, and there is
nothing to connect it to, because the simulated furble side has no controller at
all and the plan 28 simulator explicitly does not compile `lib/furble`. So the
GATT link cannot be virtualised. The payloads can.

## Design

### Overview

```
  host machine
  +--------------------------------------------------+
  |  sim (platform = native, SDL2 window)             |
  |    src/FurbleUI.cpp        real                   |
  |    src/FurbleGPS.cpp       real arbitration       |
  |    CompanionService.cpp    real, shipping source  |
  |    CompanionRigTransport   listens 127.0.0.1:6737 |
  +---------------------------|----------------------+
                              |  framed TCP
  +---------------------------|----------------------+
  |  Android emulator                                 |
  |    dials 10.0.2.2:6737                            |
  |    SocketConnection        rig flavor only        |
  |    CompanionRepository     real, shared           |
  |    FurbleProtocol.kt       real, shared           |
  |    CompanionScreens.kt     real, shared           |
  +--------------------------------------------------+
```

The rule that makes the rig worth building: everything above the transport is
the shipping source file, not a copy. On the firmware side that means the
service logic, the wire structs, the arbitration, the rate limits and the
deadman release. On the app side that means the protocol codec, the repository
state machine and every Compose screen. Only the bottom layer is swapped.

### The firmware seam

`Furble::Companion` today mixes three concerns in one class. Advertising and
bonding are NimBLE. The GATT callbacks are NimBLE. The service logic underneath
them is not NimBLE at all. `Companion::handleLocation` at
`lib/furble/Companion.cpp:613` takes a `NimBLEAttValue`, copies it into a
`companion_fix_t`, and hands the result to `GPS::setExternalFix`. Nothing in
that function needs Bluetooth. The same is true of `getStatus` at `:412`,
`handleSettings` at `:790`, `handleTrigger` at `:894`, `allowTrigger` at `:881`
and `releaseHeldCommands` at `:955`.

Split the class in two.

`CompanionService` holds the logic. It takes `const uint8_t *` and a length
instead of a `NimBLEAttValue`. It talks to the outside world through one
abstract interface:

```c
class CompanionTransport {
 public:
  virtual bool isConnected(void) const = 0;
  virtual bool isEncrypted(void) const = 0;
  virtual bool isAuthenticated(void) const = 0;
  virtual uint16_t getMaxPayload(void) const = 0;
  virtual void notify(uint8_t charId, const uint8_t *data, size_t len) = 0;
  virtual void indicate(uint8_t charId, const uint8_t *data, size_t len) = 0;
  virtual void error(uint8_t charId, uint8_t attError) = 0;
};
```

`CompanionGatt` holds the NimBLE half: `createGatt` at `:204`, the advertising
policy at `:253` and `:298`, the bond store at `:324`, the server and
characteristic callbacks at `:490` to `:611`, and an implementation of
`CompanionTransport` over `NimBLECharacteristic::notify` and `::indicate`.

`CompanionRigTransport` is the second implementation. It is compile-time gated
behind `FURBLE_RIG` and it exists only in the simulator and in debug firmware
builds. It never appears in a release binary.

This split is a pure extraction. It is worth doing on its own merit, because it
makes the service logic unit testable without a radio, but the rig is what pays
for it.

### Framing

The socket carries one frame per ATT operation. Six byte header, little endian,
which matches both the ESP32 and every host the simulator or the emulator runs
on, so the packed wire structs copy straight through.

```c
typedef struct __attribute__((packed)) {
  uint8_t  magic0;    // 'F'
  uint8_t  magic1;    // 'R'
  uint8_t  op;        // rig_op_t
  uint8_t  char_id;   // rig_char_t
  uint16_t length;    // payload bytes, 0 to 1024
} rig_header_t;       // 6 bytes, followed by length payload bytes
```

The magic exists for diagnosis, not integrity. TCP already gives ordering and
delivery. A header that does not start with `FR` is a framing bug, and the
receiver logs the offset and closes the socket rather than trying to resync.

`char_id` is the varying field of the design document UUIDs, which makes the
mapping readable in a hex dump:

| `char_id` | characteristic | properties mirrored |
|---|---|---|
| `0x00` | none, link level ops | |
| `0x02` | location and time | write, write-no-response |
| `0x03` | status | read, notify |
| `0x04` | settings | write, indicate |
| `0x05` | trigger | write |
| `0x10` | OTA control | reserved, never accepted |
| `0x11` | OTA data | reserved, never accepted |

`op` mirrors the ATT operations the two sides actually use:

| `op` | name | direction | payload |
|---|---|---|---|
| `0x01` | `WRITE` | central to peripheral | characteristic value |
| `0x02` | `WRITE_NO_RSP` | central to peripheral | characteristic value |
| `0x03` | `WRITE_RESPONSE` | peripheral to central | empty |
| `0x04` | `READ_REQUEST` | central to peripheral | empty |
| `0x05` | `READ_RESPONSE` | peripheral to central | characteristic value |
| `0x06` | `NOTIFY` | peripheral to central | characteristic value |
| `0x07` | `INDICATE` | peripheral to central | characteristic value |
| `0x08` | `INDICATE_CONFIRM` | central to peripheral | empty |
| `0x09` | `SUBSCRIBE` | central to peripheral | `uint16 cccd` |
| `0x0A` | `ERROR` | peripheral to central | `uint8 att_error` |
| `0x0B` | `HELLO` | central to peripheral | see below |
| `0x0C` | `HELLO_ACK` | peripheral to central | see below |
| `0x0D` | `PAIR_REQUEST` | peripheral to central | `uint32 pin` |
| `0x0E` | `PAIR_CONFIRM` | central to peripheral | `uint8 accept` |

The payload of `WRITE`, `NOTIFY`, `INDICATE` and `READ_RESPONSE` is the
characteristic value exactly as the GATT path would carry it. No wrapping, no
re-encoding, no JSON. `FurbleProtocol.encodeLocation()` produces 42 bytes for
the rig and 42 bytes for BLE, and the same bytes reach the same
`std::memcpy` into `companion_fix_t`.

`HELLO` and `HELLO_ACK` stand in for service discovery and MTU exchange:

```c
typedef struct __attribute__((packed)) {
  uint8_t  rig_version;      // 1
  uint8_t  wire_version;     // Companion::WIRE_VERSION or PROTOCOL_VERSION
  uint8_t  role;             // 0 peripheral, 1 central
  uint8_t  reserved;
  uint8_t  service_uuid[16]; // 128 bit base, NimBLE byte order
  uint16_t max_payload;      // negotiated ATT MTU minus 3
} rig_hello_t;               // 22 bytes
```

The service UUID travels in the handshake for one reason: so the rig catches
the mismatch described in the motivation rather than hiding it. A mismatch is
fatal by default. `--ignore-uuid-mismatch` on both sides downgrades it to a
warning, for the case where someone is deliberately testing an old app build.

`max_payload` is the smaller of the two sides' values, default 244, which is the
247 byte MTU an Android phone typically negotiates minus the three byte ATT
header. The rig transport enforces it. A write longer than `max_payload` gets an
`ERROR` frame with `att_error = 0x0D`, invalid attribute value length, which is
what a real stack would produce. This does not model MTU negotiation. It models
the limit that negotiation produces, which is enough to catch a location packet
that grows past what a 23 byte default MTU would allow.

`SUBSCRIBE` exists so `Companion::onSubscribe` at `:603` stays on the tested
path. Status notifications do not flow until the central subscribes, the same
as a CCCD write.

Connection lifecycle maps one to one. TCP accept is BLE connect. Exactly one
connection is accepted at a time, and a second connect attempt is closed
immediately, which mirrors the one companion rule from
[50-companion-app-design.md](50-companion-app-design.md) section 7. TCP close or
reset is BLE disconnect, and it drives `releaseHeldCommands` the same way.

### What pairing becomes

Nothing on the wire is secured. Say so twice, in the plan and in the app.

Over the air the companion link has LE Secure Connections, numeric comparison
against a six digit number shown on the furble screen, encryption required on
all four characteristics, an authenticated link additionally required for
settings and trigger writes, a bond in NVS, a whitelist filtered reconnect
advertisement, and a resolvable private address. The rig has a TCP socket on
loopback.

The rig models the pairing sequence and none of its properties.
`CompanionRigTransport` generates a pin, calls the same
`CompanionService::beginPairing(pin)` the SMP callback at
`Companion::onConfirmPassKey` (`:547`) calls, sends `PAIR_REQUEST`, and waits.
The simulator shows the same full screen LVGL modal. The app shows the same six
digits and the same confirm button. `PAIR_CONFIRM` reaches
`CompanionService::confirmPairing(accept)`. The two minute pairing window and
its timeout behave the same.

What that exercises: the modal, the state machine, the window timeout, the app's
`ConnectionState.BONDING` transition, and the fact that a user has to press two
buttons. What it does not exercise, at all:

- No SMP. No ECDH key agreement. No LE Secure Connections. The pin is a random
  number with no cryptographic relationship to anything.
- No encryption. Every frame is plaintext on loopback.
- No MITM protection. `isAuthenticated()` returns a constant. The rejection
  paths for settings and trigger writes on an unauthenticated link are never
  entered. Those need a unit test with a stub transport that returns false, plus
  hardware.
- No bond storage, no `loadBond` at `:324`, no whitelist, no
  `BLE_HCI_ADV_FILT_*` policy, no resolvable private address, no identity
  resolution. All of advertising and discovery, section 4 of the design
  document, is untested by the rig.
- No ATT permission enforcement by a stack. Over the air NimBLE checks the
  characteristic permission flags before the callback runs. In the rig the shim
  has to re-implement that check from a table that mirrors the flags passed to
  `createGatt` at `:204`. A table is a model. A model can be wrong in the same
  direction as the code it models.

Three hard rules follow.

1. The rig binds `127.0.0.1` only. Never `0.0.0.0`. The emulator reaches host
   loopback through `10.0.2.2` without the socket being on the LAN.
2. The rig is compile-time gated behind `FURBLE_RIG` with no runtime setting. CI
   greps the five release environments' build flags and fails if the symbol
   appears.
3. The rig flavor of the Android app carries a permanent banner reading
   "RIG BUILD, NO BLE, NO ENCRYPTION" on every screen, and installs under a
   separate application id. A screenshot from the rig must never be mistakable
   for a screenshot from a real link.

Any statement of the form "the rig proves the companion link is secure" is
false. The rig proves the companion link parses.

### The app seam

`CompanionRepository` constructs `GattConnection` directly at
`connectBondedDevice()` and owns the `CompanionDeviceManager` association,
bonding and presence logic. `GattConnection` already has a clean surface:
`connect`, `close`, `writeLocation`, `requestSettingsList`, `setSetting`,
`sendTrigger`, plus a six method `Listener`. That surface becomes the interface.

Android's build variant rules decide the shape. Gradle merges
`src/<variant>/`, `src/<buildType>/`, `src/<flavor>/` and `src/main/` in that
priority order, and throws a duplicate class error if the same fully qualified
Kotlin class appears in a flavor source set and in `src/main`. So the
implementation class cannot live in `main` at all.

- `src/main` declares `interface CompanionLink` and
  `interface CompanionLinkProvider`, and `CompanionRepository` asks the provider
  for a link.
- `src/gatt` provides `DefaultCompanionLinkProvider` returning `GattConnection`,
  and `DefaultAssociationController` wrapping `CompanionDeviceManager`.
- `src/rig` provides `DefaultCompanionLinkProvider` returning `SocketConnection`,
  and `RigAssociationController` that synthesises association, presence and bond
  state from the socket.

`CompanionUiState`, `ConnectionState`, `AssociationState`, `FurbleProtocol`,
`DeadManTriggerController`, `FusedLocationProvider`, `MainViewModel` and
`CompanionScreens.kt` are untouched and shared. `flavorDimensions("transport")`
with `gatt` as the default. `rig` gets `applicationIdSuffix = ".rig"` so both
installs coexist, and `rigRelease` is removed from the variant set so the rig
can never be released.

The endpoint is a `buildConfigField`, defaulting to `10.0.2.2:6737`, overridable
by a Gradle property for a physical phone. Port 6737 avoids `5554` to `5585`,
which the emulator console and adb own.

### A bonus mode

Because the transport is a socket, `adb reverse tcp:6737 tcp:6737` points a real
Android phone on a USB cable at the simulator. That gives the real app, on real
Android, on real hardware, against simulated firmware. It is the better half of
the rig for anything that depends on Android behaviour, because the emulator's
`FusedLocationProviderClient` and Doze behaviour are both approximations. There
is no mirror image. Real firmware against an emulated phone is not possible,
because the firmware's radio has no socket.

## Enabled test scenarios

### 1. Phone GPS feeding the simulator's arbitration

The strongest argument for the companion feature is section 1.1 of the design
document, and it is entirely untested today.

Drive: `adb emu geo fix <lon> <lat> <alt>` sets the emulator's location.
`FusedLocationProvider` batches it, `FurbleProtocol.encodeLocation()` encodes
it, the socket carries it, `CompanionService::handleLocation` copies it into a
`GPS::external_fix_t`, and `GPS::setExternalFix` hands it to the arbitration in
`src/FurbleGPS.cpp`.

Assert:

- With the simulator's NMEA file source running, the UART wins and
  `GPS::getSource()` returns `SOURCE_UART`.
- With the UART source disabled, the companion wins and the source is
  `SOURCE_COMPANION`. This is the configuration that unlocks light sleep on the
  S3, so it is the one that matters.
- Advance the simulator's virtual clock past `MAX_AGE_MS` with no new write and
  the source falls to `SOURCE_NONE`. Deterministic, because the virtual clock is
  under the driver's control.
- `age_ms` from a batched delivery starts the staleness clock at the right
  point, not at arrival.
- The status bar GPS icon reaches its third state and the GPS Data page renders
  the companion fix.

This scenario requires a change to plan 28. See Risks.

### 2. Status rendering

Drive: the simulator's fake battery, control state and intervalometer state, set
from the driver script. `notifyStatus` at `:467` runs its change plus rate limit
plus keepalive policy. The app decodes into `StatusSnapshot` and
`CompanionScreens.kt` renders it.

Assert:

- Every field survives the round trip, including the signed `battery_ma` and the
  `0xFFFF` infinite intervalometer sentinel.
- Hold every field constant for 60 s of virtual time and exactly two keepalive
  notifications arrive, not sixty.
- Change one field twice within one second and the rate limit collapses it to
  one notification.
- `control_state` and `ivl_state` render as the right labels for all six and all
  intervalometer values, which is the mapping the design document warned would
  drift.
- The app's tolerance of a 19 byte status is exercised by a driver flag that
  truncates the trailing byte.

### 3. Settings round trip

Drive: the app's settings screen, and the simulator's own Settings menu.

Assert:

- `list` returns one indication per exposed setting and terminates with
  `id = 0xFF`.
- A setting with `wire_id = 0` is absent from the list.
- Set a bool and a `uint8`, then navigate the simulator's own Settings menu and
  see the new value. That is the check that there is one NVS write path, which
  section 3.5 of the design document requires and which nothing else verifies.
- Unknown id returns status 1, a bad length returns status 2, a setting rejected
  while a camera is connected returns status 4.
- The needs-restart flag reaches the app and produces the warning.
- The optional flags byte on list records is present, which resolves the third
  documented ambiguity.

### 4. Trigger to the FauxNY camera

Drive: the app's trigger controls. The simulator's fake `Control` and the FauxNY
test camera stand in for a real camera.

Assert:

- Press and release shutter and focus reach `Control::sendCommand` with the
  right `cmd_t`, and FauxNY records the shot.
- `op 4` timed shutter holds for `hold_ms` measured on the virtual clock, and
  the measurement is furble's, not the phone's.
- Eleven commands inside one second: the eleventh is rejected by `allowTrigger`
  at `:881`.
- A trigger with `Control::getState()` not `STATE_ACTIVE` is rejected.
- Deadman: kill the socket while the shutter is held and
  `releaseHeldCommands` at `:955` fires `CMD_SHUTTER_RELEASE`. This is the
  failure that leaves a camera exposing forever, and it is currently verified by
  reading the code.

### 5. Screenshot capture on both sides

Every scenario step can emit a pair. The simulator side reuses the capture from
plan 28 phase B. The app side uses `adb exec-out screencap -p`, which is the
documented command line route since the emulator has no screenshot flag of its
own.

The driver writes them as `NN-step-sim.png` and `NN-step-app.png` and composes a
side by side sheet per scenario. That sheet is the artifact a companion PR body
needs, and today there is no way to produce it without two devices and a camera
phone pointed at a desk.

## Phases

### Phase 1: golden payload corpus and a cheap conformance job

Independent of everything else in this document. It needs neither the simulator
nor the emulator. Land it first, because it catches the UUID bug and the three
struct ambiguities today.

Scope. A directory of golden characteristic payloads with metadata, and two test
suites that read the same directory.

Files:

- `tools/rig/corpus/*.bin`, one file per payload. Location fixes at the
  boundaries, status snapshots including the sentinels, every settings request
  and response shape, every trigger op.
- `tools/rig/corpus/corpus.json`. For each file: the characteristic, the
  direction, the expected decoded field values, and the expected result for
  malformed cases.
- `tools/rig/gen_corpus.py`. Regenerates the corpus from the field tables so a
  protocol change is one edit, not sixty hex files.
- `companion/android/app/src/test/java/com/furble/companion/protocol/CorpusTest.kt`.
  Plain JVM test. Encodes and decodes every corpus entry through
  `FurbleProtocol`.
- `test/companion/test_corpus.cpp`. Host C++ test over the same corpus through
  the extracted `CompanionService` decoders. Needs phase 2 for the decoders, so
  either sequence phase 2 first or start with a `static_assert` only version
  that checks the struct sizes and offsets.
- A UUID equality assertion in both suites against a single generated header and
  a single generated Kotlin file, both produced by `tools/rig/gen_uuids.py` from
  one text file. Two hand maintained copies is how the bug happened.
- `.github/workflows/main.yml`, new `companion-conformance` job. Gradle
  `testGattDebugUnitTest` plus the host C++ test. No emulator, no SDL, runs in
  well under two minutes.

Effort: one to two days. This phase is the highest value per hour in the
document and it is the only part that belongs in per-push CI.

### Phase 2: the firmware transport seam

Scope. Pure extraction. No behaviour change.

Files:

- `include/FurbleCompanionService.h`, new. The wire structs, the enums, the
  `CompanionTransport` interface, and the `CompanionService` class.
- `include/FurbleCompanion.h`, shrinks to the NimBLE half and keeps the frozen
  UUIDs.
- `lib/furble/CompanionService.cpp`, new. Moved from `lib/furble/Companion.cpp`:
  `getStatus` `:412`, `notifyStatus` `:467`, `handleLocation` `:613`, the
  settings block `:648` to `:880`, `allowTrigger` `:881`, `handleTrigger`
  `:894`, `releaseHeldCommands` `:955`, `timedShutter` `:966`. Signatures change
  from `NimBLEAttValue` to `const uint8_t *, size_t`.
- `lib/furble/Companion.cpp`, keeps `enable` `:78`, `disable` `:142`,
  `createGatt` `:204`, the advertising block `:241` to `:322`, the bond store
  `:324` to `:366`, the server and characteristic callbacks `:490` to `:611`,
  and gains a `CompanionTransport` implementation.

Verification: hardware. Flash the S3, pair the real Android app over the air,
and confirm the whole feature behaves exactly as it did before the split.

Effort: one to two days. The risk is entirely in the extraction, so keep the
diff mechanical and reviewable.

### Phase 3: rig transport in the simulator

Depends on plan 28 phase A and on phase 2. Depends on the plan 28 GPS revision
in Risks.

Files:

- `sim/rig_frame.h`, new. The header struct, the op and characteristic enums,
  the hello struct. Shared with `tools/rig/frame.py` by generation, not by
  copying.
- `sim/CompanionRigTransport.cpp`, new. Listener on `127.0.0.1:6737`, one
  accepted connection, frame parser, the permission table, the `max_payload`
  limit, the pairing sequence, and the optional `--drop-notify` and `--delay-ms`
  impairments.
- `sim/main.cpp`, gains `--rig`, `--rig-port` and `--ignore-uuid-mismatch`.
- `sim/FurbleControlSim.cpp`, gains `getTargets`, `getState` and `sendCommand`
  backed by FauxNY, which the service needs and which the UI-only fake from plan
  28 does not provide.
- `platformio.ini`, the `sim` environment gains `-DFURBLE_RIG`.
- `lib/furble/CompanionService.cpp` joins the simulator's compile set. It is the
  one file from `lib/furble` the simulator builds, and building the shipping
  source rather than a copy is the point of phase 2.

Effort: two to three days.

### Phase 4: the app rig flavor

Depends on nothing but the app. Can be built in parallel with phase 3 against a
throwaway Python peripheral that speaks the framing.

Files:

- `companion/android/app/src/main/java/com/furble/companion/ble/CompanionLink.kt`,
  new. The interface extracted from `GattConnection`.
- `companion/android/app/src/main/java/com/furble/companion/ble/CompanionLinkProvider.kt`
  and `AssociationController.kt`, new interfaces.
- `companion/android/app/src/main/java/com/furble/companion/ble/CompanionRepository.kt`,
  edited to go through the provider. `CompanionUiState` and `ConnectionState`
  unchanged.
- `companion/android/app/src/gatt/java/...`, `DefaultCompanionLinkProvider.kt`
  and `DefaultAssociationController.kt`. `GattConnection.kt` and
  `CompanionPresenceService.kt` move here from `main`, because a class in a
  flavor source set must be absent from `main`.
- `companion/android/app/src/rig/java/...`, `DefaultCompanionLinkProvider.kt`,
  `RigAssociationController.kt`, `SocketConnection.kt`, `RigFrame.kt`.
- `companion/android/app/src/rig/res/values/strings.xml`, the banner.
- `companion/android/app/build.gradle.kts`, `flavorDimensions`, the two flavors,
  the `rigRelease` removal, the `buildConfigField` endpoint.
- `companion/android/app/src/main/AndroidManifest.xml`, the Bluetooth and
  location permissions move to `src/gatt/AndroidManifest.xml`. The rig flavor
  asks for `INTERNET` and nothing else, which is a useful proof that the rig
  path never touches the Bluetooth APIs.

Effort: two to three days. Most of it is the source set reshuffle, which is
fiddly and mechanical.

### Phase 5: the scripted end to end scenario

Files:

- `tools/rig/run_scenario.py`. Starts the simulator, waits for the listen
  socket, boots or attaches to the AVD, installs the rig APK, runs the
  instrumented test, drives the virtual clock, captures both screenshots per
  step, and composes the sheet.
- `tools/rig/scenarios/*.yaml`, the five scenarios above.
- `companion/android/app/src/androidTestRig/java/.../RigScenarioTest.kt`. A
  UiAutomator or plain instrumented test that drives `CompanionRepository`
  directly and asserts on its `StateFlow`.

The protocol assertions live in the instrumented test, not in `adb shell input
tap` coordinates. Coordinate tapping is the single largest source of flake in
this kind of rig, and driving the repository directly still exercises every
layer below the Compose screens. Screenshots are captured separately and are
artifacts, never assertions.

Effort: three to four days. This is the largest phase and the one most likely to
grow.

### Phase 6: the CI job

`.github/workflows/rig.yml`, new. `workflow_dispatch` plus a nightly
`schedule`. Never on push. Never required.

```
runs-on: ubuntu-latest
  enable KVM via the udev rule
  apt install libsdl2-dev xvfb
  pio run -e sim
  reactivecircus/android-emulator-runner with an AVD snapshot cache
  tools/rig/run_scenario.py --all
  upload sheets and logs as artifacts
```

Effort: one day, plus an indeterminate amount of flake chasing.

## CI feasibility: the honest verdict

**One job can do it on `ubuntu-latest`. It should not be a required check, and
it probably should not run on push.**

What works. GitHub enabled hardware accelerated virtualization on standard Linux
hosted runners in April 2024, so `/dev/kvm` is available once the workflow adds
the udev rule that puts the runner user in the `kvm` group.
`reactivecircus/android-emulator-runner` is the maintained way to drive it and
defaults to `-no-window -gpu swiftshader_indirect -no-snapshot -noaudio
-no-boot-anim`. The SDL side is a normal Linux binary. Use `xvfb-run` rather
than `SDL_VIDEODRIVER=dummy`, because the dummy driver has no GL and
`Panel_sdl` needs a renderer it can read pixels back from. Plan 28 phase C
assumes the dummy driver works. That assumption needs testing, and Xvfb is the
safe answer either way.

What it costs. Cold, expect twelve to twenty minutes: system image download
unless cached, AVD cold boot at two to five minutes, a cold Gradle build at two
to four, the SDL build, and the scenarios. Warm with an AVD snapshot cache and a
Gradle cache, expect eight to twelve. The current furble CI builds five firmware
images and runs clang-format. This job would be the longest thing in the
repository by a wide margin.

What breaks. Three independent flaky surfaces stacked in one job: AVD boot,
Gradle dependency resolution, and the simulator window. AVD boot flake on hosted
runners is well documented even with KVM available. Nested virtualization is
x86_64 only, so the job cannot move to the cheaper ARM runners. Any one of the
three failing turns a green pipeline red for a reason unrelated to the change
under review, and the natural response to that is to stop reading the pipeline.

What it is worth. Almost all of the bug class that motivated this document is
caught by phase 1, which needs no emulator, runs in under two minutes, and
cannot flake. The UUID mismatch, the struct sizes, the enum drift, the settings
response shape: all of it is a codec problem, and codecs are testable without a
radio or a virtual phone. The emulator rig catches a different and smaller
class: state machine and rendering bugs that only appear when both halves run at
once. Those are real, but they are not the ones that have been sitting in the
tree for weeks.

So split it. Phase 1 in per-push CI, blocking, cheap, boring. Phase 6 nightly
and on demand, artifacts only, allowed to fail, treated as a canary rather than
a gate. If the nightly is red for a week and nobody noticed a real bug in it,
delete it.

One more consideration. None of this is upstream facing. `gkoh/furble` has no
Android app and no simulator. Proposing an Android emulator job to a maintainer
who has neither is not a conversation worth opening until plan 28 and the GATT
PR have both landed. Keep the whole rig on the fork until then.

## Risks

- **Plan 28 replaces `src/FurbleGPS.cpp` wholesale.** Phase A of that plan lists
  `sim/FurbleGPSSim.cpp` as a replacement. That would delete the source
  arbitration, which is exactly what scenario 1 exists to test. Revise plan 28:
  the simulator provides fake `driver/uart.h` functions that feed NMEA from a
  file, and the real `src/FurbleGPS.cpp` compiles unchanged. This is a smaller
  shim than the replacement file and it is strictly better for the rig. Raise it
  against plan 28 before phase 3 starts.
- **The extraction in phase 2 breaks the shipping GATT path.** It touches the
  only code that talks to a bonded phone. Mitigate by keeping the diff purely
  mechanical, by leaving every function body byte identical where possible, and
  by hardware verifying the full over the air flow before phase 3 begins.
- **The rig hides transport differences and someone believes it.** TCP is
  reliable and ordered. BLE notifications are unacknowledged and can be dropped,
  indications are flow controlled one at a time, and the settings list is a
  burst of indications that a real link can stall. A green rig does not prove
  the over the air list works. Mitigate with the `--drop-notify` and `--delay-ms`
  impairments, and with a note in every scenario report that the transport is
  lossless.
- **The rig proves nothing about security and the phrasing invites confusion.**
  Mitigate with the banner, the separate application id, the dedicated section
  above, and a line in every generated report.
- **A rig socket ships in a release binary.** Mitigate with the compile-time
  gate, the loopback bind, and a CI grep over the five release environments.
- **A second protocol surface drifts from GATT.** Mitigate by sharing the
  service core and the codec, by generating the UUIDs and the frame definitions
  from one source, and by pinning the payloads in the phase 1 corpus.
- **The pairing model is cosmetic and the modal it drives looks convincing.**
  Mitigate by making the simulator's pairing modal render a visible RIG
  watermark under `FURBLE_RIG`.
- **Emulator behaviour is not phone behaviour.** `FusedLocationProviderClient`
  in an emulator with `adb emu geo fix` batches differently from a real GNSS
  receiver, and Doze is not modelled faithfully. Anything the rig says about
  battery, background delivery or wake behaviour is worthless. Use the
  `adb reverse` mode with a real phone for those questions.
- **Scope growth in phase 5.** A scenario driver is the kind of tool that
  absorbs weeks. Cap it: five scenarios, a YAML file each, no scenario language
  features beyond set, wait, assert and capture.
- **Two more builds of the same sources.** After this document the app has two
  flavors and the firmware has three transports. Mitigate by running the rig
  flavor's unit tests in the phase 1 job, so a break in the flavor split is
  caught cheaply.

## Verification

- `pio run -e sim` builds with `-DFURBLE_RIG` on macOS and on Ubuntu.
- The simulator with `--rig` listens on `127.0.0.1:6737` and on nothing else,
  confirmed with `lsof -i` or `ss -ltn`.
- `./gradlew assembleRigDebug` builds. `assembleRigRelease` does not exist.
- The rig APK's merged manifest contains no `BLUETOOTH_*` permission.
- Both app flavors pass the phase 1 corpus test, and the corpus test fails if
  either UUID file is edited by hand.
- A release firmware build's compile flags contain no `FURBLE_RIG`, checked in
  CI.
- The five scenarios pass with the emulator on a developer machine.
- The five scenarios pass under `xvfb-run` with `-no-window`, headless.
- Two runs of the same scenario on the same machine produce byte identical
  simulator screenshots, which the virtual clock from plan 28 phase B makes
  possible.
- The banner is present in every app side screenshot the driver captures. Assert
  it, rather than trusting it.
- Cross check against hardware once phase 2 lands: pair the `gatt` flavor with a
  real S3 over the air, run the same five scenarios by hand, and compare the
  captured status, settings and trigger behaviour against the rig run. Any
  difference is a rig bug and is worth fixing before anyone trusts a rig result.
- Deliberately reintroduce the UUID mismatch and confirm the handshake fails
  with a clear message, and that phase 1 fails first and faster.

## Relationship to other plans

- [28-emulator.md](28-emulator.md) phase A is a hard prerequisite for phase 3,
  and this document requests the GPS revision described in Risks.
- [28-emulator.md](28-emulator.md) phase B provides the virtual clock and the
  screenshot capture that scenarios 1, 2 and 5 need.
- [50-companion-app-design.md](50-companion-app-design.md) is the protocol
  contract. Section 3 defines every payload the rig carries. Section 7 defines
  everything the rig cannot test.
- The firmware GATT PR on `feat/50-gatt-service` must land before phase 2, since
  phase 2 refactors it.
- The Android app on `feat/50-companion-android` must land before phase 4.
- Section 8 item 2 of the design document calls for a protocol document under
  `docs/`. Phase 1's generated UUID and struct definitions should be that
  document's source, so the contract and the test corpus cannot disagree.
- [27-usb-console.md](27-usb-console.md) is unrelated but complementary. The
  console drives the firmware from the host over USB. The rig drives it from a
  phone over a socket. Both are automation surfaces and neither replaces the
  other.

## References

External links checked on 16 August 2026.

- [Android Emulator networking](https://developer.android.com/studio/run/emulator-networking).
  Confirms the `10.0.2.0/24` special address space: `10.0.2.1` gateway,
  `10.0.2.2` the host loopback interface, `10.0.2.3` DHCP, `10.0.2.15` the
  emulated device. Confirms `adb reverse tcp:<device> tcp:<host>` as the
  alternative route, which is what the physical phone mode uses.
- [Android Emulator command line](https://developer.android.com/studio/run/emulator-commandline).
  Confirms `-no-window`, `-no-audio`, `-gpu <mode>`, `-no-snapshot` and
  `-port`. Confirms there is no command line screenshot flag and that
  `adb screencap` is the route.
- [Android build variants and product flavors](https://developer.android.com/build/build-variants).
  Confirms `flavorDimensions`, the source set merge priority
  variant then build type then flavor then main, and the rule that Gradle throws
  a duplicate class error if the same class is defined in both a flavor source
  set and `src/main`. That rule is why `GattConnection.kt` has to move out of
  `main` in phase 4.
- [reactivecircus/android-emulator-runner](https://github.com/ReactiveCircus/android-emulator-runner).
  Confirms Linux and macOS runners, the KVM udev rule
  (`KERNEL=="kvm", GROUP="kvm", MODE="0666"`), the recommendation to prefer
  Ubuntu runners over macOS, and the default emulator options
  `-no-window -gpu swiftshader_indirect -no-snapshot -noaudio -no-boot-anim`.
- [GitHub Actions hardware accelerated Android virtualization](https://github.blog/changelog/2024-04-02-github-actions-hardware-accelerated-android-virtualization-now-available/).
  April 2024. KVM on standard hosted Linux runners, subject to the udev rule.
  x86_64 only, so ARM runners are excluded.
- [google/rootcanal](https://github.com/google/rootcanal). The Android
  emulator's virtual Bluetooth controller. Integrated in Cuttlefish and
  Goldfish, HCI port 7300. Its own scope statement says accurate emulation of
  the scheduler and base band is out of scope. Considered and rejected: it
  emulates a controller for the Android side, and there is no corresponding
  controller on the simulated furble side, so there is nothing to connect it to.
  Recorded here so the option is not re-proposed.
- SDL headless. `SDL_VIDEODRIVER=dummy` exists and `null` is an accepted alias,
  but the dummy driver provides no GL and is documented as unusable on macOS for
  that reason. SDL also has an `offscreen` driver for headless GL, which was
  disabled by default when introduced. Prefer `xvfb-run` on Linux CI, and treat
  plan 28 phase C's dummy driver assumption as unverified.

Read from source rather than documentation:

- `feat/50-gatt-service` at `6306be4`. `include/FurbleCompanion.h` freezes the
  UUID base at `b57f4f5e-087b-4740-b71d-8262cf26ebbc` and static asserts
  `sizeof(companion_fix_t) == 42` and `sizeof(companion_status_t) == 20`.
  `lib/furble/Companion.cpp` function offsets are cited inline above.
  `include/FurbleGPS.h` adds `source_t`, `external_fix_t`, `setExternalFix` and
  `clearExternalFix`.
- `feat/50-companion-android` at `85eeedd`.
  `protocol/FurbleProtocol.kt` still carries the design document placeholder
  `00000001-6675-7262-6c65-e0d1c2b3a495` as `SERVICE_UUID`, and documents the
  41 versus 42, 19 versus 20 and settings flags ambiguities in comments.
  `ble/GattConnection.kt` defines the six method `Listener` and the operation
  queue that becomes `CompanionLink`. `ble/CompanionRepository.kt` constructs
  `GattConnection` directly, which is the line phase 4 changes.
  `ble/CompanionState.kt` defines the `ConnectionState` enum that both flavors
  keep.
- `feat/28-emulator` currently points at `master` at `2b79ce8`. There is no
  simulator implementation on it and no `sim/` directory. Every dependency this
  document has on plan 28 is a dependency on unwritten code.
