#!/usr/bin/env python3
"""Regression tests for the M5GFX board pinning patches.

The two patches are applied separately on purpose. A libdeps tree that an
older revision already patched still contains the first marker, so a single
combining patch would be reported "present" and the second half would never
land. That is the defect plan 352 fixes, and it is the failure this test
guards: each marker has to live in exactly one patch file, and the second
patch has to apply to a source that already carries the first.
"""

from pathlib import Path
import re
import sys
import tempfile
import unittest

PATCHES = Path(__file__).resolve().parent
sys.path.insert(0, str(PATCHES))

from ble_gap_patch import apply_patch  # noqa: E402

ASSIGN = PATCHES / "m5gfx_board.patch"
NOPROBE = PATCHES / "m5gfx_board_noprobe.patch"
ASSIGN_MARKER = "furble: a single-board image pins"
NOPROBE_MARKER = "furble: a pinned board that has no arm"


def hunk_contexts(patch: Path) -> list[list[str]]:
  """The context lines of each hunk, without the added lines."""
  hunks: list[list[str]] = []
  current: list[str] | None = None
  body = patch.read_text(encoding="utf-8").splitlines()[2:]
  for line in body:
    if line.startswith("@@"):
      current = []
      hunks.append(current)
      continue
    if current is None or line.startswith("+"):
      continue
    current.append(line[1:] if line[:1] in (" ", "-", "\\") else line)
  return [hunk for hunk in hunks if hunk]


def fixture(patch: Path) -> str:
  """A file that holds this patch's context, ready to be patched."""
  lines = ["// fixture", ""]
  for hunk in hunk_contexts(patch):
    lines.extend(hunk)
    lines.append("// gap")
  return "\n".join(lines) + "\n"


def localise(patch: Path, directory: Path) -> Path:
  """Copy *patch* with every hunk anchored at line 1.

  The real headers name line 1146 and 1682 of M5GFX.cpp. The fixture is a few
  lines long, and patch refuses a hunk whose expected line is past the end of
  the input, so the headers are renumbered instead of padding the fixture with
  1700 lines.
  """
  local = directory / patch.name
  lines = []
  for line in patch.read_text(encoding="utf-8").splitlines():
    if line.startswith("@@"):
      match = re.match(r"@@ -\d+(,\d+)? \+\d+(,\d+)? @@$", line)
      assert match is not None, line
      line = f"@@ -1{match.group(1) or ''} +1{match.group(2) or ''} @@"
    lines.append(line)
  local.write_text("\n".join(lines) + "\n", encoding="utf-8")
  return local


class M5GfxBoardPatchTest(unittest.TestCase):
  def test_each_marker_lives_in_one_patch_file(self):
    for marker in (ASSIGN_MARKER, NOPROBE_MARKER):
      carrying = [
          patch.name for patch in (ASSIGN, NOPROBE)
          if marker in patch.read_text(encoding="utf-8")
      ]
      self.assertEqual(len(carrying), 1, (marker, carrying))

  def test_patches_add_the_pin_and_the_no_probe_guard(self):
    with tempfile.TemporaryDirectory() as directory:
      root = Path(directory)
      source = root / "M5GFX.cpp"
      source.write_text(fixture(ASSIGN) + fixture(NOPROBE), encoding="utf-8")
      self.assertEqual(apply_patch(source, localise(ASSIGN, root), marker=ASSIGN_MARKER),
                       "applied")
      self.assertEqual(apply_patch(source, localise(NOPROBE, root), marker=NOPROBE_MARKER),
                       "applied")
      text = source.read_text(encoding="utf-8")
      self.assertIn("board = M5GFX_BOARD;", text)
      self.assertIn("case board_t::board_M5StampS3:", text)
      self.assertIn("goto init_clear;", text)

  def test_both_patches_are_idempotent(self):
    with tempfile.TemporaryDirectory() as directory:
      root = Path(directory)
      source = root / "M5GFX.cpp"
      source.write_text(fixture(ASSIGN) + fixture(NOPROBE), encoding="utf-8")
      apply_patch(source, localise(ASSIGN, root), marker=ASSIGN_MARKER)
      apply_patch(source, localise(NOPROBE, root), marker=NOPROBE_MARKER)
      self.assertEqual(apply_patch(source, localise(ASSIGN, root), marker=ASSIGN_MARKER),
                       "present")
      self.assertEqual(apply_patch(source, localise(NOPROBE, root), marker=NOPROBE_MARKER),
                       "present")

  def test_no_probe_patch_applies_to_an_already_pinned_source(self):
    """The upgrade path: a tree patched before the guard existed."""
    with tempfile.TemporaryDirectory() as directory:
      root = Path(directory)
      source = root / "M5GFX.cpp"
      source.write_text(fixture(ASSIGN) + fixture(NOPROBE), encoding="utf-8")
      self.assertEqual(apply_patch(source, localise(ASSIGN, root), marker=ASSIGN_MARKER),
                       "applied")
      self.assertNotIn("case board_t::board_M5StampS3:",
                       source.read_text(encoding="utf-8"))
      self.assertEqual(apply_patch(source, localise(NOPROBE, root), marker=NOPROBE_MARKER),
                       "applied")
      self.assertIn("case board_t::board_M5StampS3:",
                    source.read_text(encoding="utf-8"))

  def test_no_probe_patch_names_the_stamp_s3_family(self):
    text = NOPROBE.read_text(encoding="utf-8")
    for board in ("board_M5StampS3", "board_M5StampS3Bat", "board_M5StampS3Mini"):
      self.assertIn(f"case board_t::{board}:", text)


if __name__ == "__main__":
  unittest.main()
