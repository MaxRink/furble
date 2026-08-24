#!/bin/sh

# Malformed DSL fixtures must fail with the parser's validation exit status.

set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BIN=${FURBLE_SIM_BIN:-"$ROOT/sim/build/furble-sim"}
DIR=${1:-"$ROOT/sim/scenarios/invalid"}

: "${SDL_VIDEODRIVER:=dummy}"
: "${SDL_AUDIODRIVER:=dummy}"
export SDL_VIDEODRIVER SDL_AUDIODRIVER

if [ ! -x "$BIN" ]; then
  echo "simulator binary not found at $BIN" >&2
  exit 1
fi

count=0
for scenario in "$DIR"/*.txt; do
  [ -f "$scenario" ] || continue
  name=$(basename "$scenario" .txt)
  count=$((count + 1))
  echo "=== invalid: $name ==="
  if "$BIN" --script "$scenario"; then
    echo "FAIL $name unexpectedly succeeded" >&2
    exit 1
  else
    rc=$?
  fi
  if [ "$rc" -ne 2 ]; then
    echo "FAIL $name returned $rc, expected validation status 2" >&2
    exit 1
  fi
  echo "PASS $name rejected"
done

if [ "$count" -eq 0 ]; then
  echo "no invalid scenarios found in $DIR" >&2
  exit 1
fi

echo "All $count malformed scenarios were rejected."
