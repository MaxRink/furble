#!/bin/sh

# Check the simulator-owned preference lifecycle: explicit stores survive,
# same-PID restart adoption works, orderly runs clean up their own store, and a
# later run reclaims only the exact generated names whose encoded PID is dead.
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

TMP_DIR=$(mktemp -d "${TMPDIR:-/tmp}/furble-preferences-check.XXXXXX")
trap 'rm -rf "$TMP_DIR"' EXIT

EXPLICIT="$TMP_DIR/explicit.bin"
dd if=/dev/zero of="$EXPLICIT" bs=4 count=1 2>/dev/null
EXPLICIT_WRITE_SCRIPT="$TMP_DIR/explicit-write.txt"
printf '%s\n' \
  'seed reconnect false' \
  'wait 300' \
  'assert setting.reconnect 0' \
  'action page settings' \
  'wait 200' \
  'action page features' \
  'wait 200' \
  'action toggle reconnect' \
  'wait 200' \
  'assert setting.reconnect 1' \
  'exit' >"$EXPLICIT_WRITE_SCRIPT"
EXPLICIT_READ_SCRIPT="$TMP_DIR/explicit-read.txt"
printf '%s\n' 'wait 300' 'assert setting.reconnect 1' 'exit' >"$EXPLICIT_READ_SCRIPT"
(
  cd "$TMP_DIR"
  env -u FURBLE_SIM_RESTART_STEP -u FURBLE_SIM_CRASH_STEP \
    -u FURBLE_SIM_PREFS_GENERATED FURBLE_SIM_PREFS="$EXPLICIT" "$BIN" \
    --script "$EXPLICIT_WRITE_SCRIPT" \
    --capture-dir "$TMP_DIR/explicit-write-captures"
)
(
  cd "$TMP_DIR"
  env -u FURBLE_SIM_RESTART_STEP -u FURBLE_SIM_CRASH_STEP \
    -u FURBLE_SIM_PREFS_GENERATED FURBLE_SIM_PREFS="$EXPLICIT" "$BIN" \
    --script "$EXPLICIT_READ_SCRIPT" \
    --capture-dir "$TMP_DIR/explicit-read-captures"
)
if [ ! -f "$EXPLICIT" ]; then
  echo "explicit preference store was removed or ignored" >&2
  exit 1
fi

STALE_EXPLICIT="$TMP_DIR/stale-explicit.bin"
dd if=/dev/zero of="$STALE_EXPLICIT" bs=4 count=1 2>/dev/null
STALE_SCRIPT="$TMP_DIR/stale-marker.txt"
printf '%s\n' 'wait 1' 'exit' >"$STALE_SCRIPT"
(
  cd "$TMP_DIR"
  env -u FURBLE_SIM_CRASH_STEP FURBLE_SIM_PREFS="$STALE_EXPLICIT" \
    FURBLE_SIM_PREFS_GENERATED="$STALE_EXPLICIT|0" FURBLE_SIM_RESTART_STEP=1 "$BIN" \
    --script "$STALE_SCRIPT" \
    --capture-dir "$TMP_DIR/stale-captures"
)
if [ ! -f "$STALE_EXPLICIT" ]; then
  echo "stale generated marker caused explicit store cleanup" >&2
  exit 1
fi

CRASH_SCRIPT="$TMP_DIR/crash.txt"
printf '%s\n' 'wait 1' >"$CRASH_SCRIPT"
run_expected_crash() {
  crash_dir=$1
  mkdir -p "$crash_dir"
  status=0
  (
    cd "$crash_dir"
    env -u FURBLE_SIM_PREFS -u FURBLE_SIM_PREFS_GENERATED -u FURBLE_SIM_RESTART_STEP \
      FURBLE_SIM_CRASH_STEP=0 "$BIN" --script "$CRASH_SCRIPT" \
      --capture-dir "$crash_dir/captures"
  ) || status=$?
  if [ "$status" -eq 0 ]; then
    echo "crash fixture unexpectedly succeeded" >&2
    exit 1
  fi
}
CRASH_A_DIR="$TMP_DIR/crash-a"
CRASH_B_DIR="$TMP_DIR/crash-b"
run_expected_crash "$CRASH_A_DIR"
run_expected_crash "$CRASH_B_DIR"
FILE_A=$(find "$CRASH_A_DIR/.pio" -maxdepth 1 -type f -name 'furble-sim-preferences-*.bin' \
  -print -quit)
FILE_B=$(find "$CRASH_B_DIR/.pio" -maxdepth 1 -type f -name 'furble-sim-preferences-*.bin' \
  -print -quit)
if [ -z "$FILE_A" ] || [ -z "$FILE_B" ] || \
    [ "$(basename "$FILE_A")" = "$(basename "$FILE_B")" ]; then
  echo "generated preference names were not unique" >&2
  exit 1
fi

RESTART_DIR="$TMP_DIR/restart-run"
mkdir "$RESTART_DIR"
(
  cd "$RESTART_DIR"
  env -u FURBLE_SIM_PREFS -u FURBLE_SIM_PREFS_GENERATED -u FURBLE_SIM_RESTART_STEP \
    -u FURBLE_SIM_CRASH_STEP "$BIN" \
    --script "$ROOT/sim/scenarios/e2e/restart-persist.txt" \
    --capture-dir "$RESTART_DIR/captures"
)
if [ ! -f "$FILE_A" ] || [ ! -f "$FILE_B" ]; then
  echo "a run in another directory swept unrelated crash leftovers" >&2
  exit 1
fi
if find "$RESTART_DIR/.pio" -maxdepth 1 -type f -name 'furble-sim-preferences-*' \
    -print -quit 2>/dev/null | grep -q .; then
  echo "owned generated preference store survived orderly cleanup" >&2
  exit 1
fi

# Put the two dead-PID stores into one run directory. A normal run there must
# reclaim both while leaving the caller-owned store alone.
mkdir -p "$TMP_DIR/.pio"
mv "$FILE_A" "$TMP_DIR/.pio/"
mv "$FILE_B" "$TMP_DIR/.pio/"
SWEEP_SCRIPT="$TMP_DIR/sweep.txt"
printf '%s\n' 'wait 1' 'exit' >"$SWEEP_SCRIPT"
(
  cd "$TMP_DIR"
  env -u FURBLE_SIM_PREFS -u FURBLE_SIM_PREFS_GENERATED -u FURBLE_SIM_RESTART_STEP \
    -u FURBLE_SIM_CRASH_STEP "$BIN" --script "$SWEEP_SCRIPT" \
    --capture-dir "$TMP_DIR/sweep-captures"
)
if find "$TMP_DIR/.pio" -maxdepth 1 -type f -name 'furble-sim-preferences-*.bin' \
    -print -quit 2>/dev/null | grep -q .; then
  echo "dead-PID generated preference stores were not reclaimed" >&2
  exit 1
fi
if [ ! -f "$EXPLICIT" ]; then
  echo "the dead-PID sweep removed a caller-owned store" >&2
  exit 1
fi

echo "preference ownership lifecycle check passed"
