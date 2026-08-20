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

CXXFLAGS="-std=c++17 -O0 -g -Wall -Wextra -Wno-unused-parameter $SANITIZE_FLAGS $INCLUDES $DEFINES"
CXXFLAGS="$CXXFLAGS -include $ROOT/sim/shim/esp_log.h -include $ROOT/sim/shim/esp_system.h"
CXXFLAGS="$CXXFLAGS -include $ROOT/sim/shim/esp_heap_caps.h"
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
  "$CXX" $CXXFLAGS -MMD -MP -MF "$depfile" -MT "$object" -c "$source" -o "$object"
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
  "$CC" $CFLAGS -MMD -MP -MF "$depfile" -MT "$object" -c "$source" -o "$object"
  write_depfile_recipe "$depfile"
  OBJECTS="$OBJECTS $object"
}

for source in "$ROOT"/sim/*.cpp; do
  compile_cpp "$source"
done

for source in \
  "$ROOT/src/FurbleBootScreen.cpp" \
  "$ROOT/src/FurbleCalibrate.cpp" \
  "$ROOT/src/FurbleCompanionService.cpp" \
  "$ROOT/src/FurbleGPS.cpp" \
  "$ROOT/src/FurbleOTAMQTT.cpp" \
  "$ROOT/src/FurbleOTAPartitionSink.cpp" \
  "$ROOT/src/FurbleOTAReplayStore.cpp" \
  "$ROOT/src/FurbleIMU.cpp" \
  "$ROOT/src/FurblePower.cpp" \
  "$ROOT/src/FurbleProvision.cpp" \
  "$ROOT/src/FurbleSettings.cpp" \
  "$ROOT/src/FurbleSpinValue.cpp" \
  "$ROOT/src/FurbleTimeKeeper.cpp" \
  "$ROOT/src/FurbleTimeKeeperPolicy.cpp" \
  "$ROOT/src/FurbleUI.cpp" \
  "$ROOT/src/FurbleUIBulb.cpp" \
  "$ROOT/src/FurbleUIIntervalometer.cpp" \
  "$ROOT/lib/furble/protocol/ProvisionTLV.cpp" \
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
LINK_FLAGS="-L/opt/homebrew/lib -L/usr/local/lib -lSDL2 -lpthread"
if [ "$(uname -s)" = "Darwin" ]; then
  LINK_FLAGS="$LINK_FLAGS -framework Cocoa"
fi

"$CXX" $CXXFLAGS $OBJECTS -o "$BUILD_DIR/furble-sim" $LINK_FLAGS

echo "Built $BUILD_DIR/furble-sim"
