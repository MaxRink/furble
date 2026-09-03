#!/bin/sh

# Run the thread-sanitizer race probe and fail only on a race that involves
# Control::m_ConnectCamera.
#
# A blanket "zero races" gate would need a suppression list for the races this
# PR does not claim to fix, and those top frames are template noise
# (std::_Deque_iterator, operator()), so the suppressions would be broad enough
# to hide the thing being proved. Asserting the specific claim instead is both
# tighter and free of maintenance: the guard is correct if and only if no race
# names the guarded accessor.
#
# The pre-existing count is printed, not asserted, so a rise is visible in the
# log without turning unrelated work into a failure here.

set -u

BIN=${1:?usage: run_tsan_race.sh <binary>}
OUT=$(mktemp "${TMPDIR:-/tmp}/furble-tsan.XXXXXX") || exit 1

# halt_on_error=0 so every race is reported rather than the first one only.
TSAN_OPTIONS="halt_on_error=0" "$BIN" >"$OUT" 2>&1
rc=$?

total=$(grep -c 'WARNING: ThreadSanitizer' "$OUT" || true)
guarded=$(grep -c 'getConnectingCamera\|setConnectCamera\|m_ConnectCamera' "$OUT" || true)

echo "thread sanitizer: $total race report(s), $guarded naming the guarded accessor"

if [ "$guarded" -ne 0 ]; then
  echo "FAIL: a data race names Control::m_ConnectCamera or its accessors." >&2
  echo "The guard added in plans/167 is not holding." >&2
  grep -B 2 -A 25 'getConnectingCamera\|setConnectCamera\|m_ConnectCamera' "$OUT" >&2
  rm -f "$OUT"
  exit 1
fi

# rc 66 is the sanitizer's exit for "races were found", which is expected here
# because the pre-existing ones are not fixed by this PR. Any other non-zero
# exit is the program itself failing.
if [ "$rc" -ne 0 ] && [ "$rc" -ne 66 ]; then
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
