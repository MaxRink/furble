"""Tests for the changed-file clang-format gate."""

from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = spec_from_file_location("check_format", ROOT / "tools" / "check_format.py")
assert SPEC is not None and SPEC.loader is not None
CHECKER = module_from_spec(SPEC)
sys.modules[SPEC.name] = CHECKER
SPEC.loader.exec_module(CHECKER)

FORMATTED = "int value = 0;\n"
UNFORMATTED = "int  value=0;\n"


def git(repo: Path, *args: str) -> str:
  return subprocess.run(
      ["git", "-c", "user.name=t", "-c", "user.email=t@example.invalid", *args],
      cwd=repo, check=True, capture_output=True, text=True,
  ).stdout.strip()


class CheckedPathTest(unittest.TestCase):
  def test_matches_the_ci_directories_and_suffixes(self):
    self.assertTrue(CHECKER.checked_path("src/FurbleUI.cpp"))
    self.assertTrue(CHECKER.checked_path("tests/host/boundaries/shims/esp_event.h"))
    self.assertTrue(CHECKER.checked_path("lib/furble/Camera.h"))
    self.assertFalse(CHECKER.checked_path("lib/blowfish/blowfish.cpp"))
    self.assertFalse(CHECKER.checked_path("tests/test_check_format.py"))
    self.assertFalse(CHECKER.checked_path("srcx/file.cpp"))

  def test_directories_match_main_yml(self):
    workflow = (ROOT / ".github" / "workflows" / "main.yml").read_text(encoding="utf-8")
    self.assertIn("find " + " ".join(CHECKER.DIRECTORIES) + " -type f", workflow)
    for suffix in CHECKER.SUFFIXES:
      self.assertIn(f"-name '*{suffix}'", workflow)


@unittest.skipUnless(
    shutil.which("clang-format")
    and CHECKER.clang_format_major(shutil.which("clang-format")) == CHECKER.REQUIRED_MAJOR,
    "clang-format 21 is not installed",
)
class ChangedFileGateTest(unittest.TestCase):
  def setUp(self):
    self.repo = Path(tempfile.mkdtemp())
    self.addCleanup(shutil.rmtree, self.repo)
    git(self.repo, "init", "-q", "-b", "master")
    shutil.copy(ROOT / ".clang-format", self.repo / ".clang-format")
    (self.repo / "src").mkdir()
    # An unformatted file the branch does not touch must not fail the gate.
    (self.repo / "src" / "old.cpp").write_text(UNFORMATTED, encoding="utf-8")
    git(self.repo, "add", ".")
    git(self.repo, "commit", "-q", "-m", "base")
    self.base = git(self.repo, "rev-parse", "HEAD")

  def commit(self, path: str, text: str) -> None:
    target = self.repo / path
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(text, encoding="utf-8")
    git(self.repo, "add", path)
    git(self.repo, "commit", "-q", "-m", path)

  def check(self) -> int:
    return CHECKER.main(["--root", str(self.repo), "--base", self.base])

  def test_formatted_change_passes(self):
    self.commit("src/new.cpp", FORMATTED)
    self.assertEqual(self.check(), 0)

  def test_unformatted_change_fails(self):
    self.commit("sim/new.cpp", UNFORMATTED)
    self.assertEqual(self.check(), 1)

  def test_unchecked_paths_are_ignored(self):
    self.commit("lib/blowfish/new.cpp", UNFORMATTED)
    self.assertEqual(self.check(), 0)

  def test_missing_clang_format_fails_closed(self):
    self.commit("src/new.cpp", FORMATTED)
    self.assertEqual(
        CHECKER.main(["--root", str(self.repo), "--base", self.base,
                      "--clang-format", "clang-format-does-not-exist"]),
        2,
    )


if __name__ == "__main__":
  unittest.main()
