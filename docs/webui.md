# WebUI and REST API

The WebUI is available on M5StickS3, M5Stack Core2, the headless S3 profile and
Waveshare ESP32-S3-ETH. The 4 MB profiles do not compile it.

## Enable it

Use the USB console or one-shot provisioning to configure WiFi and a companion
password. Then enable the WebUI:

```text
companion password set your-long-password
settings set web_ui on
settings set wifi on
```

Open `https://DEVICE-IP/`. The certificate is unique and self-signed, so the
browser shows a first-use warning. Compare its SHA-256 fingerprint with the
`webui` fingerprint printed on the USB log before accepting it. Use `furble` as
the HTTP username and the companion password as the password.

The server stays closed when the WebUI setting is off, WiFi has no address, the
companion password is unset or unreadable, or the TLS identity cannot be loaded.
Changing the companion password remains a USB console or provisioning action.

## API

Every route requires HTTP Basic authentication inside HTTPS. Mutation routes
require `Content-Type: application/json`.

| Method | Route | Purpose |
| :--- | :--- | :--- |
| `GET` | `/api/status` | Firmware, control, network, battery and shutter state. |
| `GET` | `/api/cameras` | Saved cameras with stable ids and connection state. |
| `POST` | `/api/cameras` | `{"action":"connect","id":ID}` or `{"action":"disconnect"}`. |
| `POST` | `/api/shutter` | `press`, `release`, `focus_press`, `focus_release`, or `hold` with `ms`. |
| `GET` | `/api/settings` | Shared settings catalog. Secret values are redacted. |
| `POST` | `/api/settings` | `{"id":WIRE_ID,"value":VALUE}` through production provisioning validation. |

Connect and control responses confirm that work was accepted into its owner
queue. Poll status for completion. Timed holds are limited to 60 seconds.

Self-signed TLS protects the password and traffic from passive capture. On the
first connection, fingerprint comparison is what protects against an active
local man-in-the-middle. The certificate persists in NVS, so a changed
fingerprint is a reason to stop unless NVS was intentionally erased.
The private key has the same at-rest protection as the other NVS credentials.
Production deployments that require resistance to physical flash extraction
must enable and provision ESP-IDF NVS encryption as a separate release step.
