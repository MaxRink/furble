#!/usr/bin/env python3
"""Regression tests for development firmware version resolution."""

import subprocess
import tarfile
import tempfile
import unittest
from pathlib import Path

from build_version import resolve_version


class BuildVersionTest(unittest.TestCase):
  @staticmethod
  def _git(args: list[str], project_dir: Path) -> str:
    return subprocess.check_output(
        ["git", *args],
        cwd=project_dir,
        stderr=subprocess.DEVNULL,
        text=True,
    ).strip()

  def _new_repo(self, root: Path) -> Path:
    project_dir = root / "repo"
    project_dir.mkdir()
    subprocess.run(["git", "init", "-q"], cwd=project_dir, check=True)
    self._git(["config", "user.name", "Version Test"], project_dir)
    self._git(["config", "user.email", "version-test@example.invalid"], project_dir)
    (project_dir / "tracked.txt").write_text("base\n", encoding="utf-8")
    self._git(["add", "tracked.txt"], project_dir)
    subprocess.run(
        ["git", "commit", "-q", "-m", "base"], cwd=project_dir, check=True
    )
    return project_dir

  def test_clean_checkout_keeps_revision(self):
    with tempfile.TemporaryDirectory() as temporary:
      project_dir = self._new_repo(Path(temporary))
      revision = self._git(["rev-parse", "--short=8", "HEAD"], project_dir)
      self.assertEqual(
          resolve_version("dev", project_dir), f"dev+g{revision}"
      )

  def test_tracked_modification_marks_dirty(self):
    with tempfile.TemporaryDirectory() as temporary:
      project_dir = self._new_repo(Path(temporary))
      (project_dir / "tracked.txt").write_text("modified\n", encoding="utf-8")
      revision = self._git(["rev-parse", "--short=8", "HEAD"], project_dir)
      self.assertEqual(
          resolve_version("dev", project_dir), f"dev+g{revision}.dirty"
      )

  def test_staged_change_marks_dirty(self):
    with tempfile.TemporaryDirectory() as temporary:
      project_dir = self._new_repo(Path(temporary))
      (project_dir / "tracked.txt").write_text("staged\n", encoding="utf-8")
      self._git(["add", "tracked.txt"], project_dir)
      revision = self._git(["rev-parse", "--short=8", "HEAD"], project_dir)
      self.assertEqual(
          resolve_version("dev", project_dir), f"dev+g{revision}.dirty"
      )

  def test_non_ignored_untracked_file_marks_dirty(self):
    with tempfile.TemporaryDirectory() as temporary:
      project_dir = self._new_repo(Path(temporary))
      (project_dir / "untracked.txt").write_text("untracked\n", encoding="utf-8")
      revision = self._git(["rev-parse", "--short=8", "HEAD"], project_dir)
      self.assertEqual(
          resolve_version("dev", project_dir), f"dev+g{revision}.dirty"
      )

  def test_ignored_only_changes_stay_clean(self):
    with tempfile.TemporaryDirectory() as temporary:
      project_dir = self._new_repo(Path(temporary))
      (project_dir / ".gitignore").write_text("ignored.txt\n", encoding="utf-8")
      self._git(["add", ".gitignore"], project_dir)
      subprocess.run(
          ["git", "commit", "-q", "-m", "ignore"], cwd=project_dir, check=True
      )
      (project_dir / "ignored.txt").write_text("ignored\n", encoding="utf-8")
      revision = self._git(["rev-parse", "--short=8", "HEAD"], project_dir)
      self.assertEqual(
          resolve_version("dev", project_dir), f"dev+g{revision}"
      )

  def test_exported_source_without_git_falls_back_to_dev(self):
    with tempfile.TemporaryDirectory() as temporary:
      root = Path(temporary)
      project_dir = self._new_repo(root)
      archive_path = root / "source.tar"
      with archive_path.open("wb") as archive:
        subprocess.run(
            ["git", "archive", "--format=tar", "HEAD"],
            cwd=project_dir,
            check=True,
            stdout=archive,
        )
      exported = root / "exported"
      exported.mkdir()
      with tarfile.open(archive_path) as archive:
        archive.extractall(exported)
      self.assertFalse((exported / ".git").exists())
      self.assertEqual(resolve_version("dev", exported), "dev")

  def test_dev_version_matches_checked_out_revision(self):
    with tempfile.TemporaryDirectory() as temporary:
      project_dir = self._new_repo(Path(temporary))
      revision = self._git(["rev-parse", "--short=8", "HEAD"], project_dir)
      self.assertTrue(revision)
      self.assertEqual(
          resolve_version("dev", project_dir), f"dev+g{revision}"
      )

  def test_dev_version_includes_short_revision(self):
    def fake_git(args: list[str], project_dir: Path) -> str:
      self.assertEqual(project_dir, Path("/repo"))
      if args == ["rev-parse", "--short=8", "HEAD"]:
        return "12ab34cd"
      self.assertEqual(
          args, ["status", "--porcelain=v1", "--untracked-files=all"]
      )
      return ""

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

  def test_explicit_release_is_unchanged_without_git(self):
    def unexpected_git(args: list[str], project_dir: Path) -> str:
      raise AssertionError(f"unexpected git call: {args} in {project_dir}")

    requested = "v3.9.1+build metadata"
    self.assertEqual(
        resolve_version(requested, Path("/repo"), unexpected_git), requested
    )


if __name__ == "__main__":
  unittest.main()
