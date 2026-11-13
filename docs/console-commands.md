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
| `boot` | `boot timing` prints the last boot timeline, its mark deltas, and the cycle-counter reset estimate. See [boot timing](boot-timing.md). |
| `status` | State, targets, uptime, heap, memory pressure, battery, reset reason. `memory_pressure` is `normal`, `amber` (WebUI server paused) or `red` (WebUI and MQTT client paused); see [WebUI memory pressure](webui.md#memory-pressure). |
| `crash` | `crash [last]` prints the retained previous-boot crash record. See [crash diagnostics](crash-diagnostics.md). |
| `power` | `stats`, or `log <seconds>` / `log off` for a CSV power log. |
| `perf` | `tasks`, `heap`, or `lvgl [overlay on\|off]`. |
| `heap` | `heap trace start [records]`, `stop`, or `dump`: ESP-IDF standalone heap tracing in leak mode, debug images only. `start` takes 1 to 1000 records (default 100) from internal RAM and refuses when the largest block cannot hold twice that. |
| `gps` | GPS status and control, see below. |
| `motion` | `status` for the motion source, or `scale [0.25-4.0]` to calibrate it. |
| `time` | `status` reports wall-clock validity and source; `flush` persists it. |
| `settings` | `list`, `get <name>`, `set <name> <value>`. |
| `companion` | `start` (open the 60 s pairing window), `stop`, `status`, `reidentify` (rotate the LE address and drop the companion bond), or `password set <pw>`, `clear`, `status`. |
| `ui` | `audit`, `page`, or `back` for UI diagnostics and navigation. |
| `cameras` | `list` saved cameras, or `status` for the active targets. |
| `connect` | `connect [index]`. No index uses the multi-connect selection. |
| `pair` | `pair <scan-index>` pairs a live scan result through the UI handler. |
| `delete` | `delete <index>` or `delete all` removes saved cameras and verifies persistence. |
| `multiconnect` | `list`, `select <index>`, `deselect <index>`, or `clear`. |
| `disconnect` | Disconnect all cameras. |
| `mqtt` | `status`, `connect`, `disconnect`, or `discovery clear`; MQTT broker control. |
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

`perf tasks` takes its two 24-entry snapshots from PSRAM when the board has it
and falls back to internal heap otherwise. If neither allocation succeeds it
prints `not enough heap for the task snapshot` instead of growing the console
stack.

### Pairing a camera that is already saved

Pairing is refused when the selected scan result is a camera the saved list
already holds. The device shows an "Already saved" box that has to be
dismissed, and no connect is started. The refusal lives in
`UI::beginPairing()`, the single entry point for "the user asked to pair this
scan result", so the Scan page row gets it without the check being written
twice. The console `pair <scan-index>` command routes through the same
`UI::beginPairing()`, which
is why it inherits the refusal with no duplicated logic.

The check is identity, not the saved index key. The index is keyed on the BLE
address, and a Fujifilm Secure body advertises a resolvable private address
that changes with every pairing, so a second pairing would add a second record
for one camera instead of replacing the first. `sameSavedIdentity()` matches on
the vendor type plus the address, and for Fujifilm Secure only, falls back to
the advertised name. Every other vendor keeps a stable address, so a second
body of the same model is still pairable. To pair a saved camera again, delete
it first with the Delete page.

Workflow requests are queued to the UI task. A bounded wait can report a
pending, unknown outcome if the UI does not complete in time; the operation may
still execute later. Do not blindly retry a timed-out delete or pairing request.

### UI workflow commands

- `cameras list | status` lists saved cameras or reports active targets. Each
  row carries `saved` and `saved_index`; `saved_index` is the value `connect`
  takes. Scan-list indices are a different sequence, so a saved scan result
  must be connected through its `saved_index`, not its scan position.
- `connect [index]` connects one saved camera, or the selected multi-connect set.
- `disconnect` disconnects all cameras.
- `pair <scan-index>` pairs a live scan result through `UI::beginPairing()`.
- `delete <index> | all` removes saved cameras and checks persistence.
- `multiconnect list | select <index> | deselect <index> | clear` edits the
  saved-camera selection used by `connect` without an index.
- `ui audit | page | back` audits or navigates the display UI.
- `interval start | stop | status` controls the Timer page on display builds.
- `bulb start | stop | status` controls the Bulb page on display builds.
- `display status | mode gui | console | brightness <value>` controls display
  state on display builds.

Workflow commands print `completed: <verb>` when the UI accepts the request, or
`failed: <verb>: <reason>` and return non-zero when it rejects it. A bounded
wait prints `pending` when the result is unknown, so do not blindly retry a
timed-out destructive request.

On the display-less Waveshare ESP32-S3-ETH, `status` reports battery level and
voltage as unknown (`-1`) and current as unavailable (`0`). It never infers USB
or optional PoE power from Ethernet link state because the optional PoE HAT has
no software-readable presence or negotiation signal.

## boot

`boot timing` prints the retained boot timeline. Each mark has `at_ms` from
`app_main` and `delta_ms` from the previous mark in time order. The
`reset_to_app_ms` value comes from the free-running CPU cycle counter and
includes the ROM bootloader and the PSRAM boot init that happen before the
firmware timeline starts. Use it to compare boots without a host-side
timestamp.

The same timeline is logged once as `boot: timeline …` after the first UI frame
or the first headless service pass. The command is useful when the log line
scrolled past or when a later mark, such as a WiFi reconnect, needs a fresh
reading. See [boot timing](boot-timing.md) for the full mark list, the measured
StickS3 baseline, and the PSRAM memory-test build option.

## crash

`status` and the crash record name reset reasons as `poweron`, `software`,
`panic`, `int_wdt`, `task_wdt`, `other_wdt`, `brownout`, `usb_uart_chip_reset`,
`jtag`, `power_glitch` or `cpu_lockup`.


`crash` and `crash last` print the record retained across the last reset:

```text
crash.summary: reset=panic task=mqtt uptime=312s heap_free=13543 heap_largest=12788 clean=no
crash.reset: panic
crash.task: mqtt
crash.uptime: 312
crash.heap_free: 13543
crash.heap_largest: 12788
crash.last_log: MQTT client start failed, attempt 10.
```

When an allocation failure caused the abort, the summary ends with
`alloc_fail=<size> caps=<caps> in=<task> fn=<function>` and four more lines
follow:

```text
crash.alloc_size: 9216
crash.alloc_caps: 0x00001800
crash.alloc_task: mqtt
crash.alloc_function: heap_caps_malloc
```

The firmware already prints the same block once at boot when the previous run
ended unclean. The record is consumed after that print, so `crash` reports
`crash.none: true` until a later reset produces a new one. The retained lines
are ANSI-normalized before storage, so colour escapes never nest across boots.
The task and log tail are the last normal snapshot, not a guaranteed panic task.
See [crash diagnostics](crash-diagnostics.md).

## settings

- `settings list` prints every setting as `key: value`.
- `settings get <name>` prints the name, type, value, and when a change applies.
- `settings set <name> <value>` saves a setting. Values are range checked and
  rejected when the board does not support the setting, for example
  `SD_GPX` on a board with no SD card slot.

Saving a setting is not the same as applying it. Settings read on every use take
effect at once. Settings the UI caches when it starts do not. `settings get`
reports which of the two a setting is: immediately, on next connect, or on
reboot. `settings set timezone Europe/Berlin` applies the zone immediately and
accepts a table index, an IANA name, a single-word roller label, or `auto`. The
`auto` mode is reported by `time status` as `timezone_source: gps` while a
derived zone is fresh and `timezone_source: utc` after the 30-minute fallback or
immediately when a fresh fix is outside the 30-degree nearest-zone radius.
See the [settings and controls reference](settings-and-controls.md) for every
setting, its default, and when it applies.

## companion

- `companion start` enables Companion mode if it is off, starts the BLE
  service, and opens the 60 s pairing window. It prints the window line only
  after the live service reports enabled; a failed start returns an error and
  leaves the persisted setting off.
- `companion stop` persists the setting off and stops the live service
  synchronously.
- `companion reidentify` is the recovery path for a host that keeps using a
  stale GATT handle map. It stops advertising, generates/applies/persists a new
  random static LE address, then disconnects the companion link, deletes only
  the companion bond (camera bonds are untouched), and opens the 60 s pairing
  window. A rotation or NVS failure restores the previous advertising state and
  keeps the link and bond. The persisted address is restored by
  `CompanionGatt::init()` and `CompanionGatt::enable()` before advertising on
  later boots, so the host keeps the bond it creates after the rotation. The
  host must pair again once.
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
  companion session and revokes any authenticated connection. It also
  re-evaluates the WebUI credential gate: clearing the password closes the
  HTTPS listener on the next supervisor tick.

## wifi

`wifi status` reports the station state and the last lifecycle outcome:

| Key | Meaning |
| :--- | :--- |
| `enabled` | Saved Wi-Fi setting. |
| `state` | `disabled`, `idle`, `connecting`, or `connected`. |
| `ssid`, `psk` | Saved network, with the passphrase reported only as `set` or `unset`. |
| `bssid`, `channel` | Access point identity once associated. |
| `rssi` | Signal in dBm while connected, otherwise `unavailable`. |
| `ip` | Lease address, `none` before DHCP. |
| `attempts` | Failed attempts in the current retry cycle. |
| `retry_ms` | Backoff before the next attempt, without the random jitter. |
| `disconnect_reason` | Raw `WIFI_REASON_*` value from the last disconnect. |
| `stopped` | `yes` after a terminal auth failure or an exhausted attempt bound; `wifi connect` clears it. |
| `current`, `estimated_runtime` | Battery drain and runtime estimate when the gauge exposes current. |

`wifi set ssid <ssid>` and `wifi set psk <psk>` write the credentials.
`wifi enable` and `wifi disable` persist the setting and start or stop the
station. `wifi connect` and `wifi disconnect` act immediately, and
`wifi forget` clears the SSID and passphrase.

## gps

- `gps` with no argument prints enabled, fix, satellites, lat, lon, alt, age,
  date, time, and error counts. `fix` is `yes` only while the position, the GGA
  quality, and a location younger than the 30 s freshness bound all hold; it
  turns `no` at 30 s even when the receiver still reports a quality. It also
  reports the receiver state behind the fix:
  `receiver` (`detecting`, `present` or `absent`), `baud` (the baud the
  autobaud ladder locked, or the configured fixed baud, 0 when none),
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
- `gps raw hex on | off` mirrors every received UART chunk as hex, including
  binary frames.
- `gps raw send <hex bytes>` writes exact bytes to the receiver UART without
  sentence or CASIC framing. For example, `gps raw send 24 50 43 41 53 04 01`.
- `gps raw` reports both raw modes.
- `gps send <body>` sends a raw sentence, for example `gps send PCAS12,10`.
- `gps binary <class hex> <id hex> [payload bytes]` sends a CASIC binary frame.
  The console accepts up to 256-byte lines and 64 arguments, enough for a 56-byte AID-INI frame.
- `gps config` lists the binary configuration status.
- `gps aid` sends an assisted-start (AID-INI) hint.
- `gps sats [on | off]` enables or disables GSV/GSA capture; `gps sats` prints
  in-view/used counts, DOP, and each decoded satellite.
- `gps platform 0..4` saves the dynamic model (`0` do not send, `1` portable,
  `2` stationary, `3` pedestrian, `4` vehicle) and reloads the receiver. The
  dyModel/PCAS11 effect is hardware-tuning-pending.
- `gps monhw` polls the CASIC MON-HW snapshot and waits up to 1.5 s for the
  reply, then prints the decoded and raw response. The offsets follow CASIC
  2.14.2; the decode is hardware-confirmed on the AT6668, while the meaning of
  individual status bits still needs a qualified antenna test.
- `gps power on | off` drives the external 5V rail, for rail-cut experiments.

## time

- `time status` reports the UTC wall-clock estimate, the selected time zone,
  the effective zone and its source (`manual`, `gps`, or `utc`), the last
  GPS-derived zone, local civil time with its current offset, uncertainty,
  source, RTC capability, and NVS write count.
- `time flush` persists the current estimate before a planned restart or power
  removal.

Calendar time is retained across power loss by the backed RTC on StickC,
StickC-Plus, and Core2 when its backup supply is healthy. StickS3 has no
battery-backed calendar RTC, so it restores the last NVS value with an explicit
uncertainty penalty until GPS, NTP, or a companion supplies a fresh sample.

On a board where M5Unified finds no calendar RTC chip, such as StickS3 or Core,
a soft reset keeps the running clock. After `esp_restart()`, an OTA restart, a
panic, a watchdog reset or a USB reset, `time status` keeps the previous source
and its uncertainty grows by the elapsed time at 100 ppm plus one second. A
power-on, brownout or unknown reset still restores the NVS value. Boards with a
calendar RTC chip still seed from it on every boot.

## imu

- `imu status` performs a read-only probe: it reports the persisted opt-in,
  detected IMU type, update result, and independent accelerometer/gyroscope
  reads with values. It performs no NVS writes and does not access LVGL.
- `imu scale` prints the gesture amplitude calibration scale and
  `imu scale <0.25-4.0>` sets it. It multiplies the tap and shake thresholds on
  top of the per-sensor gain, so a cased or strapped device can be tuned without
  a reflash. It is runtime only and is not persisted: a tuning session is one
  USB session. Available on display builds.

## motion

- `motion status` reports the IMU motion source. It performs no NVS writes.

  | Field | Meaning |
  | :--- | :--- |
  | `backend` | `none`, `software`, `bmi270-motion` or `mpu6886-wom`. |
  | `armed` | Whether a backend is running. |
  | `state` | `inactive`, `moving` or `stationary`. |
  | `wake` | `interrupt` when a light-sleep wake source is held, `polling` otherwise. |
  | `pin` | The IMU interrupt line: `asserted` or `idle`. |
  | `edges` | Polls at which the line was found asserted since arming. |
  | `interrupts` | Motion state transitions the backend has reported. |
  | `bus_retries` | IMU bus transactions retried after a first failure. |
  | `pmic_retries` | M5PM1 transactions retried after a first failure. |
  | `scale` | The software backend's calibration multiplier. |
  | `threshold` | The resulting slope threshold in g. |

  On the M5StickS3 the BMI270 interrupt never reaches the SoC, so `pin` and
  `edges` are read from the M5PM1: `pin` from the GPIO4 input level and `edges`
  from the latched GPIO4 interrupt flag. `edges` counts polls at which the line
  was asserted, not hardware edges, because a latched flag read once per second
  cannot distinguish one assertion from a thousand. That is enough to tell a
  line that never goes quiet from one that does, which is what it is for.

- `motion scale [value]` reads or sets the software backend's calibration
  multiplier, clamped to 0.25 through 4.0. Accelerometers differ in noise floor
  between parts and a cased device damps differently from a bare board, so the
  shipped threshold is a starting point. The hardware engines threshold inside
  the chip and ignore this.

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

## heap

The debug images build ESP-IDF standalone heap tracing in. `heap trace start`
allocates the record buffer and traces in leak mode, so a record is dropped
again when its block is freed. `stop` ends the trace and `dump` prints what is
left: each outstanding allocation with its size and caller addresses. Decode
the addresses with `xtensa-esp32-elf-addr2line -e firmware.elf` (or the S3
toolchain). A release image answers
`error: heap tracing is not enabled in this build`.

```
> heap trace start 200
heap_trace: started, 200 records, <bytes> bytes
> heap trace stop
heap_trace: stopped, 3 records held
> heap trace dump
```

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

- [Boot timing](boot-timing.md)
- [Settings and controls reference](settings-and-controls.md)
- [Supported hardware](supported-hardware.md)
- [UI walkthrough](ui-walkthrough.md)
