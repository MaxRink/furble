#!/bin/sh

# Guard each environment mutation while SDL is live. The current binary must
# pass both a fresh boot and a resumed boot, under the two named targets and
# under "growth", which rejects any variable added by setenv or putenv, any
# unsetenv, and clearenv, from any caller (issue 283). A small SDL program must
# first trip the growth guard, and each positive leg must print the guard's
# armed line, so the check cannot pass with a guard that never fires or never
# loaded. When FURBLE_SIM_ENV_GUARD_OLD_BIN names the pre-fix binary, it must
# also trip the exact target-specific guard at its old mutation sites.

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
    -u FURBLE_GUARD_SELFTEST \
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

# Self-tests: a program that initializes SDL and then changes the environment
# one way per mode. Every structural change must trip the growth guard with its
# exact message, so a passing simulator leg cannot mean a wrapper never fires.
# The simulator itself calls neither putenv nor clearenv. A replacement of an
# existing variable, even with a name longer than any fixed buffer, must pass.
cat > "$guard_dir/selftest.c" <<'EOF'
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char long_name[301];
static char long_set[310];

int main(int argc, char **argv) {
  const char *mode = argc > 1 ? argv[1] : "";
  memset(long_name, 'L', sizeof(long_name) - 1);
  snprintf(long_set, sizeof(long_set), "%s=2", long_name);
  if (strcmp(mode, "putenv-long-replace") == 0) {
    setenv(long_name, "1", 1);
  }
  if (SDL_Init(SDL_INIT_EVENTS) != 0) {
    return 2;
  }
  if (strcmp(mode, "setenv") == 0) {
    setenv("FURBLE_GUARD_SELFTEST", "1", 1);
  } else if (strcmp(mode, "putenv-add") == 0) {
    putenv("FURBLE_GUARD_SELFTEST=1");
  } else if (strcmp(mode, "putenv-unset") == 0) {
    putenv("PATH");
  } else if (strcmp(mode, "clearenv") == 0) {
    clearenv();
  } else if (strcmp(mode, "putenv-long-add") == 0
             || strcmp(mode, "putenv-long-replace") == 0) {
    putenv(long_set);
  } else {
    return 3;
  }
  return 0;
}
EOF
# shellcheck disable=SC2046
"$CC" -O0 $(pkg-config --cflags sdl2) -o "$guard_dir/selftest" "$guard_dir/selftest.c" \
  $(pkg-config --libs sdl2)
long_name=$(printf '%300s' '' | tr ' ' L)
selftest() {
  mode=$1
  want_status=$2
  want_line=$3
  selftest_log="$guard_dir/selftest-$mode.log"
  if run_guarded growth "$guard_dir/selftest" "$mode" >"$selftest_log" 2>&1; then
    status=0
  else
    status=$?
  fi
  if [ "$status" -ne "$want_status" ] || { [ -n "$want_line" ] \
    && ! grep -F -x "$want_line" "$selftest_log" >/dev/null 2>&1; }; then
    echo "growth guard self-test $mode: status $status, want $want_status" >&2
    cat "$selftest_log" >&2
    exit 1
  fi
}
selftest setenv 86 "sim env guard: setenv(FURBLE_GUARD_SELFTEST) after SDL initialization"
selftest putenv-add 86 "sim env guard: setenv(FURBLE_GUARD_SELFTEST) after SDL initialization"
selftest putenv-unset 86 "sim env guard: unsetenv(PATH) after SDL initialization"
selftest clearenv 86 "sim env guard: clearenv() after SDL initialization"
selftest putenv-long-add 86 "sim env guard: setenv($long_name) after SDL initialization"
selftest putenv-long-replace 0 ""

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
