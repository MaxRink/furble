# furble console commands

Debug builds (`FURBLE_CONSOLE`, every `<board>-debug` env) add a USB serial
command console on the same port that carries the log. It lets a developer or a
host script drive furble without walking the menu tree. No release build
contains it.

Open it and type `help`:

```sh
FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e m5stick-s3-debug -t upload
pio device monitor -e m5stick-s3-debug
```

Log output shares the port, so `log * warn` is usually the first thing worth
typing. Every command prints one fact per line as `key: value`, so a host script
can parse it with a split on the first colon.

The console distorts power measurements, so do not take power numbers from a
build that contains it. On the ESP32 boards the console holds an APB frequency
lock for its lifetime. On the M5StickS3 no lock is needed.

For a development build, `version` reports `dev+g<revision>`, where the suffix
is Git's unambiguous abbreviation of the checked-out commit (at least eight
characters). Explicit release versions remain unchanged. The same identity is
shown on the About page and exposed through companion BLE Device Information.

## Command summary

| Command | What it does |
| :--- | :--- |
| `version` | Firmware and IDF version. |
| `status` | State, targets, uptime, heap, battery, reset reason. |
| `power` | `stats`, or `log <seconds>` / `log off` for a CSV power log. |
| `perf` | `tasks`, `heap`, or `lvgl [overlay on\|off]`. |
| `gps` | GPS status and control, see below. |
| `settings` | `list`, `get <name>`, `set <name> <value>`. |
| `ui` | `ui audit`, dump the current page layout. |
| `cameras` | `list` saved cameras, or `status` for the active targets. |
| `connect` | `connect [index]`. No index uses the multi-connect selection. |
| `disconnect` | Disconnect all cameras. |
| `shutter` | `press`, `release`, or `hold <ms>`. |
| `focus` | `press` or `release`. |
| `ir` | `ir fire [protocol]`, 0 Nikon, 1 Sony, 2 Canon, 3 Canon 2s. |
| `scan` | `start`, `stop`, or `list`. |
| `bt` | Bluetooth diagnostics, see below. |
| `feedback` | `feedback test <shutter\|countdown\|connect\|disconnect\|battery>`. |
| `log` | `log <tag> <level>`, `*` sets all tags. |
| `debug` | Dump internal state, see below. |
| `reboot` | Restart the device. |
| `help` | List every command. |

## settings

- `settings list` prints every setting as `key: value`.
- `settings get <name>` prints the name, type, value, and when a change applies.
- `settings set <name> <value>` saves a setting. Values are range checked and
  rejected when the board does not support the setting, for example
  `SD_GPX` on a board with no SD card slot.

Saving a setting is not the same as applying it. Settings read on every use take
effect at once. Settings the UI caches when it starts do not. `settings get`
reports which of the two a setting is: immediately, on next connect, or on
reboot. See the [settings and controls reference](settings-and-controls.md) for
every setting, its default, and when it applies.

## gps

- `gps` with no argument prints enabled, fix, satellites, lat, lon, alt, age,
  date, time, and error counts.
- `gps on | off` drives the GPS setting and reloads the receiver.
- `gps raw on | off` mirrors incoming NMEA to the console.
- `gps send <body>` sends a raw sentence, for example `gps send PCAS12,10`.
- `gps binary <class hex> <id hex> [payload bytes]` sends a CASIC binary frame.
- `gps config` lists the binary configuration status.
- `gps aid` sends an assisted-start (AID-INI) hint.
- `gps power on | off` drives the external 5V rail, for rail-cut experiments.

## shutter and focus

- `shutter press` fires and `shutter release` ends. `shutter hold <ms>` fires,
  waits, and releases, up to 60000 ms.
- `focus press` half-presses focus and `focus release` ends.

These enqueue camera commands directly. They bypass the button-mode dispatch and
the shutter-lock state, so use them to trigger the camera, not to test the
button gestures.

## bt

The `bt` family drives Bluetooth diagnostics for camera bug reports. It backs
the web installer Capture BT debug dump panel.

- `bt scan [seconds | all [seconds] | stop]` sniffs advertisements.
- `bt explore <addr> [pair <mode>] [keep] | read | stop [keep]` walks a peer's
  GATT. Pair modes are none, just-works, and numeric-display.
- `bt pair yes | no | key <6 digits>` answers a pairing prompt.
- `bt journal on | off | dump [n] | clear` records a GATT journal.

## debug

`debug <target>` dumps internal state for one subsystem: `control`,
`camera [idx]`, `ble`, `heap`, `tasks`, `power`, `gps`, `settings`, or `all`.

## Related references

- [Settings and controls reference](settings-and-controls.md)
- [Supported hardware](supported-hardware.md)
- [UI walkthrough](ui-walkthrough.md)
