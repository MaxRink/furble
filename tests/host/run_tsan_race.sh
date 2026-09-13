#!/bin/sh

# Run the thread-sanitizer race probe as a fail-closed gate. Every sanitizer
# warning and every non-zero child status fails; no report-name filter or
# suppression may hide a race. The complete child output is retained on every
# failure so the actual field and both access stacks can be triaged.

set -u

BIN=${1:?usage: run_tsan_race.sh <binary>}
OUT=$(mktemp "${TMPDIR:-/tmp}/furble-tsan.XXXXXX") || exit 1

# halt_on_error=0 so every race is reported rather than the first one only.
TSAN_OPTIONS="halt_on_error=0" "$BIN" >"$OUT" 2>&1
rc=$?

total=$(grep -c 'WARNING: ThreadSanitizer' "$OUT" || true)
echo "thread sanitizer: $total race report(s)"

if [ "$total" -ne 0 ]; then
  echo "FAIL: ThreadSanitizer reported a race." >&2
  cat "$OUT" >&2
  rm -f "$OUT"
  exit 1
fi

if [ "$rc" -ne 0 ]; then
  echo "FAIL: probe exited $rc" >&2
  cat "$OUT" >&2
  rm -f "$OUT"
  exit 1
fi

if ! grep -q 'control-connect-camera-race: PASS' "$OUT"; then
  echo "FAIL: probe did not run to completion" >&2
  cat "$OUT" >&2
  rm -f "$OUT"
  exit 1
fi

rm -f "$OUT"
echo "control-connect-camera-race: PASS"
exit 0
