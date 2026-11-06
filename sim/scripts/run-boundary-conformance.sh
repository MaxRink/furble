#!/bin/sh

# Run every linked-boundary conformance target and assert the production
# tokens the flash-readiness manifest names. This is the evidence source for
# the boundary entries under sim/coverage/hardware_quirks.json.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BUILD_DIR=${FURBLE_BOUNDARY_BUILD_DIR:-"$ROOT/.pio/boundary-host"}
LOG=$(mktemp "${TMPDIR:-/tmp}/furble-boundary.XXXXXX")

cleanup() {
  rm -f "$LOG"
}
trap cleanup EXIT INT TERM

if ! command -v cmake >/dev/null 2>&1; then
  echo "cmake is required for the linked-boundary conformance suite" >&2
  exit 1
fi

echo "=== configure $BUILD_DIR ==="
cmake -S "$ROOT/tests/host" -B "$BUILD_DIR" -DBUILD_TESTING=ON >/dev/null

echo "=== build linked boundary targets ==="
cmake --build "$BUILD_DIR" -j "${FURBLE_BOUNDARY_JOBS:-4}" --target \
  boundary_ir_test boundary_sd_test boundary_feedback_test \
  boundary_ethernet_test boundary_companion_test boundary_webui_tls_test \
  boundary_headless_service_test real_wifi_lifecycle_test >/dev/null

echo "=== run linked boundary targets ==="
status=0
set +e
ctest --test-dir "$BUILD_DIR" -R '^(boundary-|real-wifi-lifecycle$)' -V >"$LOG" 2>&1
ctest_status=$?
set -e
cat "$LOG"
if [ "$ctest_status" -ne 0 ]; then
  echo "FAIL: boundary ctest exited $ctest_status" >&2
  status=1
fi

# Structured evidence marker consumed by tools/check_sim_coverage.py. Each
# token is checked against the captured ctest output, never a README or comment.
assert_token() {
  if ! grep -F -- "$1" "$LOG" >/dev/null 2>&1; then
    echo "FAIL: linked boundary token missing: $1" >&2
    status=1
  fi
}

assert_token "headless.no_panel touched=0"
assert_token "headless.services"
assert_token "eth.dhcp link=up ip=192.0.2.42"
assert_token "eth.reconnect ip=198.51.100.7"
assert_token "wifi.fallback wired_defer=1 wired_resume=1"
assert_token "ir.transmit protocol=sony carrier=40000 symbols=63"
assert_token "ir.stack psram=unavailable fallback=internal"
assert_token "ir.timeout wait_ms=1000"
assert_token "sd.write path="
assert_token "sd.full write_failed=1"
assert_token "sd.removed mount_failures=3"
assert_token "feedback.sound event=shutter"
assert_token "feedback.overcurrent begin_failures=2"
assert_token "feedback.brownout low_battery_samples=6"
assert_token "feedback.light event=connected"
assert_token "companion.enable services=2 characteristics=9"
assert_token "companion.reset_mock services=0 started=0 advertising=0"
assert_token "companion.auth bonded=1"
assert_token "companion.password_gate webui_reloads=1"
assert_token "companion.reidentify address_changed=1"
assert_token "companion.disable enabled=0"
assert_token "companion.restart enabled=1 bond=loaded"
assert_token "tls.handshake handshakes=1"
assert_token "tls.socket_count sockets="
assert_token "tls.alloc_failure failed=1"
assert_token "tls.reopen handshakes=2"
assert_token "webui_tls_socket_test: PASS"
assert_token "headless_service_test: PASS"
assert_token "companion_gatt_transport_test: PASS"

if [ "$status" -eq 0 ]; then
  echo "Linked boundary conformance passed."
fi
exit "$status"
