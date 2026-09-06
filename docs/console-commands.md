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

The OTA application image is flashed at `0x20000`. This is also enforced for
no-build uploads, so a previously built `firmware.bin` can be uploaded without
falling back to PlatformIO's old `0x10000` default. The generated
`.pio/build/<env>/flash_args` file is the authoritative complete image map.

Log output shares the port, so `log * warn` is usually the first thing worth
typing. Every command prints one fact per line as `key: value`, so a host script
can parse it with a split on the first colon.

The console distorts power measurements, so do not take power numbers from a
build that contains it. On the ESP32 boards the console holds an APB frequency
lock for its lifetime. On the M5StickS3 no lock is needed.

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
| `flash` | `prepare` disarms the StickS3 PMIC watchdog and verifies download recovery; `cancel` restores it. |
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
  rejected when the board does not support the setting, for example
  `SD_GPX` on a board with no SD card slot.

Saving a setting is not the same as applying it. Settings read on every use take
effect at once. Settings the UI caches when it starts do not. `settings get`
reports which of the two a setting is: immediately, on next connect, or on
reboot. See the [settings and controls reference](settings-and-controls.md) for
every setting, its default, and when it applies.

## gps

- `gps` with no argument prints enabled, fix, satellites, lat, lon, alt, age,
  date, time, and error counts, then the receiver state behind them:
  `degraded` and `retries` for the power cycle health, `source` (`uart`,
  `companion` or `none`), `cycle` (the power cycle state), `policy`
  (`always_on`, `standby` or `rail_cycle`), `duty` (standby seconds), `rate`
  (configured fix interval in ms, 0 for the receiver default), `sentence_age`
  (ms since the receiver last spoke, or `none` if it never has), `assist` (the
  assisted start mode) and `assist_cache`. It then reports the fix hold state:
  `fix_state` (`live`, `held` or `none`), `hold` (the configured bound in ms,
  0 when fix hold is off) and `hold_remaining` (ms left on a held fix). The GPS
  Data page shows a two row summary of the same state; this is where all of it
  is readable.
- `gps on | off` drives the GPS setting and reloads the receiver.
- `gps raw on | off` mirrors incoming NMEA to the console.
- `gps send <body>` sends a raw sentence, for example `gps send PCAS12,10`.
- `gps binary <class hex> <id hex> [payload bytes]` sends a CASIC binary frame.
- `gps config` lists the binary configuration status.
- `gps aid` sends an assisted-start (AID-INI) hint.
- `gps power on | off` drives the external 5V rail, for rail-cut experiments.

## time

- `time status` reports the UTC wall-clock estimate, uncertainty, source, RTC
  capability, and NVS write count.
- `time flush` persists the current estimate before a planned restart or power
  removal.

Calendar time is retained across power loss by the backed RTC on StickC,
StickC-Plus, and Core2 when its backup supply is healthy. StickS3 has no
battery-backed calendar RTC, so it restores the last NVS value with an explicit
uncertainty penalty until GPS, NTP, or a companion supplies a fresh sample.

## imu

- `imu status` performs a read-only probe: it reports the persisted opt-in,
  detected IMU type, update result, and independent accelerometer/gyroscope
  reads with values. It performs no NVS writes and does not access LVGL.
- `imu scale` prints the gesture amplitude calibration scale and
  `imu scale <0.25-4.0>` sets it. It multiplies the tap and shake thresholds on
  top of the per-sensor gain, so a cased or strapped device can be tuned without
  a reflash. It is runtime only and is not persisted: a tuning session is one
  USB session. Available on display builds.

## shutter and focus

- `shutter press` fires and `shutter release` ends. `shutter hold <ms>` fires,
  waits, and releases, up to 60000 ms.
- `focus press` half-presses focus and `focus release` ends. Ricoh cameras do
  not support this action over BLE, so both commands are no-ops. Their shutter
  command uses `OperationRequest {0x01, 0x01}` for capture with autofocus.

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

The journal is a fixed 32-event ring on boards without PSRAM and a 128-event
ring on ESP32-S3 builds configured for PSRAM. Recording is silent and bounded.
Live console streaming drains at most eight events per console turn, while `dump`
prints only the requested newest records. Events use the monotonic millisecond
clock and typed fields, including requested and resolved address identities,
GAP reason names, scan callback owner and generation, physical/logical scan
state, connection parameters before and after an update, security state and
key size, GATT service and characteristic UUIDs, operation kind, CCCD value,
response mode, payload length, result, and bounded duration. Use
`bt journal dump 128` for a complete bounded snapshot, then copy the console
output into a bug report. The dump reports overwritten records. The compact
record ring consumes at most 8 KiB of internal memory on non-S3 boards. On S3
with `CONFIG_SPIRAM`, the 128-record ring requests PSRAM, with a 32-record
internal fallback when PSRAM is unavailable at runtime. The journal is RAM-only
and never changes bonds or NVS.

## debug

`debug <target>` dumps internal state for one subsystem: `control`,
`camera [idx]`, `ble`, `heap`, `tasks`, `power`, `gps`, `settings`, or `all`.

## flash preflight

The StickS3 PMIC watchdog continues running while the ESP32 ROM receives a
firmware image. If its configured timeout is shorter than a slow upload, it can
reset the USB device in the middle of the transfer. On a responsive developer
build, run the guarded preflight and let it start PlatformIO only after the PMIC
confirms both safety conditions:

```sh
python3 tools/flash_prepare.py --port /dev/cu.usbmodemXXXX \
  --env m5stick-s3-debug
```

To validate the handshake without invoking PlatformIO, use
`--preflight-only`. The older `--dry-run` spelling is retained as an alias.
Both forms send `flash cancel` after a successful prepare and return success
only after confirming that the watchdog is armed. Download recovery remains
available by design, including after cancel, so a wedged device can still be
rescued. If watchdog restoration cannot be confirmed, the command returns a
failure and prints the safe manual recovery procedure.

The preflight accepts no credentials and prints no secret data. If it cannot
obtain all three acknowledgements (`flash.ready`, a disabled watchdog, and an
unlocked download path), it refuses to flash. Use the physical long-press
recovery procedure in the README when the running application is wedged. A
missing Python dependency or an unopened port is reported separately and does
not imply a retained PMIC download lock. If an upload is cancelled after
`flash prepare`, reconnect to the console and run `flash cancel`, or reboot so
normal startup re-arms the watchdog.

The helper invokes the normal PlatformIO upload target from the checkout that
contains the script and supplies the required development build identity. Do
not substitute `pio run -t nobuild -t upload`: PlatformIO build artifacts are
per-checkout and can otherwise flash a stale revision from another worktree.
If PlatformIO cannot be started or the upload exits unsuccessfully after the
handshake, the helper attempts `flash cancel` and reports whether the PMIC
watchdog was restored. If automatic restoration fails, keep the device powered
and run `flash cancel` manually.

## Related references

- [Settings and controls reference](settings-and-controls.md)
- [Supported hardware](supported-hardware.md)
- [UI walkthrough](ui-walkthrough.md)
