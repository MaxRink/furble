# PR10: adaptive connection parameters

## Goal

Use a slow connection profile while idle and snap back to a fast profile before
sending a shutter command, so the radio wakes less often between shots. Highest
BLE risk in the roadmap, so it is experimental and defaults to off.

## Scope

- Two connection parameter profiles per camera, idle and fast.
- Renegotiation on the live connection with `NimBLEClient::updateConnParams()`.
- Handle peer initiated parameter changes.
- New setting to enable the feature. Optionally gate to Fujifilm first.
- BLE debug page showing live parameters and RSSI.
- All boards. The change is host side, not chip specific.
- Out of scope: TX power (PR11), scan parameters (PR08).

## Files to change

| File | Anchor | Change |
|---|---|---|
| `lib/furble/Camera.h` | 180-185 | make the four parameter members non const, add profile constants |
| `lib/furble/Camera.h` | 173-178 | add `onConnParamsUpdateRequest()` override |
| `lib/furble/Camera.h` | 116-138 | add `setConnProfile()` and a live parameter getter |
| `lib/furble/Camera.cpp` | 14-19 | on connect, apply the idle profile if the setting is on |
| `lib/furble/Camera.cpp` | 44 | keep `setConnectionParams()` as the pre connect values |
| `src/FurbleControl.cpp` | 41-80 | snap to fast before a shutter or focus command, back to idle after |
| `src/FurbleControl.cpp` | 159-181 | alternative snap point if the per target task is too late |
| `include/FurbleSettings.h` | 16-29 | add `CONN_SAVER` |
| `include/FurbleSettings.h` | 145-148 | add `storage_type<CONN_SAVER>` = `bool` |
| `src/FurbleSettings.cpp` | 11-24 | add table row |
| `src/FurbleSettings.cpp` | 209-215 | add to the `save<bool>(false)` default group |
| `src/FurbleUI.cpp` | 2062-2082 | add the toggle under the Bluetooth submenu |

Verified current state:

- `lib/furble/Camera.h:180-185`:

  ```
  const uint16_t m_MinInterval = BLE_GAP_INITIAL_CONN_ITVL_MIN;
  const uint16_t m_MaxInterval = BLE_GAP_INITIAL_CONN_ITVL_MAX;
  // allow a packet to skip
  const uint16_t m_Latency = 1;
  // double the disconnect timeout
  const uint16_t m_Timeout = (2 * BLE_GAP_INITIAL_SUPERVISION_TIMEOUT);
  ```

  In NimBLE, `BLE_GAP_INITIAL_CONN_ITVL_MIN` is 30 ms, `BLE_GAP_INITIAL_CONN_ITVL_MAX`
  is 50 ms and `BLE_GAP_INITIAL_SUPERVISION_TIMEOUT` is `0x0100`, which is 256
  units of 10 ms, so 2560 ms. furble therefore asks for a 30 to 50 ms interval,
  peripheral latency 1 and a 5120 ms supervision timeout.
- `lib/furble/Camera.cpp:44` calls
  `m_Client->setConnectionParams(m_MinInterval, m_MaxInterval, m_Latency, m_Timeout)`
  once, before `_connect()`. There is no renegotiation anywhere in the tree:
  `updateConnParams`, `getRssi` and `BLE_GAP_EVENT_CONN_UPDATE` do not appear in
  `src/`, `include/` or `lib/`.
- `lib/furble/Camera.h:175-178` declares `onConnect` and `onDisconnect` as
  `override final`. `onConnParamsUpdateRequest` is not overridden, so the NimBLE
  default applies today.
- `src/FurbleControl.cpp:41-80` is the per camera task loop that dispatches
  `CMD_SHUTTER_PRESS`, `CMD_SHUTTER_RELEASE`, `CMD_FOCUS_PRESS`,
  `CMD_FOCUS_RELEASE` and `CMD_GPS_UPDATE`.
- `lib/furble/Fujifilm.cpp:28-31` sets `m_GeoRequested` from a notification on
  `GEOTAG_UPDATE`. That notification must still arrive promptly.
- furble is always the BLE central. It creates clients
  (`lib/furble/Camera.cpp:32`) and never advertises. Upstream removed advertising
  in commit 5564b73.

## New settings

| Enum | NVS key | Namespace | Type | Default |
|---|---|---|---|---|
| `CONN_SAVER` | `conn_saver` | `FURBLE_STR` | `bool` | `false` |

Key is 10 characters, under the 15 character NVS limit. Default `false` leaves
`lib/furble/Camera.cpp:44` as the only place connection parameters are set, which
reproduces today's behavior exactly.

## Menu placement

`Settings > Bluetooth > Connection power save`, created by PR08. Use
`UI::addSettingItem()`.

Add a `Settings > Diagnostics > BLE` page showing, per connected camera: current
interval, latency, supervision timeout, RSSI and the active profile. The
Diagnostics submenu is created by PR05.

## Implementation notes

Profiles, in NimBLE units. Interval is in 1.25 ms units, supervision timeout is
in 10 ms units. `NimBLEClient::setConnectionParams()` and
`NimBLEClient::updateConnParams()` in esp-nimble-cpp 2.5.0 pass these values
straight to `ble_gap_update_params()` with no conversion.

| Profile | Interval | Latency | Supervision timeout |
|---|---|---|---|
| Fast (current) | 30 to 50 ms | 1 | 5120 ms |
| Idle | 250 to 300 ms | 0 | 16000 ms |

Correction to the roadmap assumption, worth stating in the PR body: peripheral
latency lets the *peripheral* skip connection events. furble is the central, so
the central still has to be present at every connection event. Raising the
interval is what reduces furble's own wakeups. Latency adds delay to data
either side wants to send and saves furble nothing, so the idle profile keeps
latency 0 and gets the whole power win from the interval.

Consequences of the idle interval:

- A central-initiated parameter update applies several connection events after
  the request (the controller picks an instant at least six events out), so
  snapping to fast cannot speed up the press that triggers it. The first press
  after a quiet period always goes out at the idle interval. That is why the
  idle interval is capped at 300 ms rather than the 1 s first considered: it
  bounds the worst case first press instead of relying on the snap.
- A camera to furble notification such as Fujifilm's GEOTAG request can be
  delayed by up to one idle interval, so up to 300 ms at these values. The GPS
  `MAX_AGE_MS` budget in `src/FurbleGPS.cpp:166-172` is easily met.
- Supervision timeout must stay comfortably above interval times (latency + 1).
  16 s detects a dead link in half the time of the 32 s BLE maximum and check
  the return value of `updateConnParams()`. The controller rejects values
  outside the specification limits, so log rejections rather than assuming
  they applied.

Snap to fast:

- Do it before the write, not after the button press is queued. The queue path is
  `UI` to `Control::sendCommand()` to `Control::Target::sendCommand()` to
  `Control::Target::task()`. The earliest reliable point is
  `src/FurbleControl.cpp:165-179`, where the state machine fans a command out to
  every target. Renegotiate there, then dispatch.
- Renegotiation is not instant. It takes at least one connection interval and the
  peer must accept. Measure the real cost. If it exceeds the interval saved, keep
  the fast profile latched for a few seconds after any command instead of
  toggling per press. A latch with a 10 s idle timer is the pragmatic design.
- Never renegotiate while the shutter is held (bulb or shutter lock).

Peer renegotiation:

- Override `NimBLEClientCallbacks::onConnParamsUpdateRequest(NimBLEClient *, const ble_gap_upd_params *)`
  in `Camera`. Return true to accept. A camera that asks for its own parameters
  wins, and furble must not fight it in a loop.
- Track the parameters actually in force, not the requested ones. Read them back
  through `NimBLEClient::getConnInfo()` for the debug page.
- Add a guard so furble does not re-request the same profile more than once every
  few seconds. Repeated `ble_gap_update_params()` calls can destabilise a link.

Vendor gating:

- Only Fujifilm can be hardware tested. Consider gating the feature to
  `Camera::Type::FUJIFILM_BASIC` and `FUJIFILM_SECURE` in the first version, with
  a follow up to widen it once other vendors are confirmed by the community. If
  gated, say so in the setting description and in the PR body.

## Dependencies

- Requires PR08 for the Bluetooth submenu.
- Requires PR05 for the Diagnostics submenu that hosts the BLE debug page.
- Benefits from PR07, because a longer interval only pays off if the chip is
  allowed to sleep between events.
- Requires PR02 for the battery measurement harness.

## Risks

- Highest risk PR in the roadmap. Wrong parameters cause dropped connections,
  missed shutter commands or a camera that refuses the update.
- Missed GEOTAG requests on Fujifilm if the idle interval is too long. Test this
  explicitly.
- Renegotiation storms if furble and the camera disagree. Mitigated by accepting
  peer requests and by the rate guard.
- Supervision timeout too short for the chosen interval and latency causes
  intermittent disconnects that only show up after minutes. Only the long soak
  test catches this.
- Multi-connect makes it worse. Several links renegotiating at once increases
  scheduling pressure in the controller. Test with two cameras if two are
  available, otherwise use one camera plus FauxNY and say so.
- Mitigation of last resort: the setting defaults to off and can be gated to
  Fujifilm.

## Verification

Build matrix:

```
pio run -e m5stick-c -e m5stick-c-plus -e m5stack-core -e m5stack-core2 -e m5stick-s3
```

On device over USB:

1. `pio run -e m5stick-s3 -t upload`, then `pio device monitor`.
2. Fresh NVS boot. Confirm `CONN_SAVER` defaults to false and that the connection
   parameters logged on connect match master.
3. Enable the setting. Connect to Fujifilm. On the BLE debug page confirm the
   idle profile is in force and that the values match what was requested.
4. Shutter latency: 20 presses with a 30 s idle gap each, timing button press to
   shutter. Compare against master and against the setting off. Report the median
   and the worst case.
5. GEOTAG: with GPS enabled and a fix, confirm the camera's geotag request is
   still served and that the position written is fresh, using the
   `MAX_AGE_MS` check at `src/FurbleGPS.cpp:166-172`.
6. 60 minute connected soak with the setting on. Zero disconnects. Log every
   `onConnParamsUpdateRequest` and every `updateConnParams()` return code.
7. Peer renegotiation: if the camera requests its own parameters, confirm furble
   accepts and stops re-requesting.
8. Multi-connect with one Fujifilm plus FauxNY enabled. Confirm no regression.
9. Repeat steps 2, 3, 4 and 6 on one AXP192 board.

Battery drain, on-board instrumentation only, no external power meter:

- Unplugged 60 minute connected idle runs on M5StickS3: setting off, setting on,
  and setting on with PR07's sleep while connected also on. Log battery voltage
  and percent every 30 s and report percent per hour. The third case is the one
  that should show the real win.

Camera testing:

- Only Fujifilm cameras are available. Fujifilm gets the full matrix above.
- FauxNY exercises the control state machine without a radio and is useful for
  the multi-connect case, but it cannot validate connection parameters.
- Sony, Nikon, Canon and Ricoh are not hardware tested. This PR changes shared
  code in `Camera`, so the risk is real for them, not just theoretical. Either
  gate the feature to Fujifilm in this PR or state in the PR body, in plain
  words, that the other vendors are untested and the setting is off by default.

## Implementation status

Implemented on `feat/10-adaptive-conn-params`:

- Added the experimental `CONN_SAVER` setting with NVS key `conn_saver`, default
  `false`, under `Settings > Bluetooth > Connection power save`.
- Added fast and idle live profiles in `Camera`. Fast retains the existing 30 to
  50 ms interval, latency 1 and 5120 ms supervision timeout. Idle requests 250
  to 300 ms, latency 0 and a 16000 ms supervision timeout after 10 seconds
  without shutter or focus activity.
- The target task requests fast on every shutter or focus press without waiting
  for it, tracks held shutter state, and allows the idle request only after the
  quiet period. A three second request guard on idle requests avoids repeated
  renegotiation attempts; fast requests bypass the guard so a failed request is
  retried on the next press.
- Honest latency numbers at the idle profile: the fast request applies at least
  six connection events after it is issued, so the press that triggers it still
  goes out at the idle interval. Fujifilm shutter is two sequential write with
  response operations. Each costs up to one idle interval to reach the next
  anchor plus one interval for the response, so about 600 ms per write and
  about 1.25 s worst case for the first press including the 50 ms command
  queue tick. Typical is 600 to 900 ms. The fast profile is in force about
  1.5 to 1.8 s later (six events at 250 to 300 ms) and follow-up presses run
  at the fast profile, about 200 ms.

Rebase notes:

- `CONN_SAVER` is assigned wire_id 29, continuing after `TX_ADAPTIVE` (28)
  from PR 25.
- `Control::connectAll()` was restructured on master to snapshot cameras under
  the mutex and connect outside it. The setting is now loaded once before the
  lock and applied to every camera inside the snapshot loop.
- Console settingType, printValue and setValue cover `CONN_SAVER` as bool.
  The console reports `applies: on next connect` for it: only the UI toggle
  calls `Control::setConnSaver()` live; a console or companion write reaches
  cameras on the next connect.
- `src/FurbleCompanion.cpp` settingType and settingValue cover `CONN_SAVER`
  as SETTING_BOOL.
- `onConnParamsUpdateRequest()` accepts peer values and marks the link as peer
  controlled until a new shutter or focus activity cycle requests fast again.
  The BLE diagnostics page reads the live `NimBLEConnInfo` values and RSSI.
- Code-level Fujifilm GEOTAG review is complete. Existing Fujifilm basic and
  secure connections continue subscribing to the GEOTAG notification, and
  `Fujifilm::notify()` continues setting `m_GeoRequested` when it arrives. The
  idle interval can delay delivery by up to 2 seconds with latency 1, but it does
  not disable or drop the notification path. The existing GPS `MAX_AGE_MS` check
  remains the freshness gate. Hardware verification is still pending.
- Build verification passed with `FURBLE_VERSION=dev`, `FURBLE_TEST=0` and the
  requested PATH export: `pio run -e m5stick-s3` and
  `pio run -e m5stick-c-plus`.
- Only Fujifilm hardware is available for follow-up testing. Sony, Nikon, Canon
  and Ricoh remain untested, and the experimental setting remains off by default.

Review fixes (deep review of the first branch revision):

- Connect no longer runs at the idle interval. The idle request on
  `onConnect()` is gone and `Camera::connect()` sets a connect-in-progress
  gate that `maybeSetIdle()` checks, so discovery and subscriptions always run
  at the fast interval. Previously a saver-enabled connect could take minutes
  and the 10 s idle timer could drop a mid-discovery link to idle. The
  inactivity timer restarts when the connect completes.
- `Control::setConnSaver()` and the saver application in
  `Control::connectAll()` snapshot cameras under the control mutex and apply
  outside it, because applying to a connected camera enters NimBLE and can
  block on the HCI transport for up to two seconds (previously on the LVGL
  task with the mutex held).
- Idle profile retuned from 500 to 1000 ms, latency 1, 32 s timeout down to
  250 to 300 ms, latency 0, 16 s timeout, bounding the first-press latency
  described above.
- The three second request guard now applies only to idle requests. A fast
  request that failed (for example `BLE_HS_EALREADY` while an idle update was
  in flight) is retried on the next press instead of being latched for 3 s.
- The BLE diagnostics page reads a cached parameter and RSSI snapshot that the
  per-target task refreshes once a second. The UI no longer issues blocking
  RSSI HCI reads or touches a client that may have self-deleted on disconnect.
  `Control::getTargets()` returns a snapshot copied under the mutex.
- `setConnProfile()` mirrors the requested profile into the NimBLE client so a
  peer CONN_UPDATE_REQ is counter-proposed with the current profile instead of
  the stale pre-connect fast parameters. The pre-connect parameter members are
  const again.

## References

- [Nordic Bluetooth LE connection parameters](https://academy.nordicsemi.com/courses/bluetooth-low-energy-fundamentals/lessons/lesson-3-bluetooth-le-connections/topic/connection-parameters/)
  for the definitions of connection interval, peripheral latency and supervision
  timeout, and for the point that peripheral latency lets the peripheral skip
  events while the central is dictated by the connection.
- [ESP-IDF BLE connection guide](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/ble/get-started/ble-connection.html)
  for the 1.25 ms step and the 7.5 ms to 4.0 s connection interval range, and for
  peripheral latency being the number of events the peripheral may skip.
- [NimBLE ble_gap.h](https://github.com/apache/mynewt-nimble/blob/master/nimble/host/include/host/ble_gap.h)
  for `BLE_GAP_INITIAL_CONN_ITVL_MIN` of 30 ms, `BLE_GAP_INITIAL_CONN_ITVL_MAX`
  of 50 ms and `BLE_GAP_INITIAL_SUPERVISION_TIMEOUT` of `0x0100`.
- [esp-nimble-cpp 2.5.0 NimBLEClient.cpp](https://raw.githubusercontent.com/h2zero/esp-nimble-cpp/2.5.0/src/NimBLEClient.cpp)
  for `setConnectionParams()` and `updateConnParams()` passing values through
  without unit conversion, and for `getRssi()`.
- [esp-nimble-cpp 2.5.0 NimBLEClient.h](https://raw.githubusercontent.com/h2zero/esp-nimble-cpp/2.5.0/src/NimBLEClient.h)
  for the `NimBLEClientCallbacks::onConnParamsUpdateRequest()` signature.

## Hardware verification, 2026-08-17

Verified on the combined image with the X100VI. With `conn_saver` on, the link
dropped to the idle profile 10 s after the last activity, log:
`Requested idle connection profile (200-240, latency 0, timeout 1600)`. A
shutter press after idle immediately requested the fast profile (24-40,
latency 1, timeout 512) and issued `shutterPress` 3 ms later, so the command
is not blocked behind the renegotiation. Finding: active GPS geotagging keeps
the link permanently fast because every geotag write counts as activity, so
the saver only helps with GPS off or without a fix.
