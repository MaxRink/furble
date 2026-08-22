# Getting started

## Supported boards

furble runs on these M5Stack ESP32 boards:

- M5StickC (80x160)
- M5StickC Plus (135x240)
- M5StickC Plus2 (135x240)
- M5StickS3 (135x240)
- M5Stack Core (320x240)
- M5Stack Core2 (320x240, touch)
- M5Tough (320x240, touch)

## Flashing

The easiest path is the browser based flasher. Open the fork web installer in a
Web Serial capable browser (Chrome or Edge on desktop), pick your board, connect
the device over USB, and flash.

- Fork web installer: https://maxrink.github.io/furble/

To build and flash from source instead, furble is an ESP-IDF 5.x project built
with PlatformIO:

```
FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e m5stick-s3 -t upload
```

Swap `m5stick-s3` for your board environment: `m5stick-c`, `m5stick-c-plus`,
`m5stick-s3`, `m5stack-core`, or `m5stack-core2`. See the project README for the
full build notes.

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

For cameras that support it (Fujifilm and Sony), furble can tag photos with the
device location using an M5Stack GPS unit. Enable it under `Settings` > `GPS`,
and configure the camera to request location data from the remote. The newer
GPS/BDS Unit v1.1 (AT6668) needs the baud rate set to 115200 under
`Settings` > `GPS` > `GPS baud 115200`.

## Next steps

- [UI Walkthrough](UI-Walkthrough): a screenshot of every page.
- [Settings Reference](Settings-Reference): every setting explained.
- [Controls](Controls): the button map and input modes.
