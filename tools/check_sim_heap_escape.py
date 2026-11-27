#!/usr/bin/env python3
"""Fail on a firmware heap call the simulator model cannot see.

Every firmware allocation the simulator charges goes through the heap model:
`heap_caps_*` in `sim/shim/esp_heap_caps.h`, the replaced `operator new` family
in `sim/heap_count.cpp`, and the PSRAM pool behind both. A bare `malloc()`,
`calloc()`, `realloc()`, `free()`, `strdup()` or `strndup()` call in the firmware
escapes that model: `sim/shim/esp_heap_caps.h` includes the C library headers,
so the host libc resolves the symbol, the call succeeds on the host heap, and no
gate would notice. The device would charge TLSF for the same call.

The same applies to `pvPortMalloc()` and `vPortFree()`, which reach
`heap_caps_malloc_default()` on the device and are not modelled here either.

This is a source-text gate. It strips comments, string literals and character
literals before matching, so prose and log text cannot trip it, and it ignores a
name that any identifier or member prefix belongs to (`heap_caps_free`,
`settings.free`, `freeBytes`). A member declaration named `free` is reported,
because that shape shadows the libc name and is worth a look in review. Findings
are printed as `file:line: call`.

Typical use:
  tools/check_sim_heap_escape.py
  tools/check_sim_heap_escape.py --root .
"""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]

# The firmware trees the simulator builds. lib/testing and lib/preferences are
# inside lib/, so a double compiled into the simulator is covered too.
SCANNED_ROOTS = ("src", "lib", "include")
SOURCE_SUFFIXES = frozenset({".c", ".cc", ".cpp", ".h", ".hpp"})

# Calls that reach the device heap without going through the modelled shim.
BARE_HEAP_CALLS = ("malloc", "calloc", "realloc", "free", "strdup", "strndup",
                   "pvPortMalloc", "vPortFree")

# A name that belongs to an identifier, a member or an arrow call is not this
# gate's business: `heap_caps_free`, `settings.free` and `holder->free` all keep
# their own meaning, and only `std::free` or a bare `free` reach libc.
CALL_PATTERN = re.compile(
    r"(?<![A-Za-z0-9_.>])(?P<call>" + "|".join(BARE_HEAP_CALLS) + r")\s*\("
)


def strip_comments_and_literals(text: str) -> str:
  """Replace comments and literals with spaces, keeping line structure."""
  out: list[str] = []
  index = 0
  length = len(text)
  state = None
  quote = ""
  while index < length:
    character = text[index]
    if state is None:
      if character == "/" and index + 1 < length and text[index + 1] == "/":
        state = "line"
        index += 2
        continue
      if character == "/" and index + 1 < length and text[index + 1] == "*":
        state = "block"
        index += 2
        continue
      if character in ("'", '"'):
        state = "literal"
        quote = character
        out.append(" ")
        index += 1
        continue
      out.append(character)
      index += 1
      continue
    if state == "line":
      if character == "\n":
        state = None
        out.append(character)
      index += 1
      continue
    if state == "block":
      if character == "*" and index + 1 < length and text[index + 1] == "/":
        state = None
        index += 2
        continue
      if character == "\n":
        out.append(character)
      index += 1
      continue
    # literal
    if character == "\\":
      index += 2
      continue
    if character == quote:
      state = None
    index += 1
  return "".join(out)


def source_files(root: Path) -> list[Path]:
  files: list[Path] = []
  for scanned in SCANNED_ROOTS:
    directory = root / scanned
    if not directory.is_dir():
      continue
    for path in sorted(directory.rglob("*")):
      if path.is_file() and path.suffix in SOURCE_SUFFIXES:
        files.append(path)
  return files


def find_bare_calls(root: Path, files: list[Path]) -> list[str]:
  findings: list[str] = []
  for path in files:
    try:
      text = path.read_text(encoding="utf-8", errors="replace")
    except OSError as error:
      findings.append(f"{path.relative_to(root)}: unreadable: {error}")
      continue
    stripped = strip_comments_and_literals(text)
    for number, line in enumerate(stripped.splitlines(), 1):
      for match in CALL_PATTERN.finditer(line):
        findings.append(f"{path.relative_to(root)}:{number}: {match.group('call')}()")
  return findings


def main(argv: list[str] | None = None) -> int:
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("--root", type=Path, default=ROOT, help="checkout root")
  args = parser.parse_args(argv)
  root = args.root.resolve()
  files = source_files(root)
  if not files:
    print(f"FAIL: no firmware sources under {root}", file=sys.stderr)
    return 2
  findings = find_bare_calls(root, files)
  if findings:
    print("bare heap calls escape the simulator heap model:", file=sys.stderr)
    for finding in findings:
      print(f"  {finding}", file=sys.stderr)
    print(
      "Use heap_caps_malloc/heap_caps_calloc/heap_caps_free, the replaced "
      "operator new family, or add the API to sim/shim/ with a model charge.",
      file=sys.stderr,
    )
    return 1
  print(f"simulator heap escape check passed for {len(files)} firmware source(s)")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
