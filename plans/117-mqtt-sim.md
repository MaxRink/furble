# 117 - deterministic MQTT host broker model

Add host coverage for the MQTT service without requiring WiFi, an ESP32, or a
network broker. The original test seam was called a loopback harness even
though it only invoked the MQTT event callback directly. This plan names the
seam accurately and records the properties it can and cannot model.

## Design

The host target compiles the production `FurbleMQTT.cpp` with small dependency
shims. Its in-process broker model accepts publications, stores retained
records, enforces client state and subscription filters, and delivers retained
records after subscription. It records outgoing QoS and retain flags. It does
not claim to model a wire-level QoS handshake, TLS, broker persistence, or
concurrent esp-mqtt callbacks.

The command test covers generic network readiness, Home Assistant discovery,
retained Home Assistant status delivery, reconnect, offline command gating,
subscription filtering, fragmented frames, empty and malformed payloads,
oversized hold values, and inactive Control routing. Raw client fixtures also
verify repeated init/destroy/reset cleanup.

## Implementation state

- [x] Add the in-process broker model and rename the host target.
- [x] Enforce MQTT `+` and `#` filters and client started/connected state.
- [x] Model retained delivery and record outgoing QoS/retain flags.
- [x] Exercise framing, parser, command-state, reconnect, and fixture cleanup
      paths.
- [x] Document unsupported broker and hardware properties.

## Gates and deviations

The host suite has no real broker, socket, TLS, QoS handshake, or concurrency
coverage. PlatformIO firmware builds and hardware WiFi/HA validation remain
separate gates. The production MQTT implementation is unchanged apart from the
host-only task-step seam introduced by the parent MQTT feature.
