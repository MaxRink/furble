# Crash diagnostics

furble keeps one small record across a reset so a boot loop or a panic leaves
evidence on the next boot. The record is written in normal task context, not in
a panic handler, and it never touches flash.

## What is recorded

| Field | Meaning |
| :--- | :--- |
| reset reason | The reason the previous run ended, read with `esp_reset_reason()` on the next boot. |
| task | The task that emitted the newest captured log line. On a quiet crash this is the best available context, not a guaranteed panic task. |
| uptime | Seconds since the previous run's boot. |
| heap free / largest | Internal 8-bit heap free and largest block at the last snapshot. |
| last log lines | Up to four recent log lines, truncated to 96 bytes each. |
| clean flag | Set by the shutdown handler. A clean record is never reported as a crash. |

The snapshot is refreshed about once per second from the UI task. A panic can
therefore lose up to one second of state. That is deliberate: the record is a
plain RTC-memory write with no allocator, no flash and no lock that a crashed
task could hold.

## Reading it

At boot the firmware prints one block when the previous run ended unclean:

```text
[E] crash: CRASH RECORD: reset=panic task=mqtt uptime=312s heap_free=13543 heap_largest=12788 clean=no
[E] crash: CRASH LOG: MQTT client start failed, attempt 10.
```

The record is consumed after it is printed, so a reboot loop does not repeat an
old record forever. A debug build can print it again from the console:

```text
crash last
```

`crash last` prints `crash.summary`, `crash.reset`, `crash.task`,
`crash.uptime`, `crash.heap_free`, `crash.heap_largest` and
`crash.last_log`, then reprints the captured log tail. `status` continues to
report the current boot's reset reason and heap.

## What it does not claim

- The task and log tail describe the last normal snapshot, not the instruction
  that faulted. A full backtrace still comes from the ROM panic output and, when
  enabled, a core dump.
- The record is not a persistence format. It is invalidated by the magic,
  version, size and CRC check, so stale RTC contents after a power-on are
  ignored.
- A brownout or an abrupt power loss may leave no record. A power-on reset
  starts from an empty pool by definition.

## Simulator

The simulator keeps the same record shape and prints the same
`CRASH RECORD:` block. Its implementation stores the record in a sidecar file
next to the run's preferences file, so the real `restart` path survives it. The
`crash-inject-panic` and `crash-inject-task-wdt` actions write an unclean record
for the next boot, and the `crash.*` queries assert the printed values.

Focused scenario:

```sh
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  sim/build/furble-sim --script sim/scenarios/e2e/crash-record-survives-restart.txt
```

Host encoding coverage is `tests/host/crash_record_test.cpp`. The retained
record, log capture, one-shot recovery and shutdown-clean path are covered by
`tests/host/crash_log_test.cpp`.
