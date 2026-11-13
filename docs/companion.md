# Companion camera management

The companion keeps its bond and GATT service across `companion stop` and
`companion start`; restarting does not require a fresh pairing. After the
60-second window closes the device advertises for the bonded central, and a
central that presents a different identity must reopen the window. `companion
status` reports the persisted setting, live service, saved bond, window state
and current advertising state. Reconnect advertising is unfiltered because
macOS rotates its LE address; the connect handler still accepts only the stored
identity outside the pairing window.

The first enable after boot creates the 3.75 KiB companion task and the GATT
service, and both stay for the rest of the session. That first enable needs
8,320 bytes of free internal RAM and a 4,224-byte block. Below that the
companion stays off and the USB log says:

```text
Companion not started: internal free N, largest N (need 7552/3456). It stays off; enable it again or reboot to retry.
```

The saved setting does not change. Enabling it again once RAM is free, or a
reboot, retries. The same check runs at boot, where free RAM is well above it.

The NimBLE server is shared with Sony camera name discovery, so an unknown
inbound link is not torn down. It can keep the link (and therefore suppress
companion advertising) until it disconnects; advertising restarts when the link
ends. This is the availability cost of not breaking camera clients.

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

The companion radio advertises for 60 seconds after companion mode is opened.
Reopen companion mode to pair another phone.

## Pairing and link security

The service starts the BLE security procedure as soon as an accepted companion
link connects; a protected characteristic read alone does not initiate pairing.
The link is bonded and encrypted with LE Just Works so a macOS terminal,
background session, or other client without a usable system pairing prompt can
connect. Just Works does not provide link-level man-in-the-middle protection.
The companion password is the application authentication layer: set one with
`companion password set` before using the app in an untrusted area, and the
firmware rejects commands until the HMAC exchange succeeds. The challenge and
result packets use indications on the Auth characteristic; the client subscribes
before writing the begin packet.

When the pairing window is open and the peer presents a new LE address, the
firmware drops the stale bond for the old address before starting security.
This handles macOS LE address rotation without leaving a permanently unusable
bond. Reopen companion mode and retry if a client still holds an old bond.

### Recovering a stale host GATT cache

CoreBluetooth caches the GATT handle map per peripheral. A firmware update that
moves characteristics (for example the bench-only debug service) can leave the
host writing CCCD values to handles that no longer exist. The firmware no
longer sends a Service Changed indication during authentication because
CoreBluetooth reacts by rediscovering services while the auth exchange is in
flight, which intermittently dropped the auth write or the first status
notifications.

Use the debug console command `companion reidentify` when the host keeps using
an old handle map. It stops advertising first, then generates, applies and
persists the new random-static LE address before disconnecting the current link
and dropping only the companion bond (camera bonds are untouched). A failed
rotation restores the previous advertising state and keeps the link and bond.
On success it reopens the 60 second pairing window. The new address is restored
before advertising on later boots, so the host keeps the bond it creates after
the rotation instead of finding the previous address again. The host then sees
a new peripheral identity and builds a fresh cache. Pair once more with the
companion password.

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

## Apple macOS terminal companion

`companion/apple` also builds `furble-companion-cli`, a headless macOS client
that shares `FurbleCompanionCore` with the native SwiftUI app. It uses the same
frozen UUIDs, status/location/camera codecs, HMAC Auth exchange, CoreBluetooth
client and state machine. The CLI is intended for bench testing from a Mac
terminal; it does not replace the signed iOS app.

```sh
cd companion/apple
swift build -c release --product furble-companion-cli
.build/release/furble-companion-cli scan --scan-timeout 10
.build/release/furble-companion-cli status --device Furble
```

The CLI also has a USB CDC transport for the StickS3 console:

```sh
.build/release/furble-companion-cli scan --transport usb
.build/release/furble-companion-cli status --transport usb --port /dev/cu.usbmodem1101
.build/release/furble-companion-cli monitor --transport usb --port /dev/cu.usbmodem1101 \
  --interval 5 --duration 3600 --csv > usb-power.csv
```

USB supports `scan`, `status`, `cameras`, `monitor`, and `password status`. CSV
rows are written per sample, so a redirected monitor can be tailed and keeps the
samples already collected if it is interrupted. The monitor gives each console
command a 5 s timeout so a debug display flush does not look like a dead link.
`location` and `trigger` stay on BLE because the console has no equivalent
command. The USB monitor reads `status` and `gps` from the console and writes
the same CSV columns as the BLE monitor, so the plotter is shared. USB charging
invalidates battery-drain comparisons.

It can send a phone-GPS fallback fix, press/release shutter or focus, and poll
status notifications. The companion password comes from an argument,
environment variable, file, stdin or the login Keychain, and is never printed.
A firmware whose companion password is unset returns the explicit
`not required` Auth result; a firmware that requires a password and receives
none fails closed with a non-zero exit instead of downgrading.

The macOS terminal needs Bluetooth permission. macOS exposes a per-host
peripheral UUID rather than a BLE MAC address, so `--device` accepts the UUID
from `scan` or an advertised name substring. No camera needs to be attached for
status and power monitoring; camera command success cannot be certified from a
bench without a real camera.

## BLE debug status (bench image)

`pio run -e m5stick-s3-debug-ble` adds a read-only BLE status service for
bench debugging. It is deliberately unencrypted and is not part of the
production companion contract. The service answers even when companion auth,
the host GATT cache, or the USB CDC endpoint is broken.

- service `b57f4f70-087b-4740-b71d-8262cf26ebbc`
- status `b57f4f72-087b-4740-b71d-8262cf26ebbc` (read)

The snapshot reports reset reason, uptime, task count, internal/PSRAM free and
largest blocks, bond count, BLE connections, and the companion enabled/
connected/encrypted/authenticated/bond/window/advertising state. Enable
Companion mode so the service exists, then run:

```sh
python3 -m pip install bleak
tools/ble_debug.py --device furble-38a70 --status
tools/ble_debug.py --device furble-38a70 --status --interval 5
```

## Battery observation run

`monitor` is a read-only long-run mode. It keeps the authenticated BLE link and
status subscription alive, samples notifications at `--interval`, and writes
one CSV row per sample. It does not write settings, issue camera commands,
send location fixes or trigger the shutter. Reconnects use the shared client's
scan/auth path with 1, 2, 4 ... 30 s backoff; a drop adds a marker row instead
of ending the run.

CSV columns:

```text
host_time_iso,host_elapsed_s,uptime_s,battery_percent,battery_mv,battery_ma,gps_source,gps_satellites,control_state,rssi,label
2026-11-04T12:34:56Z,300.2,1234,87,4012,0,2,0,0,-61,GPS off
```

The firmware `battery_ma` field is reported raw. Some builds have no PMIC
current backend and return 0; the CLI prints a one-time stderr warning and
never derives a current. The useful battery signal is the percentage and
millivolt slope over time. `battery_mv` is also raw: `0xffff` means the voltage
is unavailable. CSV keeps 65535 for diagnostics, while human rows, the summary
and the status report print `unknown`; the plotter drops that voltage point but
keeps the valid percentage sample.

Procedure for a comparable drain run:

1. Charge the controller to 100% with USB attached. Open Companion mode and
   run `status` once to confirm authentication, battery percentage and the
   expected firmware build.
2. Choose one BLE/GPS configuration per run: GPS off (`Settings > GPS > GPS`
   off), GPS on with receiver standby 5 s (`Settings > GPS > Power saving >
   Receiver: Standby (PCAS12)`, `Sleep between fixes: 5 s`), or GPS always on
   (`Receiver: Always on`). Keep the screen brightness, display timeout, BLE
   interval, camera connection state and companion activity constant across
   runs.
3. Disconnect USB before starting the measured run. A run started while USB is
   attached measures charging, not drain.
4. Start the watcher before or immediately after unplugging USB. Use at least
   an hour for a trend, or `--until-empty` for a full drain:

   ```sh
   .build/release/furble-companion-cli monitor --device Furble \
     --interval 5 --duration 3600 --csv --label "GPS off" > gps-off.csv
   .build/release/furble-companion-cli monitor --device Furble \
     --interval 5 --until-empty --csv --label "GPS standby 5s" > standby-5s.csv
   ```

5. Keep the Mac awake and the terminal attached. The CLI flushes every row, so
   the CSV remains usable during the run and after an unexpected host restart.
   A transient BLE drop produces `link-lost`/`reconnected` marker rows; after
   `--retry-window` seconds with no reachable device it writes
   `retry-exhausted` and exits 0.
6. A full drain ends with the controller powering off. The watcher cannot
   receive a final 0% sample; it stops after the bounded retry window. If the
   last notification reports 1% or less with `--until-empty`, it writes a
   `battery-empty` marker.
7. Recharge the controller with USB attached. Repeat steps 1-6 for the other
   GPS configurations, then graph each CSV:

   ```sh
   python3 tools/plot_companion_power.py gps-off.csv -o gps-off.svg
   python3 tools/plot_companion_power.py standby-5s.csv -o standby-5s.svg
   ```

BLE itself adds a small, roughly constant load while the link is up, and the
Mac's radio/connection interval affects it. Compare runs with the same monitor
interval and the same Mac distance; do not subtract `battery_ma` when it is 0.
A full-drain run also cooks the battery at low voltage for the final minutes,
so prefer a 60-90 minute partial run when only comparing GPS policies.

Phone location is not a replacement for a good antenna. It is the fallback for
places where no receiver can hear the satellites.

The horizontal accuracy the phone reports travels in the same location packet
and is carried into the device fix when known. The MQTT geolocation payload
includes it as `accuracy`; a phone that reports unknown accuracy omits the
field rather than claiming a perfect fix.
