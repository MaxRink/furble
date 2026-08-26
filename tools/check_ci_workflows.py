#!/usr/bin/env python3
"""Check that pull-request validation workflows are safe for stacked PRs.

The checker uses PyYAML's structural loader instead of matching YAML text.
``BaseLoader`` deliberately leaves scalar values as strings, so the YAML 1.1
loader quirk that turns an unquoted ``on`` key into ``True`` cannot hide a
workflow trigger. Anchors, aliases, and multiline flow collections are
resolved by the loader before policy checks run.
"""

from __future__ import annotations

import argparse
from collections.abc import Mapping, Sequence
from dataclasses import dataclass
from pathlib import Path
import re
import sys
from typing import Any

try:
  import yaml
except ImportError as error:  # pragma: no cover - exercised in CI setup
  raise SystemExit(
      "PyYAML is required by check_ci_workflows.py; install "
      ".github/workflows/ci-checker-requirements.txt"
  ) from error


class WorkflowParseError(ValueError):
  """A workflow could not be parsed as one YAML mapping document."""


class WorkflowLoader(yaml.BaseLoader):
  """BaseLoader with duplicate mapping keys rejected explicitly."""


def _construct_mapping(
    loader: WorkflowLoader, node: yaml.MappingNode, deep: bool = False
) -> dict[str, Any]:
  if not isinstance(node, yaml.MappingNode):
    raise yaml.constructor.ConstructorError(
        None, None, "expected a mapping node", node.start_mark
    )
  mapping: dict[str, Any] = {}
  merged: list[Mapping[str, Any]] = []
  for key_node, value_node in node.value:
    key = loader.construct_object(key_node, deep=deep)
    if not isinstance(key, str):
      raise yaml.constructor.ConstructorError(
          "while constructing a mapping",
          node.start_mark,
          "workflow mapping keys must be strings",
          key_node.start_mark,
      )
    if key == "<<":
      merge_value = loader.construct_object(value_node, deep=deep)
      merge_values = (
          merge_value
          if isinstance(merge_value, Sequence)
          and not isinstance(merge_value, (str, bytes))
          else [merge_value]
      )
      if not all(isinstance(item, Mapping) for item in merge_values):
        raise yaml.constructor.ConstructorError(
            "while constructing a mapping",
            node.start_mark,
            "workflow merge aliases must refer to mappings",
            value_node.start_mark,
        )
      merged.extend(merge_values)
      continue
    if key in mapping:
      raise yaml.constructor.ConstructorError(
          "while constructing a mapping",
          node.start_mark,
          f"found duplicate workflow key {key!r}",
          key_node.start_mark,
      )
    mapping[key] = loader.construct_object(value_node, deep=deep)
  for merge in reversed(merged):
    for key, value in merge.items():
      mapping.setdefault(key, value)
  return mapping


WorkflowLoader.add_constructor(
    yaml.resolver.BaseResolver.DEFAULT_MAPPING_TAG, _construct_mapping
)


@dataclass(frozen=True)
class EventBlock:
  """A trigger event and its direct configuration keys."""

  keys: frozenset[str]


def _load_text(text: str) -> dict[str, Any]:
  try:
    document = yaml.load(text, Loader=WorkflowLoader)
  except yaml.YAMLError as error:
    raise WorkflowParseError(f"YAML parse failed: {error}") from error
  if not isinstance(document, dict):
    raise WorkflowParseError("workflow document must be a mapping")
  return document


def _load_workflow(path: Path) -> dict[str, Any]:
  try:
    text = path.read_text(encoding="utf-8")
  except OSError as error:
    raise WorkflowParseError(f"cannot read workflow: {error}") from error
  return _load_text(text)


def _document_from_source(
    source: Mapping[str, Any] | Sequence[str]
) -> dict[str, Any]:
  if isinstance(source, Mapping):
    return dict(source)
  return _load_text("\n".join(source))


def _event_blocks(document: Mapping[str, Any]) -> dict[str, EventBlock]:
  trigger = document.get("on")
  blocks: dict[str, EventBlock] = {}
  if isinstance(trigger, str):
    # GitHub accepts the shorthand ``on: pull_request`` form in addition to
    # the sequence and mapping forms below. Treat it as one configured event
    # so the same stacked-PR and security policy applies to every syntax.
    if trigger:
      blocks[trigger] = EventBlock(frozenset())
  elif isinstance(trigger, Mapping):
    for event, configuration in trigger.items():
      if not isinstance(event, str):
        continue
      keys = (
          frozenset(key for key in configuration if isinstance(key, str))
          if isinstance(configuration, Mapping)
          else frozenset()
      )
      blocks[event] = EventBlock(keys)
  elif isinstance(trigger, Sequence) and not isinstance(trigger, (str, bytes)):
    for event in trigger:
      if isinstance(event, str):
        blocks[event] = EventBlock(frozenset())
  return blocks


def event_blocks(
    source: Mapping[str, Any] | Sequence[str]
) -> dict[str, EventBlock]:
  """Return trigger events using YAML structure, including aliases."""

  return _event_blocks(_document_from_source(source))


def _permission_requests_write(document: Mapping[str, Any]) -> bool:
  """Return whether a semantic permissions mapping grants PR write access."""

  def visit(value: Any) -> bool:
    if isinstance(value, Mapping):
      for key, nested in value.items():
        if key == "permissions":
          if nested == "write-all":
            return True
          if isinstance(nested, Mapping) and nested.get("pull-requests") in (
              "write",
              "write-all",
          ):
            return True
        if visit(nested):
          return True
    elif isinstance(value, Sequence) and not isinstance(value, (str, bytes)):
      return any(visit(item) for item in value)
    return False

  return visit(document)


def _lint_document(document: Mapping[str, Any]) -> list[str]:
  blocks = _event_blocks(document)
  errors: list[str] = []
  if "pull_request_target" in blocks:
    errors.append("uses pull_request_target")
  if "pull_request" not in blocks:
    return errors
  pull_request = blocks["pull_request"]
  if {"branches", "branches-ignore"} & pull_request.keys:
    errors.append("pull_request has a base-branch filter")
  if not {"paths", "paths-ignore"} & pull_request.keys:
    errors.append("pull_request has no path filter")
  if "workflow_dispatch" not in blocks:
    errors.append("has no workflow_dispatch trigger")
  if _permission_requests_write(document):
    errors.append("requests pull-requests: write for pull_request runs")
  errors.extend(_lint_firmware_change_filter(document))
  return errors


def _lint_firmware_change_filter(document: Mapping[str, Any]) -> list[str]:
  """Check the optional firmware path filter used by the main workflow."""

  jobs = document.get("jobs")
  if not isinstance(jobs, Mapping):
    return []
  changes = jobs.get("changes")
  if not isinstance(changes, Mapping):
    return []
  steps = changes.get("steps")
  if not isinstance(steps, Sequence) or isinstance(steps, (str, bytes)):
    return []
  filter_step = next(
      (
          step
          for step in steps
          if isinstance(step, Mapping) and step.get("id") == "filter"
      ),
      None,
  )
  if not isinstance(filter_step, Mapping):
    return []
  script = filter_step.get("run")
  if not isinstance(script, str):
    return ["firmware change filter has no shell script"]

  errors: list[str] = []
  dispatch_full_build = re.search(
      r'if\s+\[\[\s+"\$EVENT_NAME"\s+==\s+"workflow_dispatch"\s+\]\]'
      r"[\s\S]*?firmware_changed=true",
      script,
  )
  if dispatch_full_build is None:
    errors.append("firmware change filter does not fully build manual dispatches")
  if "rev-list --max-parents=0" in script:
    errors.append("firmware change filter can produce multiple base SHAs")
  return errors


def _workflow_paths(workflow_root: Path) -> list[Path]:
  return sorted(
      [*workflow_root.glob("*.yml"), *workflow_root.glob("*.yaml")]
  )


def lint_workflow(path: Path) -> list[str]:
  """Lint one workflow, raising WorkflowParseError on invalid YAML."""

  return _lint_document(_load_workflow(path))


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
    try:
      document = _load_workflow(path)
    except WorkflowParseError as error:
      failures.append(f"{path.name}: {error}")
      continue
    blocks = _event_blocks(document)
    if "pull_request" in blocks:
      checked += 1
    failures.extend(
        f"{path.name}: {error}" for error in _lint_document(document)
    )
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
