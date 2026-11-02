#!/usr/bin/env python3
"""Check settings defaults, documentation, board branches, and wire-id contracts.

The firmware settings table in ``src/FurbleSettings.cpp`` is the source of truth
for four contracts that otherwise drift silently:

* every exposed setting has a default in ``Settings::init``;
* every board-conditional default branch is compiled by a host test target;
* the documented defaults in ``docs/settings-and-controls.md`` and
  ``docs/wiki/Settings-Reference.md`` agree with the source;
* the companion wire-id ledger in ``include/CLAUDE.md`` matches the nonzero
  wire ids in the settings table.

A fifth check runs only with ``--diff-file`` or ``--base``: a default value
change must touch the README, both settings references, and a settings test.

The parsers are deliberately conservative. A C++ construct this scanner does
not understand produces a WARN naming the construct instead of a silent pass.

Known parser limits:

* Preprocessor expressions are approximated as a conjunction of the defines in
  the branch. An ``||`` expression is named in a WARN and may over-require test
  targets. An ``#else`` negates each condition in its chain.
* Only scalar default expressions are resolved. Aggregate initializers and
  local aggregate variables are named in a WARN and are not compared with docs.
* A case body with several ``save<>`` calls keeps the first default and warns.
* Documentation comparison is limited to ``DOC_DEFAULT_RULES``; other rows are
  only parsed structurally.
* CMake generator expressions and variable-expanded compile definitions are not
  evaluated, and coverage is a textual preprocessor-guard search rather than
  execution coverage.
* Diff mode sees tracked git changes. An untracked change needs ``--diff-file``.
  It only inspects ``save<>`` calls, so changing a constant in a header that a
  default expression uses is not detected as a default change.
* Diff mode checks that the required documentation and test files were touched,
  not that their new text describes the changed setting.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass, field
from pathlib import Path, PurePosixPath
import re
import shlex
import subprocess
import sys
from typing import Iterable


SETTINGS_SOURCE = "src/FurbleSettings.cpp"
SETTINGS_HEADER = "include/FurbleSettings.h"
TEXT_SIZE_HEADER = "include/FurbleTextSize.h"
LEDGER = "include/CLAUDE.md"
HOST_CMAKE = "tests/host/CMakeLists.txt"
README = "README.md"
SETTINGS_DOCS = (
    "docs/settings-and-controls.md",
    "docs/wiki/Settings-Reference.md",
)

# Settings that are allowed to have no save call in Settings::init.
SPECIAL_DEFAULTS = frozenset({"COMPANION_PASSWORD"})

# Documented default values for the settings whose defaults are most likely to
# change without a doc update. The aliases map the human wording used in the
# tables to the canonical value of the stored setting.
@dataclass(frozen=True)
class DocDefaultRule:
  symbol: str
  labels: tuple[str, ...]
  aliases: dict[str, str] = field(default_factory=dict)


DOC_DEFAULT_RULES = (
    DocDefaultRule("BRIGHTNESS", ("Brightness",)),
    DocDefaultRule(
        "INACTIVITY",
        ("Inactivity timeout", "Inactivity"),
        {
            "never": "0",
            "30 secs": "1",
            "60 secs": "2",
            "2 mins": "4",
            "5 mins": "10",
            "10 mins": "20",
        },
    ),
    DocDefaultRule(
        "DISPLAY_OFF",
        ("Screen off", "Display off"),
        {"dim": "0", "off": "1", "off remote on": "2"},
    ),
    DocDefaultRule(
        "SLEEP_CONN",
        ("Sleep while connected",),
        {"off": "false", "on": "true"},
    ),
    DocDefaultRule(
        "AUTO_OFF",
        ("Auto off", "Auto-off"),
        {
            "never": "0",
            "5 mins": "5",
            "10 mins": "10",
            "30 mins": "30",
            "60 mins": "60",
        },
    ),
    DocDefaultRule(
        "GPS_BAUD",
        ("GPS Baud", "GPS baud rate"),
        {"auto": "0", "9600": "9600", "115200": "115200"},
    ),
    DocDefaultRule(
        "GPS_NMEA",
        ("Sentences", "GPS Sentences", "NMEA Sentences"),
        {"default": "false", "rmc+gga": "true"},
    ),
    DocDefaultRule(
        "CPU_FREQ",
        ("CPU speed", "CPU frequency"),
        {"80 mhz": "80", "160 mhz": "160", "240 mhz": "240"},
    ),
)

REQUIRED_DEFAULT_DOCS = (README, *SETTINGS_DOCS)


@dataclass(frozen=True)
class Diagnostic:
  severity: str
  path: str
  line: int
  message: str

  def render(self) -> str:
    location = f"{self.path}:{self.line}" if self.line > 0 else self.path
    return f"{self.severity}: {location}: {self.message}"


@dataclass
class CheckReport:
  diagnostics: list[Diagnostic] = field(default_factory=list)
  stats: dict[str, object] = field(default_factory=dict)

  def fail(self, path: str, line: int, message: str) -> None:
    self.diagnostics.append(Diagnostic("FAIL", path, line, message))

  def warn(self, path: str, line: int, message: str) -> None:
    self.diagnostics.append(Diagnostic("WARN", path, line, message))

  @property
  def failures(self) -> list[Diagnostic]:
    return [item for item in self.diagnostics if item.severity == "FAIL"]

  @property
  def warnings(self) -> list[Diagnostic]:
    return [item for item in self.diagnostics if item.severity == "WARN"]

  @property
  def ok(self) -> bool:
    return not self.failures


# ---------------------------------------------------------------------------
# Text helpers
# ---------------------------------------------------------------------------


def _relative(path: Path, root: Path) -> str:
  try:
    return path.resolve().relative_to(root.resolve()).as_posix()
  except ValueError:
    return path.as_posix()


def _read_text(path: Path, root: Path, report: CheckReport) -> str:
  try:
    return path.read_text(encoding="utf-8")
  except OSError as error:
    report.fail(_relative(path, root), 0, f"cannot read required file: {error}")
    return ""


def _line_starts(text: str) -> list[int]:
  starts = [0]
  for match in re.finditer("\n", text):
    starts.append(match.end())
  return starts


def _line_for_offset(starts: list[int], offset: int) -> int:
  low = 0
  high = len(starts) - 1
  while low < high:
    middle = (low + high + 1) // 2
    if starts[middle] <= offset:
      low = middle
    else:
      high = middle - 1
  return low + 1


def _mask_comments(text: str) -> str:
  """Replace C/C++ comment contents with spaces while preserving offsets."""
  result = list(text)
  index = 0
  length = len(text)
  state = "code"
  while index < length:
    char = text[index]
    following = text[index + 1] if index + 1 < length else ""
    if state == "code":
      if char == "/" and following == "/":
        result[index] = " "
        result[index + 1] = " "
        index += 2
        state = "line"
      elif char == "/" and following == "*":
        result[index] = " "
        result[index + 1] = " "
        index += 2
        state = "block"
      elif char == '"':
        state = "string"
        index += 1
      elif char == "'":
        state = "char"
        index += 1
      else:
        index += 1
    elif state == "line":
      if char == "\n":
        state = "code"
      else:
        result[index] = " "
      index += 1
    elif state == "block":
      if char == "*" and following == "/":
        result[index] = " "
        result[index + 1] = " "
        index += 2
        state = "code"
      else:
        if char != "\n":
          result[index] = " "
        index += 1
    elif state in ("string", "char"):
      if char == "\\":
        index += 2
      elif (state == "string" and char == '"') or (state == "char" and char == "'"):
        state = "code"
        index += 1
      else:
        index += 1
  return "".join(result)


def _find_matching(text: str, opening: int, open_char: str, close_char: str) -> int:
  masked = _mask_comments(text)
  depth = 0
  for index in range(opening, len(masked)):
    char = masked[index]
    if char == open_char:
      depth += 1
    elif char == close_char:
      depth -= 1
      if depth == 0:
        return index
  return -1


def _split_top_level(text: str, separator: str = ",") -> list[str]:
  masked = _mask_comments(text)
  parts: list[str] = []
  start = 0
  depth = 0
  quote = ""
  index = 0
  while index < len(masked):
    char = masked[index]
    if quote:
      if char == "\\":
        index += 2
        continue
      if char == quote:
        quote = ""
    elif char in ("\"", "'"):
      quote = char
    elif char in "({[":
      depth += 1
    elif char in ")}]":
      depth = max(0, depth - 1)
    elif char == separator and depth == 0:
      parts.append(text[start:index].strip())
      start = index + 1
    index += 1
  parts.append(text[start:].strip())
  return parts


def _normalize_space(text: str) -> str:
  return re.sub(r"\s+", " ", text).strip()


def _strip_outer_parentheses(text: str) -> str:
  value = text.strip()
  while value.startswith("(") and value.endswith(")"):
    close = _find_matching(value, 0, "(", ")")
    if close != len(value) - 1:
      break
    value = value[1:-1].strip()
  return value


# ---------------------------------------------------------------------------
# Preprocessor guards and branches
# ---------------------------------------------------------------------------


@dataclass(frozen=True)
class ConditionalBranch:
  path: str
  line: int
  branch: str
  expression: str
  requirements: tuple[tuple[str, bool], ...]
  scope: str


def _requirements_for_expression(expression: str) -> tuple[tuple[str, bool], ...]:
  pairs: list[tuple[str, bool]] = []
  defined_pattern = re.compile(
      r"(!\s*)?\bdefined\s*\(?\s*([A-Za-z_][A-Za-z0-9_]*)\s*\)?"
  )
  masked_parts: list[str] = []
  last = 0
  for match in defined_pattern.finditer(expression):
    masked_parts.append(expression[last : match.start()])
    masked_parts.append(" " * (match.end() - match.start()))
    last = match.end()
    pairs.append((match.group(2), not bool(match.group(1))))
  masked_parts.append(expression[last:])
  masked = "".join(masked_parts)

  bare_pattern = re.compile(r"(!\s*)?\b([A-Z][A-Z0-9_]{2,})\b")
  ignored = {"DEFINED", "TRUE", "FALSE", "NULL", "AND", "OR", "NOT"}
  for match in bare_pattern.finditer(masked):
    name = match.group(2)
    if name in ignored:
      continue
    pairs.append((name, not bool(match.group(1))))

  deduped: list[tuple[str, bool]] = []
  for pair in pairs:
    if pair not in deduped:
      deduped.append(pair)
  return tuple(deduped)


def _build_guards(lines: list[str]) -> list[tuple[str, ...]]:
  guards = [tuple()] * (len(lines) + 1)
  stack: list[str] = []
  for number, line in enumerate(lines, 1):
    stripped = line.strip()
    if stripped.startswith("#ifdef "):
      stack.append(f"defined({stripped[6:].strip()})")
    elif stripped.startswith("#ifndef "):
      stack.append(f"!defined({stripped[7:].strip()})")
    elif stripped.startswith("#if "):
      stack.append(stripped[3:].strip())
    elif stripped.startswith("#elif "):
      expression = stripped[5:].strip()
      previous = stack[-1] if stack else ""
      stack[-1] = f"!({previous}) && ({expression})"
    elif stripped.startswith("#else"):
      previous = stack[-1] if stack else ""
      stack[-1] = f"!({previous})"
    elif stripped.startswith("#endif"):
      if stack:
        stack.pop()
    guards[number] = tuple(stack)
  return guards


@dataclass
class _BranchFrame:
  conditions: list[str]
  requirements: tuple[tuple[str, bool], ...]


def _negated_requirements(conditions: list[str]) -> tuple[tuple[str, bool], ...]:
  pairs: list[tuple[str, bool]] = []
  for condition in conditions:
    for name, present in _requirements_for_expression(condition):
      pair = (name, not present)
      if pair not in pairs:
        pairs.append(pair)
  return tuple(pairs)


def _combined_requirements(
    frames: list[_BranchFrame],
) -> tuple[tuple[str, bool], ...]:
  pairs: list[tuple[str, bool]] = []
  for frame in frames:
    for pair in frame.requirements:
      if pair not in pairs:
        pairs.append(pair)
  return tuple(pairs)


def _parse_branches(
    text: str,
    path: str,
    scope: str,
    first_line: int = 1,
) -> list[ConditionalBranch]:
  lines = text.splitlines()
  branches: list[ConditionalBranch] = []
  frames: list[_BranchFrame] = []
  for number, line in enumerate(lines, first_line):
    stripped = line.strip()
    expression: str | None = None
    if stripped.startswith("#ifdef "):
      expression = f"defined({stripped[6:].strip()})"
    elif stripped.startswith("#ifndef "):
      expression = f"!defined({stripped[7:].strip()})"
    elif stripped.startswith("#if "):
      expression = stripped[3:].strip()

    if expression is not None:
      requirements = _requirements_for_expression(expression)
      frames.append(_BranchFrame([expression], requirements))
      branches.append(
          ConditionalBranch(
              path, number, "if", expression, _combined_requirements(frames), scope
          )
      )
    elif stripped.startswith("#elif ") and frames:
      expression = stripped[5:].strip()
      requirements = _negated_requirements(frames[-1].conditions)
      for pair in _requirements_for_expression(expression):
        if pair not in requirements:
          requirements = requirements + (pair,)
      frames[-1].conditions.append(expression)
      frames[-1].requirements = requirements
      branches.append(
          ConditionalBranch(
              path, number, "elif", expression, _combined_requirements(frames), scope
          )
      )
    elif stripped.startswith("#else") and frames:
      requirements = _negated_requirements(frames[-1].conditions)
      expression = f"!({' && '.join(frames[-1].conditions)})"
      frames[-1].requirements = requirements
      branches.append(
          ConditionalBranch(
              path, number, "else", expression, _combined_requirements(frames), scope
          )
      )
    elif stripped.startswith("#endif"):
      if frames:
        frames.pop()
  return branches


def _describe_requirements(requirements: tuple[tuple[str, bool], ...]) -> str:
  return " and ".join(
      f"{name} {'defined' if present else 'undefined'}" for name, present in requirements
  )


# ---------------------------------------------------------------------------
# Constants and expressions
# ---------------------------------------------------------------------------


@dataclass(frozen=True)
class Constant:
  name: str
  raw: str
  path: str
  line: int
  guard: tuple[str, ...]


@dataclass
class Resolved:
  kind: str
  value: object | None
  reason: str = ""
  sources: frozenset[str] = frozenset()


_CONSTEXPR_PATTERN = re.compile(
    r"(?:static\s+)?constexpr\s+(?![^;=]*[\(\{])[^;=]*?\b([A-Z][A-Z0-9_]*)\s*=\s*([^;]+);",
    re.S,
)
_ENUM_PATTERN = re.compile(
    r"(?:typedef\s+)?enum(?:\s+[A-Za-z_][A-Za-z0-9_]*)?\s*\{(.*?)\}\s*[A-Za-z_][A-Za-z0-9_]*\s*;",
    re.S,
)
_NAMESPACE_PATTERN = re.compile(r"\bnamespace\s+([A-Za-z_][A-Za-z0-9_]*)\s*\{")


def _namespace_ranges(text: str) -> list[tuple[int, int, str]]:
  masked = _mask_comments(text)
  ranges: list[tuple[int, int, str]] = []
  for match in _NAMESPACE_PATTERN.finditer(masked):
    opening = masked.find("{", match.start())
    if opening < 0:
      continue
    closing = _find_matching(masked, opening, "{", "}")
    if closing > opening:
      ranges.append((opening, closing, match.group(1)))
  return ranges


def _enclosing_namespace(offset: int, ranges: list[tuple[int, int, str]]) -> str | None:
  candidates = [entry for entry in ranges if entry[0] < offset < entry[1]]
  if not candidates:
    return None
  return min(candidates, key=lambda entry: entry[1] - entry[0])[2]


def _parse_constants(
    path: str,
    text: str,
    constants: dict[str, list[Constant]],
    source_guards: list[tuple[str, ...]],
) -> None:
  masked = _mask_comments(text)
  starts = _line_starts(text)
  ranges = _namespace_ranges(text)

  def add(name: str, raw: str, offset: int, qualified: str | None = None) -> None:
    line = _line_for_offset(starts, offset)
    guard = source_guards[line] if line < len(source_guards) else tuple()
    entry = Constant(name, raw.strip(), path, line, guard)
    constants.setdefault(name, []).append(entry)
    if qualified and qualified != name:
      constants.setdefault(qualified, []).append(entry)

  for match in _CONSTEXPR_PATTERN.finditer(masked):
    name = match.group(1)
    raw = text[match.start(2) : match.end(2)].strip()
    namespace = _enclosing_namespace(match.start(1), ranges)
    add(name, raw, match.start(1), f"{namespace}::{name}" if namespace else None)

  for match in _ENUM_PATTERN.finditer(masked):
    body = match.group(1)
    body_offset = match.start(1)
    next_value: int | None = 0
    cursor = 0
    for entry_text in _split_top_level(body):
      entry_offset = body_offset + cursor
      cursor += len(entry_text) + 1
      entry_text = _normalize_space(entry_text)
      if not entry_text:
        continue
      entry_match = re.match(r"([A-Za-z_][A-Za-z0-9_]*)\s*(?:=\s*(.+))?$", entry_text, re.S)
      if not entry_match:
        continue
      name = entry_match.group(1)
      explicit = entry_match.group(2)
      value: int | None = None
      if explicit is not None:
        explicit = explicit.strip()
        number = re.fullmatch(r"[+-]?(?:0[xX][0-9a-fA-F]+|\d+)", explicit)
        if number:
          value = int(explicit, 0)
        elif explicit in ("true", "false"):
          value = 1 if explicit == "true" else 0
        else:
          next_value = None
      elif next_value is not None:
        value = next_value
      if value is not None:
        add(name, str(value), entry_offset)
        next_value = value + 1
      else:
        add(name, explicit or "?", entry_offset)

  # Bare constexpr declarations inside a namespace are keyed both ways, but the
  # Settings header declarations live inside a class, so the unqualified name is
  # the key the defaults use.


def _lookup_constant(
    name: str,
    constants: dict[str, list[Constant]],
    depth: int,
) -> Resolved:
  entries = constants.get(name)
  if not entries:
    # A namespaced reference may fall back to a unique unqualified name.
    if "::" in name:
      entries = constants.get(name.split("::")[-1])
    if not entries:
      return Resolved("unknown", None, f"unknown identifier {name}")

  values: set[tuple[str, object]] = set()
  sources = {entry.path for entry in entries}
  reasons: list[str] = []
  for entry in entries:
    resolved = _resolve_expression(entry.raw, constants, depth + 1)
    if resolved.kind == "unknown":
      reasons.append(resolved.reason)
      continue
    values.add((resolved.kind, resolved.value))
  if len(values) == 1:
    kind, value = next(iter(values))
    return Resolved(kind, value, "", frozenset(sources))
  if len(values) > 1:
    return Resolved(
        "unknown",
        None,
        f"{name} is board-conditional ({len(values)} values)",
        frozenset(sources),
    )
  return Resolved("unknown", None, reasons[0] if reasons else f"{name} is unresolved", frozenset(sources))


def _unescape_string(value: str) -> str:
  body = value[1:-1]
  result: list[str] = []
  index = 0
  while index < len(body):
    char = body[index]
    if char != "\\" or index + 1 >= len(body):
      result.append(char)
      index += 1
      continue
    following = body[index + 1]
    mapping = {"n": "\n", "t": "\t", "r": "\r", "0": "\0", "\\": "\\", '"': '"', "'": "'"}
    if following in mapping:
      result.append(mapping[following])
      index += 2
    elif following == "x" and index + 3 < len(body):
      try:
        result.append(chr(int(body[index + 2 : index + 4], 16)))
        index += 4
      except ValueError:
        result.append(following)
        index += 2
    else:
      result.append(following)
      index += 2
  return "".join(result)


def _resolve_expression(
    expression: str,
    constants: dict[str, list[Constant]],
    depth: int = 0,
) -> Resolved:
  if depth > 12:
    return Resolved("unknown", None, "constant resolution exceeded the depth limit")
  value = _strip_outer_parentheses(_normalize_space(expression))
  if not value:
    return Resolved("unknown", None, "empty expression")

  cast = re.fullmatch(r"static_cast\s*<[^>]+>\s*\((.*)\)", value, re.S)
  if cast:
    return _resolve_expression(cast.group(1), constants, depth + 1)

  if value in ("true", "false"):
    return Resolved("bool", value == "true")
  if re.fullmatch(r"[+-]?(?:0[xX][0-9a-fA-F]+|\d+)", value):
    return Resolved("int", int(value, 0))
  if len(value) >= 2 and value[0] == '"' and value[-1] == '"':
    return Resolved("string", _unescape_string(value))
  if len(value) >= 3 and value[0] == "'" and value[-1] == "'":
    body = value[1:-1]
    if len(body) == 1:
      return Resolved("int", ord(body))
    if body.startswith("\\") and len(body) == 2:
      mapping = {"n": 10, "t": 9, "r": 13, "0": 0, "\\": 92, "'": 39}
      if body[1] in mapping:
        return Resolved("int", mapping[body[1]])
    return Resolved("unknown", None, f"character literal {value} is not understood")

  if value.startswith("{"):
    return Resolved("unknown", None, "aggregate initializer is not a scalar default")

  identifier = re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*(?:::[A-Za-z_][A-Za-z0-9_]*)*", value)
  if identifier:
    return _lookup_constant(value, constants, depth)

  return Resolved("unknown", None, f"expression {value!r} is not a literal or known constant")


def _canonical_source(resolved: Resolved) -> str | None:
  if resolved.kind == "bool":
    return "true" if resolved.value else "false"
  if resolved.kind == "int":
    return str(resolved.value)
  if resolved.kind == "string":
    return str(resolved.value)
  return None


# ---------------------------------------------------------------------------
# Settings source parsing
# ---------------------------------------------------------------------------


@dataclass(frozen=True)
class SettingRow:
  symbol: str
  wire_id: int | None
  name: str
  key: str
  line: int
  guard: tuple[str, ...]


@dataclass(frozen=True)
class CaseLabel:
  symbol: str
  line: int
  guard: tuple[str, ...]


@dataclass
class DefaultGroup:
  labels: list[CaseLabel]
  line: int
  body: str
  body_line: int
  expression: str | None = None
  expression_line: int = 0
  has_statement: bool = False


def _source_lines(text: str) -> tuple[list[str], list[tuple[str, ...]]]:
  lines = text.splitlines()
  return lines, _build_guards(lines)


def _guard_at(guards: list[tuple[str, ...]], line: int) -> tuple[str, ...]:
  if 1 <= line < len(guards):
    return guards[line]
  return tuple()


def _find_settings_table_body(text: str) -> tuple[int, int] | None:
  match = re.search(r"Settings::m_Setting\s*=\s*\{", text)
  if not match:
    return None
  opening = text.find("{", match.start())
  closing = _find_matching(text, opening, "{", "}")
  if closing < 0:
    return None
  return opening + 1, closing


def _parse_settings_table(
    text: str,
    path: str,
    report: CheckReport,
) -> list[SettingRow]:
  body_range = _find_settings_table_body(text)
  if body_range is None:
    report.fail(path, 0, "settings table initializer was not found")
    return []
  start, end = body_range
  body = text[start:end]
  starts = _line_starts(text)
  _, guards = _source_lines(text)

  row_pattern = re.compile(
      r"\{\s*([A-Z][A-Z0-9_]*)\s*,\s*"
      r"\{\s*([A-Z][A-Z0-9_]*)\s*,\s*([^,{}]+?)\s*,\s*"
      r"\"((?:[^\"\\]|\\.)*)\"\s*,\s*\"((?:[^\"\\]|\\.)*)\"\s*,\s*[^}]+?\}\s*\}",
      re.S,
  )
  rows: list[SettingRow] = []
  for match in row_pattern.finditer(body):
    outer = match.group(1)
    inner = match.group(2)
    line = _line_for_offset(starts, start + match.start())
    if outer != inner:
      report.warn(
          path,
          line,
          f"settings row {outer} wraps a different inner symbol {inner}; not understood",
      )
    raw_wire = _normalize_space(match.group(3))
    wire_id: int | None
    if re.fullmatch(r"(?:0[xX][0-9a-fA-F]+|\d+)", raw_wire):
      wire_id = int(raw_wire, 0)
    else:
      wire_id = None
      report.warn(path, line, f"{outer} wire id {raw_wire!r} is not a decimal or hex literal")
    rows.append(
        SettingRow(
            outer,
            wire_id,
            match.group(4),
            match.group(5),
            line,
            _guard_at(guards, line),
        )
    )

  outer_starts = list(
      re.finditer(r"\{\s*([A-Z][A-Z0-9_]*)\s*,\s*\{", body, re.S)
  )
  parsed_offsets = {match.start() for match in row_pattern.finditer(body)}
  for match in outer_starts:
    if match.start() not in parsed_offsets:
      line = _line_for_offset(starts, start + match.start())
      report.warn(
          path,
          line,
          f"settings row {match.group(1)} could not be parsed; the table parser will not "
          "claim it is covered",
      )
  return rows


def _parse_default_groups(
    text: str,
    path: str,
    report: CheckReport,
) -> list[DefaultGroup]:
  init_match = re.search(r"\bvoid\s+Settings::init\s*\(\s*void\s*\)\s*\{", text)
  if not init_match:
    report.fail(path, 0, "Settings::init body was not found")
    return []
  opening = text.find("{", init_match.start())
  closing = _find_matching(text, opening, "{", "}")
  if closing < 0:
    report.fail(path, _line_for_offset(_line_starts(text), opening), "Settings::init body is not closed")
    return []
  body = text[opening + 1 : closing]
  body_start_line = _line_for_offset(_line_starts(text), opening + 1)
  lines = body.splitlines()
  guards = _build_guards(text.splitlines())

  groups: list[DefaultGroup] = []
  current: DefaultGroup | None = None
  line_number = body_start_line
  for raw in lines:
    stripped = raw.strip()
    absolute_line = line_number
    line_number += 1
    if not stripped or stripped.startswith("//") or stripped.startswith("/*"):
      if current is not None:
        current.body += raw + "\n"
      continue
    if stripped.startswith("#"):
      if current is not None:
        current.body += raw + "\n"
      continue
    case_match = re.match(r"case\s+(?:Settings::)?([A-Z][A-Z0-9_]*)\s*:\s*(.*)$", stripped)
    if case_match:
      symbol = case_match.group(1)
      trailing = case_match.group(2).strip()
      if current is None or current.has_statement:
        current = DefaultGroup(
            labels=[],
            line=absolute_line,
            body="",
            body_line=absolute_line + 1,
        )
        groups.append(current)
      current.labels.append(CaseLabel(symbol, absolute_line, _guard_at(guards, absolute_line)))
      if not current.has_statement:
        current.body_line = absolute_line + 1
      if trailing and not trailing.startswith("//"):
        current.body += trailing + "\n"
        current.has_statement = True
      continue
    if stripped.startswith("default:"):
      report.warn(
          path,
          absolute_line,
          "Settings::init has a default: label; the scanner cannot tell which setting it seeds",
      )
      if current is not None:
        current.body += raw + "\n"
        current.has_statement = True
      continue
    if current is not None:
      current.body += raw + "\n"
      if stripped not in ("{", "}"):
        current.has_statement = True

  for group in groups:
    expression, expression_line = _extract_save_call(group.body, group.body_line, path, report)
    group.expression = expression
    group.expression_line = expression_line
  return groups


def _extract_save_call(
    body: str,
    body_start_line: int,
    path: str,
    report: CheckReport,
) -> tuple[str | None, int]:
  masked = _mask_comments(body)
  save_matches = list(re.finditer(r"\bsave\s*<", masked))
  if not save_matches:
    return None, 0
  match = save_matches[0]
  if len(save_matches) > 1:
    line = body_start_line + masked[: match.start()].count("\n")
    report.warn(
        path,
        line,
        f"case body has {len(save_matches)} save<> calls; only the first default is "
        "compared, so a conditional default may be unverified",
    )
  angle_open = masked.find("<", match.start())
  angle_close = _find_matching(masked, angle_open, "<", ">")
  if angle_close < 0:
    line = body_start_line + masked[: match.start()].count("\n")
    report.warn(path, line, "save<> template argument is not closed; default not parsed")
    return None, 0
  paren_open = masked.find("(", angle_close)
  if paren_open < 0:
    line = body_start_line + masked[: match.start()].count("\n")
    report.warn(path, line, "save<> call has no argument list; default not parsed")
    return None, 0
  paren_close = _find_matching(masked, paren_open, "(", ")")
  if paren_close < 0:
    line = body_start_line + masked[: match.start()].count("\n")
    report.warn(path, line, "save<> call is not closed; default not parsed")
    return None, 0
  arguments = _split_top_level(body[paren_open + 1 : paren_close])
  line = body_start_line + masked[: match.start()].count("\n")
  if len(arguments) != 2:
    report.warn(
        path,
        line,
        f"save<> call has {len(arguments)} arguments; expected setting.type and a value",
    )
    return None, 0
  return _normalize_space(arguments[1]), line


# ---------------------------------------------------------------------------
# Host test targets and branch coverage
# ---------------------------------------------------------------------------


@dataclass
class HostTarget:
  name: str
  line: int
  sources: list[str] = field(default_factory=list)
  defines: dict[str, int] = field(default_factory=dict)

  @property
  def compiles_settings(self) -> bool:
    return any(source.replace("\\", "/").endswith("src/FurbleSettings.cpp") for source in self.sources)

  @property
  def settings_related(self) -> bool:
    if self.compiles_settings:
      return True
    lowered = self.name.lower()
    if "settings" in lowered or "text_size" in lowered:
      return True
    for source in self.sources:
      basename = PurePosixPath(source.replace("\\", "/")).name.lower()
      if "settings" in basename or "text_size" in basename:
        return True
    return False

  def satisfies(self, requirements: tuple[tuple[str, bool], ...]) -> bool:
    for name, present in requirements:
      if present and name not in self.defines:
        return False
      if not present and name in self.defines:
        return False
    return True


def _strip_cmake_comments(text: str) -> str:
  result: list[str] = []
  for line in text.splitlines():
    in_quote = False
    escaped = False
    cutoff = len(line)
    for index, char in enumerate(line):
      if escaped:
        escaped = False
        continue
      if char == "\\":
        escaped = True
        continue
      if char == '"':
        in_quote = not in_quote
        continue
      if char == "#" and not in_quote:
        cutoff = index
        break
    result.append(line[:cutoff])
  return "\n".join(result)


def _cmake_calls(text: str, name: str) -> list[tuple[str, int, int]]:
  masked = _strip_cmake_comments(text)
  calls: list[tuple[str, int, int]] = []
  pattern = re.compile(rf"\b{re.escape(name)}\s*\(", re.I)
  for match in pattern.finditer(masked):
    opening = masked.find("(", match.start())
    closing = _find_matching(masked, opening, "(", ")")
    if closing < 0:
      continue
    calls.append((masked[opening + 1 : closing], match.start(), masked[: match.start()].count("\n") + 1))
  return calls


def _parse_host_targets(path: str, text: str, report: CheckReport) -> list[HostTarget]:
  targets: dict[str, HostTarget] = {}

  for arguments, offset, line in _cmake_calls(text, "add_executable"):
    try:
      tokens = shlex.split(arguments, posix=True)
    except ValueError as error:
      report.warn(path, line, f"add_executable arguments could not be tokenized: {error}")
      continue
    if not tokens:
      continue
    name = tokens[0]
    target = targets.setdefault(name, HostTarget(name, line))
    target.line = line
    target.sources = tokens[1:]
    if any("$<" in token for token in target.sources):
      report.warn(path, line, f"{name} source list uses a generator expression; not evaluated")

  for arguments, offset, line in _cmake_calls(text, "target_compile_definitions"):
    try:
      tokens = shlex.split(arguments, posix=True)
    except ValueError as error:
      report.warn(path, line, f"target_compile_definitions arguments could not be tokenized: {error}")
      continue
    if not tokens:
      continue
    name = tokens[0]
    target = targets.setdefault(name, HostTarget(name, line))
    for token in tokens[1:]:
      if token in ("PRIVATE", "PUBLIC", "INTERFACE", "BEFORE"):
        continue
      if "$<" in token or "${" in token:
        report.warn(path, line, f"{name} compile definition {token!r} is dynamic; not evaluated")
        continue
      define_name = token.split("=", 1)[0].strip()
      if define_name.startswith("-D"):
        define_name = define_name[2:]
      if define_name:
        target.defines.setdefault(define_name, line)

  return list(targets.values())


@dataclass(frozen=True)
class ExplicitTestGuard:
  path: str
  line: int
  requirements: tuple[tuple[str, bool], ...]


def _parse_explicit_test_guards(
    root: Path,
    report: CheckReport,
) -> list[ExplicitTestGuard]:
  guards: list[ExplicitTestGuard] = []
  tests = root / "tests"
  if not tests.is_dir():
    return guards
  for path in sorted(tests.rglob("*.cpp")):
    try:
      text = path.read_text(encoding="utf-8")
    except OSError:
      continue
    if "Settings::" not in text and "TextSizePolicy" not in text:
      continue
    relative = _relative(path, root)
    for branch in _parse_branches(text, relative, "test"):
      if branch.requirements:
        guards.append(ExplicitTestGuard(relative, branch.line, branch.requirements))
  return guards


def _check_branch_coverage(
    report: CheckReport,
    branches: Iterable[ConditionalBranch],
    targets: list[HostTarget],
    explicit_guards: list[ExplicitTestGuard],
) -> int:
  checked = 0
  seen: set[tuple[str, int, tuple[tuple[str, bool], ...]]] = set()
  for branch in branches:
    if not branch.requirements:
      continue
    key = (branch.path, branch.line, branch.requirements)
    if key in seen:
      continue
    seen.add(key)
    checked += 1
    if "||" in branch.expression:
      report.warn(
          branch.path,
          branch.line,
          "branch expression uses ||; this scanner approximates it as a conjunction "
          "of every define in the branch",
      )
    strong = [
        target
        for target in targets
        if target.settings_related
        and target.satisfies(branch.requirements)
        and (branch.scope != "defaults" or target.compiles_settings)
    ]
    if strong:
      continue
    weak = [target for target in targets if target.satisfies(branch.requirements)]
    if weak:
      names = ", ".join(sorted({target.name for target in weak}))
      report.warn(
          branch.path,
          branch.line,
          f"only non-settings host target(s) {names} cover "
          f"{_describe_requirements(branch.requirements)}; branch coverage is weak",
      )
      continue
    explicit = [
        guard
        for guard in explicit_guards
        if set(branch.requirements).issubset(set(guard.requirements))
    ]
    if explicit:
      continue
    report.fail(
        branch.path,
        branch.line,
        f"no host test target covers default branch requiring "
        f"{_describe_requirements(branch.requirements)}",
    )
  return checked


# ---------------------------------------------------------------------------
# Documentation parsing
# ---------------------------------------------------------------------------


@dataclass(frozen=True)
class DocRow:
  path: str
  line: int
  section: str
  label: str
  default: str
  escaped_pipe: bool


def _normalize_markdown(value: str) -> str:
  text = value.strip()
  text = re.sub(r"^(\*\*|__|\*|_)(.*)\1$", r"\2", text)
  text = text.replace("`", "")
  return _normalize_space(text)


def _split_table_row(line: str) -> tuple[list[str], bool]:
  text = line.strip()
  if text.startswith("|"):
    text = text[1:]
  if text.endswith("|"):
    text = text[:-1]
  cells: list[str] = []
  current: list[str] = []
  escaped = False
  index = 0
  while index < len(text):
    char = text[index]
    if char == "\\" and index + 1 < len(text) and text[index + 1] == "|":
      current.append("|")
      escaped = True
      index += 2
      continue
    if char == "|":
      cells.append("".join(current).strip())
      current = []
      index += 1
      continue
    current.append(char)
    index += 1
  cells.append("".join(current).strip())
  return cells, escaped


def _is_separator_row(cells: list[str]) -> bool:
  if not cells:
    return False
  for cell in cells:
    if not re.fullmatch(r":?-{2,}:?", cell.replace(" ", "")):
      return False
  return True


def _parse_docs_tables(
    path: str,
    text: str,
    report: CheckReport,
) -> list[DocRow]:
  lines = text.splitlines()
  rows: list[DocRow] = []
  section = ""
  index = 0
  while index < len(lines):
    stripped = lines[index].strip()
    if stripped.startswith("#"):
      section = stripped.lstrip("#").strip()
    if not lines[index].lstrip().startswith("|"):
      index += 1
      continue
    block: list[tuple[int, str]] = []
    while index < len(lines) and lines[index].lstrip().startswith("|"):
      block.append((index + 1, lines[index]))
      index += 1
    if len(block) < 2:
      report.warn(path, block[0][0], "single-line pipe block is not a settings table")
      continue
    header_cells, header_escaped = _split_table_row(block[0][1])
    header = [_normalize_markdown(cell).casefold() for cell in header_cells]
    if "setting" not in header or "default" not in header:
      if "setting" in header:
        report.warn(
            path,
            block[0][0],
            "settings table has no Default column; defaults cannot be checked",
        )
      continue
    if header_escaped:
      report.warn(path, block[0][0], "settings table header contains an escaped pipe")
    label_index = header.index("setting")
    default_index = header.index("default")
    for line_number, line in block[1:]:
      cells, escaped = _split_table_row(line)
      if _is_separator_row(cells):
        continue
      if len(cells) <= max(label_index, default_index):
        report.warn(
            path,
            line_number,
            f"settings row has {len(cells)} cells but the header needs at least "
            f"{max(label_index, default_index) + 1}; row skipped",
        )
        continue
      label = _normalize_markdown(cells[label_index])
      default = _normalize_markdown(cells[default_index])
      if not label:
        report.warn(path, line_number, "settings row has an empty Setting cell")
        continue
      if not default:
        report.warn(path, line_number, f"settings row {label!r} has an empty Default cell")
        continue
      if escaped:
        report.warn(path, line_number, f"settings row {label!r} contains an escaped pipe")
      rows.append(DocRow(path, line_number, section, label, default, escaped))
  return rows


def _label_candidates(label: str) -> set[str]:
  normalized = _normalize_markdown(label).casefold()
  candidates = {normalized}
  if ">" in normalized:
    candidates.add(normalized.split(">")[-1].strip())
  return candidates


def _canonical_doc_default(rule: DocDefaultRule, cell: str) -> str | None:
  token = _normalize_markdown(cell).casefold()
  token = re.sub(r"\s+", " ", token).strip().rstrip(".")
  if token in rule.aliases:
    return rule.aliases[token]
  numeric = re.fullmatch(r"([0-9]+)", token)
  if numeric:
    return str(int(numeric.group(1)))
  frequency = re.fullmatch(r"([0-9]+)\s*mhz", token)
  if frequency:
    return str(int(frequency.group(1)))
  return None


def _check_docs(
    root: Path,
    report: CheckReport,
    rows_by_symbol: dict[str, Resolved],
    resolved_lines: dict[str, int],
) -> int:
  parsed_rows: dict[str, list[DocRow]] = {}
  total_rows = 0
  for relative in SETTINGS_DOCS:
    path = root / relative
    if not path.is_file():
      report.fail(relative, 0, "required settings reference is missing")
      continue
    text = _read_text(path, root, report)
    if not text:
      continue
    rows = _parse_docs_tables(relative, text, report)
    parsed_rows[relative] = rows
    total_rows += len(rows)

  compared = 0
  for rule in DOC_DEFAULT_RULES:
    resolved = rows_by_symbol.get(rule.symbol)
    source_canonical = _canonical_source(resolved) if resolved is not None else None
    for relative in SETTINGS_DOCS:
      rows = parsed_rows.get(relative)
      if rows is None:
        continue
      matches = [
          row
          for row in rows
          if _label_candidates(row.label) & {label.casefold() for label in rule.labels}
      ]
      if not matches:
        report.fail(
            relative,
            0,
            f"no settings row documents the {rule.labels[0]!r} default for {rule.symbol}",
        )
        continue
      canonical_values: dict[str, DocRow] = {}
      for row in matches:
        canonical = _canonical_doc_default(rule, row.default)
        if canonical is None:
          report.warn(
              relative,
              row.line,
              f"could not parse documented default {row.default!r} for {rule.symbol}; "
              "the source comparison was not verified",
          )
          continue
        canonical_values.setdefault(canonical, row)
      if len(canonical_values) > 1:
        descriptions = ", ".join(
            f"{value!r} at line {row.line}" for value, row in canonical_values.items()
        )
        report.fail(
            relative,
            matches[0].line,
            f"{rule.symbol} has conflicting documented defaults: {descriptions}",
        )
        continue
      if len(matches) > 1:
        report.warn(
            relative,
            matches[0].line,
            f"{rule.symbol} is documented in {len(matches)} rows; using the first",
        )
      canonical = next(iter(canonical_values), None)
      if canonical is None:
        continue
      compared += 1
      if source_canonical is None:
        reason = resolved.reason if resolved is not None else "the setting is missing from the defaults switch"
        report.warn(
            SETTINGS_SOURCE,
            resolved_lines.get(rule.symbol, 0),
            f"{rule.symbol} source default could not be resolved to a scalar "
            f"({reason}); documented value {canonical!r} was not verified",
        )
        continue
      if canonical != source_canonical:
        report.fail(
            relative,
            matches[0].line,
            f"{rule.symbol} documents default {canonical!r} but the source default is "
            f"{source_canonical!r}",
        )
  report.stats["doc_rows"] = total_rows
  report.stats["doc_defaults_compared"] = compared
  return total_rows


# ---------------------------------------------------------------------------
# Wire-id ledger
# ---------------------------------------------------------------------------


@dataclass(frozen=True)
class LedgerEntry:
  owner: str
  wire_id: int
  line: int
  token: str


def _parse_ledger(
    path: str,
    text: str,
    report: CheckReport,
) -> list[LedgerEntry]:
  lines = text.splitlines()
  in_ledger = False
  found_header = False
  entries: list[LedgerEntry] = []
  token_pattern = re.compile(r"^(\d+)(?:\s*-\s*(\d+))?(?:\s*\(([^)]*)\))?$")
  for number, line in enumerate(lines, 1):
    stripped = line.strip()
    if stripped == "### Companion wire id reservations":
      in_ledger = True
      continue
    if not in_ledger:
      continue
    if not stripped.startswith("|"):
      if found_header:
        break
      continue
    cells, _ = _split_table_row(line)
    if len(cells) < 3:
      report.fail(path, number, f"reservation row has {len(cells)} columns; expected 3")
      continue
    owner, _keys, ids = cells[0], cells[1], cells[2]
    if owner in ("Owner", "PR") or set(owner) <= set("-: "):
      found_header = True
      continue
    found_header = True
    if not owner:
      report.fail(path, number, "reservation row has an empty owner")
      continue
    if ids.strip().casefold() == "none":
      continue
    if ids.strip().endswith(","):
      report.fail(path, number, "reservation row has an empty trailing id token")
      continue
    for token in ids.split(","):
      token = token.strip()
      if not token:
        report.fail(path, number, "reservation row has an empty id token")
        continue
      match = token_pattern.fullmatch(token)
      if not match:
        report.fail(path, number, f"reservation id token {token!r} is not understood")
        continue
      first = int(match.group(1))
      last = int(match.group(2)) if match.group(2) else first
      if first < 1 or last > 255 or first > last:
        report.fail(path, number, f"reservation id range {token!r} is out of range or reversed")
        continue
      for wire_id in range(first, last + 1):
        entries.append(LedgerEntry(owner, wire_id, number, token))
  if not found_header:
    report.fail(path, 0, "companion wire id reservation table was not found")
  return entries


def _check_ledger(
    report: CheckReport,
    ledger_entries: list[LedgerEntry],
    settings_rows: list[SettingRow],
) -> None:
  by_id: dict[int, LedgerEntry] = {}
  for entry in ledger_entries:
    previous = by_id.get(entry.wire_id)
    if previous is not None:
      report.fail(
          LEDGER,
          entry.line,
          f"wire id {entry.wire_id} is claimed by both {previous.owner!r} "
          f"(line {previous.line}) and {entry.owner!r}",
      )
      continue
    by_id[entry.wire_id] = entry

  source_by_id: dict[int, SettingRow] = {}
  for row in settings_rows:
    if row.wire_id is None:
      report.fail(
          SETTINGS_SOURCE,
          row.line,
          f"{row.symbol} wire id could not be parsed; the ledger comparison is incomplete",
      )
      continue
    if row.wire_id == 0:
      continue
    previous = source_by_id.get(row.wire_id)
    if previous is not None:
      report.fail(
          SETTINGS_SOURCE,
          row.line,
          f"{row.symbol} reuses nonzero wire id {row.wire_id} already used by "
          f"{previous.symbol} at line {previous.line}",
      )
      continue
    source_by_id[row.wire_id] = row

  master_ids = {
      entry.wire_id
      for entry in ledger_entries
      if entry.owner == "Master" or entry.owner.startswith("Master ")
  }

  for wire_id, row in sorted(source_by_id.items()):
    if wire_id not in master_ids:
      owner = by_id.get(wire_id)
      detail = f"reserved under {owner.owner!r}" if owner else "outside every ledger row"
      report.fail(
          SETTINGS_SOURCE,
          row.line,
          f"{row.symbol} uses nonzero wire id {wire_id} that is {detail}",
      )

  for wire_id in sorted(master_ids - set(source_by_id)):
    entry = by_id.get(wire_id)
    report.fail(
        LEDGER,
        entry.line if entry else 0,
        f"Master ledger reserves wire id {wire_id} but the settings table has no such id",
    )

  for entry in ledger_entries:
    if entry.owner.startswith("Master"):
      continue
    if entry.wire_id in source_by_id:
      report.fail(
          LEDGER,
          entry.line,
          f"wire id {entry.wire_id} is already shipped by "
          f"{source_by_id[entry.wire_id].symbol} but is still listed under {entry.owner!r}",
      )

  report.stats["source_wire_ids"] = len(source_by_id)
  report.stats["ledger_wire_ids"] = len(by_id)


# ---------------------------------------------------------------------------
# Diff mode
# ---------------------------------------------------------------------------


@dataclass
class DefaultChange:
  symbol: str
  old: str
  new: str
  old_line: int
  new_line: int
  kind: str


@dataclass
class DiffResult:
  changed_paths: set[str] = field(default_factory=set)
  default_changes: list[DefaultChange] = field(default_factory=list)
  added_lines: dict[str, list[str]] = field(default_factory=dict)
  unattributed: list[tuple[int, str]] = field(default_factory=list)


_HUNK_PATTERN = re.compile(r"^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@")


def _extract_single_line_save(line: str) -> str | None:
  masked = _mask_comments(line)
  match = re.search(r"\bsave\s*<", masked)
  if not match:
    return None
  angle_open = masked.find("<", match.start())
  angle_close = _find_matching(masked, angle_open, "<", ">")
  if angle_close < 0:
    return None
  paren_open = masked.find("(", angle_close)
  if paren_open < 0:
    return None
  paren_close = _find_matching(masked, paren_open, "(", ")")
  if paren_close < 0 or ";" not in masked[paren_close:]:
    return None
  arguments = _split_top_level(line[paren_open + 1 : paren_close])
  if len(arguments) != 2:
    return None
  return _normalize_space(arguments[1])


def _update_case(side: dict[str, object], content: str) -> None:
  match = re.match(r"case\s+(?:Settings::)?([A-Z][A-Z0-9_]*)\s*:", content.strip())
  if match:
    side["case"] = match.group(1)


def _record_default(
    side: dict[str, object],
    line: str,
    line_number: int,
    changed: bool = True,
) -> None:
  if "save<" not in line:
    return
  expression = _extract_single_line_save(line)
  symbol = side.get("case")
  if expression is None:
    if changed:
      side.setdefault("unattributed", []).append(
          (line_number, "multi-line or unparsable save call")
      )
    return
  if not isinstance(symbol, str):
    if changed:
      side.setdefault("unattributed", []).append(
          (line_number, f"save for {expression!r} has no visible case label in the diff hunk")
      )
    return
  defaults = side.setdefault("defaults", {})
  assert isinstance(defaults, dict)
  if symbol in defaults and defaults[symbol][0] != expression:
    side.setdefault("conflicts", []).append(
        (line_number, symbol, defaults[symbol][0], expression)
    )
    return
  defaults.setdefault(symbol, (expression, line_number))


def parse_unified_diff(text: str) -> DiffResult:
  result = DiffResult()
  old_side: dict[str, object] = {"case": None, "defaults": {}, "unattributed": [], "conflicts": []}
  new_side: dict[str, object] = {"case": None, "defaults": {}, "unattributed": [], "conflicts": []}
  current_file = ""
  in_hunk = False
  old_line = 0
  new_line = 0
  old_path = ""
  new_path = ""
  for raw in text.splitlines():
    if raw.startswith("diff --git "):
      match = re.match(r"diff --git a/(.*?) b/(.*)$", raw)
      if match:
        old_path = match.group(1)
        new_path = match.group(2)
        path = new_path if new_path != "/dev/null" else old_path
        current_file = path
        result.changed_paths.add(path)
      # Keep the accumulated defaults across file sections. A unified diff
      # orders files alphabetically, so resetting here would discard the
      # defaults parsed before the settings source appears.
      old_side["case"] = None
      new_side["case"] = None
      in_hunk = False
      continue
    if raw.startswith("--- "):
      value = raw[4:].split("\t", 1)[0].strip()
      old_path = value[2:] if value.startswith("a/") else value
      if old_path != "/dev/null":
        result.changed_paths.add(old_path)
      continue
    if raw.startswith("+++ "):
      value = raw[4:].split("\t", 1)[0].strip()
      new_path = value[2:] if value.startswith("b/") else value
      if new_path != "/dev/null":
        current_file = new_path
        result.changed_paths.add(new_path)
      else:
        current_file = old_path
      continue
    if raw.startswith("rename from "):
      old_path = raw[len("rename from ") :].strip()
      result.changed_paths.add(old_path)
      continue
    if raw.startswith("rename to "):
      new_path = raw[len("rename to ") :].strip()
      current_file = new_path
      result.changed_paths.add(new_path)
      continue
    hunk = _HUNK_PATTERN.match(raw)
    if hunk:
      old_line = int(hunk.group(1))
      new_line = int(hunk.group(3))
      in_hunk = True
      continue
    if not in_hunk:
      continue
    if raw.startswith("\\"):
      continue
    if raw.startswith(" "):
      content = raw[1:]
      _update_case(old_side, content)
      _update_case(new_side, content)
      if current_file == SETTINGS_SOURCE:
        _record_default(old_side, content, old_line, changed=False)
        _record_default(new_side, content, new_line, changed=False)
      old_line += 1
      new_line += 1
    elif raw.startswith("-"):
      content = raw[1:]
      _update_case(old_side, content)
      if current_file == SETTINGS_SOURCE:
        _record_default(old_side, content, old_line)
      old_line += 1
    elif raw.startswith("+"):
      content = raw[1:]
      _update_case(new_side, content)
      if current_file == SETTINGS_SOURCE:
        _record_default(new_side, content, new_line)
      result.added_lines.setdefault(current_file, []).append(content)
      new_line += 1

  old_defaults = old_side.get("defaults", {})
  new_defaults = new_side.get("defaults", {})
  assert isinstance(old_defaults, dict) and isinstance(new_defaults, dict)
  for symbol in sorted(set(old_defaults) | set(new_defaults)):
    if symbol in old_defaults and symbol in new_defaults:
      old_expression, old_source_line = old_defaults[symbol]
      new_expression, new_source_line = new_defaults[symbol]
      if old_expression == new_expression:
        continue
      result.default_changes.append(
          DefaultChange(symbol, old_expression, new_expression, old_source_line, new_source_line, "changed")
      )
    elif symbol in new_defaults:
      expression, source_line = new_defaults[symbol]
      result.default_changes.append(DefaultChange(symbol, "", expression, 0, source_line, "added"))
    else:
      expression, source_line = old_defaults[symbol]
      result.default_changes.append(DefaultChange(symbol, expression, "", source_line, 0, "removed"))

  for (line, message) in old_side.get("unattributed", []):  # type: ignore[misc]
    result.unattributed.append((line, f"removed side: {message}"))
  for (line, message) in new_side.get("unattributed", []):  # type: ignore[misc]
    result.unattributed.append((line, f"added side: {message}"))
  for side_name, side in (("removed", old_side), ("added", new_side)):
    for line, symbol, first, second in side.get("conflicts", []):  # type: ignore[misc]
      result.unattributed.append(
          (
              line,
              f"{side_name} side has conflicting defaults for {symbol}: "
              f"{first!r} and {second!r}",
          )
      )
  return result


def _is_settings_test(path: str) -> bool:
  pure = PurePosixPath(path)
  if not path.startswith("tests/"):
    return False
  lowered = pure.name.lower()
  if not lowered.endswith("_test.cpp"):
    return False
  return "settings" in lowered or "text_size" in lowered


def _check_diff(
    report: CheckReport,
    diff: DiffResult,
) -> None:
  report.stats["diff_changed_paths"] = len(diff.changed_paths)
  report.stats["diff_default_changes"] = len(diff.default_changes)
  if not diff.changed_paths:
    report.warn(SETTINGS_SOURCE, 0, "the diff contains no file changes; diff contract was not checked")
    return

  for line, message in diff.unattributed:
    report.warn(
        SETTINGS_SOURCE,
        line,
        f"{message}; a default change in this hunk was not attributed and is not enforced",
    )

  if not diff.default_changes:
    return

  changed = diff.changed_paths
  affected = ", ".join(change.symbol for change in diff.default_changes)
  first_line = next(
      (change.new_line for change in diff.default_changes if change.new_line > 0),
      0,
  )

  for required in REQUIRED_DEFAULT_DOCS:
    if required not in changed:
      report.fail(
          SETTINGS_SOURCE,
          first_line,
          f"default change for {affected} requires {required} to be updated in the same change",
      )

  has_settings_test = any(_is_settings_test(path) for path in changed)
  if not has_settings_test:
    report.fail(
        SETTINGS_SOURCE,
        first_line,
        f"default change for {affected} requires a settings test under tests/ to be updated",
    )

  legacy_lines: list[str] = []
  for path, lines in diff.added_lines.items():
    if _is_settings_test(path):
      legacy_lines.extend(lines)
  if has_settings_test and not any(
      "legacy" in line.casefold() or "preserv" in line.casefold() for line in legacy_lines
  ):
    report.warn(
        SETTINGS_SOURCE,
        first_line,
        f"default change for {affected} does not add legacy-preservation coverage; "
        "confirm a stored value from the previous default is still preserved",
    )


# ---------------------------------------------------------------------------
# Orchestration
# ---------------------------------------------------------------------------


def _parse_source_contracts(
    root: Path,
    report: CheckReport,
) -> tuple[list[SettingRow], dict[str, Resolved], dict[str, int]]:
  source_path = root / SETTINGS_SOURCE
  header_path = root / SETTINGS_HEADER
  if not source_path.is_file():
    report.fail(SETTINGS_SOURCE, 0, "required firmware source is missing")
    return [], {}, {}
  if not header_path.is_file():
    report.fail(SETTINGS_HEADER, 0, "required settings header is missing")
    return [], {}, {}

  source_text = _read_text(source_path, root, report)
  header_text = _read_text(header_path, root, report)
  if not source_text or not header_text:
    return [], {}, {}

  settings_rows = _parse_settings_table(source_text, SETTINGS_SOURCE, report)
  groups = _parse_default_groups(source_text, SETTINGS_SOURCE, report)

  constants: dict[str, list[Constant]] = {}
  _, header_guards = _source_lines(header_text)
  _parse_constants(SETTINGS_HEADER, header_text, constants, header_guards)
  text_size_path = root / TEXT_SIZE_HEADER
  referenced_headers: set[str] = set()
  if text_size_path.is_file():
    text_size_text = _read_text(text_size_path, root, report)
    _, text_size_guards = _source_lines(text_size_text)
    _parse_constants(TEXT_SIZE_HEADER, text_size_text, constants, text_size_guards)

  resolved_by_symbol: dict[str, Resolved] = {}
  resolved_lines: dict[str, int] = {}
  groups_by_symbol: dict[str, DefaultGroup] = {}
  for group in groups:
    for label in group.labels:
      if not group.has_statement:
        if label.symbol in SPECIAL_DEFAULTS:
          continue
        report.fail(
            SETTINGS_SOURCE,
            label.line,
            f"case {label.symbol} does not save a default",
        )
        continue
      if group.expression is None:
        # _extract_save_call already warned with the precise line when a save
        # call was present but malformed. Distinguish that from a case that has
        # no save call at all.
        if label.symbol not in SPECIAL_DEFAULTS:
          has_save_token = bool(re.search(r"\bsave\s*<", _mask_comments(group.body)))
          detail = (
              "does not call save<> with a default"
              if has_save_token
              else "does not save a default"
          )
          report.fail(SETTINGS_SOURCE, label.line, f"case {label.symbol} {detail}")
        continue
      if label.symbol in SPECIAL_DEFAULTS:
        continue
      if label.symbol in groups_by_symbol:
        previous = groups_by_symbol[label.symbol]
        report.fail(
            SETTINGS_SOURCE,
            label.line,
            f"case {label.symbol} has more than one default (first at line {previous.line})",
        )
        continue
      groups_by_symbol[label.symbol] = group
      resolved = _resolve_expression(group.expression, constants)
      resolved_by_symbol[label.symbol] = resolved
      resolved_lines[label.symbol] = group.expression_line
      if resolved.kind == "unknown" and "board-conditional" in resolved.reason:
        # Only a board-conditional constant makes a default expression depend
        # on a header's preprocessor branches. Other header constants come from
        # FurbleSettings.h itself and do not make the defaults conditional.
        referenced_headers.update(
            path for path in resolved.sources if path != SETTINGS_HEADER
        )

  table_symbols = {row.symbol for row in settings_rows}
  if len(table_symbols) != len(settings_rows):
    seen: dict[str, int] = {}
    for row in settings_rows:
      if row.symbol in seen:
        report.fail(
            SETTINGS_SOURCE,
            row.line,
            f"settings table repeats symbol {row.symbol} (first at line {seen[row.symbol]})",
        )
      else:
        seen[row.symbol] = row.line

  symbols_with_case = {
      label.symbol for group in groups for label in group.labels
  }
  for row in settings_rows:
    if row.symbol in symbols_with_case:
      group = next(
          (
              candidate
              for candidate in groups
              if any(label.symbol == row.symbol for label in candidate.labels)
          ),
          None,
      )
      label = None
      if group is not None:
        label = next(
            (item for item in group.labels if item.symbol == row.symbol),
            None,
        )
      if label is not None and label.guard != row.guard:
        report.warn(
            SETTINGS_SOURCE,
            row.line,
            f"{row.symbol} table row guard {row.guard!r} differs from defaults guard "
            f"{label.guard!r}; preprocessor parsing may be incomplete",
        )
      continue
    if row.symbol in SPECIAL_DEFAULTS:
      continue
    if row.wire_id == 0:
      report.warn(
          SETTINGS_SOURCE,
          row.line,
          f"off-wire setting {row.symbol} has no default case; excluded from the coverage requirement",
      )
      continue
    report.fail(
        SETTINGS_SOURCE,
        row.line,
        f"{row.symbol} is in the settings table but has no default case in Settings::init",
    )

  for symbol, group in groups_by_symbol.items():
    if symbol not in table_symbols:
      report.fail(
          SETTINGS_SOURCE,
          group.labels[0].line if group.labels else group.line,
          f"default case {symbol} is not in the settings table",
      )

  # Unresolved scalar defaults are named rather than silently skipped, but a
  # board-conditional scalar is expected when a setting is intentionally
  # per-board. Both kinds still get a warning so a doc claim is never assumed.
  unresolved = 0
  for symbol, resolved in sorted(resolved_by_symbol.items()):
    if resolved.kind == "unknown":
      unresolved += 1
      report.warn(
          SETTINGS_SOURCE,
          resolved_lines[symbol],
          f"{symbol} default could not be resolved to a scalar ({resolved.reason}); "
          "documentation comparison must treat it as unverified",
      )

  report.stats["table_rows"] = len(settings_rows)
  report.stats["default_groups"] = len(groups)
  report.stats["default_symbols"] = len(groups_by_symbol)
  report.stats["unresolved_defaults"] = unresolved
  report.stats["referenced_headers"] = sorted(referenced_headers)
  return settings_rows, resolved_by_symbol, resolved_lines


def _settings_branches(text: str, path: str) -> list[ConditionalBranch]:
  match = re.search(r"\bvoid\s+Settings::init\s*\(\s*void\s*\)\s*\{", text)
  if not match:
    return []
  opening = text.find("{", match.start())
  closing = _find_matching(text, opening, "{", "}")
  if closing < 0:
    return []
  body = text[opening + 1 : closing]
  first_line = _line_for_offset(_line_starts(text), opening + 1)
  return _parse_branches(body, path, "defaults", first_line=first_line)


def _referenced_header_branches(
    root: Path,
    report: CheckReport,
    referenced_headers: Iterable[str],
) -> list[ConditionalBranch]:
  branches: list[ConditionalBranch] = []
  for relative in sorted(set(referenced_headers)):
    if relative not in (SETTINGS_HEADER, TEXT_SIZE_HEADER):
      report.warn(
          SETTINGS_SOURCE,
          0,
          f"default expression references {relative}, which this scanner does not "
          "scan for board branches",
      )
      continue
    path = root / relative
    if not path.is_file():
      continue
    text = _read_text(path, root, report)
    if not text:
      continue
    # Skip the include-guard branch. It guards the whole header rather than a
    # board-conditional default, so it is not part of this contract.
    lines = text.splitlines()
    body_start = 1
    for number, line in enumerate(lines, 1):
      stripped = line.strip()
      if stripped and not stripped.startswith("#") and not stripped.startswith("//"):
        body_start = number
        break
    branches.extend(
        branch
        for branch in _parse_branches(text, relative, "header")
        if branch.line >= body_start
    )
  return branches


def check_repository(root: Path, diff: DiffResult | None = None) -> CheckReport:
  report = CheckReport()
  settings_rows, resolved_by_symbol, resolved_lines = _parse_source_contracts(root, report)

  source_path = root / SETTINGS_SOURCE
  source_text = source_path.read_text(encoding="utf-8") if source_path.is_file() else ""
  branches = _settings_branches(source_text, SETTINGS_SOURCE)
  branches.extend(
      _referenced_header_branches(
          root,
          report,
          report.stats.get("referenced_headers", []),
      )
  )

  cmake_path = root / HOST_CMAKE
  targets: list[HostTarget] = []
  if cmake_path.is_file():
    cmake_text = _read_text(cmake_path, root, report)
    targets = _parse_host_targets(HOST_CMAKE, cmake_text, report)
  else:
    report.warn(HOST_CMAKE, 0, "host test CMake file is missing; branch coverage cannot be checked")
  explicit_guards = _parse_explicit_test_guards(root, report)
  report.stats["host_targets"] = len(targets)
  report.stats["branches_checked"] = _check_branch_coverage(
      report, branches, targets, explicit_guards
  )

  _check_docs(root, report, resolved_by_symbol, resolved_lines)

  ledger_path = root / LEDGER
  if ledger_path.is_file():
    ledger_text = _read_text(ledger_path, root, report)
    ledger_entries = _parse_ledger(LEDGER, ledger_text, report)
  else:
    report.fail(LEDGER, 0, "required wire-id ledger is missing")
    ledger_entries = []
  _check_ledger(report, ledger_entries, settings_rows)

  if diff is not None:
    _check_diff(report, diff)
  return report


def _run_git_diff(root: Path, base: str, head: str | None) -> str:
  command = ["git", "-C", str(root), "diff", "--no-color", "--unified=8", base]
  if head:
    command.append(head)
  completed = subprocess.run(command, check=False, capture_output=True, text=True)
  if completed.returncode != 0:
    raise RuntimeError(completed.stderr.strip() or f"git diff exited {completed.returncode}")
  return completed.stdout


def main(argv: list[str] | None = None) -> int:
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument(
      "--root",
      type=Path,
      default=Path(__file__).resolve().parents[1],
      help="repository root to inspect",
  )
  parser.add_argument(
      "--diff-file",
      type=Path,
      help="unified diff to enforce the default-change documentation contract",
  )
  parser.add_argument(
      "--base",
      help="git revision to diff against; enforces the default-change contract",
  )
  parser.add_argument(
      "--head",
      help="git revision for the new side of --base (defaults to the working tree)",
  )
  args = parser.parse_args(argv)

  if args.diff_file and args.base:
    parser.error("--diff-file and --base are mutually exclusive")
  if args.head and not args.base:
    parser.error("--head requires --base")

  diff: DiffResult | None = None
  if args.diff_file:
    try:
      diff_text = args.diff_file.read_text(encoding="utf-8")
    except OSError as error:
      print(f"FAIL: {args.diff_file}: cannot read diff: {error}", file=sys.stderr)
      return 1
    diff = parse_unified_diff(diff_text)
  elif args.base:
    try:
      diff = parse_unified_diff(_run_git_diff(args.root, args.base, args.head))
    except (OSError, RuntimeError) as error:
      print(f"FAIL: git diff: {error}", file=sys.stderr)
      return 1

  report = check_repository(args.root, diff)
  for diagnostic in report.diagnostics:
    print(diagnostic.render(), file=sys.stderr if diagnostic.severity == "FAIL" else sys.stdout)

  print("settings contract check")
  print(f"  settings table rows: {report.stats.get('table_rows', 0)}")
  print(
      "  default groups: "
      f"{report.stats.get('default_groups', 0)} "
      f"({report.stats.get('default_symbols', 0)} symbols, "
      f"{report.stats.get('unresolved_defaults', 0)} unresolved)"
  )
  print(f"  host test targets: {report.stats.get('host_targets', 0)}")
  print(f"  conditional branches checked: {report.stats.get('branches_checked', 0)}")
  print(f"  documentation rows parsed: {report.stats.get('doc_rows', 0)}")
  print(f"  documented defaults compared: {report.stats.get('doc_defaults_compared', 0)}")
  print(f"  source wire ids: {report.stats.get('source_wire_ids', 0)}")
  print(f"  ledger wire ids: {report.stats.get('ledger_wire_ids', 0)}")
  if diff is not None:
    print(
        f"  diff paths: {report.stats.get('diff_changed_paths', 0)}, "
        f"default changes: {report.stats.get('diff_default_changes', 0)}"
    )
  print(f"  summary: {len(report.failures)} FAIL, {len(report.warnings)} WARN")
  print("settings contracts: " + ("FAIL" if report.failures else "PASS"))
  return 1 if report.failures else 0


if __name__ == "__main__":
  raise SystemExit(main())
