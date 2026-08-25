#!/usr/bin/env python3
"""Check that pull-request validation workflows are safe for stacked PRs.

The checker intentionally has no third-party YAML dependency. It parses the
small workflow-trigger subset needed here while preserving GitHub's treatment
of the YAML 1.1-looking key ``on`` as a string.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import json
from pathlib import Path
import sys


@dataclass(frozen=True)
class MappingEntry:
  path: tuple[str, ...]
  key: str
  value: str


@dataclass(frozen=True)
class EventBlock:
  """A trigger event and its direct configuration keys."""

  keys: frozenset[str]


def _strip_comment(value: str) -> str:
  quote = ""
  escaped = False
  for index, char in enumerate(value):
    if quote == '"' and escaped:
      escaped = False
      continue
    if quote == '"' and char == "\\":
      escaped = True
      continue
    if char in "'\"":
      if not quote:
        quote = char
      elif quote == char:
        quote = ""
    elif char == "#" and not quote and (
        index == 0 or value[index - 1].isspace()
    ):
      return value[:index].rstrip()
  return value.rstrip()


def _decode_key(value: str) -> str:
  value = value.strip()
  if len(value) >= 2 and value[0] == value[-1]:
    if value[0] == "'":
      return value[1:-1].replace("''", "'")
    if value[0] == '"':
      try:
        decoded = json.loads(value)
      except json.JSONDecodeError:
        return value[1:-1]
      return decoded if isinstance(decoded, str) else value
  return value


def _key_line(line: str) -> tuple[int, str, str] | None:
  """Parse one plain/quoted YAML mapping key line.

  Workflow keys are simple names, but locating the colon outside quotes also
  handles quoted ``on`` and event/config keys without depending on indentation
  being exactly two or four spaces.
  """

  indent = len(line) - len(line.lstrip(" "))
  if "\t" in line[:indent]:
    return None
  content = _strip_comment(line[indent:]).strip()
  if not content or content.startswith("-"):
    return None
  quote = ""
  escaped = False
  colon = None
  for index, char in enumerate(content):
    if quote == '"' and escaped:
      escaped = False
      continue
    if quote == '"' and char == "\\":
      escaped = True
      continue
    if char in "'\"":
      if not quote:
        quote = char
      elif quote == char:
        quote = ""
    elif char == ":" and not quote:
      if index + 1 == len(content) or content[index + 1].isspace():
        colon = index
        break
  if colon is None:
    return None
  return indent, _decode_key(content[:colon]), content[colon + 1 :].strip()


def _split_flow(value: str) -> list[str]:
  """Split a flow collection at commas outside nested collections/quotes."""

  result: list[str] = []
  start = 0
  quote = ""
  escaped = False
  depth = 0
  for index, char in enumerate(value):
    if quote == '"' and escaped:
      escaped = False
      continue
    if quote == '"' and char == "\\":
      escaped = True
      continue
    if char in "'\"":
      if not quote:
        quote = char
      elif quote == char:
        quote = ""
    elif not quote and char in "[{":
      depth += 1
    elif not quote and char in "]}":
      depth -= 1
    elif not quote and char == "," and depth == 0:
      result.append(value[start:index].strip())
      start = index + 1
  result.append(value[start:].strip())
  return [item for item in result if item]


def _split_flow_colon(value: str) -> tuple[str, str] | None:
  quote = ""
  escaped = False
  depth = 0
  for index, char in enumerate(value):
    if quote == '"' and escaped:
      escaped = False
      continue
    if quote == '"' and char == "\\":
      escaped = True
      continue
    if char in "'\"":
      if not quote:
        quote = char
      elif quote == char:
        quote = ""
    elif not quote and char in "[{":
      depth += 1
    elif not quote and char in "]}":
      depth -= 1
    elif char == ":" and not quote and depth == 0:
      return value[:index], value[index + 1 :]
  return None


def _flow_pairs(value: str) -> list[tuple[str, str]]:
  value = value.strip()
  if not (value.startswith("{") and value.endswith("}")):
    return []
  pairs: list[tuple[str, str]] = []
  for item in _split_flow(value[1:-1]):
    parts = _split_flow_colon(item)
    if parts is not None:
      key, nested = parts
      pairs.append((_decode_key(key), nested.strip()))
  return pairs


def _block_entries(
    lines: list[str], trigger_index: int, trigger_key: str
) -> list[MappingEntry]:
  entries: list[MappingEntry] = []
  stack: list[tuple[int, str]] = [(0, trigger_key)]
  block_scalar_indent: int | None = None
  for line in lines[trigger_index + 1 :]:
    if not line.strip():
      continue
    indent = len(line) - len(line.lstrip(" "))
    if indent == 0:
      break
    if block_scalar_indent is not None:
      if indent > block_scalar_indent:
        continue
      block_scalar_indent = None
    parsed = _key_line(line)
    if parsed is None:
      continue
    indent, key, value = parsed
    while stack and indent <= stack[-1][0]:
      stack.pop()
    path = tuple(item[1] for item in stack)
    entries.append(MappingEntry(path, key, value))
    if value in ("|", ">", "|-", ">-", "|+", ">+"):
      block_scalar_indent = indent
    elif not value:
      stack.append((indent, key))
  return entries


def _all_entries(lines: list[str]) -> list[MappingEntry]:
  """Collect mapping entries in the workflow using their real nesting."""

  entries: list[MappingEntry] = []
  stack: list[tuple[int, str]] = []
  block_scalar_indent: int | None = None
  for line in lines:
    if not line.strip():
      continue
    indent = len(line) - len(line.lstrip(" "))
    if block_scalar_indent is not None:
      if indent > block_scalar_indent:
        continue
      block_scalar_indent = None
    parsed = _key_line(line)
    if parsed is None:
      continue
    indent, key, value = parsed
    while stack and indent <= stack[-1][0]:
      stack.pop()
    entries.append(MappingEntry(tuple(item[1] for item in stack), key, value))
    if value in ("|", ">", "|-", ">-", "|+", ">+"):
      block_scalar_indent = indent
    elif not value:
      stack.append((indent, key))
  return entries


def event_blocks(lines: list[str]) -> dict[str, EventBlock]:
  """Return trigger events using structural indentation and flow parsing."""

  trigger_index = None
  trigger_value = ""
  for index, line in enumerate(lines):
    parsed = _key_line(line)
    if parsed is not None and parsed[0] == 0 and parsed[1] == "on":
      trigger_index = index
      trigger_value = parsed[2]
      break
  if trigger_index is None:
    return {}

  blocks: dict[str, EventBlock] = {}
  if trigger_value.startswith("[") and trigger_value.endswith("]"):
    for item in _split_flow(trigger_value[1:-1]):
      blocks[_decode_key(item)] = EventBlock(frozenset())
    return blocks
  if trigger_value.startswith("{"):
    for event, config in _flow_pairs(trigger_value):
      blocks[event] = EventBlock(
          frozenset(key for key, _ in _flow_pairs(config))
      )
    return blocks

  for entry in _block_entries(lines, trigger_index, "on"):
    if len(entry.path) == 1 and entry.path[0] == "on":
      blocks.setdefault(entry.key, EventBlock(frozenset()))
    elif len(entry.path) == 2 and entry.path[0] == "on":
      event = entry.path[1]
      current = blocks.get(event, EventBlock(frozenset()))
      blocks[event] = EventBlock(current.keys | {entry.key})
  return blocks


def _workflow_paths(workflow_root: Path) -> list[Path]:
  return sorted(
      [*workflow_root.glob("*.yml"), *workflow_root.glob("*.yaml")]
  )


def lint_workflow(path: Path) -> list[str]:
  lines = path.read_text(encoding="utf-8").splitlines()
  blocks = event_blocks(lines)
  errors: list[str] = []
  if "pull_request" not in blocks:
    return errors
  pull_request = blocks["pull_request"]
  if {"branches", "branches-ignore"} & pull_request.keys:
    errors.append("pull_request has a base-branch filter")
  if not {"paths", "paths-ignore"} & pull_request.keys:
    errors.append("pull_request has no path filter")
  if "workflow_dispatch" not in blocks:
    errors.append("has no workflow_dispatch trigger")
  write_permission = False
  for entry in _all_entries(lines):
    if entry.key == "pull-requests" and _decode_key(entry.value) == "write":
      write_permission = True
    if entry.key == "permissions":
      if any(
          key == "pull-requests" and _decode_key(value) == "write"
          for key, value in _flow_pairs(entry.value)
      ):
        write_permission = True
  if write_permission:
    errors.append("requests pull-requests: write for pull_request runs")
  return errors


def main() -> int:
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument(
      "--root",
      type=Path,
      default=Path(__file__).resolve().parents[1],
      help="repository root (default: inferred from this script)",
  )
  args = parser.parse_args()
  workflow_root = args.root / ".github" / "workflows"
  failures: list[str] = []
  checked = 0
  for path in _workflow_paths(workflow_root):
    if "pull_request" not in event_blocks(
        path.read_text(encoding="utf-8").splitlines()
    ):
      continue
    checked += 1
    for error in lint_workflow(path):
      failures.append(f"{path.name}: {error}")
  if failures:
    print("CI workflow trigger check failed:", file=sys.stderr)
    for failure in failures:
      print(f"- {failure}", file=sys.stderr)
    return 1
  print(
      "CI workflow trigger check passed for "
      f"{checked} stacked-PR/manual-dispatch workflows."
  )
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
