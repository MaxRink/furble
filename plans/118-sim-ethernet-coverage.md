# 118 - Sim Ethernet coverage

Status: design only. Adds host coverage for the wired-Ethernet netif from
`plans/42-waveshare-eth-node.md`, proving the MQTT client is transport-agnostic.

**Codex-implementable, but BLOCKED on the FurbleEthernet implementation
(`plans/42`, NOT started).** This doc includes a **stub-Ethernet slice** Codex
can build the harness against now, so the transport-agnostic seam is locked in
before the W5500 driver exists.

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
  can drive `start()` -> emit `ETHERNET_EVENT_CONNECTED` -> emit
  `IP_EVENT_ETH_GOT_IP` with a synthetic IP, and `stop()` -> emit
  `ETHERNET_EVENT_DISCONNECTED`.
- An `eth_netif_test` that links the real `src/FurbleEthernet.cpp` (once it
  exists) against `MockEthNetif` and asserts:
  - `Ethernet::init()` installs the driver, registers `ETH_EVENT` and
    `IP_EVENT_ETH_GOT_IP`, and starts the netif.
  - On the got-IP event, the same generic network-up path the MQTT client keys
    on fires exactly once (assert via the `MockEspMqtt` from `plans/117`: the
    client's `start` is called after got-IP, never before).
  - A link-down event stops the client / marks the transport down without
    tearing the process.
  - The client is fed identically whether the up-event came from the eth mock or
    a WiFi mock (parameterise the same test over both transports to prove
    transport-agnosticism directly).
- A **stub-Ethernet slice**: a minimal `FurbleEthernet` singleton shaped like
  `plans/42` describes (`init()`, a got-IP callback into the shared network-up
  seam) with no `esp_eth` include, host-guarded. Codex builds and passes the
  harness against this now; the real W5500-backed `FurbleEthernet` swaps in with
  no test change.

Out of scope:

- The W5500 SPI bring-up, pin table and PoE. Hardware-only, `plans/42`.
- TLS to a real broker. `plans/42` verification.

## Files to change

- New `tests/host/eth/MockEthNetif.{h,cpp}`, `tests/host/eth/eth_netif_test.cpp`.
- `tests/host/CMakeLists.txt`: `add_executable(eth_netif_test ...)` and
  `add_test(NAME eth-netif COMMAND eth_netif_test)`.
- Reuse `tests/host/mqtt/MockEspMqtt` from `plans/117`.

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
cmake -S tests/camera -B build/camera-tests -DCMAKE_BUILD_TYPE=Release
cmake --build build/camera-tests --parallel 2
ctest --test-dir build/camera-tests -R eth-netif --output-on-failure
```

Exit 0 proves the eth netif brings up, emits got-IP, and starts the MQTT client
on it, with no W5500 and no wire. Against the stub slice it proves the harness
ahead of `plans/42`.

## Residual (Claude / hardware) verification

- `plans/42` hardware suite on a real Waveshare ESP32-S3-ETH: W5500 links, DHCP
  assigns an address, MQTT over TLS to a real broker, Fujifilm trigger fires.
  Only real hardware proves the SPI bring-up and the physical link.
