#!/usr/bin/env python3
"""Heuristic C++ safety checks for review regressions.

The checks are stdlib-only and deliberately conservative. Every rule bails out
when it cannot resolve the construct, so a symbolic size or an unusual fixture
is skipped instead of guessed. Findings are printed as ``file:line``.

Rules:
  container-bounds       A local vector/array size or reserve/resize followed by
                         a literal index at or past that size.
  filesystem-remove      Removing a directory_iterator entry without an
                         is_regular_file/is_directory check.
  low-byte-sentinel      Asserting a 16/32/64-bit field with a one-byte literal
                         while the fixture writes only one byte.
  terminal-state-acquire An acquisition call without an early return for the
                         enum states the file documents as terminal.

Typical use:
  tools/check_cpp_safety.py --base origin/master
  tools/check_cpp_safety.py --diff-file review.diff
  tools/check_cpp_safety.py --all
  tools/check_cpp_safety.py path/to/changed.cpp
"""

from __future__ import annotations

import argparse
import ast
from bisect import bisect_right
from dataclasses import dataclass, field
from pathlib import Path
import re
import subprocess
import sys
from typing import Iterable, Iterator, Sequence

CPP_SUFFIXES = frozenset(
    {".cpp", ".cc", ".cxx", ".c++", ".h", ".hh", ".hpp", ".hxx", ".inl", ".ipp", ".tpp"}
)
HEADER_SUFFIXES = frozenset({".h", ".hh", ".hpp", ".hxx"})
IGNORED_DIRS = frozenset(
    {".git", ".pio", ".venv", "build", "node_modules", "third_party", "dist"}
)

RULE_BOUNDS = "container-bounds"
RULE_FILESYSTEM = "filesystem-remove"
RULE_LOW_BYTE = "low-byte-sentinel"
RULE_TERMINAL = "terminal-state-acquire"
ALL_RULES = (RULE_BOUNDS, RULE_FILESYSTEM, RULE_LOW_BYTE, RULE_TERMINAL)

DEFAULT_TERMINAL_STATES = ("ABSENT", "DISABLED")
DEFAULT_ACQUIRE_FUNCTIONS = ("beginBurst", "acquirePowerLock")
DEFAULT_ACQUIRE_REGEX = r"^(?:beginBurst|acquire[A-Za-z0-9_]*)$"

ERROR = "ERROR"
WARN = "WARN"

# Words that introduce a block but never a function definition.
CONTROL_WORDS = frozenset(
    {
        "if",
        "for",
        "while",
        "switch",
        "catch",
        "else",
        "do",
        "try",
        "return",
        "sizeof",
        "alignof",
        "decltype",
        "static_assert",
        "struct",
        "class",
        "union",
        "enum",
        "namespace",
        "extern",
        "using",
        "template",
    }
)

# Type-like words that can be followed by "(" inside a template argument and
# must not be mistaken for the name of the function being defined.
NOT_FUNCTION_NAMES = CONTROL_WORDS | frozenset(
    {
        "void",
        "bool",
        "char",
        "short",
        "int",
        "long",
        "float",
        "double",
        "unsigned",
        "signed",
        "auto",
        "size_t",
        "uint8_t",
        "uint16_t",
        "uint32_t",
        "uint64_t",
        "int8_t",
        "int16_t",
        "int32_t",
        "int64_t",
    }
)

_IDENTIFIER = re.compile(r"[A-Za-z_]\w*")
_INT_TYPES = {
    "uint16_t": 16,
    "uint32_t": 32,
    "uint64_t": 64,
    "int16_t": 16,
    "int32_t": 32,
    "int64_t": 64,
}
_TYPE_DECL = re.compile(
    r"\b(?:std::)?(uint16_t|uint32_t|uint64_t|int16_t|int32_t|int64_t)"
    r"\s+([A-Za-z_]\w*)\s*(?=[;=:{([,)])"
)
_FIXTURE_BYTE_WRITE = re.compile(
    r"\b[A-Za-z_]\w*\s*\[\s*(?:0[xX][0-9a-fA-F]+|\d+)\s*\]\s*=\s*"
    r"(0[xX][0-9a-fA-F]+)\s*[;,]"
)
_ASSERTION_CONTEXT = re.compile(
    r"\b(?:check|require|assert|expect|verify|CHECK|EXPECT|ASSERT)\w*\s*\(", re.I
)
_HEX_COMPARE_LEFT = re.compile(
    r"(?P<left>[A-Za-z_]\w*(?:\s*(?:\.|->)\s*[A-Za-z_]\w*)*)"
    r"\s*==\s*(?P<lit>0[xX][0-9a-fA-F]+)[uUlL]*\b"
)
_HEX_COMPARE_RIGHT = re.compile(
    r"(?P<lit>0[xX][0-9a-fA-F]+)[uUlL]*\s*==\s*"
    r"(?P<left>[A-Za-z_]\w*(?:\s*(?:\.|->)\s*[A-Za-z_]\w*)*)"
)
_REMOVE_CALL = re.compile(
    r"\b(?:(?P<qualifier>std::filesystem|fs|filesystem)::)?"
    r"(?P<operation>remove_all|remove)\s*\("
)
_ENUM_BLOCK = re.compile(
    r"\benum\s+(?:class\s+|struct\s+)?([A-Za-z_]\w*)"
    r"(?:\s*:\s*[A-Za-z_:][\w:]*(?:\s*<[^>{}]*>)?)?\s*\{([^{}]*)\}",
    re.S,
)


@dataclass(frozen=True)
class Finding:
  """One scanner result at a source line."""

  path: Path
  line: int
  severity: str
  rule: str
  message: str

  def format(self, root: Path) -> str:
    try:
      display = self.path.resolve().relative_to(root.resolve()).as_posix()
    except ValueError:
      display = str(self.path)
    return f"{display}:{self.line}: {self.severity} {self.rule}: {self.message}"


@dataclass(frozen=True)
class FunctionScope:
  """A brace-matched function or lambda body."""

  name: str
  body_start: int
  body_end: int


@dataclass(frozen=True)
class CapacityEvent:
  """A point where a container gets a known capacity."""

  name: str
  offset: int
  block_start: int
  block_end: int
  low: int
  high: int
  min_when_guard: bool | None
  guard: str | None
  kind: str


@dataclass(frozen=True)
class ScanOptions:
  """Knobs for one scan. The defaults match the review policy."""

  rules: frozenset[str] = field(default_factory=lambda: frozenset(ALL_RULES))
  terminal_states: tuple[str, ...] = DEFAULT_TERMINAL_STATES
  acquire_functions: tuple[str, ...] = DEFAULT_ACQUIRE_FUNCTIONS
  acquire_regex: str = DEFAULT_ACQUIRE_REGEX
  type_index: dict[str, set[int]] | None = None


class SourceFile:
  """One C++ file with comments, strings, and preprocessor lines masked."""

  def __init__(self, path: Path, text: str):
    self.path = path
    self.text = text
    self.masked = mask_code(text)
    self.line_starts = [0, *(index + 1 for index, char in enumerate(text) if char == "\n")]
    self.brace_pairs = match_braces(self.masked)
    self.functions = find_functions(self.masked, self.brace_pairs)

  def line_of(self, offset: int) -> int:
    return bisect_right(self.line_starts, offset)

  def line_text(self, line: int) -> str:
    if line < 1 or line > len(self.line_starts):
      return ""
    start = self.line_starts[line - 1]
    end = self.text.find("\n", start)
    return self.text[start:] if end < 0 else self.text[start:end]

  def enclosing_block(self, offset: int) -> tuple[int, int] | None:
    candidates = [
        (start, end)
        for start, end in self.brace_pairs.items()
        if start < offset < end
    ]
    if not candidates:
      return None
    return min(candidates, key=lambda pair: pair[1] - pair[0])


def _blank(characters: list[str], start: int, end: int) -> None:
  for index in range(start, min(end, len(characters))):
    if characters[index] != "\n":
      characters[index] = " "


def mask_code(text: str) -> str:
  """Replace comments, literals, and preprocessor lines with spaces."""

  characters = list(text)
  length = len(text)
  index = 0
  while index < length:
    char = text[index]
    next_char = text[index + 1] if index + 1 < length else ""

    if char == "/" and next_char == "/":
      end = text.find("\n", index)
      end = length if end < 0 else end
      _blank(characters, index, end)
      index = end
      continue

    if char == "/" and next_char == "*":
      end = text.find("*/", index + 2)
      end = length if end < 0 else end + 2
      _blank(characters, index, end)
      index = end
      continue

    if char == "R" and next_char == '"':
      delimiter_end = text.find("(", index + 2)
      if delimiter_end < 0:
        _blank(characters, index, length)
        break
      delimiter = text[index + 2 : delimiter_end]
      terminator = ")" + delimiter + '"'
      end = text.find(terminator, delimiter_end + 1)
      end = length if end < 0 else end + len(terminator)
      _blank(characters, index, end)
      index = end
      continue

    if char in {'"', "'"}:
      cursor = index + 1
      while cursor < length:
        if text[cursor] == "\\":
          cursor += 2
          continue
        if text[cursor] == char:
          cursor += 1
          break
        cursor += 1
      _blank(characters, index, cursor)
      index = cursor
      continue

    if char == "#":
      line_start = text.rfind("\n", 0, index) + 1
      if text[line_start:index].strip() == "":
        cursor = index
        while cursor < length:
          newline = text.find("\n", cursor)
          if newline < 0:
            newline = length
          _blank(characters, cursor, newline)
          if newline == length or not text[cursor:newline].rstrip().endswith("\\"):
            cursor = newline
            break
          cursor = newline + 1
        index = cursor
        continue

    index += 1

  return "".join(characters)


def match_braces(masked: str) -> dict[int, int]:
  """Map every matched opening brace to its closing brace."""

  stack: list[int] = []
  pairs: dict[int, int] = {}
  for index, char in enumerate(masked):
    if char == "{":
      stack.append(index)
    elif char == "}" and stack:
      pairs[stack.pop()] = index
  return pairs


def _previous_structural(masked: str, offset: int) -> int:
  return max(masked.rfind(char, 0, offset) for char in ";{}") + 1


def _function_name(header: str) -> str | None:
  """Return the last component of the first function-like name in a header."""

  for match in re.finditer(r"([A-Za-z_~]\w*(?:::[A-Za-z_~]\w*)*)\s*\(", header):
    candidate = match.group(1)
    final = candidate.split("::")[-1].lstrip("~")
    if final and final not in NOT_FUNCTION_NAMES:
      return final
  if re.search(r"\[\s*[&=]?\s*\]", header):
    return "<lambda>"
  return None


def find_functions(masked: str, pairs: dict[int, int]) -> list[FunctionScope]:
  """Find brace bodies that look like function definitions or lambdas."""

  functions: list[FunctionScope] = []
  for body_start, body_end in pairs.items():
    header_start = _previous_structural(masked, body_start)
    header = masked[header_start:body_start].strip()
    if not header or header.startswith("#"):
      continue
    first_word_match = re.match(r"([A-Za-z_]\w*)", header)
    if first_word_match and first_word_match.group(1) in CONTROL_WORDS:
      continue
    if header.startswith(("struct ", "class ", "union ", "enum ", "namespace ")):
      continue
    if header.endswith("="):
      continue
    name = _function_name(header)
    if name is None:
      continue
    functions.append(FunctionScope(name=name, body_start=body_start, body_end=body_end))
  return functions


def _matching_delimiter(masked: str, opening: int, open_char: str, close_char: str) -> int | None:
  depth = 0
  for index in range(opening, len(masked)):
    char = masked[index]
    if char == open_char:
      depth += 1
    elif char == close_char:
      depth -= 1
      if depth == 0:
        return index
  return None


def _split_top_level(text: str, separator: str) -> list[str]:
  """Split on a separator outside (), [], {}, and angle groups."""

  parts: list[str] = []
  start = 0
  depths = {"(": 0, "[": 0, "{": 0, "<": 0}
  closing = {")": "(", "]": "[", "}": "{", ">": "<"}
  index = 0
  while index < len(text):
    char = text[index]
    if char in "([{":
      depths[char] += 1
    elif char in ")]}":
      opener = closing[char]
      depths[opener] = max(0, depths[opener] - 1)
    elif char == "<":
      # Treat '<' as a template group only when it looks like one.
      if index + 1 < len(text) and (text[index + 1].isalpha() or text[index + 1] in "_:"):
        depths["<"] += 1
    elif char == ">":
      if depths["<"] > 0:
        depths["<"] -= 1
    elif char == separator and not any(depths.values()):
      parts.append(text[start:index])
      start = index + 1
    index += 1
  parts.append(text[start:])
  return parts


def _strip_parens(expression: str) -> str:
  expression = expression.strip()
  while expression.startswith("(") and expression.endswith(")"):
    depth = 0
    balanced = True
    for index, char in enumerate(expression):
      if char == "(":
        depth += 1
      elif char == ")":
        depth -= 1
        if depth == 0 and index != len(expression) - 1:
          balanced = False
          break
    if not balanced:
      break
    expression = expression[1:-1].strip()
  return expression


def _split_ternary(expression: str) -> tuple[str, str, str] | None:
  depth = 0
  question = -1
  index = 0
  while index < len(expression):
    char = expression[index]
    if char in "([{":
      depth += 1
    elif char in ")]}":
      depth -= 1
    elif char == "?" and depth == 0:
      question = index
    elif char == ":" and depth == 0 and question >= 0:
      if index > 0 and expression[index - 1] == ":":
        index += 1
        continue
      return expression[:question], expression[question + 1 : index], expression[index + 1 :]
    index += 1
  return None


def _replace_identifiers(expression: str, constants: dict[str, int]) -> str | None:
  def replace(match: re.Match[str]) -> str:
    name = match.group(0)
    if name in {"true", "false"}:
      return "1" if name == "true" else "0"
    if name in constants:
      return str(constants[name])
    raise KeyError(name)

  try:
    return _IDENTIFIER.sub(replace, expression)
  except KeyError:
    return None


def eval_int(expression: str, constants: dict[str, int] | None = None) -> int | None:
  """Evaluate a C++ integer expression made of literals and known constants."""

  constants = constants or {}
  expression = _strip_parens(expression)
  if not expression:
    return None
  ternary = _split_ternary(expression)
  if ternary is not None:
    condition, truthy, falsey = ternary
    values = [eval_int(truthy, constants), eval_int(falsey, constants)]
    if all(value is not None for value in values):
      return max(value for value in values if value is not None)
    return None
  if re.search(r"[A-Za-z_]\w*\s*\(", expression):
    return None
  expression = re.sub(r"\b(0[xX][0-9a-fA-F]+|\d+)[uUlL]+\b", r"\1", expression)
  replaced = _replace_identifiers(expression, constants)
  if replaced is None:
    return None
  if re.search(r"::|->|\.|,|\?|:|\bsizeof\b|\balignof\b", replaced):
    return None
  replaced = replaced.replace("/", "//")
  try:
    parsed = ast.parse(replaced, mode="eval")
  except SyntaxError:
    return None

  allowed_nodes = (
      ast.Expression,
      ast.Constant,
      ast.BinOp,
      ast.UnaryOp,
      ast.Add,
      ast.Sub,
      ast.Mult,
      ast.FloorDiv,
      ast.Mod,
      ast.LShift,
      ast.RShift,
      ast.BitOr,
      ast.BitAnd,
      ast.BitXor,
      ast.USub,
      ast.UAdd,
      ast.Invert,
  )
  for node in ast.walk(parsed):
    if not isinstance(node, allowed_nodes):
      return None
    if isinstance(node, ast.Constant) and not isinstance(node.value, int):
      return None
  try:
    value = eval(compile(parsed, "<capacity>", "eval"), {"__builtins__": {}}, {})
  except (ArithmeticError, ValueError):
    return None
  return value if isinstance(value, int) and not isinstance(value, bool) else None


def _collect_constants(masked_body: str) -> dict[str, int]:
  constants: dict[str, int] = {}
  pattern = re.compile(
      r"\b(?:constexpr|const)\b[^;{}]{0,120}?\b([A-Za-z_]\w*)\s*=\s*([^;{}]+);"
  )
  pending = [(match.group(1), match.group(2)) for match in pattern.finditer(masked_body)]
  for _ in range(4):
    changed = False
    for name, expression in pending:
      if name in constants:
        continue
      value = eval_int(expression, constants)
      if value is not None:
        constants[name] = value
        changed = True
    if not changed:
      break
  return constants


def _capacity(
    expression: str, constants: dict[str, int]
) -> tuple[int, int, str | None, bool | None] | None:
  """Return (low, high, guard, low_when_guard) or None if unresolved."""

  expression = _strip_parens(expression)
  ternary = _split_ternary(expression)
  if ternary is None:
    value = eval_int(expression, constants)
    return None if value is None else (value, value, None, None)
  condition, truthy, falsey = ternary
  condition = _strip_parens(condition)
  truth = eval_int(truthy, constants)
  falsity = eval_int(falsey, constants)
  if truth is None or falsity is None:
    return None
  guard_positive = True
  if condition.startswith("!"):
    condition = _strip_parens(condition[1:])
    guard_positive = False
  if not re.fullmatch(r"[A-Za-z_]\w*", condition):
    return None
  if guard_positive:
    low, high, low_when_guard = truth, falsity, True
  else:
    low, high, low_when_guard = falsity, truth, False
  if low > high:
    low, high = high, low
    low_when_guard = not low_when_guard
  return low, high, condition, low_when_guard


def _condition_truth(condition: str, name: str) -> bool | None:
  """Return the value of name asserted by a simple if condition."""

  condition = _strip_parens(condition)
  patterns_false = (
      rf"!\s*{re.escape(name)}",
      rf"{re.escape(name)}\s*==\s*(?:false|0)\b",
      rf"(?:false|0)\s*==\s*{re.escape(name)}",
      rf"{re.escape(name)}\s*!=\s*(?:true|1)\b",
      rf"(?:true|1)\s*!=\s*{re.escape(name)}",
  )
  patterns_true = (
      rf"{re.escape(name)}",
      rf"{re.escape(name)}\s*==\s*(?:true|1)\b",
      rf"(?:true|1)\s*==\s*{re.escape(name)}",
      rf"{re.escape(name)}\s*!=\s*(?:false|0)\b",
      rf"(?:false|0)\s*!=\s*{re.escape(name)}",
  )
  for pattern in patterns_false:
    if re.fullmatch(pattern, condition):
      return False
  for pattern in patterns_true:
    if re.fullmatch(pattern, condition):
      return True
  return None


def _enclosing_condition_values(
    source: SourceFile, function: FunctionScope, offset: int, name: str
) -> set[bool]:
  values: set[bool] = set()
  masked = source.masked
  search_start = function.body_start
  for match in re.finditer(r"\bif\s*\(", masked[search_start:offset]):
    open_paren = search_start + match.end() - 1
    close_paren = _matching_delimiter(masked, open_paren, "(", ")")
    if close_paren is None or close_paren >= offset:
      continue
    body_open = close_paren + 1
    while body_open < len(masked) and masked[body_open].isspace():
      body_open += 1
    if body_open >= len(masked) or masked[body_open] != "{":
      continue
    body_end = source.brace_pairs.get(body_open)
    if body_end is None or not (body_open < offset < body_end):
      continue
    value = _condition_truth(masked[open_paren + 1 : close_paren], name)
    if value is not None:
      values.add(value)
  return values


def _declared_containers(
    source: SourceFile, function: FunctionScope
) -> Iterator[CapacityEvent]:
  masked = source.masked
  body = masked[function.body_start : function.body_end + 1]
  constants = _collect_constants(body)

  def block_for(offset: int) -> tuple[int, int]:
    block = source.enclosing_block(offset)
    return block if block is not None else (function.body_start, function.body_end)

  for match in re.finditer(r"\bstd::vector\s*<", body):
    angle_open = function.body_start + match.end() - 1
    angle_close = _matching_delimiter(masked, angle_open, "<", ">")
    if angle_close is None:
      continue
    name_match = re.match(r"\s*([A-Za-z_]\w*)\s*\(", masked[angle_close + 1 :])
    if name_match is None:
      continue
    name = name_match.group(1)
    open_paren = angle_close + 1 + name_match.end() - 1
    close_paren = _matching_delimiter(masked, open_paren, "(", ")")
    if close_paren is None:
      continue
    arguments = _split_top_level(masked[open_paren + 1 : close_paren], ",")
    capacity = _capacity(arguments[0], constants)
    if capacity is None:
      continue
    low, high, guard, low_when_guard = capacity
    block_start, block_end = block_for(open_paren)
    yield CapacityEvent(
        name=name,
        offset=close_paren + 1,
        block_start=block_start,
        block_end=block_end,
        low=low,
        high=high,
        guard=guard,
        min_when_guard=low_when_guard,
        kind="std::vector",
    )

  for match in re.finditer(r"\bstd::array\s*<", body):
    angle_open = function.body_start + match.end() - 1
    angle_close = _matching_delimiter(masked, angle_open, "<", ">")
    if angle_close is None:
      continue
    parameters = _split_top_level(masked[angle_open + 1 : angle_close], ",")
    if len(parameters) != 2:
      continue
    capacity = _capacity(parameters[1], constants)
    if capacity is None:
      continue
    name_match = re.match(r"\s*([A-Za-z_]\w*)", masked[angle_close + 1 :])
    if name_match is None:
      continue
    low, high, guard, low_when_guard = capacity
    block_start, block_end = block_for(angle_close)
    yield CapacityEvent(
        name=name_match.group(1),
        offset=angle_close + name_match.end(),
        block_start=block_start,
        block_end=block_end,
        low=low,
        high=high,
        guard=guard,
        min_when_guard=low_when_guard,
        kind="std::array",
    )

  for match in re.finditer(
      r"\b([A-Za-z_]\w*)\s*(?:\.|->)\s*(reserve|resize)\s*\(", body
  ):
    name = match.group(1)
    open_paren = function.body_start + match.end() - 1
    close_paren = _matching_delimiter(masked, open_paren, "(", ")")
    if close_paren is None:
      continue
    arguments = _split_top_level(masked[open_paren + 1 : close_paren], ",")
    if match.group(2) == "reserve":
      # reserve() allocates capacity but leaves the size at zero. Any indexing
      # before resize() or insertion is undefined behaviour, regardless of the
      # reserved count.
      low = high = 0
      guard = low_when_guard = None
    else:
      capacity = _capacity(arguments[0], constants)
      if capacity is None:
        continue
      low, high, guard, low_when_guard = capacity
    block_start, block_end = block_for(open_paren)
    yield CapacityEvent(
        name=name,
        offset=close_paren + 1,
        block_start=block_start,
        block_end=block_end,
        low=low,
        high=high,
        guard=guard,
        min_when_guard=low_when_guard,
        kind=f".{match.group(2)}()",
    )


def _effective_capacity(
    source: SourceFile, function: FunctionScope, event: CapacityEvent, use_offset: int
) -> int:
  if event.guard is None or event.min_when_guard is None:
    return event.low
  proven = _enclosing_condition_values(source, function, use_offset, event.guard)
  if len(proven) == 1:
    value = next(iter(proven))
    return event.low if value == event.min_when_guard else event.high
  return event.low


def check_container_bounds(source: SourceFile) -> list[Finding]:
  findings: list[Finding] = []
  masked = source.masked
  for function in source.functions:
    events = list(_declared_containers(source, function))
    if not events:
      continue
    constants = _collect_constants(
        masked[function.body_start : function.body_end + 1]
    )
    by_name: dict[str, list[CapacityEvent]] = {}
    for event in events:
      by_name.setdefault(event.name, []).append(event)
    for name, name_events in by_name.items():
      name_events.sort(key=lambda event: event.offset)
      pattern = re.compile(
          rf"(?<![\w.>]){re.escape(name)}\s*\[\s*([^\[\]]*?)\s*\]"
      )
      search_start = min(event.offset for event in name_events)
      for match in pattern.finditer(masked, search_start, function.body_end):
        use_offset = match.start()
        block = source.enclosing_block(use_offset)
        block_start, block_end = block if block is not None else (
            function.body_start,
            function.body_end,
        )
        candidates = [
            event
            for event in name_events
            if event.offset < use_offset
            and event.block_start <= block_start
            and block_end <= event.block_end
        ]
        if not candidates:
          continue
        event = max(candidates, key=lambda item: item.offset)
        index = eval_int(match.group(1), constants)
        if index is None:
          continue
        capacity = _effective_capacity(source, function, event, use_offset)
        if index < capacity:
          continue
        findings.append(
            Finding(
                path=source.path,
                line=source.line_of(use_offset),
                severity=ERROR,
                rule=RULE_BOUNDS,
                message=(
                    f"'{name}[{index}]' is outside the {event.kind} capacity {capacity}; "
                    + (
                        "reserve() does not create elements"
                        if event.kind == ".reserve()"
                        else "the container is smaller than the index"
                    )
                ),
            )
        )
  return _deduplicate(findings)


def _range_variable(declaration: str) -> str | None:
  declaration = declaration.strip()
  declaration = re.sub(r"[&*]\s*$", "", declaration).strip()
  match = re.search(r"([A-Za-z_]\w*)\s*$", declaration)
  if match is None:
    return None
  name = match.group(1)
  return None if name in CONTROL_WORDS or name in {"auto", "const"} else name


def _top_level_colon(text: str) -> int | None:
  depth = 0
  for index, char in enumerate(text):
    if char in "([{":
      depth += 1
    elif char in ")]}":
      depth -= 1
    elif char == ":" and depth == 0:
      if index + 1 < len(text) and text[index + 1] == ":":
        continue
      if index > 0 and text[index - 1] == ":":
        continue
      return index
  return None


def _entry_path_target(target: str, variable: str) -> bool:
  target = target.strip()
  variable = re.escape(variable)
  return bool(
      re.fullmatch(rf"{variable}\s*(?:\.|->)\s*path\s*\(\s*\)", target)
      or re.fullmatch(variable, target)
  )


def _derived_entry_target(target: str, variable: str) -> bool:
  target = target.strip()
  variable = re.escape(variable)
  return bool(re.match(rf"^{variable}\s*(?:\.|->)\s*path\s*\(", target))


def check_filesystem_remove(source: SourceFile) -> list[Finding]:
  findings: list[Finding] = []
  masked = source.masked
  unqualified_allowed = bool(re.search(r"\busing\s+namespace\s+std::filesystem\s*;", masked))
  for match in re.finditer(r"\bfor\s*\(", masked):
    open_paren = match.end() - 1
    close_paren = _matching_delimiter(masked, open_paren, "(", ")")
    if close_paren is None:
      continue
    header = masked[open_paren + 1 : close_paren]
    if "directory_iterator" not in header:
      continue
    variable: str | None = None
    colon = _top_level_colon(header)
    if colon is not None:
      variable = _range_variable(header[:colon])
    else:
      # Classic loop: for (directory_iterator it(directory), end; it != end; ...)
      declaration = _split_top_level(header, ";")[0]
      iterator_match = re.search(
          r"directory_iterator\s+([A-Za-z_]\w*)", declaration
      )
      if iterator_match is None:
        iterator_match = re.search(
            r"([A-Za-z_]\w*)\s*=\s*[A-Za-z_:]*directory_iterator\s*\(",
            declaration,
        )
      if iterator_match is not None:
        variable = iterator_match.group(1)
    if variable is None:
      continue
    body_open = close_paren + 1
    while body_open < len(masked) and masked[body_open].isspace():
      body_open += 1
    if body_open >= len(masked) or masked[body_open] != "{":
      continue
    body_end = source.brace_pairs.get(body_open)
    if body_end is None:
      continue
    body = masked[body_open + 1 : body_end]
    filter_pattern = re.compile(
        rf"(?:\b{re.escape(variable)}\s*(?:\.|->)\s*is_(?:regular_file|directory)\s*\("
        rf"|is_(?:regular_file|directory)\s*\(\s*{re.escape(variable)}\b)"
    )
    for removal in _REMOVE_CALL.finditer(body):
      if removal.group("qualifier") is None and not unqualified_allowed:
        continue
      if filter_pattern.search(body[: removal.start()]):
        continue
      call_open = body_open + 1 + removal.end() - 1
      call_close = _matching_delimiter(masked, call_open, "(", ")")
      if call_close is None:
        continue
      target = _split_top_level(masked[call_open + 1 : call_close], ",")[0]
      direct = _entry_path_target(target, variable)
      derived = not direct and _derived_entry_target(target, variable)
      severity = ERROR if direct else WARN
      if direct:
        detail = (
            "removes the directory_iterator entry without an "
            "is_regular_file/is_directory check; an empty directory can be removed"
        )
      elif derived:
        detail = (
            "removes a path derived from the directory_iterator entry without an "
            "is_regular_file/is_directory check"
        )
      else:
        detail = (
            "remove() inside a directory_iterator loop has no visible "
            "is_regular_file/is_directory filter for the entry"
        )
      findings.append(
          Finding(
              path=source.path,
              line=source.line_of(call_open),
              severity=severity,
              rule=RULE_FILESYSTEM,
              message=detail,
          )
      )
  return _deduplicate(findings)


def _is_ignored(path: Path, root: Path) -> bool:
  try:
    parts = path.relative_to(root).parts
  except ValueError:
    parts = path.parts
  return any(part in IGNORED_DIRS for part in parts)


def _type_index(root: Path, extra_files: Sequence[Path]) -> dict[str, set[int]]:
  widths: dict[str, set[int]] = {}
  candidates: list[Path] = []
  for directory in ("include", "lib", "src", "sim", "tests"):
    base = root / directory
    if base.is_dir():
      candidates.extend(
          path
          for path in base.rglob("*")
          if path.suffix in HEADER_SUFFIXES and not _is_ignored(path, root)
      )
  candidates.extend(path for path in extra_files if path.suffix in HEADER_SUFFIXES)
  for path in {path.resolve() for path in candidates}:
    try:
      text = path.read_text(encoding="utf-8", errors="replace")
    except OSError:
      continue
    for match in _TYPE_DECL.finditer(mask_code(text)):
      width = _INT_TYPES.get(match.group(1))
      if width is not None:
        widths.setdefault(match.group(2), set()).add(width)
  return widths


def _hex_value(literal: str) -> int:
  return int(literal, 16)


def check_low_byte_sentinel(
    source: SourceFile, type_index: dict[str, set[int]]
) -> list[Finding]:
  findings: list[Finding] = []
  fixture_bytes = {
      _hex_value(match.group(1)) for match in _FIXTURE_BYTE_WRITE.finditer(source.masked)
  }
  if not fixture_bytes:
    return findings
  for pattern in (_HEX_COMPARE_LEFT, _HEX_COMPARE_RIGHT):
    for match in pattern.finditer(source.masked):
      literal = match.group("lit")
      digits = literal[2:]
      value = _hex_value(literal)
      if value > 0xFF or len(digits) > 2:
        continue
      if value not in fixture_bytes:
        continue
      left = match.group("left").strip()
      field_name = re.split(r"(?:\.|->)", left)[-1].strip()
      widths = type_index.get(field_name)
      if not widths or len(widths) != 1:
        continue
      width = next(iter(widths))
      if width <= 8:
        continue
      line = source.line_of(match.start())
      context = source.line_text(line - 1) + "\n" + source.line_text(line)
      if not _ASSERTION_CONTEXT.search(context):
        continue
      findings.append(
          Finding(
              path=source.path,
              line=line,
              severity=WARN,
              rule=RULE_LOW_BYTE,
              message=(
                  f"'{left}' is a {width}-bit field but the assertion compares it with "
                  f"{literal}; the fixture writes one byte, so the upper bytes are "
                  "unasserted. Assert the full-width value or cast deliberately"
              ),
          )
      )
  return _deduplicate(findings)


def _enum_states(masked: str) -> set[str]:
  states: set[str] = set()
  for match in _ENUM_BLOCK.finditer(masked):
    for item in _split_top_level(match.group(2), ","):
      item = item.split("=", 1)[0].strip()
      if re.fullmatch(r"[A-Za-z_]\w*", item):
        states.add(item)
  return states


def _if_blocks(
    source: SourceFile, start: int, end: int
) -> Iterator[tuple[int, int, int, int, int]]:
  """Yield (if_start, condition_start, condition_end, body_start, body_end)."""

  masked = source.masked
  for match in re.finditer(r"\bif\s*\(", masked[start:end]):
    if_start = start + match.start()
    open_paren = start + match.end() - 1
    close_paren = _matching_delimiter(masked, open_paren, "(", ")")
    if close_paren is None or close_paren >= end:
      continue
    body_open = close_paren + 1
    while body_open < len(masked) and masked[body_open].isspace():
      body_open += 1
    if body_open >= len(masked):
      continue
    if masked[body_open] == "{":
      body_end = source.brace_pairs.get(body_open)
      if body_end is not None:
        yield if_start, open_paren + 1, close_paren, body_open, body_end
      continue
    semicolon = masked.find(";", body_open)
    if semicolon >= 0:
      yield if_start, open_paren + 1, close_paren, body_open, semicolon


def _guarded_before(
    source: SourceFile, function: FunctionScope, call_offset: int, state: str
) -> bool:
  state_pattern = re.compile(rf"\b{re.escape(state)}\b")
  call_block = source.enclosing_block(call_offset)
  call_block_start, call_block_end = (
      call_block if call_block is not None else (function.body_start, function.body_end)
  )
  for (
      if_start,
      condition_start,
      condition_end,
      body_start,
      body_end,
  ) in _if_blocks(source, function.body_start, call_offset):
    # The guard must dominate the call. A guard nested in a sibling branch does
    # not, and neither does a guard whose body contains the call.
    if body_start <= call_offset <= body_end:
      continue
    if body_end >= call_offset:
      continue
    parent = source.enclosing_block(if_start)
    parent_start, parent_end = (
        parent if parent is not None else (function.body_start, function.body_end)
    )
    if not (parent_start <= call_block_start and call_block_end <= parent_end):
      continue
    condition = source.masked[condition_start:condition_end]
    if not state_pattern.search(condition):
      continue
    body = source.masked[condition_end + 1 : body_end]
    if re.search(r"\b(?:return|continue)\b", body):
      return True
  return False


def check_terminal_state_acquire(source: SourceFile, options: ScanOptions) -> list[Finding]:
  findings: list[Finding] = []
  states = _enum_states(source.masked)
  for state in options.terminal_states:
    if re.search(rf"\b[A-Za-z_]\w*::{re.escape(state)}\b", source.masked):
      states.add(state)
  terminal = tuple(state for state in options.terminal_states if state in states)
  if not terminal:
    return findings
  try:
    acquire_pattern = re.compile(options.acquire_regex)
  except re.error:
    acquire_pattern = re.compile(DEFAULT_ACQUIRE_REGEX)
  acquire_names = set(options.acquire_functions)
  call_names = acquire_names | set(DEFAULT_ACQUIRE_FUNCTIONS)
  for function in source.functions:
    if function.name not in acquire_names and not acquire_pattern.search(function.name):
      continue
    body = source.masked[function.body_start : function.body_end + 1]
    for match in re.finditer(r"\b([A-Za-z_]\w*)\s*\(", body):
      name = match.group(1)
      if name not in call_names and not acquire_pattern.search(name):
        continue
      call_offset = function.body_start + match.start()
      missing = [
          state
          for state in terminal
          if not _guarded_before(source, function, call_offset, state)
      ]
      if not missing:
        continue
      findings.append(
          Finding(
              path=source.path,
              line=source.line_of(call_offset),
              severity=WARN,
              rule=RULE_TERMINAL,
              message=(
                  f"{function.name}() calls {name}() without an early return for "
                  f"terminal state(s) {', '.join(missing)}; an async event serviced "
                  "after the terminal transition can re-acquire the resource"
              ),
          )
      )
  return _deduplicate(findings)


def _deduplicate(findings: Iterable[Finding]) -> list[Finding]:
  unique: dict[tuple[Path, int, str, str], Finding] = {}
  for finding in findings:
    unique.setdefault(
        (finding.path, finding.line, finding.rule, finding.message), finding
    )
  return list(unique.values())


def scan_files(
    root: Path, paths: Sequence[Path], options: ScanOptions | None = None
) -> list[Finding]:
  """Scan the given source files and return sorted findings."""

  options = options or ScanOptions()
  resolved = [path if path.is_absolute() else root / path for path in paths]
  if options.type_index is None:
    type_index = _type_index(root, resolved)
  else:
    type_index = options.type_index
  findings: list[Finding] = []
  for path in resolved:
    try:
      text = path.read_text(encoding="utf-8", errors="replace")
    except OSError as error:
      findings.append(
          Finding(
              path=path,
              line=1,
              severity=ERROR,
              rule="input",
              message=f"cannot read file: {error}",
          )
      )
      continue
    source = SourceFile(path, text)
    if RULE_BOUNDS in options.rules:
      findings.extend(check_container_bounds(source))
    if RULE_FILESYSTEM in options.rules:
      findings.extend(check_filesystem_remove(source))
    if RULE_LOW_BYTE in options.rules:
      findings.extend(check_low_byte_sentinel(source, type_index))
    if RULE_TERMINAL in options.rules:
      findings.extend(check_terminal_state_acquire(source, options))
  findings.sort(key=lambda finding: (str(finding.path), finding.line, finding.rule))
  return findings


def _git(root: Path, arguments: Sequence[str]) -> subprocess.CompletedProcess[str]:
  return subprocess.run(
      ["git", *arguments],
      cwd=root,
      capture_output=True,
      text=True,
      check=False,
  )


def _parse_diff_paths(diff_text: str) -> list[str]:
  paths: list[str] = []
  for line in diff_text.splitlines():
    if not line.startswith("+++ "):
      continue
    name = line[4:].strip().split("\t", 1)[0]
    if not name or name == "/dev/null":
      continue
    if name.startswith('"') and name.endswith('"'):
      try:
        name = ast.literal_eval(name)
      except (SyntaxError, ValueError):
        name = name[1:-1]
    if name.startswith("b/"):
      name = name[2:]
    if name not in paths:
      paths.append(name)
  return paths


def _changed_files(args: argparse.Namespace, root: Path) -> list[Path]:
  candidates: list[Path] = []
  if args.diff_file is not None:
    try:
      diff_text = args.diff_file.read_text(encoding="utf-8", errors="replace")
    except OSError as error:
      raise SystemExit(f"cannot read diff file {args.diff_file}: {error}")
    candidates = [root / name for name in _parse_diff_paths(diff_text)]
  elif args.base:
    range_spec = args.base if ".." in args.base else f"{args.base}...{args.head}"
    result = _git(root, ["diff", "--name-only", "--diff-filter=ACMR", range_spec])
    if result.returncode != 0:
      raise SystemExit(f"git diff failed: {result.stderr.strip()}")
    candidates = [root / name for name in result.stdout.splitlines() if name]
  elif args.paths:
    for path in args.paths:
      candidate = path if path.is_absolute() else root / path
      if candidate.is_dir():
        candidates.extend(
            child
            for child in candidate.rglob("*")
            if child.is_file()
            and child.suffix in CPP_SUFFIXES
            and not _is_ignored(child, root)
        )
      elif candidate.is_file():
        candidates.append(candidate)
      else:
        raise SystemExit(f"path does not exist: {path}")
  elif args.all:
    if (root / ".git").exists():
      result = _git(root, ["ls-files", "-z"])
      if result.returncode != 0:
        raise SystemExit(f"git ls-files failed: {result.stderr.strip()}")
      candidates = [
          root / name for name in result.stdout.split("\0") if name
      ]
    else:
      candidates = [
          path
          for path in root.rglob("*")
          if path.is_file()
          and path.suffix in CPP_SUFFIXES
          and not _is_ignored(path, root)
      ]
  else:
    raise SystemExit(
        "select files with --base <ref>, --diff-file <patch>, --all, or explicit paths"
    )

  selected: list[Path] = []
  seen: set[Path] = set()
  for candidate in candidates:
    if candidate.suffix not in CPP_SUFFIXES:
      continue
    if not candidate.is_file():
      if args.diff_file is None and not args.all:
        raise SystemExit(f"selected file does not exist: {candidate}")
      continue
    resolved = candidate.resolve()
    if resolved in seen:
      continue
    seen.add(resolved)
    selected.append(resolved)
  selected.sort()
  return selected


def build_parser() -> argparse.ArgumentParser:
  parser = argparse.ArgumentParser(
      description=(
          "Heuristic C++ checks for the review regressions that clang-tidy and "
          "cppcheck are not installed to catch locally."
      ),
      formatter_class=argparse.RawDescriptionHelpFormatter,
      epilog=__doc__,
  )
  parser.add_argument("paths", nargs="*", type=Path, help="explicit files or directories, relative to --root")
  parser.add_argument(
      "--root",
      type=Path,
      default=Path(__file__).resolve().parents[1],
      help="repository root (default: inferred from this script)",
  )
  parser.add_argument("--base", help="git base revision or range, for example origin/master")
  parser.add_argument("--head", default="HEAD", help="git head revision (default: HEAD)")
  parser.add_argument("--diff-file", type=Path, help="saved unified diff to inspect")
  parser.add_argument("--all", action="store_true", help="scan every tracked C++ file")
  parser.add_argument(
      "--strict",
      action="store_true",
      help="exit non-zero on warnings as well as errors",
  )
  parser.add_argument(
      "--disable",
      action="append",
      choices=ALL_RULES,
      default=[],
      metavar="RULE",
      help="disable one rule (repeatable)",
  )
  parser.add_argument(
      "--terminal-states",
      default=",".join(DEFAULT_TERMINAL_STATES),
      help="terminal enum states for the acquisition rule",
  )
  parser.add_argument(
      "--acquire-functions",
      default=",".join(DEFAULT_ACQUIRE_FUNCTIONS),
      help=(
          "acquisition entry points to inspect and acquisition calls to match "
          "for the terminal-state rule"
      ),
  )
  parser.add_argument(
      "--acquire-regex",
      default=DEFAULT_ACQUIRE_REGEX,
      help="extra acquisition function pattern for the terminal-state rule",
  )
  parser.add_argument(
      "--no-repo-types",
      action="store_true",
      help="do not build the field-width index from repository headers",
  )
  parser.add_argument("--quiet", action="store_true", help="print only findings")
  return parser


def main(argv: Sequence[str] | None = None) -> int:
  parser = build_parser()
  args = parser.parse_args(argv)
  try:
    re.compile(args.acquire_regex)
  except re.error as error:
    parser.error(f"invalid --acquire-regex: {error}")
  root = args.root.resolve()
  if not root.is_dir():
    parser.error(f"root is not a directory: {root}")
  try:
    paths = _changed_files(args, root)
  except SystemExit as error:
    print(error, file=sys.stderr)
    return 2
  if not paths:
    if not args.quiet:
      print("no C++ files to check")
    return 0
  rules = frozenset(rule for rule in ALL_RULES if rule not in set(args.disable))
  terminal_states = tuple(
      state.strip() for state in args.terminal_states.split(",") if state.strip()
  )
  acquire_functions = tuple(
      name.strip() for name in args.acquire_functions.split(",") if name.strip()
  )
  type_index = {} if args.no_repo_types else None
  options = ScanOptions(
      rules=rules,
      terminal_states=terminal_states,
      acquire_functions=acquire_functions,
      acquire_regex=args.acquire_regex,
      type_index=type_index,
  )
  findings = scan_files(root, paths, options)
  for finding in findings:
    print(finding.format(root))
  errors = sum(finding.severity == ERROR for finding in findings)
  warnings = sum(finding.severity == WARN for finding in findings)
  if not args.quiet:
    print(
        f"checked {len(paths)} file(s): {errors} error(s), {warnings} warning(s)"
    )
  if errors or (warnings and args.strict):
    return 1
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
