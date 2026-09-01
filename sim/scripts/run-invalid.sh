#!/bin/sh

# Malformed DSL fixtures must fail with the parser's validation exit status.

set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BIN=${FURBLE_SIM_BIN:-"$ROOT/sim/build/furble-sim"}
BOARD=${FURBLE_SIM_BOARD_ID:-m5stick-s3}

: "${SDL_VIDEODRIVER:=dummy}"
: "${SDL_AUDIODRIVER:=dummy}"
export SDL_VIDEODRIVER SDL_AUDIODRIVER

if [ ! -x "$BIN" ]; then
  echo "simulator binary not found at $BIN" >&2
  exit 1
fi

count=0
scenarios=$(python3 "$ROOT/tools/check_sim_scenarios.py" --list-certified --suite invalid --board "$BOARD")
for scenario in $scenarios; do
  scenario="$ROOT/$scenario"
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
  echo "no certified invalid scenarios owned for $BOARD" >&2
  exit 1
fi

echo "All $count malformed scenarios were rejected."

expect_invalid() {
  rc=0
  "$@" || rc=$?
  if [ "$rc" -eq 0 ]; then
    echo "FAIL command unexpectedly succeeded: $*" >&2
    exit 1
  fi
  if [ "$rc" -ne 2 ]; then
    echo "FAIL command returned $rc, expected validation status 2: $*" >&2
    exit 1
  fi
}

expect_status() {
  want=$1
  shift
  rc=0
  "$@" || rc=$?
  if [ "$rc" -ne "$want" ]; then
    echo "FAIL command returned $rc, expected $want: $*" >&2
    exit 1
  fi
}

# Keep command-line validation in the same pre-runtime gate as script parsing.
expect_invalid "$BIN" --unknown-option
expect_invalid "$BIN" --script
expect_invalid "$BIN" --seed not-a-number
expect_invalid "$BIN" --script "$ROOT/sim/scripts/smoke.txt" --fuzz
expect_invalid env FURBLE_FUZZ_STEPS=not-a-number "$BIN" --fuzz
expect_invalid env FURBLE_FUZZ_STEPS=0 "$BIN" --fuzz

# The restart seam (plan 156). A continuation step outside the script's range
# is a pre-runtime rejection like any other bad input, and a failure raised
# after the `restart` step must survive the re-exec as the run's own status
# instead of being masked by the reboot.
expect_invalid env FURBLE_SIM_RESTART_STEP=99999 "$BIN" --script "$ROOT/sim/scripts/smoke.txt"
expect_status 1 "$BIN" --script "$ROOT/sim/scripts/restart-post-failure.txt"
echo "CLI and environment validation passed."
