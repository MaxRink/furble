# furble settings and controls reference

This is the complete reference for every setting and every button input mode in
furble. It has two parts:

- Part 1 lists every setting, grouped to mirror the on-device Settings menu.
- Part 2 documents the physical buttons per board and how you navigate, shoot,
  and lock the shutter.

Everything here was taken from the current firmware source. Where a value is a
default, it is the value the firmware writes on first boot, so it matches the
shipped behaviour.

For a friendly walkthrough of first use, pairing, GPS tagging, the
intervalometer, and bulb, see the Usage section of the top level
[README](../README.md). This document is the exhaustive reference that sits
underneath it.

For a screen by screen tour of the interface with a screenshot of every page,
see the [UI walkthrough](ui-walkthrough.md).

## Quick reference

Top level menu: `Connect`, `Scan`, `Delete`, `Settings`, `Off`. An `IR` entry
also appears at the top level when Infrared is supported and enabled.

Two-button navigation (the default and the only navigation model):

- Left button: move focus to the previous item. A short click on the shutter
  page is Back. A long press (about 0.8 s) is Back from anywhere.
- Middle button: select, or press the shutter on the remote page.
- Right button: move focus to the next item, or hold focus on the remote page.

Which physical button is which:

| Board family | Left (prev / back) | Middle (select / shutter) | Right (next / focus) |
| :--- | :--- | :--- | :--- |
| M5StickC, StickC Plus | Power (PEK) button | BtnA (front) | BtnB (top) |
| M5StickC Plus2, StickS3 | Side power button | BtnA (front) | BtnB (top) |
| M5Stack Core, Core2, Tough | BtnA (left) | BtnB (middle) | BtnC (right) |

When a dialog is open, it temporarily owns the physical navigation and shutter
inputs. Closing it returns control to the page underneath; with stacked dialogs,
input remains with the topmost dialog.

Settings menu at a glance (in on-screen order): Display, Features, Infrared,
Sensors, GPS, Time, Timer, Theme, Text size, Bluetooth, About, Power, Feedback,
Diagnostics, Storage.

## Part 1: Settings reference

### How to read the tables

- **Setting** is the on-screen label.
- **Default** is what the firmware writes on first boot.
- **Values** lists the allowed choices or the range.
- **Applies** says when a saved change takes effect. `Now` means it takes effect
  without a reboot. `Restart` means the value is read at startup, so it needs a
  reboot or a Restart button. `Next boot` and `Next connection` are noted where
  they apply. This column follows the firmware's own `appliesImmediately` map,
  which is also what the serial console reports for each setting.
- Settings marked (dangerous) can affect an active camera link or a running
  connection. Change them with care while connected.

Setting rows place a full-width wrapping name above the switch on every
panel. This can increase the page's scrollable height, including on wider
screens. It preserves the complete wording and selected
font; it is a layout contract, not a claim that every panel has been checked
on physical hardware.

Some rows are pages rather than settings (for example the Battery page or Raw
NMEA). Those are read-only status pages and are listed for completeness but have
no stored value.

### Display

Submenu: `Settings` > `Display`.

Rows leave room for focused control outlines. The page scrolls to reach controls
that do not fit; focus outlines must not cover adjacent setting names.

| Setting | Default | Values | Applies | Notes |
| :--- | :--- | :--- | :--- | :--- |
| Brightness | 128 | slider, roughly 0 to 240 | Now (live preview) | Saved when you release the slider. |
| Inactivity timeout | 60 secs | Never, 30 secs, 60 secs, 2 mins, 5 mins, 10 mins | Now | Fresh devices start at 60 secs; an existing stored value is kept. Dims and then sleeps the screen after this idle time. |
| Screen off | Off (Dim on M5Stack Core Basic) | Dim, Off, Off remote on, Dim then off (button boards). Dim, Off, Dim then off (touch boards) | Restart | Fresh devices start at Off, except the M5Stack Core Basic keeps Dim so its IP5306 boost does not cut the board. Touch boards do not offer the remote-on option. Dim then off dims at the inactivity timeout and sleeps the panel 30 s later. |
| Calibrate | n/a | action | n/a | Touch boards only. Launches touch calibration and stores the result. |
| Show Title | On | On, Off | Restart | Hiding the title frees header space on narrow stick displays. |
| Legend | Buttons | Buttons, Bottom, Off | Restart | Physical-button Stick boards only. Buttons keeps the shipped Right legend beside its button, narrowing only the rows level with that floating legend. Row width and translation update during the scroll; label sizing (including button-label wrapping) and the full layout settle wait for the gesture to end. Bottom places all three legends in the navigation band; Off hides the rendered indicators but retains physical input anchors and layout reservation. |

### Features

Submenu: `Settings` > `Features`.

| Setting | Default | Values | Applies | Notes |
| :--- | :--- | :--- | :--- | :--- |
| Button Mode | Two-button | Two-button, One-button | Now | See Part 2. The gesture hint is shown under the roller. Read live on every shutter press. |
| Auto-Connect | Off | On, Off | Now | Connect to the saved camera at boot without a menu step. |
| FauxNY | Off | On, Off | Now | Adds a fake test camera to the scan list for development. |
| Infinite-ReConnect | Off | On, Off | Now | Keep retrying a lost connection forever. |
| Reconnect Backoff | On | On, Off | Now | Space out reconnect attempts to save battery. The first retry stays quick at 2.5 s; later attempts back off to 120 s. |
| Multi-Connect | Off | On, Off | Now | Allow connecting to more than one camera at once. |
| Companion | Off | On, Off | Restart (dangerous) | Enables the companion BLE service. Connection-affecting. |
| Watchdog | On | On, Off | Restart | M5StickS3 only. Turn off before attaching a JTAG debugger. |
| Preset Picker | Off | On, Off | Restart | Adds the exposure preset stepper on the shutter page. |
| Boot screen | On | On, Off | Next boot | The startup splash. Off restores the old plain boot. |

The companion password is console/provisioning-only, not a menu item. Wire ID
47 accepts 1 to 63 UTF-8 bytes. Use `companion password clear` to restore the
default unset state, or `companion password status` to inspect set/unset without
printing the secret. Changes revoke the live password authentication. An unset
password needs no challenge; unreadable credentials deny privileged writes.

### Infrared

Submenu: `Settings` > `Infrared`. The whole submenu is hidden unless the board
has an IR LED.

| Setting | Default | Values | Applies | Notes |
| :--- | :--- | :--- | :--- | :--- |
| Infrared | Off | On, Off | Now | Enables IR shutter output. Also reveals the top level and remote `IR` entries. |
| IR Protocol | Nikon | Nikon, Sony, Canon, Canon 2s | Now | Camera maker IR code set. Canon 2s is the two-second delay variant. |

### GPS

Submenu: `Settings` > `GPS`. All rows below the GPS toggle are hidden until GPS
is enabled.

| Setting | Default | Values | Applies | Notes |
| :--- | :--- | :--- | :--- | :--- |
| GPS | Off | On, Off | Now | Master switch for the GPS receiver. |
| GPS Baud | 9600 | Auto, 9600, 115200 | Now | Auto probes the six supported rates and requires two checksummed NMEA sentences. The v1.1 AT6668 unit is expected to use 115200; live hardware verification is pending. |
| Update rate | Default | Default, 1000 ms, 500 ms, 200 ms, 100 ms | Now | How often the receiver reports a position. Default leaves the receiver alone. |
| Sentences | Default | Default, RMC+GGA | Now | RMC+GGA prunes output to the sentences furble reads. |
| Constellation | Default | Default, GPS, BDS, GPS+BDS, GLONASS, GPS+GLO, BDS+GLO, All | Now | Which satellite systems the receiver uses. |
| Power saving > Receiver | Always on | Always on, Standby (PCAS12), Rail cycling | Now | Receiver low-power policy. Rail cycling is experimental. |
| Power saving > Sleep between fixes | No standby | No standby, 5 s, 10 s, 15 s | Now | Standby interval for the PCAS12 policy. |
| Assisted start | Off | Off, Position and time | Now | Feeds the receiver a position and time hint for a faster fix. |
| Fix Hold | Off | Off, 30 s, 2 min, 10 min, 60 min | Now | Keep sending the last fix to the camera for this long after the receiver loses it, so photos taken in a tunnel or indoors still get a geotag. The held fix keeps its position and advances its clock, and the GPS Data page says how much of the window is left. The window is measured from the moment the fix is declared stale, which is 30 s after the receiver's last good reading, so a 30 s hold can hand the camera a position up to a minute old. The timestamp sent with it counts that whole time, so it is never wrong about how old the fix is. A held fix is not written to the GPX track log. Off sends nothing once the fix is lost, which is the behaviour without this setting. |
| Extrapolate | Off | On, Off | Now | While a fix is held, project the position along the last measured course and speed instead of repeating it. Needs Fix Hold, a course and speed from the receiver, and at least 2 m/s, so a stationary user is never moved. The projection stops advancing 30 seconds after the fix is lost. Experimental: it is a straight line, so it is wrong as soon as you turn. |
| GPS Data | n/a | page | n/a | Live position, satellites, speed, altitude, and time, plus the receiver fix source, the time since its last sentence, and its power cycle state. With Fix Hold on it also shows whether the fix is live or held, and how long a held fix has left. The position, altitude and time rows always report the receiver's own last reading, not the held or extrapolated values being sent to the camera. The receiver rows are not shown on the 80x160 M5StickC. |
| Assisted start | Off | Off, Position and time, Position, time and ephemeris | Now | Feeds the receiver a position and time hint for a faster fix. The ephemeris option also caches and replays receiver frames; its time to first fix benefit is hardware-tuning-pending. |
| Platform | Do not send | Do not send, Portable, Stationary, Pedestrian, Vehicle | Now | Applies the CASIC dynamic model through CFG-NAVX, with a PCAS11 fallback. The receiver effect is hardware-tuning-pending. |
| GPS Data | n/a | page | n/a | Live position, satellites, speed, altitude, and time, plus the receiver fix source, the time since its last sentence, and its power cycle state. The receiver rows are not shown on the 80x160 M5StickC. |
| GPS Satellites | n/a | page | n/a | Per satellite constellation, PRN, C/N0 and used-in-fix marker, with the in-view and used counts and PDOP, HDOP, VDOP. GSV and GSA capture runs only while the page is open. |
| Raw NMEA | n/a | page | n/a | Live receiver sentences, fix state, and error counts. Includes a Hot restart button. |
| Satellites | n/a | page | n/a | GSV/GSA detail: in-view/used counts, DOP, C/N0 and used flags. Capture runs only while this page is open. |

The receiver is set from these when GPS is enabled, and it returns to its own
defaults the next time it is powered off. Auto declares a receiver only after
two checksummed NMEA sentences; a fixed baud arms a 30-second acquisition
deadline. In both cases a silent receiver is reported as absent, the external
rail is dropped, and furble retries once after 60 seconds. The AT6668 path has
simulator coverage but has not yet been verified against a live unit.

### Time

Submenu: `Settings` > `Time`.

| Setting | Default | Values | Applies | Notes |
| :--- | :--- | :--- | :--- | :--- |
| Time zone | UTC | UTC, Auto (GPS), Europe/Berlin, Europe/London, US Eastern, US Central, US Mountain, US Pacific, Australia/Sydney, Asia/Tokyo, India, and 29 more curated zones | Now | Selects the local civil time used for display and camera clock writes. UTC keeps the previous camera clock behaviour. Auto (GPS) uses the nearest curated zone from a fresh wired fix. The page also shows local time with its current offset, plain UTC, the effective zone, and whether the zone is manual, GPS, or UTC. |

The manual selection always wins. With `Auto (GPS)` selected, the last derived
zone is used while the last fresh wired fix is at most 30 minutes old. A
candidate zone change is held for at least 5 minutes after the previous change,
so a noisy boundary fix cannot churn the camera clock. Auto falls back to UTC
after 30 minutes without a fresh fix, and immediately when a fresh fix is
outside the 30-degree nearest-zone radius. The default UTC is a manual choice,
so an existing device is never silently moved.

Auto mode uses 39 curated zone rules and 61 representative points. It is a
nearest-zone approximation, not a polygon database: borders, large countries,
and remote regions can resolve to a neighbouring rule. The table lives in
`src/FurbleTimezone.cpp`; `tools/timezone_table.py --check` validates it and
`--zone-tab` prints refresh coordinates from a tzdata `zone.tab`.

The camera clock path always keeps UTC as its source of truth. A camera whose
protocol carries a timezone field receives the standard UTC offset and a DST
flag; a camera with no timezone field receives the selected or derived local
civil time. The date and time rows on the GPS Data page stay UTC so a fix
timestamp is never ambiguous. See the vendor notes in
[docs/camera-research/parity-matrix.md](camera-research/parity-matrix.md).

### Timer (intervalometer)

Submenu: `Settings` > `Timer`. These four values are stored together as the
interval configuration.

| Setting | Default | Values | Applies | Notes |
| :--- | :--- | :--- | :--- | :--- |
| Count | 10 | 1 to 999, or infinite | Restart* | Number of frames to take. |
| Delay | 15 s | 0 to 999 in ms, s, or mins | Restart* | Time between frames. |
| Shutter | 30 ms | 0 to 999 in ms, s, or mins | Restart* | How long the shutter is held per frame. |
| Wait | 0 s | 0 to 999 in ms, s, or mins | Restart* | Initial wait before the first frame. |
| Deep Sleep | Off | On, Off | Now | On supported boards, power off between frames when the next delay is at or above the threshold. |
| Sleep Threshold | 60 s | 0 to 999 seconds | Now | Minimum delay before Deep Sleep is used. The row is hidden while Deep Sleep is off. |

*The interval blob is read when the timer page is built. In practice you set
these on the device and press Start, so the value you see is the value that
runs. Cameras that trigger with a single operation (for example Ricoh GR) ignore
the Shutter duration but still honour Count, Delay, and Wait.

Deep Sleep and Sleep Threshold appear only on boards with a timed wake source:
the M5StickS3 (M5PM1 timer) and the M5StickC Plus2 (BM8563 alarm). On other
boards the controls are hidden and the intervalometer runs awake. After a timed
wake, furble reconnects once with a 3 second settle gap and continues the
remaining frames. A failed reconnect clears the saved run and shows
`RESUME FAILED`.

### Theme

Submenu: `Settings` > `Theme`.

| Setting | Default | Values | Applies | Notes |
| :--- | :--- | :--- | :--- | :--- |
| Theme | Default | Dark, Default, Mono Furble | Restart | Press the Restart button to save and apply. |

### Text size

Submenu: `Settings` > `Text size`.

| Setting | Default | Values | Applies | Notes |
| :--- | :--- | :--- | :--- | :--- |
| Text size | Normal | Small, Normal, Large | Restart | Press the Restart button to save and apply. |

### Bluetooth

Submenu: `Settings` > `Bluetooth`.

| Setting | Default | Values | Applies | Notes |
| :--- | :--- | :--- | :--- | :--- |
| TX Power > Adaptive | Off | On, Off | Now (dangerous) | Let the radio adapt transmit power. Connection-affecting. |
| TX Power | Low (P3) | slider 0 to 2 (P3, P6, P9) | Now (dangerous) | Radio transmit power. Higher reaches further and costs battery. |
| Connection power save | Off | On, Off | Next connection | Power save on the active connection. Read when a connection is made, so set it before connecting. |
| Scan mode | Full | Full, Balanced, Low | Now | How hard the radio listens during Scan. Full finds a camera fastest. |
| Scan timeout | 60 secs | Never, 30 secs, 60 secs, 120 secs | Now | Ends a scan by itself. A fresh device starts at 60 secs so a forgotten Scan page cannot listen at full duty until it is left. An explicit Never is kept. Connect still scans at reduced initiator duty for the whole attempt. |

### About

Submenu: `Settings` > `About`. A read-only page: firmware version, device ID,
build date, IDF version, uptime, heap, and reset reason. No stored settings.
Development firmware shows `dev+g<revision>` and appends `.dirty` when the
checkout has tracked, staged, or non-ignored untracked changes. Explicit release
versions are shown unchanged.

### Power

Submenu: `Settings` > `Power`.

| Setting | Default | Values | Applies | Notes |
| :--- | :--- | :--- | :--- | :--- |
| Sleep while connected | Off (On on M5StickS3) | On, Off | Next connection (dangerous) | M5StickS3 only. Doze between BLE events while connected. Read when a connection is made, so set it before connecting. Fresh StickS3 devices start at On. |
| CPU speed | 160 MHz | 80 MHz, 160 MHz, 240 MHz | Now (dangerous) | Higher feels snappier and costs battery. |
| Battery Style | Icon | Icon, Percent, Both | Now | What the header battery indicator shows. |
| Auto off | Never | Never, 5 mins, 10 mins, 30 mins, 60 mins | Now | Power off after this idle time. Not offered on M5Stack Core Basic. |
| Low battery | None | None, Warn, Warn then off | Now | Low battery action. Not offered on M5Stack Core Basic. |
| Battery | n/a | page | n/a | Charge level, voltage, current, charging state, and runtime estimate. Rows the board cannot measure are hidden. |

### Feedback

Submenu: `Settings` > `Feedback`. The whole submenu is skipped on boards that
have no output beyond Off.

| Setting | Default | Values | Applies | Notes |
| :--- | :--- | :--- | :--- | :--- |
| Output | Off | Off, Sound, Light, Vibrate, Sound and Light (board dependent) | Restart | Only the outputs the board supports appear. Press Restart to apply. |
| Feedback Events | All on | Shutter fired, Countdown, Connect and disconnect, Low battery | Now | Per-event switches. All four ship enabled. |
| Volume | 64 | slider 0 to 255 | Now | Shown only when the chosen output includes sound. |

### Diagnostics

Submenu: `Settings` > `Diagnostics`. Read-only pages:

- Device info: chip, cores, flash, heap, uptime, reset reason.
- Battery: the same page linked from Power.
- Power state: frequency, sleep, power lock rows, tickless idle, Dump locks
  button.
- BLE: live BLE status row.

### Sensors

Submenu: `Settings` > `Sensors`.

| Setting | Default | Values | Applies | Notes |
| :--- | :--- | :--- | :--- | :--- |
| IMU | Off | On, Off | Restart | Enables the spirit level and live IMU diagnostics. The setting is stored as wire ID 46. The supported sensor must be present; press Restart after changing it. |
| Wake Gesture | Off | Off, Tap, Shake, Both | Immediately | Software IMU wake detector, on the `Gestures` page (wire ID 72). Active only when IMU is enabled. |
| Double-Tap Shutter | Off | On, Off | Immediately | A debounced double tap fires one short shutter command on an active Connected or Remote page (wire ID 73). Also on the `Gestures` page. |
| Motion Engine | Software | Auto, Software, Hardware | Restart | Chooses how motion is detected while the IMU is on. Software is the shipped default while the hardware path is unverified. Auto prefers the hardware engine the board's IMU offers and falls back to the software detector. Stored as wire ID 74. When Wake Gesture is enabled, detected motion counts as user activity and picking the device up wakes the panel. |

When enabled, `Connected` contains the **Level** page and `Settings` >
`Diagnostics` contains the live **IMU** page. The simulator injects deterministic
accelerometer and gyro samples through the same production read seam.

### Storage

Submenu: `Settings` > `Storage`. The whole submenu appears only on boards with a
supported SD card slot.

| Setting | Default | Values | Applies | Notes |
| :--- | :--- | :--- | :--- | :--- |
| GPX Logging | Off | On, Off | Now | Log a GPX track to the SD card. |
| GPX Interval | 5 | 1, 2, 5, 10, 30, 60 (seconds) | Now | Track point period. |
| Export Settings | n/a | action | n/a | Write settings to the SD card. |
| Import Settings | n/a | action | n/a | Read settings from the SD card. |
| Card Info | n/a | page | n/a | Mount state, capacity, free space. |

### MQTT (8 MB+ debug/console builds)

MQTT is available only in the documented 8 MB and 16 MB board profiles and is
off by default at runtime. It is configured with `settings set` in a debug
build:
`mqtt` is the master switch, `mqtt_uri` accepts `mqtt://host:1883` or
`mqtts://host:8883`, `mqtt_user` and `mqtt_pass` are optional broker
credentials, `mqtt_base` is the topic root (default `furble`), and `mqtt_ha`
enables Home Assistant discovery. The console also provides `mqtt status`,
`mqtt connect`, `mqtt disconnect`, and `mqtt discovery clear`.

`mqtt_ha` is read when the MQTT settings reload and cached for the owner task.
A change takes effect on the next reload, not between owner iterations.

MQTT is the largest optional network consumer. Before the esp-mqtt client is
created the firmware requires 8,192 bytes of free internal heap and a 4,096-byte
largest block for a plain `mqtt://` URI. A secure `mqtts://` URI requires
11,264 free and 7,168 largest because its worker is 6 KiB, not 3 KiB. Home
Assistant discovery is optional and requires 10,240 bytes free and a 4,096-byte
largest block. Below the matching floor the firmware logs the measured pair and
either defers the start (the owner retry limit still applies) or skips only the
retained discovery records. The broker link and the command path stay up when
discovery is skipped. The plain MQTT worker uses a 3 KiB stack and 1,536-byte
network and outbox buffers; TLS keeps a 6 KiB worker.

`mqtt://` is intentionally supported for local-broker compatibility, but it is
unencrypted and sends credentials and commands in plaintext. Use it only on a
trusted LAN; use `mqtts://` when transport confidentiality is required. The
accepted URI schemes are `mqtt://` and `mqtts://`; WebSocket MQTT is not part of
the product contract because no firmware build enables that transport.

Commands below `BASE/ID/cmd/` must be published with retain off. Retained
commands are rejected to prevent a broker replay from firing an actuator after
reconnect. A clean `mqtt disconnect` publishes retained `offline` and waits
briefly for the broker acknowledgement before teardown; it tears down on
acknowledgement or timeout. A timeout does not prove broker delivery; the last
will covers an unclean loss.

The 4 MB M5StickC, M5StickC Plus, and M5Stack Core profiles do not compile the
MQTT implementation or its settings/console capability. The headless S3
profile is eligible because it uses the documented 8 MB ESP32-S3-DevKitC-1
board.

### Settings that live outside the Settings menu

`WebUI` is an 8 MB and 16 MB profile setting with wire id 62. It defaults Off
and applies immediately. Set `web_ui` from the USB console, provisioning, or an
already authenticated WebUI. The HTTPS server also requires a loaded non-empty
companion password and enough internal RAM; below the floor the setting stays
On but the server stays closed and logs the measured budget. The TLS identity is
created at the next boot when possible; enabling the WebUI for the first time
after boot uses a short one-shot task, and the log asks for a reboot if that
task cannot be allocated. See [WebUI and REST API](webui.md).

- **Bulb duration** default 30 seconds. Set it under the connected `Bulb` page,
  `Bulb` > `Duration`. Restart to change is not needed. The value is remembered
  for next time.
- **Touch calibration** is stored when you run Calibrate under Display on a touch
  board.

### Board and hardware gating summary

- M5StickS3 only: Watchdog, Sleep while connected.
- Touch boards only (Core2, Tough): Calibrate, the reduced Screen off options,
  the on-screen shutter buttons, and the power-button screen lock.
- Not on M5Stack Core Basic: Auto off, Low battery.
- Infrared entries appear only when the board has an IR LED and Infrared is on.
- Feedback appears only when the board has a real output. Volume appears only
  when the output includes sound.
- Storage appears only with a supported SD card slot.
- Battery page rows appear only for values the board can actually measure.

## Part 2: Button input and controls

### Physical inputs per board

furble drives the interface with three logical inputs: previous, select, and
next. They map onto real buttons differently per board.

- **M5StickC, M5StickC Plus.** Front M5 button (BtnA), a small top button
  (BtnB), and the power button. The power button is the PMIC PEK button, read as
  a debounced click.
- **M5StickC Plus2, M5StickS3.** Front M5 button (BtnA), top button (BtnB), and a
  dedicated side power button. On the StickS3 the side button is a plain input
  owned by furble. Its hardware reset and power-off gestures are disabled at
  boot, so it never resets or powers off the device on its own.
- **M5Stack Core Basic.** Three buttons under the screen: BtnA, BtnB, BtnC. No
  touch.
- **M5Core2, M5Tough.** Three touch zones act as BtnA, BtnB, BtnC, plus a touch
  screen. On the shutter page these boards show on-screen Shutter, Focus, and
  Shutter Lock buttons. A double-click of the power button toggles a screen
  lock.

### Navigation model (two-button)

This is the default and the only navigation model. Three inputs drive a standard
scrolling menu:

- **Previous (left).** Moves focus to the previous item.
- **Select (middle).** Activates the focused item. On the shutter page it is the
  shutter.
- **Next (right).** Moves focus to the next item. On the shutter page it holds
  focus.

Per board, previous, select, next are:

- Sticks: power button, BtnA (front), BtnB (top).
- Cores: BtnA (left), BtnB (middle), BtnC (right).

**Back.** A long press of the left button, about 0.8 seconds, is Back from
anywhere. It is deliberately stronger than the on-screen back arrow, so it works
even where that arrow is hidden. On the shutter page a short click of the left
button is also Back. Menus also carry a focusable back arrow in the header that
you can select the normal way.

### Shutter, focus, and shutter lock (two-button)

Open the connected `Remote` page to reach the shutter control.

- **Select (middle) is the shutter.** Press to fire, release to end.
- **Next (right) is focus.** Press to half-press focus, release to end.
- Ricoh cameras do not support the focus action over this BLE path. Their
  focus control is a no-op; use the camera body to configure autofocus. Ricoh's
  supported shutter operation sends `OperationRequest {0x01, 0x01}`, which means
  capture with autofocus, not a separate focus command.
- **Shutter lock.** Hold focus (right), then press the shutter (select). The
  shutter locks open and stays open until you press a button. This is handy for
  a long exposure without holding a button down. You can also long-press the
  on-screen lock icon to toggle the lock. On touch boards the lock icon is
  always on screen.

To end a locked exposure, press any button. Leaving the page also releases the
shutter, so an exposure never keeps running out of sight.

### One-button mode

One-button mode is a shipped, opt-in alternative. Set it under `Settings` >
`Features` > `Button Mode` > `One-button`. It changes only the shutter page. All
menu navigation stays the same. The single select button (BtnA on sticks) then
does everything on the shutter page:

- **Hold to focus.** Press and hold the button. Focus engages while held and
  releases when you let go.
- **Double-click to shoot.** A second press that lands within about 0.4 seconds
  of a short click fires the shutter with no leading focus.
- **Click then hold to hold the shutter.** Click once, then press and hold. The
  shutter fires and stays down until you release.
- A single lone click does nothing on its own.

The gesture hint is printed on the Button Mode page: "One-button: hold=focus,
double-click=shoot, click+hold=hold shutter". The default is two-button.

### Other remote page controls

The connected `Remote` group also offers:

- `IR`: fire the camera over IR, shown only when Infrared is on.
- `Bulb`: timed long exposure. Set `Duration`, press Start, and the shutter
  releases at zero.
- `Interval`: the intervalometer, sharing the Timer configuration.
- `GPS Data`: the live GPS page.
- `Disconnect`: drop the camera and return.

### Simulator buttons

A visible interactive run of the SDL simulator creates a borderless control
strip with a top view of the modeled device. It is placed flush left of the
panel, then below or above it, with the right side last, so it does not cover
the panel or its button hints. The strip matches the panel's total window height,
including its title bar, so the two read as one window.

- **Sticks.** B is on the top edge, A is on the front/bottom edge, and PWR is
  on the left side. The emulated GPIOs are 39, 38, and 36.
- **Cores.** A, B, and C are in a row along the front edge. The emulated GPIOs
  are 39, 38, and 37.

Mouse down and mouse up model hold and release, and a click and the matching
keyboard letter (`a`/`b`/`p` on Sticks, `a`/`b`/`c` on Cores) drive the same
emulated GPIOs. A normal click is one press/release; repeats start only after
holding for about one second. The scenario `btn` verb is a different simulator
seam: it injects on the UI task through `UI::simPressButton` and does not toggle
those pins. The simulator detaches the touch device while the control window is
active, so the panel uses the non-touch layout and shows its legends. Set
`FURBLE_SIM_BUTTONS=0` to hide the control window, or
`FURBLE_SIM_NO_TOUCH=0` to keep the touch device for a deliberate mixed-input
session. Scripted, headless and fuzz runs never create it.

### Automation surface

Debug builds expose a USB serial console for host scripts. Two facts about how it
drives input:

- The simulator `btn` verb injects the same LVGL button read path on the UI
  task, including Back on a left-button hold, and it respects one-button versus
  two-button mode. It does not drive the emulated GPIOs; the interactive
  control window and keyboard letters do.
- The console `shutter` and `focus` commands are lower level. They enqueue camera
  commands directly and bypass the button-mode dispatch and the shutter-lock
  state. Use them to trigger the camera, not to test the button gestures.

See the [console commands reference](console-commands.md) for the full command
list, and [supported hardware](supported-hardware.md) for the board, camera, and
GPS unit matrices.
