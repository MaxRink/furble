#!/bin/sh

# Run every scripted end-to-end scenario headlessly and fail if any scenario
# fails an assertion. Each scenario asserts app state and control-flow outcomes,
# so no display-backed pixel readback is needed. The SDL dummy video driver is
# enough.

set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BIN=${FURBLE_SIM_BIN:-"$ROOT/sim/build/furble-sim"}
BOARD=${FURBLE_SIM_BOARD_ID:-m5stick-s3}
# Wall-clock ceiling for one scenario. Every other bound in the simulator is
# denominated in virtual time, so none of them can see a stall that stops
# virtual time advancing at all. Without this a wedged boot spun at full CPU
# until the whole CI job timed out, with no output naming the scenario. -k
# follows up with SIGKILL because a wedged run ignores SIGTERM.
SCENARIO_TIMEOUT=${FURBLE_SIM_SCENARIO_TIMEOUT:-300}

: "${SDL_VIDEODRIVER:=dummy}"
: "${SDL_AUDIODRIVER:=dummy}"
export SDL_VIDEODRIVER SDL_AUDIODRIVER

# Report the optional IR LED, feedback output and SD card present so the
# capability-submenus scenario can reach the Infrared, Feedback and Storage
# submenus. These are sim-only flags and do not change on-device detection.
: "${FURBLE_SIM_IR:=1}"
: "${FURBLE_SIM_FEEDBACK:=1}"
: "${FURBLE_SIM_SD:=1}"
export FURBLE_SIM_IR FURBLE_SIM_FEEDBACK FURBLE_SIM_SD

if [ ! -x "$BIN" ]; then
  echo "simulator binary not found at $BIN" >&2
  exit 1
fi

status=0
count=0
scenarios=$(python3 "$ROOT/tools/check_sim_scenarios.py" --list-certified --suite e2e --board "$BOARD")
for scenario in $scenarios; do
  scenario="$ROOT/$scenario"
  name=$(basename "$scenario" .txt)
  count=$((count + 1))
  echo "=== $name ==="
  if timeout -k 10 "$SCENARIO_TIMEOUT" "$BIN" --script "$scenario"; then
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

if [ "$count" -eq 0 ]; then
  echo "no certified end-to-end scenarios owned for $BOARD" >&2
  exit 1
fi

if [ "$status" -eq 0 ]; then
  echo "All $count end-to-end scenarios passed."
fi
exit $status
