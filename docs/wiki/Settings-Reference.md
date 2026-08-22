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
[README](Getting-Started). This document is the exhaustive reference that sits
underneath it.

For a screen by screen tour of the interface with a screenshot of every page,
see the [UI walkthrough](UI-Walkthrough).

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

Settings menu at a glance (in on-screen order): Display, Features, Infrared,
GPS, Timer, Theme, Text size, Bluetooth, About, Power, Feedback, Diagnostics,
Storage.

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

Some rows are pages rather than settings (for example the Battery page or Raw
NMEA). Those are read-only status pages and are listed for completeness but have
no stored value.

### Display

Submenu: `Settings` > `Display`.

| Setting | Default | Values | Applies | Notes |
| :--- | :--- | :--- | :--- | :--- |
| Brightness | 128 | slider, roughly 0 to 240 | Now (live preview) | Saved when you release the slider. |
| Inactivity timeout | Never | Never, 30 secs, 60 secs, 2 mins, 5 mins, 10 mins | Now | Dims and then sleeps the screen after this idle time. |
| Screen off | Dim | Dim, Off, Off remote on (button boards). Dim, Off (touch boards) | Restart | Touch boards do not offer the remote-on option. |
| Calibrate | n/a | action | n/a | Touch boards only. Launches touch calibration and stores the result. |
| Show Title | On | On, Off | Restart | Hiding the title frees header space on narrow stick displays. |

### Features

Submenu: `Settings` > `Features`.

| Setting | Default | Values | Applies | Notes |
| :--- | :--- | :--- | :--- | :--- |
| Button Mode | Two-button | Two-button, One-button | Now | See Part 2. The gesture hint is shown under the roller. Read live on every shutter press. |
| Auto-Connect | Off | On, Off | Now | Connect to the saved camera at boot without a menu step. |
| FauxNY | Off | On, Off | Now | Adds a fake test camera to the scan list for development. |
| Infinite-ReConnect | Off | On, Off | Now | Keep retrying a lost connection forever. |
| Reconnect Backoff | Off | On, Off | Now | Space out reconnect attempts to save battery. |
| Multi-Connect | Off | On, Off | Now | Allow connecting to more than one camera at once. |
| Companion | Off | On, Off | Restart (dangerous) | Enables the companion BLE service. Connection-affecting. |
| Watchdog | On | On, Off | Restart | M5StickS3 only. Turn off before attaching a JTAG debugger. |
| Preset Picker | Off | On, Off | Restart | Adds the exposure preset stepper on the shutter page. |
| Boot screen | On | On, Off | Next boot | The startup splash. Off restores the old plain boot. |

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
| GPS baud 115200 | Off (9600) | 9600, 115200 | Now | The v1.1 (AT6668) unit needs 115200. The older unit uses 9600. |
| Update rate | Default | Default, 1000 ms, 500 ms, 200 ms, 100 ms | Now | How often the receiver reports a position. Default leaves the receiver alone. |
| Sentences | Default | Default, RMC+GGA | Now | RMC+GGA prunes output to the sentences furble reads. |
| Constellation | Default | Default, GPS, BDS, GPS+BDS, GLONASS, GPS+GLO, BDS+GLO, All | Now | Which satellite systems the receiver uses. |
| Power saving > Receiver | Always on | Always on, Standby (PCAS12), Rail cycling | Now | Receiver low-power policy. Rail cycling is experimental. |
| Power saving > Sleep between fixes | No standby | No standby, 5 s, 10 s, 15 s | Now | Standby interval for the PCAS12 policy. |
| Assisted start | Off | Off, Position and time | Now | Feeds the receiver a position and time hint for a faster fix. |
| GPS Data | n/a | page | n/a | Live position, satellites, speed, altitude, and time. |
| Raw NMEA | n/a | page | n/a | Live receiver sentences, fix state, and error counts. Includes a Hot restart button. |

The receiver is set from these when GPS is enabled, and it returns to its own
defaults the next time it is powered off.

### Timer (intervalometer)

Submenu: `Settings` > `Timer`. These four values are stored together as the
interval configuration.

| Setting | Default | Values | Applies | Notes |
| :--- | :--- | :--- | :--- | :--- |
| Count | 10 | 1 to 999, or infinite | Restart* | Number of frames to take. |
| Delay | 15 s | 0 to 999 in ms, s, or mins | Restart* | Time between frames. |
| Shutter | 30 ms | 0 to 999 in ms, s, or mins | Restart* | How long the shutter is held per frame. |
| Wait | 0 s | 0 to 999 in ms, s, or mins | Restart* | Initial wait before the first frame. |

*The interval blob is read when the timer page is built. In practice you set
these on the device and press Start, so the value you see is the value that
runs. Cameras that trigger with a single operation (for example Ricoh GR) ignore
the Shutter duration but still honour Count, Delay, and Wait.

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
| Scan timeout | Never | Never, 30 secs, 60 secs, 120 secs | Now | Ends a scan by itself. Connect always scans at full duty. |

### About

Submenu: `Settings` > `About`. A read-only page: firmware version, device ID,
build date, IDF version, uptime, heap, and reset reason. No stored settings.

### Power

Submenu: `Settings` > `Power`.

| Setting | Default | Values | Applies | Notes |
| :--- | :--- | :--- | :--- | :--- |
| Sleep while connected | Off | On, Off | Next connection (dangerous) | M5StickS3 only. Doze between BLE events while connected. Read when a connection is made, so set it before connecting. |
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

### Settings that live outside the Settings menu

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

### Automation surface

Debug builds expose a USB serial console for host scripts. Two facts about how it
drives input:

- The simulator button injector reproduces the real button path, including Back
  on a left-button hold, and it respects one-button versus two-button mode.
- The console `shutter` and `focus` commands are lower level. They enqueue camera
  commands directly and bypass the button-mode dispatch and the shutter-lock
  state. Use them to trigger the camera, not to test the button gestures.

See the [Console Commands](Console-Commands) reference for the full command
list, and [Supported Hardware](Supported-Hardware) for the board, camera, and
GPS unit matrices.
