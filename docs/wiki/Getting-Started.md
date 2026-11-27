# Getting started

## Supported boards

furble runs on these ESP32 boards:

- M5StickC (80x160)
- M5StickC Plus (135x240)
- M5StickC Plus2 (135x240)
- M5StickS3 (135x240)
- M5Stack Core (320x240)
- M5Stack Core2 (320x240, touch)
- M5Tough (320x240, touch, untested)
- LilyGO T-Beam Supreme (128x64 1-bit OLED, bench port, no device run yet)

and on these LilyGO ESP32-S3 handhelds:

- LilyGO T-Deck (320x240, trackball and keyboard)
- LilyGO T-Deck Plus (the same board plus an onboard MIA-M10Q GNSS)
- LilyGO T-Watch Ultra (502x410 AMOLED, one button, onboard GNSS)

On the T-Deck the trackball moves the focus and its center switch activates the
focused row; the keyboard's Enter activates and Backspace goes back, because the
keyboard has no arrow keys. On the T-Watch Ultra the touch panel has no driver
yet, so the single BOOT button can activate the focused row but cannot move the
focus.

furble defines eight firmware environments. Six are the release images;
M5Unified detects the exact M5 board at runtime, so one image covers an M5 board
family. Each LilyGO board has its own image and those two are new: they are not
in the release matrix or the web installer yet. See
furble builds six release images plus one bench image. M5Unified detects the
exact M5 board at runtime, so one image covers a board family. The T-Beam
Supreme is not an M5 board and is not a release image yet: M5Unified cannot
detect it, so that image carries its own board row and drives the AXP2101 PMIC
and the SH1106 OLED directly. Its GPS receiver is detected at start, so an L76K
unit and a MAX-M10S unit run the same image. See
[Supported Hardware](Supported-Hardware) for the board to environment matrix.

## Flashing

The easiest path is the browser based flasher. Open the fork web installer in a
Web Serial capable browser (Chrome or Edge on desktop), pick your board, connect
the device over USB, and flash.

- Fork web installer: https://maxrink.github.io/furble/

Pick the exact model. Each manifest covers one chip family, and the installer
refuses an image that does not match the board you selected: the ESP32 boards
and the ESP32-S3 boards load their bootloader at different offsets, so the wrong
image does not boot until the board is re-flashed over USB.

The installer can also provision a freshly flashed debug build. Its "WiFi and
settings provisioning" step sends the WiFi credentials, the companion password,
the theme, TX power and the MQTT broker settings over the same USB cable, using
the debug console. A release image has no console, so tick "Debug build" first.
The step writes settings and never erases NVS, so run it again to change a value
or to retry.

To build and flash from source instead, furble is an ESP-IDF 5.x project built
with PlatformIO:

```
FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e m5stick-s3 -t upload
```

Swap `m5stick-s3` for your board environment: `m5stick-c`, `m5stick-c-plus`,
`m5stick-s3`, `m5stack-core`, `m5stack-core2`, `waveshare-s3-eth` or
`lilygo-tbeam-supreme`. See the project README for the full build notes.

If the 4 MB Core debug image exceeds its OTA slot, use the developer-only
`m5stack-core-usb-debug` environment. It is USB-UART only, uses one factory app
partition, and is not a release or wireless-update image. Avoid a full erase
when preserving NVS settings and camera bonds. Normal CI uses this profile for
a Core debug lane, together with the dual-OTA Core debug profile.

## First pairing

1. Put the camera into its Bluetooth pairing mode. Check the camera manual for
   where this lives; on Fujifilm it is under the Bluetooth or connection menu.
2. On furble, open **Scan** from the main menu.
3. When the camera appears in the list, select it. furble connects and saves the
   camera for next time.
4. On later use, open **Connect** and pick the saved camera.

Once connected you land on the Connected menu, with Remote, Bulb, Interval, GPS
Data, and Disconnect. See the [UI Walkthrough](UI-Walkthrough) for a tour of each
page and the [Controls](Controls) page for the button map.

## GPS location tagging

For cameras that support it (Fujifilm, Sony, and Ricoh), furble can tag photos with the
device location using an M5Stack GPS unit on Grove Port A. Enable it under
`Settings` > `GPS`, and configure the camera to request location data from the
remote. The GPS/BDS Unit v1.1 (AT6668) needs the baud rate set to 115200 under
`Settings` > `GPS` > `GPS baud 115200`.

The AT6668 units are all supported. furble also detects and configures a u-blox
M10, a Quectel LC76G or LC29H and a Unicore UC6580 on the same port: it sends
three identification probes at GPS start and whichever receiver answers decides
the configuration protocol, so nothing needs selecting by hand. A receiver it
does not recognise still supplies fixes over NMEA.

The AT6668 units are all supported. For weak or indoor reception, the Unit-GPS
with the external SMA antenna is the recommended drop in. A u-blox receiver
(MAX-M10S, MIA-M10Q or another M10 UART module, or a dual-band NEO-F10N) is
detected automatically and configured over UBX; select `Auto` baud for it. A
dual-band part needs a dual-band L1/L5 antenna and runs both bands of each
constellation it enables. u-blox support is simulator-tested and
hardware-untested. If the receiver has
no fresh fix and the companion app is connected with phone GPS enabled, furble
uses the phone fix. The phone also sends its horizontal accuracy, which furble
keeps with the fix; the `255` sentinel means unknown rather than a zero-metre
fix. See [Companion camera management](../companion.md) for the phone location
behavior, including the macOS terminal CLI's read-only battery CSV monitor over
BLE or the StickS3 USB CDC console. See [Supported Hardware](Supported-Hardware)
for the full GPS unit matrix.

## Next steps

- [UI Walkthrough](UI-Walkthrough): a screenshot of every page.
- [Settings Reference](Settings-Reference): every setting explained.
- [Controls](Controls): the button map and input modes.
