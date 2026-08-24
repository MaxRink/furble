#!/bin/sh

# Prove that the direct simulator build follows compiler-discovered header
# dependencies. This deliberately builds a complete simulator once, then
# touches one project header and checks that its dependents rebuild while an
# unrelated firmware source remains cached.

set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
DEP_ROOT=${FURBLE_DEP_ROOT:-"$ROOT/.pio/libdeps/m5stick-s3"}
LVGL_DIR=${FURBLE_LVGL_DIR:-"$ROOT/managed_components/lvgl__lvgl"}
TEST_ROOT=$(mktemp -d "${TMPDIR:-/tmp}/furble-sim-build-deps.XXXXXX")
HEADER="$ROOT/include/FurbleGPS.h"
HEADER_SNAPSHOT="$TEST_ROOT/FurbleGPS.h"
WRAPPER="$ROOT/sim/scripts/compile-log-wrapper.sh"

restore_header() {
  if [ -f "$HEADER_SNAPSHOT" ]; then
    touch -r "$HEADER_SNAPSHOT" "$HEADER"
  fi
}

cleanup() {
  restore_header
  rm -rf "$TEST_ROOT"
}
trap cleanup EXIT HUP INT TERM

if [ ! -f "$DEP_ROOT/M5GFX/src/M5GFX.cpp" ] ||
  [ ! -f "$DEP_ROOT/M5Unified/src/M5Unified.cpp" ] ||
  [ ! -f "$DEP_ROOT/TinyGPSPlus/src/TinyGPS++.cpp" ] ||
  [ ! -f "$LVGL_DIR/CMakeLists.txt" ]; then
  echo "simulator dependencies are missing; set FURBLE_DEP_ROOT and FURBLE_LVGL_DIR" >&2
  exit 2
fi

cp -p "$HEADER" "$HEADER_SNAPSHOT"
ln -s "$WRAPPER" "$TEST_ROOT/compile-cxx"
ln -s "$WRAPPER" "$TEST_ROOT/compile-cc"

run_build() {
  log=$1
  output=$2
  : >"$log"
  if ! FURBLE_DEP_ROOT="$DEP_ROOT" \
    FURBLE_LVGL_DIR="$LVGL_DIR" \
    FURBLE_SIM_BUILD_DIR="$TEST_ROOT/build" \
    FURBLE_COMPILE_LOG="$log" \
    CXX="$TEST_ROOT/compile-cxx" \
    CC="$TEST_ROOT/compile-cc" \
    sh "$ROOT/sim/build.sh" >"$output" 2>&1; then
    cat "$output" >&2
    exit 1
  fi
}

run_build "$TEST_ROOT/clean-compile.log" "$TEST_ROOT/clean-build.log"

# The same invocation must also produce depfiles for the C icon sources; this
# guards the C path even though the behavioral mtime check below uses C++
# sources that include FurbleGPS.h.
if ! find "$TEST_ROOT/build/obj" -name '*_c.o.d' -print -quit | grep -q .; then
  echo "expected at least one C compiler depfile" >&2
  exit 1
fi

# Ensure the header mtime is unambiguously newer than the just-built objects,
# while the trap restores the original timestamp after the test.
sleep 1
touch "$HEADER"
run_build "$TEST_ROOT/incremental-compile.log" "$TEST_ROOT/incremental-build.log"

case "$(grep -c '/src/FurbleGPS.cpp$' "$TEST_ROOT/incremental-compile.log" || true)" in
  1) ;;
  *)
    echo "expected FurbleGPS.cpp to rebuild after FurbleGPS.h changed" >&2
    cat "$TEST_ROOT/incremental-compile.log" >&2
    exit 1
    ;;
esac

case "$(grep -c '/src/FurbleUI.cpp$' "$TEST_ROOT/incremental-compile.log" || true)" in
  1) ;;
  *)
    echo "expected FurbleUI.cpp to rebuild after FurbleGPS.h changed" >&2
    cat "$TEST_ROOT/incremental-compile.log" >&2
    exit 1
    ;;
esac

if grep -q '/src/FurbleBootScreen.cpp$' "$TEST_ROOT/incremental-compile.log"; then
  echo "unrelated FurbleBootScreen.cpp was rebuilt" >&2
  cat "$TEST_ROOT/incremental-compile.log" >&2
  exit 1
fi

echo "sim build dependency self-test passed: GPS dependents rebuilt, unrelated source cached"
