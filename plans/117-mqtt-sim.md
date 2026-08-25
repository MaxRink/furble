# 117 - deterministic MQTT host broker model

Add host coverage for the MQTT service without requiring WiFi, an ESP32, or a
network broker. The original test seam was called a loopback harness even
though it only invoked the MQTT event callback directly. This plan names the
seam accurately and records the properties it can and cannot model.

## Design

The host target compiles the production `FurbleMQTT.cpp` with small dependency
shims. Its in-process broker model accepts publications, stores retained
records, enforces client state and subscription filters, and delivers retained
records after subscription. It removes retained records on an empty retained
publish, clears clean-session subscriptions on destruction or link loss,
rejects malformed filters, and applies the MQTT `$` wildcard rule. It records
outgoing QoS and retain flags. It does not claim to model a wire-level QoS
handshake, TLS, broker persistence, or concurrent esp-mqtt callbacks.

The command test covers generic network readiness, Home Assistant discovery,
retained Home Assistant status delivery, reconnect, offline command gating,
subscription filtering, fragmented frames, empty and malformed payloads,
oversized hold values, and inactive Control routing. It also exercises
unexpected link loss and a deterministic reconnect event that recreates the
clean-session subscriptions, retained discovery deletion, clean-session
subscription reset, invalid filters, and `$` topic exclusion. Raw client
fixtures verify repeated init/destroy/reset cleanup.

## Implementation state

- [x] Add the in-process broker model and rename the host target.
- [x] Enforce MQTT `+` and `#` filters and client started/connected state.
- [x] Model retained delivery and record outgoing QoS/retain flags.
- [x] Exercise framing, parser, command-state, reconnect, and fixture cleanup
      paths.
- [x] Delete retained records on empty retained publications and reset clean
      session subscriptions on client destruction.
- [x] Reject invalid wildcard filters and exclude `$` topics from wildcard
      subscriptions.
- [x] Document unsupported broker and hardware properties.

## Gates and deviations

The host suite has no real broker, socket, TLS, QoS handshake, esp-mqtt retry
backoff, or concurrency coverage. The link-loss helper emits the production
`MQTT_EVENT_DISCONNECTED` and the reconnect helper emits a subsequent
`MQTT_EVENT_CONNECTED`; it deliberately does not model esp-mqtt's internal
retry scheduler. PlatformIO firmware builds and hardware WiFi/HA validation
remain separate gates. The production MQTT implementation is unchanged apart
from the host-only task-step seam introduced by the parent MQTT feature.
