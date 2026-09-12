#!/bin/sh

# Exercise the opt-in simulator MQTT transport against an external broker.
# This proves broker socket delivery and the production UI/Control/catalog path;
# it is not a broker-restart, TLS, ESP-IDF, or hardware test. The broker is
# intentionally managed outside this script; active-camera resubscription
# after a broker restart is not exercised here.

set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BIN=${FURBLE_SIM_BIN:-"$ROOT/sim/build/furble-sim"}
HOST=${FURBLE_MQTT_BROKER_HOST:-127.0.0.1}
PORT=${FURBLE_MQTT_BROKER_PORT:-1883}
BASE=${FURBLE_SIM_MQTT_BASE:-"furble-sim-$$"}
DEVICE_ID=${FURBLE_SIM_MQTT_ID:-}
CAMERA_ID=${FURBLE_SIM_MQTT_CAMERA_ID:-1}
WAIT_STEPS=${FURBLE_MQTT_SIM_WAIT_STEPS:-600}

case "$WAIT_STEPS" in
  ''|*[!0-9]*|0) echo "FURBLE_MQTT_SIM_WAIT_STEPS must be a positive integer" >&2; exit 2 ;;
esac
if [ -z "$DEVICE_ID" ]; then
  echo "FURBLE_SIM_MQTT_ID must be the measured simulator device ID" >&2
  exit 2
fi
case "$CAMERA_ID" in
  ''|*[!0-9]*) echo "FURBLE_SIM_MQTT_CAMERA_ID must be a numeric saved-camera ID" >&2; exit 2 ;;
esac
for tool in mosquitto_pub mosquitto_sub; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "$tool is required for the real-broker MQTT regression" >&2
    exit 1
  fi
done
if [ ! -x "$BIN" ]; then
  echo "simulator binary not found at $BIN" >&2
  echo "Build it with FURBLE_SIM_MQTT=1 and installed native dependencies." >&2
  exit 1
fi

TMP_DIR=$(mktemp -d "${TMPDIR:-/tmp}/furble-mqtt-broker.XXXXXX")
EVENTS="$TMP_DIR/events.log"
SIM_LOG="$TMP_DIR/simulator.log"
SCENARIO="$TMP_DIR/scenario.txt"
ROOT_TOPIC="$BASE/$DEVICE_ID"
STATUS_TOPIC="$ROOT_TOPIC/status"
SIM_PID=
SUB_PID=
TEST_STATUS=1

cleanup() {
  if [ -n "$SIM_PID" ]; then
    kill "$SIM_PID" 2>/dev/null || true
    wait "$SIM_PID" 2>/dev/null || true
  fi
  if [ -n "$SUB_PID" ]; then
    kill "$SUB_PID" 2>/dev/null || true
    wait "$SUB_PID" 2>/dev/null || true
  fi
  if [ "$TEST_STATUS" -eq 0 ]; then
    rm -rf "$TMP_DIR"
  else
    echo "MQTT broker regression artifacts preserved at $TMP_DIR" >&2
  fi
}
trap cleanup EXIT INT TERM

{
  echo "seed fauxny true"
  echo "seed saved_camera true"
  i=0
  while [ "$i" -lt "$WAIT_STEPS" ]; do
    echo "wait 100"
    i=$((i + 1))
  done
  echo "assert camera.shutter_presses 0"
  echo "exit"
} >"$SCENARIO"

mosquitto_sub -h "$HOST" -p "$PORT" -q 1 -t "$BASE/#" -v >"$EVENTS" 2>"$TMP_DIR/subscriber.log" &
SUB_PID=$!

# Seed the retained actuator command before the simulator connects and creates
# its command subscription. FURBLE_SIM_MQTT_ID must come from the measured
# status topic of the exact simulator binary under test.
mosquitto_pub -h "$HOST" -p "$PORT" -q 1 -r -t "$ROOT_TOPIC/cmd/shutter" -m press

FURBLE_SIM_MQTT_BASE="$BASE" FURBLE_SIM_MQTT_URI="mqtt://$HOST:$PORT" \
  "$BIN" --script "$SCENARIO" >"$SIM_LOG" 2>&1 &
SIM_PID=$!

wait_for_text() {
  text=$1
  limit=${2:-300}
  i=0
  while [ "$i" -lt "$limit" ]; do
    if grep -F "$text" "$EVENTS" >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.1
    i=$((i + 1))
  done
  echo "Timed out waiting for broker event: $text" >&2
  cat "$SIM_LOG" >&2
  cat "$EVENTS" >&2 || true
  exit 1
}

wait_for_sim_log() {
  text=$1
  limit=${2:-300}
  i=0
  while [ "$i" -lt "$limit" ]; do
    if grep -F "$text" "$SIM_LOG" >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.1
    i=$((i + 1))
  done
  echo "Timed out waiting for simulator log: $text" >&2
  cat "$SIM_LOG" >&2
  cat "$EVENTS" >&2 || true
  exit 1
}

wait_for_text "$STATUS_TOPIC online"

# This command is consumed by the real MQTT owner, then routed through the
# production UI request and Control task to the saved FauxNY catalog entry.
# A fresh `saved_camera` fixture assigns stable camera ID 1. Override it when
# the measured broker state comes from a non-fresh preference store.
mosquitto_pub -h "$HOST" -p "$PORT" -q 1 -t "$ROOT_TOPIC/cmd/connect" -m "$CAMERA_ID"
wait_for_text '"connected":true'
wait_for_sim_log "state connecting -> active"

# This externally managed mode has no broker restart leg. The retained replay
# exercises pre-subscription rejection, but not active-camera resubscription
# after a broker restart.
wait_for_text "retained MQTT command rejected"
wait_for_sim_log "MQTT simulator offline PUBACK received."

if ! wait "$SIM_PID"; then
  echo "Simulator real-broker regression exited unsuccessfully" >&2
  cat "$SIM_LOG" >&2
  exit 1
fi
SIM_PID=

if ! grep -F "assert ok: camera.shutter_presses = 0" "$SIM_LOG" >/dev/null 2>&1; then
  echo "Simulator did not prove camera.shutter_presses remained zero" >&2
  cat "$SIM_LOG" >&2
  exit 1
fi

# Process success includes the owner shutdown acknowledgment. Subscriber
# output is not used as a post-exit callback proof because it may have backlog.
TEST_STATUS=0
echo "Real-broker MQTT simulator regression passed (restart leg not exercised; not hardware/TLS proof)."
