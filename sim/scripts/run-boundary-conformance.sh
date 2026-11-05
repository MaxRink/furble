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
ctest --test-dir "$BUILD_DIR" -R '^(boundary-|real-wifi-lifecycle$)' -V | tee "$LOG"

status=0
for token in \
  "headless.no_panel touched=0" \
  "headless.services" \
  "eth.dhcp link=up ip=192.0.2.42" \
  "eth.reconnect ip=198.51.100.7" \
  "wifi.fallback wired_defer=1 wired_resume=1" \
  "ir.transmit protocol=sony carrier=40000 symbols=63" \
  "ir.stack psram=unavailable fallback=internal" \
  "ir.timeout wait_ms=1000" \
  "sd.write path=" \
  "sd.full write_failed=1" \
  "sd.removed mount_failures=3" \
  "feedback.sound event=shutter" \
  "feedback.overcurrent begin_failures=2" \
  "feedback.brownout low_battery_samples=6" \
  "feedback.light event=connected" \
  "companion.enable services=2 characteristics=9" \
  "companion.auth bonded=1" \
  "companion.reidentify address_changed=1" \
  "companion.restart enabled=1 bond=loaded" \
  "tls.handshake handshakes=1" \
  "tls.alloc_failure failed=1" \
  "tls.reopen handshakes=2" \
  "webui_tls_socket_test: PASS" \
  "headless_service_test: PASS" \
  "companion_gatt_transport_test: PASS"; do
  if ! grep -F -- "$token" "$LOG" >/dev/null 2>&1; then
    echo "FAIL: linked boundary token missing: $token" >&2
    status=1
  fi
done

if [ "$status" -eq 0 ]; then
  echo "Linked boundary conformance passed."
fi
exit "$status"
