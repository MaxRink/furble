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

DEFINES="\
-DFURBLE_M5STICKS3 \
-DFURBLE_SIM \
-DFURBLE_VERSION=\"sim\" \
-DFURBLE_TEST_VERSION=1 \
-DFURBLE_BATTERY_DEBUG=0 \
-DM5GFX_SCALE=2 \
-DM5GFX_BOARD=board_M5StickS3 \
-DLV_CONF_INCLUDE_SIMPLE \
-DLV_LVGL_H_INCLUDE_SIMPLE \
-DLV_KCONFIG_IGNORE \
-D_THREAD_SAFE"

CXXFLAGS="-std=c++17 -O0 -g -Wall -Wextra -Wno-unused-parameter $INCLUDES $DEFINES"
CXXFLAGS="$CXXFLAGS -include $ROOT/sim/shim/esp_log.h -include $ROOT/sim/shim/esp_system.h"
CXXFLAGS="$CXXFLAGS -include $ROOT/sim/shim/esp_heap_caps.h"
CFLAGS="-std=c11 -O0 -g -Wall -Wextra $INCLUDES $DEFINES"

OBJECTS=

compile_cpp() {
  source=$1
  name=$(printf '%s' "$source" | sed "s|$ROOT/||; s|[^A-Za-z0-9_]|_|g")
  object="$BUILD_DIR/obj/$name.o"
  config_is_newer=true
  case "$source" in
    "$DEP_ROOT/M5GFX/"*|"$DEP_ROOT/M5Unified/"*) config_is_newer=false ;;
  esac
  if [ -f "$object" ] && [ "$object" -nt "$source" ] &&
    { [ "$config_is_newer" = false ] || [ "$object" -nt "$ROOT/sim/lv_conf.h" ]; } &&
    [ -z "$(find "$ROOT/sim/shim" -type f -newer "$object" -print -quit)" ]; then
    echo "[SKIP] ${source#$ROOT/}"
    OBJECTS="$OBJECTS $object"
    return
  fi
  echo "[CXX] ${source#$ROOT/}"
  "$CXX" $CXXFLAGS -c "$source" -o "$object"
  OBJECTS="$OBJECTS $object"
}

compile_c() {
  source=$1
  name=$(printf '%s' "$source" | sed "s|$ROOT/||; s|[^A-Za-z0-9_]|_|g")
  object="$BUILD_DIR/obj/$name.o"
  config_is_newer=true
  case "$source" in
    "$DEP_ROOT/M5GFX/"*|"$DEP_ROOT/M5Unified/"*) config_is_newer=false ;;
  esac
  if [ -f "$object" ] && [ "$object" -nt "$source" ] &&
    { [ "$config_is_newer" = false ] || [ "$object" -nt "$ROOT/sim/lv_conf.h" ]; } &&
    [ -z "$(find "$ROOT/sim/shim" -type f -newer "$object" -print -quit)" ]; then
    echo "[SKIP] ${source#$ROOT/}"
    OBJECTS="$OBJECTS $object"
    return
  fi
  echo "[C]   ${source#$ROOT/}"
  "$CC" $CFLAGS -c "$source" -o "$object"
  OBJECTS="$OBJECTS $object"
}

for source in "$ROOT"/sim/*.cpp; do
  compile_cpp "$source"
done

for source in \
  "$ROOT/src/FurbleCalibrate.cpp" \
  "$ROOT/src/FurbleGPS.cpp" \
  "$ROOT/src/FurblePower.cpp" \
  "$ROOT/src/FurbleSettings.cpp" \
  "$ROOT/src/FurbleSpinValue.cpp" \
  "$ROOT/src/FurbleUI.cpp" \
  "$ROOT/src/FurbleUIBulb.cpp" \
  "$ROOT/src/FurbleUIIntervalometer.cpp" \
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
