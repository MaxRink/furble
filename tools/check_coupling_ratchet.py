#!/usr/bin/env python3
"""Ratchet the board and platform coupling counts of the firmware sources.

The portability inventory guards the declared portable core. This checker
guards everything else: it counts, per file, the coupling a future port has to
undo, and compares the counts with the committed manifest
`tools/coupling_ratchet.json`.

Categories:
  board_guards   preprocessor conditionals naming a board identity macro.
                 FURBLE_NO_DISPLAY and FURBLE_ETHERNET are profile capability
                 flags mirrored in CMake, so they are not counted.
  m5_calls       `M5.` uses outside comments
  m5_get_board   `M5.getBoard()` runtime board switches
  header_types   ESP-IDF and NimBLE type names in public headers

A count above its manifest entry fails. A count below it also fails until the
manifest is regenerated, so every reduction is locked in. Regenerate with
`python3 tools/check_coupling_ratchet.py --update` and commit the result.

`include/FurbleBoardCaps.h` is where board identity is turned into capability
macros, so it is the one file allowed to name boards without a ratchet entry.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = Path("tools/coupling_ratchet.json")
SCAN_ROOTS = (Path("src"), Path("include"), Path("lib/furble"), Path("lib/blowfish"),
              Path("lib/preferences"))
HEADER_ROOTS = (Path("include"), Path("lib/furble"))
SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".h", ".hh", ".hpp"}
HEADER_SUFFIXES = {".h", ".hh", ".hpp"}
BOARD_CAPS_HEADER = "include/FurbleBoardCaps.h"

BOARD_MACROS = (
    "FURBLE_M5STICKC",
    "FURBLE_M5STICKC_PLUS",
    "FURBLE_M5STICKS3",
    "FURBLE_M5COREX",
    "FURBLE_M5STACK_CORE",
    "FURBLE_GROVE_CORE",
    "FURBLE_WAVESHARE_S3_ETH",
)
BOARD_GUARD = re.compile(
    r"^\s*#\s*(?:if|ifdef|ifndef|elif)\b.*\b(?:" + "|".join(BOARD_MACROS) + r")\b",
    re.MULTILINE,
)
M5_CALL = re.compile(r"\bM5\.")
M5_GET_BOARD = re.compile(r"\bM5\.getBoard\s*\(")
HEADER_TYPE = re.compile(
    r"\b(?:(?:esp|nvs|gpio|uart|i2c|rmt|ledc|adc|wifi|httpd)_\w*_t"
    r"|ble_\w+_t|BLE_HS_\w+|NimBLE\w+)\b"
)
# Comments and string literals are removed before counting. Newlines are kept
# so a preprocessor line stays on its own line.
STRIP = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\\n])*"', re.DOTALL)

CATEGORIES = ("board_guards", "m5_calls", "m5_get_board", "header_types")


def strip_comments(text: str) -> str:
  return STRIP.sub(lambda match: "\n" * match.group(0).count("\n"), text)


def files_under(root: Path, relatives: tuple[Path, ...], suffixes: set[str]) -> list[Path]:
  found: list[Path] = []
  for relative in relatives:
    base = root / relative
    if base.is_dir():
      found.extend(path for path in base.rglob("*")
                   if path.is_file() and path.suffix in suffixes)
  return sorted(found)


def count(root: Path) -> dict[str, dict[str, int]]:
  counts: dict[str, dict[str, int]] = {name: {} for name in CATEGORIES}

  def record(category: str, name: str, value: int) -> None:
    if value:
      counts[category][name] = value

  for path in files_under(root, SCAN_ROOTS, SOURCE_SUFFIXES):
    name = path.relative_to(root).as_posix()
    text = strip_comments(path.read_text(encoding="utf-8", errors="replace"))
    if name != BOARD_CAPS_HEADER:
      record("board_guards", name, len(BOARD_GUARD.findall(text)))
    record("m5_calls", name, len(M5_CALL.findall(text)))
    record("m5_get_board", name, len(M5_GET_BOARD.findall(text)))

  for path in files_under(root, HEADER_ROOTS, HEADER_SUFFIXES):
    name = path.relative_to(root).as_posix()
    text = strip_comments(path.read_text(encoding="utf-8", errors="replace"))
    record("header_types", name, len(HEADER_TYPE.findall(text)))
  return counts


def compare(manifest: dict[str, dict[str, int]],
            current: dict[str, dict[str, int]]) -> tuple[list[str], list[str]]:
  grown: list[str] = []
  stale: list[str] = []
  for category in CATEGORIES:
    expected = manifest.get(category, {})
    actual = current.get(category, {})
    for name in sorted(set(expected) | set(actual)):
      before = expected.get(name, 0)
      after = actual.get(name, 0)
      if after > before:
        grown.append(f"{category}: {name} grew from {before} to {after}")
      elif after < before:
        stale.append(f"{category}: {name} shrank from {before} to {after}")
  return grown, stale


def totals(counts: dict[str, dict[str, int]]) -> str:
  return ", ".join(
      f"{category} {sum(counts[category].values())} in {len(counts[category])} files"
      for category in CATEGORIES)


def main(argv: list[str] | None = None) -> int:
  parser = argparse.ArgumentParser(description=__doc__,
                                   formatter_class=argparse.RawDescriptionHelpFormatter)
  parser.add_argument("--root", type=Path, default=ROOT, help="repository root")
  parser.add_argument("--update", action="store_true",
                      help="rewrite the manifest from the current tree")
  args = parser.parse_args(argv)
  root = args.root.resolve()
  manifest_path = root / MANIFEST
  current = count(root)

  if args.update:
    manifest_path.write_text(json.dumps(current, indent=2, sort_keys=True) + "\n",
                             encoding="utf-8")
    print(f"wrote {MANIFEST}: {totals(current)}")
    return 0

  if not manifest_path.is_file():
    print(f"FAIL: missing {MANIFEST}; run with --update", file=sys.stderr)
    return 1
  try:
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
  except ValueError as error:
    print(f"FAIL: {MANIFEST} is not valid JSON: {error}", file=sys.stderr)
    return 1

  grown, stale = compare(manifest, current)
  print(f"coupling: {totals(current)}")
  for line in grown:
    print(f"GREW  {line}", file=sys.stderr)
  for line in stale:
    print(f"STALE {line}", file=sys.stderr)
  if grown:
    print("FAIL: coupling grew. Use a capability macro from include/FurbleBoardCaps.h "
          "or a Platform function instead.", file=sys.stderr)
    return 1
  if stale:
    print(f"FAIL: coupling shrank. Lock it in with: python3 tools/check_coupling_ratchet.py "
          f"--update", file=sys.stderr)
    return 1
  print("coupling ratchet: clean")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
