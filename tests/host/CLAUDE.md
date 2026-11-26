# tests/host/ (host regression tests)

Host tests must exercise real production control flow and preserve diagnostic
output on failure. ThreadSanitizer wrappers are fail-closed: every sanitizer
warning and every non-zero child status fails, regardless of report names or
the sanitizer's conventional exit code. The wrapper's exact completion marker
is `control-connect-camera-race: PASS`; near matches do not count. Keep the
shell contract test runnable without a compiler so wrapper behavior remains
covered even when the TSAN binary is unavailable.

`control-gps-race` is the GPS fan-out probe: one thread loops
`Control::updateGPS()` while the main thread alternates `addActive()` and
`disconnect()`. It sits beside `control-connect-camera-race` and
`control-connecting-snapshot` in the TSan exclusion list of
`tools/run_sanitized_host_tests.sh`, because `-fsanitize=thread` cannot share a
tree with ASan. Its plain twin, `control-gps-race-plain`, builds the same source
without the sanitizer, so the walk stays covered in the normal host suite and
under ASan and UBSan.

Every other host test must pass under ASan and UBSan
(`tools/run_sanitized_host_tests.sh`) and, on Linux, LeakSanitizer. A host
double that stands in for a production type must keep its full value range:
UBSan rejects an enum value the stub does not declare. A shim that creates
objects for host tasks frees them after joining the threads, and a test that
replaces a thread's alternate signal stack restores the original one.

Provisioning tests cover Legend Off through the production apply path and NVS
store. Keep `Furble::Settings` qualified outside its namespace; run both the
ordinary and MQTT-enabled provisioning targets when changing this coverage.

## Simulator shims

`pmic_recovery_test` and `sim_clock_test` include `sim/shim/M5PM1.h`, so they
cover the host PMIC model: the idle-wake retry, the retained download lock and
watchdog, the IRQ Status 1 and 2 latches (write 0 to clear, unmasked bits hold
the aggregated line) and a watchdog expiry as a system power cycle counted by
`watchdogPowerCycles()`. Keep them aligned with M5PM1 v1.0.6 when the pinned
driver version in `platformio.ini` changes.

## Linked boundary conformance

`boundaries/` and `headless/` compile production drivers and the production
`FURBLE_NO_DISPLAY` service loop against host peripheral doubles. The runner is
`sh sim/scripts/run-boundary-conformance.sh`; it builds the targets and checks
the production tokens named in `sim/coverage/hardware_quirks.json`. The test
targets are normal CTest entries, so the host CI job runs them.

`boundary_host.h` timestamps GPIO level changes on a virtual microsecond clock
that only the `esp_rom_delay_us()` double advances, so a test can measure a
production busy-wait pulse instead of asserting a pin number. It measures the
requested delay, not elapsed time. `boundary_ethernet_test` uses it for the
500 us W5500 RESETn minimum; keep new timing doubles on the same clock. That
test snapshots the edge count before every `init()`, and its ordering check
requires the init to add its own low and high edges, so a pulse moved after the
driver install cannot pass on an earlier init's edges. Keep any new ordering
check on the same baseline rather than on the whole edge list.

The SD boundary compiles `src/FurbleSD.cpp` with `FURBLE_SD_MOUNT_POINT` set to
`/tmp/furble-boundary-sd`. That define is a host-only override; the firmware
default remains `/sd` and no device behavior changes. The same target sets
`FURBLE_GPX_DIRECTORY` for `src/FurbleGPX.cpp`.

The `gpx-writer` suite sets `FURBLE_GPX_DIRECTORY` to an absolute sandbox in
the build tree, deliberately long. `GPX::open()` sizes its path buffer from
the directory and fails on a truncated path, so any build tree works; a
64-byte buffer once truncated it in the deeper sanitized tree.

The boundary log shim stores production log lines under a mutex because worker
tasks write them while the test thread polls. Keep `BoundaryHost::logContains()`
as the reader; do not return the underlying vector without the lock. The
`MockNimBLE::resetMock()` path resets the server services, callbacks, started
flag, counters and advertising counters, so a test never inherits the previous
case's GATT database.

`time-keeper-persistence` models the ESP system clock through the weak
`furble_sim_system_time_us()` hooks: a soft reset keeps it running while
esp_timer restarts, a power-on loses it. Its `time_keeper_stubs/esp_system.h`
sets the reset reason with `#include_next`, so keep the settings stub's
`esp_restart()` reachable.

`device-identity` covers the durable companion random-static identity,
including reboot restore and untouched camera bonds. `webui-handler` covers the
password half of the HTTP gate and the client socket count. `mqtt-owner` uses a
virtual timer: never reintroduce wall-clock reads into the host double, or an
offline-ACK deadline becomes a timing flake. `console-commands` pins the
`companion reidentify` command and `crash` argument validation.

## GPS receiver protocols

`gps-ubx` covers the UBX codec, the settings planner, the `Settle` gate
(including ticks around 2^31) and the `GpsDemux` split. Its
`testB1cRejections()` case pins what the receiver refuses: BeiDou B1C is
selected by BeiDou plus GLONASS, B1C refuses AssistNow Autonomous, and the
`OPERATEMODE=FULL` write is ordered before the signal batch. `gps-ubx-m10`
drives the simulator's virtual M10 (`sim/ubx_m10.cpp`) directly, with no clock
or UART, so its validity rules and start types are tested where the scenarios
rely on them; `testB1cConfigOrder()` replays the signal and power writes in
both orders and proves the receiver NAKs the wrong one. `gps-casic` covers
`Casic::sentenceCommand()`, so a `$PCAS03` body that stops before its fourteen
rate fields fails here. `tests/test_gnss_command_contract.py` is the source
half of the same rule: `$PCAS10` only inside `GPS::restart()`, no hand-written
`$PCAS03` in the driver, and `$PCAS04` bounded at the seven values both CASIC
revisions agree on. `gps_ubx_fuzz` checks `Ubx::Stream` against a reference
splitter built on `scan()`, intact-frame delivery, and that chunked demux
input yields the same frames and NMEA text as one-shot input. It runs under
ASan and UBSan like every fuzz target.

## WebUI and companion contracts

- `webui-security` and `webui-handler` run the production WebUI gate and
  handlers against host doubles. Keep the 20,480/12,288 server floors, the
  4,608/4,096 supervisor floors, the 20,864/12,672 runtime identity floors and
  their refuse-then-retry path, the cached setting/password state, the
  streamed settings JSON and the identity persistence contract covered there.
  A new route or a new credential path needs a matching case.
- `companion-memory` pins the 8,320/4,224 companion first-enable floors.
- `control-e2e-admit-hook-once-per-target` owns where connect admission
  runs: inside `Control::addActive()`, once per new target, never for a
  duplicate. The other Control harnesses register no admit hook and do not
  link the governor.
- `ui-memory` pins `FurbleUIMemory::drawBufferBytes()` and the narrow-panel
  single-buffer rule. Update its expected sizes with any panel or row change.
- `companion-gatt` and `companion-auth` own the transport-side auth and bond
  state; `companion-fuzz` owns malformed and fragmented writes. Keep the
  `onAuthenticationComplete()` before `onConnect()` ordering and the
  no-Service-Changed contract covered. `device-identity` owns durable address
  restore and untouched camera bonds. The live LE address rotation is asserted
  by `sim/scenarios/e2e/hw-ble-auth-gatt-reidentify.txt`, not by the host
  double.
- The port stack moved the host doubles with the production seams. The
  companion doubles implement `Platform::getBatteryStatus()` instead of
  `UI::getBattery*()`. The headless doubles gained `TxPower`,
  `Control::savedTxPower()` and `GPS::setRailPower()`. The NimBLE mock records
  each `setSecurityIOCap()` value in `mockSecurityIOCaps()`, cleared by
  `resetMock()`. `tx-power-mapping` pins the power mapping through `Control`.
