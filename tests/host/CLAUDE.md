# tests/host/ (host regression tests)

Host tests must exercise real production control flow and preserve diagnostic
output on failure. Board and platform policy tests pin hardware constants to
the vendor documents: `board-profile` covers every `Board::Profile` row,
`settings-default-policy*` the board-conditional defaults, and `ip5306-policy`
the IP5306 SYS_CTL0/SYS_CTL2 bits plus the boot keep-on write and readback on a
fake PMIC. Change a constant only with a citation. ThreadSanitizer wrappers are fail-closed: every sanitizer
warning and every non-zero child status fails, regardless of report names or
the sanitizer's conventional exit code. The wrapper's exact completion marker
is `control-connect-camera-race: PASS`; near matches do not count. Keep the
shell contract test runnable without a compiler so wrapper behavior remains
covered even when the TSAN binary is unavailable.

`control-adaptive-floor` owns the adaptive transmit policy's station floor. It
drives `Control::setWifiConnectedProbe()` directly and asserts the dBm the radio
is asked for, because the policy's input on device is the WiFi service's state
and its output is a radio request; neither is observable through another seam.
`FURBLE_HOST_RSSI_SAMPLE_INTERVAL_MS` shrinks only the sample cadence, so a
three-sample policy step is milliseconds rather than fifteen seconds. The
`hostAdaptiveEnabled()` and `hostTxPower()` knobs in `control/FurbleSettings.h`
default to the fresh-device values, so every other control target is unchanged.
`canon-identity-ack` asserts the `response` flag on the writes the Canon peers
receive, which is the only observable for "did the connect sequence wait for the
camera to accept this identity step".

`control-gps-race` is the GPS fan-out probe: one thread loops
`Control::updateGPS()` while the main thread alternates `addActive()` and
`disconnect()`. It sits beside `control-connect-camera-race` and
`control-connecting-snapshot` in the TSan exclusion list of
`tools/run_sanitized_host_tests.sh`, because `-fsanitize=thread` cannot share a
tree with ASan. Its plain twin, `control-gps-race-plain`, builds the same source
without the sanitizer, so the walk stays covered in the normal host suite and
under ASan and UBSan.

`fujifilm-journal-race` is the write-journal probe: one thread appends shutter
writes through `FujifilmVirtualCamera::write()` while the main thread takes
`writes()` snapshots and calls `clearEvents()`. The peer appends under
`Host::peerWriteJournalMutex()`, so the accessor has to take it as well; without
that, ASan and UBSan see only a torn vector and only when the window is hit,
which is how the sanitized job once failed on `control-e2e-multi-connect-fujifilm`.
It has no plain twin because no other sanitizer can see the overlap, and it is
in the same TSan exclusion list for the same reason.

Every other host test must pass under ASan and UBSan
(`tools/run_sanitized_host_tests.sh`) and, on Linux, LeakSanitizer. A host
double that stands in for a production type must keep its full value range:
UBSan rejects an enum value the stub does not declare. A shim that creates
objects for host tasks frees them after joining the threads, and a test that
replaces a thread's alternate signal stack restores the original one.

`imu-motion-scale` owns the motion scales. It pins the mg and ms every motion
configuration produces, the full-scale span of each register field and the
assumption boundary on the MPU6886 count size, and it drives
`sim/imu_motion_model.h`, the decision code the simulator's virtual engines run,
so the double cannot model a sensitivity the hardware does not have. It includes
`${FURBLE_ROOT}/sim` for that header and needs no stubs. `imu-motion-encoding`
owns the register bytes and decodes them back to mg through the same header.

`ricoh-wake` owns the Ricoh GR IV standby wake. Its peer models a sleeping body
(`setSleepingBody()`): the link stays up, CameraPower reads ON and OperationMode
reads BLE_STARTUP until the central drops and re-establishes the link, after
which it reports CAPTURE once the modelled wake delay has passed. The test
asserts both directions of the machine, one relink per attempt, at least one
post-relink poll, capture writes in order, no capture request while the body is
asleep (`captureRequestsWhileAsleep()`), and that a wake timeout refuses while
the relinked connection stays usable. `ricoh-sleep-gate` owns the phase 1
invariant on the same peer and asserts it as "no capture write", not "no write":
the wake repeats the connect-path identity write. Keep both: a peer that only
models a refusal cannot show that the gate survives a wake. Both cancel cases
must stay: the host build compiles `Ricoh.cpp`'s wait on the wall clock, so the
delay hook cannot reach it, and the probe-time cancel is delivered by a helper
thread that waits for the peer to accept the reconnect. Do not replace that with
a pre-press `cancelConnect()`, which the pre-relink check short-circuits before
the probe runs.

`ricoh-wake` also pins the control-task stall the wake would otherwise cause.
`Control::sampleAdaptivePower()` samples `Camera::getRssi()` from state active,
which is the only state a wake runs in, and the wake holds `Camera::m_Mutex` for
its relink (the settle, the reconnect and its handshake). The probe that follows
runs without it. A helper thread therefore hammers `getRssi()` while a
never-waking body is driven through a wake, and asserts the sampler sees
contended reads and reports the "no reading" 0 instead of waiting. Reverting the
`try_lock` in `getRssi()` makes every call wait the relink out, so the sampler
never sees a contended read and fails the assertion.

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

`boundary-shared-pin-shared`, `boundary-shared-pin-shared-off` and
`boundary-shared-pin-separate` compile the production `src/FurblePinShare.cpp`
with `src/FurbleFeedback.cpp` and `src/FurbleIR.cpp`. The `shared` case blocks a
frame inside `rmt_transmit`, then asserts that a blink cannot write the pin, that
the pad comes back from the RMT signal afterwards, and that the level requested
mid-frame is the one restored. `shared-off` boots with light feedback off, so no
level was ever recorded, and asserts the hand-over parks the pad at the profile's
inactive level on the GPIO output path instead of floating it.
The GPIO double deliberately lets a plain write reach the pad while a peripheral
owns it, so an arbitration regression fails instead of hiding behind the matrix.
`boundary-ir-polarity-*` runs one process per family and asserts the channel
inversion plus the level that reaches the emitter, and `pin-share-policy` pins the
pure state machine.

The SD boundary compiles `src/FurbleSD.cpp` with `FURBLE_SD_MOUNT_POINT` set to
`/tmp/furble-boundary-sd`. That define is a host-only override; the firmware
default remains `/sd` and no device behavior changes. The same target sets
`FURBLE_GPX_DIRECTORY` for `src/FurbleGPX.cpp`. Its unmount double can hold the
writer, so `boundary-sd-shutdown-refused` runs the production power-off
handshake against a writer that never answers: the test asserts the 3 s wait,
its 5 s extension, the refusal return value and both log lines, then releases
the write and checks the protected track still has its tail.
`boundary-sd-shutdown-undelivered` turns logging off behind a held unmount, fills
the writer queue, and asserts that `SD::powerOff()` refuses without waiting
rather than reporting a settled card, then releases the unmount and checks the
writer finishes it.
`FURBLE_GPX_DIRECTORY` for `src/FurbleGPX.cpp`. Its `export-full` mode pins the
settings backup contract: the first export must complete without the companion
password and without leaving `settings.tmp`, a second export must replace it, and
a third under a reduced `RLIMIT_FSIZE` must fail while `settings.txt` keeps its
previous contents. The mode defines `rename()` with FatFs semantics, `EEXIST`
when the destination exists, because the host libc replaces and would otherwise
hide the fact that the production export must drop the old copy first.

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

`boundary-feedback` compiles the production feedback driver. Its `sound` mode
waits out the amplifier settle window (`settleAmplifier()`, plans/363) only when
the session is cold, and covers the StickS3 battery volume cap end to end: 255 on
battery reaches the amplifier as 191 with the stored setting untouched, 255 on
VBUS stays 255, and a zero VBUS reading with the charge flag set still caps to
191, because a failed PMIC read reports "charging". `setBatteryPower()` in
`boundary_host.h` is that switch. Its `idle-window` mode is the amplifier-settle
regression: a pattern that follows a completed one inside the idle window sounds
its first tone on the next timer pass with no settle wait, a cold session still
waits, and console mode (no `lv_task_handler()`) plays instead of parking a tone
nothing would retry. `clearAmplifier()` in that file waits past the window.

`pmic-irq` links `include/FurblePmicIrq.h` and the latching PMIC shim only, so it
covers the header's clear sequence and the model, not the production call sites.
`platform-wake` is the production coverage: it compiles `src/FurblePlatform.cpp`
unchanged against `sim/shim/M5PM1.h` and drives `armMotionWake()`,
`motionWakeSample()`, `clearMotionWake()` and `disarmMotionWake()`. It reads the
PMIC through its own `M5PM1` handle because the model's retained state is static.
Unreferenced platform entry points are dropped by the platform linker's section
discard (`-Wl,-dead_strip`, `-Wl,--gc-sections`), which is what keeps the target
from needing Control, SD and TimeKeeper; without it the target stops linking, so
never remove it. `feedback-volume-policy` covers the cap arithmetic for both
states.

`boundary-headless-service` compiles the unchanged `src/main.cpp` with
`FURBLE_NO_DISPLAY` and asserts the boot order and the service loop. Its
`watchdog-refused` argument runs the same boot with the PMIC platform double
returning false from `watchdogEnable()`, and asserts the boot still reaches
every stage and that `M5PM1 watchdog arm refused` reached the log through the
boundary `esp_log` store. Never make a refused watchdog fatal: it is a degraded
state, not a boot loop.

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

## Rail ownership

`rail-account` pins the EXT_5V ownership accounting (`Furble::RailAccount` in
`include/FurbleRail.h`) and the output-mode supply interlock
(`PlatformPolicy::railEnableVerdict()`): the first holder switches the rail on,
the last release switches it off, a cut is refused while another holder remains,
and the interlock fails closed on an external source or an unreadable supply.
Both are header-only and the firmware and the simulator Platform drive them
unchanged; the refused probe and the deferred IR cut are covered by
`sim/scenarios/e2e/ext5v-rail-holders.txt`. Never reimplement the count in a
test double. See plans/implemented/364-ext5v-rail.md.

`boundary-platform-rail` compiles the production `src/FurblePlatform.cpp` itself
against `boundaries/platform_shims/M5PM1.h`, a register-bus fake that records
every `PWR_CFG` 0x06 write. It is the only target that links that file, and it
exists because `rail-account` and the e2e scenarios both kept passing when the
cut decision there was reverted to `false`. It asserts that a refused enable
writes nothing, that a refused cut writes nothing while the last release still
cuts, and that an interleaved LED and rail update each keep the other's bit and
serialize. Change those assertions when the rail policy changes.

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
rate fields fails here. `gps-text` covers the vendor text seam
(`lib/furble/protocol/GpsText.{h,cpp}`) and both dialects behind it
(`GpsQuectel`, `GpsUnicore`): framing against checksums the Quectel documents
print, prefix routing with `$PCAS` and `$GPGGA` negatives, reply parsing, every
Quectel result code, `$OK` and `$FAIL`, the session's ack, nack, timeout, retry
and "processing" behaviour, both planners and the demultiplexer's vendor
pieces. `gps-text-sim` drives the same seam end to end against
`sim/vendor_gnss.cpp`, so a model that stops answering like the documented part
fails there rather than only in a scenario. Both take their facts from
plans/388, which cites the Quectel and Unicore documents by section and page.
`gps-text` also pins that the `GpsText` constellation and power-save values
equal the `Ubx` ones, because they are one stored setting. `console-commands`
covers `gps text` and the `backend:` line the `gps` summary prints.
`tests/test_gnss_command_contract.py` is the source
half of the same rule: `$PCAS10` only inside `GPS::restart()`, no hand-written
`$PCAS03` in the driver, and `$PCAS04` bounded at the seven values both CASIC
revisions agree on. `gps_ubx_fuzz` checks `Ubx::Stream` against a reference
splitter built on `scan()`, intact-frame delivery, and that chunked demux
input yields the same frames and NMEA text as one-shot input, and it builds
a plan from a random request so any key the request's capability says the
receiver lacks fails the run. `testDualBand()` covers the four L5 key ids,
`capsFromVersion()`, both `maxRateHz()` tables, the dual-band plan and the
`CapsProbe` state machine; `testDualBandVariant()` drives both virtual
receiver variants, including the NAK an L1-only part returns for an L5 key
and the refusal a band-splitting plan gets. They run under ASan and UBSan
like every fuzz target.

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
- The NimBLE mock models one ATT MTU per link. `NimBLEDevice::setMTU/getMTU`
  hold the local preference (default 256, the sdkconfig value),
  `NimBLEClient::connect` starts a link at the mandatory 23 and runs the
  exchange, `getMTU()` reports the negotiated value, and
  `writeValue`/`setValue` refuse a value above `MTU - 3` and count it in
  `mockAttWriteRejections()`, which is the only observable for the host-side
  `BLE_HS_EMSGSIZE` failure. `NimBLEMockPeer::negotiateAttMtu()` defaults to 517
  so a suite that does not model a narrow body keeps working, and a peer that
  answers 23 models a body without MTU support. `sizeof(NimBLEClient)` is
  charged against the modelled device heap by `sim/heap_count.cpp`, so a field
  that does not fit the existing padding moves every calibrated floor in
  `sim/scenarios/e2e/hw-memory-*.txt`. Keep the object the same size, and
  measure it with a `sizeof` probe on both ABIs when adding one.
- The port stack moved the host doubles with the production seams. The
  companion doubles implement `Platform::getBatteryStatus()` instead of
  `UI::getBattery*()`. The headless doubles gained `TxPower`,
  `Control::savedTxPower()` and `GPS::setRailPower()`. The NimBLE mock records
  each `setSecurityIOCap()` value in `mockSecurityIOCaps()`, cleared by
  `resetMock()`. `tx-power-mapping` pins the power mapping through `Control`.
