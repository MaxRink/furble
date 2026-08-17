# 51 - Companion app feature parity

Status: firmware settings parity v2 implemented. The app and camera phases
remain design only.

## Implementation state, firmware settings parity v2

- `Settings::appliesImmediately` is the shared source for console and companion
  restart metadata. The settings layer also owns dangerous-write metadata.
- Settings list records keep the v1 reader layout. The trailing flags byte uses
  bit 0 as the inverse of `appliesImmediately`. Bit 1 marks `COMPANION`,
  `TX_POWER`, `SLEEP_CONN` and `CPU_FREQ`.
- The capability characteristic is `b57f4f64-087b-4740-b71d-8262cf26ebbc`.
  Its capability version is 1, its wire version is 2, and it advertises only
  feature bit 0 for settings v2. The Cameras characteristic is not included.
- GPS, GPS baud, GPS rate, GPS sentence filtering and GPS constellation writes
  reload the receiver through the existing GPS path.
- A companion disable written over the companion link waits one second before
  removing the service. The settings response is indicated first.
- No new NVS setting was added. Existing defaults, keys and wire ids are
  unchanged. Hardware verification is still pending.

The companion app from [50-companion-app-design.md](50-companion-app-design.md)
shipped with status, trigger, location push and a first settings editor. The
on-device UI can do more. It can edit every setting with a proper widget, it
can pick multi-connect targets, and it can watch each camera connect, drop and
reconnect. This document designs the protocol and app work to close that gap.

All line anchors below were read on fork master at commit `916e831`.

## Motivation

The phone is the better screen. The device has three buttons and a 1.14 inch
panel. The app already holds an encrypted, authenticated GATT link that can
carry every byte the on-device menu can produce. What is missing is not
transport, it is protocol surface:

- The settings characteristic already carries every exposed setting, but the
  app renders `"Setting $id"` placeholders and can only edit bools and bytes
  (`companion/android/.../protocol/FurbleProtocol.kt:117-121`).
- There is no way to see saved cameras, pick multi-connect targets, or connect
  and disconnect from the phone at all. The status packet carries only two
  aggregate counters, `camera_total` and `camera_connected`
  (`include/FurbleCompanion.h:69-70`).
- There is no capability signal. The app cannot tell a firmware that speaks the
  new protocol from one that does not, except by poking it.

Parity for managing connections and settings makes the phone a full second
interface, which is what section 1.3 of plan 50 promised.

## 1. Settings parity

### 1.1 What already works

The firmware side is nearly complete. `Settings::setting_t` carries a frozen
`wire_id` for every setting (`src/FurbleSettings.cpp:11-39`). Ids 1 to 23 are
assigned. `TOUCH_CALIBRATION` and `BULB` carry `wire_id = 0`, meaning not on
the wire. `Companion::handleSettings` (`src/FurbleCompanion.cpp:814`) serves
list, get and set for the full table, typed as bool, u8, u32, string or blob
(`src/FurbleCompanion.cpp:643-680`), and every write goes through the same
`Settings::save` path the menu uses.

So settings parity is mostly an app problem plus three small wire additions.

### 1.2 Setting metadata lives in the app, not on the wire

The app needs names, enum labels, units and ranges. Two options:

**Send metadata over the wire.** Self-describing, but every list indication
grows by a name and a label table, and the strings already exist in the
firmware (`setting_t.name`) and would now exist in a third place on the phone
anyway for translation.

**Compile a table into the app keyed by `wire_id`.** The wire id column is
frozen and never reused, which is exactly what makes a static table safe. An
unknown id renders as a read-only hex row, so a new firmware setting degrades
to visible-but-uneditable instead of invisible.

The table is chosen. One Kotlin object, one row per wire id: display name,
value type, enum labels where the u8 is an enum (TX_POWER, SCAN_MODE,
BATT_STYLE, CPU_FREQ, GPS_RATE, GPS_CONSTEL), allowed values for GPS_BAUD,
range for BRIGHTNESS and INACTIVITY, and the theme name list for THEME. The
table cites `src/FurbleSettings.cpp` and the console printers in
`src/FurbleConsole.cpp` as its source of truth and the PR that adds a setting
must update it, the same rule the console already follows.

### 1.3 Typed editors

| Wire type | Settings | Editor |
|---|---|---|
| bool | GPS, GPS_NMEA, MULTICONNECT, RECONNECT, RECON_BACKOFF, FAUXNY, AUTOCONNECT, COMPANION, SHOW_TITLE, SLEEP_CONN, WATCHDOG | switch |
| u8 enum | TX_POWER, SCAN_MODE, BATT_STYLE, CPU_FREQ, GPS_RATE, GPS_CONSTEL | dropdown with labels |
| u8 range | BRIGHTNESS, INACTIVITY | slider with unit text |
| u32 | GPS_BAUD, SCAN_TIMEOUT | dropdown (baud) or stepper (seconds) |
| string | THEME | dropdown of known themes |
| blob | INTERVAL | dedicated structured editor |

INTERVAL is the one blob worth an editor. It is `interval_t`, count, delay,
shutter and wait, and the firmware already accepts a full-size blob write
(`src/FurbleCompanion.cpp:772-779`). The app editor mirrors the on-device
spinner semantics: count with the infinite sentinel, times in the same units
the device shows. The blob layout is copied into the app protocol layer with a
size check, the same pattern `FurbleProtocol` uses for the fix and status
structs.

### 1.4 Apply semantics mirrored from the console

The console is the precedent. `appliesImmediately()`
(`src/FurbleConsole.cpp:205-226`) returns true for GPS, GPS_BAUD, GPS_RATE,
GPS_NMEA, GPS_CONSTEL, MULTICONNECT, RECONNECT, RECON_BACKOFF, FAUXNY,
AUTOCONNECT, CPU_FREQ, BATT_STYLE, SCAN_MODE, SCAN_TIMEOUT and SLEEP_CONN, and
false for everything else, which the console prints as `applies: on reboot`.

The companion wire currently disagrees. `Companion::settingNeedsRestart`
returns true only for THEME (`src/FurbleCompanion.cpp:784-786`), so the app
tells the user BRIGHTNESS applies immediately when it does not.

Fix: move the function out of the console into the settings layer as
`Settings::appliesImmediately(type_t)`, and have both the console and
`Companion` call it. One list, two consumers, no drift. The list record flags
byte becomes:

| Bit | Meaning |
|---|---|
| 0 | needs restart to take effect |
| 1 | dangerous over the air, see 1.5 |
| 2-7 | reserved, app must ignore |

Bit 0 is now the inverse of `appliesImmediately`. The current app masks only
bit 0 (`FurbleProtocol.kt:123-124`), so old apps ignore bit 1 safely.

Post-save apply hooks stay minimal. The firmware already pokes GPS, TX_POWER
and COMPANION after a save (`src/FurbleCompanion.cpp:889-901`). Add GPS_RATE,
GPS_NMEA and GPS_CONSTEL to the GPS reload case, since the console applies
them immediately and the companion should match. Settings read at point of use
(SCAN_MODE, SCAN_TIMEOUT, MULTICONNECT, backoff and reconnect flags) need no
hook.

### 1.5 Safety rules for over-the-air writes

Some settings can sever or degrade the very link that carries the write.

| Setting | Risk |
|---|---|
| COMPANION | turning it off disconnects the companion and stops advertising |
| TX_POWER | lowering it can drop the link margin below usable at range |
| SLEEP_CONN | changes connection timing behavior under light sleep |
| CPU_FREQ | changes timing margins for the BLE stack under load |

Rules:

1. These four carry flags bit 1. The app shows a two-step confirm that states
   the consequence in one sentence before writing.
2. Ordering guarantee: the firmware always sends the settings indication
   response before applying a link-affecting change. `handleSettings` already
   does this, the response indication at `src/FurbleCompanion.cpp:884` precedes
   the apply switch. Keep that ordering as a stated invariant, so the phone
   always learns the write succeeded even if the link drops a moment later.
3. COMPANION off over the air additionally delays the actual disable briefly so
   the indication confirmation completes. The exact grace period is an
   implementation decision, on the order of one second, and must be stated in
   the protocol document.
4. No automatic revert in version 1. A network-interface style
   "revert unless confirmed in 30 s" guard was considered and rejected: the
   on-device menu is always available as recovery, and a revert timer is a
   state machine that can itself misfire mid-shoot. Revisit only if a real
   lockout is reported for TX_POWER.

Settings that are not on the wire stay off it. `TOUCH_CALIBRATION` and `BULB`
keep `wire_id = 0`.

## 2. Connection management

### 2.1 Stable camera ids

Saved cameras live in an NVS index of `{char name[16]; Camera::Type type}`
entries (`lib/furble/CameraList.h:68-71`). Index position is not stable: a
delete shifts every later entry. The wire needs the same treatment settings
got, a stable id that survives reorder and delete.

Extend the index entry with a `uint8_t camera_id`, allocated from a monotonic
counter persisted in the same NVS namespace, never reused, 0 reserved as
invalid. Migration follows the `interval_t` precedent
(`src/FurbleSettings.cpp:106-116`): `load_index` accepts the old entry size and
assigns fresh ids on first load after upgrade. The FauxNY test camera gets an
id like any other entry, which is what makes rig scenarios reproducible.

### 2.2 One new characteristic

One new characteristic on the existing companion service, from the frozen UUID
base (`include/FurbleCompanion.h:31-37`), next free derived value
`b57f4f63-087b-4740-b71d-8262cf26ebbc`:

| Name | Properties | Purpose |
|---|---|---|
| Cameras | write, indicate, notify | commands and responses via indicate, unsolicited state events via notify |

Same TLV request-response shape as settings, same encryption and
authentication requirement, one attribute and one CCCD spent. Indications
carry command responses and list records. Notifications carry state change
events. The split matters: a response must be acknowledged, a state event is
sampled data and the next one supersedes it, exactly the settings-versus-status
distinction plan 50 already draws.

Request, written by the phone:

```
uint8  op        // 0 list, 1 connect, 2 disconnect, 3 select, 4 deselect
uint8  camera_id // 0xFF = all, ignored for list and disconnect
```

Response and event record, indicated or notified by furble:

```
uint8  status     // 0 ok, 1 unknown id, 2 rejected, 3 busy
uint8  camera_id  // 0xFF terminates a list
uint8  cam_type   // Camera::Type as frozen wire constants
uint8  flags      // bit0 saved, bit1 selected, bit2 active target, bit3 connected
uint8  progress   // Camera::getConnectProgress(), 0-100
int8   rssi       // valid only while connected, -128 = unknown
uint8  state      // wire copy of the plans/25 row state, see 2.4
uint8  name_len
uint8  name[name_len]
```

Names fit. Camera names are at most 64 bytes and the negotiated MTU is at
least 247 in practice, with 256 preferred in every sdkconfig, so one record per
camera per indication needs no pagination. This resolves the camera-name open
question from plan 50 section 9.

### 2.3 Command semantics

- `list` walks `CameraList` saved entries and indicates one record each,
  terminated by `camera_id = 0xFF`, mirroring the settings list terminator.
- `select` and `deselect` edit the multi-connect target set, the same set the
  on-device flow in [25-multiconnect-ui.md](25-multiconnect-ui.md) edits.
  Rejected with status 2 when MULTICONNECT is off.
- `connect` with an id connects one camera. `connect` with `0xFF` runs the
  multi-connect set through the same path as the on-device connect, honoring
  RECONNECT. Rejected with status 3 while a scan is running or a connect is
  already in flight.
- `disconnect` maps to `Control::disconnect()`
  (`include/FurbleControl.h:110-112`), which is all-targets today. Per-camera
  disconnect needs `Control` to grow target addressing and is deferred; the
  wire format already carries the id so adding it later is not a wire change.
- Companion link loss never disconnects cameras. The deadman rule from plan 50
  section 3.6 applies to held shutter and focus only. A dropped phone must not
  end a timelapse.

Connect and disconnect must route the same way the console routes them, through
the control layer request path, never by calling UI functions. In a GUI build
the on-device screen follows via its existing state polling.

### 2.4 Live per-camera state

The row model is lifted from plans/25, which derives display state from
`Control::getState()` plus `Camera::isConnected()` and
`Camera::getConnectProgress()`. Freeze those derived states as wire constants:

| Wire state | plans/25 row |
|---|---|
| 0 | idle, not a target |
| 1 | connecting, with progress |
| 2 | connected |
| 3 | reconnecting |
| 4 | lost |
| 5 | disconnecting |

Notification policy copies the status characteristic: notify on state change
immediately, rate limit steady-state refresh (rssi, progress) to one batch per
second, and skip entirely when nothing changed. The on-device Cameras page
polls at 1 Hz while visible; the companion sends only deltas, because every
radio byte contends with the camera links.

The 20-byte status packet is unchanged. Aggregate counters stay where they
are, so old apps lose nothing.

## 3. App UI

The app shell gains a fourth tab, Cameras, next to Status, Settings and
Trigger (`companion/android/.../ui/CompanionScreens.kt:56`). It is the phone
rendering of the plans/25 on-device Cameras page plus the controls that page
deliberately does not have:

- One row per saved camera: name, type icon, state label using the exact
  plans/25 wording (`connected`, `reconnecting NN%`, `lost`), rssi while
  connected.
- With MULTICONNECT on, a checkbox per row bound to select and deselect. With
  it off, tapping a row connects that camera, matching the on-device
  single-connect flow.
- One Connect all / Disconnect action pair, disabled in states where the
  firmware would answer busy, so the button state mirrors the reject rule
  instead of discovering it.
- The Settings tab replaces placeholder rows with the metadata table editors
  from section 1, grouped and searchable, with restart-required and dangerous
  badges driven by the flags bits.

The repository layer extends `CompanionUiState`
(`companion/android/.../ble/CompanionState.kt:26-40`) with a camera list
`StateFlow`, populated by a list command on connect and folded from
notifications after that. Rig instrumented tests drive this flow directly, per
the plans/29 phase 5 rule that assertions live in the repository, not in
screen coordinates.

## 4. Protocol versioning

The reference is the rig handshake: `rig_hello_t` carries `rig_version` and
`wire_version` and fails loudly on mismatch (`plans/29-virtual-test-rig.md`,
transport section). GATT has no hello, so the equivalent is a capability read:

New read-only characteristic, derived UUID
`b57f4f64-087b-4740-b71d-8262cf26ebbc`:

```
uint8  version       // 1
uint8  wire_version  // Companion::WIRE_VERSION
uint32 features      // bit0 settings v2 flags, bit1 cameras characteristic
                     // bit2 reserved for OTA, rest reserved
```

Degradation rules, all of which cost the app nothing beyond checks it already
half does:

- Old firmware, new app: the capability characteristic is absent from
  discovery. The app assumes wire version 1, hides the Cameras tab, and treats
  all settings flags beyond bit 0 as unset. Characteristic absence is the
  primary signal; the feature bits exist so a future firmware can ship the
  characteristic without shipping every feature.
- New firmware, old app: the old app never discovers the new UUIDs it does not
  know, never subscribes, and the firmware sends nothing on them. Settings
  flags bit 1 is ignored by the old parser, which masks bit 0 only.
- Per-packet version bytes stay exactly as plan 50 defined them. The
  capability read is additive, not a replacement.
- `WIRE_VERSION` bumps to 2 with this work. The rig hello carries the same
  value on both sides, so a version-skew rig scenario is a one-line config.

## 5. Phased delivery

Each step is one PR, independently mergeable, and each firmware PR precedes
the app PR that consumes it.

1. **Firmware: settings parity v2.** `Settings::appliesImmediately` shared
   with the console, flags bits 0 and 1, GPS reload hooks for the $PCAS
   settings, capability characteristic with feature bit 0. No new UI.
2. **Firmware: camera management.** `camera_id` in the CameraList index with
   migration, the cameras characteristic, wire state mapping, feature bit 1.
3. **App: settings editors.** Metadata table, typed editors, INTERVAL editor,
   restart and danger badges, confirm flow. Works against firmware 1; against
   older firmware it degrades to the current behavior.
4. **App: cameras tab.** List, select, connect, disconnect, live state.
   Hidden entirely when feature bit 1 or the characteristic is absent.
5. **Rig: scenarios and corpus.** Plans/29 phase 1 golden payloads gain the
   cameras records, the capability read and the v2 settings flags. Phase 5
   gains three scenarios: full settings sweep (list, edit one of each type,
   verify persistence across simulated reboot), camera lifecycle with FauxNY
   (list, select two, connect all, drop one, watch reconnect states, disconnect),
   and version skew (rig peer pinned to wire version 1, assert the app hides
   the Cameras tab and downgrades flags handling).

The protocol document from plan 50 rollout step 2 is updated in the same PR as
each firmware change. That document remains the contract; this plan is the
design rationale.

## 6. Open questions

- The COMPANION-off grace period in 1.5 rule 3: exact value, and whether it is
  worth a firmware acknowledgment state instead of a delay.
- Per-camera disconnect: extend `Control` with target addressing now, or wait
  for a user request. The wire reserves the id either way.
- Whether the app should ever initiate a scan for new cameras. Plan 50 left
  camera connect initiation as an open question; this design allows connecting
  saved cameras only, and scanning stays on the device.
- Whether TX_POWER deserves the rejected auto-revert after field experience.

## 7. References

Verified on fork master at `916e831`:

- `src/FurbleSettings.cpp:11-39` frozen wire_id table
- `src/FurbleConsole.cpp:205-226` appliesImmediately
- `src/FurbleCompanion.cpp:643-680` wire types, `:784-786` settingNeedsRestart,
  `:814-901` handleSettings and apply hooks
- `include/FurbleCompanion.h:31-37` frozen UUID base, `:63-78` status struct
- `lib/furble/CameraList.h:68-71` index entry
- `include/FurbleControl.h:100-112` targets, connectAll, disconnect
- `companion/android/.../protocol/FurbleProtocol.kt:117-134` SettingRecord
- `companion/android/.../ble/CompanionState.kt:26-40` CompanionUiState
- [25-multiconnect-ui.md](25-multiconnect-ui.md) Cameras page row model
- [29-virtual-test-rig.md](29-virtual-test-rig.md) hello handshake, phases 1
  and 5
- [50-companion-app-design.md](50-companion-app-design.md) sections 3.5, 3.6,
  7, 8 and 9
