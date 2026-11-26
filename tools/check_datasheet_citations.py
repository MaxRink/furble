#!/usr/bin/env python3
"""Fail when a lib/hal driver names a register, command or timing without a
datasheet citation nearby.

`lib/hal/CLAUDE.md` requires a datasheet title, revision and section next to
every register, bit and command. The document identity lives in the driver file
header. This gate enforces the section half for the token families that carry
register addresses, command bytes, chip identity and timings, and it names the
`docs/components/` page that must hold the citation when the driver has none.

Rules:

1. A token is a numeric definition whose name starts with a register, command or
   identity prefix (`REG_`, `CMD_`, `OPCODE_`, `ADDRESS`, `CHIP_ID`) or ends with
   a timing suffix (`_US`, `_MS`). The gate reads `constexpr`, `static const` and
   `const` definitions and `#define` macros, in every file under `lib/hal`,
   recursively, with the C and C++ suffixes the format gate formats. A
   definition counts when it initialises with an integer literal: bare, in
   parentheses, in braces, or one of several declarators on the line, and with
   any integer suffix. A raw hex literal passed in the register position of a
   bus call is a token too.
2. A token is cited when a comment holds a well formed reference: a dotted
   section (`section 6.13.2.30`), a plain section (`section 3`), a table
   (`Table 2-6`, `Table A-1`), or a section sign. A fragment such as
   `section 6x` or `Table Item` is not a reference and does not count. A cited
   comment covers the code run it heads, up to `COVER_MAX_LINES` lines, so one
   datasheet line over a register table covers the table without one heading
   covering a whole file. A trailing `//` comment on the token line counts, and
   only a `//` outside a string or character literal starts one. A definition
   that sits inside a string is not a token. Bit masks written `1U << n` and
   switch labels are not tokens: the register's citation covers the bit, and
   the host test states the bit numbers from the datasheet.
3. A token without a citation is accepted only when the component page lists it
   in the "Citation gaps" table. That table must carry the documented four
   columns on every page under `docs/components/`, whether a driver maps to the
   page or not. A malformed table is an error, not a silent pass. Every other
   case fails and the message names the page.

Usage:
  python3 tools/check_datasheet_citations.py
  python3 tools/check_datasheet_citations.py --root . --verbose

Exit status is 0 when every token is cited or listed as a page gap, 1 on a
violation, and 2 for an invalid invocation.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
DRIVER_DIR = Path("lib/hal")
PAGE_DIR = Path("docs/components")
CITATION_WINDOW = 6
COVER_MAX_LINES = 16
GAP_COLUMNS = ["constant or fact", "driver location", "why no section", "page row"]

# Driver stem to component page. A stem with no entry maps to <stem>.md in
# lower case, so a new driver lands on a predictable page path.
PAGE_BY_STEM = {
  "AXP2101": "axp2101.md",
  "BQ27220": "bq27220.md",
  "HalBus": "README.md",
  "SX1262": "sx1262.md",
  "XL9555": "xl9555.md",
}

# C and C++ suffixes, kept in step with the format gate: a driver file the
# format gate formats is a driver file this gate scans.
SUFFIXES = (".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx")

# An integer literal with any C++ suffix (U, L, UL, ULL, Z) and any base.
INT_LITERAL = r"(?:0[xX][0-9A-Fa-f]+|0[bB][01]+|\d+)[uUlLzZ]*"
# The same literal bare or in parentheses, which is how a definition may wrap it.
PAREN_LITERAL = rf"\(\s*{INT_LITERAL}\s*\)|{INT_LITERAL}"
# The initialiser: `= 0x10`, `= (0x10)`, `{0x10}` or `= {0x10}`.
INITIALISER = rf"(?:=\s*)?\{{?\s*(?:{PAREN_LITERAL})\s*\}}?"
# A declarator ends the value or hands over to the next one, so a shift or an
# or of two names after the value is not a definition of this name.
DECLARATOR = re.compile(rf"\b([A-Za-z_]\w*)\s*{INITIALISER}(?=\s*(?:,|$))")
# `constexpr` and `const` open a statement that may declare several names.
QUALIFIER = re.compile(r"\bconstexpr\b|\bconst\b")
DEFINE = re.compile(
  rf"^\s*#\s*define\s+([A-Za-z_]\w*)\s+\(?\s*{INT_LITERAL}\s*\)?"
  r"(?=\s*(?:$|[,;)]|/\*))")
BUS_CALL = re.compile(r"\.(?:read8|write8|read|write|update8)\(\s*(0x[0-9A-Fa-f]+)")
COMMENT = re.compile(r"^\s*(?://|\*|/\*)")
GAPS_HEADING = re.compile(r"(?im)^#{1,6}\s+citation gaps\s*$")
NEXT_HEADING = re.compile(r"(?m)^#{1,6}\s+\S")
HEADER_SEPARATOR = re.compile(r"^\s*\|(?:\s*:?-{3,}:?\s*\|)+\s*$")
TOKEN_CELL = re.compile(r"`([^`]+)`")

# A reference is a structured identifier. A bare word after "section" or
# "table", or a digit followed by more digits or letters, is not one.
CITATION_PATTERNS = (
  r"\bsections?\s+\d{1,3}(?:\.\d{1,3}){1,3}\b",
  r"\bsections?\s+\d{1,3}\b",
  r"\btables?\s+\d{1,3}(?:-\d{1,3})?\b",
  r"\btables?\s+[A-Z]-\d{1,3}\b",
  r"\u00a7\s*\d{1,3}(?:\.\d{1,3}){0,3}\b",
)
CITATION = re.compile("|".join(CITATION_PATTERNS), re.IGNORECASE)
FAMILY_PREFIXES = ("REG_", "CMD_", "OPCODE_", "ADDRESS", "CHIP_ID")
FAMILY_SUFFIXES = ("_US", "_MS")


class Token:
  def __init__(self, path: Path, line: int, name: str) -> None:
    self.path = path
    self.line = line
    self.name = name

  def __str__(self) -> str:
    return f"{self.path.as_posix()}:{self.line}: {self.name}"


def page_for(path: Path) -> Path:
  stem = path.stem
  return PAGE_DIR / PAGE_BY_STEM.get(stem, stem.lower() + ".md")


def parse_gap_table(text: str) -> tuple[list[str], list[str]]:
  """Return the names in the Citation gaps table and its structural errors."""
  names: list[str] = []
  errors: list[str] = []
  match = GAPS_HEADING.search(text)
  if match is None:
    return names, errors
  rest = text[match.end():]
  following = NEXT_HEADING.search(rest)
  body = rest[:following.start()] if following else rest
  rows = [line.strip() for line in body.splitlines() if line.strip().startswith("|")]
  if not rows:
    return names, errors
  header = [cell.strip().lower() for cell in rows[0].strip("|").split("|")]
  if header != GAP_COLUMNS:
    return names, [
      "Citation gaps table header must be: " + " | ".join(GAP_COLUMNS)]
  for row in rows[1:]:
    if HEADER_SEPARATOR.match(row):
      continue
    cells = [cell.strip() for cell in row.strip("|").split("|")]
    if len(cells) != len(GAP_COLUMNS) or any(not cell for cell in cells):
      errors.append(
        "Citation gaps row must have four non-empty columns: " + row[:77])
      continue
    for token in TOKEN_CELL.findall(cells[0]):
      names.append(token.strip())
  return names, errors


def gap_names(page: Path, root: Path) -> tuple[set[str], list[str]]:
  """Names listed in the page's Citation gaps table, with table errors."""
  full = root / page
  if not full.is_file():
    return set(), []
  names, errors = parse_gap_table(full.read_text(encoding="utf-8"))
  return set(names), [f"{page.as_posix()}: {error}" for error in errors]


def is_citation(text: str) -> bool:
  return CITATION.search(text) is not None


def covered_lines(lines: list[str]) -> set[int]:
  """One-based line numbers a datasheet citation speaks for."""
  cited: set[int] = set()
  total = len(lines)
  index = 0
  while index < total:
    if not (COMMENT.match(lines[index]) and is_citation(lines[index])):
      index += 1
      continue
    line = index + 1
    end = index
    while end + 1 < total and COMMENT.match(lines[end + 1]):
      end += 1
    for near in range(max(1, line - CITATION_WINDOW), line + CITATION_WINDOW + 1):
      cited.add(near)
    block = end + 1
    covered = 0
    while (block < total and covered < COVER_MAX_LINES
           and lines[block].strip() and not COMMENT.match(lines[block])):
      cited.add(block + 1)
      covered += 1
      block += 1
    index = end + 1
  return cited


def split_line(line: str) -> tuple[str, str]:
  """Return the code half of a line and its trailing `//` comment.

  Only a `//` outside a string or character literal opens a comment, and the
  literal bodies are blanked in the code half. A URL in a string therefore
  cannot look like a comment, and a definition inside a string is not a token.
  """
  code: list[str] = []
  index = 0
  quote = ""
  while index < len(line):
    char = line[index]
    if quote:
      if char == "\\" and index + 1 < len(line):
        code.extend("  ")
        index += 2
        continue
      if char == quote:
        quote = ""
        code.append(char)
      else:
        code.append(" ")
      index += 1
      continue
    if char in "\"'":
      quote = char
      code.append(char)
      index += 1
      continue
    if line.startswith("//", index):
      return "".join(code), line[index + 2:]
    code.append(char)
    index += 1
  return "".join(code), ""


def declared_names(code: str) -> list[str]:
  """Names a `constexpr` or `const` statement initialises with an integer.

  A statement may declare several, so every declarator counts, not the first.
  """
  names: list[str] = []
  for statement in code.split(";"):
    if QUALIFIER.search(statement) is None:
      continue
    names.extend(match.group(1) for match in DECLARATOR.finditer(statement))
  return names


def tokens_in(path: Path, root: Path) -> list[Token]:
  lines = (root / path).read_text(encoding="utf-8").splitlines()
  found: list[Token] = []
  for index, line in enumerate(lines):
    code, _ = split_line(line)
    names = declared_names(code)
    names.extend(match.group(1) for match in DEFINE.finditer(code))
    for literal in BUS_CALL.findall(code):
      found.append(Token(path, index + 1, literal))
    for name in names:
      if name.startswith(FAMILY_PREFIXES) or name.endswith(FAMILY_SUFFIXES):
        found.append(Token(path, index + 1, name))
  return found


def driver_files(root: Path) -> list[Path]:
  base = root / DRIVER_DIR
  if not base.is_dir():
    return []
  return sorted(
    candidate.relative_to(root)
    for candidate in base.rglob("*")
    if candidate.is_file() and candidate.suffix in SUFFIXES
  )


def component_pages(root: Path) -> list[Path]:
  base = root / PAGE_DIR
  if not base.is_dir():
    return []
  return sorted(candidate.relative_to(root) for candidate in base.glob("*.md"))


def inline_comment(line: str) -> str:
  return split_line(line)[1]


def check(root: Path, verbose: bool = False) -> tuple[list[str], int, int, int]:
  errors: list[str] = []
  cited = 0
  tolerated = 0
  checked = 0
  # Every page's gap table is validated, driver or not, so a page that no
  # driver maps to cannot carry a malformed table unnoticed.
  gaps_by_page: dict[Path, set[str]] = {}
  for page in component_pages(root):
    gaps_by_page[page], page_errors = gap_names(page, root)
    errors.extend(page_errors)
  for relative_path in driver_files(root):
    page = page_for(relative_path)
    if not (root / page).is_file():
      errors.append(
        f"{relative_path.as_posix()}: no component page at "
        f"{(root / page).as_posix()}; add it under docs/components/")
    gaps = gaps_by_page.get(page, set())
    lines = (root / relative_path).read_text(encoding="utf-8").splitlines()
    cited_lines = covered_lines(lines)
    for token in tokens_in(relative_path, root):
      checked += 1
      if token.line in cited_lines or is_citation(inline_comment(lines[token.line - 1])):
        cited += 1
        if verbose:
          print(f"cited    {token}")
        continue
      if token.name in gaps:
        tolerated += 1
        print(f"gap      {token} -> listed in {page.as_posix()}")
        continue
      errors.append(
        f"{token}: no datasheet section within {CITATION_WINDOW} lines "
        f"and no row in the Citation gaps table of {page.as_posix()}")
  return errors, checked, cited, tolerated


def main(argv: list[str] | None = None) -> int:
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("--root", type=Path, default=ROOT,
                      help="checkout root (default: this repository)")
  parser.add_argument("--verbose", action="store_true",
                      help="print every cited token")
  args = parser.parse_args(argv)

  root = args.root.resolve()
  if not (root / DRIVER_DIR).is_dir():
    print(f"FAIL: {root} has no {DRIVER_DIR.as_posix()} directory", file=sys.stderr)
    return 2

  errors, checked, cited, tolerated = check(root, args.verbose)
  for error in errors:
    print(f"FAIL: {error}", file=sys.stderr)
  print(f"datasheet citations: {cited} cited, {tolerated} page gaps, "
        f"{len(errors)} errors of {checked} tokens")
  return 1 if errors else 0


if __name__ == "__main__":
  raise SystemExit(main())
