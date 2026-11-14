# furble supported hardware

This is the reference for the controllers, cameras, and GPS units furble
supports. It is taken from the firmware source and the build configuration, so
it matches the shipped behaviour. For a friendly overview see the top level
[README](../README.md).

## Controllers and build environments

furble builds six release firmware images. M5Unified detects the exact board at
runtime, so one image covers a board family. Every environment also has a
matching `-debug` variant that adds verbose logging and the USB serial console.

| Build env | Chip | Flash | MQTT | Boards it runs on | Touch | Notes |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `m5stick-c` | ESP32 | 4 MB | No | M5StickC | No | End of life. |
| `m5stick-c-plus` | ESP32 | 4 MB | No | M5StickC Plus, M5StickC Plus2 (EOL) | No | Plus and Plus2 share the image. The Plus2 is end of life. |
| `m5stick-s3` | ESP32-S3 | 8 MB | Yes | M5StickS3 | No | Native USB Serial/JTAG. PSRAM enabled. Only board with Sleep while connected and Watchdog. |
| `m5stack-core` | ESP32 | 4 MB | No | M5Stack Core (Basic/Gray) | No | No Auto off or Low battery. |
| `m5stack-core2` | ESP32 | 16 MB | Yes | M5Stack Core2, M5Tough (untested) | Yes | PSRAM enabled. On-screen shutter buttons, touch calibration, power-button screen lock. The firmware detects the M5Tough and branches for it, but it has not been verified on hardware. |
| `waveshare-s3-eth` | ESP32-S3 | 16 MB | Yes | Waveshare ESP32-S3-ETH | No | Display-less wired-Ethernet profile; hardware verification remains outstanding. |

The opt-in HTTPS WebUI is compiled for the four 8 MB/16 MB profiles. The
M5StickS3 is **not yet a supported WebUI target**. With WiFi connected, the
companion service and the display running, it measured 13,543 free / 12,788
largest internal bytes with the WebUI off before plan 305, and 10,607 / 9,716
with the 3 KiB supervisor, which is below the 20,480 / 12,288-byte server
floor. Plan 305 then measured 27,351 free / 25,588 largest with the WebUI off,
above the floor on paper but without a server start. The 8 KiB main-stack
experiment measured 30,007 free / 28,660 largest with MQTT off. Plan 311
restored the 12 KiB main stack, which leaves an inferred 25,911 free, and #407
moved the MQTT event queue to PSRAM. The largest block, a real TLS handshake, a
settings write and a soak are still unmeasured. The runtime gate keeps a
low-memory image closed with a measured log line instead of boot-looping. The
Core2, `esp32-s3-headless` and Waveshare profiles have more headroom but their
WebUI budgets are inferred, not measured. See
[WebUI and REST API](webui.md#memory-budget).

The developer-only `esp32-s3-headless` profile uses the documented 8 MB
ESP32-S3-DevKitC-1 board and also includes MQTT. The 4 MB profiles omit the
MQTT implementation, component dependencies, settings, and console capability.
For an oversized Core debug image, `m5stack-core-usb-debug` is a separate
USB-UART-only fallback with one factory app partition. It is not a supported
release environment, is the mandatory Core debug profile in normal CI, does not
provide wireless updates or rollback, and is not included in the web installer.
The legacy dual-OTA Core debug profile is an explicit workflow-dispatch opt-in.
It fits its OTA slot again after the plan 314 flash slimming.

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
| Fujifilm (Basic) | `FujifilmBasic` | Older firmware with the token pairing service `91F1DE68`. That service is absent from X100VI 1.31 and X-S20 3.30, so no Basic path is present; use Secure. |
| Fujifilm (Secure) | `FujifilmSecure` | Firmware that requires the status and registration handshake. X100VI 1.30 and later, and X-S20 3.30, are this generation. |
| Canon EOS (Smart) | `CanonEOSSmart` | Smart device pairing. Supports GPS, no manual focus. |
| Canon EOS (Remote) | `CanonEOSRemote` | Remote controller (BR-E1 style) pairing. Focus, no GPS. |
| Nikon | `Nikon` | Remote controller mode works (ML-L7 style, shutter only). Smart mode appears but does not pair. |
| Sony | `Sony` | Sony ZV and most modern Sony bodies. |
| Ricoh | `Ricoh` | GR IV and GR IV HDF, firmware 1.20. Shutter capture with autofocus and GPS supported. Focus-only control is unsupported. GR III and GR IIIx expose the same BLE service UUIDs but remain uncertified because power and WLAN behaviour differ. GR II is Wi-Fi only. Numeric-comparison pairing displays the camera's six-digit code on the furble screen and is untested on hardware. |
| Panasonic LUMIX | `Lumix` | Implemented from S5II and BGH1 protocol traces. Shutter and GPS with clock are supported. There is no focus opcode, so focus control is disabled. Untested on hardware. |
| PENTAX K-3 Mark III | `Ricoh` | Untested on hardware. The K-3 Mark III and K-3 Mark III Monochrome are matched by the Ricoh service UUIDs or the PENTAX name and use the Ricoh Imaging protocol path unchanged. The K-1, K-1 Mark II, KP, KF and K-70 connect over Wi-Fi only and are not supported. Shutter capture with autofocus and GPS follow the Ricoh class. Focus-only control is unsupported. |
| DJI Osmo Action | `DJIOsmo` | Untested on hardware. Action 4 and Action 5 Pro, from DJI's published R SDK protocol and ESP32 reference. The shutter toggles video recording. Focus control is disabled and GPS is not implemented. Pairing is DJI's own camera approval, not a Bluetooth passkey. |
| FauxNY | `FauxNY` | Software test camera. Enabled by the FauxNY setting for development. |

## GPS units

Location tagging uses an M5Stack GPS unit on Grove Port A. Every M5Stack GPS
receiver furble targets is the AT6668/CASIC family, so the existing $PCAS and
NMEA support covers them with no per-unit protocol code. The stored default
baud is 9600; select `Auto` or a fixed 115200 under `Settings` > `GPS` >
`GPS Baud` for the AT6668 units.

| Unit | Chipset | Antenna | furble boards | Wiring | Status |
| :--- | :--- | :--- | :--- | :--- | :--- |
| GPS/BDS Unit v1.1 | AT6668 | Ceramic patch (built in) | All five | Grove Port A | Supported. Indoor reception is marginal because of the patch antenna. |
| Unit-GPS (SMA) | AT6668 | External SMA active antenna | All five | Grove Port A | Supported, drop in, zero wiring change. Recommended fix for weak or indoor reception because of the external antenna. |
| Module GPS v2.1 | AT6668 | SMA | Core, Core2 only | M5-Bus module | Planned, not yet in firmware. Needs a Core/Core2 gated port setting before it works. |
| Atomic GPS Base v2.0 | AT6668 | SMA | None | Atom base | Out of scope. furble targets no Atom board. |

The older Mini GPS/BDS Unit is end of life and runs at 9600 baud. `Auto` probes
115200, 9600, 38400, 57600, 19200, and 4800, and requires two checksummed NMEA
sentences before declaring the receiver present. If probing fails, furble marks
the receiver absent, cuts the external rail, and retries once after 60 seconds.

Source: the M5Stack product pages for each unit (docs.m5stack.com), cross
checked against the firmware GPS support in `src/FurbleGPS.cpp`.

## Related references

- [Settings and controls reference](settings-and-controls.md)
- [Console commands reference](console-commands.md)
- [UI walkthrough](ui-walkthrough.md)
- [Candidate hardware catalog](hardware-catalog.md). Board and receiver options
  for a port or a purchase, with GNSS classes, input schemes, and runtime
  estimates. Nothing in the catalog is a support claim.
- [Plan 287](../plans/287-mcu-portability-and-bundle-hardware.md). The porting
  cost, battery model, and GPS fix ladder behind the catalog.
