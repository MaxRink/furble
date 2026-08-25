# 42 - Waveshare ESP32-S3-ETH wired MQTT node and display-less onboarding

Status: board profile and Ethernet transport implemented in PR #170. The
transport is ready for simulator and host verification. Real Waveshare bench
verification remains outstanding. Extends the WiFi/MQTT track in
`plans/33-wifi-hub.md` and the companion and security design in
`plans/50-companion-app-design.md`. Listed as a wired-network-node board in
`plans/41-alternative-hardware.md`.

Relates to gkoh/furble WiFi/MQTT track (plan 33).

## Scope

Add the Waveshare ESP32-S3-ETH as a headless wired-Ethernet furble board. Its
one job is to be a rock-solid Home Assistant MQTT node on a wired link, and to
have a secure onboarding path even though it has no screen. Wired Ethernet is
the reliability case that plan 33 wanted: a fixed studio or home install that
may use USB-C/external power or the optional PoE add-on, always on, and driven
from Home Assistant.

The board transport in this PR depends on two pieces of plan 33 landing first:

- The headless build unblock, plan 33a (PR-A). The display-less profile and the
  headless main loop are a hard prerequisite. This board has no display at all.
- The transport-agnostic MQTT client, plan 33c (PR-C). The client must key on a
  generic IP-up event and the default netif, not on WiFi, so an Ethernet link
  feeds it the same way a WiFi link does. This is the single cross-dependency
  and it is called out again under Dependencies.

The host seam models the same link-up, DHCP IP, link-down and reconnect events
without a W5500. It exposes a transport-agnostic network-up callback for the
MQTT client and rejects empty or stale IP events. MQTT and Home Assistant
discovery stay owned by plan 33c and are not duplicated in this board PR.

Ethernet is a transport peer of WiFi here, not a competitor. A wired link means
the plan 33 BLE and WiFi coexistence tax does not apply, so this board is the
better low-latency hub.

## Board facts

Waveshare ESP32-S3-ETH. Verified against the Waveshare product page, wiki and
schematic listed under References. Confirm the pin table against the schematic
PDF on real hardware before the board profile is trusted.

- MCU: ESP32-S3R8, Xtensa LX7 dual core up to 240 MHz, 512 KB SRAM, 8 MB octal
  PSRAM wired on GPIO33 to GPIO37, 16 MB flash.
- Radios: WiFi and BLE5. Both stay available. BLE is the onboarding channel.
- Ethernet: Wiznet W5500 over SPI, 10/100. The ESP32-S3 has no internal EMAC,
  so an external SPI MAC plus PHY is mandatory. The W5500 is that part.
- Power: the ESP32-S3-ETH is the base board; the Waveshare PoE Module (B) is an
  optional external add-on (IEEE 802.3af). The `ESP32-S3-POE-ETH` listing is a
  board-plus-module bundle, not a different built-in-PoE board. Use one power
  source at a time during bench testing; do not connect PoE and USB together.
- No display, no IMU, no RTC, no battery.
- Buttons: BOOT on GPIO0 is a strapping pin, reused here as the onboarding and
  factory-reset button. It is the only button.
- Status surface: one WS2812 RGB LED on GPIO21. That is the only status output.
- USB: native USB-Serial/JTAG on GPIO19 and GPIO20. This is the console path.
- Extras: microSD in 1-line mode, a camera header, a Pico-compatible header.
- Reserved: GPIO33 to GPIO37 belong to the octal PSRAM. Do not remap them.

## Power-source observability

PoE is not built into the ESP32-S3-ETH base board. The optional module supplies
the `POE_5V` rail through the PoE expansion header. The published schematic has
no MCU-readable HAT-presence, PoE-capability, or PoE-negotiation signal, so
firmware must not claim to detect any of those facts. Ethernet link state is a
W5500/PHY observation and is not evidence that PoE is available; USB VBUS is a
separate external-power observation.

Production power-source status therefore remains unknown unless a later board
revision documents a real sense signal. The host-only `poe-power-model` fixture
uses explicit independent observations with a no-HAT/PoE-unavailable/link-down/
USB-absent default. It covers no-HAT USB power, a present/capable HAT without
negotiated power, negotiated PoE, power loss/recovery, and unknown observations;
it is not a runtime hardware detector.

## W5500 SPI pins

Corroborated by three sources. These MUST be confirmed against the schematic
GPIO table before the profile is trusted, because a wrong pin here is a board
that never links.

| Signal | GPIO |
|---|---|
| SCLK | 13 |
| MOSI | 11 |
| MISO | 12 |
| CS | 14 |
| INT | 10 |
| RST | 9 |

None of these collide with the octal PSRAM pins (33 to 37) or the native USB
pins (19, 20). The W5500 INT line can be flaky in the field. The esp_eth driver
supports interrupt-less polling as a fallback: set `int_gpio_num = -1` and a
`poll_period_ms`. Keep that fallback in mind before blaming the link.

## esp_eth W5500 bring-up

Build config: `CONFIG_ETH_USE_SPI_ETHERNET=y` plus
`CONFIG_ETH_SPI_ETHERNET_W5500=y`, or pull the `espressif/w5500` managed
component. The ESP-IDF `ethernet_init` component and the ethernet basic example
are the reference. Sequence:

1. `spi_bus_initialize(SPI2_HOST)` with MOSI on 11, MISO on 12, SCLK on 13.
2. SPI device in mode 0, 20 to 36 MHz, CS on 14.
3. `eth_w5500_config` with `int_gpio_num = 10`, or `-1` with a poll period as
   the fallback.
4. `esp_eth_mac_new_w5500` and `esp_eth_phy_new_w5500` with
   `reset_gpio_num = 9`.
5. `esp_eth_driver_install`.
6. Set the MAC from eFuse. The W5500 has no factory MAC, so read one with
   `esp_read_mac(mac, ESP_MAC_ETH)` and apply it with
   `esp_eth_ioctl(ETH_CMD_S_MAC_ADDR)`.
7. esp_netif glue: `ESP_NETIF_DEFAULT_ETH`, attach the eth netif glue, register
   `ETH_EVENT` and `IP_EVENT_ETH_GOT_IP`, then `esp_eth_start`.

On `IP_EVENT_ETH_GOT_IP`, signal the same network-up path the MQTT client keys
on. Do not add a second path.

## Files

- New `src/FurbleEthernet.cpp` and `include/FurbleEthernet.h`. A singleton
  shaped like `FurbleWiFi` from plan 33b.
- `Ethernet::init()` is called from `app_main` after `Settings::init()` and
  after the single guarded `esp_netif_init()` and
  `esp_event_loop_create_default()` that plan 33b owns. Do not double-init those
  process-global calls.
- `src/CMakeLists.txt`: `PRIV_REQUIRES` gains `esp_eth`.
- If `sim/build.sh` keeps an explicit source list, add `FurbleEthernet.cpp`
  there too.
- Console: add an `eth status` command that prints link, IP, speed and duplex,
  mirroring `wifi status`.

## PlatformIO env

New `[env:waveshare-s3-eth]` that extends `[env:esp32-s3-headless]` from plan
33a.

- `board = esp32-s3-devkitc-1`, or a committed
  `boards/waveshare-esp32-s3-eth.json` describing 16 MB flash and 8 MB octal
  PSRAM. The board-json is the cleaner long-term choice per plan 41 and is the
  preferred path.
- Own `sdkconfig.waveshare-s3-eth`. This is a sixth sdkconfig file. The five
  release sdkconfigs stay untouched.
- `build_flags` add `-DFURBLE_WAVESHARE_S3_ETH -DFURBLE_ETHERNET`.

sdkconfig deltas from the headless base:

- `CONFIG_ETH_USE_SPI_ETHERNET=y` and `CONFIG_ETH_SPI_ETHERNET_W5500=y` on.
- `CONFIG_ETH_USE_ESP32_EMAC` off. There is no internal EMAC on the S3.
- `CONFIG_ESPTOOLPY_FLASHSIZE_16MB`.
- `CONFIG_SPIRAM_MODE_OCT` for the octal PSRAM.
- Keep NimBLE, esp-mqtt, esp_tls and `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE`.
- Drop LVGL and icons through `FURBLE_NO_DISPLAY`.
- Disable automatic light sleep and hold a fixed APB and CPU frequency. This
  headless Ethernet node has no battery path, regardless of whether its supply
  is USB/external or the optional PoE add-on. A fixed frequency also sidesteps
  the DFS clock-family trap for the W5500 SPI, the same trap the GPS UART hit.

## Subsystems off or stubbed

- Display, LVGL, UI and icons: out through `FURBLE_NO_DISPLAY`. Hard dependency
  on plan 33a.
- `M5.begin()` on a bare S3-ETH is the one unproven assumption, the same one
  plan 33a has on a devkit. Fallback: skip M5Unified under
  `FURBLE_WAVESHARE_S3_ETH` and drive `Platform::tick()` from `esp_timer`.
- No IMU, no M5PM1, no battery. Do not synthesize a PoE/USB source state or a
  battery percentage: the source is unknown to firmware unless hardware later
  provides a documented sense signal. Any MQTT power fields must preserve that
  unknown rather than claiming external power.
- GPS default off. There is no Grove port. GPS is optional through the Pico
  header. With light sleep off, the S3 UART clock issue that forces
  `setSleep(false)` elsewhere is moot here.
- Only BOOT on GPIO0 as a button. WS2812 on GPIO21 as the only status output.

## Secure onboarding, display-less

### Threat model

- LAN attacker: sniffs or spoofs plaintext MQTT, fires the shutter.
- BLE-range attacker: advertises, connects, pairs, sniffs.
- Power-cycle attacker: equivalent to reflashing, outside the software trust
  boundary.
- Replay.
- MQTT MITM.

### Channel

The onboarding channel is BLE, reusing the plan 50 companion GATT service. It is
NOT any device-side LAN socket. Plan 33d already rejected a device REST surface,
and the same logic applies to onboarding.

### Association model

No screen kills LE Secure Connections numeric comparison and passkey entry, the
model plan 50 uses on a device with a display. That leaves Just Works
(encrypted, unauthenticated) or out-of-band.

Chosen default: physical-presence TOFU (trust on first use) as the trust anchor,
plus LE Secure Connections, plus an app-level SRP6a proof-of-possession
handshake (protocomm security2) so the shared secret never crosses the wire. The
web installer can inject a real per-device PoP when that flashing path is used,
because the browser is the missing display.

### Flows

The RGB LED is the status surface:

- blue blink: pairing window open.
- green: bonded.
- solid green: MQTT up.
- red: error.

Flows:

- First boot opens a single-bond pairing window for 120 s.
- Re-pair: a short BOOT press opens a fresh single window and keeps the MQTT
  config. Companion bonds are capped at 1, so per plan 50 a new phone replaces
  the old one.
- Factory reset: a BOOT long-press over 10 s wipes the bond, the PoP and the
  MQTT config, then reopens the first-boot window.

Do not gate onboarding on flash encryption or secure boot. Both are heavy and
irreversible. TOFU needs no stored secret. Offer the web-installer PoP and
optional flash encryption as an advanced hardening path only.

## MQTT security

- TLS is required even on Ethernet. Use `mqtts://host:8883` through esp_tls with
  server-certificate validation against `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE`.
- Self-signed Mosquitto or Home Assistant brokers: the onboarding app delivers
  the broker CA over the encrypted GATT link and furble pins it.
- Client auth: a per-device username and password delivered during onboarding
  (`MQTT_USER` and `MQTT_PASS`, plan 33c). An optional client certificate is
  later work.
- Home Assistant discovery topics have no separate auth. The broker ACL plus the
  delivered credential gate them.
- Plaintext `mqtt://` is available only as an explicit, warned opt-out for an
  isolated segment.

## Companion app

The companion app is `companion/android`, Kotlin, from plan 50. This board adds
a screenless onboarding flow to it:

- A PoP field for the web-installer path, or the button-TOFU flow that prompts
  "press the button on the device" and then confirms the resolved `furble-xxxx`
  id.
- An SRP6a protocomm-security2 client that runs before the privileged
  characteristics unlock.
- An MQTT-config-over-GATT push: broker URI, username and password, base topic,
  and the broker CA for pinning.
- RPA identity pinning through the distributed IRK, as plan 50 already
  describes.

Reuse the plan 50 status and trigger characteristics unchanged.

## Dependencies

- Hard dependency on the plan 33a headless unblock. No display exists on this
  board.
- The plan 33c MQTT client MUST be transport-agnostic. It has to key on a
  generic `IP_EVENT` and the default netif, not on WiFi, so Ethernet feeds it
  equally. This is the single cross-dependency and it is the one thing to get
  right in 33c for this board to work.
- Ethernet is a transport peer of WiFi, not a competitor. A wired link means the
  plan 33 BLE and WiFi coexistence tax does not apply, so this board is the
  better low-latency hub.

## Risks

1. The W5500 SPI pins need on-hardware schematic confirmation. INT flakiness is
   handled by the polling fallback (`int_gpio_num = -1` plus a poll period).
2. A real per-device secret would need flash encryption plus secure boot, which
   are heavy and irreversible. This is why TOFU is the default trust anchor.
3. Concurrent RAM: BLE with up to 9 connections, lwIP, esp-mqtt and roughly
   20 KB of esp_tls at once. The S3 has 512 KB SRAM plus 8 MB PSRAM, which has
   headroom. Route TLS and other large allocations to PSRAM, keep SPI and DMA
   buffers internal, and measure free heap on hardware.
4. `M5.begin()` on a bare S3-ETH is unproven. Fallback is the `esp_timer` tick
   path.
5. DFS and light sleep are avoided by holding a fixed frequency. This is a
   documented decision, not an oversight, and it also protects the W5500 SPI
   clock.

## Implementation status

- `boards/waveshare-esp32-s3-eth.json` describes the 16 MB flash and octal
  PSRAM board instead of borrowing the 8 MB, no-PSRAM DevKit profile.
- `partitions_waveshare_s3_eth.csv` keeps NVS at `0x9000`, otadata at `0xf000`,
  and provides two 6 MiB OTA slots beginning at `0x20000` and `0x620000`.
- `Ethernet::init()` is called during application startup after the platform
  and companion services are ready. It initializes the process-wide netif and
  event loop idempotently, then starts the W5500 transport.
- `Ethernet::stop()` unregisters handlers and releases the SPI, MAC, PHY,
  netif and driver resources. Host tests exercise stop, reinitialization,
  duplicate IP suppression, stale IP rejection and link loss.
- PlatformIO CI and release/page matrices build the board and use its 6 MiB
  slot when reporting firmware size.

## Verification

Hardware-gated. Needs a physical Waveshare ESP32-S3-ETH board.

The following can be verified without the board: host Ethernet state tests,
the complete simulator suite, the Waveshare PlatformIO release and debug
builds, partition image offsets and the web-installer manifest. A real board
is still required for the gates below.

1. W5500 link comes up and DHCP assigns an address.
2. `M5.begin()` behaves on a bare S3-ETH, or the `esp_timer` fallback is used.
3. Measure free heap with BLE and Ethernet both active.
4. Button-TOFU onboarding end to end: pairing window, SRP6a handshake, MQTT
   config push, bond persists across reboot.
5. MQTT over TLS to a real Mosquitto or Home Assistant broker, with a pinned
   self-signed CA.
6. Fujifilm shutter trigger fires. Fujifilm is the only vendor available on
   hardware. Other vendors get code review plus the FauxNY test camera and are
   declared untested, per the repo rule.
7. With no PoE add-on, verify USB-C/external power and Ethernet independently.
8. With the optional Waveshare PoE Module (B), verify IEEE 802.3af negotiation,
   link-up, measured `POE_5V`, power loss, and recovery with USB disconnected.
9. Verify the firmware does not report HAT presence or PoE availability as a
   sensed fact; those remain unknown in the absence of a documented sense pin.

## References

Verify each URL resolves before relying on it. The Waveshare hosts return HTTP
403 to automated fetching (bot protection) but the pages are valid in a browser,
the same situation plan 41 records for TinyTronics.

Waveshare ESP32-S3-ETH

- Product page: https://www.waveshare.com/product/iot-communication/wired-comm-converter/esp32-s3-eth.htm
- Wiki: https://www.waveshare.com/wiki/ESP32-S3-ETH
- Schematic PDF, the authority for the GPIO pin table:
  https://files.waveshare.com/wiki/ESP32-S3-ETH/ESP32-S3-ETH-Schematic.pdf

ESP-IDF Ethernet and W5500

- esp_eth driver documentation, SPI-Ethernet modules:
  https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/network/esp_eth.html
- Ethernet basic example, the bring-up reference:
  https://github.com/espressif/esp-idf/tree/master/examples/ethernet/basic
- ethernet_init managed component, lists the W5500 MAC-PHY module:
  https://components.espressif.com/components/espressif/ethernet_init
- W5500 driver README, which documents the Ethernet MAC/PHY boundary but no
  PoE power-source sensing:
  https://github.com/espressif/esp-eth-drivers/blob/master/w5500/README.md
- espressif/w5500 driver component:
  https://components.espressif.com/components/espressif/w5500

ESP-IDF provisioning security2, SRP6a

- Unified provisioning, Security 2 scheme based on SRP6a (RFC 5054):
  https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/provisioning/provisioning.html
- protocomm:
  https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/provisioning/protocomm.html
- wifi_provisioning, for the security2 client shape:
  https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/provisioning/wifi_provisioning.html

Related furble plans

- `plans/33-wifi-hub.md`, the WiFi and MQTT track, headless build and HA
  discovery.
- `plans/41-alternative-hardware.md`, the alternate hardware survey.
- `plans/50-companion-app-design.md`, the companion app and its security
  section.
