# 339 - MQTT retry backoff instead of a permanent block

## Motivation

`MAX_CONNECT_FAILURES` (10) turned a broker outage into a permanent silence.
The esp-mqtt component retried a refused or unreachable broker at its fixed
`reconnect_timeout_ms` (10,000 ms), and every failed attempt reached
`MQTT::handleLifecycleEvent()` as `MQTT_EVENT_DISCONNECTED`, which incremented
`m_ConnectFailures`. The tenth failure about 100 s into an outage set
`m_Blocked = true` and `m_StopClient = true`, and `taskStep()` then refused to
start a client until an operator ran `mqtt connect` (which only works from the
USB console) or the device rebooted. `MQTT::startClient()` returning false took
the same path.

For a hub whose purpose is Home Assistant control, that is the worst failure
mode available: a router reboot, a broker container restart or a maintenance
window longer than about 100 s silently ended the hub's MQTT service, and the
retained `offline` last will made the device look cleanly stopped rather than
stuck. Plan 96 recorded it as C5 (`MEDIUM, CONFIRMED`).

## Policy

- A retryable failure schedules the next start attempt instead of counting
  towards a limit. The gap starts at `RETRY_INITIAL_MS` (10 s, the component's
  previous fixed period, so a short blip recovers exactly as before) and
  doubles on every further failure up to `RETRY_MAX_MS` (60 s), then stays
  there and retries forever: 10, 20, 40, 60, 60, 60 s.
- The 60 s cap is the availability bound. The WiFi policy caps at 300 s
  (`RETRY_MAX_MS` in `FurbleWiFi.cpp`) because a station that keeps retrying
  costs radio time and a scan; MQTT retries are one TCP connect, and the cost
  of a long cap lands on the user: after a broker restart the hub would stay
  invisible, and a shutter command from Home Assistant would go unanswered, for
  up to five minutes. One attempt a minute is negligible broker traffic and
  negligible heap churn, and it rejoins a returning broker inside a minute.
- `m_Blocked` survives only for the two classes a retry cannot fix, so the
  operator still gets a definite stop with an actionable log line and
  `mqtt connect` still clears it through `reloadSetting()`.

## Classifier

`classifyFailure(error_type, connect_return_code)` in `src/FurbleMQTT.cpp`
mirrors `classifyDisconnect()` in `src/FurbleWiFi.cpp:150-160`: a small enum, a
switch over the component's own reason codes, and a retryable default.

| esp-mqtt class | Reason | Class | Why |
| --- | --- | --- | --- |
| `MQTT_ERROR_TYPE_CONNECTION_REFUSED` | `MQTT_CONNECTION_REFUSE_BAD_USERNAME` | `AUTH` | A confirmed credential failure; the operator must change the setting. Same role as `WIFI_REASON_AUTH_FAIL`. |
| `MQTT_ERROR_TYPE_CONNECTION_REFUSED` | `MQTT_CONNECTION_REFUSE_NOT_AUTHORIZED` | `AUTH` | The broker denies this identity. |
| `MQTT_ERROR_TYPE_CONNECTION_REFUSED` | `MQTT_CONNECTION_REFUSE_PROTOCOL` | `CONFIG` | The firmware pins MQTT 3.1.1, so a broker that rejects the protocol rejects every attempt. |
| `MQTT_ERROR_TYPE_CONNECTION_REFUSED` | `MQTT_CONNECTION_REFUSE_ID_REJECTED` | `CONFIG` | The client id is derived from the device identity; nothing on the device can retry it into acceptance. |
| `MQTT_ERROR_TYPE_CONNECTION_REFUSED` | `MQTT_CONNECTION_REFUSE_SERVER_UNAVAILABLE` | `TRANSIENT` | A broker restart, a maintenance window or an overloaded broker. |
| `MQTT_ERROR_TYPE_TCP_TRANSPORT` | any | `TRANSIENT` | DNS, socket, TLS or timeout: the broker, the router or the link is out. `classifyDisconnect()` reports the same shapes (handshake timeouts, no AP) as retryable, so a fixed trust store recovers without a reboot. |
| anything else, including no error handle | any | `TRANSIENT` | The default matches the WiFi classifier's `default:` arm. |

The component reports the classification in the same `esp_mqtt_error_codes_t`
it used for the error event, and leaves it readable on the disconnect that
follows, so the two fields are copied into the owner's lifecycle slot
(`m_LifecycleErrorType`, `m_LifecycleConnectReturnCode`) before the client
callback returns. They are read on the owner task only.

## Implementation

- `MQTT::scheduleRetry()` increments the failure count, doubles
  `m_RetryDelayMs` under `m_Mutex` up to the cap, and sets
  `m_NextAttemptMs`. `MQTT::retryDue()` gates the start block in `taskStep()`,
  which now also covers a refused admission gate or a refused
  `esp_mqtt_client_start()`: those are failed attempts too, so the backoff paces
  them instead of retrying every task period.
- `MQTT::clearRetry()` runs on `MQTT_EVENT_CONNECTED`, so a live session starts
  the policy over. `reloadSetting()` clears it with the block, so `mqtt connect`
  and any settings reload retry immediately.
- A retryable disconnect retires the client in the handler
  (`stopClient()`) before scheduling, so the effective cadence is the owner's
  and not the component's fixed period, and any further event from the retired
  client is rejected by `acceptsEvent()` against a null `m_Client`. That makes
  the accounting deterministic: one counted failure per attempt.
- Requirement: every admission gate and memory floor is unchanged. The gates
  are re-run for each rebuilt client, and the owner task, the event queue and
  the two event copies are exactly the objects that are *not* recreated, so a
  retry costs one esp-mqtt client and its worker.
- `config.network.reconnect_timeout_ms` stays 10,000 ms as a component-level
  backstop, with a comment saying the owner owns the cadence. It is not raised
  to the current backoff: it also bounds the reconnect poll that a stop has to
  wait out, so a 60 s value would let a teardown stall the owner for up to 30 s.

## Evidence

| Gate | Result |
| --- | --- |
| `mqtt-owner` host test (`mqtt_owner_test` in a Ninja host build) | PASS, including the progression 10,000 / 20,000 / 40,000 / 60,000 / 60,000 ms, no esp_mqtt_client_start() before a deadline, never blocked during the outage, five counted failures, and the `AUTH` / `CONFIG` / `SERVER_UNAVAILABLE` classifier cases plus `mqtt connect` clearing the block |
| `mqtt_fuzz --seed 3 --iterations 200` | `MQTT FUZZ SUMMARY seed=3 iterations=200 discovery_oom=skipped findings=0` |
| `sim/scenarios/e2e/memory-mqtt-unreachable.txt`, `FURBLE_SIM_MQTT=1`, closed port | PASS. Exact `mqtt.retry_delay` at each failure: 10000, 20000, 40000, 60000, 60000 ms with `mqtt.blocked no` and `heap.alloc_failures 0` throughout, `platform.watchdog armed`. The production log shows `MQTT attempt 1..5 failed, retrying in 10000/20000/40000/60000/60000 ms`. Three consecutive runs, all PASS |
| `sim/scripts/run-memory.sh` with the native transport | `All 4 MQTT memory scenarios passed` |
| Firmware header check: `xtensa-esp32s3-elf-g++ -fsyntax-only -DFURBLE_MQTT=1` on `src/FurbleMQTT.cpp` | exit 0, so `MQTT_ERROR_TYPE_CONNECTION_REFUSED`, the five `MQTT_CONNECTION_REFUSE_*` codes and `error_handle->connect_return_code` are the pinned ESP-IDF 5.5.3 names |
| `FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e m5stick-s3` | SUCCESS. The linked `firmware.elf` carries `MQTT attempt %lu failed, retrying in %lu ms.` and no copy of the removed `MQTT disconnected, attempt %lu.` string, so the image contains the new policy. pio reports flash 59.2% (1,861,577 of 3,145,728) and RAM 14.5% (47,488 of 327,680) |
| `tools/check_size_budget.py --env m5stick-s3`, `tools/stack_budget.py --env m5stick-s3` | PASS both. The MQTT owner task resolves to 3,408 of 10,240 bytes and the esp-mqtt client task to 2,240 of 3,072 |
| `python3 tools/pre_review.py --base origin/master` | PASS, 10 gates |

The heap shape changed where the old policy kept the client alive through an
outage. `memory-pressure-mqtt-shed.txt` was re-measured on the native transport:
with the client retired between attempts the owner alone leaves 24,265 bytes
free instead of 17,713, four 4 KiB squeezes cross the 8,192 RED floor (7,881),
the pause releases nothing because the client is already gone, and after
`action heap-release` the retry produces the second counted failure. Its
assertions now record those measured figures, and the settled level it asserts
is NORMAL: the retired client leaves 24,265 free, which is above the 20,480
AMBER exit, so AMBER is a level the ladder passes through rather than a level it
holds. The old policy held 17,713 free, below that exit, which is why the
pre-339 assertion could name AMBER and stay true.

## Implementation state

Done on `fix/mqtt-retry-backoff`. No hardware gate: a real broker restart, a
router reboot and an `mqtts://` credential rejection were not exercised on a
board. The classifier is covered by the host double only, which reports the
CONNACK codes the component reports.

## Deviations

- The plan number 339 was requested; 326-338 remain unassigned in
  `plans/README.md`.
- `MAX_CONNECT_FAILURES` is removed rather than kept at a larger value. A
  larger limit only moves the silence later; the requirement is that an outage
  never stops the service.
- `sim/scenarios/e2e/memory-mqtt-low-heap-retry-limit.txt` keeps its name. It
  never asserted a retry limit; it seeds below the pre-task owner floor, and
  `sim/coverage/crashes.json`, `sim/coverage/hardware_quirks.json` and the
  scenario manifest reference the path.
- The scenario advances a crossed backoff deadline with a following 1 s step.
  The simulator releases a task parked on a scheduler deadline on the clock
  advance after the one that crossed it, and the native transport reports the
  refused connection on wall-clock time, so the extra step keeps the wall-bound
  `assert-eventually` from racing the owner. `memory-mqtt-unreachable.txt`
  records this next to the waits.
- `memory-pressure-mqtt-shed.txt` settles the governor level with
  `assert-eventually-virtual` instead of a fixed wait followed by a plain
  assert. The governor takes one reading per second of virtual time, but the
  number of readings a `wait` yields is host scheduled, so a fixed wait samples
  whichever rung the ladder has reached: the first version asserted AMBER after
  `wait 12000`, passed on the development host and failed in `sim-e2e` with
  `expected 'amber' got 'normal'`. NORMAL is the attractor here, because 24,265
  clears both exits and a resident client at about 17,700 still clears the
  12,288 AMBER floor, so eventual-on-the-virtual-clock is deterministic. The
  measured figures (24,265 free, 7,881 at RED, the second counted failure) are
  unchanged.
