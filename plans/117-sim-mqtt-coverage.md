# 117 - Sim MQTT coverage

Status: design only. Adds host coverage for the MQTT client from
`plans/33-wifi-hub.md` PR33c (the #66 MQTT work) using a mocked esp-mqtt /
loopback broker in the host harness.

**Codex-implementable, but BLOCKED on the MQTT client (#66 / PR33c) landing.**
The harness, the mock broker and the scenarios can be written now; they compile
and pass only once `src/FurbleMQTT.cpp` exists. This doc includes a
stub-`FurbleMQTT` slice Codex can build against so the harness lands green ahead
of the real client.

## Motivation

The MQTT client is the network control surface for the studio use case: it
publishes furble state, publishes Home Assistant discovery, and accepts inbound
command topics for shutter, settings and OTA. None of that is exercised off
hardware today. The existing host vehicles cover BLE (`tests/host`) and the UI
(`sim/`), but there is no seam for the MQTT event loop. A mocked broker in the
host harness lets the discovery payloads and the inbound command routing be
asserted deterministically, the same way `control_e2e` asserts the real control
state machine.

## Scope

In scope, under `tests/host/mqtt/` (host-only, release binary unchanged):

- A `MockEspMqtt` that stands in for `esp_mqtt_client_*`: it captures published
  topics/payloads/QoS/retain, lets a test inject `MQTT_EVENT_CONNECTED`,
  `MQTT_EVENT_DATA` (an inbound command) and `MQTT_EVENT_DISCONNECTED`, and
  records subscriptions. A loopback variant echoes published command topics back
  as inbound data for a round-trip.
- A `mqtt_client_test` that links the real `src/FurbleMQTT.cpp` (once it exists)
  against `MockEspMqtt` plus the existing `Control` doubles from
  `control_e2e/doubles/`.
- Assertions:
  - On `MQTT_EVENT_CONNECTED`: publishes `online` retained on
    `BASE/ID/status`, publishes retained state, then subscribes to the command
    topics, in that order (the ordering rule from PR33c).
  - Last-will config is `BASE/ID/status = offline`, QoS 1, retain true.
  - With `MQTT_HA` on: one retained publish to
    `homeassistant/device/furble_<ID>/config` with a `dev` and an `o` block and
    every entity carrying `unique_id` and `availability_topic`. With `MQTT_HA`
    off: no `homeassistant/...` topic is published.
  - Inbound `BASE/ID/cmd/shutter = hold 200` routes to `Control::sendCommand`
    (assert via the doubles' command counter) exactly once per delivery.
  - Inbound `cmd/shutter` with no camera connected publishes an error and does
    not enqueue a command.
  - `homeassistant/status = online` triggers a discovery republish.
  - `mqtt discovery clear` publishes an empty retained payload to the discovery
    topic.
- A **stub-`FurbleMQTT` slice**: a minimal `FurbleMQTT` with the connect/publish/
  subscribe/dispatch seams but no real esp-mqtt include, guarded so it compiles
  host-only. Codex builds the harness against this until #66 lands, then swaps to
  the real client with no test change.

Out of scope:

- A real broker or TLS. `plans/33` PR33c hardware verification covers Mosquitto
  and Home Assistant.
- The OTA topic beyond routing (the OTA state machine has its own tests in
  `plans/115`).

## Files to change

- New `tests/host/mqtt/MockEspMqtt.{h,cpp}`, `tests/host/mqtt/mqtt_client_test.cpp`.
- `tests/host/CMakeLists.txt`: `add_executable(mqtt_client_test ...)` and
  `add_test(NAME mqtt-client COMMAND mqtt_client_test)` inside the existing
  `enable_testing()` block, aggregated by `tests/camera`.
- Once #66 lands: link `src/FurbleMQTT.cpp` in place of the stub slice.

## Settings and defaults

None. Test-only. The MQTT settings themselves are owned by PR33c.

## Dependencies

- `plans/33-wifi-hub.md` PR33c / #66 MQTT: **hard blocker** for the real
  assertions. The harness + stub land first.
- `plans/115-ota-update-mqtt.md`: shares the inbound-command routing seam; the
  `cmd/ota` handler test there and the `cmd/shutter` routing test here use the
  same `MockEspMqtt`.
- `plans/118-sim-ethernet-coverage.md`: reuses `MockEspMqtt` to prove the client
  starts on the Ethernet netif.

## Risks

- **The mock must match esp-mqtt's event contract**, or the test passes against a
  fiction. Model the `esp_mqtt_event_t` fields the client actually reads and cite
  the IDF esp-mqtt reference `plans/33` already lists.
- **Topic-string brittleness.** Assert on parsed segments (`BASE`, `ID`,
  `cmd/shutter`) not on a full literal, so a base-topic change does not churn the
  test.
- The stub slice must not drift from the real client's seam names; keep them in
  one header the real client also uses.

## Codex self-verification (headless)

```
cmake -S tests/camera -B build/camera-tests -DCMAKE_BUILD_TYPE=Release
cmake --build build/camera-tests --parallel 2
ctest --test-dir build/camera-tests -R mqtt-client --output-on-failure
```

Exit 0 proves connect-order, discovery publication, and inbound command routing
with no broker and no radio. While #66 is unlanded, the same command passes
against the stub slice, proving the harness itself.

## Residual (Claude / hardware) verification

- `plans/33` PR33c hardware suite: real Mosquitto, real Home Assistant
  autodiscovery, retained-state-after-restart, LWT on battery pull. Only a real
  broker and a real radio prove those.
