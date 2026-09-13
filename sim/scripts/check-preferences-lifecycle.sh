#!/bin/sh

# Check only the simulator-owned preference lifecycle. This does not reclaim
# abnormal leftovers and never supplies a caller-owned path for cleanup.
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
  status=0
  (
    cd "$TMP_DIR"
    env -u FURBLE_SIM_PREFS -u FURBLE_SIM_PREFS_GENERATED -u FURBLE_SIM_RESTART_STEP \
      FURBLE_SIM_CRASH_STEP=0 "$BIN" --script "$CRASH_SCRIPT" \
      --capture-dir "$TMP_DIR/crash-captures"
  ) || status=$?
  if [ "$status" -eq 0 ]; then
    echo "crash fixture unexpectedly succeeded" >&2
    exit 1
  fi
}
run_expected_crash
run_expected_crash
find "$TMP_DIR/.pio" -maxdepth 1 -type f -name 'furble-sim-preferences-*.bin' -print \
  | sort >"$TMP_DIR/crash-before.txt"
crashCount=$(wc -l <"$TMP_DIR/crash-before.txt" | tr -d ' ')
uniqueCrashCount=$(sort -u "$TMP_DIR/crash-before.txt" | wc -l | tr -d ' ')
if [ "$crashCount" -ne 2 ] || [ "$uniqueCrashCount" -ne 2 ]; then
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
find "$TMP_DIR/.pio" -maxdepth 1 -type f -name 'furble-sim-preferences-*.bin' -print \
  | sort >"$TMP_DIR/crash-after.txt"
if ! cmp -s "$TMP_DIR/crash-before.txt" "$TMP_DIR/crash-after.txt"; then
  echo "orderly run swept unrelated crash leftovers" >&2
  exit 1
fi
if find "$RESTART_DIR/.pio" -maxdepth 1 -type f -name 'furble-sim-preferences-*' \
    -print -quit 2>/dev/null | grep -q .; then
  echo "owned generated preference store survived orderly cleanup" >&2
  exit 1
fi

echo "preference ownership lifecycle check passed"
