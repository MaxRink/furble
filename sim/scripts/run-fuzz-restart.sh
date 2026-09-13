#!/bin/sh

# Exercise the real Platform::restart path discovered in the seeded fuzzer.
# Seed 2 reaches CalibrationUI::calibrate, which requests a process restart;
# the resumed process must still report one complete 600-event run.

set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BIN=${FURBLE_SIM_BIN:-"$ROOT/sim/build/furble-sim"}
STEPS=${FURBLE_FUZZ_STEPS:-600}

if [ ! -x "$BIN" ]; then
  echo "simulator binary not found at $BIN" >&2
  exit 1
fi
if [ "$STEPS" -ne 600 ]; then
  echo "FURBLE_FUZZ_STEPS must remain 600 for the restart regression" >&2
  exit 1
fi
if command -v timeout >/dev/null 2>&1; then
  TIMEOUT=timeout
elif command -v gtimeout >/dev/null 2>&1; then
  TIMEOUT=gtimeout
else
  echo "GNU timeout is required" >&2
  exit 1
fi

WORK=$(mktemp -d "${TMPDIR:-/tmp}/furble-fuzz-restart.XXXXXX")
trap 'rm -rf "$WORK"' EXIT INT TERM

run_valid_restart() {
  output="$WORK/valid.log"
  if ! (cd "$WORK" && FURBLE_SIM_NO_TOUCH=0 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
      "$TIMEOUT" -k 10 "${FURBLE_FUZZ_SEED_TIMEOUT:-600}" "$BIN" \
      --seed 2 --fuzz-steps 600 >"$output" 2>&1); then
    cat "$output"
    echo "real restart fuzz run failed" >&2
    return 1
  fi
  starts=$(grep -c '^FUZZ START seed=2 steps=600$' "$output" || true)
  summaries=$(grep -c '^FUZZ SUMMARY ' "$output" || true)
  summary=$(grep '^FUZZ SUMMARY ' "$output" || true)
  [ "$starts" -ge 2 ] || { cat "$output"; echo "real restart did not produce two boots" >&2; return 1; }
  [ "$summaries" -eq 1 ] || { cat "$output"; return 1; }
  expected='FUZZ SUMMARY seed=2 steps=600 attempted=600 '
  case "$summary" in
    "$expected"*) : ;;
    *) cat "$output"; echo "restart summary has wrong leading fields" >&2; return 1;;
  esac
  for token in 'observed_delta=' 'no_observed_delta=' 'settled=600'; do
    case "$summary" in *" $token"*) : ;; *) cat "$output"; return 1 ;; esac
  done
  [ -d "$WORK/.pio" ] || { echo "restart run did not create .pio" >&2; return 1; }
  leftovers=$(find "$WORK/.pio" -type f -name 'furble-sim-fuzz-checkpoint-*' -print -quit) || return 1
  if [ -n "$leftovers" ]; then
    echo "generated fuzz checkpoint was not cleaned up" >&2
    return 1
  fi
}

run_rejected_checkpoint() {
  path="$WORK/foreign-checkpoint"
  printf '%s\n' 'not a checkpoint' >"$path"
  cp "$path" "$path.original"
  if (cd "$WORK" && FURBLE_SIM_FUZZ_CHECKPOINT="$path" \
      FURBLE_SIM_FUZZ_CHECKPOINT_OWNER="$path|foreign-pid" \
      "$TIMEOUT" -k 10 "${FURBLE_SIM_SEED_TIMEOUT:-60}" "$BIN" --seed 2 --fuzz-steps 600 \
      >"$WORK/rejected.log" 2>&1); then
    cat "$WORK/rejected.log"
    echo "foreign checkpoint unexpectedly accepted" >&2
    return 1
  fi
  rc=$?
  [ "$rc" -eq 2 ] || { cat "$WORK/rejected.log"; echo "foreign checkpoint status was $rc" >&2; return 1; }
  grep -Fxq "Invalid fuzz checkpoint ownership marker" "$WORK/rejected.log" || return 1
  cmp -s "$path.original" "$path" || { echo "foreign checkpoint changed" >&2; return 1; }

  malformed="$WORK/malformed-checkpoint"
  printf '%s\n' 'not a checkpoint' >"$malformed"
  cp "$malformed" "$malformed.original"
  if (cd "$WORK" && "$TIMEOUT" -k 10 "${FURBLE_SIM_SEED_TIMEOUT:-60}" sh -c \
      'export FURBLE_SIM_FUZZ_CHECKPOINT="$1" FURBLE_SIM_FUZZ_CHECKPOINT_OWNER="$1|$$"; exec "$2" --seed 2 --fuzz-steps 600' \
      restart-regression "$malformed" "$BIN" >"$WORK/malformed.log" 2>&1; then
    cat "$WORK/malformed.log"
    echo "malformed checkpoint unexpectedly accepted" >&2
    return 1
  fi
  rc=$?
  [ "$rc" -eq 2 ] || { cat "$WORK/malformed.log"; echo "malformed checkpoint status was $rc" >&2; return 1; }
  grep -Fxq "Invalid fuzz checkpoint contents: $malformed" "$WORK/malformed.log" || return 1
  cmp -s "$malformed.original" "$malformed" || { echo "malformed checkpoint changed" >&2; return 1; }
}

run_valid_restart
run_rejected_checkpoint
echo "fuzz restart regression passed"
