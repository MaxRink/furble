"""Resolve the version string embedded in furble firmware."""

from __future__ import annotations

import subprocess
from pathlib import Path
from typing import Callable


GitOutput = str | bytes
GitRunner = Callable[[list[str], Path], GitOutput]


def _run_git(args: list[str], project_dir: Path) -> GitOutput:
  result = subprocess.run(
      ["git", *args],
      cwd=project_dir,
      capture_output=True,
      check=True,
      text=False,
  )
  if args and args[0] == "status":
    return result.stdout
  return result.stdout.decode("utf-8").strip()


def resolve_version(
    requested: str, project_dir: Path, run_git: GitRunner = _run_git
) -> str:
  """Append an unambiguous short revision to the conventional ``dev`` version."""
  if requested != "dev":
    return requested

  try:
    revision = run_git(["rev-parse", "--short=8", "HEAD"], project_dir)
    dirty = run_git(
        ["status", "--porcelain=v1", "--untracked-files=all"], project_dir
    )
  except (OSError, subprocess.CalledProcessError):
    return requested

  if isinstance(revision, bytes):
    try:
      revision = revision.decode("ascii").strip()
    except UnicodeDecodeError:
      return requested
  if not revision:
    return requested
  return f"dev+g{revision}{'.dirty' if dirty else ''}"
