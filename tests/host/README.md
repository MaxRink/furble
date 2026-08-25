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

## MQTT host broker model

`mqtt_host_broker_test` compiles the production MQTT module with host-only
dependency shims. Its deterministic in-process broker model enforces client
started/connected state, MQTT `+` and `#` subscription matching, retained
message delivery, and records outgoing QoS and retain flags. It has no socket,
TLS stack, broker persistence, QoS handshake, or concurrency model, so those
properties still require an integration or hardware gate.

Run it with the full host harness:

```sh
ctest --test-dir /tmp/furble-host-build -R mqtt-host-broker --output-on-failure
```
