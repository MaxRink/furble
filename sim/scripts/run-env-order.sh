#!/bin/sh

# Guard each environment mutation while SDL is live. The current binary must
# pass both a fresh boot and a resumed boot, under the two named targets and
# under "growth", which rejects any setenv that adds a variable and any unsetenv
# from any caller, production Timezone included (issue 283). Each positive leg
# must print the guard's armed line, so a preload that never loaded cannot pass.
# When FURBLE_SIM_ENV_GUARD_OLD_BIN names the pre-fix binary, it must also trip
# the exact target-specific guard at its old mutation sites.

set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BIN=${FURBLE_SIM_BIN:-"$ROOT/sim/build/furble-sim"}
OLD_BIN=${FURBLE_SIM_ENV_GUARD_OLD_BIN:-}
CC=${CC:-cc}

if [ "$(uname -s)" != "Linux" ]; then
  echo "Linux preload environment guard is not supported on this host." >&2
  exit 2
fi
if [ ! -x "$BIN" ]; then
  echo "simulator binary not found at $BIN" >&2
  exit 1
fi
if [ -n "$OLD_BIN" ] && [ ! -x "$OLD_BIN" ]; then
  echo "FURBLE_SIM_ENV_GUARD_OLD_BIN is not an executable: $OLD_BIN" >&2
  exit 1
fi
if ! command -v pkg-config >/dev/null 2>&1; then
  echo "pkg-config is required to compile the SDL environment guard" >&2
  exit 1
fi

guard_dir=$(mktemp -d "${TMPDIR:-/tmp}/furble-sim-env-guard.XXXXXX")
trap 'rm -rf "$guard_dir"' EXIT HUP INT TERM

# The guard uses RTLD_NEXT for libc's APIs. SDL is linked explicitly so the
# SDL_WasInit reference is resolved by the same library used by furble-sim.
# shellcheck disable=SC2046
"$CC" -shared -fPIC -O2 -Wall -Wextra \
  $(pkg-config --cflags sdl2) \
  -o "$guard_dir/env_mutation_guard.so" "$ROOT/sim/env_mutation_guard.c" \
  $(pkg-config --libs sdl2) -ldl

run_guarded() {
  target=$1
  shift
  # TZ is unset so a caller's zone cannot pre-create the variable and hide a
  # growing setenv("TZ") from the guard.
  env -u FURBLE_SIM_PREFS -u FURBLE_SIM_RESTART_STEP -u FURBLE_SIM_FIX_SECOND -u TZ \
    FURBLE_SIM_ENV_GUARD_TARGET="$target" \
    SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
    LD_PRELOAD="$guard_dir/env_mutation_guard.so" "$@"
}

expect_current_pass() {
  target=$1
  scenario=$2
  log="$guard_dir/current-${target}-$(basename "$scenario" .txt).log"
  if ! run_guarded "$target" "$BIN" --script "$scenario" >"$log" 2>&1; then
    echo "current simulator failed env guard $target: $scenario" >&2
    cat "$log" >&2
    exit 1
  fi
  if ! grep -F -x "sim env guard: armed $target" "$log" >/dev/null 2>&1; then
    echo "env guard was not loaded for $target: $scenario" >&2
    cat "$log" >&2
    exit 1
  fi
}

for target in FURBLE_SIM_PREFS FURBLE_SIM_RESTART_STEP growth; do
  expect_current_pass "$target" "$ROOT/sim/scripts/smoke.txt"
  expect_current_pass "$target" "$ROOT/sim/scenarios/e2e/restart-persist.txt"
done

if [ -z "$OLD_BIN" ]; then
  echo "environment ordering guard passed for current; no old binary to reject."
  exit 0
fi

expect_old_guard() {
  target=$1
  scenario=$2
  log="$guard_dir/old-${target}.log"
  if run_guarded "$target" "$OLD_BIN" --script "$scenario" >"$log" 2>&1; then
    echo "old simulator unexpectedly passed env guard: $scenario" >&2
    exit 1
  else
    status=$?
  fi
  if [ "$status" -ne 86 ]; then
    echo "old simulator failed without the expected env guard status: $status" >&2
    exit 1
  fi
  expected="sim env guard: setenv(${target}) after SDL initialization"
  if ! grep -F -x "$expected" "$log" >/dev/null 2>&1; then
    echo "old simulator did not report the exact guarded variable: $target" >&2
    cat "$log" >&2
    exit 1
  fi
}

expect_old_guard FURBLE_SIM_PREFS "$ROOT/sim/scripts/smoke.txt"
expect_old_guard FURBLE_SIM_RESTART_STEP "$ROOT/sim/scenarios/e2e/restart-persist.txt"

echo "environment ordering guard passed for current and rejected the old simulator."
