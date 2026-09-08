# 117 - Sim MQTT coverage

Status: host owner-loop harness implemented. The tests exercise the production
`src/FurbleMQTT.cpp` against deterministic esp-mqtt, timer and FreeRTOS queue
doubles; they remain host-only evidence and do not replace broker, simulator or
hardware validation.

The harness now specifically covers stale-client callback rejection, retained
actuator-command rejection, owner-task timer handling, and interval stop
serialization. The queue is intentionally bounded to match the production
contract. Timer callbacks only notify the owner task; the owner polls explicit deadlines,
so queue overflow cannot strand a shutter hold and stale timer callbacks cannot
release a newer hold or advance a restarted interval.
The host notification double verifies the wake signal and deadline ordering; it
does not measure real ESP scheduler latency or prove a five-millisecond expiry.
If an actuator release is rejected, its owner bit remains set with a pending
release marker. The owner retries that release on later iterations, including
while MQTT remains connected, and gates new presses or interval starts until
the camera accepts the cleanup.

Lifecycle events use a short mailbox rather than competing with payload events,
and a clean stop waits for the retained offline PUBACK or a bounded timeout
before destroying the client. Timeout is not broker-delivery proof. Replacement
startup clears copied events from the old session first.

The broker double matches subscribed topic levels, replays retained records when
a subscription is added, removes records on empty retained publishes, and drops
clean-session subscriptions on link loss or client destruction. These checks are
an in-process broker model, not wire-level MQTT or a real broker.
The lifecycle queue-saturation case uses an explicitly named raw callback
injection because the replacement client has no subscriptions until CONNECTED;
that seam is callback-queue coverage, not broker receipt or PUBACK proof.
The residual broker-model checks also cover fragmented and empty actuator
payloads, the hold upper bound, malformed location JSON, inactive-Control
rejection, QoS on retained online state, discovery-record deletion, and raw
client duplicate-init/reinitialization cleanup.

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
  records subscriptions.
- A `mqtt_owner_test` that links the real `src/FurbleMQTT.cpp` against
  deterministic `MockEspMqtt`, timer, queue and settings doubles.
- Assertions currently implemented:
  - On `MQTT_EVENT_CONNECTED`: publishes retained online/state/discovery records
    and subscribes to the command and Home Assistant status topics.
  - Inbound press/release routes to `Control::sendCommand` exactly once per
    delivery; a retained actuator command is rejected and publishes an error.
  - Hold expiry and interval stop are processed by the owner task after virtual
    time advances; timer firing notifies the owner task, and a stale client
    disconnect cannot change live state.
  - A full payload queue does not lose CONNECTED, and offline teardown completes
    on either a matching PUBACK or the bounded timeout.
  - Unsubscribed topics are not delivered, retained Home Assistant state is
    replayed after reconnect, and clean-session subscriptions reset on link loss.
Out of scope:

- A real broker or TLS. `plans/33` PR33c hardware verification covers Mosquitto
  and Home Assistant.
- The OTA topic beyond routing (the OTA state machine has its own tests in
  `plans/115`).

## Files to change

- New `tests/host/mqtt/` dependency doubles and `tests/host/mqtt_test.cpp`.
- `tests/host/CMakeLists.txt`: `mqtt_owner_test` links the production client
  through its host-only owner-task step seam.

## Settings and defaults

None. Test-only. The MQTT settings themselves are owned by PR33c.

## Dependencies

- `plans/33-wifi-hub.md` PR33c / #66 MQTT: broker and hardware validation remain
  separate from this host-only owner-loop harness.
- `plans/115-ota-update-mqtt.md`: shares the inbound-command routing seam; OTA
  routing remains outside this owner-loop regression.
- `plans/118-sim-ethernet-coverage.md`: the harness uses a generic host netif
  double to prove startup from an Ethernet-labelled GOT_IP condition.

## Risks

- **The mock must match esp-mqtt's event contract**, or the test passes against a
  fiction. Model the `esp_mqtt_event_t` fields the client actually reads and cite
  the IDF esp-mqtt reference `plans/33` already lists.
- **Topic-string brittleness.** The current test uses a fixed base and parses
  discovery JSON; add segmented-topic assertions if the base-topic contract
  changes.
- Keep the host dependency doubles aligned with the fields read by the real
  client; they must not become a second MQTT implementation.

## Codex self-verification (headless)

```
cmake -S tests/host -B build/host-tests -DCMAKE_BUILD_TYPE=Release
cmake --build build/host-tests --parallel 2
ctest --test-dir build/host-tests -R 'mqtt-owner|provision-apply-mqtt' --output-on-failure
```

Exit 0 proves owner-loop queue serialization, timer deadline handling, retained
command rejection and the MQTT-enabled provisioning branch with no broker and
no radio. It is host evidence only.

## Residual (Claude / hardware) verification

- `plans/33` PR33c hardware suite: real Mosquitto, real Home Assistant
  autodiscovery, retained-state-after-restart, LWT on battery pull. Only a real
  broker and a real radio prove those.
