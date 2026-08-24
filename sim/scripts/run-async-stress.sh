#!/bin/sh

# Repeat the cross-task GPS scenarios so simulator scheduling regressions are
# exercised under CI load instead of depending on one favorable host handoff.

set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BIN=${FURBLE_SIM_BIN:-"$ROOT/sim/build/furble-sim"}
ROUNDS=${FURBLE_ASYNC_STRESS_ROUNDS:-5}

case "$ROUNDS" in
  ''|*[!0-9]*|0) echo "FURBLE_ASYNC_STRESS_ROUNDS must be a positive integer" >&2; exit 2 ;;
esac

: "${SDL_VIDEODRIVER:=dummy}"
: "${SDL_AUDIODRIVER:=dummy}"
export SDL_VIDEODRIVER SDL_AUDIODRIVER

if [ ! -x "$BIN" ]; then
  echo "simulator binary not found at $BIN" >&2
  exit 1
fi

round=1
while [ "$round" -le "$ROUNDS" ]; do
  for name in \
    gps-uart-ack \
    gps-uart-malformed \
    gps-uart-nack \
    gps-uart-partial \
    gps-uart-timeout \
    gps-uart-write-error; do
    "$BIN" --script "$ROOT/sim/scenarios/e2e/$name.txt"
  done
  echo "GPS async stress round $round/$ROUNDS passed"
  round=$((round + 1))
done
