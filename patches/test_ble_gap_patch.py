#!/usr/bin/env python3
"""Regression tests for the shared-framework patch guard."""

from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import sys
import tempfile
import types
import unittest

from ble_gap_patch import apply_patch


PATCH = """--- a/test.c
+++ b/test.c
@@ -1 +1 @@
-old
+new
"""


class BleGapPatchTest(unittest.TestCase):
  def make_files(self, root: Path) -> tuple[Path, Path]:
    source = root / "test.c"
    patch = root / "test.patch"
    source.write_text("old\n", encoding="utf-8")
    patch.write_text(PATCH, encoding="utf-8")
    return source, patch

  def test_second_application_is_a_noop(self):
    with tempfile.TemporaryDirectory() as directory:
      source, patch = self.make_files(Path(directory))
      self.assertEqual(apply_patch(source, patch, marker="new"), "applied")
      self.assertEqual(apply_patch(source, patch, marker="new"), "present")
      self.assertEqual(source.read_text(encoding="utf-8"), "new\n")
      self.assertEqual(list(Path(directory).glob("*.rej")), [])

  def test_concurrent_applications_only_patch_once(self):
    with tempfile.TemporaryDirectory() as directory:
      source, patch = self.make_files(Path(directory))
      with ThreadPoolExecutor(max_workers=8) as pool:
        results = list(
            pool.map(
                lambda _index: apply_patch(source, patch, marker="new"),
                range(8),
            )
        )
      self.assertEqual(results.count("applied"), 1, results)
      self.assertEqual(results.count("present"), 7, results)
      self.assertEqual(list(Path(directory).glob("*.rej")), [])

  def test_rejected_hunk_is_an_error_without_reject_file(self):
    with tempfile.TemporaryDirectory() as directory:
      source, patch = self.make_files(Path(directory))
      source.write_text("unexpected\n", encoding="utf-8")
      with self.assertRaises(RuntimeError):
        apply_patch(source, patch, marker="new")
      self.assertEqual(list(Path(directory).glob("*.rej")), [])

  def test_platformio_entrypoint_does_not_require_dunder_file(self):
    """SCons executes extra scripts without defining ``__file__``."""
    with tempfile.TemporaryDirectory() as directory:
      project_dir = Path(directory) / "project"
      framework_dir = Path(directory) / "framework"
      (project_dir / "patches").mkdir(parents=True)
      calls = []

      fake_module = types.ModuleType("ble_gap_patch")
      fake_module.apply_patch = lambda source, patch: calls.append((source, patch))

      class FakePlatform:
        def get_package_dir(self, name):
          self.requested_name = name
          return str(framework_dir)

      class FakeEnv:
        platform = FakePlatform()

        def subst(self, value):
          self.requested_value = value
          return str(project_dir)

        def PioPlatform(self):
          return self.platform

      previous_module = sys.modules.get("ble_gap_patch")
      sys.modules["ble_gap_patch"] = fake_module
      try:
        entrypoint = Path(__file__).with_name("apply.py").read_text(
            encoding="utf-8"
        )
        namespace = {"Import": lambda _name: None, "env": FakeEnv()}
        exec(compile(entrypoint, "patches/apply.py", "exec"), namespace)
      finally:
        if previous_module is None:
          del sys.modules["ble_gap_patch"]
        else:
          sys.modules["ble_gap_patch"] = previous_module

      self.assertEqual(len(calls), 1)
      self.assertEqual(calls[0][1], project_dir / "patches" / "ble_gap.patch")
      self.assertEqual(calls[0][0].name, "ble_gap.c")


if __name__ == "__main__":
  unittest.main()
