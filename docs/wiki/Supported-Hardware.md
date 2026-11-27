# Supported hardware

The controllers, cameras, and GPS units furble supports. Taken from the firmware
source and the build configuration. See the project README for the tested camera
list.

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
| `m5stack-core2` | ESP32 | 16 MB | Yes | M5Stack Core2, M5Tough (untested) | Yes | PSRAM enabled. On-screen shutter buttons, touch calibration, power-button screen lock. The M5Tough row enables its microSD slot, NS4168 speaker and battery-backed BM8563 RTC alarm wake from the vendor pinmap and schematic, but no Tough hardware has verified them. |
| `waveshare-s3-eth` | ESP32-S3 | 16 MB | Yes | Waveshare ESP32-S3-ETH | No | Display-less wired-Ethernet node. Wi-Fi and BLE 5 on the SoC, W5500 for wired Ethernet. No PMIC, battery, RTC part, IMU or IR, and no software power-off path. Hardware verification remains outstanding. |

The Waveshare ESP32-S3-ETH is a mains or PoE node. Its board row reports no
battery, no PMIC, no RTC part, no IR emitter, no IMU and no software power-off
path: there is no PMIC, no battery and no switchable rail to cut, so a
power-off request is refused rather than leaving the node in a deep sleep with
no wake source. A microSD slot is wired to GPIO 4 to 7, but no card-select pin
is mapped, so the profile reports no SD slot. Wi-Fi and BLE 5 work alongside
the wired link, and the optional PoE module feeds the same net as USB VBUS, so
firmware cannot tell which supply is present.

The developer-only `esp32-s3-headless` profile uses the documented 8 MB
ESP32-S3-DevKitC-1 board and also includes MQTT. The 4 MB profiles omit the
MQTT implementation, component dependencies, settings, and console capability.

## Cameras

Only Fujifilm cameras are available for hardware tests. Other vendors are covered
by code review and the FauxNY test camera.

| Vendor mode | Notes |
| :--- | :--- |
| Fujifilm (Basic) | Older firmware with the token pairing service `91F1DE68`. That service is absent from X100VI 1.31 and X-S20 3.30, so no Basic path is present; use Secure. |
| Fujifilm (Secure) | Firmware that requires the status and registration handshake. X100VI 1.30 and later, and X-S20 3.30, are this generation. |
| Canon EOS (Smart) | Smart device pairing. Supports GPS and AF-only focus. The AF writes come from Camera Connect evidence and await hardware validation; manual lens focus is not available. |
| Canon EOS (Remote) | Remote controller pairing. Focus, no GPS. |
| Nikon | Remote controller mode works (shutter only). Smart mode appears but does not pair. |
| Sony | Sony ZV and most modern Sony bodies. Two advertisement generations are accepted: 2020 and later bodies set tag 0x22 bit 1 and use the DD30/DD31 location handshake, while the 2019 generation (0xE8 or 0xEC) clears it and gets a direct DD11 write. The 2019 generation is covered by the virtual peer only and is untested on hardware. |
| Ricoh | GR IV and GR IV HDF, firmware 1.20. Shutter capture with autofocus and GPS supported. Focus-only control is unsupported. GR III and GR IIIx expose the same BLE service UUIDs but remain uncertified because power and WLAN behaviour differ. GR II is Wi-Fi only. Numeric-comparison pairing displays the camera's six-digit code on the furble screen and is untested on hardware. A body in BLE standby gets one bounded reconnect wake attempt before a shutter press is refused; whether a reconnect actually wakes a GR IV, and how long it takes, is untested on hardware. |
| Panasonic LUMIX | Implemented from S5II and BGH1 protocol traces. Shutter and GPS with clock are supported. There is no focus opcode, so focus control is disabled. Untested on hardware. |
| PENTAX K-3 Mark III | Untested on hardware. The K-3 Mark III and K-3 Mark III Monochrome are matched by the Ricoh service UUIDs or the PENTAX name and use the Ricoh Imaging protocol path unchanged. The K-1, K-1 Mark II, KP, KF and K-70 connect over Wi-Fi only and are not supported. Shutter capture with autofocus and GPS follow the Ricoh class. Focus-only control is unsupported. |
| DJI Osmo Action | Untested on hardware. Action 4 and Action 5 Pro, from DJI's published R SDK protocol and ESP32 reference. The shutter toggles video recording. Focus control is disabled and GPS is not implemented. Pairing is DJI's own camera approval, not a Bluetooth passkey. |
| FauxNY | Software test camera for development. |

## GPS units

Location tagging uses an M5Stack GPS unit on Grove Port A. M5Stack's own
receivers are the AT6668/CASIC family, which the $PCAS and NMEA path covers with
no per-unit protocol code. A u-blox M10 receiver is detected at GPS start by a
UBX-MON-VER poll and configured over UBX; any other receiver keeps the CASIC and
NMEA path. The stored default
baud is 9600; select `Auto` or a fixed 115200 under `Settings` > `GPS` >
`GPS Baud` for the AT6668 units.

| Unit | Chipset | Antenna | furble boards | Wiring | Status |
| :--- | :--- | :--- | :--- | :--- | :--- |
| GPS/BDS Unit v1.1 | AT6668 | Ceramic patch | All five | Grove Port A | Supported. Indoor reception is marginal. |
| Unit-GPS (SMA) | AT6668 | External SMA active antenna | All five | Grove Port A | Supported, drop in. Recommended for weak or indoor reception. |
| Module GPS v2.1 | AT6668 | SMA | Core, Core2 only | M5-Bus module | Planned, not yet in firmware. |
| Atomic GPS Base v2.0 | AT6668 | SMA | None | Atom base | Out of scope. furble targets no Atom board. |
| Generic M10 UART module | u-blox MAX-M10S or other M10 | Module dependent | All five | Grove Port A, 3.3 V logic | Simulator-tested, hardware-untested. MAX-M10S runs at 9600 baud and MIA-M10Q at 38400, so select `Auto`. |
| LilyGO T-Beam Supreme M10S | u-blox MAX-M10S | SMA | Not a furble board yet | On board | Receiver supported, board not ported. See plan 318. |
| LilyGO T-Watch Ultra | u-blox MIA-M10Q | Built in | `lilygo-twatch-ultra` | On board | Ported in plan 387. The GNSS is on the UBX path through the pin seam; the receiver is on ALDO1/BLDO1 rails the image brings up. |
| LilyGO T-LoRa Pager | u-blox MIA-M10Q | Built in | Not a furble board yet | On board | Receiver supported, board not ported. See plan 387. |

The M5Stack Core and the Waveshare ESP32-S3-ETH have no GPS UART. Grove
Port A on the Core is the internal I2C bus that carries the IP5306 and the
MPU6886, so it cannot host a UART, and GPIO13 on the Waveshare is the W5500
SPI clock. Both report the receiver absent, and the GPS setting does not
create one.

The older Mini GPS/BDS Unit is end of life and runs at 9600 baud. `Auto` probes
115200, 9600, 38400, 57600, 19200, and 4800, and requires two checksummed NMEA
sentences before declaring the receiver present. If probing fails, furble marks
the receiver absent, cuts the external rail, and retries once after 60 seconds.

The u-blox M10 support covers detection, NAV-PVT fix and accuracy,
constellation selection, update rate, dynamic model, cyclic tracking and on/off
power save, RXM-PMREQ software standby, AssistNow Autonomous and UPD-SOS
save-on-shutdown. It is verified against a virtual M10 in the simulator only; no
M10 hardware has been tested. A board that keeps the receiver's V_BCKP supplied
defines `FURBLE_GPS_BACKUP_SUPPLY`; no supported board does.

## Related pages

- [Settings Reference](Settings-Reference)
- [Console Commands](Console-Commands)
- [UI Walkthrough](UI-Walkthrough)
