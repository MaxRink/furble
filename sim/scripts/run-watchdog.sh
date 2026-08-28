#!/bin/sh

# Run the M5StickS3 watchdog contract scenarios against the freshly built
# simulator. Keep this separate from the broad UI matrix so a stale or
# differently configured binary cannot silently omit the PMIC boundary gate.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BIN=${FURBLE_SIM_BIN:-"$ROOT/sim/build/furble-sim"}

: "${SDL_VIDEODRIVER:=dummy}"
: "${SDL_AUDIODRIVER:=dummy}"
export SDL_VIDEODRIVER SDL_AUDIODRIVER

if [ ! -x "$BIN" ]; then
  echo "M5StickS3 simulator binary not found at $BIN" >&2
  exit 1
fi

for scenario in watchdog-feed watchdog-before watchdog-stall clock-wrap-before clock-wrap; do
  script="$ROOT/sim/scenarios/e2e/$scenario.txt"
  echo "=== M5StickS3 PM1 watchdog: $scenario ==="
  "$BIN" --script "$script"
done

echo "M5StickS3 PM1 watchdog boundary scenarios passed."
