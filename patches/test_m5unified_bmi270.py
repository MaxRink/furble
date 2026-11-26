#!/usr/bin/env python3
"""Regression tests for the M5Unified BMI270 guard patches.

The guard exists to drop an 8,192-byte init blob from a board that has no
Bosch BMI270, and it must never do that by omission: the macro has to default
to 1 so a tree built without `-DFURBLE_IMU_BMI270=0` behaves exactly as
upstream, and the probe the first patch leaves behind has to stay guarded by
the second. Getting either wrong silently removes the part from a shipped
board, and the only symptom is a motion wake that never fires.

Both patches replace the line they anchor on instead of carrying context lines,
because a blank context line in a `.patch` file is a whitespace-only line that
`git diff --check` rejects. The tests below replay each hunk against a file
whose lines at the named positions are the hunk's own removed lines, which is
what makes the line numbers and the counts self-consistent.
"""

from pathlib import Path
import re
import sys
import tempfile
import unittest

PATCHES = Path(__file__).resolve().parent
sys.path.insert(0, str(PATCHES))

from ble_gap_patch import apply_patch  # noqa: E402

BLOB = PATCHES / "m5unified_bmi270_blob.patch"
PROBE = PATCHES / "m5unified_bmi270_probe.patch"
BLOB_MARKER = "furble: a board with no Bosch BMI270 never runs this probe"
PROBE_MARKER = "furble: the probe and its 8 KiB blob only exist"

HUNK_RE = re.compile(r"^@@ -(\d+),(\d+) \+(\d+),(\d+) @@$")


def hunks(patch: Path) -> list[tuple[int, list[str], list[str]]]:
  """(old start line, removed lines, added lines) for each hunk."""
  result: list[tuple[int, list[str], list[str]]] = []
  current: tuple[int, list[str], list[str]] | None = None
  for line in patch.read_text(encoding="utf-8").splitlines()[2:]:
    match = HUNK_RE.match(line)
    if match:
      current = (int(match.group(1)), [], [])
      result.append(current)
      continue
    if current is None or not line:
      continue
    if line[0] == "-":
      current[1].append(line[1:])
    elif line[0] == "+":
      current[2].append(line[1:])
    else:
      raise AssertionError(f"context line in {patch.name}: {line!r}")
  return result


def fixture(patch: Path) -> str:
  """A file whose hunk positions hold exactly the lines each hunk removes."""
  parsed = hunks(patch)
  last = max(start + len(removed) for start, removed, _ in parsed)
  lines = ["// filler"] * (last + 2)
  for start, removed, _ in parsed:
    for offset, text in enumerate(removed):
      lines[start - 1 + offset] = text
  return "\n".join(lines) + "\n"


class M5UnifiedBmi270PatchTest(unittest.TestCase):
  def test_each_marker_lives_in_one_patch_file(self):
    for marker in (BLOB_MARKER, PROBE_MARKER):
      carrying = [
          patch.name for patch in (BLOB, PROBE)
          if marker in patch.read_text(encoding="utf-8")
      ]
      self.assertEqual(len(carrying), 1, (marker, carrying))

  def test_both_patches_default_the_macro_to_enabled(self):
    """Omission must keep the part, so the default is 1 in both files."""
    for patch in (BLOB, PROBE):
      text = patch.read_text(encoding="utf-8")
      self.assertIn("+#if !defined ( FURBLE_IMU_BMI270 )", text, patch.name)
      self.assertIn("+#define FURBLE_IMU_BMI270 1", text, patch.name)
      self.assertNotIn("#define FURBLE_IMU_BMI270 0", text, patch.name)

  def test_blob_patch_guards_the_translation_unit(self):
    text = BLOB.read_text(encoding="utf-8")
    self.assertIn("+#if defined (ESP_PLATFORM) && FURBLE_IMU_BMI270", text)
    # The blob is defined inside that guard, so the guard has to wrap the file
    # body rather than the include of the config alone.
    self.assertNotIn("BMI270_config.inl", text)

  def test_probe_patch_guards_the_include_and_the_probe(self):
    text = PROBE.read_text(encoding="utf-8")
    guarded_include = (
        '+#if FURBLE_IMU_BMI270\n+#include "imu/BMI270_Class.hpp"\n+#endif')
    self.assertIn(guarded_include, text)
    # The two `if (_imu == imu_t::imu_none)` lines are identical, so each hunk
    # has to be number-anchored and ordered: open the guard on the first, close
    # it on the second.
    parsed = hunks(PROBE)
    self.assertEqual([start for start, _, _ in parsed], [14, 16, 74, 122])
    self.assertEqual(parsed[2][2], ["#if FURBLE_IMU_BMI270",
                                    "    if (_imu == imu_t::imu_none)"])
    self.assertEqual(parsed[3][2], ["#endif",
                                    "",
                                    "    if (_imu == imu_t::imu_none)"])

  def test_each_patch_applies_at_its_stated_line_numbers(self):
    with tempfile.TemporaryDirectory() as directory:
      root = Path(directory)
      for patch, marker, expected in (
          (BLOB, BLOB_MARKER, "#if defined (ESP_PLATFORM) && FURBLE_IMU_BMI270"),
          (PROBE, PROBE_MARKER, "#if FURBLE_IMU_BMI270"),
      ):
        source = root / f"{patch.stem}.cpp"
        source.write_text(fixture(patch), encoding="utf-8")
        self.assertEqual(apply_patch(source, patch, marker=marker), "applied")
        text = source.read_text(encoding="utf-8")
        self.assertIn(expected, text)
        # Every added line has to land above the filler that followed it, so
        # nothing is appended at the end of the file instead of in place.
        self.assertLess(text.index(expected), text.rindex("// filler"))

  def test_both_patches_are_idempotent(self):
    with tempfile.TemporaryDirectory() as directory:
      root = Path(directory)
      for patch, marker in ((BLOB, BLOB_MARKER), (PROBE, PROBE_MARKER)):
        source = root / f"{patch.stem}.cpp"
        source.write_text(fixture(patch), encoding="utf-8")
        apply_patch(source, patch, marker=marker)
        self.assertEqual(apply_patch(source, patch, marker=marker), "present")


if __name__ == "__main__":
  unittest.main()
