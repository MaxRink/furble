# 115b - HTTPS OTA delivery and OTA-over-MQTT

Status: 115b host slice implemented, see Implementation status below. The
transport-independent OTA engine from `plans/implemented/115-ota-engine.md`
landed in PR #168. This document is the delivery follow-up: HTTPS transport,
user-facing status, and an MQTT command trigger on top of
`plans/33-wifi-hub.md` PR33c. The real HTTPS download and the Home Assistant
end-to-end flow remain hardware work.

**Split delivery.** The HTTPS adapter, precondition policy, and MQTT
`cmd/ota` topic handler are Codex-implementable and host-testable with mock
events, without a real download. Linking the transport into the firmware was
blocked on WiFi (PR33b, #53) and MQTT (PR33c, #66); both have merged. The real HTTPS
download and the Home Assistant end to end flow are Claude / hardware work.

## Motivation

`plans/implemented/34-ota-partitions.md` landed the two-slot layout, the rollback config and
the health check design, and explicitly scoped PR34a-2 (the `esp_https_ota`
delivery) as follow-on work "once there is a network to fetch over". Plan 33
PR33c adds MQTT so a Home Assistant dashboard drives furble. The natural union:
let a studio operator push a firmware update from the same dashboard that fires
the shutter, with progress reported back to a topic, and never touch a cable.

## Scope

In scope:

- An HTTPS caller using the advanced `esp_https_ota_begin` /
  `esp_https_ota_perform` loop / `esp_https_ota_finish` API with
  `partial_http_download = true`, `crt_bundle_attach`, and the Cloudflare Pages
  manifest source, all per `plans/34` PR34a-2's implementation notes.
- Console `ota` command group: `status`, `check`, `update`, `rollback`,
  `confirm` (verbatim from `plans/34` PR34a-2).
- An MQTT command topic `BASE/ID/cmd/ota` carrying a URL (or `check`), handled by
  the PR33c MQTT event task, with progress and result published to
  `BASE/ID/state/ota` (retained: last result; not retained: live progress).
- Preconditions are enforced, not advisory: refuse if a camera is connected
  (`Control::getState() == STATE_ACTIVE`), if the intervalometer is running, or
  if battery is below 40% and not charging on boards that report it. Print/publish
  the reason.
- Extend the landed `OTA::Engine` with an injected HTTPS transport and preserve
  its progress and abort invariants.
- Add the boot health check and rollback arming from `plans/34`, if not already
  landed with PR34a-1.

Out of scope:

- Signed images and secure boot (`plans/34` rejected list).
- Automatic background updates. Every update is explicit.
- BLE OTA. That is `plans/34` PR34b; this doc only builds the reusable state
  machine it will share.

## Files to change

- Extend `src/FurbleOTA.cpp` / `include/FurbleOTA.h`; add the required
  `PRIV_REQUIRES` (`esp_https_ota`, `esp_http_client`, `app_update`, `esp-tls`)
  in the same change that links the transport.
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

- `plans/implemented/34-ota-partitions.md` PR34a-1 (layout, rollback, health): landed.
- `plans/implemented/113-per-board-ota-slots.md` PR #167: gives the S3 real headroom to hold the
  WiFi+MQTT+TLS+OTA image. The partition invariant is already on fork/master.
- `plans/33-wifi-hub.md` PR33b (WiFi): **not landed**. Hard blocker for the
  firmware download path.
- `plans/33-wifi-hub.md` PR33c (MQTT, #66): **not landed**. Hard blocker for the
  `cmd/ota` topic and progress publish.
- The OTA transport and topic-handler logic can be **written and host-tested
  now** against mocks; only firmware linkage waits on 33b/33c.

## Risks

- **A firmware download saturates the shared radio.** The camera-connected
  precondition is mandatory, not advisory. Test it fires.
- **QoS-1 redelivery of `cmd/ota`.** A duplicated OTA command must not start two
  downloads. The state machine rejects a `begin` while already running and the
  topic handler is idempotent on the URL. Assert this in the mock test.
- **No image is ever marked valid (latent today).** Every OTA-capable
  sdkconfig sets `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`, and nothing calls
  `esp_ota_mark_app_valid_cancel_rollback()`. ESP-IDF marks a still-pending
  image ABORTED on its second boot and reverts to the old slot
  ([esp_ota_ops.c](https://github.com/espressif/esp-idf/blob/v5.5.1/components/app_update/esp_ota_ops.c)).
  No production path calls the partition sink yet, so nothing is affected
  today. This delivery must wire the plan 125 boot-health evaluator to the
  mark-valid API before it links the sink, or a successful update silently
  reverts after the second reboot. The hardware check is OTA, reboot twice,
  then read `esp_ota_get_running_partition()`.
- **Rollback masks a broken release.** `ota status` and the `state/ota` topic
  must report a rollback loudly, per `plans/34`.
- **Pages CDN is a runtime dependency.** `OTA_URL` being configurable is the
  mitigation, same as `plans/34`.
- **Progress must not hold the Control mutex.** The OTA event handler runs on the
  esp-mqtt / OTA task; publish through the same lock-free `Control::sendCommand`
  and MQTT publish seams PR33c uses. Never block a camera task.
- Nothing here is vendor specific.

## Codex self-verification (headless, no download, no network)

PR #168 already provides `ota_state_machine_test` for the landed injected
engine. The delivery follow-up adds host tests under `tests/host`, registered in
its CTest suite, for the HTTPS adapter and MQTT handler:

- A mock HTTPS transport drives a complete engine update, preserves the engine
  progress invariants, and aborts cleanly on a transport error.
- A synthetic `BASE/ID/cmd/ota` `check` publishes an available version without
  starting a download.
- A URL is refused while a camera or intervalometer is active, and when the
  battery precondition is not met.
- A URL with clear preconditions starts exactly once, and a duplicate QoS-1
  delivery cannot start a second update.
- Progress and the final result use the retained/non-retained policy in the
  scope above.

Run:

```
cmake -S tests/host -B build/host-tests -DCMAKE_BUILD_TYPE=Release
cmake --build build/host-tests --parallel 2
ctest --test-dir build/host-tests -R 'ota-state-machine|ota-mqtt-handler|ota-https' \
  --output-on-failure
```

Exit 0 proves the OTA logic and the MQTT-trigger wiring headless, with no HTTPS
server and no broker. See `plans/implemented/117-sim-mqtt-coverage.md` for the broader MQTT
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

## Implementation status

The host-testable delivery slice landed. It adds no network fetch, no flash
write and no partition table change, so nothing here was validated on hardware.

Implemented:

- `include/FurbleOTAHTTPS.h` / `src/FurbleOTAHTTPS.cpp`: `OTA::HTTPS::Adapter`
  behind the existing `OTA::Transport` seam, over an injected
  `OTA::HTTPS::Client` with the shape of the advanced `esp_https_ota` call
  sequence. The adapter owns the https-only URL rule, the resume offset, the
  announced image length bounds, the inactive slot capacity check, and the
  stable reason labels for a wrong size, a digest mismatch, an invalid
  signature, an interrupted transfer and a failed apply. `abort()` is
  idempotent and keeps the root cause instead of overwriting it.
- `include/FurbleOTAAdmission.h` / `src/FurbleOTAAdmission.cpp`: the enforced
  precondition policy, in evaluation order: busy, camera session,
  intervalometer, link, slot, battery. The 30 percent floor from the earlier
  admission gate is kept, and a charging board is exempt. `compareVersions()`
  compares dotted numeric versions, ignores a `v` prefix and a `-` or `+`
  suffix, and treats an installed `dev+g<hash>` build as older than a release.
- `include/FurbleOTACommand.h` / `src/FurbleOTACommand.cpp`: the documented
  `cmd/ota` handler. The payload grammar is `status`, `check`,
  `check <https-url>`, `abort`, or a bare `https://` URL. Retained commands are
  refused. The identical URL is idempotent while an update runs, after a
  successful update, and for a `DUP` redelivery after a failure; a fresh
  command retries a failed transfer from its confirmed checkpoint.
- `include/FurbleOTAStatus.h` / `src/FurbleOTAStatus.cpp`: the flat `state/ota`
  record, its JSON formatter, its console formatter and the mutex-guarded
  `StatusStore` shared by the MQTT publisher and the console.
- `src/FurbleMQTT.cpp`: routes `cmd/ota` with the retain and duplicate
  metadata, advances one engine stage or download poll per owner iteration,
  publishes `state/ota` with the retained policy (final result retained, live
  progress not, one retained `checking` record to clear a stale result), and
  answers `transport_unavailable` when no handler is attached.
  `attachOTAHandler()`, `setOTABootReport()` and `otaContext()` are the
  composition seam for the hardware transport and the boot health evaluation.
- `src/FurbleConsole.cpp`: `ota status` prints the shared record.
- Tests: `ota-https` (adapter and engine over a scripted client, including a
  complete update, a wrong size, a bad digest, a bad signature, an interrupted
  transfer with a resumed retry and an apply failure), `ota-mqtt-handler`
  (payload parsing, every refusal, version comparison, duplicate delivery,
  check without a download, retained policy, transport detail and a rollback
  after a failed health check), the `mqtt-owner` `cmd/ota` block (mock MQTT
  events through the production topic route and HTTPS adapter), and the
  `console-commands` `ota status` case.

Deviations from the plan text:

- No `esp_https_ota` call, no `PRIV_REQUIRES` change and no network fetch. The
  scope was the host-testable adapter, and the user authority for this slice
  excluded a real download. The firmware adapter is the next hardware slice.
- No `OTA_URL` or `OTA_CHANNEL` setting. Without a manifest source the settings
  have no consumer. `check` therefore requires an explicit URL or the last URL
  the device was given.
- The console group is `status` only. `check`, `update`, `rollback` and
  `confirm` need the linked transport and the boot health composition, so they
  are not advertised as working commands.
- Boot health is reported, not executed. `setOTABootReport()` records the
  verdict the plan 125 evaluator will produce. `app_main` still does not arm
  the health check or call the mark-valid API, so the plan 34 rollback risk
  remains open until that composition lands.
- `slotAvailable` is `true` in the MQTT context because no partition read is
  linked. The adapter's slot capacity check is authoritative for the image.
- Plan 130's signed `FOA1` envelope remains the reserved update path. The
  `cmd/ota` topic in this slice carries the documented bare command form and
  does not dispatch a signed envelope.
- External power is a VBUS measurement, not the board's charge capability and
  not the charge flag. The first draft of the composition read
  `battery_caps_t::charging`, which is a compile-time constant and made the 30
  percent floor unreachable on the StickS3, the Core2 and the Tough. The
  independent review caught it; see
  [115-review-feedback.md](115-review-feedback.md). `OTA::externalPowerPresent()`
  is the rule, and a board without VBUS sense keeps the floor.
- The OTA status store and the MQTT singleton live in the heap, not in .bss.
  The slice linked a 188-byte store plus its 8-byte guard into the image, and
  the MQTT singleton that holds the handler, the context and the status payload
  grew by about 43 bytes. `waveshare-s3-eth-debug` measured 56,096 on master
  with 224 bytes of headroom, so the 288-byte slice failed
  `check_size_budget.py` while every host suite passed. Both owners are now
  heap allocated: the MQTT singleton (952 bytes) at `MQTT::init()`, which is
  before the owner gate and therefore leaves that gate's free-byte measurement
  unchanged, and the status store (188 bytes) at console start and in
  `MQTT::init()`. Static DRAM falls to 55,256 on that env, so the recorded
  56,320-byte budget is unchanged and the image keeps 1,064 bytes of headroom.
  The release profile falls to 46,136 at its 48,128-byte budget. The saving
  applies to every MQTT profile and to every console build, not only to the
  debug image. See [115-review-feedback.md](115-review-feedback.md).
- The engine advances once per MQTT owner iteration whether or not the broker is
  connected. Gating the step on the connection froze a running transfer in
  `busy()` during an outage, with no command able to abort it. Only the publish
  is gated on the broker.

Remaining hardware validation:

- Link a real `esp_https_ota` client, then update over WiFi from the Pages
  manifest, reboot into the other slot, and read `ota status`.
- Pull the AP mid-download: clean abort, running image untouched, retry resumes
  from the confirmed checkpoint.
- Publish `cmd/ota` from Home Assistant or `mosquitto_pub` and watch
  `state/ota` progress while the camera link survives.
- A deliberately broken image: failed health check, rollback to the old slot,
  and `state/ota` reporting `slot: aborted` with the rollback action.
