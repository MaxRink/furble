# tests/host/ (host regression tests)

Host tests must exercise real production control flow and preserve diagnostic
output on failure. ThreadSanitizer wrappers are fail-closed: every sanitizer
warning and every non-zero child status fails, regardless of report names or
the sanitizer's conventional exit code. The wrapper's exact completion marker
is `control-connect-camera-race: PASS`; near matches do not count. Keep the
shell contract test runnable without a compiler so wrapper behavior remains
covered even when the TSAN binary is unavailable.

Every other host test must pass under ASan and UBSan
(`tools/run_sanitized_host_tests.sh`) and, on Linux, LeakSanitizer. A host
double that stands in for a production type must keep its full value range:
UBSan rejects an enum value the stub does not declare. A shim that creates
objects for host tasks frees them after joining the threads, and a test that
replaces a thread's alternate signal stack restores the original one.

Provisioning tests cover Legend Off through the production apply path and NVS
store. Keep `Furble::Settings` qualified outside its namespace; run both the
ordinary and MQTT-enabled provisioning targets when changing this coverage.

## Linked boundary conformance

`boundaries/` and `headless/` compile production drivers and the production
`FURBLE_NO_DISPLAY` service loop against host peripheral doubles. The runner is
`sh sim/scripts/run-boundary-conformance.sh`; it builds the targets and checks
the production tokens named in `sim/coverage/hardware_quirks.json`. The test
targets are normal CTest entries, so the host CI job runs them.

The SD boundary compiles `src/FurbleSD.cpp` with `FURBLE_SD_MOUNT_POINT` set to
`/tmp/furble-boundary-sd`. That define is a host-only override; the firmware
default remains `/sd` and no device behavior changes. The same target sets
`FURBLE_GPX_DIRECTORY` for `src/FurbleGPX.cpp`.

The boundary log shim stores production log lines under a mutex because worker
tasks write them while the test thread polls. Keep `BoundaryHost::logContains()`
as the reader; do not return the underlying vector without the lock. The
`MockNimBLE::resetMock()` path resets the server services, callbacks, started
flag, counters and advertising counters, so a test never inherits the previous
case's GATT database.

`device-identity` covers the durable companion random-static identity,
including reboot restore and untouched camera bonds. `webui-handler` covers the
password half of the HTTP gate and the client socket count. `mqtt-owner` uses a
virtual timer: never reintroduce wall-clock reads into the host double, or an
offline-ACK deadline becomes a timing flake. `console-commands` pins the
`companion reidentify` command and `crash` argument validation.

## WebUI and companion contracts

- `webui-security` and `webui-handler` run the production WebUI gate and
  handlers against host doubles. Keep the 20,480/12,288 server floors, the
  4,608/4,096 supervisor floors, the 20,864/12,672 runtime identity floors and
  their refuse-then-retry path, the cached setting/password state, the
  streamed settings JSON and the identity persistence contract covered there.
  A new route or a new credential path needs a matching case.
- `companion-memory` pins the 7,552/3,456 companion first-enable floors.
- `ui-memory` pins `FurbleUIMemory::drawBufferBytes()` and the narrow-panel
  single-buffer rule. Update its expected sizes with any panel or row change.
- `companion-gatt` and `companion-auth` own the transport-side auth and bond
  state; `companion-fuzz` owns malformed and fragmented writes. Keep the
  `onAuthenticationComplete()` before `onConnect()` ordering and the
  no-Service-Changed contract covered. `device-identity` owns durable address
  restore and untouched camera bonds. The live LE address rotation is asserted
  by `sim/scenarios/e2e/hw-ble-auth-gatt-reidentify.txt`, not by the host
  double.
