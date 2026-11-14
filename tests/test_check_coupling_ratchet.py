"""Tests for the board and platform coupling ratchet."""

from pathlib import Path
import contextlib
import io
import subprocess
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


CAPS = """\
#define FURBLE_HAS_SD 1
#define FURBLE_PANEL_80X160 0
"""


def git(root: Path, *args: str) -> None:
  subprocess.run(["git", "-C", str(root), "-c", "user.name=t", "-c", "user.email=t@t",
                  "-c", "commit.gpgsign=false", *args], check=True, capture_output=True)


class CouplingRatchetTest(unittest.TestCase):
  def setUp(self):
    self.temp = tempfile.TemporaryDirectory(prefix="furble-ratchet-")
    self.root = Path(self.temp.name)
    (self.root / "src").mkdir()
    (self.root / "include").mkdir()
    (self.root / "tools").mkdir()
    (self.root / "src/a.cpp").write_text(SOURCE, encoding="utf-8")
    (self.root / "include/a.h").write_text(HEADER, encoding="utf-8")
    (self.root / "include/FurbleBoardCaps.h").write_text(CAPS, encoding="utf-8")

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
        CAPS + "#if defined(FURBLE_M5STICKS3)\n#define FURBLE_HAS_M5PM1 1\n#endif\n",
        encoding="utf-8")
    self.assertEqual(self.run_main(), 0)

  def test_widened_condition_counts_each_board_macro(self):
    one = ratchet.board_guards("#if defined(FURBLE_M5STICKC)\n#endif\n")
    two = ratchet.board_guards(
        "#if defined(FURBLE_M5STICKC) || defined(FURBLE_M5STICKC_PLUS)\n#endif\n")
    self.assertEqual((one, two), (1, 2))
    self.run_main("--update")
    (self.root / "src/a.cpp").write_text(
        SOURCE.replace("defined(FURBLE_M5STICKS3)",
                       "defined(FURBLE_M5STICKS3) || defined(FURBLE_M5COREX)"), encoding="utf-8")
    self.assertEqual(self.run_main(), 1)

  def test_continuation_is_joined_before_counting(self):
    text = "#if defined(FURBLE_M5STICKC) || \\\n    defined(FURBLE_M5COREX)\n#endif\n"
    self.assertEqual(ratchet.board_guards(text), 2)

  def test_define_alias_counts(self):
    self.assertEqual(ratchet.board_guards("#define IS_STICK FURBLE_M5STICKC\n"), 1)
    self.assertEqual(ratchet.board_guards("#define UNRELATED 1\n"), 0)
    self.run_main("--update")
    (self.root / "src/b.cpp").write_text("#define IS_CORE defined(FURBLE_M5COREX)\n",
                                         encoding="utf-8")
    self.assertEqual(self.run_main(), 1)

  def test_update_cannot_hide_growth_from_the_base(self):
    self.run_main("--update")
    git(self.root, "init", "-q")
    git(self.root, "add", "-A")
    git(self.root, "commit", "-q", "-m", "base")
    (self.root / "src/b.cpp").write_text("#ifdef FURBLE_M5COREX\n#endif\n", encoding="utf-8")
    self.run_main("--update")
    self.assertEqual(self.run_main(), 0)
    self.assertEqual(self.run_main("--base", "HEAD"), 1)

  def test_base_without_manifest_is_skipped(self):
    git(self.root, "init", "-q")
    git(self.root, "add", "-A")
    git(self.root, "commit", "-q", "-m", "base")
    self.run_main("--update")
    self.assertEqual(self.run_main("--base", "HEAD"), 0)

  def test_unknown_base_fails(self):
    self.run_main("--update")
    git(self.root, "init", "-q")
    self.assertEqual(self.run_main("--base", "no-such-revision"), 1)

  def test_capability_macro_must_exist(self):
    self.run_main("--update")
    (self.root / "src/b.cpp").write_text(
        '#include "FurbleBoardCaps.h"\n#if FURBLE_HAS_SDD\n#endif\n', encoding="utf-8")
    self.assertEqual(self.run_main(), 1)

  def test_capability_scan_covers_sim_and_host_doubles(self):
    self.run_main("--update")
    for name in ("sim/x.cpp", "tests/host/x.cpp"):
      path = self.root / name
      path.parent.mkdir(parents=True, exist_ok=True)
      path.write_text("#ifdef FURBLE_HAS_SD\n#endif\n", encoding="utf-8")
      self.assertEqual(self.run_main(), 1, name)
      path.unlink()

  def test_capability_macro_needs_direct_include(self):
    self.run_main("--update")
    (self.root / "src/b.cpp").write_text("#if FURBLE_HAS_SD\n#endif\n", encoding="utf-8")
    self.assertEqual(self.run_main(), 1)
    (self.root / "src/b.cpp").write_text(
        '#include "FurbleBoardCaps.h"\n#if FURBLE_HAS_SD\n#endif\n', encoding="utf-8")
    self.assertEqual(self.run_main(), 0)

  def test_capability_macro_rejects_ifdef(self):
    self.run_main("--update")
    (self.root / "src/b.cpp").write_text(
        '#include "FurbleBoardCaps.h"\n#ifdef FURBLE_HAS_SD\n#endif\n', encoding="utf-8")
    self.assertEqual(self.run_main(), 1)
    (self.root / "src/b.cpp").write_text(
        '#include "FurbleBoardCaps.h"\n#if defined(FURBLE_PANEL_80X160)\n#endif\n',
        encoding="utf-8")
    self.assertEqual(self.run_main(), 1)

  def test_platform_adapter_may_call_m5(self):
    self.run_main("--update")
    (self.root / "src/FurblePlatform.cpp").write_text("void f() { M5.getBoard(); }\n",
                                                       encoding="utf-8")
    self.assertEqual(self.run_main(), 0)

  def test_board_profile_may_switch_on_board(self):
    self.run_main("--update")
    (self.root / "src/FurbleBoard.cpp").write_text("void f() { M5.getBoard(); }\n",
                                                   encoding="utf-8")
    self.assertEqual(self.run_main(), 0)

  def test_current_tree_matches_manifest(self):
    with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
      self.assertEqual(ratchet.main(["--root", str(ROOT)]), 0)


if __name__ == "__main__":
  unittest.main()
