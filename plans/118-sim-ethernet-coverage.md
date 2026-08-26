# 118 - Sim Ethernet coverage

Status: host lifecycle coverage implemented on PR #170's Ethernet seam. The
test links the production `src/FurbleEthernet.cpp` and drives a deterministic
W5500-shaped transport without ESP-IDF, a broker, or hardware. MQTT startup
ordering remains a follow-up when the MQTT host seam lands.

The implementation is intentionally narrower than the original design: PR
#170 already provides the production Ethernet seam, so the host test covers
that seam directly. The MQTT callback integration stays with plan 117 because
PR #170 does not contain the MQTT client.

## Motivation

`plans/42` (Waveshare ESP32-S3-ETH) hinges on one property of the MQTT client:
it must key on a generic `IP_EVENT` and the default netif, not on WiFi, so an
Ethernet link feeds it the same way a WiFi link does. `plans/42` calls this "the
single cross-dependency and the one thing to get right in 33c for this board to
work". That property is untested off hardware and the board itself is expensive
and not yet in hand. A mocked `esp_eth` / netif in the host harness proves the
seam: bring the eth netif up, emit `IP_EVENT_ETH_GOT_IP`, and assert the MQTT
client starts on it, all without a W5500.

## Scope

In scope, under `tests/host/eth/` (host-only):

- A `MockEthNetif` that stands in for the `esp_eth` + `esp_netif` glue: a test
  can drive `start()` -> link-up -> got-IP with a synthetic address, and then
  link-down and reconnect.
- The existing `ethernet_test` links the real `src/FurbleEthernet.cpp` against
  `MockEthNetif` and asserts link/IP ordering, empty and stale IP rejection,
  duplicate suppression, clean stop, restart, and deterministic init/start
  failure recovery.

Out of scope:

- The W5500 SPI bring-up, pin table and PoE. Hardware-only, `plans/42`.
- TLS to a real broker. `plans/42` verification.

## Files to change

- New `tests/host/eth/MockEthNetif.{h,cpp}`.
- `tests/host/CMakeLists.txt` links the mock into the existing
  `ethernet_test`, registered as `ethernet-transport`.
- MQTT mock reuse is deferred to plan 117 because that seam is not present on
  PR #170.

## Settings and defaults

None. Test-only. `plans/42` owns any Ethernet settings.

## Dependencies

- `plans/42-waveshare-eth-node.md` FurbleEthernet: **hard blocker** for the real
  assertions. Harness + stub land first.
- `plans/33-wifi-hub.md` PR33c / #66 MQTT: the transport-agnostic client. If the
  client still keys on WiFi specifically when this lands, this test is the thing
  that catches it. That is the point.
- `plans/117-sim-mqtt-coverage.md`: reuses `MockEspMqtt`.

## Risks

- **The seam may not exist yet.** If PR33c ships a WiFi-coupled client, the
  stub-Ethernet harness will not be able to start it on the eth path, which is a
  true finding to raise against 33c, not a test bug. Document that the harness is
  also a design check on 33c.
- **Event ordering.** The client must start on got-IP, not on link-up (a link
  with no IP cannot reach a broker). Assert the ordering explicitly.
- Mock fidelity to the `esp_eth` / `esp_netif` event contract; cite the IDF
  `esp_eth` reference `plans/42` lists.

## Codex self-verification (headless)

```
cmake -S tests/host -B build/host-tests -DCMAKE_BUILD_TYPE=Release
cmake --build build/host-tests --parallel 2
ctest --test-dir build/host-tests -R ethernet-transport --output-on-failure
```

Exit 0 proves the Ethernet seam orders link-up before got-IP, rejects invalid
or stale addresses, suppresses duplicate notifications, and recovers from
driver initialization/start failures without a W5500 or wire. MQTT startup on
the callback remains covered by plan 117.

## Residual (Claude / hardware) verification

- `plans/42` hardware suite on a real Waveshare ESP32-S3-ETH: W5500 links, DHCP
  assigns an address, MQTT over TLS to a real broker, Fujifilm trigger fires.
  Only real hardware proves the SPI bring-up and the physical link.
