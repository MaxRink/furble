# furble

furble is a Bluetooth Low Energy camera shutter remote for M5Stack ESP32
devices. It connects to a supported camera over BLE and gives you a shutter
remote, a bulb timer, an intervalometer, and optional GPS location tagging, all
driven from the device buttons.

This wiki is a friendly fork of the upstream furble project.

## Pages

- **[Getting Started](Getting-Started)**: supported boards, flashing, and first
  pairing.
- **[UI Walkthrough](UI-Walkthrough)**: a screen by screen tour with a
  screenshot of every page.
- **[Settings Reference](Settings-Reference)**: every setting, with its default,
  values, and when a change applies.
- **[Controls](Controls)**: the per board button map and both input modes.
- **[Supported Hardware](Supported-Hardware)**: controllers, cameras, and GPS
  units.
- **[Console Commands](Console-Commands)**: the USB serial console in debug
  builds.

## At a glance

- **Boards**: M5StickC, M5StickC Plus, M5StickC Plus2, M5StickS3, M5Stack Core,
  M5Stack Core2, M5Tough (untested).
- **Cameras**: Fujifilm, Canon, Sony, Nikon, Ricoh, and others. See the project
  README for the current list.
- **Framework**: ESP-IDF 5.x built with PlatformIO.

Two board renders, dark and default themes:

![Main menu](img/main.png)
![Dark connected menu](img/dark-connected.png)
