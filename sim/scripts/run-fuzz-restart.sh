#!/bin/sh

# Prove the fuzz walk survives a production UI restart: the process must
# resume its private checkpoint and finish the seed. Whether the random walk
# reaches a Restart button by itself depends on host scheduling (seed 2 did on
# macOS, no seed from 1 to 20 does on Linux), so step 200 of the first boot is
# replaced with the restart-apply action, which clicks the Sensors page
# "Restart to apply" button and runs Platform::restart(). The rest of the walk
# stays random and passes through run-fuzz.sh's summary checks.

set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
attempt=1
while [ "$attempt" -le 5 ]; do
  output=$(mktemp "${TMPDIR:-/tmp}/furble-fuzz-restart.XXXXXX")
  if FURBLE_FUZZ_SEEDS=2 FURBLE_FUZZ_STEPS=600 FURBLE_FUZZ_REPEAT_SEED= FURBLE_FUZZ_RESTART_STEP=200 \
      sh "$ROOT/sim/scripts/run-fuzz.sh" >"$output" 2>&1; then
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
