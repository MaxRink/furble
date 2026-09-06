# furble - ***F***lexible ***U***nified ***R***emote ***B***luetooth ***L***ow ***E***nergy

![PlatformIO CI](https://github.com/gkoh/furble/workflows/PlatformIO%20CI/badge.svg)

## About this fork

This is a friendly fork of [gkoh/furble](https://github.com/gkoh/furble),
which does the hard work this fork builds on. Every change here is offered
upstream as a pull request. The fork exists because development moves faster
than review, and because some changes need soak time before they are ready
for everyone.

What this fork adds over upstream right now:
- Power work for the M5StickS3: BLE modem sleep, light sleep while connected,
  a CPU speed setting, and PSRAM support that also fixes out of memory crashes
- Battery percent, voltage, current and a runtime estimate
- GPS data reachable while connected, receiver configuration (update rate,
  sentences, constellations), and a raw NMEA page
- A bulb timer for long exposures
- An intervalometer, IR shutter trigger, and audible, visual and haptic feedback
- SD card GPX track logging and settings backup
- Diagnostics pages: device info, power state, reset reason, heap
- BLE scan duty cycle and scan timeout settings
- A USB serial console for developers and test automation
- A host SDL simulator for the UI, plus an Android companion app
- A simulator-tested IMU spirit level and live IMU diagnostics page. Enable it
  under Settings > Sensors; the Level page appears while connected.
- Plan documents for every change under `plans/`, and CI on every pull request

Use this fork if you want battery life on a StickS3, the newest features, or
the developer tooling, and you accept that changes land here first with less
soak time. Use upstream if you want the most settled firmware and release
process.

A Bluetooth wireless remote shutter release originally targeted at Fujifilm mirrorless
cameras. furble now supports:
- Fujifilm
- Canon
- Ricoh
- Nikon
- Sony

The remote uses the camera's native Bluetooth Low Energy interface thus additional
adapters are not required.

furble is developed on ESP32 devices as a PlatformIO project.

## M5StickC Plus2 (Dark theme)

![furble - Dark - M5StickC Plus2](https://github.com/user-attachments/assets/9888e348-1476-4d5e-bd47-3c03b87f062a)

## M5Core2 (Default theme)

![furble - Default - M5Core2](https://github.com/user-attachments/assets/0e60cff4-fc1b-4345-b0fc-5d85be0a705d)

## Supported Cameras

The following devices have been tested and confirmed to work:
- Fujifilm
   - Fujifilm GFX100 II ([@matthudsonau](https://github.com/matthudsonau))
   - Fujifilm GFX100RF ([@GFXUser101](https://github.com/GFXUser101))
   - Fujifilm GFX100S ([@adrianuseless](https://github.com/adrianuseless))
   - Fujifilm GFX100S II ([@GFXUser101](https://github.com/GFXUser101))
   - Fujifilm GFX50S II ([@TomaszLojewski](https://github.com/TomaszLojewski))
   - Fujifilm X-E4 ([@Rediwed](https://github.com/Rediwed))
   - Fujifilm X-E5 ([@daniel-ch73](https://github.com/daniel-ch73))
   - Fujifilm X-H1
   - Fujifilm X-H2S ([@val123456](https://github.com/val123456))
   - Fujifilm X-S10 ([@dimitrij2k](https://github.com/dimitrij2k))
   - Fujifilm X-S20 ([@kelvincabaldo07](https://github.com/kelvincabaldo07))
   - Fujifilm X-T200 ([@Cronkan](https://github.com/Cronkan))
   - Fujifilm X-T3 ([@ubuntuproductions](https://github.com/ubuntuproductions))
   - Fujifilm X-T30
   - Fujifilm X-T4 ([@TomaszLojewski](https://github.com/TomaszLojewski))
   - Fujifilm X-T5 ([@stulevine](https://github.com/stulevine))
   - Fujifilm X100V
   - Fujifilm X100VI (secure connection and shutter command verified; physical capture pending)
- Canon
   - Canon EOS M6 ([@tardisx](https://github.com/tardisx))
   - Canon EOS R6 Mark II ([@hijae](https://github.com/hijae))
   - Canon EOS RP ([@wolcano](https://github.com/wolcano))
   - Canon PowerShot G9 X Mark II ([@Mich2e](https://github.com/Mich2e))
- Ricoh
   - Ricoh GR IV HDF ([@sky18Dragon](https://github.com/sky18Dragon))
- Nikon
   - Nikon COOLPIX B600
   - Nikon Z6 III ([@herrfrei](https://github.com/herrfrei))
- Sony
   - Sony ZV-1F

## Table of Features

| Camera             | Scanning | Pairing | Shutter Release | Focus   | GPS     |
| :---:              | :---:    | :---:   | :---:           | :---:   | :---:   |
| Fujifilm X & GFX   | ✔️        | ✔️       | ✔️               | ✔️[^1]   | ✔️       |
| Canon EOS (Remote) | ✔️        | ✔️       | ✔️               | ✔️       | :x:[^2] |
| Canon EOS (Smart)  | ✔️        | ✔️       | ✔️               | :x:[^2] | ✔️       |
| Ricoh              | ✔️        | ✔️       | ✔️[^3]           | :x:[^4]  | ✔️       |
| Nikon (Remote)     | ✔️        | ✔️       | ✔️[^3]           | :x:[^2] | :x:[^2] |
| Nikon (Smart)      | ✔️        | :x:     | :x:             | :x:     | :x:     |
| Sony ZV            | ✔️        | ✔️       | ✔️               | ✔️       | ✔️       |

[^1]: see [#99](https://github.com/gkoh/furble/discussions/99)
[^2]: Non-existent
[^3]: Auto-shutter release only, no manual exposure control
[^4]: Focus-only controls are unsupported and do not send a camera command.
The supported shutter command performs an immediate capture with autofocus.

## Supported Controllers

Initially targeted at the M5StickC, the following controllers from [M5Stack](https://m5stack.com/) are supported:
* M5StickC (EOL)
* M5StickC Plus
* M5StickC Plus2
* M5StickS3
* M5Core Basic
* M5Core2
* M5Tough (untested)

furble builds five release firmware images, one per board environment. M5Unified
detects the exact board at runtime, so one image covers a board family. The
M5Tough is not a build environment. It shares the M5Core2 image through
M5Unified board detection, but it has not been verified on hardware. See
[docs/supported-hardware.md](docs/supported-hardware.md) for the board to
environment matrix.

## Installation

### Easy Install

The simplest way to get started is with the
[furble browser installer](https://maxrink.github.io/furble/).
It flashes a supported M5Stack device from a desktop browser.

### Browser flashing

The installer uses Web Serial. Use Chrome or Edge on a desktop computer with a
data-capable USB cable. Safari and Firefox do not provide the required browser
interface.

1. Connect the M5Stack device to the computer.
2. Open the [furble browser installer](https://maxrink.github.io/furble/).
3. Select the device model and click `Install`.
4. Approve the serial port when the browser asks.

For M5StickS3, the installer first opens the running developer-console image
and requires all PMIC safety acknowledgements before it offers the firmware
port. Approve the same port again to continue flashing. If the running image
does not answer, follow the physical battery-power-loss recovery procedure
shown by the installer. This prevents a retained PMIC watchdog or download
lock from making a serial upload unsafe.

If the installer offers to erase the device, decline to keep existing settings
and paired cameras. Accept the erase when starting from a clean device.

### PlatformIO

PlatformIO does everything assuming things are installed and connected properly.
In most cases it should be:
- clone the repository
- plug in the M5StickC
    - `platformio run -e m5stick-c -t upload`
- OR plug in M5StickC Plus/Plus2
    - `platformio run -e m5stick-c-plus -t upload`
- OR plug in the M5Stack Core2
    - `platformio run -e m5stack-core2 -t upload`

More details are on the wiki: [PlatformIO](https://github.com/gkoh/furble/wiki/Linux-Command-Line-(For-Developers))

### Debug builds (developers)

Every board has an optional `<board>-debug` environment, for example
`m5stick-s3-debug`. These are built with `build_type = debug` and with
`LOG_LOCAL_LEVEL=ESP_LOG_VERBOSE`, so `ESP_LOGD` and `ESP_LOGV` in furble
sources are compiled in. They share the release `sdkconfig` of the board they
extend, so nothing but the compiler flags changes. CI and releases build the
five release environments only, never the debug ones.

Build, flash and watch the log:
- `platformio run -e m5stick-s3-debug -t upload`
- `platformio device monitor -e m5stick-s3-debug`

The runtime log level still starts at `INFO` because `CONFIG_LOG_DEFAULT_LEVEL`
is `3` in the committed `sdkconfig` files. Call
`esp_log_level_set("*", ESP_LOG_VERBOSE)` to lift it. The ceiling
`CONFIG_LOG_MAXIMUM_LEVEL` is already `5`, so no `sdkconfig` change is needed.

The M5StickS3 is an ESP32-S3, which has a built-in USB-Serial/JTAG peripheral on
GPIO19 and GPIO20. Its debug environment selects `debug_tool = esp-builtin`, so
source level debugging over the USB-C port needs no extra hardware:
- `platformio debug -e m5stick-s3-debug`

Log output already reaches that port as the secondary console,
`CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG` is set in `sdkconfig.m5stick-s3`,
so the monitor works over the same cable.

The debugger halts the CPU. An active camera connection will drop while you sit
on a breakpoint, and the task watchdog will complain. It logs rather than
reboots.

The other boards are plain ESP32 and reach the host through a USB to UART
bridge. They have no JTAG peripheral, so their debug environments give verbose
logging and unoptimised code only. There are no breakpoints on a StickC.

### Serial console (developers)

The debug environments also set `-DFURBLE_CONSOLE=1`, which builds a text
console onto the same USB port that carries the log. It exists so a developer or
a host script can drive furble without walking the menu tree by hand. No release
environment contains it, so there is nothing to enable and nothing to switch
off.

Open it with `platformio device monitor -e m5stick-s3-debug` and type `help`.
Log output shares the port, so `log * warn` is usually the first thing worth
typing. Every command prints one fact per line as `key: value`, so a host script
can parse it with a split on the first colon.

Development builds identify their source as `dev+g<revision>` in the About
page, companion BLE Device Information, and the `version` command. The revision
is Git's unambiguous abbreviation of at least eight characters. A dirty checkout
adds the deterministic `.dirty` suffix; explicit release versions are shown
unchanged.

```
version                             firmware and IDF version
status                              state, targets, uptime, heap, battery, reset reason
power                               power stats, or a CSV power log
perf                                task, heap, and LVGL performance
gps                                 GPS status and control, eg. gps send PCAS12,10
imu status                         read-only IMU type/read diagnostic
time status | flush                 wall-clock status or persist before shutdown
settings list | get | set           read and write every setting
ui audit                            dump the current page layout
cameras list | status               saved cameras, or the active targets
connect [index]                     no index uses the multi-connect selection
disconnect
shutter press | release | hold <ms>
focus press | release
ir fire [protocol]                  fire the IR emitter
scan start | stop | list
bt scan | explore | pair | journal   Bluetooth diagnostics
feedback test <event>               play a feedback pattern
log <tag> <level>
debug <subsystem>                   dump internal state
reboot
```

The full command reference, with every subcommand, is in
[docs/console-commands.md](docs/console-commands.md).

On the display-less Waveshare ESP32-S3-ETH, `status` reports battery level and
voltage as unknown and current as unavailable. It does not infer USB or
optional PoE power from Ethernet link state. The optional PoE HAT has no
software-readable presence or negotiation signal.

Saving a setting is not the same as applying it. Settings read on every use take
effect at once, settings the UI caches when it starts do not. `settings get` and
`settings set` say which of the two a setting is.

Two things about the console distort measurements, so do not take power numbers
from a build that contains it. On the ESP32 boards the console holds an APB
frequency lock for its lifetime, because UART receive drops characters while
automatic light sleep gates the APB clock. On the M5StickS3 no lock is needed,
`CONFIG_USJ_NO_AUTO_LS_ON_CONNECTION` already keeps the chip out of light sleep
while USB is connected.

### Recovering a locked-up M5StickS3

The Watchdog setting is enabled by default. Turn it off in `Settings->Features->Watchdog`
and reboot before attaching a JTAG debugger, because halting the CPU stops the watchdog feed.

If the M5StickS3 is powered off and will not turn on, single click the side button.

If the device is wedged, the screen is dark, and USB is not enumerating, first
try the PMIC-safe uploader below while the application can still answer. USB
unplugging alone is not a PMIC reset: an already-set `DL_LOCK` survives an ESP
reset, a PMIC watchdog reset, and removal of USB power while the battery is
connected. If the lock is already set and the application cannot clear it, the
device needs true PMIC power loss (battery disconnect/depletion or service)
before the side-button recovery can work:

1. Remove battery power or have the battery fully depleted/service-disconnected.
2. Restore battery power, then press and hold the side button for about two seconds.
3. When the green LED inside the device flashes, release the button.
4. Connect USB. The port should enumerate in download mode.
5. Reflash with `pio run -e m5stick-s3 -t upload`.

For a responsive developer-console build, use the PMIC-safe uploader. It
disarms the external watchdog before entering ROM download mode, verifies that
the long-press recovery path is unlocked, and starts PlatformIO only after both
checks pass:

```sh
python3 tools/flash_prepare.py --port /dev/cu.usbmodemXXXX \
  --env m5stick-s3-debug
```

If preflight cannot reach the application, it refuses to flash and prints the
manual recovery steps above. A cancelled preflight should be followed by
`flash cancel` on the console, or by a reboot, so normal watchdog protection is
restored.

## Usage

For a screen by screen tour of the interface with a screenshot of every page,
see the [UI walkthrough](docs/ui-walkthrough.md). For the exhaustive reference
of every setting and every button input mode, see
[settings and controls](docs/settings-and-controls.md).

The top level menu has the following entries:
- `Connect`
- `Scan`
- `Delete`
- `Settings`
- `Off`

On first use, put the target camera into pairing mode, then hit `Scan`. If the
camera advertises a known, matching signature, it should appear in the list.
You can then connect to the target camera, which, if successful, will save the
entry and show the remote menu.

`furble` will identify as `furble-xxxx` where `xxxx` is a consistent identifier enabling one to differentiate multiple controllers.

Upon subsequent use it should be enough to hit `Connect`, selecting the
previously paired device and leading to the remote menu.

From the remote menu you may choose to disconnect, control the shutter or activate the intervalometer.

More details are on the wiki: [Usage Guide](https://github.com/gkoh/furble/wiki/Usage-Guide)

### GPS Location Tagging

For Fujifilm & Sony cameras, location tagging is supported with an M5Stack GPS
unit on Grove Port A. Every unit furble targets is the AT6668/CASIC family, so
one set of $PCAS and NMEA support covers them all.

| Unit | Antenna | Status |
| :--- | :--- | :--- |
| [GPS/BDS Unit v1.1 (AT6668)](https://shop.m5stack.com/products/gps-bds-unit-v1-1-at6668) | Ceramic patch | Supported. Indoor reception is marginal. |
| Unit-GPS (SMA) | External SMA antenna | Supported. Recommended for weak or indoor reception. |
| Module GPS v2.1 | SMA | Planned. Not yet in firmware, Core and Core2 only. |
| [Mini GPS/BDS Unit](https://shop.m5stack.com/products/mini-gps-bds-unit) | Ceramic patch | End of life, 9600 baud. |

See [docs/supported-hardware.md](docs/supported-hardware.md) for the full unit
matrix. GPS support can be enabled in `furble` in `Settings->GPS`, the camera
must also be configured to request location data.

The default baud rate for the GPS unit is 9600.
The new v1.1 unit runs at a higher baud rate and must be configured under
`Settings->GPS->GPS baud 115200` for correct operation.

The GPS receiver itself can also be configured under `Settings->GPS`:
- `Update rate` (how often the receiver reports a position, from 1000ms down to 100ms)
- `Sentences` (cut the receiver down to the sentences `furble` actually reads)
- `Constellation` (which satellite systems the receiver listens to)
- `Fix Hold` (keep sending the last fix for up to an hour after the receiver
  loses it, so a tunnel or a doorway does not cost a run of geotags)
- `Extrapolate` (while a fix is held, project it along the last course and
  speed, experimental)

Each of these defaults to `Default`, which leaves the receiver on its own
settings and behaves exactly as before.
A change is sent to the receiver when GPS is enabled, and the receiver goes
back to its own defaults the next time it is powered off.

`Settings->GPS->Raw NMEA` shows the sentences arriving from the receiver along
with the fix state and error counts.
It is the place to look to confirm the receiver accepted a change.

### Intervalometer/Timer

The intervalometer can be configured via three settings in `Settings->Timer`:
- Count (number of images to take)
- Delay (time between images)
- Shutter (time to keep shutter open)

Count can be configured up to 999 or infinite.
Delay and shutter time can be figured with custom or preset values from 0 to 999 in milliseconds, seconds or minutes.

Some camera protocols such as Ricoh GR trigger capture with a single operation request and do not expose separate exposure start/stop control. For those cameras the intervalometer still controls the wait, count, and delay between captures, but the camera ignores the configured shutter-open duration.

### Bulb

`Bulb` on the connected menu holds the shutter open for a set time, which saves
watching a stopwatch during a long exposure.

Set the length under `Bulb->Duration`, then hit `Start`.
The page counts down and the shutter is released at zero.
The duration is remembered for next time and defaults to 30 seconds.

`Stop` ends the exposure early, and leaving the page also releases the shutter,
so an exposure never keeps running out of sight.

The camera must be in bulb mode, otherwise it will end the exposure on its own
and the countdown will simply run out.

### Shutter Lock

When in `Shutter` remote control, holding focus (button B) then release (button A) will engage shutter lock, holding the shutter open until a button is pressed.

### Bluetooth

The Bluetooth options live under `Settings->Bluetooth`.
`TX Power` has moved there from the top level of `Settings`.

`Scan mode` sets how hard the radio listens while `Scan` is looking for
cameras.
`Full` is the default and spots a camera fastest.
`Balanced` and `Low` listen less of the time, which is easier on the battery
but can take longer to find a camera.

`Scan timeout` ends a scan by itself after 30, 60 or 120 seconds.
The default is `Never`, which keeps scanning until you leave the page.
When a scan does end on its own, the `Scan` page shows a `Scan finished` notice
with a `Rescan` button.

Pairing with a saved camera always scans at full duty, so neither setting slows
down `Connect`.

### Power

The CPU speed can be selected under `Settings->Power->CPU speed`, with a choice
of 80, 160 or 240 MHz.
The default is 160 MHz, and a change takes effect immediately.

A higher speed makes the interface feel snappier, but it costs battery life.
80 MHz is the gentlest on the battery, 240 MHz is the most demanding.

`Settings->Power->Battery Style` controls what the battery indicator in the
header shows.
`Icon` is the default, `Percent` replaces the icon with the charge level, and
`Both` shows the icon and the level together.

`Settings->Power->Battery` is a page of battery detail, showing the charge
level, the voltage, the current draw where the controller can measure it,
whether it is charging, and an estimate of the remaining runtime.
Rows the controller cannot measure are simply not shown, so the page is shorter
on some boards.

`Settings->Power->Sleep while connected` lets the controller doze between
Bluetooth events while a camera is connected.
It is off by default, and it is only offered on the M5StickS3, the other
controllers cannot sleep with a connection up.

Turning it on is a worthwhile saving on a long shoot.
The trade is that the first press after a quiet spell may take a moment longer
to reach the camera.
The setting is read when a connection is made, so switch it before connecting.

### Display

The window title can be hidden with `Settings->Display->Show Title`.
It is on by default.
Hiding it frees up space in the header for the status icons, which is welcome
on the narrow stick displays.

### Themes

A few basic themes are included, to change:
* `Settings->Themes-><desired theme>`
   * hit 'Restart' to save and restart for the theme to take effect
   * better dynamic theme change support is improving in upstream LVGL

## Motivation

I found current smartphone apps for basic wireless remote shutter control to be
generally terrible.
Research revealed the main alternative was attaching a dongle to the camera, of
which there were many options varying in price and quality.
I really just wanted the [Canon
BR-E1](https://www.eos-magazine.com/articles/remotes/br-e1-canon-bluetooth-remote.html),
but for my camera.

### Possibly Supported Cameras

#### Fujifilm

Given reports from the community and access to additional cameras, it
seems many (all?) Fujifilm cameras use the same Bluetooth protocol.
Reports of further confirmed working Fujifilm cameras are welcome.

#### Canon

With access to a Canon EOS M6, I was able to implement support for it. Other
Canon cameras might work, but I suspect the shutter control protocol will be
different.
@wolcano kindly implemented initial support for the Canon EOS RP.
@hijae kindly helped with better Canon EOS R support.

#### Ricoh

All Ricoh GR IV series cameras are theoretically supported. This support was
graciously implemented by @sky18Dragon.
The current implementation will _not_ work with GR III or GR II.
Focus-only controls are intentionally unsupported. The documented Ricoh Focus
Mode setting does not trigger autofocus, and no separate half-press command has
been verified. The BLE `OperationRequest` `{0x01, 0x01}` sequence is a capture
request with autofocus, not a focus-only command. See the [Ricoh BLE protocol
reference](https://github.com/dm-zharov/ricoh-gr-bluetooth-api).

#### Nikon

Nikon cameras that support the remote wireless controller (ML-L7) should work,
use the "Connection to remote" menu option.
This has been tested on a Nikon COOLPIX B600. Unfortunately, the remote wireless
mode has no support for GPS or focus functions, thus only shutter release works.
Note that other Nikon cameras will appear in the scan, but will not pair
(further support is under investigation).
@herrfrei kindly assisted with Z6 III support.

#### Sony

Sony cameras appear to use a reasonably uniform and robust bluetooth control
protocol. Most modern Sony cameras should be supported. Testing was performed
on Sony ZV-1F.

To pair with a Sony camera (some models may have different menu options, the
following matches the ZV-1F):
- set 'Bluetooth Rmt Ctrl' to 'On'
- set 'Bluetooth Function' 'On'
- under Bluetooth, start 'Pairing'
- start 'Scan' with `furble'
   - due to an oddity with the Bluetooth library, if `furble` 'Scan' is started
     first, the Sony camera may not appear

#### Protocol Reverse Engineering

Android supports snooping bluetooth traffic so it was trivial to grab a HCI log
to see what the manufacturer supplied camera app was doing.

For all supported cameras, a snoop log of:
- scanning
- pairing
- re-pairing
- shutter release

was analysed with Wireshark.

It was then an experiment in reducing the interaction to the bare minimum just
to trigger the shutter release.

### Supporting More Cameras

The best way is to repeat the previous steps, analyse the bluetooth HCI snoop
log with Wireshark, implement, then test against the actual device.

## Background Story

### Requirements

#### Hardware

I wanted a complete solution out of the box to have:
- bluetooth low energy
- physical button
- visual indicator (LED or display)
- battery
- case
- low cost

My search concluded with the [M5StickC](https://m5stack.com/products/stick-c)
from [M5Stack](https://m5stack.com).
The M5StickC and M5StickC Plus have since been EOL and replaced with the [M5StickC Plus2](https://shop.m5stack.com/products/m5stickc-plus2-esp32-mini-iot-development-kit).

The M5StickC is an ESP32 based mini-IoT development kit which covered all of the
requirements (and more). At time of writing, M5Stack sell the M5StickC for
US$9.95.
The M5StickC Plus(2) sells for US$19.95.

#### Software

The project is built with [PlatformIO](https://platformio.org) and depends on
the following libraries:
- [esp-nimble-cpp](https://github.com/h2zero/esp-nimble-cpp)
- [LVGL](https://github.com/lvgl/lvgl)
- [M5Unified](https://github.com/m5stack/M5Unified)
- [TinyGPSPlus](https://github.com/mikalhart/TinyGPSPlus)

# Known Issues

- depending on your perspective, battery life is anywhere from reasonable to bad
   - with an active BLE connection and power management, the ESP32 consumes around 30mA
      - an M5StickC Plus2 would last around 6 hours
      - an M5StickC Plus would last around 4 hours
      - an old M5StickC would last around 3 hours
   - if battery life is crucial, and form factor is not, consider an M5Stack Core with the 1500mAh module
      - this might last 50 hours

# Things To Do

- Support more camera makes and models
   - Get access to and support the following:
     - Nikon Z
     - Others?

# Links

Inspiration, references and related information for this project came from the following projects/posts:
- Canon
   - https://iandouglasscott.com/2017/09/04/reverse-engineering-the-canon-t7i-s-bluetooth-work-in-progress/
   - https://github.com/ArthurFDLR/BR-M5
   - https://github.com/RReverser/eos-remote-web
- Fujifilm
  - https://github.com/hkr/fuji-cam-wifi-tool
  - https://github.com/petabyt/fudge
- Ricoh
  - https://github.com/dm-zharov/ricoh-gr-bluetooth-api
- Sony
   - https://gethypoxic.com/blogs/technical/sony-camera-ble-control-protocol-di-remote-control
   - https://gregleeds.com/reverse-engineering-sony-camera-bluetooth
   - https://github.com/Staacks/alpharemote
