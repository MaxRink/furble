# MQTT and Home Assistant discovery

furble is an MQTT client. It publishes state, accepts commands, and publishes
Home Assistant discovery records. The implementation is `src/FurbleMQTT.cpp`, the
memory contract is `include/FurbleMQTTMemory.h`.

| Item | Value |
| :--- | :--- |
| Status | compiled on the 8 MB and 16 MB profiles only (`-DFURBLE_MQTT=1`) |
| Transport | `mqtt://` or `mqtts://`. WebSocket is not enabled in any sdkconfig (`src/FurbleMQTT.cpp:714-717`) |
| Settings | MQTT base topic (default `furble`), URI, user and password (`src/FurbleMQTT.cpp:680-688`) |
| Component page | none. There is no MQTT chip. |
| Setting semantics | [Settings and controls](../settings-and-controls.md#mqtt-8-mb-debugconsole-builds) |

## Topic tree

The root is the base topic plus the device string id
(`src/FurbleMQTT.cpp:688`). With the default base topic `furble` and an id of
`ABCDEF`, the topics are:

| Topic | Direction | Payload | Citation |
| :--- | :--- | :--- | :--- |
| `furble/<id>/status` | furble publishes, retained | `online` or `offline`. `offline` is also the last will | `src/FurbleMQTT.cpp:689,814-818,1027` |
| `furble/<id>/state/cameras` | publish | JSON array of camera records | `src/FurbleMQTT.cpp:690` |
| `furble/<id>/state/battery` | publish | JSON object | `src/FurbleMQTT.cpp:691` |
| `furble/<id>/state/gps` | publish | JSON object, also the Home Assistant JSON attributes topic | `src/FurbleMQTT.cpp:692,2032-2035` |
| `furble/<id>/state/interval` | publish | JSON object | `src/FurbleMQTT.cpp:693` |
| `furble/<id>/state/shutter` | publish | `held` or `idle` | `src/FurbleMQTT.cpp:694,1155,1172` |
| `furble/<id>/state/error` | publish | one error string | `src/FurbleMQTT.cpp:695` |
| `furble/<id>/camera/<camera_id>/state` | publish | one camera record | `src/FurbleMQTT.cpp:2076` |
| `furble/<id>/cmd/shutter` | subscribe | `press`, `release` or `hold <ms>` | `src/FurbleMQTT.cpp:696` |
| `furble/<id>/cmd/focus` | subscribe | `press` or `release` | `src/FurbleMQTT.cpp:697` |
| `furble/<id>/cmd/interval` | subscribe | `start` or `stop` | `src/FurbleMQTT.cpp:698` |
| `furble/<id>/cmd/connect` | subscribe | `all`, empty, or a decimal camera id | `src/FurbleMQTT.cpp:699` |
| `furble/<id>/cmd/disconnect` | subscribe | any payload | `src/FurbleMQTT.cpp:700` |
| `furble/<id>/cmd/location` | subscribe | JSON with `latitude`, `longitude` and `timestamp` | `src/FurbleMQTT.cpp:701,1248-1268` |
| `furble/<id>/cmd/ota` | subscribe | `status`, `check`, `check <url>`, `abort`, or an `https://` URL | `src/FurbleMQTT.cpp:801,1255,1478` |
| `furble/<id>/state/ota` | publish | flat JSON OTA status record; retained final result | `src/FurbleMQTT.cpp:802,1500`, `src/FurbleOTAStatus.cpp:148` |
| `homeassistant/status` | subscribe | `online` re-publishes discovery | `src/FurbleMQTT.cpp:57,1131-1136` |

Command topics are matched exactly, not by prefix, so a trailing level or a
wildcard subscription on the broker does not widen what furble accepts
(`src/FurbleMQTT.cpp:1128-1136`).

## Command semantics

| Command | Accepted values | Rejection and error | Citation |
| :--- | :--- | :--- | :--- |
| Shutter | `press`, `release`, `hold <ms>` with ms in 0 to 60000 | `shutter press rejected: actuator release pending`, `shutter hold must be 0-60000 ms`, `unknown shutter command` | `src/FurbleMQTT.cpp:1138-1176`, `include/FurbleMQTT.h:93` |
| Focus | `press`, `release` | `unknown focus command`, `focus command rejected` | `src/FurbleMQTT.cpp:1177-1207` |
| Interval | `start`, `stop` | `unknown interval command` | `src/FurbleMQTT.cpp:1208-1217` |
| Connect | empty, `all`, or a decimal id that is not 0 and not 255 | `camera id is invalid`, `connect request rejected` | `src/FurbleMQTT.cpp:1219-1238` |
| Disconnect | any | `disconnect request rejected` | `src/FurbleMQTT.cpp:1239-1246` |
| Location | JSON with a positive `timestamp` | `location payload is not JSON`, `location needs latitude, longitude and timestamp` | `src/FurbleMQTT.cpp:1248-1268` |

A **retained** message on any `cmd/` topic is rejected with
`retained MQTT command rejected` and is not acted on, so a broker restart cannot
replay an old shutter command (`src/FurbleMQTT.cpp:1121-1124`).

## OTA command and status

`furble/<id>/cmd/ota` carries the bare payload form, so a dashboard, a shell
script and `mosquitto_pub` all use the same bytes. Surrounding ASCII whitespace
is ignored and keywords are lower case (`src/FurbleOTACommand.cpp:68`).

| Payload | Effect |
| :--- | :--- |
| `status` | Re-publish the current `state/ota` record. |
| `check` | Look up the last URL this device was given. |
| `check <https-url>` | Look up the available version and slot for that URL. |
| `abort` | Cancel the running update. |
| `<https-url>` | Start an update from that URL. |

Every refusal is published as `ota <command> refused: <reason>` on
`state/error`, and the same reason is a field of the `state/ota` record. The
reasons are `busy`, `camera_session`, `intervalometer`, `link_down`,
`battery_low`, `slot_unavailable`, `source_unavailable`, `no_newer_version`,
`invalid_source`, `retained_command` and `transport_unavailable`
(`include/FurbleOTAAdmission.h:50`, `src/FurbleOTAAdmission.cpp:120`).

The preconditions are enforced, not advisory. An update is refused while any
camera session owns the radio, while the intervalometer runs, while the station
link is down, while the inactive slot is missing, and below 30 percent battery
unless the board reports external power from a positive VBUS reading. The charge
flag alone is not that evidence: a failed PMIC read reports "charging", and the
board's charge *capability* is a compile-time constant, so only a VBUS
measurement exempts the floor. A board that cannot sense VBUS therefore keeps
the floor, and so does one whose charge flag is set. A board without a battery
gauge is admitted, because there is no threshold to evaluate.

The gates are evaluated once, when the command arrives, not continuously. A
camera session started while a transfer runs is not refused retroactively; the
transfer continues and two radios share the antenna. A download link that dies
mid-transfer surfaces as `transfer_interrupted` from the adapter, which discards
the staged image and leaves the running slot untouched.

A different URL while an update runs
is `busy`; the identical URL is an idempotent duplicate, so a QoS-1 redelivery
cannot start a second download. The version comparison needs a strictly newer
dotted numeric release, and an installed `dev+g<hash>` build counts as older
than any release (`include/FurbleOTAAdmission.h:75`,
`src/FurbleOTAAdmission.cpp:143`).

A check reads the release manifest, not the image: it never opens the download.
An update additionally requires the manifest image to fit the inactive slot.
The handler reads battery, control, intervalometer and link state through one
context owned by the MQTT task (`src/FurbleMQTT.cpp:1544`).

`state/ota` is a flat object with stable keys: `state`, `error`, `progress`,
`bytes`, `total`, `reason`, `detail`, `command`, `result`, `installed`,
`available`, `update`, `checked`, `busy`, `slot`, `boot` and `failure`
(`include/FurbleOTAStatus.h:66`, `src/FurbleOTAStatus.cpp:148`). `state` and
`error` come from the OTA engine, `reason` from the precondition policy or the
handler, `detail` from the delivery adapter, and `slot`/`boot` from the boot
health verdict (`include/FurbleOTAStatus.h:52`). The adapter labels a rejected
digest or an interrupted transfer with `reasonName()`
(`src/FurbleOTAHTTPS.cpp:62`). A pending or rolled-back image is therefore
visible on the same retained record as the update result.

Only a final `done`, `error` or `aborted` record is retained, so a dashboard
that connects later still sees the result. Live progress is not retained. The
first `checking` record of a new update is retained once, which replaces the
previous retained result instead of leaving it on the broker for the whole
transfer (`src/FurbleOTACommand.cpp:321`).

No HTTPS download is linked in this build. Without an attached transport the
topic still answers, with `transport_unavailable`, so a dashboard can tell a
build without OTA from an idle device (`src/FurbleMQTT.cpp:1524`). The console
command `ota status` prints the same record (`src/FurbleConsole.cpp:3587`).

The 60 s hold limit and the 256-byte topic limit are
`include/FurbleMQTT.h:93,96`.

## State payloads

`state/battery` carries `level`, `voltage`, `current` and `charging`
(`src/FurbleMQTT.cpp:1778-1791`).

`state/gps` carries `fix`, `source` (`uart` or `external`), `satellites`,
`latitude`, `longitude`, `age`, and `altitude` or `accuracy` only when they are
valid (`src/FurbleMQTT.cpp:1794-1820`).

`state/cameras` is an array of objects with `name`, `type`, `connected`,
`progress` and `rssi` (`src/FurbleMQTT.cpp:1740-1749`). The per-camera record adds
`id`, `state` (`connected`, `connecting` or `disconnected`) and `active`
(`src/FurbleMQTT.cpp:1754-1774`).

RSSI is bucketed to a multiple of 5 and clamped to -125 to -5, with -127 as the
unknown value, so a jittering link does not republish on every sample
(`src/FurbleMQTT.cpp:217-224`).

An interval payload carries `running`, `remaining`, `total` and the next
deadline (`src/FurbleMQTT.cpp:1822-1840`).

## Home Assistant discovery

| Item | Value | Citation |
| :--- | :--- | :--- |
| Discovery topic, hub | `homeassistant/device/furble_<id>/config` | `src/FurbleMQTT.cpp:702` |
| Discovery topic, camera | `homeassistant/device/furble_<id>_<camera_id>/config` | `src/FurbleMQTT.cpp:2051-2053,2089` |
| QoS and retain | QoS 1, retained | `src/FurbleMQTT.cpp:1929` |
| Availability | `online` and `offline` on the status topic, plus the camera state topic for camera entities | `src/FurbleMQTT.cpp:107-142` |
| Re-publish trigger | `homeassistant/status` with payload `online` | `src/FurbleMQTT.cpp:1131-1136` |
| Removed camera | the old discovery record is published empty and retained, which deletes it | `src/FurbleMQTT.cpp:2094-2106` |

Hub components, in record creation order: `shutter` (button, payload
`hold 200`), `focus` (button, payload `press`), `interval` (switch),
`battery` (sensor, percent), `cameras` (sensor, count of connected),
`camera_connected` (binary sensor), `voltage` (sensor, mV, diagnostic) and `gps`
(device tracker with JSON attributes) (`src/FurbleMQTT.cpp:1966-2038`).

Camera components: `connect` (button, payload is the camera id), `connected`
(binary sensor) and `rssi` (sensor) (`src/FurbleMQTT.cpp:2056-2088`).

The component object is attached to the device object before serialisation
instead of being serialised and parsed again, because that second tree was the
largest transient allocation on the discovery path
(`src/FurbleMQTT.cpp:1917-1928`).

## Memory gates

| Gate | Plain | TLS | Citation |
| :--- | :--- | :--- | :--- |
| Owner task stack | 10240 bytes | 10240 bytes | `include/FurbleMQTTMemory.h:18` |
| Worker stack | 3072 bytes release | 6144 bytes | `include/FurbleMQTTMemory.h:27,40` |
| Network buffers | 1536 bytes | 1536 bytes | `include/FurbleMQTTMemory.h:50` |
| Event queue | 9392 bytes, in PSRAM | same | `include/FurbleMQTTMemory.h:62` |
| Event copy | 2348 bytes times 2 | same | `include/FurbleMQTTMemory.h:74-76` |
| Start gate | 8192 free, 4096 largest | 11264, 7168 | `include/FurbleMQTTMemory.h:109-112` |
| Owner gate | start floor plus the owner stack, 12288 largest | same shape | `include/FurbleMQTTMemory.h:123-126` |
| Discovery gate | 10240 free, 4096 largest | same | `include/FurbleMQTTMemory.h:136-137` |

A closed discovery gate logs and skips only the retained discovery records, so
the broker session and the command path stay alive
(`src/FurbleMQTT.cpp:1938-1950`).

## Error handling

- The event handler runs on the 3 KiB esp-mqtt worker and must never put a
  2348-byte `queued_event_t` on its stack. It fills the owner's preallocated copy
  instead (`include/FurbleMQTTMemory.h:74-76`, `CLAUDE.md` memory traps).
- An incoming event with a null topic, a null payload, a negative length, a topic
  longer than 256 bytes or an embedded NUL is dropped rather than parsed
  (`src/FurbleMQTT.cpp:996-1010,1101-1119`).
- A stopped client may still deliver a callback after `stop()` returns, so every
  event is compared against the current client handle before use
  (`src/FurbleMQTT.cpp:226-232`).
- Every command rejection publishes a human readable string on `state/error`,
  which Home Assistant shows as an entity state (`src/FurbleMQTT.cpp:1128-1270`).

## Open rows

| Missing | Why the repository cannot close it | Document and section needed |
| :--- | :--- | :--- |
| MQTT 3.1.1 or 5.0 conformance | The client is esp-mqtt and the code never states the protocol version it negotiates | OASIS MQTT 3.1.1 or 5.0 specification, conformance section, if a third-party broker must be certified |
| Home Assistant device discovery schema | The records follow the schema in code, not a pinned version | Home Assistant developer documentation, MQTT device discovery, `cmps` object schema |
| Broker-specific keepalive and clean-session rules | Set through esp-mqtt defaults | ESP-IDF MQTT documentation plus the broker's configuration guide |
