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
| `boot` | `boot timing` prints the last boot timeline, its mark deltas, and the cycle-counter reset estimate. |
| `status` | State, targets, uptime, heap, battery, reset reason. |
| `crash` | `crash [last]` prints the retained previous-boot crash record. |
| `power` | `stats`, or `log <seconds>` / `log off` for a CSV power log. |
| `perf` | `tasks`, `heap`, or `lvgl [overlay on\|off]`. |
| `gps` | GPS status and control, see below. |
| `time` | `status` reports wall-clock validity and source; `flush` persists it. |
| `settings` | `list`, `get <name>`, `set <name> <value>`. |
| `companion` | `start` (open the 60 s pairing window), `stop`, `status`, `reidentify` (rotate the LE address and drop only the companion bond), or `password set <pw>`, `clear`, `status`. |
| `ui` | `audit`, `page`, or `back` for UI diagnostics and navigation. |
| `cameras` | `list` saved cameras, or `status` for the active targets. Saved rows carry `saved_index`, which is the value `connect` takes. |
| `connect` | `connect [index]`. No index uses the multi-connect selection. A scan-list position is not a saved index. |
| `pair` | `pair <scan-index>` pairs a live scan result through the UI handler. |
| `delete` | `delete <index>` or `delete all` removes saved cameras and verifies persistence. |
| `multiconnect` | `list`, `select <index>`, `deselect <index>`, or `clear`. |
| `disconnect` | Disconnect all cameras. |
| `interval` | `start`, `stop`, or `status` for the Timer page on display builds. |
| `bulb` | `start`, `stop`, or `status` for the Bulb page on display builds. |
| `display` | `status`, `mode gui|console`, or `brightness <value>` on display builds. |
| `mqtt` | `status`, `connect`, `disconnect`, or `discovery clear`; MQTT broker control. |
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

`perf tasks` takes its two 24-entry snapshots from PSRAM when the board has it
and falls back to internal heap otherwise. If neither allocation succeeds it
prints `not enough heap for the task snapshot` instead of growing the console
stack.

On the display-less Waveshare ESP32-S3-ETH, `status` reports battery level and
voltage as unknown (`-1`) and current as unavailable (`0`). It never infers USB
or optional PoE power from Ethernet link state because the optional PoE HAT has
no software-readable presence or negotiation signal.

## BLE debug status (bench image)

`pio run -e m5stick-s3-debug-ble` adds a read-only, unencrypted BLE status
service for bench diagnosis. It is not part of the production companion
contract. It answers when companion authentication, the host GATT cache, or the
USB CDC endpoint is broken.

- service `b57f4f70-087b-4740-b71d-8262cf26ebbc`
- status `b57f4f72-087b-4740-b71d-8262cf26ebbc` (read)

The snapshot reports reset reason, uptime, task count, internal and PSRAM free
and largest blocks, bond count, BLE connections, and companion enabled/
connected/encrypted/authenticated/bond/window/advertising state. Enable
Companion mode so the service exists, then run:

```sh
python3 -m pip install bleak
tools/ble_debug.py --device furble-38a70 --status
tools/ble_debug.py --device furble-38a70 --status --interval 5
```

## wifi

`wifi status` prints `enabled`, `state`, `ssid`, `psk` (only `set` or `unset`),
`bssid`, `channel`, `rssi`, `ip`, `attempts`, `retry_ms`,
`disconnect_reason`, `stopped`, and the battery `current` /
`estimated_runtime` when available. `attempts` is the failed count in the
current retry cycle, `retry_ms` is the next backoff without jitter, and
`stopped` is set by a terminal auth failure or an exhausted attempt bound;
`wifi connect` clears it.

`wifi set ssid|psk`, `wifi enable|disable`, `wifi connect|disconnect`, and
`wifi forget` manage the saved network.

## boot

`boot timing` prints the retained boot timeline. Each mark has `at_ms` from
`app_main` and `delta_ms` from the previous mark in time order. The
`reset_to_app_ms` value comes from the CPU cycle counter and includes the ROM
bootloader and the PSRAM boot init that happen before the firmware timeline
starts. The same timeline is logged once as `boot: timeline …` after the first
UI frame or the first headless service pass.

See [boot timing](../boot-timing.md) for the mark list, the measured StickS3
baseline, and the PSRAM memory-test build option.

## crash

`status` and the crash record name reset reasons as `poweron`, `software`,
`panic`, `int_wdt`, `task_wdt`, `other_wdt`, `brownout`, `usb_uart_chip_reset`,
`jtag`, `power_glitch` or `cpu_lockup`.


`crash` and `crash last` print the record retained across the last reset. The
firmware also prints the same block once at boot when the previous run ended
unclean, then consumes the record. It reports `crash.reset`, `crash.task`,
`crash.uptime`, `crash.heap_free`, `crash.heap_largest`, `crash.last_log` and a
one-line summary. The task and log tail are the last normal snapshot, not a
guaranteed panic task. The record lives in RTC memory and never writes flash.

## settings

- `settings list` prints every setting as `key: value`.
- `settings get <name>` prints the name, type, value, and when a change applies.
- `settings set <name> <value>` saves a setting. Values are range checked and
  rejected when the board does not support the setting.
- `settings set timezone Europe/Berlin` applies the zone immediately. It accepts
  a table index, an IANA name, a single-word roller label, or `auto`. Auto uses
  a fresh wired GPS fix and falls back to UTC after 30 minutes without one or
  immediately when a fresh fix is outside the 30-degree nearest-zone radius.


## companion

- `companion start` enables Companion mode if it is off, starts the BLE
  service, and opens the 60 s pairing window. It prints the window line only
  after the live service reports enabled; a failed start returns an error and
  leaves the persisted setting off.
- `companion stop` persists the setting off and stops the live service
  synchronously.
- `companion reidentify` is the recovery path for a host with a stale GATT
  handle map. It disconnects the link, deletes only the companion bond, persists
  a new random-static LE address, and reopens the 60 s pairing window. The
  address is restored before advertising on later boots, so the host keeps the
  bond it creates after the rotation. Camera bonds are untouched.
- `companion status` prints `companion.setting` (the persisted value),
  `companion.enabled` (the live service), `companion.bond` (a saved peer exists),
  `companion.window` (the 60 s pairing window is open) and
  `companion.advertising` (the device is currently advertising). A settings-page
  or companion-write disable can keep the live service up for one second so the
  BLE write response can leave; the console stop path does not wait.
- `companion password set <pw>`, `companion password clear`, and
  `companion password status`. The password is write-only: status prints only
  `set`, `unset`, or `unavailable`. The generic `settings get` and `settings
  set` paths reject `companion_pw`. A successful set or clear reloads the live
  companion session and revokes any authenticated connection.

Workflow requests are queued to the UI task. A timeout means the outcome is
unknown and the operation may still execute later. Do not blindly retry a
timed-out destructive request.

UI workflow commands include `pair <scan-index>`, `delete <index> | all`, and
`multiconnect list | select <index> | deselect <index> | clear`. Pairing routes
through the same UI handler as the display scan page. `ui page` reports the
current page; `ui back` navigates back. `interval`, `bulb`, and `display` control
their display pages on display builds. A workflow command prints
`completed: <verb>` on success, or `failed: <verb>: <reason>` and returns
non-zero when the UI rejects it. A bounded wait prints `pending` when the result
is unknown, so do not blindly retry a timed-out destructive request.

See the [Settings Reference](Settings-Reference) for every setting, its default,
and when it applies.

## gps

- `gps` with no argument prints the receiver state (`detecting`, `present` or
  `absent`), the detected baud, the fix, satellites, position, age, and error
  counts. `fix` is `yes` only while the position, the GGA quality, and a
  location younger than the 30 s freshness bound all hold; it turns `no` at
  30 s even when the receiver still reports a quality.
- `gps on | off` drives the GPS setting and reloads the receiver.
- `gps raw on | off` mirrors incoming NMEA to the console.
- `gps raw hex on | off` mirrors every received UART chunk as hex, including
  binary frames.
- `gps raw send <hex bytes>` writes exact bytes to the receiver UART without
  sentence or CASIC framing. For example, `gps raw send 24 50 43 41 53 04 01`.
- `gps raw` reports both raw modes.
- `gps send <body>` sends a raw sentence, for example `gps send PCAS12,10`.
- `gps binary <class hex> <id hex> [payload bytes]` sends a CASIC binary frame.
  The console accepts up to 256-byte lines and 64 arguments, enough for a 56-byte AID-INI frame.
- `gps config` lists the binary configuration status.
- `gps aid` sends an assisted-start hint.
- `gps sats [on | off]` enables or disables GSV/GSA capture; `gps sats` prints
  in-view/used counts, DOP, and each decoded satellite.
- `gps platform 0..4` saves the dynamic model (`0` do not send, `1` portable,
  `2` stationary, `3` pedestrian, `4` vehicle) and reloads the receiver. The
  dyModel/PCAS11 effect is hardware-tuning-pending.
- `gps monhw` polls the CASIC MON-HW snapshot and waits up to 1.5 s for the
  reply, then prints the decoded and raw response. The offsets follow CASIC
  2.14.2; the decode is hardware-confirmed on the AT6668, while the meaning of
  individual status bits still needs a qualified antenna test.
- `gps power on | off` drives the external 5V rail.

## time

- `time status` reports the UTC wall-clock estimate, the selected time zone,
  the effective zone and its source (`manual`, `gps`, or `utc`), the last
  GPS-derived zone, local civil time with its current offset, uncertainty,
  source, RTC capability, and NVS write count.
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

## bt

- `bt scan [seconds | all [seconds] | stop]` sniffs advertisements.
- `bt explore <addr> [pair <mode>] [keep] | read | stop [keep]` walks a peer's
  GATT.
- `bt pair yes | no | key <6 digits>` answers a pairing prompt.
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
