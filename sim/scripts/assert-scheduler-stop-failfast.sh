#!/bin/sh

# The simulator's SchedulerStopped escape must fail fast without entering
# cleanup or returning a success that could trigger a restart.

set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BIN=${FURBLE_SIM_BIN:-"$ROOT/sim/build/furble-sim"}

: "${SDL_VIDEODRIVER:=dummy}"
: "${SDL_AUDIODRIVER:=dummy}"
export SDL_VIDEODRIVER SDL_AUDIODRIVER

if [ ! -x "$BIN" ]; then
  echo "simulator binary not found at $BIN" >&2
  exit 1
fi

# GNU timeout, or the coreutils build Homebrew installs as gtimeout on macOS.
if command -v timeout >/dev/null 2>&1; then
  TIMEOUT=timeout
elif command -v gtimeout >/dev/null 2>&1; then
  TIMEOUT=gtimeout
else
  echo "GNU timeout is required to bound the simulator subprocess." >&2
  exit 1
fi

output=$(mktemp "${TMPDIR:-/tmp}/furble-scheduler-stop.XXXXXX")
trap 'rm -f "$output"' EXIT

status=0
FURBLE_SIM_TEST_SCHEDULER_STOP=1 FURBLE_SIM_WATCHDOG_SECONDS=0 \
  "$TIMEOUT" -k 2 10 "$BIN" --script "$ROOT/sim/scripts/smoke.txt" >"$output" 2>&1 || status=$?

if [ "$status" -eq 124 ] || [ "$status" -eq 137 ]; then
  echo "scheduler-stop fail-fast subprocess timed out" >&2
  cat "$output" >&2
  exit 1
fi
if [ "$status" -ge 128 ]; then
  echo "scheduler-stop fail-fast subprocess died from signal $((status - 128))" >&2
  cat "$output" >&2
  exit 1
fi
if [ "$status" -ne 1 ]; then
  echo "scheduler-stop fail-fast returned $status, expected 1" >&2
  cat "$output" >&2
  exit 1
fi
if ! grep -F "SIM FAIL: SchedulerStopped in UI task; exiting without cleanup" "$output" \
    >/dev/null 2>&1; then
  echo "scheduler-stop fail-fast diagnostic was missing" >&2
  cat "$output" >&2
  exit 1
fi

echo "SchedulerStopped UI fail-fast returned status 1 with its bounded diagnostic."
