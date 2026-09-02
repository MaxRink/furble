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
  assisted start mode) and `assist_cache`. The GPS Data page shows a two row
  summary of the same state; this is where all of it is readable.
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

## Workflow coverage

Every workflow the on-device menus offer is scriptable here, through the same
production code the menus run. A console verb never carries its own copy of a
UI behaviour: it parses, gates, and hands one `UI::Request` to the UI task,
which then runs exactly the handler the button click runs. So the pairing
prompt, the registration gate, the save on success and the shutter release on
stop all behave identically whichever way the workflow is driven.

| UI workflow | Console verb | State |
| :--- | :--- | :--- |
| Scan for cameras | `scan start` / `scan stop` / `scan list` | existing |
| Scan page, tap a result to pair | `pair <scan index>` | added |
| Answer the pairing prompt | `bt pair yes \| no \| key <6 digits>` | existing |
| Saved camera list | `cameras list` | existing, now flags every row selected |
| Scan results, saved or not | `scan list` | existing, now flags every row saved and selected |
| Connect page, tap a saved camera | `connect <index>` | existing |
| Connect page, multi-select checkboxes | `multiconnect select \| deselect <index>` | added |
| Connect page, the remembered set | `multiconnect list \| clear` | added |
| Multi-connect on or off | `settings set multiconnect on \| off` | existing |
| Connect page, the `Connect N` button | `connect` with no index | existing |
| Delete page, tap a saved camera | `delete <index>` | added |
| Forget every saved camera | `delete all` | added, no menu equivalent |
| Cameras page, per-target link state | `cameras status` | existing |
| Remote shutter | `shutter press \| release \| hold <ms>` | existing |
| Remote focus | `focus press \| release` | existing |
| Bulb page, start and stop an exposure | `bulb start \| stop` | added |
| Bulb page, exposure state and duration | `bulb status` | added |
| Timer page, start and stop the intervalometer | `interval start \| stop` | added |
| Timer page, run state and configured spinners | `interval status` | added |
| Disconnect | `disconnect` | existing |
| IR shutter | `ir fire [protocol]` | existing |
| Level page readout | `imu status` | existing |
| GPS pages | `gps ...`, see below | existing |
| Display page, brightness | `display brightness <value>` | added, applies live |
| Display page, values and usable range | `display status` | added |
| Display page, inactivity and screen off | `settings set inactivity \| display_off` | existing, applies on reboot |
| Display mode | `display mode gui \| console` | existing as `settings set display_mode` |
| Off menu entry | `power off` | added |
| Battery page | `status`, `power stats` | existing |
| Feedback test tones | `feedback test <event>` | existing |
| Every other Settings page control | `settings get \| set <name> <value>` | existing |
| Page layout dump | `ui audit` | existing |
| Current page name | `ui page` | added |
| Header back button | `ui back` | added |
| Forget the multi-connect set | `multiconnect clear` | added |
| Boot autoconnect | `settings set autoconnect on \| off` | existing |
| Restart | `reboot` | existing |

Four workflows have no console verb, for reasons that are not worth working
around:

- **Navigate to a named page.** The name to page tables live only in the
  simulator build. Adding a second navigation mechanism to firmware to serve
  the console is not worth it, so `ui page` reads and `ui back` steps out, and
  nothing jumps to an arbitrary page.
- **Touch calibration.** The calibration page needs real touches at real
  screen coordinates.
- **Timer and Bulb configuration.** Count, delay, shutter, wait and bulb
  duration live in struct settings that only the LVGL rollers write, and the
  UI reads them once at construction. `interval status` and `bulb status`
  report the live values so a script can at least assert them.
- **Settings export and import to SD.** `settings list`, `settings set` and
  `provision` already carry the same data over the console.

### Answering a workflow verb

Every verb below ends its answer with one machine readable outcome line,
`result: <token>`, so a host script never has to match on prose:

| Token | Meaning |
| :--- | :--- |
| `ok` | the workflow ran |
| `no_scan_result` | `pair` was given an index that names no scan result |
| `no_saved_camera` | an index named no saved camera |
| `not_running` | `interval stop` or `bulb stop` with no run in progress |
| `no_button` | the page holding that control has not been built yet |
| `range` | `display brightness` outside the board's usable range |
| `selection_full` | the multi-connect set already holds `MULTISELECT_MAX` names |

The refusals are printed by the UI task, which owns the list or the widget the
verb addresses, so these verbs wait for the answer before returning to the
prompt rather than reporting a queue depth. The token is also the exit status:
`ok` returns 0 and every other token returns 1, so a script can branch on the
return code without parsing at all.

The wait is a bounded 100 ms, which is twenty times the UI loop's queue drain
interval. A UI task still busy after it, with a `delete all` sweep or a scan
start, answers `error: no answer from the ui task` and a non-zero status rather
than an outcome it never gave.

### Pairing a camera

`pair` is the Scan page row click. The index is a row of the most recent
`scan list`, not of `cameras list`. Only the UI task knows whether the
connectable list currently holds scan results, so an index that names nothing
comes back as `error: no scan result at index N` with `result:
no_scan_result`. That is also the answer when the list holds saved cameras,
because `connect` is their verb.

A console `scan start` applies the same duty and timeout settings the Scan page
applies, so it ends after `scan_timeout` seconds like any other scan (zero
scans until `scan stop`). The results stay pairable after it ends, exactly as
the Scan page keeps its rows clickable. The usual sequence is:

```
log * warn
scan start
scan list
pair 0
```

`scan list` marks every row `camera<N>.saved: true|false`. That is where the
distinction earns its keep: the connectable list carries saved cameras and scan
results in the same sequence, and a scan can rediscover a camera the device
already knows, so the flag is how a script tells whether a row wants `pair` or
`connect`. `cameras list` reloads the saved list first, so every row it prints
is saved by construction and the flag reads `true` throughout.

Both also print `camera<N>.selected`, the row's place in the remembered
multi-connect set.

Cameras which use a pairing confirmation answer the prompt with
`bt pair yes | no | key <6 digits>`. Fujifilm Secure and Ricoh raise it during
registration, so a full unattended onboarding is `scan start`, `pair <n>`, then
`bt pair yes` when the prompt appears. The camera itself may also need a
confirmation on its own body. On success the camera is saved, exactly as the
Scan page saves it, and `cameras list` shows it with `saved: true`.

### Multi-connect

The remembered set is stored by camera name, so an index is resolved against
the saved list on the UI task. `multiconnect select <index>` ticks one
checkbox and persists the whole set, the same pair of steps the Connect page
takes when its `Connect N` button is pressed. It reports `selected:` from what
the store actually took, not from the flag it just set: the set holds at most
eight names and a ninth select is refused with `result: selection_full` rather
than silently dropped.

Names are stored truncated to 15 characters, so two saved cameras sharing that
prefix are indistinguishable to the remembered set. That is a limitation of the
stored format, not of these verbs.

`multiconnect list` prints the remembered names and whether the feature is
enabled. `multiconnect clear` runs on the UI task so it empties the loaded
active flags and the drawn checkboxes along with the store; clearing only the
store would let the next Connect press write the whole set straight back.
`connect` with no index then connects that set.

### Timer and bulb

Both fire the shutter, so both refuse to start without an active connection.
All four verbs send the real button event rather than calling a start or stop
helper, because both buttons carry behaviour beyond it.

`interval start` and `bulb start` start the run and then navigate to its run
page, in that order. That ordering matters for the bulb: leaving the Bulb run
page stops the exposure, so a start that skipped the navigation would be
cancelled by the next page change.

`interval stop` and `bulb stop` release the shutter and click the header back
button, which returns to whatever page the UI was on. Both refuse with
`result: not_running` when no run is in progress: the synthetic click would
otherwise release a shutter a script is deliberately holding and navigate away,
and the Bulb Stop button restarts a finished exposure rather than stopping it.

`interval status` reports `state`, `remaining` (`65535` for an infinite count),
`next_ms`, and the configured `count`, `count_unit`, `delay_ms`, `shutter_ms`
and `wait_ms`. `bulb status` reports `state`, `remaining_ms` and `duration_ms`.
Both countdowns read zero once their deadline has passed.

### Display

`display brightness <value>` applies the value and then persists it, which is
the pair of calls the Display page slider makes; `settings set brightness` only
persists, so it needs a reboot. The slider's range is narrower than 0-255 and
is a board fact: 32 to 240 on most panels, 48 to 240 on the StickC and
StickC-Plus. A value below the minimum leaves a black panel that needs a
reflash to undo, so it is refused with `result: range` rather than clamped
silently. `display status` reports `brightness_min` and `brightness_max` so a
script can pick a value this board accepts.

Any value in the range is accepted and applied verbatim, but the slider itself
only ever produces multiples of `brightness_step`, which `display status`
reports alongside the range. So `display brightness 100` lights the panel at
100 and the Display page then draws its slider at 96, the nearest step it can
represent. Use a multiple of the step when a script also asserts what the page
shows.

`display mode gui | console` is the same path `settings set display_mode`
takes, including the live UI request. `ui page` answers one whitespace-free
token: the two run pages carry a trailing space in the menu, which the answer
trims. `ui back` is the header back button plus the two things pressing it
implies on a real device: it force-enables the button and returns the input to
MENU mode.

### Headless builds

The display-less Waveshare ESP32-S3-ETH carries the console but no UI task, so
the verbs which drive an LVGL page or resolve an index against a list it owns
answer `not supported in this build`: `pair`, `interval`, `bulb`, `display`,
`power off`, all three `ui` subcommands, and `multiconnect select | deselect |
clear`. `cameras`, `connect`, `delete`, `disconnect`, `scan`, `shutter`,
`focus` and `multiconnect list` work there. Pairing needs the save on a
successful registration, which lives on the UI task; without it the headless
build would connect and then forget the camera.

### Deleting a camera

`delete <index>` is the Delete page row click, and it removes the stored entry
and the BLE bond together. `delete all` sweeps the whole saved list, which the
menus offer no button for. Both print one `deleted: <name>` line per camera and
a final `count:`.

The headless build answers `delete` too, and answers it the same way: the same
`deleted:` and `count:` lines, the same `error: no saved camera at index N`
refusal, and the same `result:` token. The one thing it does not do is refresh
a Delete page, because it has none. The whole sweep runs as one request, so a
`delete all` over a large saved list holds the UI task for as many NVS commits
and bond removals as there are cameras.

## imu

- `imu status` performs a read-only probe: it reports the persisted opt-in,
  detected IMU type, update result, and independent accelerometer/gyroscope
  reads with values. It performs no NVS writes and does not access LVGL.

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
- `bt pair yes | no | key <6 digits>` answers a pairing prompt, including the
  one a `pair <scan index>` onboarding raises.
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
