#!/bin/sh

# Run the M5StickS3 watchdog contract scenarios against the freshly built
# simulator. Keep this separate from the broad UI matrix so a stale or
# differently configured binary cannot silently omit the PMIC boundary gate.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BIN=${FURBLE_SIM_BIN:-"$ROOT/sim/build/furble-sim"}
# Same host wall-clock ceiling as run-e2e.sh, for the same reason: a wedged run
# stops virtual time, so only the host clock can bound it.
SCENARIO_TIMEOUT=${FURBLE_SIM_SCENARIO_TIMEOUT:-300}

: "${SDL_VIDEODRIVER:=dummy}"
: "${SDL_AUDIODRIVER:=dummy}"
export SDL_VIDEODRIVER SDL_AUDIODRIVER

if [ ! -x "$BIN" ]; then
  echo "M5StickS3 simulator binary not found at $BIN" >&2
  exit 1
fi

# GNU timeout, or the coreutils build Homebrew installs as gtimeout on macOS.
# This bound is the only thing standing between a wedged run and a hung CI job,
# so a missing tool is a hard failure rather than a silent run without it.
if command -v timeout >/dev/null 2>&1; then
  TIMEOUT=timeout
elif command -v gtimeout >/dev/null 2>&1; then
  TIMEOUT=gtimeout
else
  echo "GNU timeout is required to bound a simulator run." >&2
  echo "On macOS: brew install coreutils, which provides gtimeout." >&2
  exit 1
fi


for scenario in watchdog-feed watchdog-before watchdog-stall clock-wrap-before clock-wrap; do
  script="$ROOT/sim/scenarios/e2e/$scenario.txt"
  echo "=== M5StickS3 PM1 watchdog: $scenario ==="
  "$TIMEOUT" -k 10 "$SCENARIO_TIMEOUT" "$BIN" --script "$script"
done

echo "M5StickS3 PM1 watchdog boundary scenarios passed."
