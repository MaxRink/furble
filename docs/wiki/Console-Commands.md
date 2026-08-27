# Console commands

Debug builds (every `<board>-debug` env) add a USB serial command console on the
same port that carries the log. It lets a developer or a host script drive furble
without walking the menu tree. No release build contains it.

Open it and type `help`:

```sh
pio device monitor -e m5stick-s3-debug
```

Log output shares the port, so `log * warn` is usually the first thing worth
typing. Every command prints one fact per line as `key: value`.

For a development build, `version` reports `dev+g<revision>`, where the suffix
is Git's unambiguous abbreviation of the checked-out commit (at least eight
characters). A tracked, staged, or non-ignored untracked change adds `.dirty`.
Explicit release versions remain unchanged. The same identity is shown on the
About page and exposed through companion BLE Device Information.

## Command summary

| Command | What it does |
| :--- | :--- |
| `version` | Firmware and IDF version. |
| `status` | State, targets, uptime, heap, battery, reset reason. |
| `power` | `stats`, or `log <seconds>` / `log off` for a CSV power log. |
| `perf` | `tasks`, `heap`, or `lvgl [overlay on\|off]`. |
| `gps` | GPS status and control, see below. |
| `time` | `status` reports wall-clock validity and source; `flush` persists it. |
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

On the display-less Waveshare ESP32-S3-ETH, `status` reports battery level and
voltage as unknown (`-1`) and current as unavailable (`0`). It never infers USB
or optional PoE power from Ethernet link state because the optional PoE HAT has
no software-readable presence or negotiation signal.

## settings

- `settings list` prints every setting as `key: value`.
- `settings get <name>` prints the name, type, value, and when a change applies.
- `settings set <name> <value>` saves a setting. Values are range checked and
  rejected when the board does not support the setting.

See the [Settings Reference](Settings-Reference) for every setting, its default,
and when it applies.

## gps

- `gps` with no argument prints the fix, satellites, position, age, and error
  counts.
- `gps on | off` drives the GPS setting and reloads the receiver.
- `gps raw on | off` mirrors incoming NMEA to the console.
- `gps send <body>` sends a raw sentence, for example `gps send PCAS12,10`.
- `gps binary <class hex> <id hex> [payload bytes]` sends a CASIC binary frame.
- `gps config` lists the binary configuration status.
- `gps aid` sends an assisted-start hint.
- `gps power on | off` drives the external 5V rail.

## time

- `time status` reports the UTC wall-clock estimate, uncertainty, source, RTC
  capability, and NVS write count.
- `time flush` persists the current estimate before a planned restart or power
  removal.

StickC, StickC-Plus, and Core2 retain calendar time across power loss through
their backed RTC when its backup supply is healthy. StickS3 has no
battery-backed calendar RTC, so it restores the last NVS value with an explicit
uncertainty penalty until GPS, NTP, or a companion supplies a fresh sample.

## shutter and focus

- `shutter press`, `shutter release`, `shutter hold <ms>`.
- `focus press`, `focus release`.

These enqueue camera commands directly. They bypass the button-mode dispatch and
the shutter-lock state.

## bt

- `bt scan [seconds | all [seconds] | stop]` sniffs advertisements.
- `bt explore <addr> [pair <mode>] [keep] | read | stop [keep]` walks a peer's
  GATT.
- `bt pair yes | no | key <6 digits>` answers a pairing prompt.
- `bt journal on | off | dump [n] | clear` records a GATT journal.

## debug

`debug <target>` dumps internal state for one subsystem: `control`,
`camera [idx]`, `ble`, `heap`, `tasks`, `power`, `gps`, `settings`, or `all`.

## Related pages

- [Settings Reference](Settings-Reference)
- [Supported Hardware](Supported-Hardware)
- [UI Walkthrough](UI-Walkthrough)
