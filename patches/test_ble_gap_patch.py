#!/usr/bin/env python3
"""Regression tests for the shared-framework patch guard."""

from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import tempfile
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


if __name__ == "__main__":
  unittest.main()
