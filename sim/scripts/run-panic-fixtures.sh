#!/bin/sh

# Fail-closed allocation-abort gate. Every furble sdkconfig sets
# CONFIG_HEAP_ABORT_WHEN_ALLOCATION_FAILS, so a failed allocation aborts the
# device. Each fixture must exit with the simulator panic status 3, print the
# hardware-shaped abort line, and leave a panic crash record that the next boot
# reports, the way the RTC record survives a hardware reset.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BIN=${FURBLE_SIM_BIN:-"$ROOT/sim/build/furble-sim"}
MQTT_BIN=${FURBLE_SIM_MQTT_BIN:-"$ROOT/sim/build-mqtt/furble-sim"}
PANIC_EXIT=3
if command -v timeout >/dev/null 2>&1; then
  TIMEOUT_CMD=timeout
elif command -v gtimeout >/dev/null 2>&1; then
  TIMEOUT_CMD=gtimeout
else
  echo "GNU timeout is required to bound a simulator run." >&2
  exit 1
fi
export SDL_VIDEODRIVER=${SDL_VIDEODRIVER:-dummy}
export SDL_AUDIODRIVER=${SDL_AUDIODRIVER:-dummy}
export FURBLE_SIM_MQTT_URI=${FURBLE_SIM_MQTT_URI:-mqtt://127.0.0.1:1}
export FURBLE_SIM_MQTT_FAST_RECONNECT=${FURBLE_SIM_MQTT_FAST_RECONNECT:-1}

for binary in "$BIN" "$MQTT_BIN"; do
  if [ ! -x "$binary" ]; then
    echo "simulator binary not found at $binary" >&2
    echo "Build the default binary and the FURBLE_SIM_MQTT=1 binary first." >&2
    exit 1
  fi
done

WORK=$(mktemp -d "${TMPDIR:-/tmp}/furble-panic.XXXXXX")
trap 'rm -rf "$WORK"' EXIT
LOG="$WORK/result.log"
: >"$LOG"
status=0

# run_fixture BINARY FIXTURE ABORT_LINE TASK RECORD_TASK
# Each fixture must name its own allocation: its abort line and task are
# checked against that run alone, so another fixture cannot supply them.
# RECORD_TASK is TASK cut to the 15 characters a crash record keeps.
run_fixture() {
  binary=$1
  fixture=$2
  abort_line=$3
  task=$4
  record_task=$5
  prefs="$WORK/$(basename "$fixture" .txt).prefs"
  echo "=== expect panic: $fixture ===" | tee -a "$LOG"
  set +e
  FURBLE_SIM_PREFS=$prefs "$TIMEOUT_CMD" -k 10 120 "$binary" --script "$ROOT/$fixture" \
    >"$WORK/run.log" 2>&1
  rc=$?
  set -e
  cat "$WORK/run.log" >>"$LOG"
  if [ "$rc" -ne "$PANIC_EXIT" ]; then
    echo "FAIL: $fixture exited $rc, expected the panic status $PANIC_EXIT" >&2
    cat "$WORK/run.log" >&2
    status=1
    return
  fi
  if ! grep -F -- "abort() was called: $abort_line" "$WORK/run.log" >/dev/null 2>&1 \
    || ! grep -F -- "SIM PANIC: allocation failure in task $task aborts" "$WORK/run.log" \
      >/dev/null 2>&1; then
    echo "FAIL: $fixture aborted, but not on '$abort_line' in task $task" >&2
    cat "$WORK/run.log" >&2
    status=1
    return
  fi
  set +e
  FURBLE_SIM_PREFS=$prefs "$TIMEOUT_CMD" -k 10 120 "$binary" \
    --script "$ROOT/sim/fixtures/hardware/panic-record.txt" >"$WORK/record.log" 2>&1
  rc=$?
  set -e
  cat "$WORK/record.log" >>"$LOG"
  if [ "$rc" -ne 0 ] || ! grep -F -- "crash.task = $record_task" "$WORK/record.log" \
    >/dev/null 2>&1; then
    echo "FAIL: $fixture left no panic crash record for task $record_task" >&2
    cat "$WORK/record.log" >&2
    status=1
    return
  fi
  # The failed-allocation callback must have written the same size, caps and
  # task the abort line names into the record.
  alloc_size=$(($(echo "$abort_line" | sed 's/.*size \(0x[0-9a-fA-F]*\).*/\1/')))
  alloc_caps=$(echo "$abort_line" | sed 's/.*caps \(0x[0-9a-fA-F]*\).*/\1/')
  for expected in "crash.alloc_size = $alloc_size" "crash.alloc_caps = $alloc_caps" \
    "crash.alloc_task = $record_task"; do
    if ! grep -F -- "$expected" "$WORK/record.log" >/dev/null 2>&1; then
      echo "FAIL: $fixture record lacks '$expected'" >&2
      cat "$WORK/record.log" >&2
      status=1
      return
    fi
  done
  echo "PASS: $fixture aborted and its record survived the reboot"
}

run_fixture "$BIN" sim/fixtures/hardware/panic-webui-pre-gate-alloc.txt \
  "Mem alloc fail. size 0x2000 caps 0x00000804" webui_supervisor webui_superviso
run_fixture "$BIN" sim/fixtures/hardware/panic-icon-no-psram.txt \
  "Mem alloc fail. size 0x3003 caps 0x00001800" icon_decode icon_decode
run_fixture "$MQTT_BIN" sim/fixtures/hardware/panic-mqtt-task-alloc-fail.txt \
  "Mem alloc fail. size 0x2800 caps 0x00000804" mqtt mqtt

# Structured evidence markers consumed by tools/check_sim_coverage.py. Each
# token is checked against the captured run output. A crash record keeps 15
# task name characters, like a 16-byte FreeRTOS task name.
assert_token() {
  if ! grep -F -- "$1" "$LOG" >/dev/null 2>&1; then
    echo "FAIL: panic token missing: $1" >&2
    status=1
  fi
}

assert_token "abort() was called: Mem alloc fail. size 0x2000 caps 0x00000804"
assert_token "SIM PANIC: allocation failure in task webui_supervisor"
assert_token "abort() was called: Mem alloc fail. size 0x3003 caps 0x00001800"
assert_token "SIM PANIC: allocation failure in task icon_decode"
assert_token "SIM PANIC: allocation failure in task mqtt aborts"
assert_token "crash.task = webui_superviso"
assert_token "crash.task = icon_decode"
assert_token "crash.task = mqtt"

if [ "$status" -eq 0 ]; then
  echo "Allocation abort gate passed."
fi
exit "$status"
