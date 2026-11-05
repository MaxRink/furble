# WebUI and REST API

The WebUI is compiled for M5StickS3, M5Stack Core2, the headless S3 profile and
Waveshare ESP32-S3-ETH. The 4 MB profiles do not compile it. On M5StickS3 it is
gated by the internal-heap check described below; the other profiles are
expected to fit but have not all been measured.

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

## Memory budget

The WebUI is optional because it is the largest internal-RAM consumer in the
network profiles. Both WebUI tasks must stay in internal RAM: the supervisor
starts and stops the server, and the HTTPS task runs settings reads and writes,
including NVS commits. A PSRAM task stack is unsafe there because the flash
cache is disabled during NVS writes.

Measured on the M5StickS3 debug image with WiFi connected, companion disabled,
before this budget work:

```text
heap.internal.free: 6827
heap.internal.largest_block: 6388
```

The old fixed 320-pixel-wide LVGL draw buffers (two 10,240-byte buffers even on
the 135x240 panel), an 8 KiB supervisor and a 12 KiB HTTPS task did not fit. The current
firmware sizes the two draw buffers to the detected panel, reduces the StickS3
WiFi static RX pool from 10 to 6 (dynamic RX stays 32), keeps the supervisor at
3 KiB and the HTTPS task at 8 KiB, and creates the TLS identity before the
radios take the heap.

The server refuses to start below a conservative internal-heap floor instead of
resetting the device. On the USB log look for:

```text
HTTPS stays closed: internal free N, largest N (need 20480/12288)
```

or

```text
Authenticated WebUI listening on HTTPS port 443 (internal free N, largest N)
```

Separate log lines report the supervisor and HTTPS stack headroom after the
first response; those are the numbers to collect for a new board.

| Build | Radio | PSRAM used | Internal free | WebUI |
| :--- | :--- | :--- | :--- | :--- |
| `m5stick-s3` (M5StickS3) | WiFi + BLE | 8 MB, mbedTLS in PSRAM | 6.8 KiB measured before this change; about 25 KiB expected with the WebUI off and about 22 KiB at server start | Marginal, gated; needs a board measurement |
| `m5stack-core2` | WiFi + BLE | 8 MB on board, disabled in firmware (`CONFIG_SPIRAM` unset); TLS buffers internal | Not measured | Expected to fit |
| `esp32-s3-headless` | WiFi + BLE | 8 MB enabled; mbedTLS and NimBLE host memory internal | Not measured; no display buffers | Expected to fit |
| `waveshare-s3-eth` | Ethernet (WiFi optional) | 8 MB enabled; mbedTLS internal | Not measured; WiFi RX pool not allocated while wired | Expected to fit |

Only the StickS3 measurement is real. The other rows are configuration
arithmetic until someone runs `perf heap` and opens the page on each board.
The 4 MB profiles do not compile the WebUI.

The server stays closed when the WebUI setting is off, WiFi has no address, the
companion password is unset or unreadable, the TLS identity cannot be loaded,
or the internal-heap floor is not met. An out-of-memory start keeps the stored
identity; only a rejected certificate/key pair is replaced. Changing the
companion password remains a USB console or provisioning action; clearing it
closes the listener on the next WebUI reload.

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

A repeated press is idempotent within one complete Control target session. A
user disconnect followed by a new connect creates a new target session, clears
the old held ownership and allows the next press to reach the new session even
when both transitions occur between WebUI supervisor polls.

Automatic BLE link recovery keeps the same target session. It intentionally
keeps held and pending-release ownership so surviving cameras remain protected
and furble does not guess what a vendor retained across link loss. After an
automatic reconnect, send an explicit `release` before sending another `press`.

Self-signed TLS protects the password and traffic from passive capture. On the
first connection, fingerprint comparison is what protects against an active
local man-in-the-middle. The certificate persists in NVS, so a changed
fingerprint is a reason to stop unless NVS was intentionally erased.
The private key has the same at-rest protection as the other NVS credentials.
Production deployments that require resistance to physical flash extraction
must enable and provision ESP-IDF NVS encryption as a separate release step.
