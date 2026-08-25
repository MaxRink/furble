#!/bin/sh

set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
SIM=${FURBLE_SIM_BIN:-"$ROOT/sim/build/furble-sim"}
PREFS=${FURBLE_SIM_DEEP_SLEEP_PREFS:-"$ROOT/.pio/furble-sim-deep-sleep-cycle.bin"}
EVIDENCE=${FURBLE_SIM_DEEP_SLEEP_EVIDENCE:-"$PREFS.ready"}
START_TIMEOUT=${FURBLE_SIM_DEEP_SLEEP_START_TIMEOUT:-30}

case "$START_TIMEOUT" in
  ''|*[!0-9]*)
    echo "FURBLE_SIM_DEEP_SLEEP_START_TIMEOUT must be a positive integer" >&2
    exit 2
    ;;
esac
timeout_digits=$START_TIMEOUT
while [ "${timeout_digits#0}" != "$timeout_digits" ]; do
  timeout_digits=${timeout_digits#0}
done
if [ -z "$timeout_digits" ]; then
  echo "FURBLE_SIM_DEEP_SLEEP_START_TIMEOUT must be a positive integer" >&2
  exit 2
fi

if [ ! -x "$SIM" ]; then
  echo "Simulator binary not found: $SIM" >&2
  exit 1
fi

export SDL_VIDEODRIVER=dummy
export SDL_AUDIODRIVER=dummy
export FURBLE_SIM_PREFS="$PREFS"
export FURBLE_SIM_DEEP_SLEEP_EVIDENCE="$EVIDENCE"

run_start() {
  timeout_marker=$(mktemp "${TMPDIR:-/tmp}/furble-deep-sleep-timeout.XXXXXX")
  rm -f "$timeout_marker"

  "$SIM" --script "$ROOT/sim/scenarios/deep-sleep/intervalometer-deep-sleep-start.txt" &
  sim_pid=$!
  (
    sleep "$START_TIMEOUT"
    if kill -0 "$sim_pid" 2>/dev/null; then
      if kill "$sim_pid" 2>/dev/null; then
        : > "$timeout_marker"
        kill -KILL "$sim_pid" 2>/dev/null || :
      fi
    fi
  ) &
  watcher_pid=$!

  if wait "$sim_pid"; then
    sim_status=0
  else
    sim_status=$?
  fi
  kill "$watcher_pid" 2>/dev/null || :
  wait "$watcher_pid" 2>/dev/null || :

  if [ -f "$timeout_marker" ]; then
    rm -f "$timeout_marker"
    echo "Timed-sleep start scenario did not reach simulated power-off within ${START_TIMEOUT}s" >&2
    return 1
  fi
  rm -f "$timeout_marker"
  return "$sim_status"
}

# The first invocation exits at the simulated timed shutdown. Its sidecar was
# written only after the simulator read both keys back before _Exit. The second
# process keeps the NVS file and its scenario assertions prove that the keys
# survived the process boundary and that the resume shot completed.
unset FURBLE_SIM_PRESERVE_PREFS
rm -f "$EVIDENCE"
run_start
expected_evidence=resume_and_timed_wake_persisted
if evidence=$(cat "$EVIDENCE" 2>/dev/null); then
  :
else
  evidence=
fi
if [ "$evidence" != "$expected_evidence" ]; then
  echo "Timed-sleep start did not verify the expected pre-exit persistence evidence" >&2
  exit 1
fi
export FURBLE_SIM_PRESERVE_PREFS=1
"$SIM" --script "$ROOT/sim/scenarios/deep-sleep/intervalometer-deep-sleep-resume.txt"

"$SIM" --script "$ROOT/sim/scenarios/deep-sleep/intervalometer-deep-sleep-invalid.txt"
"$SIM" --script "$ROOT/sim/scenarios/deep-sleep/intervalometer-deep-sleep-stale.txt"
"$SIM" --script "$ROOT/sim/scenarios/deep-sleep/intervalometer-deep-sleep-fallback.txt"

if [ "${FURBLE_SIM_UNSUPPORTED_BIN:-}" != "" ]; then
  "$FURBLE_SIM_UNSUPPORTED_BIN" \
    --script "$ROOT/sim/scenarios/deep-sleep/intervalometer-deep-sleep-unsupported.txt"
fi
