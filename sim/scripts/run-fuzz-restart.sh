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
  if ! (cd "$WORK" && SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
      "$TIMEOUT" -k 10 "${FURBLE_FUZZ_SEED_TIMEOUT:-600}" "$BIN" \
      --seed 2 --fuzz-steps 600 >"$output" 2>&1); then
    cat "$output"
    echo "real restart fuzz run failed" >&2
    return 1
  fi
  summaries=$(grep -c '^FUZZ SUMMARY ' "$output" || true)
  summary=$(grep '^FUZZ SUMMARY ' "$output" || true)
  [ "$summaries" -eq 1 ] || { cat "$output"; return 1; }
  case "$summary" in
    *"seed=2"*"steps=600"*"attempted=600"*"settled=600"*) : ;;
    *) cat "$output"; echo "restart summary is incomplete" >&2; return 1;;
  esac
  if find "$WORK/.pio" -type f -name 'furble-sim-fuzz-checkpoint-*' -print -quit | grep -q .; then
    echo "generated fuzz checkpoint was not cleaned up" >&2
    return 1
  fi
}

run_rejected_checkpoint() {
  path="$WORK/foreign-checkpoint"
  printf '%s\n' 'not a checkpoint' >"$path"
  if (cd "$WORK" && FURBLE_SIM_FUZZ_CHECKPOINT="$path" \
      FURBLE_SIM_FUZZ_CHECKPOINT_OWNER="$path|foreign-pid" \
      "$BIN" --seed 2 --fuzz-steps 600 >"$WORK/rejected.log" 2>&1); then
    cat "$WORK/rejected.log"
    echo "foreign checkpoint unexpectedly accepted" >&2
    return 1
  fi
  [ -f "$path" ] || { echo "foreign checkpoint was removed" >&2; return 1; }
}

run_valid_restart
run_rejected_checkpoint
echo "fuzz restart regression passed"
