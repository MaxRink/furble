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
| `power` | `stats`, `log <seconds>` / `log off` for a CSV power log, or `off` to shut down. |
| `perf` | `tasks`, `heap`, or `lvgl [overlay on\|off]`. |
| `gps` | GPS status and control, see below. |
| `time` | `status` reports wall-clock validity and source; `flush` persists it. |
| `settings` | `list`, `get <name>`, `set <name> <value>`. |
| `ui` | `audit` dumps the page layout, `page` names the current page, `back` presses the header back button. |
| `cameras` | `list` saved cameras, or `status` for the active targets. |
| `connect` | `connect [index]`. No index uses the multi-connect selection. |
| `pair` | `pair <scan index>`, onboard a camera from `scan list`. |
| `delete` | `delete <saved index>` or `delete all`, forgets the camera and its bond. |
| `multiconnect` | `list`, `select <index>`, `deselect <index>`, `clear`. |
| `disconnect` | Disconnect all cameras. |
| `shutter` | `press`, `release`, or `hold <ms>`. |
| `focus` | `press` or `release`. |
| `interval` | `start`, `stop`, or `status`, the Timer page. |
| `bulb` | `start`, `stop`, or `status`, the Bulb page. |
| `display` | `status`, `mode gui\|console`, or `brightness <value>`. |
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
- `focus press`, `focus release`. Ricoh cameras treat both commands as no-ops.
  Their shutter command uses `OperationRequest {0x01, 0x01}` for capture with
  autofocus.

These enqueue camera commands directly. They bypass the button-mode dispatch and
the shutter-lock state.

## Workflow coverage

Every workflow the menus offer is scriptable, through the same production code
the menus run. A console verb parses and gates, then hands one request to the
UI task, which runs the handler the button click runs. The pairing prompt, the
registration gate and the save on success behave identically either way.

`pair <scan index>` is the Scan page row click. The index names a row of the
most recent `scan list`, and the verb is refused unless a scan is running,
because otherwise that list holds saved cameras and `connect` is their verb. A
console scan runs until `scan stop`, so a full onboarding is `scan start`,
`scan list`, `pair 0`, then `bt pair yes` if the camera raises a prompt. On
success the camera is saved, and `cameras list` shows it as
`camera<N>.saved: true`.

`delete <index>` is the Delete page row click, dropping the stored entry and
the BLE bond together; `delete all` sweeps the list, which the menus have no
button for. `multiconnect select | deselect <index>` ticks a Connect page
checkbox and persists the remembered set; `multiconnect list` and
`multiconnect clear` read and empty it.

`interval start | stop | status` drives the Timer page and
`bulb start | stop | status` the Bulb page. Both refuse to start without an
active connection, since both fire the shutter.

`display brightness <value>` applies live, matching the Display page slider;
`settings set brightness` only persists. `power off` runs the Off menu entry.
`ui page` names the current page and `ui back` presses the header back button.
Navigating to an arbitrary page by name is deliberately not offered: those
tables exist only in the simulator build.

## bt

- `bt scan [seconds | all [seconds] | stop]` sniffs advertisements.
- `bt explore <addr> [pair <mode>] [keep] | read | stop [keep]` walks a peer's
  GATT.
- `bt pair yes | no | key <6 digits>` answers a pairing prompt, including the
  one a `pair <scan index>` onboarding raises.
- `bt journal on | off | dump [n] | clear` records a GATT journal.

The journal is a fixed 32-event ring on boards without PSRAM and a 128-event
ring on StickS3 when PSRAM is available. Recording is silent, live output is
limited to eight events per console turn, and `dump [n]` is bounded. Entries
carry monotonic time, GAP reason text and address identity types, scan owner /
generation and physical/logical state, connection parameter transitions,
security state and key size, and typed GATT operation details including UUIDs,
CCCD value, response mode, result, payload length, and duration. The compact
record ring stays below 8 KiB of internal memory on non-S3 boards. StickS3 uses
PSRAM for its 128 records and falls back to 32 internal records without PSRAM.
It does not write bonds or NVS.

## debug

`debug <target>` dumps internal state for one subsystem: `control`,
`camera [idx]`, `ble`, `heap`, `tasks`, `power`, `gps`, `settings`, or `all`.

## Related pages

- [Settings Reference](Settings-Reference)
- [Supported Hardware](Supported-Hardware)
- [UI Walkthrough](UI-Walkthrough)
