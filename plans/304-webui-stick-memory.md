# 304 - WebUI memory budget on the StickS3

> Plan 305 supersedes the task-stack and draw-buffer numbers in this document.
> The measurements below are the pre-305 baseline. See the follow-up section at
> the end.

## Finding

Enabling `web_ui` on the M5StickS3 with WiFi connected and the debug image
(Bluetooth initialized, companion available) boot-looped before the console was
ready. The ESP32-S3 reported `TG1WDT_SYS_RST`; the first capture named the
`webui` task and a later one showed the allocation failures:

```text
Mem alloc fail. size 0x00002000 caps 0x00000804   # 8 KiB supervisor task
Mem alloc fail. size 0x00003000 caps 0x00000804   # 12 KiB httpd task
esp_https_server: Starting server
```

`perf heap` on the same image with WiFi connected and companion disabled:

```text
heap.internal.free: 6827
heap.internal.largest_block: 6388
```

Those are the only measured StickS3 numbers. The rest of this plan is a
compile-time budget and a set of inferred board estimates. **No board was
measured for this change: the StickS3 is off USB.**

Two independent problems were mixed together:

1. The supervisor task and the HTTPS task were 8 KiB + 12 KiB of internal RAM.
   Internal RAM is the only safe home for both stacks. The supervisor starts
   and stops the server, and the HTTPS task runs `Settings::load`/`save`, which
   writes NVS. A PSRAM stack is unsafe for a task that performs a flash write:
   the cache is disabled while the NVS commit runs and the task cannot resume
   from a PSRAM stack.
2. The TLS identity generation (ECDSA P-256 key + X.509 write + NVS save) ran
   inside the supervisor. That is thousands of bytes of stack on top of the
   already-overcommitted budget.

The display was also wasting internal DMA RAM. `UI::BUFFER_SIZE` was fixed at
`320 * 16 * 2 = 10240` bytes for both LVGL draw buffers even on the 135x240
StickS3 and the 80x160 StickC. On the StickS3 the two buffers held 20,480 bytes,
11,840 of which were outside the panel.

## Attempted and rejected

- Cache `WEB_UI` in `WebUI::reloadSetting` instead of reading NVS every tick.
  Correct, but it does not create memory.
- Add `vTaskDelay` between TLS identity-generation steps. It does not reduce the
  stack requirement and does not make the allocation contiguous.
- Move the WebUI supervisor and companion task stacks to PSRAM
  (`xTaskCreateWithCaps`). The device still reset before the console in the
  WebUI-enabled build, and a PSRAM stack is wrong for any task that writes NVS
  while the flash cache is disabled. Do not merge this.
- Reduce the HTTPS task stack from 12 KiB to 4 KiB and sockets to one while the
  supervisor stayed internal. The allocation still failed; a 4 KiB TLS task is
  also below the stack needed for a handshake plus a JSON request.
- Treat the identity as disposable on every failed start. An out-of-memory
  start would wipe the certificate and regenerate it on the next attempt,
  adding NVS writes and fragmentation to an already low heap.

## Chosen fix

1. **Identity before the radios.** `WebUI::initEarly()` runs from `app_main`
   after `Device::init()` (so the device ID for the CN exists) and before
   `Companion::init()`/WiFi/display init. When `web_ui` is on and a companion
   password exists, it loads or creates the identity on the 16 KiB main task,
   while NVS writes are safe and the internal heap is still large. A runtime
   enable with no cached identity uses a one-shot 12 KiB internal task that
   deletes itself; if that cannot be allocated, the log says to reboot instead
   of crashing or boot-looping.
2. **No NVS in the supervisor loop.** `reloadSetting()` caches `m_Enabled`,
   `m_PasswordReady` and `m_CredentialsReady`. The supervisor polls atomics and
   never calls `Settings::load`/`loadPassword`. Every live password reload,
   including the companion GATT write path, invokes `WebUI::reloadSetting()`
   through the service callback so clearing the password closes the listener.
3. **Small internal tasks.** The supervisor is 3 KiB and the HTTPS task is
   8 KiB (`httpd_config_t::stack_size`). Five total sockets (ESP-IDF reserves
   three, leaving two client slots), `lru_purge_enable = true`, `max_req_hdr_len = 1024`,
   seven response headers and seven URI handlers remove per-request/per-server
   bookkeeping. The TLS
   certificate fingerprint is computed once and cached; `startServer()` no
   longer re-parses the certificate on the 3 KiB supervisor stack.
4. **Draw buffers sized to the panel.** `UIMemory::drawBufferBytes()` replaces
   the fixed 320x240 size. On the StickS3 each buffer drops from 10,240 to
   4,320 bytes (11,840 bytes total); on the StickC from 10,240 to 1,600 each.
   Core2/Core (320x240) are unchanged. Both buffers stay DMA-capable internal
   RAM. A failed second allocation falls back to single buffering.
5. **StickS3 WiFi static RX pool 10 -> 6.** ESP-IDF recommends at least the
   AMPDU RX BA window (6); dynamic RX buffers stay at 32. This frees about
   6.4 KiB of internal RAM on the constrained StickS3. It is a documented
   trade-off, not a throughput claim; the release image gets the same setting.
6. **Smaller request peak.** `buildSettingsJSON()` serializes one setting at a
   time and prints/deletes it, instead of building the full cJSON tree and then
   a second text copy. The wire JSON is unchanged. This removes the largest
   transient allocation (roughly 6-7 KiB with the full catalog) from the HTTPS
   task.
7. **Fail closed on memory.** `startServer()` checks the internal free and
   largest-block values against `FurbleWebUIMemory.h` before allocating. Below
   the floor it logs the measured values and keeps HTTPS closed; it retries at
   most every 10 s. An out-of-memory `httpd_ssl_start` keeps the identity; only
   a non-memory start failure drops it once. The supervisor logs its own stack
   headroom and the HTTPS task logs its minimum headroom, which gives the next
   hardware run real numbers.

## Compile-time budget

Sizes measured from this checkout's ESP-IDF 5.5.3 headers with the target
compiler (`sizeof` for the StickS3 configuration):

| Object | Bytes |
| :--- | ---: |
| `struct httpd_data` | 776 |
| `struct sock_db` (five total sockets) | 960 |
| `struct httpd_config` | 96 |
| `struct resp_hdr` (seven headers) | 56 |
| `esp_tls_t` (one connection) | 2,184 |
| `esp_tls_cfg_server_t` | 56 |
| `httpd_ssl_ctx_t` + transport context | 28 |
| copied certificate + private key | ~800 |
| request scratch (`max_req_hdr_len`) | 1,024 |
| settings JSON peak (streamed) | ~2,000 |
| lwIP socket/pbufs for one response | ~3,000 |

New internal allocations at first connection, excluding the task stacks:
about 10.4 KiB. The two stacks add 3,172 + 8,292 bytes (including TCBs), so
enabling the WebUI from a boot with it off needs about 21.7 KiB of internal
heap. At server start the supervisor is already allocated, so the server adds
about 18.7 KiB; the 20,480-byte free floor leaves roughly 1.8 KiB of headroom
beyond that. `FurbleWebUIMemory.h` also requires a 12,288-byte largest block so
the 8 KiB httpd task and the per-connection `esp_tls_t` can be carved without
relying on one oversized allocation staying contiguous.

Expected StickS3 after this change (inferred, not measured):

```text
measured before, web_ui off:        6,827 free / 6,388 largest
panel-sized draw buffers:          +11,840
WiFi static RX 10 -> 6:             +6,400
expected after, web_ui still off:  ~25,067 free, largest at least 12,288
measured after, web_ui still off:  13,543 free, 12,788 largest
measured with the 3 KiB supervisor: 10,607 free, 9,716 largest (server gated closed)
```

The runtime gate is the authority. If the inferred free value is wrong, the
server logs the real value and stays closed instead of resetting.

## Hardware matrix

Measured = read from this firmware on the board. Inferred = configuration and
allocation arithmetic, not a board measurement.

| Build / board | Radio | PSRAM | Internal free | WebUI | Minimum change |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `m5stick-s3` (M5StickS3, ESP32-S3, 8 MB flash) | WiFi + BLE | 8 MB octal, enabled; mbedTLS allocations in PSRAM | **Measured 13,543 / 12,788 with WebUI off** and **10,607 / 9,716 with the 3 KiB supervisor**. The server gate then logs 10,591 / 9,716 and stays closed. | **No**: below the 20,480 / 12,288 floor; the HTTPS task alone cannot fit | This change is the safe failure: no boot loop, measured log line. Enabling WebUI on the StickS3 needs a much larger internal-RAM reduction, not a task-size tweak. |
| `m5stack-core2` (ESP32, 16 MB flash) | WiFi + BLE | 8 MB hardware PSRAM, **not enabled** (`CONFIG_SPIRAM` unset), so TLS buffers are internal | Unmeasured. 320 KB DRAM, no display-free win; LVGL stays 20,480 bytes. Expected tens of KiB free. | **Expected yes, unmeasured** | Nothing for the task budget. Verify with `perf heap`; if it fails, the same heap gate keeps it closed. |
| `esp32-s3-headless` (ESP32-S3 DevKitC, 8 MB flash) | WiFi + BLE | 8 MB octal, enabled; **mbedTLS internal** | Unmeasured. No display and no LVGL buffers (`FURBLE_NO_DISPLAY`), but NimBLE host memory is internal. Expected more free than the StickS3. | **Expected yes, unmeasured** | Nothing. Optionally move NimBLE/mbedTLS to PSRAM later; not needed to pass the gate. |
| `waveshare-s3-eth` (ESP32-S3, 16 MB flash) | Ethernet; WiFi only if separately enabled | 8 MB octal, enabled; mbedTLS internal | Unmeasured. No display; with the default WiFi off the 16 KB static RX pool is not allocated at all. | **Expected yes, unmeasured** | Nothing. Verify the default Ethernet-only boot and, separately, WiFi fallback. |
| 4 MB profiles (`m5stick-c`, `m5stick-c-plus`, `m5stack-core`) | n/a | n/a | n/a | **Not compiled** | None. |

The StickS3 is measured and **cannot host the WebUI** with WiFi, BLE, the
companion service and the display active. The gate keeps the listener closed
and the device stable; it is not a supported WebUI target. Core2, headless and
Waveshare have more headroom but remain inferred until someone runs
`perf heap` and opens the page on each board. Core2 or the headless profile is
the practical minimum unless a headless/Waveshare measurement says otherwise.

## Stack evidence

`-fstack-usage` on the target build gives the deepest static chain on the first
connection (private-key import into PSA), which is the largest stack consumer in
the HTTPS task:

```text
httpd_ssl_open                              48
esp_tls_server_session_create               32
esp_mbedtls_server_session_create           32
esp_mbedtls_server_session_init             64
esp_create_mbedtls_handle                   64
set_server_config                           80
mbedtls_pk_parse_key                        80
pk_parse_key_pkcs8_unencrypted_der          96
import_pair_into_psa                     2,432
copy_from_psa                            2,448
                                         -----
                                         5,296 bytes
```

The handshake frames are smaller (`mbedtls_ssl_handshake_server_step` 256,
`mbedtls_ssl_derive_keys` 352, `ecp_mul_comb_after_precomp` 416). The 8 KiB
HTTPS stack leaves about 2.9 KiB for call overhead and interrupt frames. This is
static evidence, not a runtime high-water mark; the firmware logs the real
high-water mark after the first response.

## Validation done

- Host tests: `webui-security` (memory floors and protocol rules), `webui-handler`
  (identity persistence, budget gate, out-of-memory start keeps the identity,
  settings serialization), `ui-memory` (draw-buffer sizing).
- Firmware: `FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e m5stick-s3-debug`.
- Static analysis: `python3 tools/check_static_analysis.py --base origin/master`.
- Pre-review: `python3 tools/pre_review.py --base origin/master`.
- StickS3 hardware, WebUI off: 13,543 free / 12,788 largest internal bytes with
  WiFi connected, companion enabled, display active.
- StickS3 hardware, WebUI on: the supervisor starts (high-water 512 bytes of
  its 3 KiB stack), the server gate logs
  `HTTPS stays closed: internal free 10591, largest 9716 (need 20480/12288)`,
  and the device stays up with no watchdog reset. This is the measured
  fail-closed result.

## Still needs hardware

1. Core2 / `esp32-s3-headless` / Waveshare: `perf heap` with the WebUI open, then
   confirm the `HTTPS listening ...` line clears the floors and `/api/settings`
   loads. These boards are inferred, not measured.
2. Read the HTTPS task high-water mark after a GET/POST and raise
   `HTTPD_STACK_BYTES` if it is under 1 KiB.
3. Repeat on the chosen board with a BLE camera connected and the companion
   enabled.
4. Confirm the 10 -> 6 WiFi static RX change does not regress MQTT/OTA
   throughput on the StickS3 release image.

## Prioritized follow-ups (not in this change)

| Candidate | Current | Expected saving | Safety | Evidence |
| :--- | ---: | ---: | :--- | :--- |
| NimBLE host task stack 16 KiB | 16,384 internal | up to 4-8 KiB if 8-12 KiB is enough | Internal stack, no flash-cache issue, but BLE callbacks are hard to bound | inferred; needs `uxTaskGetStackHighWaterMark` on hardware |
| Console task stack (debug only) | 8,192 internal | 2-4 KiB | Internal, printf-heavy; needs high-water measurement | inferred |
| WiFi dynamic RX/TX pools 32 | on demand | no steady-state saving | It is a cap, not a preallocation | measured config |
| `CONFIG_MBEDTLS_DYNAMIC_BUFFER` | off | frees idle TLS buffers | StickS3 already has mbedTLS in PSRAM; helps headless/Waveshare | inferred; IDF documents 42 KB -> 22 KB heap |
| `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC` on headless/Waveshare | internal | ~20 KiB internal | Already used on StickS3; PSRAM is present | inferred |
| `SPIRAM_MALLOC_ALWAYSINTERNAL` 4096 -> lower | 4,096 threshold | a few KiB | Risky: driver small allocations could move to PSRAM | inferred; do not change without a soak |
| Single LVGL draw buffer | two buffers | 4,320 bytes on StickS3 | UI-only; serializes flush | inferred; not needed if the gate passes |
| NimBLE external memory on headless/Waveshare | internal | tens of KiB | Same setting StickS3 already ships | inferred |
| WebUI JSON size | ~2-3 KiB settings response | none without a contract change | Do not change the wire format | n/a |

## Debug service (shipped in #391)

A `FURBLE_CONSOLE`-only, read-only BLE debug service was prototyped with:
- service `b57f4f70-087b-4740-b71d-8262cf26ebbc`
- status characteristic `b57f4f72-...` (reset reason, uptime, internal/PSRAM
  heap, task count, bonds, companion enabled/connected/encrypted/authenticated/
  bond/window/advertising)
- a log mirror characteristic `b57f4f71-...` fed by `esp_log_set_vprintf`.

The first prototype crashed the debug image because `attach()` called the
status getters while `enable()` held `m_Mutex`, and the log hook was also
unproven. The shipped bench build now uses a status-only characteristic
(`b57f4f72-...`), attaches without reading any `CompanionGatt` state, and
refreshes the snapshot only from the companion service task. It is gated on
`FURBLE_BLE_DEBUG` with the `m5stick-s3-debug-ble` environment and uses
`tools/ble_debug.py`. It was validated on the StickS3 in #391 and shows the
reset reason, heap, tasks, bonds and companion state without touching the
encrypted companion link.

## Remaining hardware gate

The StickS3 fail-closed path is validated. Core2, headless and Waveshare still
need the `perf heap` + page-load measurement before the WebUI can be called
supported there.

## Follow-up: plan 305

[305-general-internal-ram.md](305-general-internal-ram.md) is the general pass
that follows this document. It keeps the fail-closed gate and changes the
StickS3 baseline:

- The main stack drops from 16 KiB to 12 KiB. The boot TLS identity now runs on
  the existing 12 KiB internal identity task, and the main task waits for it.
- The IR task stack moves to PSRAM.
- Narrow panels halve the LVGL partial rows. Each StickS3 draw buffer drops from
  4,320 to 2,160 bytes and two internal DMA buffers are still used.
- The debug console stack drops from 8 KiB to 6 KiB and its `perf tasks`
  snapshots move off the stack.
- Headless and Waveshare move mbedTLS allocations to PSRAM.

The StickS3 idle internal saving is about 12.5 KiB in a release image and about
14.6 KiB in a debug image. In inference that clears the 20,480 / 12,288 server
floor with about 25 KiB free while the supervisor runs. The numbers in this
document remain the measured pre-305 baseline. The StickS3 is not a supported
WebUI target until plan 305's hardware gate passes.

