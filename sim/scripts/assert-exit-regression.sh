#!/bin/sh

# Regression gate for the simulator's assertion contract. A failed DSL assert
# must reach the caller as a non-zero status; otherwise CI can silently accept
# a scenario that printed ASSERT FAILED.

set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BIN=${FURBLE_SIM_BIN:-"$ROOT/sim/build/furble-sim"}
: "${SDL_VIDEODRIVER:=dummy}"
: "${SDL_AUDIODRIVER:=dummy}"
export SDL_VIDEODRIVER SDL_AUDIODRIVER

if [ ! -x "$BIN" ]; then
  echo "simulator binary not found at $BIN" >&2
  exit 1
fi

script=$(mktemp "${TMPDIR:-/tmp}/furble-assert-contract.XXXXXX")
trap 'rm -f "$script"' EXIT
printf '%s\n' 'wait 100' 'assert ui.page definitely_not_a_page' 'exit' >"$script"

if "$BIN" --script "$script" >/tmp/furble-assert-contract.log 2>&1; then
  echo "simulator assertion unexpectedly returned success" >&2
  cat /tmp/furble-assert-contract.log >&2
  exit 1
fi

echo "Simulator assertion failure correctly returned non-zero status."
