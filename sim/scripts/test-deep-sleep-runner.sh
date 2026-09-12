#!/bin/sh

set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
RUNNER="$ROOT/sim/scripts/run-deep-sleep.sh"
FAKE="$ROOT/sim/scripts/deep-sleep-runner-fake.sh"
RUNNER_SHELL=${FURBLE_SIM_RUNNER_SHELL:-sh}
TMPDIR_RUN=$(mktemp -d "${TMPDIR:-/tmp}/furble-deep-sleep-runner.XXXXXX")
trap 'rm -rf "$TMPDIR_RUN"' EXIT HUP INT TERM

run_case() {
  label=$1
  mode=$2
  expected=$3
  prefs="$TMPDIR_RUN/$label.prefs"
  evidence="$TMPDIR_RUN/$label.evidence"
  if FURBLE_SIM_BIN="$FAKE" \
      FURBLE_SIM_DEEP_SLEEP_PREFS="$prefs" \
      FURBLE_SIM_DEEP_SLEEP_EVIDENCE="$evidence" \
      FURBLE_SIM_DEEP_SLEEP_START_TIMEOUT=3 \
      FURBLE_SIM_DEEP_SLEEP_FAKE_MODE="$mode" \
      "$RUNNER_SHELL" "$RUNNER" >/dev/null 2>&1; then
    status=0
  else
    status=$?
  fi
  if [ "$status" -ne "$expected" ]; then
    echo "$label: expected exit $expected, got $status" >&2
    exit 1
  fi
  echo "PASS $label"
}

# Run close-to-deadline exits repeatedly to expose a watcher race. The fake
# exits after two seconds while the watchdog is three seconds.
n=1
while [ "$n" -le 5 ]; do
  run_case "near-deadline-$n" near_deadline 0
  n=$((n + 1))
done

run_invalid_timeout() {
  invalid_value=$1
  invalid_prefs="$TMPDIR_RUN/invalid-timeout-$invalid_value.prefs"
  invalid_evidence="$TMPDIR_RUN/invalid-timeout-$invalid_value.evidence"
  if FURBLE_SIM_BIN="$FAKE" \
      FURBLE_SIM_DEEP_SLEEP_PREFS="$invalid_prefs" \
      FURBLE_SIM_DEEP_SLEEP_EVIDENCE="$invalid_evidence" \
      FURBLE_SIM_DEEP_SLEEP_START_TIMEOUT="$invalid_value" \
      FURBLE_SIM_DEEP_SLEEP_FAKE_MODE=normal \
      "$RUNNER_SHELL" "$RUNNER" >/dev/null 2>&1; then
    echo "invalid-timeout-$invalid_value: expected failure" >&2
    exit 1
  else
    status=$?
    if [ "$status" -ne 2 ]; then
      echo "invalid-timeout-$invalid_value: expected exit 2, got $status" >&2
      exit 1
    fi
  fi
  echo "PASS invalid-timeout-$invalid_value"
}

run_invalid_timeout 0
run_invalid_timeout -1
run_invalid_timeout abc
run_invalid_timeout 3601
run_invalid_timeout 99999
run_invalid_timeout 12345678901234567890

run_case arbitrary-evidence arbitrary_evidence 1
run_case hung hung 1
run_case term-trap-zero term_trap_zero 1
run_case nonzero nonzero 7
