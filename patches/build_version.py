"""Resolve the version string embedded in furble firmware."""

from __future__ import annotations

import subprocess
from pathlib import Path
from typing import Callable


GitRunner = Callable[[list[str], Path], str]


def _run_git(args: list[str], project_dir: Path) -> str:
  return subprocess.check_output(
      ["git", *args], cwd=project_dir, stderr=subprocess.DEVNULL, text=True
  ).strip()


def resolve_version(
    requested: str, project_dir: Path, run_git: GitRunner = _run_git
) -> str:
  """Append an unambiguous short revision to the conventional ``dev`` version."""
  if requested != "dev":
    return requested

  try:
    revision = run_git(["rev-parse", "--short=8", "HEAD"], project_dir)
  except (OSError, subprocess.CalledProcessError):
    return requested

  return f"dev+g{revision}" if revision else requested
