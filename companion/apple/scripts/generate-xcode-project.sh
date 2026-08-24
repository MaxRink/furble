#!/bin/sh
set -eu

# XcodeGen is built from an immutable release tag so the project generated in
# CI is derived from the same schema and generator version on every run.
XCODEGEN_VERSION="2.42.0"
# The release tag is human-readable, but the commit is the reproducibility
# boundary. Keep the tag check below as a diagnostic if the release is moved.
XCODEGEN_COMMIT="82c6ab9bbd5b6075fc0887d897733fc0c4ffc9ab"
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
WORK=$(mktemp -d "${TMPDIR:-/tmp}/furble-xcodegen.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM

git clone --quiet --depth 1 --branch "$XCODEGEN_VERSION" \
  https://github.com/yonaskolb/XcodeGen.git "$WORK/XcodeGen"
test "$(git -C "$WORK/XcodeGen" describe --tags --exact-match HEAD)" = "$XCODEGEN_VERSION"
test "$(git -C "$WORK/XcodeGen" rev-parse HEAD)" = "$XCODEGEN_COMMIT"

swift run --package-path "$WORK/XcodeGen" --configuration release xcodegen \
  generate --spec "$ROOT/project.yml" --project "$ROOT/FurbleCompanion.xcodeproj"
