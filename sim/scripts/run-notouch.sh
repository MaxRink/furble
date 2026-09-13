#!/bin/sh

# Run the certified bug-hunt and end-to-end sets in the physical-button layout,
# which is the layout every board sim/build.sh models actually ships. The M5GFX
# SDL panel always attaches a mouse-driven touch device, so an unseeded run
# renders the touch layout instead, and only the Core2, which is not modeled,
# ships that one. FURBLE_SIM_NO_TOUCH=1 detaches the device for the whole run.
#
# The layout is 26 px shorter, so this is a real second pass over every page
# assertion rather than a repeat. plans/168-notouch-layout-overflows.md closed
# the gaps that kept it red.
#
# Two kinds of scenario are skipped, and both say so as they go.
#
# A scenario that asserts "ui.nav_layout touch" anywhere in it belongs to the
# touch layout, not this one: it is written for the layout the unmodeled Core2
# ships, and forcing it into this layout would make it measure the wrong
# subject. The skip is derived from that guard rather than from a name list, so
# a new touch-layout scenario is handled without editing this script.
#
# Nothing is skipped by name any more. Two 80x160 scenarios used to be, both
# seeding the maximum text size, because that board cannot render its pages at
# Large in the layout it ships. TextSizePolicy caps that board at Normal, the
# Text size roller offers Small and Normal there, and a stored Large clamps to
# Normal, so the two run like any other: they seed a size the board clamps and
# measure the size it actually renders.

set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BIN=${FURBLE_SIM_BIN:-"$ROOT/sim/build/furble-sim"}
BOARD=${FURBLE_SIM_BOARD_ID:-m5stick-s3}
SCENARIO_TIMEOUT=${FURBLE_SIM_SCENARIO_TIMEOUT:-300}

: "${SDL_VIDEODRIVER:=dummy}"
: "${SDL_AUDIODRIVER:=dummy}"
export SDL_VIDEODRIVER SDL_AUDIODRIVER

FURBLE_SIM_NO_TOUCH=1
export FURBLE_SIM_NO_TOUCH

# The same optional capabilities the touch-layout runners report, so the
# Infrared, Feedback and Storage submenus are reachable here too.
: "${FURBLE_SIM_IR:=1}"
: "${FURBLE_SIM_FEEDBACK:=1}"
: "${FURBLE_SIM_SD:=1}"
export FURBLE_SIM_IR FURBLE_SIM_FEEDBACK FURBLE_SIM_SD

if [ ! -x "$BIN" ]; then
  echo "simulator binary not found at $BIN" >&2
  exit 1
fi

if command -v timeout > /dev/null 2>&1; then
  TIMEOUT=timeout
elif command -v gtimeout > /dev/null 2>&1; then
  TIMEOUT=gtimeout
else
  echo "GNU timeout is required to bound a simulator run." >&2
  echo "On macOS: brew install coreutils, which provides gtimeout." >&2
  exit 1
fi

# A scenario that guards on the touch layout is measuring the other layout.
touch_layout_scenario() {
  grep -Eq '^[[:space:]]*assert[[:space:]]+ui\.nav_layout[[:space:]]+touch[[:space:]]*$' "$1"
}

# The two 80x160 maximum text size scenarios, see the header.

status=0
count=0
skips=0
for suite in bughunt e2e; do
  if ! scenarios=$(python3 "$ROOT/tools/check_sim_scenarios.py" \
      --list-certified --suite "$suite" --board "$BOARD"); then
    echo "Failed to select certified $BOARD $suite scenarios." >&2
    exit 1
  fi
  if [ -z "$scenarios" ]; then
    echo "No certified $BOARD $suite scenarios selected." >&2
    exit 1
  fi
  for scenario in $scenarios; do
    name=$(basename "$scenario" .txt)
    if touch_layout_scenario "$ROOT/$scenario"; then
      echo "SKIP $name (asserts the touch layout)"
      skips=$((skips + 1))
      continue
    fi
    count=$((count + 1))
    echo "=== $name ==="
    if "$TIMEOUT" -k 10 "$SCENARIO_TIMEOUT" "$BIN" --script "$ROOT/$scenario"; then
      echo "PASS $name"
    else
      rc=$?
      if [ "$rc" -eq 124 ] || [ "$rc" -eq 137 ]; then
        echo "FAIL $name (timed out after ${SCENARIO_TIMEOUT}s)"
      else
        echo "FAIL $name (exit $rc)"
      fi
      status=1
    fi
  done
done

if [ "$count" -eq 0 ]; then
  echo "no certified scenarios owned for $BOARD" >&2
  exit 1
fi

if [ "$status" -eq 0 ]; then
  echo "All $count $BOARD scenarios passed in the physical-button layout, $skips skipped."
fi
exit $status
