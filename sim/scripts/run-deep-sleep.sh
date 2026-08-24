#!/bin/sh

set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
SIM=${FURBLE_SIM_BIN:-"$ROOT/sim/build/furble-sim"}
PREFS=${FURBLE_SIM_DEEP_SLEEP_PREFS:-"$ROOT/.pio/furble-sim-deep-sleep-cycle.bin"}

if [ ! -x "$SIM" ]; then
  echo "Simulator binary not found: $SIM" >&2
  exit 1
fi

export SDL_VIDEODRIVER=dummy
export SDL_AUDIODRIVER=dummy
export FURBLE_SIM_PREFS="$PREFS"

# The first invocation exits at the simulated timed shutdown. The second keeps
# the NVS file and therefore sees both the persisted resume record and wake
# marker, exactly like app_main after a PMIC power-on.
unset FURBLE_SIM_PRESERVE_PREFS
"$SIM" --script "$ROOT/sim/scenarios/deep-sleep/intervalometer-deep-sleep-start.txt"
export FURBLE_SIM_PRESERVE_PREFS=1
"$SIM" --script "$ROOT/sim/scenarios/deep-sleep/intervalometer-deep-sleep-resume.txt"

"$SIM" --script "$ROOT/sim/scenarios/deep-sleep/intervalometer-deep-sleep-invalid.txt"
"$SIM" --script "$ROOT/sim/scenarios/deep-sleep/intervalometer-deep-sleep-stale.txt"
"$SIM" --script "$ROOT/sim/scenarios/deep-sleep/intervalometer-deep-sleep-fallback.txt"

if [ "${FURBLE_SIM_UNSUPPORTED_BIN:-}" != "" ]; then
  "$FURBLE_SIM_UNSUPPORTED_BIN" \
    --script "$ROOT/sim/scenarios/deep-sleep/intervalometer-deep-sleep-unsupported.txt"
fi
