#!/bin/sh

# Build and run the host test suite under AddressSanitizer and
# UndefinedBehaviorSanitizer in a dedicated build tree. The script is
# fail-closed: a missing compiler, a compiler without both sanitizers, an
# unmatched test regex, or any sanitizer failure returns non-zero.
#
# The normal host build tree is never reused or modified. The default build
# directory is .pio/host-sanitized, a separate ignored tree.
#
# Usage:
#   tools/run_sanitized_host_tests.sh
#   tools/run_sanitized_host_tests.sh '^gps-casic$'
#   tools/run_sanitized_host_tests.sh --clean --jobs 4 -R '^(gps|control)-'
#   tools/run_sanitized_host_tests.sh --list
#
# Environment:
#   CC, CXX            compiler commands (default: cc and c++)
#   ASAN_OPTIONS       overrides the default fail-closed ASan options
#   UBSAN_OPTIONS      overrides the default fail-closed UBSan options

set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
SOURCE_DIR="$ROOT/tests/host"
BUILD_DIR="$ROOT/.pio/host-sanitized"
BUILD_TYPE="Debug"
JOBS=""
TEST_REGEX='^(gps-casic|gps-power-cycle|preferences-sim-)'
CLEAN=0
LIST_TESTS=0
CC=${CC:-cc}
CXX=${CXX:-c++}

usage() {
  sed -n '3,20p' "$0" | sed 's/^# \{0,1\}//'
}

die() {
  echo "FAIL: $*" >&2
  exit 1
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --build-dir)
      [ "$#" -ge 2 ] || die "--build-dir needs a path"
      BUILD_DIR=$2
      shift 2
      ;;
    --build-dir=*)
      BUILD_DIR=${1#*=}
      shift
      ;;
    --source)
      [ "$#" -ge 2 ] || die "--source needs a path"
      SOURCE_DIR=$2
      shift 2
      ;;
    --source=*)
      SOURCE_DIR=${1#*=}
      shift
      ;;
    --build-type)
      [ "$#" -ge 2 ] || die "--build-type needs a value"
      BUILD_TYPE=$2
      shift 2
      ;;
    --build-type=*)
      BUILD_TYPE=${1#*=}
      shift
      ;;
    --jobs)
      [ "$#" -ge 2 ] || die "--jobs needs a number"
      JOBS=$2
      shift 2
      ;;
    --jobs=*)
      JOBS=${1#*=}
      shift
      ;;
    --clean)
      CLEAN=1
      shift
      ;;
    --list)
      LIST_TESTS=1
      shift
      ;;
    -R|--test)
      [ "$#" -ge 2 ] || die "$1 needs a regular expression"
      TEST_REGEX=$2
      shift 2
      ;;
    --test=*)
      TEST_REGEX=${1#*=}
      shift
      ;;
    --cc)
      [ "$#" -ge 2 ] || die "--cc needs a command"
      CC=$2
      shift 2
      ;;
    --cxx)
      [ "$#" -ge 2 ] || die "--cxx needs a command"
      CXX=$2
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    --)
      shift
      break
      ;;
    -*)
      die "unknown option: $1 (use --help)"
      ;;
    *)
      TEST_REGEX=$1
      shift
      ;;
  esac
done

if [ "$#" -gt 0 ]; then
  die "unexpected argument: $1"
fi

command -v cmake >/dev/null 2>&1 || die "cmake is required"
command -v ctest >/dev/null 2>&1 || die "ctest is required"
command -v python3 >/dev/null 2>&1 || die "python3 is required to map tests to targets"
command -v "$CC" >/dev/null 2>&1 || die "C compiler not found: $CC"
command -v "$CXX" >/dev/null 2>&1 || die "C++ compiler not found: $CXX"
[ -f "$SOURCE_DIR/CMakeLists.txt" ] || die "host CMakeLists.txt not found under $SOURCE_DIR"

case "$BUILD_DIR" in
  /*) ;;
  *) BUILD_DIR="$ROOT/$BUILD_DIR" ;;
esac

# Resolve symlinks, trailing slashes, "." and ".." before comparing. A purely
# lexical check misses --build-dir ., --build-dir=, and a parent path.
ROOT_REAL=$(python3 -c 'import os,sys; print(os.path.realpath(sys.argv[1]))' "$ROOT") \
  || die "could not resolve the repository root"
SOURCE_REAL=$(python3 -c 'import os,sys; print(os.path.realpath(sys.argv[1]))' "$SOURCE_DIR") \
  || die "could not resolve the host source directory"
BUILD_REAL=$(python3 -c 'import os,sys; print(os.path.realpath(sys.argv[1]))' "$BUILD_DIR") \
  || die "could not resolve the sanitizer build directory"

case "$BUILD_REAL" in
  ""|"/"|"$ROOT_REAL"|"$ROOT_REAL/.pio"|"$ROOT_REAL/.pio/build"|"$ROOT_REAL/build"|"$ROOT_REAL/build/host"|"$ROOT_REAL/tests/host"|"$SOURCE_REAL"|"$SOURCE_REAL"/*)
    die "refusing to use the normal or source tree as the sanitizer build dir: $BUILD_REAL"
    ;;
esac
case "$ROOT_REAL" in
  "$BUILD_REAL"/*)
    die "refusing a sanitizer build dir that contains the repository: $BUILD_REAL"
    ;;
esac
BUILD_DIR=$BUILD_REAL

if [ -z "$JOBS" ]; then
  JOBS=$(getconf _NPROCESSORS_ONLN 2>/dev/null || true)
  [ -n "$JOBS" ] || JOBS=$(sysctl -n hw.ncpu 2>/dev/null || true)
  [ -n "$JOBS" ] || JOBS=2
fi

SANITIZER_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"

PROBE_DIR=$(mktemp -d "${TMPDIR:-/tmp}/furble-sanitizer-probe.XXXXXX") \
  || die "cannot create a temporary directory"
cleanup() {
  rm -rf "$PROBE_DIR"
}
trap cleanup EXIT INT TERM

cat >"$PROBE_DIR/probe.cpp" <<'PROBE'
#include <cstdint>
int main() {
  std::uint32_t *values = new std::uint32_t[2];
  values[0] = 1;
  values[1] = 2;
  const std::uint32_t total = values[0] + values[1];
  delete[] values;
  return total == 3 ? 0 : 1;
}
PROBE

# Fail closed before touching any build tree. A compiler that cannot compile,
# link, and run the sanitizer probe is not usable for this gate.
if ! "$CXX" $SANITIZER_FLAGS "$PROBE_DIR/probe.cpp" -o "$PROBE_DIR/probe" \
    >"$PROBE_DIR/build.log" 2>&1; then
  cat "$PROBE_DIR/build.log" >&2
  die "$CXX does not support AddressSanitizer+UndefinedBehaviorSanitizer"
fi
if ! ASAN_OPTIONS=detect_leaks=0 "$PROBE_DIR/probe" \
    >"$PROBE_DIR/run.log" 2>&1; then
  cat "$PROBE_DIR/run.log" >&2
  die "the $CXX sanitizer probe did not run successfully"
fi

if [ "$CLEAN" -eq 1 ]; then
  [ -d "$BUILD_DIR" ] && rm -rf "$BUILD_DIR"
fi

# A cache from the plain host build, or from a different source tree, must not
# leak uninstrumented objects into this run.
if [ -f "$BUILD_DIR/CMakeCache.txt" ]; then
  CACHED_SOURCE=$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$BUILD_DIR/CMakeCache.txt")
  if [ -n "$CACHED_SOURCE" ] && [ "$CACHED_SOURCE" != "$(CDPATH= cd -- "$SOURCE_DIR" && pwd)" ]; then
    echo "removing build dir configured for a different source tree: $BUILD_DIR"
    rm -rf "$BUILD_DIR"
  elif ! grep -Fq -- "-fsanitize=address,undefined" "$BUILD_DIR/CMakeCache.txt"; then
    echo "removing build dir without sanitizer flags: $BUILD_DIR"
    rm -rf "$BUILD_DIR"
  fi
fi

echo "configuring sanitized host tests"
echo "  source: $SOURCE_DIR"
echo "  build:  $BUILD_DIR"
echo "  cxx:    $CXX"
echo "  flags:  $SANITIZER_FLAGS"

cmake -S "$SOURCE_DIR" -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DCMAKE_C_COMPILER="$CC" \
  -DCMAKE_CXX_COMPILER="$CXX" \
  -DCMAKE_C_FLAGS="$SANITIZER_FLAGS" \
  -DCMAKE_CXX_FLAGS="$SANITIZER_FLAGS" \
  -DCMAKE_EXE_LINKER_FLAGS="$SANITIZER_FLAGS" \
  || die "cmake configure failed"

if [ "$LIST_TESTS" -eq 1 ]; then
  ctest --test-dir "$BUILD_DIR" -N || die "ctest could not list tests"
  echo "sanitized host test list: PASS"
  exit 0
fi

echo "building sanitized host tests with $JOBS job(s)"

# Build only the targets that back the selected tests. Building every target
# would also build the ThreadSanitizer probes, and TSAN cannot be combined with
# ASan in one compiler invocation. A selected script-only test contributes no
# target and runs unchanged.
SELECTED_NAMES=$(ctest --test-dir "$BUILD_DIR" -N -R "$TEST_REGEX" 2>/dev/null \
  | sed -n 's/^[[:space:]]*Test[[:space:]]*#[0-9][0-9]*:[[:space:]]*//p')
[ -n "$SELECTED_NAMES" ] || die "no tests match regex: $TEST_REGEX"
if printf '%s\n' "$SELECTED_NAMES" \
    | grep -Eq '^(control-connect-camera-race|control-connecting-snapshot)$'; then
  die "ThreadSanitizer tests cannot run under ASan+UBSan; use tests/host/run_tsan_race.sh"
fi
export SELECTED_NAMES

TARGET_CANDIDATES=$(BUILD_DIR="$BUILD_DIR" python3 -c '
import os
import re
from pathlib import Path

names = {line.strip() for line in os.environ.get("SELECTED_NAMES", "").splitlines()
         if line.strip()}
targets = []
for test_file in Path(os.environ["BUILD_DIR"]).rglob("CTestTestfile.cmake"):
  try:
    lines = test_file.read_text(encoding="utf-8", errors="replace").splitlines()
  except OSError:
    continue
  for line in lines:
    match = re.match(
        r"\s*add_test\(\s*(?:\[(=*)\[(.*?)\]\1\]|([^\s]+))\s+(.+)\)\s*$", line)
    if match is None:
      continue
    name = match.group(2) or match.group(3)
    if name not in names:
      continue
    arguments = match.group(4).strip()
    if arguments.startswith("\""):
      end = arguments.find("\"", 1)
      command = arguments[1:end] if end > 0 else ""
    else:
      command = arguments.split()[0] if arguments else ""
    target = os.path.basename(command)
    if target and target not in targets:
      targets.append(target)
print("\n".join(targets))
') || die "could not map selected tests to build targets"

AVAILABLE_TARGETS=$(cmake --build "$BUILD_DIR" --target help 2>/dev/null \
  | sed -n 's/^\.\.\. \([^ ][^ ]*\).*/\1/p')
BUILD_TARGETS=""
for target in $TARGET_CANDIDATES; do
  if printf '%s\n' "$AVAILABLE_TARGETS" | grep -Fxq "$target"; then
    BUILD_TARGETS="$BUILD_TARGETS $target"
  fi
done

if [ -n "$BUILD_TARGETS" ]; then
  # The leading space is intentional: word splitting passes each target.
  cmake --build "$BUILD_DIR" --target $BUILD_TARGETS --parallel "$JOBS" \
    || die "sanitized build failed"
else
  echo "no compiled targets selected; running script-only tests"
fi

LEAK_OPTION=1
if [ "$(uname -s 2>/dev/null)" = "Darwin" ]; then
  # LeakSanitizer is not supported by the Apple sanitizer runtime.
  LEAK_OPTION=0
fi
ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=$LEAK_OPTION:halt_on_error=1:abort_on_error=1}
UBSAN_OPTIONS=${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1:abort_on_error=1}
export ASAN_OPTIONS UBSAN_OPTIONS

echo "running sanitized host tests matching: $TEST_REGEX"
if ! ctest --test-dir "$BUILD_DIR" --output-on-failure -R "$TEST_REGEX"; then
  die "sanitized host tests failed"
fi

echo "sanitized host tests: PASS"
