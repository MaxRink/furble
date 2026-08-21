# 100: console debug dump family

Status: implemented on `feat/console-debug`, off fork master `b71d895`. Debug
build only, behind `FURBLE_CONSOLE`. Release binaries are unchanged in behaviour:
every new command is compiled out of release, and the new log lines are all
`ESP_LOGD`, which release builds drop at compile time.

This is a follow-up to plan 64. Plan 64 gave the console its power, perf, and BT
tooling. This plan closes the state-observability gaps that were left: the
Control state machine internals, per-camera BLE detail, and the NimBLE client
pool, none of which a developer could see from the console before.

## Motivation

The console already exposes a lot: `status`, `cameras status` (conn params and
RSSI), `power stats` (pm lock table and owners), `perf tasks|heap`, `gps`
(status and config), and `settings list|get`. What it could not show was the
part of the firmware that is hardest to reason about from the outside: the
Control state machine. During a connect, reconnect, or disconnect the only
observable signal was the top-level `state`, and the internals that actually
drive the behaviour, the reconnect attempt counter and backoff flag, the
`m_ConnectAbort` and `m_ConnectInProgress` flags, the zombie drain set, and the
adaptive transmit power tracker, were all invisible.

The NimBLE client pool was equally opaque. The client leak that exhausted the
fixed-size pool after nine failed reconnects (plan 99) could only be inferred
from a reboot. There was no way to read the created-client count live and watch
it climb.

State transitions themselves were silent. A connect/disconnect trace had to be
reconstructed from the shutter and reconnect log lines around it, with the
`setState()` calls between them invisible.

## What this adds

### Console: `debug` command family

One `debug` command with subcommands, all `key: value` output like the rest of
the console:

- `debug control` dumps the full Control state machine snapshot: state, target
  count, connected count, zombie (draining teardown) count, the
  `connect_in_progress`, `connect_abort`, and `sleep_lock_held` flags, the
  `infinite_reconnect` and `reconnect_backoff` flags, the `reconnect_attempt`
  counter, the adaptive power tracker (`adaptive_active`, `power_level`,
  `adaptive_power_level`, `rssi_strong_samples`, `rssi_weak_samples`), and the
  name of the camera currently being connected. Fed by a new
  `Control::getDebugState()` that captures the internals under `m_Mutex`.
- `debug camera [idx]` dumps deep per-camera BLE detail for every live target,
  or one by index: name, BLE address, camera type, connected, active, connect
  progress, connection profile, and the cached connection parameters (interval,
  latency, timeout, RSSI). Uses the cached snapshot, so it never blocks on the
  HCI transport from the console task, matching `cameras status`.
- `debug ble` dumps the NimBLE stack state: initialised flag, own address,
  connection transmit power, the created-client count and the pool maximum
  (`CONFIG_BT_NIMBLE_MAX_CONNECTIONS`), and whether each control target has a
  live client in the NimBLE client list. The created-client count is the direct
  read of the plan 99 leak: a count that climbs across failed connects and never
  falls is the leak signature.
- `debug heap`, `debug tasks`, `debug power`, `debug gps`, `debug settings` are
  thin aliases onto the existing `perf heap`, `perf tasks`, `power stats`, `gps`
  status, and `settings list` dumps, so they read the same numbers.
- `debug all` runs status, control, cameras, ble, power, heap, gps, and settings
  back to back, so a bug report is one paste.

`status` also gains a `reset:` line reporting the last reset cause
(`esp_reset_reason()`), for boot-loop and crash diagnosis.

### Log points (all `ESP_LOGD`, release drops them)

- `Control::setState()` logs every state transition as `state <old> -> <new>`.
  This is the backbone of a connect/disconnect trace.
- `Control::connectAll()` logs the target count, how many need connecting, the
  reconnect attempt, and the timeout at the start of each connect pass.
- `Control::reapZombieTargets()` logs how many quarantined targets were reaped
  and how many are still draining.
- `Camera::connect()` logs `createClient` with the live pool count, and
  `deleteClient` with the new pool count on the failed-connect reclaim path.
  Together with `debug ble` these make the plan 99 client leak directly
  observable.

The pm-lock acquire and release already log owner and count (plan 64), so no new
power log point was needed.

## Design notes

- `Control::getDebugState()` reads `m_State` and the volatile abort/progress
  flags without `m_StateMutex`, mirroring `getState()`. A debug snapshot
  tolerates a benign torn read, and taking `m_StateMutex` here would risk a lock
  ordering hazard against `setState()`. The target, zombie, and adaptive fields
  are read under `m_Mutex`, the same lock `getTargets()` uses.
- The whole snapshot struct and accessor are behind `FURBLE_CONSOLE`, so the
  release Control object is byte identical.
- The new log lines are unconditional `ESP_LOGD`. Release builds set the compile
  time maximum log level to INFO, so the debug lines are dropped entirely and
  cost nothing. This is the same idiom `sampleAdaptivePower()` already uses.

## Settings

None. Every command is compile-time gated behind `FURBLE_CONSOLE`, the same
argument plans 27 and 64 made. No new NVS keys, menu switches, or companion
characteristics. Nothing here changes normal device operation.

## Gaps deliberately left

- No per-satellite C/N0 breakdown in `debug gps`. The `gps` status and `gps
  config` commands already cover fix, satellite count, sentence counters, and
  the PCAS configuration state machine. Per-satellite SNR is a larger GPS change
  and out of scope here.
- No companion peripheral state dump. The companion is a BLE peripheral with its
  own pairing flow; a `debug companion` command is a reasonable follow-up but
  was not needed to close the connect/disconnect and BLE observability gaps this
  plan targets.
- Per-camera transmit power is not printed. Transmit power is shared across all
  connections in this firmware (`Control::applyPower`), so it is reported once
  in `debug control` rather than per camera.

## Verification

- `m5stick-s3-debug` builds and links cleanly with all new commands and log
  points compiled in.
- All five release envs (`m5stick-c`, `m5stick-c-plus`, `m5stick-s3`,
  `m5stack-core`, `m5stack-core2`) build cleanly, confirming the new code
  compiles out of release and the `Camera::connect()` log lines do not disturb
  the release path.
- Host ctest stays green.
- clang-format 21 clean, no em-dashes.
- Hardware verification on the attached M5StickS3 is outstanding, per the fork
  process. The console dumps are read-only and drive no new device behaviour.

## Relationship to other plans

- Plan 27: created the console, the `key: value` contract, and the
  byte-identical release proof this plan reuses.
- Plan 64: gave the console its power, perf, and BT tooling. This plan is the
  state-observability follow-up.
- Plan 99: the NimBLE client leak. `debug ble` and the `createClient` /
  `deleteClient` log lines make that leak directly observable from the console.
