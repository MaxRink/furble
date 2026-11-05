#!/bin/sh

# Fail-closed hardware-quirk gate. The two fixtures must exit non-zero and name
# the violated quirk. The positive scenarios must pass with their assertions.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BIN=${FURBLE_SIM_BIN:-"$ROOT/sim/build/furble-sim"}
TIMEOUT_CMD=
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

if [ ! -x "$BIN" ]; then
  echo "simulator binary not found at $BIN" >&2
  exit 1
fi

status=0

for fixture in \
  sim/fixtures/hardware/fail-psram-stack-flash-write.txt \
  sim/fixtures/hardware/fail-mqtt-owner-stack.txt \
  sim/fixtures/hardware/fail-mqtt-buffer.txt; do
  echo "=== expect fail: $fixture ==="
  log=$(mktemp "${TMPDIR:-/tmp}/furble-quirk.XXXXXX")
  set +e
  "$TIMEOUT_CMD" -k 10 120 "$BIN" --script "$ROOT/$fixture" >"$log" 2>&1
  rc=$?
  set -e
  if [ "$rc" -eq 0 ]; then
    echo "FAIL: $fixture exited zero; the quirk was not enforced" >&2
    status=1
  elif ! grep -F "SIM FAIL" "$log" >/dev/null 2>&1; then
    echo "FAIL: $fixture did not print SIM FAIL" >&2
    cat "$log" >&2
    status=1
  else
    echo "PASS: $fixture failed closed"
  fi
  rm -f "$log"
done

for scenario in \
  sim/scenarios/e2e/hw-memory-post394-budget.txt \
  sim/scenarios/e2e/hw-memory-webui-post394.txt \
  sim/scenarios/e2e/hw-memory-webui-tg1wdt.txt \
  sim/scenarios/e2e/hw-memory-mqtt-ha-oom.txt \
  sim/scenarios/e2e/hw-usb-jtag-rom-stuck-recovery.txt \
  sim/scenarios/e2e/hw-power-button-state-machine.txt; do
  echo "=== $scenario ==="
  if ! "$TIMEOUT_CMD" -k 10 120 "$BIN" --script "$ROOT/$scenario"; then
    echo "FAIL: $scenario" >&2
    status=1
  fi
done

if [ "$status" -eq 0 ]; then
  echo "Hardware quirk gate passed."
fi
exit "$status"
