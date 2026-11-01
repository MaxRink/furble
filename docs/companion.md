# Companion camera management

The companion BLE service exposes camera management through the Cameras
characteristic:

`b57f4f63-087b-4740-b71d-8262cf26ebbc`

It supports write, indicate, and notify. Write requests are two bytes:

```
uint8 op        // 0 list, 1 connect, 2 disconnect, 3 select, 4 deselect
uint8 camera_id // 0xff means all; ignored by list and disconnect
```

List records and command responses carry this fixed header followed by the
camera name:

```
uint8 status     // 0 ok, 1 unknown ID, 2 rejected, 3 busy
uint8 camera_id
uint8 camera_type
uint8 flags      // bit 0 saved, bit 1 selected, bit 2 active target, bit 3 connected
uint8 progress   // 0 to 100
int8  rssi       // -128 when unknown
uint8 state      // 0 idle, 1 connecting, 2 connected, 3 reconnecting, 4 lost, 5 disconnecting
uint8 name_len
uint8 name[name_len]
```

The list ends with a record whose camera ID is `0xff`. Indications carry list
records and command responses. Notifications carry changed camera state.

Camera IDs are stable across catalog reordering and deletion. Selection is the
same in-memory Multi-Connect target set used by the on-device Cameras page. A
scan updates only transient scan results, so it does not hide saved cameras.
Connect and disconnect requests follow the device control request path. A
connect request is busy while scanning or another connect is in flight.

## Phone location

The companion can supply a location fix when the wired GPS receiver has none.
This is the indoor fallback. A GNSS receiver needs a usable sky view. A phone
usually has a recent cell, WiFi, or GNSS fix and can hand it to furble over the
existing BLE link.

The device firmware prefers a fresh wired UART fix. It uses a companion fix
only when the UART has no fresh fix. A fix is rejected when its age plus its
transit time exceeds the firmware freshness window. When the companion link
drops, the cached companion fix is cleared. The status notification reports
`gps_source` as one of:

| Value | Source | App label |
| :--- | :--- | :--- |
| 0 | none | No fix |
| 1 | wired UART receiver | Receiver, with a satellite count |
| 2 | phone companion | Phone |

Both apps have a phone GPS toggle. Android also has an interval setting that
defaults to 10 seconds and a count of fixes sent. The apps request precise
location, not approximate location.

Platform behavior:

- iOS declares the `location` background mode and sets
  `allowsBackgroundLocationUpdates` while the toggle is on. When In Use
  authorization is enough for a user-started session. The system shows the
  location indicator while background updates are active. Turning the toggle
  off stops updates and the indicator.
- Android uses a balanced-power fused location provider. The current app has
  no foreground location service, so continuous delivery in the background is
  not implemented. `CompanionPresenceService` is a system-bound presence
  callback and never becomes a foreground service. A future change needs a
  service with `foregroundServiceType="location"`, the matching permissions, a
  notification, and a field test.

Phone location is not a replacement for a good antenna. It is the fallback for
places where no receiver can hear the satellites.
