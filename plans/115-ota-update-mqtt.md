# 115 - OTA update engine and OTA-over-MQTT

Status: design only. Implements the delivery half of `plans/34-ota-partitions.md`
(PR34a-2) plus an MQTT command trigger on top of `plans/33-wifi-hub.md` PR33c.

**Split delivery.** The OTA state machine and the MQTT `cmd/ota` topic handler
are **Codex-implementable and host-testable now** with mock events, WITHOUT a
real download. Linking the module into firmware is **blocked on WiFi (PR33b) and
MQTT (PR33c, the #66 MQTT work)**. The real HTTPS download and the
Home-Assistant-triggered end to end are **Claude / hardware**.

## Motivation

`plans/34-ota-partitions.md` landed the two-slot layout, the rollback config and
the health check design, and explicitly scoped PR34a-2 (the `esp_https_ota`
delivery) as follow-on work "once there is a network to fetch over". Plan 33
PR33c adds MQTT so a Home Assistant dashboard drives furble. The natural union:
let a studio operator push a firmware update from the same dashboard that fires
the shutter, with progress reported back to a topic, and never touch a cable.

## Scope

In scope:

- A transport-agnostic OTA state machine in `src/FurbleOTA.cpp` /
  `include/FurbleOTA.h` with `begin(size)`, `write(chunk)`, `end()`, `abort()`
  wrapping `esp_ota_begin` / `esp_ota_write` / `esp_ota_end` /
  `esp_ota_set_boot_partition`, exactly the interface `plans/34` PR34b asked for
  so BLE OTA can reuse it later.
- An HTTPS caller using the advanced `esp_https_ota_begin` /
  `esp_https_ota_perform` loop / `esp_https_ota_finish` API with
  `partial_http_download = true`, `crt_bundle_attach`, and the Cloudflare Pages
  manifest source, all per `plans/34` PR34a-2's implementation notes.
- Console `ota` command group: `status`, `check`, `update`, `rollback`,
  `confirm` (verbatim from `plans/34` PR34a-2).
- An MQTT command topic `BASE/ID/cmd/ota` carrying a URL (or `check`), handled by
  the PR33c MQTT event task, with progress and result published to
  `BASE/ID/state/ota` (retained: last result; not retained: live progress).
- Preconditions enforced not advised: refuse if a camera is connected
  (`Control::getState() == STATE_ACTIVE`), if the intervalometer is running, or
  if battery is below 40% and not charging on boards that report it. Print/publish
  the reason.
- The boot health check and rollback arming from `plans/34`, if not already
  landed with PR34a-1.

Out of scope:

- Signed images and secure boot (`plans/34` rejected list).
- Automatic background updates. Every update is explicit.
- BLE OTA. That is `plans/34` PR34b; this doc only builds the reusable state
  machine it will share.

## Files to change

- New `src/FurbleOTA.cpp` / `include/FurbleOTA.h`; add to `furble_sources` and to
  `src/CMakeLists.txt` `PRIV_REQUIRES` (`esp_https_ota`, `esp_http_client`,
  `app_update`, `esp-tls`).
- New `src/FurbleHealth.cpp` / `include/FurbleHealth.h`, or folded into
  `FurbleOTA.cpp`, per `plans/34`.
- `src/main.cpp` `app_main`: arm the health check after the control task.
- `src/FurbleMQTT.cpp` (from PR33c): route `cmd/ota` to `FurbleOTA`, publish
  `state/ota`. This is the coupling point to the #66 MQTT work.
- The PR27 console table: the `ota` group.
- `web-installer` / `.github/workflows/release.yml`: `ota_data_initial.bin` and
  the manifest `version` field already exist from `plans/34`; confirm the
  per-board custom tables from `plans/113` still emit the initial otadata.

## Settings and defaults

| Setting | Type | Default | Effect |
|---|---|---|---|
| `OTA_URL` | string | project Pages base URL | where `check`/`update` fetch |
| `OTA_CHANNEL` | enum `stable`,`prerelease` | `stable` | release track |

No enable flag: there is no background activity to gate. Defaults keep the
device inert until the user runs `ota update` or publishes to `cmd/ota`.

## Dependencies

- `plans/34-ota-partitions.md` PR34a-1 (layout, rollback, health): landed.
- `plans/113-per-board-ota-slots.md`: gives the S3 real headroom to hold the
  WiFi+MQTT+TLS+OTA image. Land 113 first.
- `plans/33-wifi-hub.md` PR33b (WiFi): **not landed**. Hard blocker for the
  firmware download path.
- `plans/33-wifi-hub.md` PR33c (MQTT, #66): **not landed**. Hard blocker for the
  `cmd/ota` topic and progress publish.
- The OTA state machine and the topic handler logic can be **written and
  host-tested now** against mocks; only firmware linkage waits on 33b/33c.

## Risks

- **A firmware download saturates the shared radio.** The camera-connected
  precondition is mandatory, not advisory. Test it fires.
- **QoS-1 redelivery of `cmd/ota`.** A duplicated OTA command must not start two
  downloads. The state machine rejects a `begin` while already running and the
  topic handler is idempotent on the URL. Assert this in the mock test.
- **Rollback masks a broken release.** `ota status` and the `state/ota` topic
  must report a rollback loudly, per `plans/34`.
- **Pages CDN is a runtime dependency.** `OTA_URL` being configurable is the
  mitigation, same as `plans/34`.
- **Progress must not hold the Control mutex.** The OTA event handler runs on the
  esp-mqtt / OTA task; publish through the same lock-free `Control::sendCommand`
  and MQTT publish seams PR33c uses. Never block a camera task.
- Nothing here is vendor specific.

## Codex self-verification (headless, no download, no network)

The state machine and the MQTT topic handler are the parts most likely to have
bugs and both are pure logic. Add host tests under `tests/host`, registered in
`tests/camera` ctest.

`ota_state_machine_test` (mocks `esp_ota_*` via a small double that counts calls
and can force an `esp_ota_end` failure):

- `begin(size)` then a sequence of `write()` totalling `size` then `end()`
  drives `esp_ota_begin` once, `esp_ota_write` per chunk, `esp_ota_end` once,
  `esp_ota_set_boot_partition` once.
- A second `begin()` while running is rejected (idempotency / no double start).
- `abort()` mid-stream calls `esp_ota_abort` and leaves the boot partition
  unchanged.
- A forced `esp_ota_end` failure (truncated image) does not set the boot
  partition and reports the error.

`ota_mqtt_handler_test` (feeds a synthetic `MQTT_EVENT_DATA` for
`BASE/ID/cmd/ota` into the handler with a mock OTA-begin that never touches the
network):

- A `check` payload triggers a manifest fetch stub and publishes an available
  version to `state/ota` without starting a download.
- A URL payload with a camera "connected" (mock `Control::getState() ==
  STATE_ACTIVE`) is refused and publishes the reason; the mock OTA-begin is
  never called.
- A URL payload with preconditions clear calls the mock OTA-begin exactly once.
- A duplicated URL payload (QoS-1 redelivery) does not call OTA-begin twice.

Run:

```
cmake -S tests/camera -B build/camera-tests -DCMAKE_BUILD_TYPE=Release
cmake --build build/camera-tests --parallel 2
ctest --test-dir build/camera-tests -R 'ota-state-machine|ota-mqtt-handler' \
  --output-on-failure
```

Exit 0 proves the OTA logic and the MQTT-trigger wiring headless, with no HTTPS
server and no broker. See `plans/117-sim-mqtt-coverage.md` for the broader MQTT
client coverage that these handler tests plug into once #66 lands.

## Residual (Claude / hardware) verification

- The real `esp_https_ota` download over WiFi from the Pages manifest, progress
  output, reboot into the other slot, and `ota status` reflecting it (`plans/34`
  PR34a-2 steps 11-22).
- Pull the AP mid-download: clean abort, running image untouched, retry resumes
  via the HTTP range.
- Publish `BASE/ID/cmd/ota` from Home Assistant / `mosquitto_pub` and watch
  `state/ota` progress on a dashboard, camera link surviving the outage.
- The rollback path on a deliberately broken image.
