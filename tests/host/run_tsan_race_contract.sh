#!/bin/sh

# Contract test for run_tsan_race.sh. The fixtures cover clean success,
# warning-only output, sanitizer status 66 (with and without a field name), an
# unexpected child status, and a missing PASS marker. Every negative case must
# retain the complete fixture output for diagnosis.

set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
WRAPPER="$ROOT/run_tsan_race.sh"
WORK=$(mktemp -d "${TMPDIR:-/tmp}/furble-tsan-contract.XXXXXX")
trap 'rm -rf "$WORK"' EXIT INT TERM

make_fixture() {
  name=$1
  body=$2
  status=$3
  path="$WORK/$name"
  printf '%s\n' '#!/bin/sh' "printf '%s\\n' '$body'" "exit $status" >"$path"
  chmod +x "$path"
}

run_case() {
  name=$1
  expected=$2
  marker=$3
  log="$WORK/$name.log"
  if "$WRAPPER" "$WORK/$name" >"$log" 2>&1; then
    rc=0
  else
    rc=$?
  fi
  [ "$rc" -eq "$expected" ] || { cat "$log" >&2; return 1; }
  if [ -n "$marker" ]; then
    grep -Fq "$marker" "$log" || { cat "$log" >&2; return 1; }
  fi
}

make_fixture clean 'control-connect-camera-race: PASS' 0
make_fixture unrelated 'WARNING: ThreadSanitizer: data race (unrelated)' 66
make_fixture named 'WARNING: ThreadSanitizer: data race in m_ConnectAbort' 66
make_fixture warning_zero 'WARNING: ThreadSanitizer: data race (status zero)' 0
make_fixture unexpected 'unexpected child marker' 7
make_fixture missing_pass 'clean child output without completion marker' 0

run_case clean 0 ''
run_case unrelated 1 'WARNING: ThreadSanitizer: data race (unrelated)'
run_case named 1 'WARNING: ThreadSanitizer: data race in m_ConnectAbort'
run_case warning_zero 1 'WARNING: ThreadSanitizer: data race (status zero)'
run_case unexpected 1 'unexpected child marker'
run_case missing_pass 1 'clean child output without completion marker'
echo 'run_tsan_race wrapper contract: PASS'
