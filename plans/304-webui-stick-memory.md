# 304 - WebUI memory budget on the StickS3 debug image

## Finding

Enabling `web_ui` on the M5StickS3 with WiFi connected and the debug image
(Bluetooth initialized, companion available) boot-loops before the console is
ready. The ESP32-S3 reports `TG1WDT_SYS_RST`; an earlier capture named the
`webui` task and a later one showed the first allocation failures:

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

WebUI currently asks for an 8 KiB supervisor task plus a 12 KiB HTTPS server
task, 20 KiB of internal RAM that is not available after WiFi and the BLE
controller have initialized. The supervisor also called `Settings::load` on
every tick; that NVS read was the task-watchdog backtrace in the first capture.
Caching the setting removes that read but not the allocation failure.

## Attempted

- Cache `WEB_UI` in `WebUI::reloadSetting` instead of reading NVS every tick.
- Add `vTaskDelay` between TLS identity-generation steps.
- Move the WebUI supervisor and companion task stacks to PSRAM
  (`xTaskCreateWithCaps`). The device still reset before the console in the
  WebUI-enabled build; the PSRAM companion stack is suspect during flash
  operations and must not be merged untested.
- Reduce the HTTPS task stack from 12 KiB to 4 KiB and sockets to one; still not
  enough while the supervisor is internal.

## Proposed fix

1. Keep the companion task in internal RAM. PSRAM task stacks are not safe for
   code paths that write NVS while the flash cache is disabled.
2. Do not budget a one-shot identity task against the 6.8 KiB total. Only
   6388 bytes are contiguous before the supervisor stack; after a 2 KiB
   supervisor there are roughly 4.2 KiB total and less contiguous space, while
   the HTTPS task must also coexist. Generate the identity before WiFi/BLE
   initialization (while the main task stack is available) or on a dedicated
   task that runs before the server and frees its stack, rather than alongside
   the supervisor and httpd task.
3. Start `httpd_ssl_start` with `stack_size = 4096` and one socket, but measure
   the contiguous block before the server task starts; a 4 KiB stack cannot be
   assumed to fit beside the supervisor with the measured 6388-byte largest
   block. If it does not fit, keep WebUI disabled on StickS3.
4. Alternatively, keep WebUI disabled on StickS3 and document Core2/headless as
   the supported WebUI targets until the memory split is measured.

## Debug service design (not yet validated)

A `FURBLE_CONSOLE`-only, read-only BLE debug service was prototyped with:
- service `b57f4f70-087b-4740-b71d-8262cf26ebbc`
- status characteristic `b57f4f72-...` (reset reason, uptime, internal/PSRAM
  heap, task count, bonds, companion enabled/connected/encrypted/authenticated/
  bond/window/advertising)
- a log mirror characteristic `b57f4f71-...` fed by `esp_log_set_vprintf`.

The first prototype crashed the debug image when companion started; the log
hook was removed to isolate it, but the device became unresponsive before the
status-only version could be tested. Do not ship the service until it is
validated with companion enabled and disabled, and until the log hook is proven
safe from every logging context. The status-only tool design remains the right
fallback for a dead USB CDC endpoint.

## Validation blocked

The StickS3 was left unresponsive after the last WebUI boot loop and needs a
physical power cycle. Host tests and the rest of the companion work are already
merged; this plan is the remaining hardware gate.
