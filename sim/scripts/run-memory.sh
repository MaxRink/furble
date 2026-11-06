#!/bin/sh

# Run the MQTT memory-pressure scenarios against the optional native transport.
# No broker is required: the URI points at a closed port so every connection is
# refused and the production retry accounting runs. Build the binary first with
# FURBLE_SIM_MQTT=1; see docs/sim.md.

set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BIN=${FURBLE_SIM_BIN:-"$ROOT/sim/build-mqtt/furble-sim"}
SCENARIO_TIMEOUT=${FURBLE_SIM_SCENARIO_TIMEOUT:-300}

: "${SDL_VIDEODRIVER:=dummy}"
: "${SDL_AUDIODRIVER:=dummy}"
: "${FURBLE_SIM_MQTT_URI:=mqtt://127.0.0.1:1}"
: "${FURBLE_SIM_MQTT_FAST_RECONNECT:=1}"
export SDL_VIDEODRIVER SDL_AUDIODRIVER FURBLE_SIM_MQTT_URI FURBLE_SIM_MQTT_FAST_RECONNECT

if [ ! -x "$BIN" ]; then
  echo "MQTT simulator binary not found at $BIN" >&2
  echo "Build it with FURBLE_SIM_MQTT=1 and installed native dependencies." >&2
  exit 1
fi

if command -v timeout >/dev/null 2>&1; then
  TIMEOUT=timeout
elif command -v gtimeout >/dev/null 2>&1; then
  TIMEOUT=gtimeout
else
  echo "GNU timeout is required to bound a simulator run." >&2
  echo "On macOS: brew install coreutils, which provides gtimeout." >&2
  exit 1
fi

status=0
count=0
for name in \
  memory-mqtt-unreachable \
  memory-mqtt-low-heap-retry-limit \
  memory-boot-combination; do
  scenario="$ROOT/sim/scenarios/e2e/$name.txt"
  count=$((count + 1))
  echo "=== $name ==="
  if "$TIMEOUT" -k 10 "$SCENARIO_TIMEOUT" "$BIN" --script "$scenario"; then
    echo "PASS $name"
  else
    rc=$?
    if [ "$rc" -eq 124 ] || [ "$rc" -eq 137 ]; then
      echo "FAIL $name (timed out after ${SCENARIO_TIMEOUT}s)"
    else
      echo "FAIL $name (exit $rc)"
    fi
    status=1
  fi
done

if [ "$status" -eq 0 ]; then
  echo "All $count MQTT memory scenarios passed."
fi
exit $status
