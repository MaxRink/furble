#!/usr/bin/env python3
"""Check that validation workflows run for stacked pull requests safely.

This intentionally uses only the Python standard library. GitHub Actions treats
the pull request base branch as a trigger filter, so a ``branches: [master]``
filter silently drops a pull request stacked on another feature branch. The
validation workflows instead use path filters and can also be started against
any branch with ``workflow_dispatch``.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import sys


VALIDATION_WORKFLOWS = {
  "main.yml",
  "power-gate.yml",
  "reproducible.yml",
  "sim-e2e.yml",
  "ui-screenshots.yml",
  "camera-tests.yml",
  "protocol-tests.yml",
  "android.yml",
}

TOP_LEVEL_KEY = re.compile(r"^(?P<indent> *)(?P<key>[^:#]+):(?:\s|$)")
EVENT_KEY = re.compile(r"^  (?P<key>\S[^:#]*):(?:\s|$)")


def unquote(value: str) -> str:
  value = value.strip()
  if len(value) >= 2 and value[0] == value[-1] and value[0] in "'\"":
    return value[1:-1]
  return value


def event_blocks(lines: list[str]) -> dict[str, list[str]]:
  """Return top-level Actions event blocks from a workflow.

  This is a narrow YAML reader for the trigger shape accepted by GitHub. It
  avoids making a host lint depend on PyYAML, whose YAML 1.1 boolean handling
  also turns the key ``on`` into ``True``.
  """

  trigger_start = None
  trigger_inline = ""
  for index, line in enumerate(lines):
    match = TOP_LEVEL_KEY.match(line)
    if match and not match.group("indent") and unquote(match.group("key")) == "on":
      trigger_start = index
      trigger_inline = line.split(":", 1)[1].strip()
      break
  if trigger_start is None:
    return {}

  if trigger_inline and trigger_inline.startswith("["):
    names = trigger_inline.strip("[] ").split(",")
    return {unquote(name): [] for name in names if unquote(name)}

  blocks: dict[str, list[str]] = {}
  current: str | None = None
  for line in lines[trigger_start + 1 :]:
    if line and not line.startswith(" "):
      break
    match = EVENT_KEY.match(line)
    if match:
      current = unquote(match.group("key"))
      blocks[current] = []
    elif current is not None:
      blocks[current].append(line)
  return blocks


def has_mapping_key(lines: list[str], key: str) -> bool:
  return any(re.match(rf"^    {re.escape(key)}:\s*", line) for line in lines)


def lint_workflow(path: Path) -> list[str]:
  lines = path.read_text(encoding="utf-8").splitlines()
  blocks = event_blocks(lines)
  errors: list[str] = []
  if "pull_request" not in blocks:
    errors.append("has no pull_request trigger")
  else:
    pull_request = blocks["pull_request"]
    if has_mapping_key(pull_request, "branches"):
      errors.append("pull_request has a base-branch filter")
    if not has_mapping_key(pull_request, "paths") and not has_mapping_key(
        pull_request, "paths-ignore"
    ):
      errors.append("pull_request has no path filter")
  if "workflow_dispatch" not in blocks:
    errors.append("has no workflow_dispatch trigger")

  # A pull_request workflow receives a read-only token for fork events. A
  # write permission here is both unnecessary for validation and unsafe to
  # assume is available, so reports belong in the run summary or artifacts.
  if "pull_request" in blocks and any(
      re.match(r"^\s+pull-requests:\s*write\s*(?:#.*)?$", line)
      for line in lines
  ):
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
  for name in sorted(VALIDATION_WORKFLOWS):
    path = workflow_root / name
    if not path.is_file():
      failures.append(f"{name}: workflow is missing")
      continue
    for error in lint_workflow(path):
      failures.append(f"{name}: {error}")
  if failures:
    print("CI workflow trigger check failed:", file=sys.stderr)
    for failure in failures:
      print(f"- {failure}", file=sys.stderr)
    return 1
  print(
      "CI workflow trigger check passed for "
      f"{len(VALIDATION_WORKFLOWS)} stacked-PR/manual-dispatch workflows."
  )
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
