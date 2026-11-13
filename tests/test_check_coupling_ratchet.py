"""Tests for the board and platform coupling ratchet."""

from pathlib import Path
import contextlib
import io
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import check_coupling_ratchet as ratchet  # noqa: E402

SOURCE = """\
#if defined(FURBLE_M5STICKS3)
  M5.Power.setExtOutput(true);
#elif defined(FURBLE_M5STICKC) || defined(FURBLE_ETHERNET)
#endif
#if !defined(FURBLE_NO_DISPLAY)
#endif
// #if defined(FURBLE_M5COREX) M5.getBoard() in a comment
const char *text = "M5.getBoard()";
switch (M5.getBoard()) {}
"""

HEADER = """\
#ifndef FURBLE_ETHERNET_H
void connect(esp_power_level_t power);  // BLE_HS_IO_DISPLAY_YESNO
NimBLEClient *client;
"""


class CouplingRatchetTest(unittest.TestCase):
  def setUp(self):
    self.temp = tempfile.TemporaryDirectory(prefix="furble-ratchet-")
    self.root = Path(self.temp.name)
    (self.root / "src").mkdir()
    (self.root / "include").mkdir()
    (self.root / "tools").mkdir()
    (self.root / "src/a.cpp").write_text(SOURCE, encoding="utf-8")
    (self.root / "include/a.h").write_text(HEADER, encoding="utf-8")

  def tearDown(self):
    self.temp.cleanup()

  def run_main(self, *args: str) -> int:
    with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
      return ratchet.main(["--root", str(self.root), *args])

  def test_counts_ignore_comments_strings_and_header_guards(self):
    counts = ratchet.count(self.root)
    self.assertEqual(counts["board_guards"], {"src/a.cpp": 2})
    self.assertEqual(counts["m5_calls"], {"src/a.cpp": 2})
    self.assertEqual(counts["m5_get_board"], {"src/a.cpp": 1})
    self.assertEqual(counts["header_types"], {"include/a.h": 2})

  def test_missing_manifest_fails(self):
    self.assertEqual(self.run_main(), 1)

  def test_unchanged_tree_passes(self):
    self.assertEqual(self.run_main("--update"), 0)
    self.assertEqual(self.run_main(), 0)

  def test_growth_fails(self):
    self.run_main("--update")
    (self.root / "src/b.cpp").write_text("#ifdef FURBLE_M5COREX\n#endif\n", encoding="utf-8")
    self.assertEqual(self.run_main(), 1)

  def test_reduction_fails_until_manifest_is_updated(self):
    self.run_main("--update")
    (self.root / "include/a.h").write_text("int unrelated;\n", encoding="utf-8")
    self.assertEqual(self.run_main(), 1)
    self.run_main("--update")
    self.assertEqual(self.run_main(), 0)

  def test_board_caps_header_may_name_boards(self):
    self.run_main("--update")
    (self.root / "include/FurbleBoardCaps.h").write_text(
        "#if defined(FURBLE_M5STICKS3)\n#define FURBLE_HAS_M5PM1 1\n#endif\n", encoding="utf-8")
    self.assertEqual(self.run_main(), 0)

  def test_current_tree_matches_manifest(self):
    with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
      self.assertEqual(ratchet.main(["--root", str(ROOT)]), 0)


if __name__ == "__main__":
  unittest.main()
