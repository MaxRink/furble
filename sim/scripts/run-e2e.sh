#!/bin/sh

# Run every scripted end-to-end scenario headlessly and fail if any scenario
# fails an assertion. Each scenario asserts app state and control-flow outcomes,
# so no display-backed pixel readback is needed. The SDL dummy video driver is
# enough.

set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BIN=${FURBLE_SIM_BIN:-"$ROOT/sim/build/furble-sim"}
BOARD=${FURBLE_SIM_BOARD_ID:-m5stick-s3}

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
  if "$BIN" --script "$scenario"; then
    echo "PASS $name"
  else
    rc=$?
    echo "FAIL $name (exit $rc)"
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
