# Host camera harness

The host harness compiles the production Fujifilm Basic client with macOS
clang or a normal Linux C++ compiler. It uses the in-memory NimBLE seam in
`nimble/` and the server-side `peer/FujifilmVirtualCamera` model. No radio,
ESP-IDF, PlatformIO, or Arduino dependency is required.

The current master branch does not contain the Tier B mock from the plan. This
directory carries the small compatible seam needed to build Tier C standalone.
When Tier B lands, the virtual peer API is the boundary to preserve.

Build and run:

```sh
cmake -S tests/host -B /tmp/furble-host-build
cmake --build /tmp/furble-host-build
ctest --test-dir /tmp/furble-host-build --output-on-failure
```

The replay test takes the synthetic capture path from the CMake source
directory. Real X100VI captures are intentionally not included. Plan 64's BT
journal work must produce reviewed, normalized vectors before this fixture can
be replaced with hardware evidence.

## BLE lifecycle fuzzer

`fuzz/control_fuzz.cpp` (plan 104) drives the real `Furble::Control` state
machine through seeded, randomized sequences of connect/disconnect/reconnect
operations with injected BLE faults, asserting state, leak, wedge, and handshake
invariants after each step. It builds under AddressSanitizer and
UndefinedBehaviorSanitizer. Run a seed directly:

```sh
./control_fuzz <seed> [iterations]        # e.g. ./control_fuzz 42 20
./control_fuzz --repro missing-shutter-service
```

The CI job runs five fixed seeds plus two seed-pinned regression guards for the
FujifilmBasic missing-shutter findings, both now fixed and passing.

## Wired Ethernet lifecycle

`ethernet-transport` links the production `src/FurbleEthernet.cpp` against the
deterministic `eth/MockEthNetif` transport. It covers the W5500-shaped event
order without ESP-IDF or hardware: link-up must precede a usable DHCP address,
empty and stale addresses are ignored, duplicate addresses do not refire the
network-up callback, and init/start failures recover cleanly. The main host CI
job runs this test with the rest of the CTest suite.

This does not prove SPI pin wiring, W5500 PHY behavior, DHCP on a physical LAN,
TLS, or MQTT broker behavior. Those remain the hardware boundary in
`plans/42-waveshare-eth-node.md` and the MQTT host coverage in plan 117.

`poe-power-model` is a test-only fixture for the Waveshare ESP32-S3-ETH power
topology. Its default is no optional PoE HAT, unavailable PoE, link down, and no
USB/external power. HAT presence/capability, PoE availability, Ethernet link,
and USB/external power are independent explicit observations, including an
unknown state. The fixture never infers PoE from Ethernet link or USB power;
the firmware has no documented signal to sense the optional module or PoE
negotiation. Run it with:

```sh
ctest --test-dir /tmp/furble-host-build -R '^poe-power-model$' --output-on-failure
```
