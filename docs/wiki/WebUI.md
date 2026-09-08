# WebUI and REST API

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
