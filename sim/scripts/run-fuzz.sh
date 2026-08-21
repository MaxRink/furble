#!/bin/sh

# Run the seeded UI fuzzer across a fixed set of seeds and fail if any seed
# reports a finding. The fuzzer is deterministic, so a green run here is a
# regression guard: the same seeds keep exercising the same event streams, and
# a new UI change that introduces a crash, a stale focus, a stacked modal, a
# narrow-panel overflow, a timer-state leak or a navigation trap flips the
# matching seed to a non-zero exit.
#
# Expected-fail seeds. When the fuzzer finds a real bug that is tracked but not
# yet fixed, pin its seed in FURBLE_FUZZ_XFAIL_SEEDS. Such a seed is required to
# FAIL and keeps CI green; if it starts passing (the bug was fixed) the runner
# fails loudly so the seed can be promoted back to the guarded set. This keeps
# the tooling PR green while the fix lands in a separate PR.
#
# Environment:
#   FURBLE_SIM_BIN         simulator binary (default sim/build/furble-sim)
#   FURBLE_FUZZ_SEEDS      space or newline separated seed list (default below)
#   FURBLE_FUZZ_XFAIL_SEEDS seeds required to fail (default empty)
#   FURBLE_FUZZ_STEPS      events per seed (default 600)

set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BIN=${FURBLE_SIM_BIN:-"$ROOT/sim/build/furble-sim"}
SEEDS=${FURBLE_FUZZ_SEEDS:-"1 2 3 7 42 99 1000 31337"}
XFAIL=${FURBLE_FUZZ_XFAIL_SEEDS:-}
STEPS=${FURBLE_FUZZ_STEPS:-600}

: "${SDL_VIDEODRIVER:=dummy}"
: "${SDL_AUDIODRIVER:=dummy}"
export SDL_VIDEODRIVER SDL_AUDIODRIVER

if [ ! -x "$BIN" ]; then
  echo "simulator binary not found at $BIN" >&2
  exit 1
fi

is_xfail() {
  for s in $XFAIL; do
    [ "$s" = "$1" ] && return 0
  done
  return 1
}

status=0
count=0
for seed in $SEEDS $XFAIL; do
  count=$((count + 1))
  echo "=== fuzz seed $seed ($STEPS steps) ==="
  if "$BIN" --seed "$seed" --fuzz-steps "$STEPS"; then
    rc=0
  else
    rc=$?
  fi
  if is_xfail "$seed"; then
    if [ "$rc" -ne 0 ]; then
      echo "XFAIL fuzz seed $seed (expected finding, exit $rc)"
    else
      echo "XPASS fuzz seed $seed (tracked bug no longer reproduces, promote it)"
      status=1
    fi
  else
    if [ "$rc" -eq 0 ]; then
      echo "PASS fuzz seed $seed"
    else
      echo "FAIL fuzz seed $seed (exit $rc)"
      status=1
    fi
  fi
done

if [ "$count" -eq 0 ]; then
  echo "no fuzz seeds configured" >&2
  exit 1
fi

if [ "$status" -eq 0 ]; then
  echo "All fuzz seeds behaved as expected ($count runs)."
fi
exit $status
