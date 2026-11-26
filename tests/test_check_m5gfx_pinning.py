#!/usr/bin/env python3
"""Tests for the M5GFX board pinning guard."""

from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import check_m5gfx_pinning as pinning  # noqa: E402


# The shape of the patched M5GFX::autodetect that matters: the pin at the top,
# one target branch per MCU, and the no-probe case list for the display-less
# pin before the first probe. Preprocessor directives sit at column 0, as they
# do in the real file.
PATCHED = """\
  board_t M5GFX::autodetect(bool use_reset, board_t board)
  {
#if defined ( M5GFX_BOARD )
    // furble: a single-board image pins its board, so only that pin's arm
    // below can match.
    board = M5GFX_BOARD;
#endif
    auto bus_spi = new Bus_SPI();
    _bus_last.reset(bus_spi);

#if !defined (CONFIG_IDF_TARGET) || defined (CONFIG_IDF_TARGET_ESP32)
    if (board == 0 || board == board_t::board_M5StickC)
    {
      _pin_reset(GPIO_NUM_18, use_reset);
    }
#elif defined (CONFIG_IDF_TARGET_ESP32S3)
    std::uint32_t id;
    std::uint32_t pkg_ver = m5gfx::get_pkg_ver();
#if defined ( M5GFX_BOARD )
    // furble: a pinned board that has no arm in this switch is returned here.
    switch (board)
    {
    case board_t::board_M5StampS3:
      goto init_clear;
    default: break;
    }
#endif
    if (board == 0 || board == board_t::board_M5StickS3)
    {
      id = _read_panel_id(bus_spi, GPIO_NUM_3);
    }
    board = board_t::board_unknown;
    goto init_clear;
init_clear:
    panel(_panel_last.get());
    return board;
  }
"""

GUARD = """\
#if defined ( M5GFX_BOARD )
    // furble: a pinned board that has no arm in this switch is returned here.
    switch (board)
    {
    case board_t::board_M5StampS3:
      goto init_clear;
    default: break;
    }
#endif
"""

WITHOUT_GUARD = PATCHED.replace(GUARD, "")
GUARD_AFTER_PROBE = PATCHED.replace(GUARD, "").replace(
    "    board = board_t::board_unknown;", GUARD + "    board = board_t::board_unknown;")
WITHOUT_MARKER = PATCHED.replace(
    "furble: a single-board image pins its board, so only that pin's arm\n"
    "    // below can match.", "a single-board image pins its board.")
WITHOUT_ASSIGNMENT = PATCHED.replace("board = M5GFX_BOARD;", "")
WITHOUT_ARM = PATCHED.replace(
    "    if (board == 0 || board == board_t::board_M5StickC)", "    if (false)")

INI = """\
[furble]
build_flags = -Wall

[env]
extra_scripts =
  pre:patches/version.py
  post:patches/m5gfx_board.py

[env:m5stick-c]
board = m5stick-c
build_flags = ${furble.build_flags} -DM5GFX_BOARD=board_t::board_M5StickC

[env:m5stick-c-debug]
extends = env:m5stick-c
board = m5stick-c
build_flags = ${env:m5stick-c.build_flags} -DLOG_LOCAL_LEVEL=ESP_LOG_VERBOSE

[env:m5stick-c-debug-no-script]
extends = env:m5stick-c
extra_scripts =
  pre:patches/version.py
build_flags = ${env:m5stick-c.build_flags} -DLOG_LOCAL_LEVEL=ESP_LOG_VERBOSE

[env:waveshare-s3-eth]
board = waveshare-esp32-s3-eth
build_flags = ${furble.build_flags} -DM5GFX_BOARD=board_t::board_M5StampS3

[env:unpinned]
board = esp32-s3-devkitc-1
build_flags = ${furble.build_flags}
"""


def write_source(root: Path, env: str, text: str) -> None:
  source = root / ".pio" / "libdeps" / env / "M5GFX" / "src" / "M5GFX.cpp"
  source.parent.mkdir(parents=True)
  source.write_text(text, encoding="utf-8")


class PinningCheckTest(unittest.TestCase):
  def make_tree(self, ini: str = INI) -> tuple[Path, tempfile.TemporaryDirectory]:
    holder = tempfile.TemporaryDirectory()
    root = Path(holder.name)
    (root / "platformio.ini").write_text(ini, encoding="utf-8")
    return root, holder

  def test_current_tree_passes(self):
    result = subprocess.run(
        [sys.executable, str(ROOT / "tools" / "check_m5gfx_pinning.py")],
        check=False,
        capture_output=True,
        text=True,
    )
    self.assertEqual(result.returncode, 0, result.stderr)

  def test_every_release_env_pins_its_board(self):
    sections = pinning.read_ini(ROOT / "platformio.ini")
    for env, board in pinning.REQUIRED_PINS.items():
      self.assertEqual(pinning.pinned_board(sections, env), board, env)

  def test_debug_envs_inherit_the_pin_and_the_post_script(self):
    sections = pinning.read_ini(ROOT / "platformio.ini")
    self.assertEqual(
        pinning.pinned_board(sections, "m5stick-s3-debug"), "board_M5StickS3")
    scripts = pinning.expand(
        pinning.inherited(sections, "m5stick-s3-debug", "extra_scripts") or "",
        sections, "extra_scripts")
    self.assertIn(pinning.PATCH_SCRIPT, scripts)

  def test_unpinned_env_passes(self):
    root, holder = self.make_tree()
    with holder:
      self.assertEqual(pinning.check(root, ["unpinned"], False), [])

  def test_dropped_pin_fails(self):
    root, holder = self.make_tree(
        INI.replace(" -DM5GFX_BOARD=board_t::board_M5StickC", ""))
    with holder:
      failures = pinning.check(root, ["m5stick-c"], False)
      self.assertTrue(
          any("no -DM5GFX_BOARD" in failure for failure in failures), failures)

  def test_wrong_pin_fails(self):
    root, holder = self.make_tree(
        INI.replace("board_t::board_M5StickC", "board_t::board_M5StickCPlus"))
    with holder:
      failures = pinning.check(root, ["m5stick-c"], False)
      self.assertTrue(
          any("expected board_M5StickC" in failure for failure in failures),
          failures)

  def test_env_without_the_post_script_fails(self):
    root, holder = self.make_tree()
    with holder:
      failures = pinning.check(root, ["m5stick-c-debug-no-script"], False)
      self.assertTrue(
          any(pinning.PATCH_SCRIPT in failure for failure in failures), failures)

  def test_missing_source_requires_a_build(self):
    root, holder = self.make_tree()
    with holder:
      failures = pinning.check(root, ["m5stick-c"], True)
      self.assertTrue(
          any("build the env" in failure for failure in failures), failures)

  def test_patched_displayless_pin_passes(self):
    root, holder = self.make_tree()
    with holder:
      write_source(root, "waveshare-s3-eth", PATCHED)
      self.assertEqual(pinning.check(root, ["waveshare-s3-eth"], True), [])

  def test_arm_pin_passes(self):
    root, holder = self.make_tree()
    with holder:
      write_source(root, "m5stick-c", PATCHED)
      self.assertEqual(pinning.check(root, ["m5stick-c"], True), [])

  def test_pin_without_an_arm_fails(self):
    root, holder = self.make_tree()
    with holder:
      write_source(root, "waveshare-s3-eth", WITHOUT_GUARD)
      failures = pinning.check(root, ["waveshare-s3-eth"], True)
      self.assertTrue(
          any("matches no arm" in failure for failure in failures), failures)

  def test_arm_removed_from_the_esp32_branch_fails(self):
    root, holder = self.make_tree()
    with holder:
      write_source(root, "m5stick-c", WITHOUT_ARM)
      failures = pinning.check(root, ["m5stick-c"], True)
      self.assertTrue(
          any("matches no arm" in failure for failure in failures), failures)

  def test_guard_after_the_first_probe_fails(self):
    root, holder = self.make_tree()
    with holder:
      write_source(root, "waveshare-s3-eth", GUARD_AFTER_PROBE)
      failures = pinning.check(root, ["waveshare-s3-eth"], True)
      self.assertTrue(
          any("is returned after" in failure for failure in failures), failures)

  def test_unpatched_source_fails(self):
    root, holder = self.make_tree()
    with holder:
      write_source(root, "waveshare-s3-eth", WITHOUT_MARKER)
      failures = pinning.check(root, ["waveshare-s3-eth"], True)
      self.assertTrue(
          any("carries no pin" in failure for failure in failures), failures)

  def test_missing_pin_assignment_fails(self):
    root, holder = self.make_tree()
    with holder:
      write_source(root, "m5stick-c", WITHOUT_ASSIGNMENT)
      failures = pinning.check(root, ["m5stick-c"], True)
      self.assertTrue(
          any("no longer starts from M5GFX_BOARD" in failure
              for failure in failures), failures)

  def test_unknown_env_is_an_error(self):
    root, holder = self.make_tree()
    with holder:
      failures = pinning.check(root, ["nope"], False)
      self.assertTrue(
          any("has no env nope" in failure for failure in failures), failures)


if __name__ == "__main__":
  unittest.main()
