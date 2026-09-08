#!/bin/sh

set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
RUNNER="$ROOT/sim/scripts/run-fuzz.sh"
TMPDIR_RUN=$(mktemp -d "${TMPDIR:-/tmp}/furble-fuzz-runner.XXXXXX")
trap 'rm -rf "$TMPDIR_RUN"' EXIT HUP INT TERM

FAKE="$TMPDIR_RUN/fake-sim"
COUNT="$TMPDIR_RUN/count"
cat >"$FAKE" <<'EOF'
#!/bin/sh
seed=
steps=
while [ "$#" -gt 0 ]; do
  case "$1" in
    --seed) seed=$2; shift 2 ;;
    --fuzz-steps) steps=$2; shift 2 ;;
    *) shift ;;
  esac
done
count=0
if [ -f "$FURBLE_FAKE_COUNT" ]; then
  count=$(cat "$FURBLE_FAKE_COUNT")
fi
count=$((count + 1))
printf '%s\n' "$count" >"$FURBLE_FAKE_COUNT"
if [ "$FURBLE_FAKE_MODE" = missing ] || \
   { [ "$FURBLE_FAKE_MODE" = replay_missing ] && [ "$seed" = 2 ] && [ "$count" -eq 3 ]; }; then
  exit 0
fi
if [ "$FURBLE_FAKE_MODE" = invalid ]; then
  echo "FUZZ SUMMARY seed=$seed steps=$steps attempted=1 observed_delta=1 no_observed_delta=0 settled=1"
  exit 0
fi
if [ "$FURBLE_FAKE_MODE" = one_restart ]; then
  echo "FUZZ SUMMARY seed=$seed steps=$steps attempted=2 observed_delta=1 no_observed_delta=0 settled=1 interrupted_by_restart=1 resumed_boots=1"
  exit 0
fi
if [ "$FURBLE_FAKE_MODE" = mismatch ]; then
  echo "FUZZ SUMMARY seed=$seed steps=$steps attempted=$steps observed_delta=0 no_observed_delta=$steps settled=$steps interrupted_by_restart=1 resumed_boots=0"
  exit 0
fi
if [ "$FURBLE_FAKE_MODE" = inflated_boots ]; then
  echo "FUZZ SUMMARY seed=$seed steps=$steps attempted=$steps observed_delta=0 no_observed_delta=$steps settled=$steps interrupted_by_restart=0 resumed_boots=1"
  exit 0
fi
echo "FUZZ SUMMARY seed=$seed steps=$steps attempted=$steps observed_delta=0 no_observed_delta=$steps settled=$steps interrupted_by_restart=0 resumed_boots=0"
if [ "$FURBLE_FAKE_MODE" = replay_diff ] && [ "$seed" = 2 ] && [ "$count" -eq 3 ]; then
  echo "FUZZ EVENT replay-only"
fi
exit 0
EOF
chmod +x "$FAKE"

run_failure_case() {
  mode=$1
  xfail=$2
  output="$TMPDIR_RUN/$mode.out"
  : >"$COUNT"
  if FURBLE_SIM_BIN="$FAKE" \
      FURBLE_FUZZ_SEEDS=1 \
      FURBLE_FUZZ_XFAIL_SEEDS="$xfail" \
      FURBLE_FUZZ_REPEAT_SEED= \
      FURBLE_FAKE_MODE="$mode" \
      FURBLE_FAKE_COUNT="$COUNT" \
      "$RUNNER" >"$output" 2>&1; then
    echo "$mode: expected failure" >&2
    exit 1
  fi
  if grep -q 'PASS fuzz seed\|XFAIL fuzz seed' "$output"; then
    echo "$mode: invalid summary was classified as PASS/XFAIL" >&2
    exit 1
  fi
}

run_failure_case missing 1
run_failure_case invalid 1

run_success_case() {
  mode=$1
  output="$TMPDIR_RUN/$mode.out"
  : >"$COUNT"
  if ! FURBLE_SIM_BIN="$FAKE" \
      FURBLE_FUZZ_SEEDS=1 \
      FURBLE_FUZZ_STEPS=2 \
      FURBLE_FUZZ_REPEAT_SEED= \
      FURBLE_FAKE_MODE="$mode" \
      FURBLE_FAKE_COUNT="$COUNT" \
      "$RUNNER" >"$output" 2>&1; then
    cat "$output" >&2
    echo "$mode: expected success" >&2
    exit 1
  fi
  grep -q 'PASS fuzz seed 1' "$output"
}

run_success_case normal
run_success_case one_restart

run_failure_case mismatch ""
run_failure_case inflated_boots ""

: >"$COUNT"
output="$TMPDIR_RUN/replay-missing.out"
if FURBLE_SIM_BIN="$FAKE" \
    FURBLE_FUZZ_SEEDS=1 \
    FURBLE_FUZZ_REPEAT_SEED=2 \
    FURBLE_FAKE_MODE=replay_missing \
    FURBLE_FAKE_COUNT="$COUNT" \
    "$RUNNER" >"$output" 2>&1; then
  echo "replay-missing: expected failure" >&2
  exit 1
fi
if grep -q 'PASS determinism replay' "$output"; then
  echo "replay-missing: invalid replay was classified as PASS" >&2
  exit 1
fi

: >"$COUNT"
output="$TMPDIR_RUN/replay-diff.out"
if FURBLE_SIM_BIN="$FAKE" \
    FURBLE_FUZZ_SEEDS=1 \
    FURBLE_FUZZ_REPEAT_SEED=2 \
    FURBLE_FAKE_MODE=replay_diff \
    FURBLE_FAKE_COUNT="$COUNT" \
    "$RUNNER" >"$output" 2>&1; then
  echo "replay-diff: expected failure" >&2
  exit 1
fi
if grep -q 'PASS determinism replay' "$output"; then
  echo "replay-diff: divergent replay was classified as PASS" >&2
  exit 1
fi

echo "PASS run-fuzz classification checks"
