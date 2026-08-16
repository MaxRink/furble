# 50 - Companion App Design

Status: design only. No upstream code in this document.

This document specifies the furble side of a phone companion app. The phone app
itself lives in a separate repository and is not part of furble. What furble
needs is a BLE GATT service, an opt-in setting, and a controlled advertising
policy. That firmware work is a future PR and is out of scope for the 19 PRs in
the roadmap index.

## 1. Goals

The companion app has four jobs.

### 1.1 Phone as GPS provider

furble supports geotagging through a wired GPS unit on the Grove port
(M5Stack Unit GPS v1.1, AT6668 receiver, UART at 115200 baud). The unit works,
but it costs money, occupies the port, drains the battery, and on the M5StickS3
it disables light sleep for the whole device. `GPS::enable()` calls
`Platform::getInstance().setSleep(false)` under `FURBLE_M5STICKS3` because the
ESP32-S3 UART does not survive light sleep (`src/FurbleGPS.cpp:120-123`). That
single call removes the largest available power saving on the S3.

A phone already has a GNSS receiver, and it is already in the photographer's
pocket. If the phone pushes fixes over BLE, the user gets geotagging without the
hardware unit, without the UART, and without the sleep lock. This is the
strongest argument for the whole feature.

The phone writes a location and time record. furble feeds it into the existing
geotag path, which ends at `Control::updateGPS()`
(`include/FurbleControl.h:89`, `src/FurbleControl.cpp:193`). Cameras see no
difference. `Camera::updateGeoData()` is unchanged.

### 1.2 Status monitor

furble has a 1.14 inch screen. Some state does not fit on it and some state is
easier to read on a phone. The companion app shows battery percentage, battery
voltage in mV, charging state, the list of connected cameras, the GPS fix source
and satellite count, and intervalometer progress.

This also helps during long unattended runs. A timelapse can run for hours. The
user can check remaining shots and battery from the phone without touching the
device and waking its screen.

### 1.3 Remote settings

Settings live in NVS behind `Furble::Settings` (`include/FurbleSettings.h:16-29`,
`src/FurbleSettings.cpp:11-24`). The on-device menu is the primary interface and
stays that way. The companion app is a second interface for the same values. It
is useful for settings with many options, for settings that are awkward to enter
on a small screen, and for saving and restoring a whole configuration.

### 1.4 Remote trigger

The phone can press and release the shutter and focus. This maps directly onto
`Control::sendCommand()` with the existing `cmd_t` values
(`include/FurbleControl.h:13-22`). The phone becomes a second remote for the
remote. This is useful when furble is mounted on the rig and the user is not.

## 2. Scope and non-goals

In scope for the future firmware PR:

- One custom GATT service on the existing NimBLE server.
- One new setting, `COMPANION`, default false.
- Advertising, gated on that setting.
- Bonding and encryption for the companion link.

Not in scope:

- The phone app. Separate repository.
- Wi-Fi, cloud, or accounts. BLE only.
- Camera control protocol changes. The companion service sits above the existing
  `Control` layer and does not touch vendor code.
- OTA. Sketched in section 3.7, deferred, needs a partition table change.

## 3. GATT service design

### 3.1 Existing BLE state

furble already runs a NimBLE GATT server. `Scan::getInstance()` calls
`NimBLEDevice::createServer()` and `Scan::start()` calls `m_Server->start()`
(`lib/furble/Scan.cpp`). The server exists because Sony cameras read the client
device name during connection. `CONFIG_BT_NIMBLE_ROLE_PERIPHERAL=y` and
`CONFIG_BT_NIMBLE_GATT_SERVER=y` in every sdkconfig. Adding a service therefore
adds no new role, no new stack config, and no new memory allocation mode. It
adds attributes to a table that is already built.

The library is `h2zero/esp-nimble-cpp` 2.5.0 (`src/idf_component.yml`), so the
service is defined with `NimBLEService`, `NimBLECharacteristic`, and
`NimBLECharacteristicCallbacks` rather than raw `ble_gatts_add_svcs()`.

### 3.2 UUIDs

The service is private and is not registered with the Bluetooth SIG, so it needs
128 bit UUIDs. Generate one random base UUID with `uuidgen` before
implementation, freeze it in the protocol document, and derive every
characteristic from it by varying the first 32 bit field. The values below are
placeholders and must be replaced by the generated base.

| Name | UUID | Properties |
|---|---|---|
| Companion service | `00000001-6675-7262-6c65-e0d1c2b3a495` | primary service |
| Location and time | `00000002-6675-7262-6c65-e0d1c2b3a495` | write, write-no-response |
| Status | `00000003-6675-7262-6c65-e0d1c2b3a495` | read, notify |
| Settings | `00000004-6675-7262-6c65-e0d1c2b3a495` | write, indicate |
| Trigger | `00000005-6675-7262-6c65-e0d1c2b3a495` | write |
| OTA control | `00000010-6675-7262-6c65-e0d1c2b3a495` | write, indicate |
| OTA data | `00000011-6675-7262-6c65-e0d1c2b3a495` | write-no-response |

Firmware version and device name do not need custom characteristics. Use the
standard Device Information Service (`0x180A`) with Firmware Revision String
(`0x2A26`) and Manufacturer Name String (`0x2A29`). `FURBLE_VERSION` already
exists (`lib/furble/Scan.h:11-13`). Standard UUIDs mean the phone app gets them
for free from platform helpers.

Every payload below starts with a one byte `version` field. The phone must
tolerate a higher version by ignoring trailing fields it does not know, and
furble must tolerate a shorter write by rejecting it with an ATT error rather
than reading past the buffer.

### 3.3 Location and time characteristic

This mirrors `Camera::gps_t` and `Camera::timesync_t`
(`lib/furble/Camera.h:50-68`) so the conversion on the furble side is a field
copy with no unit maths.

```c
typedef struct __attribute__((packed)) {
  uint8_t  version;      // 1
  uint8_t  flags;        // bit0 position valid, bit1 time valid, bit2 altitude valid
  uint8_t  satellites;   // 0 if the phone does not report it
  uint8_t  accuracy_m;   // horizontal accuracy, metres, 255 = unknown
  double   latitude;     // WGS84 degrees
  double   longitude;    // WGS84 degrees
  double   altitude;     // metres above mean sea level
  uint16_t year;
  uint8_t  month;        // 1-12
  uint8_t  day;          // 1-31
  uint8_t  hour;         // 0-23, UTC
  uint8_t  minute;       // 0-59
  uint8_t  second;       // 0-60
  uint8_t  centisecond;  // 0-99
  uint8_t  reserved;
  uint32_t age_ms;       // age of the fix when the phone issued the write
} companion_fix_t;       // 42 bytes
```

`latitude`, `longitude` and `altitude` are `double` to match `gps_t` exactly.
The alternative is a scaled `int32_t` at 1e-7 degrees, which is about 1 cm of
resolution and saves 12 bytes. That is a real saving but it introduces a
conversion that has to be verified against every vendor geotag encoder. The
copy-through form is chosen because it cannot lose precision and cannot
introduce a rounding bug in the camera path. Revisit if the packet ever needs to
fit a 23 byte ATT MTU.

`timesync_t` uses `unsigned int` for every field, which is 32 bit on ESP32. The
wire format uses the smallest type that holds the range. The conversion is a
widening assignment and cannot overflow.

42 bytes does not fit the default 23 byte ATT MTU. All sdkconfigs already set
`CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU=256`, so a negotiated MTU of at least 45 is
expected. The phone must negotiate MTU before writing. If MTU negotiation fails,
the phone falls back to a write with response split across two writes with a
sequence byte. Keep that fallback out of version 1 unless a real phone needs it.

`age_ms` exists because the phone batches. A batched fix can be tens of seconds
old when it arrives. furble subtracts nothing and adds nothing to the position,
it only uses `age_ms` to start the staleness clock at the correct point.

#### Fix source arbitration

furble already has one authority on GPS freshness. `GPS::update()` runs on a 1 s
LVGL timer, checks `location.age() < MAX_AGE_MS` with `MAX_AGE_MS = 30 s`
(`include/FurbleGPS.h:38`), sets `m_HasFix`, drives the status bar icon, and
calls `Control::updateGPS()` (`src/FurbleGPS.cpp:161-192`).

The companion service does not call `Control::updateGPS()` directly. It hands
the record to `Furble::GPS` through a new method, for example
`GPS::setExternalFix(const companion_fix_t &)`. `GPS::update()` then picks a
source:

1. UART unit, if enabled and its fix is younger than 30 s.
2. Companion, if its fix plus elapsed time since the write is younger than 30 s.
3. No fix.

The wired unit wins when both are present. It is local, it has a known accuracy,
and it does not depend on a radio link. The same 30 s window applies to both, so
the freshness guarantee that vendors like Fujifilm depend on does not change.

Keeping the decision inside `GPS` means one place owns the icon, the staleness
rule, and the arbitration. It also lets the companion feed a fix while the UART
receiver is disabled, which is exactly the configuration that unlocks light
sleep on the S3.

The status bar icon gains a third state so the user can tell where the fix came
from. Reuse the existing icons and add a small badge, or change the icon colour.
Decide during implementation, not here.

### 3.4 Status characteristic

Read for the current value, notify for changes.

```c
typedef struct __attribute__((packed)) {
  uint8_t  version;           // 1
  uint8_t  battery_percent;   // 0-100, 255 = unknown
  uint16_t battery_mv;
  int16_t  battery_ma;        // negative = discharging, 0 = not measurable
  uint8_t  power_flags;       // bit0 charging, bit1 external power present
  uint8_t  camera_total;      // targets in Control::getTargets()
  uint8_t  camera_connected;
  uint8_t  control_state;     // Control::state_t
  uint8_t  gps_source;        // 0 none, 1 uart unit, 2 companion
  uint8_t  gps_satellites;
  uint8_t  ivl_state;         // UI::Intervalometer::state_t
  uint16_t ivl_remaining;     // shots left, 0xFFFF = infinite
  uint32_t uptime_s;
} companion_status_t;         // 20 bytes
```

`control_state` and `ivl_state` are the existing enums
(`include/FurbleControl.h:24-37`, `include/FurbleUI.h:105-111`). Do not
renumber them for the wire. Instead, copy them into fixed wire constants in the
protocol document and map explicitly, so a future refactor of the internal enum
cannot silently change the protocol.

Battery values come from the same source as the status bar
(`M5.Power.getBatteryLevel()`, `M5.Power.getBatteryCurrent()`,
`src/FurbleUI.cpp:161-170`). PR02 promotes this into a proper battery module, so
the companion service should consume that module rather than call M5Unified
directly.

Notification policy: send on change, rate limited to one notification per
second, plus a keepalive notification every 30 s so the phone can tell the
difference between "nothing changed" and "link is dead". One notification per
second at 20 bytes is negligible traffic and it is what wakes a backgrounded
phone app, which matters for section 5.

### 3.5 Settings characteristic

Two options were considered.

**Per setting characteristic.** One characteristic per setting, typed, with its
own notify. Clean to consume on the phone, no parsing. The cost is one
attribute set per setting, and the setting list grows with almost every PR in
the roadmap. It also burns CCCDs, and `CONFIG_BT_NIMBLE_MAX_CCCDS=16` in every
sdkconfig. Twelve settings exist today and the roadmap adds around twenty more.
This does not fit.

**Single TLV characteristic.** One write for the request, one indication for the
response. Costs one attribute and one CCCD regardless of how many settings
exist. The phone has to parse, and there is no per setting subscribe.

TLV is chosen. The attribute budget is the binding constraint and the parsing
cost on the phone is small.

Request, written by the phone:

```
uint8  op       // 0 = list, 1 = get, 2 = set
uint8  id       // wire id, ignored for list
uint8  len      // value length, 0 for list and get
uint8  value[len]
```

Response, indicated by furble:

```
uint8  status   // 0 ok, 1 unknown id, 2 bad length, 3 read only, 4 rejected
uint8  id
uint8  type     // 0 bool, 1 u8, 2 u32, 3 string, 4 blob
uint8  len
uint8  value[len]
```

`list` returns one indication per setting, terminated by a record with
`id = 0xFF`.

The `id` must not be the raw `Settings::type_t` enum value. That enum is
appended to by many PRs and could be reordered by any of them. Add an explicit
stable `wire_id` field to `Settings::setting_t` (`include/FurbleSettings.h:55-60`)
and assign ids that are never reused. Settings that must not be exposed, such as
`TOUCH_CALIBRATION`, get `wire_id = 0` meaning "not on the wire".

Some settings cannot be changed safely while a camera is connected, and some
need a restart to take effect (the theme setting is the existing precedent). The
response `status = 4 rejected` covers the first case. The list response gains a
flags byte for "needs restart" so the phone can tell the user.

Writes take effect through the same code path the menu uses. There must be no
second write path into NVS.

### 3.6 Trigger characteristic

```
uint8  version  // 1
uint8  op       // 0 shutter release, 1 shutter press, 2 focus press,
                // 3 focus release, 4 timed shutter
uint16 hold_ms  // op 4 only
```

Mapping to `Control::sendCommand()` (`include/FurbleControl.h:84`):

| op | command |
|---|---|
| 0 | `CMD_SHUTTER_RELEASE` |
| 1 | `CMD_SHUTTER_PRESS` |
| 2 | `CMD_FOCUS_PRESS` |
| 3 | `CMD_FOCUS_RELEASE` |
| 4 | `CMD_SHUTTER_PRESS`, then `CMD_SHUTTER_RELEASE` after `hold_ms` |

Rules:

- Reject any write on an unencrypted or unauthenticated link.
- Reject if `Control::getState()` is not `STATE_ACTIVE`. A remote trigger with
  no camera connected is a user error, not a queue entry.
- Rate limit to ten commands per second. The `Control` queue is 32 entries deep
  and each target queue is 8 (`include/FurbleControl.h:57,135`). A misbehaving
  phone must not be able to fill them.
- Deadman release. If the companion link drops while the shutter is held by a
  companion press, send `CMD_SHUTTER_RELEASE`. The same applies to focus. This
  mirrors what the on-device shutter lock already has to do and prevents a
  dropped phone connection from leaving a camera exposing forever.
- The on-device button always wins. A companion trigger never blocks or delays a
  local press.

`op 4` exists so that a short exposure does not depend on phone round trip
timing. The phone asks for 30 ms and furble measures the 30 ms.

### 3.7 OTA concept sketch (deferred)

Firmware updates today go over USB or through the web installer. BLE OTA would
let a user update from a phone in the field. The mechanism is well understood.

Control characteristic commands:

```
START  uint32 image_size, uint8 sha256[32]
ABORT
APPLY
```

Data characteristic: write-no-response, `uint16 seq` followed by up to
`MTU - 5` bytes of image. furble indicates progress and any error on the control
characteristic. Out of order or missing `seq` aborts the session.

Firmware side uses the standard ESP-IDF app update API: `esp_ota_begin()` on
START, `esp_ota_write()` per chunk, `esp_ota_end()` and
`esp_ota_set_boot_partition()` on APPLY, then `esp_restart()`. With rollback
enabled, the new image calls `esp_ota_mark_app_valid_cancel_rollback()` after it
proves it can boot, initialise BLE, and read NVS. Anything else rolls back on
the next reset.

Why this is deferred:

- **Partition table.** Every board builds with
  `board_build.partitions = partitions_singleapp_large.csv`
  (`platformio.ini:11`) and `CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE=y`. There is
  exactly one app slot. Two OTA slots roughly halve the space available to the
  app. The M5StickS3 has 8 MB of flash and can absorb that. The 4 MB boards are
  much tighter. This needs a measured binary size per board before it can be
  promised.
- **Flash layout change.** Changing the partition table changes the flash layout
  for existing users and affects the web installer. That is a migration
  question, not a feature question.
- **Throughput.** At a 256 byte MTU and a fast connection interval, expect
  roughly 15 to 20 kB/s. A 1.5 MB image is a couple of minutes with the radio at
  full duty and the light sleep lock held. This is fine but it is not free, and
  it wants the connection parameter work from PR10 to be settled first.
- **Trust.** An unsigned image accepted over BLE from a bonded peer is a weaker
  boundary than a USB cable. Secure boot and signed images are the correct
  answer and they are a separate, larger decision.

The GATT service should reserve the two OTA UUIDs now so that adding OTA later
does not change the service layout.

## 4. Advertising and discovery

### 4.1 The constraint

Upstream removed BLE advertising in commit 5564b73, "Remove BLE advertising.
(#281)". Advertising had been kept only so Sony cameras could connect, and the
investigation in that PR showed it was not needed. Sony makes a single request
for the client device name during connection, which requires only the server to
be running. The commit deletes `NimBLEAdvertising *m_Advertising` from
`Scan` and replaces `m_Advertising->start()` with `m_Server->start()`.

Re-adding unconditional advertising would undo that. The companion feature must
not do so.

### 4.2 Policy

Advertising is enabled only when the `COMPANION` setting is true. When the
setting is false, behaviour is byte for byte what it is today.

When the setting is true, there are two advertising modes.

**Pairing window.** Started explicitly by the user from
Settings, Bluetooth, Companion, Pair. Connectable and general discoverable,
100 ms interval, name set from `Device::getStringID()`
(`lib/furble/Device.cpp:38-40`), which is already a readable
`furble-xxxxx` string. The window closes after two minutes or on success. No
filter policy, because there is no bond yet.

**Reconnect window.** Active only when `COMPANION` is true and no companion is
currently connected. Undirected connectable, 1000 ms interval, with the accept
list set to the bonded companion's identity address and filter policy set to
accept connections only from that list. NimBLE exposes this as `ble_gap_wl_set()`
and the `BLE_HCI_ADV_FILT_*` policies, wrapped by esp-nimble-cpp. An unbonded
scanner sees an advertisement it cannot act on.

Advertising stops immediately on companion connect and restarts on disconnect.
Advertising never runs while `Scan` is actively scanning for cameras unless
measurement shows the radio contention is acceptable.

### 4.3 Privacy

`Device::init()` already sets `NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_RPA_PUBLIC_DEFAULT)`
and distributes both the encryption key and the identity key
(`lib/furble/Device.cpp:45-48`). Advertisements therefore use a resolvable
private address, and the bonded phone resolves it with the distributed IRK. No
extra work is needed for address privacy. This is worth stating in the PR body
because "we turned advertising back on" reads worse than it is.

### 4.4 The setting

```
COMPANION, "Companion", "companion", FURBLE_STR
```

Type `bool`, default `false`. NVS key `companion` is 9 characters, within the
15 character limit. Menu location: Settings, Bluetooth, created by PR08.

Turning the setting off stops advertising, disconnects any companion, and offers
to forget the bond.

## 5. Phone architecture without a wake lock

The design constraint is that the companion app must not hold a wake lock and
must not run a permanent foreground service. A camera remote that eats 10% of a
phone battery per hour will be uninstalled.

### 5.1 Android

**Association.** Use `CompanionDeviceManager.associate()` with a
`BluetoothLeDeviceFilter` matching the companion service UUID. The user picks
the device from a system dialog. Association is what makes the rest of this
work: it allows the scan without `ACCESS_FINE_LOCATION`, and after pairing the
app holds `REQUEST_COMPANION_RUN_IN_BACKGROUND`,
`REQUEST_COMPANION_USE_DATA_IN_BACKGROUND` and
`REQUEST_COMPANION_START_FOREGROUND_SERVICES_FROM_BACKGROUND`. The app can be
started from the background and can start a foreground service from the
background when it actually needs one.

**Wakes.** `BluetoothGatt` callbacks are delivered to the app process. The 1 Hz
status notification from section 3.4 is enough to keep the process scheduled
while connected. Device presence, appearing and disappearing, is delivered
through `CompanionDeviceService`, which the system binds when the associated
device comes into range. That is the reconnect trigger. No polling loop and no
foreground service are required for it.

**Periodic work.** Anything that has to happen on a schedule regardless of the
link, for example a daily settings backup, goes to `WorkManager` as a
`PeriodicWorkRequest`. WorkManager persists across reboots and respects Doze.
The minimum period is 15 minutes, which is fine for the tasks that need it.

**Location.** `FusedLocationProviderClient` with
`PRIORITY_BALANCED_POWER_ACCURACY`, the largest acceptable
`setIntervalMillis()`, and a `setMaxUpdateDelayMillis()` several times larger so
the OS batches deliveries. Batched deliveries arrive as a group, and each is
written to the location characteristic with its own `age_ms`.

Be honest about the limits. In the background, Android 8.0 and later computes
location only a few times per hour. That is not enough for geotagging a burst of
frames. So there are two modes:

- **Default.** Fixes are pushed while the app is in the foreground or while the
  screen is on, at 1 to 5 s. In the background the app pushes whatever the OS
  gives it, and furble marks stale fixes as no fix after 30 s. No wake lock, no
  foreground service, negligible battery cost.
- **Opt in tracking.** For a shoot where the phone must stay in a pocket, the
  user enables a tracking mode which starts a foreground service with a visible
  notification and a stop button. This requires `ACCESS_BACKGROUND_LOCATION`.
  This is the only way to get continuous background fixes on modern Android, and
  pretending otherwise would produce a feature that silently fails.

Making that trade explicit in the UI is a design requirement, not an
implementation detail.

### 5.2 iOS

**Background mode.** Declare `bluetooth-central` in `UIBackgroundModes`. The
system then wakes the app to deliver `CBCentralManagerDelegate` and
`CBPeripheralDelegate` callbacks while it is in the background. Background scans
behave differently from foreground scans: `CBCentralManagerScanOptionAllowDuplicatesKey`
is ignored and scan intervals stretch, so discovery is slower. That is
acceptable, because after the first pairing the app connects to a known
peripheral rather than scanning.

**State restoration.** Create the central with
`CBCentralManagerOptionRestoreIdentifierKey`. iOS then preserves the services
being scanned for, the peripherals being connected to or already connected, and
the subscribed characteristics. If the app is terminated, the system relaunches
it on a Bluetooth event and calls `centralManager(_:willRestoreState:)` first.
The app rebuilds its state from `CBCentralManagerRestoredStatePeripheralsKey`
and carries on. This is what replaces a background service on iOS.

**Wakes are short.** Work done in a restoration or notification wake should
finish in about ten seconds. So the wake handler does one thing: read the
current location if a recent one is cached, write it, and return. Anything
longer belongs in the foreground.

**Location.** Continuous background location on iOS requires the `location`
background mode and `allowsBackgroundLocationUpdates`, which shows the system
indicator. The same two mode split as Android applies. Default is fixes while
foregrounded plus opportunistic fixes during Bluetooth wakes. Opt in tracking
turns on the background location mode with a clear explanation of the cost.

iOS is second in the rollout because state restoration is harder to get right
than the Android path and there is no CompanionDeviceManager equivalent that
grants background start.

## 6. Power budget on the furble side

The whole point of this feature is to save power, so the feature itself must not
cost more than it saves.

**What it saves.** A user who drops the wired GPS unit removes the Grove 5V
output, removes the UART, and on the M5StickS3 removes the `setSleep(false)`
call that currently disables light sleep for the entire device. The index plan
puts the S3 connected idle target at about 3.3 mA with the PR07 work, against
roughly 240 mA today with GPS enabled. Nothing else in the roadmap frees a
larger amount.

**What advertising costs.** An advertising event is three packets on three
channels. Average current scales with the event rate, so the 1000 ms reconnect
interval costs a tenth of what a 100 ms interval costs. The 100 ms interval is
used only during the two minute pairing window, where responsiveness matters and
the user is looking at the screen. Absolute figures must be measured with the
PR02 battery instrumentation on the actual board rather than asserted here. The
budget to hold the design to is: reconnect advertising must not measurably move
the 30 minute battery drain run.

**What the extra connection costs.** The companion link is one more peripheral
connection. With a 1 s connection interval and a slave latency that gives an
effective 4 s of skipped events, the radio work per second is comparable to one
idle camera link. It does not create a new current floor. It adds radio events
on top of the floor that PR07 establishes. `CONFIG_BT_NIMBLE_MAX_CONNECTIONS=9`
on every board, so a companion link plus up to eight cameras still fits the
stack limit. Multi-connect in practice uses far fewer than eight.

**Interaction with light sleep.** The companion link must not hold a
`NO_LIGHT_SLEEP` power management lock. ESP-IDF automatic light sleep is entered
when no lock is held, and the BLE controller wakes the system for its own
scheduled events. A GATT write from the phone arrives on a connection event and
brings the CPU up through the normal path. The only place a lock is justified is
the OTA transfer, which should acquire the named lock from the PR06 power module
for the duration of the session and release it on completion or abort.

The 1 Hz status notification is the one thing to watch. Sending a notification
every second forces a wake every second. If measurement shows that this costs
meaningfully more than the connection interval already does, drop the default to
one notification every 5 s while the intervalometer is idle and 1 Hz only while
it is running. Make it adaptive rather than fixed.

## 7. Security

The companion link can change settings and fire the shutter. It gets the
strongest protection the stack offers.

- **Bonding required.** No characteristic other than the Device Information
  Service is readable before bonding. NVS bond persistence is already enabled
  (`CONFIG_BT_NIMBLE_NVS_PERSIST=y`).
- **Encryption required.** Location, status, settings and trigger
  characteristics are all marked as requiring encryption. Reads and writes on an
  unencrypted link return the appropriate ATT error rather than being silently
  ignored.
- **MITM protection.** Settings write and trigger write additionally require an
  authenticated link. LE Secure Connections is enabled
  (`CONFIG_BT_NIMBLE_SM_SC=y`). furble has a display and a button, so numeric
  comparison is the right association model: both devices show a six digit
  number, the user confirms it on both. This reuses the existing IO capability,
  `SecurityMode::SECURE_DISPLAY_YESNO`, which is the class default in
  `Camera` (`lib/furble/Camera.h:42-45,188`). Choosing numeric comparison rather
  than passkey entry means the host wide IO capability does not have to be
  switched between the central and peripheral roles, which avoids a race against
  a camera connect that happens to be in progress.
- **The confirmation screen is a full screen LVGL modal.** The user must not be
  able to confirm a pairing by accident while navigating a menu.
- **One companion bond.** `CONFIG_BT_NIMBLE_MAX_BONDS=15` is shared with camera
  bonds. Cap companion bonds at one so pairing a phone can never evict a camera
  bond. Pairing a second phone replaces the first, with a confirmation prompt.
- **Off by default.** `COMPANION` defaults to false. A user who never opens the
  menu is not advertising, is not bondable, and has no new attack surface.
- **Forget.** Settings, Bluetooth, Companion, Forget removes the bond and stops
  advertising. Turning the feature off offers the same.
- **Input validation.** Every characteristic write is length checked against its
  struct before any field is read. Every enum on the wire is range checked
  before it is cast. Rate limits as described in section 3.6.

Threat model in one sentence: an attacker within radio range who has not
completed a numeric comparison on the device screen can learn that a furble
exists and nothing else.

## 8. Rollout

The order below keeps each step independently useful and independently
reviewable.

1. **Firmware GATT PR.** The `COMPANION` setting, the service, the location
   write, the status notify, the trigger write, advertising policy, bonding and
   the pairing UI. Not the settings characteristic and not OTA. Defaults off, so
   the risk to existing users is that a setting exists. Verification includes a
   defaults unchanged regression: with `COMPANION` false, radio behaviour is
   identical to the previous build.
2. **Protocol document.** A versioned document in the furble repository under
   `docs/`, with the frozen base UUID, every struct, every enum value, and the
   wire id table. The phone app repository references it. This is the contract.
   It must land with or before the firmware PR.
3. **Android app MVP.** CompanionDeviceManager association, bonded GATT
   connection, status display, remote trigger, foreground location push. No
   settings editing, no OTA. This validates the protocol against a real stack
   before more of it is committed to.
4. **Settings characteristic.** Requires the `wire_id` column in
   `Settings::setting_t`, which is a small independent refactor and can land
   before the characteristic that needs it.
5. **iOS app.** Core Bluetooth with state restoration, feature parity with the
   Android MVP.
6. **OTA.** Only after the partition table question has an answer with measured
   binary sizes for all five build environments, and only with a plan for
   existing installations.

## 9. Open questions

- Which board does the pairing UI target first. The S3 is the primary device but
  the Core2 touch screen gives the best confirmation dialog.
- Should the companion be allowed to initiate a camera connect, or only control
  cameras that are already connected. Starting with the latter is safer.
- Whether the status notification should carry the camera names. Names are up to
  64 characters (`lib/furble/Camera.h:13`) and would need a separate
  characteristic or a paginated read.
- Whether to expose the raw NMEA debug stream from PR14 over the companion link
  for field diagnosis.

## 10. References

Verified before inclusion.

Furble source, for the interfaces this design attaches to:

- `include/FurbleControl.h:13-22` command enum, `:84` `sendCommand`, `:89` `updateGPS`
- `lib/furble/Camera.h:50-68` `gps_t` and `timesync_t`
- `src/FurbleGPS.cpp:120-123` S3 sleep lock, `:161-192` fix validation and dispatch
- `include/FurbleGPS.h:38` `MAX_AGE_MS`
- `lib/furble/Device.cpp:45-48` RPA and key distribution
- `include/FurbleSettings.h:16-29` and `src/FurbleSettings.cpp:11-24` settings table
- upstream commit [5564b73 Remove BLE advertising. (#281)](https://github.com/gkoh/furble/commit/5564b7354bb06f32d8d60b614ad2ae41eb841d03)

Bluetooth and ESP-IDF:

- [NimBLE GATT server API (Apache Mynewt)](https://mynewt.apache.org/latest/network/ble_hs/ble_gatts.html)
- [NimBLE GAP API, advertising and white list (Apache Mynewt)](https://mynewt.apache.org/latest/network/ble_hs/ble_gap.html)
- [esp-nimble-cpp documentation](https://h2zero.github.io/esp-nimble-cpp/)
- [ESP-IDF NimBLE host integration](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/bluetooth/nimble/index.html)
- [ESP-IDF BLE connection parameters](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/ble/get-started/ble-connection.html)
- [ESP-IDF power management, DFS, light sleep, locks](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/power_management.html)
- [ESP-IDF NimBLE power save example](https://github.com/espressif/esp-idf/tree/master/examples/bluetooth/nimble/power_save)
- [ESP-IDF over the air updates API](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/ota.html)

Phone platforms:

- [Android companion device pairing and background permissions](https://developer.android.com/develop/connectivity/bluetooth/companion-device-pairing)
- [Android CompanionDeviceService](https://developer.android.com/reference/android/companion/CompanionDeviceService)
- [Android WorkManager](https://developer.android.com/topic/libraries/architecture/workmanager)
- [Android optimising location for battery](https://developer.android.com/develop/sensors-and-location/location/battery)
- [Apple Core Bluetooth background processing and state restoration](https://developer.apple.com/library/archive/documentation/NetworkingInternetWeb/Conceptual/CoreBluetooth_concepts/CoreBluetoothBackgroundProcessingForIOSApps/PerformingTasksWhileYourAppIsInTheBackground.html)

Hardware:

- [M5Stack Unit GPS v1.1, AT6668, UART 115200](https://docs.m5stack.com/en/unit/Unit-GPS%20v1.1)
- [M5Stack StickS3](https://docs.m5stack.com/en/core/StickS3)

## Implementation state, firmware GATT service

Implemented in PR #21 on the fork. Notes and deviations after the rebase onto
the integrated master:

- The service source lives in `src/FurbleCompanion.cpp`, not `lib/furble/`.
  The service drives UI, Control, GPS and Settings, which are all app layer,
  and `lib/furble` cannot see the app headers or the M5 board libraries.
- The wire id table now covers every setting the integrated master has. The
  ids assigned by this PR are frozen and must never be reused:

  | wire id | setting | type |
  |---|---|---|
  | 1 | BRIGHTNESS | u8 |
  | 2 | INACTIVITY | u8 |
  | 3 | THEME | string |
  | 4 | TX_POWER | u8 |
  | 5 | GPS | bool |
  | 6 | GPS_BAUD | u32 |
  | 7 | INTERVAL | blob |
  | 8 | MULTICONNECT | bool |
  | 9 | RECONNECT | bool |
  | 10 | FAUXNY | bool |
  | 11 | AUTOCONNECT | bool |
  | 12 | COMPANION | bool |
  | 13 | GPS_RATE | u8 |
  | 14 | GPS_NMEA | bool |
  | 15 | GPS_CONSTEL | u8 |
  | 16 | RECON_BACKOFF | bool |
  | 17 | CPU_FREQ | u8 |
  | 18 | BATT_STYLE | u8 |
  | 19 | SHOW_TITLE | bool |
  | 20 | SLEEP_CONN | bool |
  | 21 | SCAN_MODE | u8 |
  | 22 | SCAN_TIMEOUT | u32 |
  | 23 | WATCHDOG | bool, StickS3 builds only |
  | 0 | TOUCH_CALIBRATION, BULB | not on the wire |

- Settings written over the wire are saved to NVS. Only GPS, TX_POWER and
  COMPANION are hot applied, everything else takes effect where the firmware
  reads it, which for some settings means the next boot.
- The GPS status icon keeps the changed check from the integrated master and
  adds a third state, `icon_location_searching`, for a companion sourced fix.
- The standalone protocol document under `docs/` from section 8 does not exist
  yet. This table and the structs in this plan remain the contract until it is
  written.
- Verified: m5stick-s3, m5stick-s3-debug and m5stick-c build. End to end BLE
  testing against the Android app has not happened yet.
