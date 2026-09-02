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
#   FURBLE_FUZZ_SEED_TIMEOUT  wall-clock seconds per seed (default 600)
#   FURBLE_FUZZ_REPEAT_SEED   seed replayed for the determinism check
#                             (default 2, empty to skip)

set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BIN=${FURBLE_SIM_BIN:-"$ROOT/sim/build/furble-sim"}
SEEDS=${FURBLE_FUZZ_SEEDS:-"1 2 3 7 42 99 1000 31337"}
XFAIL=${FURBLE_FUZZ_XFAIL_SEEDS:-}
STEPS=${FURBLE_FUZZ_STEPS:-600}
# Wall-clock ceiling for one seed. Generous against a loaded runner, tight
# enough that a hung seed fails the job instead of burning its whole budget.
SEED_TIMEOUT=${FURBLE_FUZZ_SEED_TIMEOUT:-600}
# One guarded seed is replayed and its two FUZZ report lines are required to
# match: the same seed must drive the same event stream and reach the same
# pages, on the same binary, every time. A change that lets host timing pick
# the fuzzer's path fails here instead of surfacing later as a finding nobody
# can reproduce.
#
# The comparison is deliberately scoped to those lines rather than to the whole
# output, and the reason is a measured limit, not convenience. Firmware
# behaviour under the fuzzer is not yet reproducible line for line: two runs of
# the same seed can still differ by one connect attempt, because production
# code blocks on plain host mutexes the simulator scheduler cannot see, so how
# far a connect gets before a disconnect lands is still host timed. That is the
# scheduler-visible mutex gap plan 158 Phase 3 owns. Asserting byte equality of
# the whole log today would be a flaky gate, which is worse than none.
#
# Within the summary line the two observation counters are masked for the same
# reason: they record whether a visible change had landed by the end of a
# settle window, and that boundary moves by one step for the same cause.
# Seed 2 by default, and the choice is measured rather than arbitrary. Seed 2 is
# the seed whose whole output reproduces: two runs on the 320x240 binary match
# on all 215 log lines apart from the two masked counters. Seed 1 is the seed
# that does not, differing by one connect attempt between runs, so defaulting to
# it would replay the least reproducible seed available. Both seeds satisfy this
# check today, which compares only the fuzz report lines, but the default should
# be the seed with headroom, so that tightening the comparison later does not
# start from the known-bad case.
REPEAT_SEED=${FURBLE_FUZZ_REPEAT_SEED-2}

: "${SDL_VIDEODRIVER:=dummy}"
: "${SDL_AUDIODRIVER:=dummy}"
export SDL_VIDEODRIVER SDL_AUDIODRIVER

if [ ! -x "$BIN" ]; then
  echo "simulator binary not found at $BIN" >&2
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


is_xfail() {
  for s in $XFAIL; do
    [ "$s" = "$1" ] && return 0
  done
  return 1
}

validate_summary() {
  output=$1
  requested_seed=$2
  requested_steps=$3
  summary_count=$(grep -c '^FUZZ SUMMARY ' "$output" || true)
  if [ "$summary_count" -ne 1 ]; then
    echo "fuzz seed $requested_seed produced $summary_count FUZZ SUMMARY lines" >&2
    return 1
  fi

  summary=$(grep '^FUZZ SUMMARY ' "$output")
  summary_seed=$(printf '%s\n' "$summary" | sed -n 's/.* seed=\([^ ]*\).*/\1/p')
  summary_steps=$(printf '%s\n' "$summary" | sed -n 's/.* steps=\([^ ]*\).*/\1/p')
  attempted=$(printf '%s\n' "$summary" | sed -n 's/.* attempted=\([^ ]*\).*/\1/p')
  observed=$(printf '%s\n' "$summary" | sed -n 's/.* observed_delta=\([^ ]*\).*/\1/p')
  no_observed=$(printf '%s\n' "$summary" | sed -n 's/.* no_observed_delta=\([^ ]*\).*/\1/p')
  settled=$(printf '%s\n' "$summary" | sed -n 's/.* settled=\([^ ]*\).*/\1/p')
  if [ "$summary_seed" != "$requested_seed" ] || [ "$summary_steps" != "$requested_steps" ]; then
    echo "fuzz summary request mismatch for seed $requested_seed: $summary" >&2
    return 1
  fi
  if [ "$attempted" != "$requested_steps" ] || [ "$settled" != "$requested_steps" ]; then
    echo "fuzz summary count mismatch for seed $requested_seed: $summary" >&2
    return 1
  fi
  for counter in "$attempted" "$observed" "$no_observed" "$settled"; do
    case "$counter" in
      ''|*[!0-9]*)
        echo "fuzz summary counters are not unsigned integers for seed" \
          "$requested_seed: $summary" >&2
        return 1
        ;;
    esac
  done
  delta_sum=$((observed + no_observed))
  if [ "$delta_sum" -ne "$attempted" ]; then
    echo "fuzz summary delta mismatch for seed $requested_seed: $summary" >&2
    return 1
  fi
  return 0
}

status=0
count=0
for seed in $SEEDS $XFAIL; do
  count=$((count + 1))
  echo "=== fuzz seed $seed ($STEPS steps) ==="
  # Keep both explicit arguments in the wrapper contract: CLI seed/steps win
  # over any FURBLE_FUZZ_SEED/FURBLE_FUZZ_STEPS fallback inherited by the run.
  output_file=$(mktemp "${TMPDIR:-/tmp}/furble-fuzz.XXXXXX") || exit 1
  # Hard per-seed bound. A simulator scheduler deadlock leaves the process
  # spinning on a condition variable that no signal can reach, and it ignores
  # SIGTERM, so CI would hang for the whole job timeout with no output. -k
  # follows up with SIGKILL. A timed out seed is a failure, not a pass.
  if "$TIMEOUT" -k 10 "$SEED_TIMEOUT" "$BIN" --seed "$seed" --fuzz-steps "$STEPS" \
      >"$output_file" 2>&1; then
    rc=0
  else
    rc=$?
    if [ "$rc" -eq 124 ] || [ "$rc" -eq 137 ]; then
      echo "fuzz seed $seed exceeded ${SEED_TIMEOUT}s and was killed" >&2
    fi
  fi
  cat "$output_file"
  if [ "$rc" -eq 124 ] || [ "$rc" -eq 137 ]; then
    # A killed seed produced no summary. Report the timeout, not a summary
    # parse error, and never let it look like an expected finding.
    echo "FAIL fuzz seed $seed (timed out after ${SEED_TIMEOUT}s)"
    rm -f "$output_file"
    status=1
    continue
  fi
  if ! validate_summary "$output_file" "$seed" "$STEPS"; then
    status=1
  fi
  rm -f "$output_file"
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

# Determinism replay. Runs after the guarded seeds so a plain finding is
# reported first and this never masks one.
if [ -n "$REPEAT_SEED" ]; then
  echo "=== determinism replay seed $REPEAT_SEED ($STEPS steps) ==="
  first=$(mktemp "${TMPDIR:-/tmp}/furble-fuzz-a.XXXXXX") || exit 1
  second=$(mktemp "${TMPDIR:-/tmp}/furble-fuzz-b.XXXXXX") || exit 1
  for output in "$first" "$second"; do
    "$TIMEOUT" -k 10 "$SEED_TIMEOUT" "$BIN" --seed "$REPEAT_SEED" \
      --fuzz-steps "$STEPS" >"$output" 2>&1
    rc=$?
    if [ "$rc" -ne 0 ]; then
      echo "determinism replay seed $REPEAT_SEED exited $rc" >&2
      status=1
    fi
    grep '^FUZZ ' "$output" \
      | sed -e 's/observed_delta=[0-9]*/observed_delta=X/g' \
        -e 's/no_observed_delta=[0-9]*/no_observed_delta=X/g' >"$output.fuzz"
  done
  if diff -u "$first.fuzz" "$second.fuzz" >/dev/null 2>&1; then
    echo "PASS determinism replay seed $REPEAT_SEED"
  else
    echo "FAIL determinism replay seed $REPEAT_SEED (fuzz report lines diverged)"
    diff -u "$first.fuzz" "$second.fuzz" | head -40
    status=1
  fi
  rm -f "$first" "$second" "$first.fuzz" "$second.fuzz"
fi

if [ "$status" -eq 0 ]; then
  echo "All fuzz seeds behaved as expected ($count runs)."
fi
exit $status
