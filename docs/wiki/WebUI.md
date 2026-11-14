# WebUI and REST API

On the M5StickS3 debug image, WiFi plus Bluetooth with the companion service
and display active measured 13,543 free / 12,788 largest internal bytes with
the WebUI off before plan 305, and 10,607 / 9,716 with the 3 KiB supervisor.
Plan 314 later raised the supervisor to 3.5 KiB (floors 4,608 / 4,096),
because ESP-IDF Log V2 adds 144 bytes to its deepest logging path.
That was below the 20,480 / 12,288 server floor, so the listener stayed closed
with a measured log line instead of resetting. Plan 305 measured 27,351 free /
25,588 largest with the WebUI off, above the floor on paper but without a
server start. The 8 KiB main-stack experiment measured 30,007 free / 28,660
largest with MQTT off. Plan 311 restored the 12 KiB main stack, which leaves an
inferred 25,911 free, and #407 moved the MQTT event queue to PSRAM. The largest
block and a real TLS handshake are still unmeasured. The StickS3 is therefore
**not yet a supported WebUI target** until a board run passes the gate, loads
the settings page, writes a setting and soaks. Core2 and the
headless/Waveshare profiles have more headroom but remain inferred.

The WebUI is compiled into the 8 MB and 16 MB network hub profiles. It is off
by default. Configure WiFi and a companion password over USB, then run:

```text
settings set web_ui on
settings set wifi on
```

Open `https://DEVICE-IP/`, use `furble` as the username, and use the companion
password. The certificate is unique and self-signed. Compare the browser's
SHA-256 fingerprint with the `webui` fingerprint printed on the USB log before
accepting the first-use warning.

The TLS identity is created at boot when the WebUI is on. If you first turn the
WebUI on at runtime with little internal RAM free, furble does not create the
identity and logs `TLS identity not generated: ... to retry.` HTTPS stays closed until the identity is generated. Reboot with the
WebUI on, or change a setting once more internal RAM is free, to try again.

The server stays closed without a loaded non-empty companion password. Secret
settings are never returned. Camera connect/disconnect, shutter, focus, timed
hold and validated settings writes are available from the page. REST route and
security details are in the repository's `docs/webui.md`.

The certificate and private key persist in NVS. The key has the same at-rest
protection as the other NVS credentials. Enable and provision ESP-IDF NVS
encryption where physical flash extraction is in the threat model.

When internal RAM runs low, furble pauses the WebUI server first and then the
MQTT client, so a camera connect still fits, and brings them back once there is
room again. A camera connect that would run short pauses the WebUI as it
starts, and the WebUI returns after the camera disconnects. The saved settings
do not change. `status` on the USB console
reports `memory_pressure`; the thresholds are in the repository's
`docs/webui.md`.
