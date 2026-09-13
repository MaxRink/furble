#!/bin/sh

# The simulator's SchedulerStopped escape must fail fast without entering
# cleanup or returning a success that could trigger a restart.

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

# GNU timeout, or the coreutils build Homebrew installs as gtimeout on macOS.
if command -v timeout >/dev/null 2>&1; then
  TIMEOUT=timeout
elif command -v gtimeout >/dev/null 2>&1; then
  TIMEOUT=gtimeout
else
  echo "GNU timeout is required to bound the simulator subprocess." >&2
  exit 1
fi

output=$(mktemp "${TMPDIR:-/tmp}/furble-scheduler-stop.XXXXXX")
trap 'rm -f "$output"' EXIT

run_bounded() {
  output_path=$1
  shift
  status=0
  "$TIMEOUT" -k 2 10 "$@" >"$output_path" 2>&1 || status=$?
}

valid_failfast_result() {
  if [ "$status" -eq 124 ] || [ "$status" -eq 137 ] || [ "$status" -ge 128 ]; then
    return 1
  fi
  [ "$status" -eq 1 ] || return 1
  grep -F "SIM FAIL: SchedulerStopped in UI task; exiting without cleanup" "$output" \
    >/dev/null 2>&1
}

run_bounded "$output" env FURBLE_SIM_TEST_SCHEDULER_STOP= \
  FURBLE_SIM_WATCHDOG_SECONDS=0 "$BIN" --script "$ROOT/sim/scripts/smoke.txt"
if [ "$status" -ne 0 ] || grep -F "SIM FAIL: SchedulerStopped" "$output" >/dev/null 2>&1; then
  echo "scheduler-stop positive control did not complete normally" >&2
  cat "$output" >&2
  exit 1
fi
echo "SchedulerStopped disabled positive control returned status 0."

run_bounded "$output" env FURBLE_SIM_TEST_SCHEDULER_STOP=1 \
  FURBLE_SIM_WATCHDOG_SECONDS=0 "$BIN" --script "$ROOT/sim/scripts/smoke.txt"

if [ "$status" -eq 124 ] || [ "$status" -eq 137 ]; then
  echo "scheduler-stop fail-fast subprocess timed out" >&2
  cat "$output" >&2
  exit 1
fi
if [ "$status" -ge 128 ]; then
  echo "scheduler-stop fail-fast subprocess died from signal $((status - 128))" >&2
  cat "$output" >&2
  exit 1
fi
if [ "$status" -ne 1 ]; then
  echo "scheduler-stop fail-fast returned $status, expected 1" >&2
  cat "$output" >&2
  exit 1
fi
if ! valid_failfast_result; then
  echo "scheduler-stop fail-fast diagnostic was missing" >&2
  cat "$output" >&2
  exit 1
fi

echo "SchedulerStopped UI fail-fast returned status 1 with its bounded diagnostic."

# These tiny wrappers exercise the harness rejection cases without pretending
# that a normal process, a signal, or a timeout is the simulator fail-fast.
for fixture in \
  "$ROOT/sim/scripts/fixtures/scheduler-stop-exit-zero.sh" \
  "$ROOT/sim/scripts/fixtures/scheduler-stop-exit-one-no-banner.sh" \
  "$ROOT/sim/scripts/fixtures/scheduler-stop-signal.sh" \
  "$ROOT/sim/scripts/fixtures/scheduler-stop-timeout.sh"; do
  run_bounded "$output" "$fixture"
  name=$(basename "$fixture")
  case "$name" in
    scheduler-stop-exit-zero.sh)
      expected_status=0
      ;;
    scheduler-stop-exit-one-no-banner.sh)
      expected_status=1
      ;;
    scheduler-stop-signal.sh)
      expected_status=143
      ;;
    scheduler-stop-timeout.sh)
      expected_status=124
      ;;
    *)
      echo "unknown fail-fast fixture: $fixture" >&2
      exit 1
      ;;
  esac
  if [ "$name" = "scheduler-stop-timeout.sh" ]; then
    if [ "$status" -ne 124 ] && [ "$status" -ne 137 ]; then
      echo "timeout fixture returned $status, expected 124 or documented kill status 137" >&2
      cat "$output" >&2
      exit 1
    fi
  elif [ "$status" -ne "$expected_status" ]; then
    echo "$name returned $status, expected $expected_status" >&2
    cat "$output" >&2
    exit 1
  fi
  if [ "$name" = "scheduler-stop-exit-one-no-banner.sh" ]; then
    if grep -F "SIM FAIL: SchedulerStopped in UI task; exiting without cleanup" "$output" \
        >/dev/null 2>&1; then
      echo "$name unexpectedly emitted the fail-fast banner" >&2
      cat "$output" >&2
      exit 1
    fi
  elif ! grep -F "SIM FAIL: SchedulerStopped in UI task; exiting without cleanup" "$output" \
      >/dev/null 2>&1; then
    echo "$name omitted the exact expected fail-fast banner" >&2
    cat "$output" >&2
    exit 1
  fi
  echo "Rejected invalid fail-fast fixture: $name (status $status)."
done
