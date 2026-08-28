# furble supported hardware

This is the reference for the controllers, cameras, and GPS units furble
supports. It is taken from the firmware source and the build configuration, so
it matches the shipped behaviour. For a friendly overview see the top level
[README](../README.md).

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

Build a board with its environment name:

```sh
FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e m5stick-s3 -t upload
```

## Cameras

The following vendor modes are implemented in `lib/furble/`. Only Fujifilm
cameras are available for hardware tests; other vendors are covered by code
review and the FauxNY test camera. See the README for the per-model tested list
and the feature matrix.

| Vendor mode | Class | Notes |
| :--- | :--- | :--- |
| Fujifilm (Basic) | `FujifilmBasic` | Older Fujifilm firmware, unsecured pairing. |
| Fujifilm (Secure) | `FujifilmSecure` | Newer Fujifilm firmware that requires the secure handshake. |
| Canon EOS (Smart) | `CanonEOSSmart` | Smart device pairing. Supports GPS, no manual focus. |
| Canon EOS (Remote) | `CanonEOSRemote` | Remote controller (BR-E1 style) pairing. Focus, no GPS. |
| Nikon | `Nikon` | Remote controller mode works (ML-L7 style, shutter only). Smart mode appears but does not pair. |
| Sony | `Sony` | Sony ZV and most modern Sony bodies. |
| Ricoh | `Ricoh` | Ricoh GR IV series. Shutter capture with autofocus and GPS supported. Focus-only control is unsupported. Does not work with GR III or GR II. |
| FauxNY | `FauxNY` | Software test camera. Enabled by the FauxNY setting for development. |

## GPS units

Location tagging uses an M5Stack GPS unit on Grove Port A. Every M5Stack GPS
receiver furble targets is the AT6668/CASIC family, so the existing $PCAS and
NMEA support covers them with no per-unit protocol code. Set
`Settings` > `GPS` > `GPS baud 115200` for the AT6668 units.

| Unit | Chipset | Antenna | furble boards | Wiring | Status |
| :--- | :--- | :--- | :--- | :--- | :--- |
| GPS/BDS Unit v1.1 | AT6668 | Ceramic patch (built in) | All five | Grove Port A | Supported. Indoor reception is marginal because of the patch antenna. |
| Unit-GPS (SMA) | AT6668 | External SMA active antenna | All five | Grove Port A | Supported, drop in, zero wiring change. Recommended fix for weak or indoor reception because of the external antenna. |
| Module GPS v2.1 | AT6668 | SMA | Core, Core2 only | M5-Bus module | Planned, not yet in firmware. Needs a Core/Core2 gated port setting before it works. |
| Atomic GPS Base v2.0 | AT6668 | SMA | None | Atom base | Out of scope. furble targets no Atom board. |

The older Mini GPS/BDS Unit is end of life and runs at 9600 baud.

Source: the M5Stack product pages for each unit (docs.m5stack.com), cross
checked against the firmware GPS support in `src/FurbleGPS.cpp`.

## Related references

- [Settings and controls reference](settings-and-controls.md)
- [Console commands reference](console-commands.md)
- [UI walkthrough](ui-walkthrough.md)
