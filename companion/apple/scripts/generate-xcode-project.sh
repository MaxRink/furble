#!/bin/sh
set -eu

# XcodeGen is built from an immutable release tag so the project generated in
# CI is derived from the same schema and generator version on every run.
XCODEGEN_VERSION="2.42.0"
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
WORK=$(mktemp -d "${TMPDIR:-/tmp}/furble-xcodegen.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM

git clone --quiet --depth 1 --branch "$XCODEGEN_VERSION" \
  https://github.com/yonaskolb/XcodeGen.git "$WORK/XcodeGen"
test "$(git -C "$WORK/XcodeGen" describe --tags --exact-match HEAD)" = "$XCODEGEN_VERSION"

swift run --package-path "$WORK/XcodeGen" --configuration release xcodegen \
  generate --spec "$ROOT/project.yml" --project "$ROOT/FurbleCompanion.xcodeproj"
