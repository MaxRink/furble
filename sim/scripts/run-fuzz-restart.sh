#!/bin/sh

# Seed 2 reaches a production UI Restart button on the 135x240 simulator, but
# the exact visit depends on the remaining host scheduling gaps. Repeat the
# real randomized walk until one run proves the process resumed its private
# checkpoint. Every attempt still passes through run-fuzz.sh's summary checks.

set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
attempt=1
while [ "$attempt" -le 5 ]; do
  output=$(mktemp "${TMPDIR:-/tmp}/furble-fuzz-restart.XXXXXX")
  if FURBLE_FUZZ_SEEDS=2 FURBLE_FUZZ_STEPS=600 FURBLE_FUZZ_REPEAT_SEED= \
      "$ROOT/sim/scripts/run-fuzz.sh" >"$output" 2>&1; then
    cat "$output"
    if grep -q '^FUZZ RESUME seed=2 ' "$output"; then
      rm -f "$output"
      echo "PASS fuzz resumed after a production UI restart"
      exit 0
    fi
  else
    cat "$output"
    rm -f "$output"
    exit 1
  fi
  rm -f "$output"
  attempt=$((attempt + 1))
done

echo "seed 2 did not reach a production UI restart in 5 runs" >&2
exit 1
