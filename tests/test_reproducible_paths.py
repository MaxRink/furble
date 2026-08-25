#!/usr/bin/env python3
import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "furble_reproducible_build", ROOT / "tools" / "reproducible_build.py"
)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


class ReproduciblePathTest(unittest.TestCase):
  def test_preserves_tmp_symlink_spelling(self):
    with tempfile.TemporaryDirectory() as directory:
      real = Path(directory) / "real"
      real.mkdir()
      alias = Path(directory) / "alias"
      alias.symlink_to(real, target_is_directory=True)
      lexical = alias / "source"
      self.assertEqual(MODULE.stable_project_path(lexical), lexical)
      self.assertNotEqual(MODULE.stable_project_path(lexical), lexical.resolve())


if __name__ == "__main__":
  unittest.main()
