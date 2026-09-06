# Supported hardware

The controllers, cameras, and GPS units furble supports. Taken from the firmware
source and the build configuration. See the project README for the tested camera
list.

## Controllers and build environments

furble builds five release firmware images. M5Unified detects the exact board at
runtime, so one image covers a board family. Every environment also has a
matching `-debug` variant that adds verbose logging and the USB serial console.

| Build env | Chip | Boards it runs on | Touch | Notes |
| :--- | :--- | :--- | :--- | :--- |
| `m5stick-c` | ESP32 | M5StickC | No | End of life. |
| `m5stick-c-plus` | ESP32 | M5StickC Plus, M5StickC Plus2 | No | Plus and Plus2 share the image. |
| `m5stick-s3` | ESP32-S3 | M5StickS3 | No | Native USB Serial/JTAG. PSRAM enabled. Only board with Sleep while connected and Watchdog. |
| `m5stack-core` | ESP32 | M5Stack Core (Basic/Gray) | No | No Auto off or Low battery. |
| `m5stack-core2` | ESP32 | M5Stack Core2, M5Tough (untested) | Yes | On-screen shutter buttons, touch calibration, power-button screen lock. The firmware detects the M5Tough and branches for it, but it has not been verified on hardware. |

## Cameras

Only Fujifilm cameras are available for hardware tests. Other vendors are covered
by code review and the FauxNY test camera.

| Vendor mode | Notes |
| :--- | :--- |
| Fujifilm (Basic) | Older Fujifilm firmware, unsecured pairing. |
| Fujifilm (Secure) | Newer Fujifilm firmware that requires the secure handshake. |
| Canon EOS (Smart) | Smart device pairing. Supports GPS, no manual focus. |
| Canon EOS (Remote) | Remote controller pairing. Focus, no GPS. |
| Nikon | Remote controller mode works (shutter only). Smart mode appears but does not pair. |
| Sony | Sony ZV and most modern Sony bodies. |
| Ricoh | Ricoh GR IV series. Shutter capture with autofocus and GPS supported. Focus-only control is unsupported. Does not work with GR III or GR II. |
| FauxNY | Software test camera for development. |

## GPS units

Location tagging uses an M5Stack GPS unit on Grove Port A. Every M5Stack GPS
receiver furble targets is the AT6668/CASIC family, so the existing $PCAS and
NMEA support covers them with no per-unit protocol code. The stored default
baud is 9600; select `Auto` or a fixed 115200 under `Settings` > `GPS` >
`GPS Baud` for the AT6668 units.

| Unit | Chipset | Antenna | furble boards | Wiring | Status |
| :--- | :--- | :--- | :--- | :--- | :--- |
| GPS/BDS Unit v1.1 | AT6668 | Ceramic patch | All five | Grove Port A | Supported. Indoor reception is marginal. |
| Unit-GPS (SMA) | AT6668 | External SMA active antenna | All five | Grove Port A | Supported, drop in. Recommended for weak or indoor reception. |
| Module GPS v2.1 | AT6668 | SMA | Core, Core2 only | M5-Bus module | Planned, not yet in firmware. |
| Atomic GPS Base v2.0 | AT6668 | SMA | None | Atom base | Out of scope. furble targets no Atom board. |

The older Mini GPS/BDS Unit is end of life and runs at 9600 baud. `Auto` probes
115200, 9600, 38400, 57600, 19200, and 4800, and requires two checksummed NMEA
sentences before declaring the receiver present. If probing fails, furble marks
the receiver absent, cuts the external rail, and retries once after 60 seconds.

## Related pages

- [Settings Reference](Settings-Reference)
- [Console Commands](Console-Commands)
- [UI Walkthrough](UI-Walkthrough)
