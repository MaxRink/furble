# furble supported hardware

This is the reference for the controllers, cameras, and GPS units furble
supports. It is taken from the firmware source and the build configuration, so
it matches the shipped behaviour. For a friendly overview see the top level
[README](../README.md).

## Controllers and build environments

furble defines eight firmware environments. Six are the release matrix, and
M5Unified detects the exact board at runtime, so one M5 image covers a board
family. An image that runs on exactly one board pins it at build time instead,
which skips detection and drops the other boards' probes and display drivers.
The other two are LilyGO boards, which M5Unified cannot detect at all: their
row, panel, input devices and rails come from `include/FurbleLilyGo.h` and the
seams it feeds. **Those two are not in the CI or release matrix yet**: neither
has been built, so neither has a recorded size or stack budget, and the table
below describes the code as written rather than a shipped image. Every
environment except `lilygo-twatch-ultra` has a matching `-debug` variant that
adds verbose logging and the USB serial console.
furble builds six release firmware images and one bench image. M5Unified
detects the exact M5 board at runtime, so one image covers a board family. An
image that runs on exactly one board pins it at build time instead, which skips
detection and drops the other boards' probes and display drivers. The LilyGO
T-Beam Supreme is not an M5 board and is not a release image: its environment
names the compile-time row and drives the AXP2101 and the SH1106 OLED itself.
Every environment also has a matching `-debug` variant that adds verbose
logging and the USB serial console.

| Build env | Chip | Flash | MQTT | Boards it runs on | Touch | Notes |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `m5stick-c` | ESP32 | 4 MB | No | M5StickC | No | End of life. |
| `m5stick-c-plus` | ESP32 | 4 MB | No | M5StickC Plus, M5StickC Plus2 (EOL) | No | Plus and Plus2 share the image. The Plus2 is end of life. |
| `m5stick-s3` | ESP32-S3 | 8 MB | Yes | M5StickS3 | No | Native USB Serial/JTAG. PSRAM enabled. Only board with Sleep while connected and Watchdog. |
| `m5stack-core` | ESP32 | 4 MB | No | M5Stack Core (Basic/Gray) | No | No Auto off or Low battery. No IR emitter, no RTC, no software power off. |
| `m5stack-core2` | ESP32 | 16 MB | Yes | M5Stack Core2, M5Tough (untested) | Yes | PSRAM enabled. On-screen shutter buttons, touch calibration, power-button screen lock. The firmware detects the M5Tough and branches for it, but it has not been verified on hardware. The Tough shares the Core2 SD slot, speaker and BM8563; no backup cell is recorded for its RTC. |
| `waveshare-s3-eth` | ESP32-S3 | 16 MB | Yes | Waveshare ESP32-S3-ETH | No | Display-less wired-Ethernet node pinned to the display-less StampS3, so no M5 probe touches the W5500 pins. Wi-Fi and BLE 5 on the SoC, W5500 for wired Ethernet. No PMIC, battery, RTC part, IMU or IR, and no software power-off path. Hardware verification remains outstanding. |
| `lilygo-tdeck` | ESP32-S3 | 16 MB | Yes | LilyGO T-Deck and T-Deck Plus | No | A 2.8 inch 320x240 ST7789, an ESP32-C3 keyboard over I2C, a five-switch trackball, a MAX98357A speaker and a microSD slot. No PMIC and no gauge: the battery divider on GPIO4 is not read yet, so no battery level or voltage is shown. No software power-off, no IR, no IMU, no RTC part. GPS pins are the Grove UART the T-Deck Plus puts its MIA-M10Q on. |
| `lilygo-twatch-ultra` | ESP32-S3 | 16 MB | Yes | LilyGO T-Watch Ultra | No | A 2.06 inch 502x410 CO5300 AMOLED over quad SPI, an AXP2101 whose rails this image brings up, an MIA-M10Q GNSS, a MAX98357A speaker and a microSD slot. **Input is one button**: the CST9217 touch controller has no driver, so the UI can activate the focused row but not move focus. No battery level, voltage or charging state is reported and there is no software power-off, because the AXP2101 power backend is a separate change. The BHI260AP and the DRV2605 have no backend. |
| `lilygo-tbeam-supreme` | ESP32-S3 | 8 MB | Yes | LilyGO T-Beam Supreme, L76K or MAX-M10S trim | No | Bench port with no device run yet. 8 MB quad PSRAM, 8 MB flash, AXP2101 battery level, voltage and charge direction, 128x64 1-bit SH1106 OLED, GPS on UART RX 9 / TX 8, 32.768 kHz crystal on XTAL_32K. No touch, IR, LED, speaker, vibration motor or SD mount. |

The LilyGO T-Beam Supreme is the first non-M5 board. M5Unified cannot detect
it, so the image pins M5GFX at the display-less StampS3 family only to take the
no-probe return, and `Board::compiledProfile()` answers with the T-Beam row
rather than with whatever M5Unified reports. The AXP2101 runs through the
`lib/hal` register driver over the `HalBus` seam on the board's own I2C bus 1
(SDA 42, SCL 41); the OLED runs through M5GFX's `Panel_SH110x` on bus 0 (SDA 17,
SCL 18) with its address probed at 0x3C and then 0x3D, and is drawn at its real
128x64 geometry with 24 px menu icons. ALDO4 is the receiver's supply and the
only rail furble switches; ALDO1 (OLED, BME280, magnetometer and their I2C
pull-ups) is never cut. No enable pin is driven, because GPIO7 is the L76K's
WAKE_UP while pin 5 of the MAX-M10S is EXTINT, and an image cannot tell the
trims apart. The microSD slot is on its own SPI bus and is not mounted, the
SX1262 radio is left unpowered rather than slept, the QMI8658 IMU has no
backend, the RTC part is not read because the vendor wiki and the schematic
disagree on it (PCF85063ATL against PCF8563), and the row keeps the
conservative RTC-backing and GPS-backup answers. Every one of those is a bench
item in [plan 390](../plans/390-tbeam-supreme-port.md), not a measured result.

The Waveshare ESP32-S3-ETH is a mains or PoE node rather than a handheld. Its
board row reports no battery and no PMIC, so `status` shows battery level and
voltage as unknown and never infers USB or PoE power. It reports no software
power-off path: there is no PMIC, no battery and no switchable rail, so
`Platform::powerOff()` logs a refusal and returns failure instead of falling
through to a deep sleep start, which arms no wake source on this board and
would leave the node asleep until a power cycle rather than remove power. The
board has no RTC part, no IR emitter and no IMU. A microSD slot is wired to
GPIO 4 to 7, but no card-select pin is mapped, so the profile reports no SD
slot while the image still compiles the SD stack. Wi-Fi and BLE 5 are
available on the SoC alongside the wired link. PoE is an optional module that
feeds POE_5V, the same net as VBUS, so firmware cannot claim either source.

Flash figures are the build configuration, not the current retail part. The
`m5stack-core` environments are built for 4 MB (`CONFIG_ESPTOOLPY_FLASHSIZE_4MB`
and `partitions_two_ota_large.csv`), while current Core Basic units ship 16 MB;
the simulator carries both as `core-4mb` and `core-16mb` so a scenario states
which one it models. The Core family has no IR emitter and the Core has no RTC
chip; the IP5306 reports a charge-state bit but no voltage or current.

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
release environment, does not provide wireless updates or rollback, and is not
included in the web installer. Normal CI builds two mandatory Core debug
profiles: this one and the dual-OTA `m5stack-core-debug`, which fits its OTA
slot again after the plan 314 flash slimming and keeps the full
`ESP_LOG_VERBOSE` log maximum by compiling out the BMI270 IMU driver that the
Core, whose part is an MPU6886 or MPU9250, can never probe (plans/347).

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
| Sony | `Sony` | Sony ZV and most modern Sony bodies. Two advertisement generations are accepted: the 2020 and later bodies set tag 0x22 bit 1 and use the DD30/DD31 location handshake, while the 2019 generation (0xE8 or 0xEC) clears it and gets a direct DD11 write. The location packet needs a 98-byte ATT MTU; the connect path asks for 158 and skips the write instead of failing when the link is narrower. The 2019 generation is covered by the virtual peer only and is untested on hardware. See [plan 359](../plans/359-sony-legacy-generation.md). |
| Ricoh | `Ricoh` | GR IV and GR IV HDF, firmware 1.20. Shutter capture with autofocus and GPS supported. Focus-only control is unsupported. GR III and GR IIIx expose the same BLE service UUIDs but remain uncertified because power and WLAN behaviour differ. GR II is Wi-Fi only. Numeric-comparison pairing displays the camera's six-digit code on the furble screen and is untested on hardware. A body in BLE standby gets one bounded reconnect wake attempt before a shutter press is refused; whether a reconnect actually wakes a GR IV, and how long it takes, is untested on hardware. See [the Ricoh standby gate and wake](protocols/camera-remotes.md#ricoh-standby-gate-and-wake). |
| Panasonic LUMIX | `Lumix` | Implemented from S5II and BGH1 protocol traces. Shutter and GPS with clock are supported. There is no focus opcode, so focus control is disabled. Untested on hardware. |
| PENTAX K-3 Mark III | `Ricoh` | Untested on hardware. The K-3 Mark III and K-3 Mark III Monochrome are matched by the Ricoh service UUIDs or the PENTAX name and use the Ricoh Imaging protocol path unchanged. The K-1, K-1 Mark II, KP, KF and K-70 connect over Wi-Fi only and are not supported. Shutter capture with autofocus and GPS follow the Ricoh class. Focus-only control is unsupported. |
| DJI Osmo Action | `DJIOsmo` | Untested on hardware. Action 4 and Action 5 Pro, from DJI's published R SDK protocol and ESP32 reference. The shutter toggles video recording. Focus control is disabled and GPS is not implemented. Pairing is DJI's own camera approval, not a Bluetooth passkey. |
| FauxNY | `FauxNY` | Software test camera. Enabled by the FauxNY setting for development. |

## GPS units

Location tagging uses a GPS unit on Grove Port A. M5Stack's own receivers are
the AT6668/CASIC family, which the $PCAS and NMEA path covers with no per-unit
protocol code. At GPS start furble sends three identification probes in one
window: a UBX-MON-VER poll, `$PQTMVERNO` for a Quectel part and `$PDTINFO` for a
Unicore part. Whichever one answers decides the configuration backend, and a
receiver that answers none keeps the CASIC and NMEA path with no extra delay.
The stored default baud is 9600; select `Auto` or a fixed 115200 under
`Settings` > `GPS` > `GPS Baud` for the AT6668 units.

Which signal bands a u-blox receiver implements is measured, not assumed. After
MON-VER names the firmware family, furble asks the receiver for the four L5
signal keys with one read-only request: an L1-only M10 refuses it and stays on
L1 for the session, and a dual-band F10 answers with the keys and is configured
for both bands of each enabled constellation. The answer appears as `bands` in
`gps ubx` and as `ubx l1` or `ubx l1+l5` on the GPS receiver page.

| Unit | Chipset | Antenna | furble boards | Wiring | Status |
| :--- | :--- | :--- | :--- | :--- | :--- |
| GPS/BDS Unit v1.1 | AT6668 | Ceramic patch (built in) | All five | Grove Port A | Supported. Indoor reception is marginal because of the patch antenna. |
| Unit-GPS (SMA) | AT6668 | External SMA active antenna | All five | Grove Port A | Supported, drop in, zero wiring change. Recommended fix for weak or indoor reception because of the external antenna. |
| Module GPS v2.1 | AT6668 | SMA | Core, Core2 only | M5-Bus module | Planned, not yet in firmware. Needs a Core/Core2 gated port setting before it works. |
| Atomic GPS Base v2.0 | AT6668 | SMA | None | Atom base | Out of scope. furble targets no Atom board. |
| Generic M10 UART module | u-blox MAX-M10S or other M10 | Module dependent | All five | Grove Port A, 3.3 V logic | Simulator-tested, hardware-untested. MAX-M10S runs at 9600 baud and MIA-M10Q at 38400, so select `Auto`. |
| LilyGO T-Beam Supreme M10S | u-blox MAX-M10S | SMA | Not a furble board yet | On board | Receiver supported, board not ported. See plan 318. |
| LilyGO T-Watch Ultra | u-blox MIA-M10Q | Built in | `lilygo-twatch-ultra` | On board | Ported in plan 387. The GNSS is on the UBX path through the pin seam; the receiver is on ALDO1/BLDO1 rails the image brings up. |
| LilyGO T-LoRa Pager | u-blox MIA-M10Q | Built in | Not a furble board yet | On board | Receiver supported, board not ported. See plan 387. |
| Generic dual-band UART module | u-blox NEO-F10N or other F10 | Dual-band L1/L5 antenna required | All five | Grove Port A, 3.3 V logic | Simulator-tested, hardware-untested. Both bands of each constellation run together and there is no power save mode. See [ublox-neo-f10n.md](components/ublox-neo-f10n.md). |
| LilyGO T-Beam Supreme M10S | u-blox MAX-M10S | SMA | T-Beam Supreme image | On board, RX 9 / TX 8 | Simulator-tested, hardware-untested. Detected by the UBX-MON-VER poll; the factory default 38400 baud needs `Auto` or an explicit 38400. GPIO7 is not an enable on this trim and furble drives no enable pin. |
| LilyGO T-Beam Supreme L76K | Quectel L76K, `$PCAS` | SMA | T-Beam Supreme image | On board, RX 9 / TX 8 | Simulator-tested, hardware-untested. Keeps the NMEA and `$PCAS` backend. GPIO7 is this trim's WAKE_UP input and furble leaves it undriven. |
| Quectel LC76G or LC29H module, or a breakout of one | Quectel LC76G, LC29H | Module dependent | All five | Grove Port A, 3.3 V logic | Simulator-tested, hardware-untested. Detected by `$PQTMVERNO`; configured over `$PAIR` and `$PQTM`. |
| Unicore UC6580 module, or a breakout of one | Unicore UC6580 | Module dependent | All five | Grove Port A, 3.3 V logic | Simulator-tested, hardware-untested. Detected by `$PDTINFO`; configured over `$CFG`. |

The M5Stack Core and the Waveshare ESP32-S3-ETH have no GPS UART. Grove
Port A on the Core is the internal I2C bus that carries the IP5306 and the
MPU6886, so it cannot host a UART, and GPIO13 on the Waveshare is the W5500
SPI clock. Both report the receiver absent, and the GPS setting does not
create one.

The older Mini GPS/BDS Unit is end of life and runs at 9600 baud. `Auto` probes
115200, 9600, 38400, 57600, 19200, and 4800, and requires two checksummed NMEA
sentences before declaring the receiver present. If probing fails, furble marks
the receiver absent, cuts the external rail, and retries once after 60 seconds.

The u-blox support covers detection, NAV-PVT fix and accuracy, constellation
selection, signal band capability, update rate, dynamic model, cyclic tracking
and on/off power save where the firmware has it, RXM-PMREQ software standby,
AssistNow Autonomous and UPD-SOS save-on-shutdown. The M10 firmware implements
L1 signals only; the F10 firmware adds GPS L5, Galileo E5a, BeiDou B2a and QZSS
L5, and has no GLONASS or power-save keys at all. It is verified against a
virtual receiver in the simulator only, both variants; no u-blox hardware has
been tested. A board that keeps the receiver's V_BCKP supplied defines
`FURBLE_GPS_BACKUP_SUPPLY`; no supported board does.
the T-Beam Supreme is the first whose vendor states that the 18650 backs the

The Quectel support covers detection, fix rate (`$PAIR050`), the NMEA sentence
set (`$PAIR062`), the constellation mask (`$PAIR066`), SBAS (`$PAIR410`), the
navigation mode (`$PAIR080`), timed backup mode (`$PAIR650`), restart and the
`$PAIR513` and `$PQTMSAVEPAR` save on shutdown. Two gaps are reported rather than
hidden: `PAIR650` has no setting below 10 seconds, so the 5 second duty interval
is refused, and the LC29H (BA, CA, DA, EA) variants do not implement `PAIR066`,
so a constellation change there comes back refused. The Unicore support covers
detection, fix rate (`$CFGNAV`), the NMEA sentence set (`$CFGMSG`), the
constellation mask (`$CFGSYS`), the dynamic platform model (`$CFGDYN`, Portable
and Stationary only), restart and `$CFGSAVE`. UC R1.5 documents no standby
command, so that setting is reported refused on that part, and Pedestrian and
Vehicle have no `$CFGDYN` counterpart and are refused too. Both paths are
verified against virtual receivers in the simulator only; no Quectel or Unicore
hardware has been tested.

Source: the M5Stack product pages for each unit (docs.m5stack.com), cross
checked against the firmware GPS support in `src/FurbleGPS.cpp`. The u-blox M10
documents are listed in `plans/implemented/321-ublox-m10-driver.md`, and the
dual-band ones in `plans/512-dual-band-l5.md`. The two text protocols are in
[docs/protocols/gnss-quectel-pair.md](protocols/gnss-quectel-pair.md) and
[docs/protocols/gnss-unicore-cfg.md](protocols/gnss-unicore-cfg.md), and the
driver plan is [plans/388-gnss-vendor-text.md](../plans/388-gnss-vendor-text.md).

## Related references

- [Component reference](components/README.md). One page per chip with the bus, address,
  registers and timing the firmware relies on, and [the board map](components/board-map.md)
  for every pin and address.
- [Protocol reference](protocols/README.md). UBX, CASIC and `$PCAS`, NMEA, the camera
  remote services, IR, MQTT and the companion GATT service.
- [Settings and controls reference](settings-and-controls.md)
- [Console commands reference](console-commands.md)
- [UI walkthrough](ui-walkthrough.md)
- [Candidate hardware catalog](hardware-catalog.md). Board and receiver options
  for a port or a purchase, with GNSS classes, input schemes, and runtime
  estimates. Nothing in the catalog is a support claim.
- [Plan 287](../plans/287-mcu-portability-and-bundle-hardware.md). The porting
  cost, battery model, and GPS fix ladder behind the catalog.
