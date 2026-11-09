#!/usr/bin/env python3
"""Claude Code PreToolUse hook: run the pre-review gates before a git push.

It reads the hook event on stdin and blocks (exit 2) a Bash call that runs
`git push` when `tools/pre_review.py --base origin/master` fails, so the
failure goes back to the agent. It stays out of the way when the checkout
already runs .githooks/pre-push through core.hooksPath, and for
`git push --no-verify`.
"""

from __future__ import annotations

import json
from pathlib import Path
import re
import subprocess
import sys

PUSH = re.compile(r"(?:^|[;&|(]|\s)git(?:\s+-C\s+\S+)?\s+push\b")
TAIL_LINES = 40


def is_push(command: str) -> bool:
  return PUSH.search(command) is not None and "--no-verify" not in command


def git(cwd: Path, *args: str) -> str:
  result = subprocess.run(["git", *args], cwd=cwd, capture_output=True, text=True)
  return result.stdout.strip() if result.returncode == 0 else ""


def main() -> int:
  try:
    event = json.load(sys.stdin)
  except ValueError:
    return 0
  command = (event.get("tool_input") or {}).get("command", "")
  if not isinstance(command, str) or not is_push(command):
    return 0
  # ponytail: reviews the session's working directory, not a `cd` or `-C`
  # target inside the command; parse those if agents push from other trees.
  root = git(Path(event.get("cwd") or "."), "rev-parse", "--show-toplevel")
  if not root or not (Path(root) / "tools" / "pre_review.py").is_file():
    return 0
  if git(Path(root), "config", "core.hooksPath") == ".githooks":
    return 0
  result = subprocess.run(
      [sys.executable, "tools/pre_review.py", "--base", "origin/master"],
      cwd=root, capture_output=True, text=True)
  if result.returncode == 0:
    return 0
  output = (result.stdout + result.stderr).splitlines()[-TAIL_LINES:]
  sys.stderr.write("\n".join(output))
  sys.stderr.write("\npre-review failed; fix it before pushing\n")
  return 2


if __name__ == "__main__":
  raise SystemExit(main())
