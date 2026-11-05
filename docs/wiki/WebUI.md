# WebUI and REST API

On the M5StickS3 debug image, WiFi plus Bluetooth with the companion service
and display active measured 13,543 free / 12,788 largest internal bytes with
the WebUI off before plan 305, and 10,607 / 9,716 with the 3 KiB supervisor.
That was below the 20,480 / 12,288 server floor, so the listener stayed closed
with a measured log line instead of resetting. Plan 305 measured 27,351 free /
25,588 largest with the WebUI off, above the floor on paper but without a
server start. Plan 308 is inferred to reach about 34,631 free for the combined
MQTT and WebUI budget, but the largest block and a real TLS handshake are still
unmeasured. The StickS3 is therefore **not yet a supported WebUI
target** until a board run passes the gate, loads the settings page, writes a
setting and soaks. Core2 and the headless/Waveshare profiles have more headroom
but remain inferred.

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

The server stays closed without a loaded non-empty companion password. Secret
settings are never returned. Camera connect/disconnect, shutter, focus, timed
hold and validated settings writes are available from the page. REST route and
security details are in the repository's `docs/webui.md`.

The certificate and private key persist in NVS. The key has the same at-rest
protection as the other NVS credentials. Enable and provision ESP-IDF NVS
encryption where physical flash extraction is in the threat model.
