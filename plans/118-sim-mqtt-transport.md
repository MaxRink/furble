# 118 - Optional simulator MQTT transport

Status: implemented as an opt-in native transport. The default simulator
build remains unchanged because the transport is enabled only with
`FURBLE_SIM_MQTT=1` and installed libmosquitto plus cJSON.

The opt-in build compiles the production `src/FurbleMQTT.cpp` together with
the production Control, UI and CameraList sources. `sim/SimMQTTClient.cpp`
adapts only the ESP MQTT calls that production MQTT uses to libmosquitto.
Callbacks stay asynchronous and enter the existing production owner queue;
the simulator does not add a second command path.

The adapter accepts `mqtt://host[:port]` and rejects `mqtts://` until a
trust-store-backed TLS adapter exists. The local broker, socket, PUBACK and
credential evidence from this mode are separate from the in-process host
broker model in plan 117 and from hardware validation.
The host broker-model residual checks tracked here remain test-only evidence;
they do not add a second command path to the native adapter or expand its
plaintext-only validation boundary.

## Build configuration

CMake users opt in with:

```
cmake -S sim -B build/sim-mqtt -DFURBLE_SIM_MQTT=ON
```

The shell build uses the equivalent `FURBLE_SIM_MQTT=1 sh sim/build.sh` and
requires libmosquitto plus cJSON. Both paths prefer the ESP-IDF
`components/json/cJSON/cJSON.c` source when `IDF_PATH` (or
`FURBLE_IDF_JSON_DIR`) contains its matching `cJSON/cJSON.h`, and otherwise
use installed pkg-config packages. Neither path bundles an MQTT implementation
or a broker.

The simulator source inventory names `src/FurbleMQTT.cpp` directly in the
CMake source block so the host inventory checker can compare it with the
guarded shell-build entry. The production translation unit is preprocessor
empty unless `FURBLE_MQTT` is enabled, so this keeps the default simulator
transport-disabled while making both source inventories explicit.

The simulator supplies only the shared ESP event/netif/CRT symbols needed by
the production source. Its existing virtual timer dispatcher is reused. The
FreeRTOS task-notification scheduler shim is reused. Simulator teardown sends
the owner task a notification and waits for an explicit transport-destroyed
acknowledgment before joining simulator tasks. If the bounded wait expires,
the run is reported failed rather than freeing a live transport.

## Evidence boundary

The 2026-09-07 native CMake build exposed a `getRSSI()` mutex type mismatch.
Using the existing `connect_mutex_t` alias fixed compilation without changing
firmware locking. The rebuilt simulator passed `e2e/boot-splash.txt` against
a loopback-only Mosquitto broker on port 18884, reporting MQTT connected and
offline PUBACK received before successful exit. Evidence is in
`/tmp/c66-native-smoke.log`. This smoke run does not exercise MQTT commands,
retained replay, broker restart, TLS, or hardware.

The remaining regression needs a separately managed local broker and must report plaintext
socket and broker-receipt evidence separately from TLS, real ESP-IDF and
hardware evidence. `sim/scripts/test-mqtt-broker.sh` supplies that bounded
regression for a built opt-in binary; its offline assertion is broker receipt,
plus the owner-side PUBACK callback, not hardware/TLS proof. The script uses a
measured simulator ID to seed a retained command before subscription and
asserts the real camera-task shutter counter remains zero, and preserves failure
artifacts. The script does not restart its externally managed broker, so active
camera resubscription after broker restart is explicitly reported as not
exercised. It requires `FURBLE_SIM_MQTT_ID` from the measured status topic of
the exact binary under test rather than assuming a portable device ID. A fresh
fixture assigns saved camera ID `1`; set `FURBLE_SIM_MQTT_CAMERA_ID` when using
an existing preference store.

Root-reported limited validation on 2026-09-07 used a binary built from
`111c73c6` and script `1d055648` against a loopback Mosquitto broker. It
observed MQTT connection, saved-camera routing, retained-command rejection,
and the owner-side offline PUBACK callback. The earlier failed attempt was a
finite virtual-clock window racing host-side broker I/O: the external command
arrived after the scenario's virtual window had elapsed. This is simulator
coordination-window evidence, not a production UI-service ordering result.
The result does not cover broker restart or active-camera resubscription,
TLS, real ESP-IDF networking, or hardware.

Root reports the final integrated-head validation at `3dd77276e4b481cf404dd6fb4304f28ec0df54f9`
(including the reviewed empty-selection Control guard): the native IDF/cJSON
build passed (`/tmp/c66-native-final-build.log`) and the loopback Mosquitto
regression passed with `sim/scripts/test-mqtt-broker.sh`
(`/tmp/c66-native-broker-final.log`). These are root-run artifacts, not tests
run in this worktree. The scope remains plaintext local-broker behavior only;
broker restart/resubscription, TLS, real ESP-IDF networking, and hardware are
not covered.
