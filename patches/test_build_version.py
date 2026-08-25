#!/usr/bin/env python3
"""Regression tests for development firmware version resolution."""

import subprocess
import unittest
from pathlib import Path

from build_version import resolve_version


class BuildVersionTest(unittest.TestCase):
  def test_dev_version_matches_checked_out_revision(self):
    project_dir = Path(__file__).resolve().parents[1]
    try:
      revision = subprocess.check_output(
          ["git", "rev-parse", "--short=8", "HEAD"],
          cwd=project_dir,
          stderr=subprocess.DEVNULL,
          text=True,
      ).strip()
    except (OSError, subprocess.CalledProcessError):
      self.skipTest("the source tree has no usable Git metadata")

    self.assertTrue(revision)
    self.assertEqual(
        resolve_version("dev", project_dir), f"dev+g{revision}"
    )

  def test_dev_version_includes_short_revision(self):
    def fake_git(args: list[str], project_dir: Path) -> str:
      self.assertEqual(args, ["rev-parse", "--short=8", "HEAD"])
      self.assertEqual(project_dir, Path("/repo"))
      return "12ab34cd"

    self.assertEqual(
        resolve_version("dev", Path("/repo"), fake_git), "dev+g12ab34cd"
    )

  def test_release_version_is_unchanged_without_git(self):
    def unexpected_git(args: list[str], project_dir: Path) -> str:
      raise AssertionError(f"unexpected git call: {args} in {project_dir}")

    self.assertEqual(
        resolve_version("v0.1.0+1", Path("/repo"), unexpected_git), "v0.1.0+1"
    )

  def test_dev_version_falls_back_when_git_is_unavailable(self):
    def failed_git(args: list[str], project_dir: Path) -> str:
      raise subprocess.CalledProcessError(128, ["git", *args])

    self.assertEqual(resolve_version("dev", Path("/repo"), failed_git), "dev")


if __name__ == "__main__":
  unittest.main()
