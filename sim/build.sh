#!/bin/sh

set -eu
# pipefail is not POSIX, enable it where the shell supports it
if (set -o pipefail) 2> /dev/null; then
  set -o pipefail
fi

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD_DIR=${FURBLE_SIM_BUILD_DIR:-"$ROOT/sim/build"}
DEP_ROOT=${FURBLE_DEP_ROOT:-"$ROOT/.pio/libdeps/m5stick-s3"}
LVGL_DIR=${FURBLE_LVGL_DIR:-}
CXX=${CXX:-clang++}
CC=${CC:-clang}

# Board selection. Defaults model the M5StickS3 (135x240) so CI and the plain
# `sh sim/build.sh` invocation are unchanged. Override both together to emulate
# a different panel class:
#   M5StickC   80x160  : FURBLE_SIM_FURBLE_BOARD=FURBLE_M5STICKC   FURBLE_SIM_M5GFX_BOARD=board_M5StickC
#   M5Stack    320x240 : FURBLE_SIM_FURBLE_BOARD=FURBLE_M5COREX    FURBLE_SIM_M5GFX_BOARD=board_M5Stack
FURBLE_BOARD=${FURBLE_SIM_FURBLE_BOARD:-FURBLE_M5STICKS3}
M5GFX_BOARD=${FURBLE_SIM_M5GFX_BOARD:-board_M5StickS3}

if [ ! -f "$DEP_ROOT/M5GFX/src/M5GFX.cpp" ]; then
  echo "M5GFX was not found at $DEP_ROOT" >&2
  exit 1
fi

if [ ! -f "$DEP_ROOT/M5Unified/src/M5Unified.cpp" ]; then
  echo "M5Unified was not found at $DEP_ROOT" >&2
  exit 1
fi

if [ ! -f "$DEP_ROOT/TinyGPSPlus/src/TinyGPS++.cpp" ]; then
  echo "TinyGPSPlus was not found at $DEP_ROOT" >&2
  exit 1
fi

# The managed component tracks the LVGL version pinned by src/idf_component.yml.
if [ -z "$LVGL_DIR" ]; then
  candidate="$ROOT/managed_components/lvgl__lvgl"
  if [ -f "$candidate/CMakeLists.txt" ]; then
    LVGL_DIR=$candidate
  fi
fi

if [ -z "$LVGL_DIR" ] || [ ! -f "$LVGL_DIR/CMakeLists.txt" ]; then
  echo "LVGL 9 was not found. Set FURBLE_LVGL_DIR to its source directory." >&2
  exit 1
fi

if ! command -v make >/dev/null 2>&1; then
  echo "make is required for simulator dependency checks" >&2
  exit 1
fi

mkdir -p "$BUILD_DIR/obj"

INCLUDES="\
-I$ROOT/sim \
-I$ROOT/sim/shim \
-I$ROOT/include \
-I$ROOT/components/icons \
-I$ROOT/lib/preferences \
-I$ROOT/lib/furble \
-I$ROOT/lib/blowfish \
-I$ROOT/lib/testing/nimble \
-I$ROOT/lib/testing/peer \
-I$ROOT/components \
-I$DEP_ROOT/TinyGPSPlus/src \
-I$DEP_ROOT/M5GFX/src \
-I$DEP_ROOT/M5Unified/src \
-I$LVGL_DIR \
-I$LVGL_DIR/.. \
-I/opt/homebrew/include \
-I/opt/homebrew/include/SDL2 \
-I/usr/local/include \
-I/usr/local/include/SDL2"

# The companion rig transport and its placeholder header title
# ("RIG BUILD, NO BLE, NO ENCRYPTION") are on by default so the rig scenarios
# keep working. Doc captures set FURBLE_SIM_RIG=0 to build without the rig, which
# renders the shipped one-line header title instead of the placeholder string.
RIG_DEFINE="-DFURBLE_RIG"
if [ "${FURBLE_SIM_RIG:-1}" = "0" ]; then
  RIG_DEFINE=
fi

DEFINES="\
-D$FURBLE_BOARD \
$RIG_DEFINE \
-DFURBLE_SIM \
-DFURBLE_VERSION=\"sim\" \
-DFURBLE_TEST_VERSION=1 \
-DFURBLE_BATTERY_DEBUG=0 \
-DM5GFX_SCALE=2 \
-DM5GFX_BOARD=$M5GFX_BOARD \
-DLV_CONF_INCLUDE_SIMPLE \
-DLV_LVGL_H_INCLUDE_SIMPLE \
-DLV_KCONFIG_IGNORE \
-D_THREAD_SAFE"

# Optional sanitizers for the UI fuzzer's deeper memory hunt. Off by default so
# the plain build and CI stay fast; set FURBLE_SIM_SANITIZE to a clang sanitizer
# list, for example FURBLE_SIM_SANITIZE=address or address,undefined. Use a
# separate FURBLE_SIM_BUILD_DIR so the instrumented objects never overwrite the
# release-config build.
SANITIZE=${FURBLE_SIM_SANITIZE:-}
SANITIZE_FLAGS=
if [ -n "$SANITIZE" ]; then
  SANITIZE_FLAGS="-fsanitize=$SANITIZE -fno-omit-frame-pointer"
fi

# Optional source-based coverage instrumentation, used by tools/coverage.py.
# Off by default so the plain build and every existing CI job are unchanged.
# Set FURBLE_SIM_COVERAGE=1 together with a separate FURBLE_SIM_BUILD_DIR so the
# instrumented objects never overwrite the release-config build. Each run of the
# instrumented binary writes a raw profile to the path in LLVM_PROFILE_FILE.
#
# Only firmware sources are instrumented. They are the only ones the coverage
# report counts, and instrumenting LVGL and M5GFX as well slows the render path
# enough to turn a scenario run into minutes. The link still needs the profile
# runtime, so the generate flag stays on the link line.
COVERAGE_LINK_FLAGS=
COVERAGE=${FURBLE_SIM_COVERAGE:-0}
if [ "$COVERAGE" = "1" ]; then
  COVERAGE_LINK_FLAGS="-fprofile-instr-generate"
fi

# Echo the per-source coverage flags for a firmware translation unit, and
# nothing for a dependency.
coverage_flags_for() {
  [ "$COVERAGE" = "1" ] || return 0
  case "$1" in
    "$ROOT"/src/*|"$ROOT"/lib/*)
      printf '%s' "-fprofile-instr-generate -fcoverage-mapping"
      ;;
  esac
}

# The object cache keys on source timestamps only, so it cannot see a flag
# change. Toggling coverage, a sanitizer or the board in an existing build dir
# would otherwise silently reuse objects compiled with the previous flags. Stamp
# the shaping flags and drop the cache when they change. A build dir holding
# objects but no stamp predates this check, so it is treated as a mismatch once.
FLAG_STAMP="$BUILD_DIR/build-flags"
FLAG_VALUE="board=$FURBLE_BOARD m5gfx=$M5GFX_BOARD rig=${FURBLE_SIM_RIG:-1} sanitize=$SANITIZE coverage=$COVERAGE"
if [ ! -f "$FLAG_STAMP" ] || [ "$(cat "$FLAG_STAMP")" != "$FLAG_VALUE" ]; then
  if [ -f "$FLAG_STAMP" ] || [ -n "$(ls -A "$BUILD_DIR/obj" 2>/dev/null)" ]; then
    echo "[CLEAN] build flags changed, dropping $BUILD_DIR/obj"
    rm -rf "$BUILD_DIR/obj"
    mkdir -p "$BUILD_DIR/obj"
  fi
  printf '%s' "$FLAG_VALUE" >"$FLAG_STAMP"
fi

CXXFLAGS="-std=c++17 -O0 -g -Wall -Wextra -Wno-unused-parameter $SANITIZE_FLAGS $INCLUDES $DEFINES"
CXXFLAGS="$CXXFLAGS -include $ROOT/sim/shim/esp_log.h -include $ROOT/sim/shim/esp_system.h"
CXXFLAGS="$CXXFLAGS -include $ROOT/sim/shim/esp_heap_caps.h"
# The production connection stack declares FreeRTOS queue, task and tick types
# in its headers and picks them up transitively from ESP-IDF on device. Force
# include the host shim so the same headers compile unchanged here.
CXXFLAGS="$CXXFLAGS -include $ROOT/sim/shim/freertos/FreeRTOS.h"
# glibc hides strnlen and other POSIX names under strict -std=c11, which
# breaks the LVGL clib build on Linux. _DEFAULT_SOURCE restores them and is
# inert on macOS.
CFLAGS="-std=c11 -D_DEFAULT_SOURCE -O0 -g -Wall -Wextra $SANITIZE_FLAGS $INCLUDES $DEFINES"

OBJECTS=

dependency_is_current() {
  object=$1
  depfile=$2
  [ -f "$object" ] && [ -f "$depfile" ] || return 1
  # Depfiles created before recipe-backed checks were introduced are treated
  # as a one-time cache miss so they are upgraded safely.
  grep -q '^[[:space:]]*@:$' "$depfile" || return 1

  # The compiler-generated file is a make rule containing the complete
  # project-header closure. BSD make and GNU make both implement -q, so this
  # checks the rule without duplicating make's escaping and path handling in
  # shell code. A missing or newer prerequisite makes the object stale.
  make -q -f "$depfile" "$object" >/dev/null 2>&1
}

write_depfile_recipe() {
  depfile=$1
  target=$(sed -n '1s/:.*$//p' "$depfile")
  {
    printf '\n%s:\n' "$target"
    printf '\t@:\n'
  } >>"$depfile"
}

object_is_current() {
  object=$1
  depfile=$2
  config_is_newer=$3

  [ "$config_is_newer" = false ] || [ "$object" -nt "$ROOT/sim/lv_conf.h" ] || return 1
  dependency_is_current "$object" "$depfile"
}

compile_cpp() {
  source=$1
  name=$(printf '%s' "$source" | sed "s|$ROOT/||; s|[^A-Za-z0-9_]|_|g")
  object="$BUILD_DIR/obj/$name.o"
  depfile="$object.d"
  config_is_newer=true
  case "$source" in
    "$DEP_ROOT/M5GFX/"*|"$DEP_ROOT/M5Unified/"*) config_is_newer=false ;;
  esac
  if object_is_current "$object" "$depfile" "$config_is_newer"; then
    echo "[SKIP] ${source#$ROOT/}"
    OBJECTS="$OBJECTS $object"
    return
  fi
  echo "[CXX] ${source#$ROOT/}"
  # TinyGPSPlus ages readings against a global millis(). Suppress its host
  # wall-clock fallback so sim/clock.cpp can supply the virtual one, which is
  # what makes fix age deterministic. __AVR__ guards nothing else in that file.
  extra=""
  case "$source" in
    "$DEP_ROOT/TinyGPSPlus/"*) extra="-D__AVR__" ;;
  esac
  "$CXX" $CXXFLAGS $extra $(coverage_flags_for "$source") \
    -MMD -MP -MF "$depfile" -MT "$object" -c "$source" -o "$object"
  write_depfile_recipe "$depfile"
  OBJECTS="$OBJECTS $object"
}

compile_c() {
  source=$1
  name=$(printf '%s' "$source" | sed "s|$ROOT/||; s|[^A-Za-z0-9_]|_|g")
  object="$BUILD_DIR/obj/$name.o"
  depfile="$object.d"
  config_is_newer=true
  case "$source" in
    "$DEP_ROOT/M5GFX/"*|"$DEP_ROOT/M5Unified/"*) config_is_newer=false ;;
  esac
  if object_is_current "$object" "$depfile" "$config_is_newer"; then
    echo "[SKIP] ${source#$ROOT/}"
    OBJECTS="$OBJECTS $object"
    return
  fi
  echo "[C]   ${source#$ROOT/}"
  "$CC" $CFLAGS $(coverage_flags_for "$source") \
    -MMD -MP -MF "$depfile" -MT "$object" -c "$source" -o "$object"
  write_depfile_recipe "$depfile"
  OBJECTS="$OBJECTS $object"
}

for source in "$ROOT"/sim/*.cpp; do
  compile_cpp "$source"
done

for source in \
  "$ROOT/src/FurbleBootScreen.cpp" \
  "$ROOT/src/FurbleCalibrate.cpp" \
  "$ROOT/src/FurbleCompanionAuth.cpp" \
  "$ROOT/tests/host/companion/companion_hmac.cpp" \
  "$ROOT/src/FurbleCompanionService.cpp" \
  "$ROOT/src/FurbleControl.cpp" \
  "$ROOT/src/FurbleGPS.cpp" \
  "$ROOT/src/FurbleOTAMQTT.cpp" \
  "$ROOT/src/FurbleOTAPartitionSink.cpp" \
  "$ROOT/src/FurbleOTAReplayStore.cpp" \
  "$ROOT/src/FurblePower.cpp" \
  "$ROOT/src/FurbleProvision.cpp" \
  "$ROOT/src/FurbleSettings.cpp" \
  "$ROOT/src/FurbleSpinValue.cpp" \
  "$ROOT/src/FurbleTimeKeeper.cpp" \
  "$ROOT/src/FurbleTimeKeeperPolicy.cpp" \
  "$ROOT/src/FurbleUI.cpp" \
  "$ROOT/src/FurbleUIBulb.cpp" \
  "$ROOT/src/FurbleUIGesture.cpp" \
  "$ROOT/src/FurbleUIIntervalometer.cpp" \
  "$ROOT/lib/blowfish/Blowfish.cpp" \
  "$ROOT/lib/furble/BtDebugJournal.cpp" \
  "$ROOT/lib/furble/Camera.cpp" \
  "$ROOT/lib/furble/CameraList.cpp" \
  "$ROOT/lib/furble/CanonEOS.cpp" \
  "$ROOT/lib/furble/CanonEOSRemote.cpp" \
  "$ROOT/lib/furble/CanonEOSSmart.cpp" \
  "$ROOT/lib/furble/Device.cpp" \
  "$ROOT/lib/furble/DJIOsmo.cpp" \
  "$ROOT/lib/furble/FauxNY.cpp" \
  "$ROOT/lib/furble/Fujifilm.cpp" \
  "$ROOT/lib/furble/FujifilmBasic.cpp" \
  "$ROOT/lib/furble/FujifilmSecure.cpp" \
  "$ROOT/lib/furble/Lumix.cpp" \
  "$ROOT/lib/furble/Nikon.cpp" \
  "$ROOT/lib/furble/NikonBase.cpp" \
  "$ROOT/lib/furble/NikonRemote.cpp" \
  "$ROOT/lib/furble/NikonSmart.cpp" \
  "$ROOT/lib/furble/Ricoh.cpp" \
  "$ROOT/lib/furble/Scan.cpp" \
  "$ROOT/lib/furble/Sony.cpp" \
  "$ROOT/lib/furble/protocol/AdvertisementProtocol.cpp" \
  "$ROOT/lib/furble/protocol/CameraListProtocol.cpp" \
  "$ROOT/lib/furble/protocol/FujifilmProtocol.cpp" \
  "$ROOT/lib/furble/protocol/GpsCasic.cpp" \
  "$ROOT/lib/furble/protocol/ProvisionTLV.cpp" \
  "$ROOT/lib/testing/nimble/MockNimBLE.cpp" \
  "$ROOT/lib/testing/peer/FujifilmVirtualCamera.cpp" \
  "$ROOT/lib/testing/peer/RicohVirtualCamera.cpp" \
  "$DEP_ROOT/TinyGPSPlus/src/TinyGPS++.cpp"; do
  compile_cpp "$source"
done

while IFS= read -r source; do
  compile_c "$source"
done <<EOF
$(find "$ROOT/components/icons" -type f -name '*.c' -print)
EOF

while IFS= read -r source; do
  compile_c "$source"
done <<EOF
$(find "$LVGL_DIR/src" -type f -name '*.c' -print)
EOF

while IFS= read -r source; do
  compile_c "$source"
done <<EOF
$(find "$DEP_ROOT/M5GFX/src" -type f -name '*.c' -print)
EOF

while IFS= read -r source; do
  compile_cpp "$source"
done <<EOF
$(find "$DEP_ROOT/M5GFX/src" -type f -name '*.cpp' -print)
EOF

while IFS= read -r source; do
  compile_cpp "$source"
done <<EOF
$(find "$DEP_ROOT/M5Unified/src" -type f -name '*.cpp' -print)
EOF

echo "[LD]  sim/build/furble-sim"
# -rdynamic exports the executable's symbols into the dynamic table so the
# stall watchdog's backtraces name functions instead of raw addresses. A dump
# of a wedged run is the whole point of that watchdog.
LINK_FLAGS="-rdynamic -L/opt/homebrew/lib -L/usr/local/lib -lSDL2 -lpthread"
if [ "$(uname -s)" = "Darwin" ]; then
  LINK_FLAGS="$LINK_FLAGS -framework Cocoa"
fi

"$CXX" $CXXFLAGS $COVERAGE_LINK_FLAGS $OBJECTS -o "$BUILD_DIR/furble-sim" $LINK_FLAGS

echo "Built $BUILD_DIR/furble-sim"
