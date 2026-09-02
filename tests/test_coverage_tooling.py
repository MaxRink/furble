"""Mutation-oriented contracts for the coverage floor logic.

The floor gate is the only thing standing between a coverage regression and a
green build, so the comparison itself needs tests. Everything here runs on
synthetic lcov text and synthetic summaries, so no clang, llvm or built
binaries are required.
"""
from pathlib import Path
import importlib.util
import json
import os
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("furble_coverage", ROOT / "tools/coverage.py")
assert SPEC and SPEC.loader
COVERAGE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = COVERAGE
SPEC.loader.exec_module(COVERAGE)

LCOV_HOST = """TN:
SF:/repo/src/FurbleControl.cpp
DA:10,4
DA:11,0
DA:12,0
DA:13,1
end_of_record
SF:/repo/lib/furble/Camera.cpp
DA:5,2
DA:6,0
end_of_record
SF:/repo/sim/driver.cpp
DA:1,9
end_of_record
"""

LCOV_SIM = """TN:
SF:/repo/src/FurbleControl.cpp
DA:10,0
DA:11,7
DA:12,0
DA:13,0
end_of_record
SF:/repo/src/FurbleUI.cpp
DA:1,1
DA:2,1
end_of_record
"""


def summary_from_lcov(root, *texts):
  """Build a full summary the way the tool does, from lcov text."""
  reports = {}
  for name, text in texts:
    report = COVERAGE.filter_firmware(
        COVERAGE.normalize_paths(COVERAGE.parse_lcov(text), root))
    reports[name] = report
  stacks = {name: COVERAGE.summarize(report) for name, report in reports.items()}
  union = COVERAGE.summarize(COVERAGE.union_coverage(list(reports.values())))
  return {"stacks": stacks, "union": union, "not_instrumented": []}


class LcovParsingTest(unittest.TestCase):
  def test_parse_reads_only_sf_and_da_records(self):
    parsed = COVERAGE.parse_lcov(LCOV_HOST)
    self.assertEqual(sorted(parsed), [
        "/repo/lib/furble/Camera.cpp",
        "/repo/sim/driver.cpp",
        "/repo/src/FurbleControl.cpp",
    ])
    self.assertEqual(parsed["/repo/src/FurbleControl.cpp"], {10: 4, 11: 0, 12: 0, 13: 1})

  def test_repeated_da_records_sum(self):
    parsed = COVERAGE.parse_lcov("SF:/repo/src/a.cpp\nDA:1,2\nDA:1,3\nend_of_record\n")
    self.assertEqual(parsed["/repo/src/a.cpp"], {1: 5})

  def test_da_records_outside_a_record_are_ignored(self):
    parsed = COVERAGE.parse_lcov("DA:1,5\nSF:/repo/src/a.cpp\nDA:2,1\nend_of_record\nDA:9,9\n")
    self.assertEqual(parsed, {"/repo/src/a.cpp": {2: 1}})

  def test_malformed_counts_do_not_raise(self):
    parsed = COVERAGE.parse_lcov("SF:/repo/src/a.cpp\nDA:x,y\nDA:3,1\nend_of_record\n")
    self.assertEqual(parsed["/repo/src/a.cpp"], {3: 1})

  def test_branch_suffixed_counts_are_read(self):
    # lcov permits a checksum field after the hit count.
    parsed = COVERAGE.parse_lcov("SF:/repo/src/a.cpp\nDA:3,2,abcdef\nend_of_record\n")
    self.assertEqual(parsed["/repo/src/a.cpp"], {3: 2})


class FirmwareFilterTest(unittest.TestCase):
  def test_only_firmware_directories_count(self):
    self.assertTrue(COVERAGE.is_firmware_path("src/FurbleUI.cpp"))
    self.assertTrue(COVERAGE.is_firmware_path("lib/furble/Camera.cpp"))
    self.assertTrue(COVERAGE.is_firmware_path("include/FurbleControl.h"))
    self.assertFalse(COVERAGE.is_firmware_path("sim/driver.cpp"))
    self.assertFalse(COVERAGE.is_firmware_path("tests/host/lifecycle_test.cpp"))
    self.assertFalse(COVERAGE.is_firmware_path("lib/furble-extra/x.cpp"))
    self.assertFalse(COVERAGE.is_firmware_path("srcx/x.cpp"))

  def test_paths_outside_the_repository_are_dropped(self):
    with tempfile.TemporaryDirectory() as directory:
      root = Path(directory)
      (root / "src").mkdir()
      report = {
          str(root / "src/a.cpp"): {1: 1},
          "/elsewhere/lvgl/lv_obj.c": {1: 1},
      }
      self.assertEqual(sorted(COVERAGE.normalize_paths(report, root)), ["src/a.cpp"])


class UnionTest(unittest.TestCase):
  def test_union_covers_a_line_either_stack_reached(self):
    host = COVERAGE.parse_lcov(LCOV_HOST)
    sim = COVERAGE.parse_lcov(LCOV_SIM)
    merged = COVERAGE.union_coverage([host, sim])
    control = merged["/repo/src/FurbleControl.cpp"]
    # Line 11 is reached only by the simulator, line 13 only by the host, and
    # line 12 by neither.
    self.assertGreater(control[10], 0)
    self.assertGreater(control[11], 0)
    self.assertEqual(control[12], 0)
    self.assertGreater(control[13], 0)
    covered, total = COVERAGE.file_totals(control)
    self.assertEqual((covered, total), (3, 4))

  def test_union_percentage_beats_either_stack(self):
    root = Path("/repo")
    summary = summary_from_lcov(root, ("host", LCOV_HOST), ("sim", LCOV_SIM))
    self.assertLess(summary["stacks"]["host"]["percent"], summary["union"]["percent"])
    self.assertLess(summary["stacks"]["sim"]["percent"], summary["union"]["percent"])

  def test_union_of_nothing_is_zero_not_a_division_error(self):
    self.assertEqual(COVERAGE.summarize({})["percent"], 0.0)
    self.assertEqual(COVERAGE.percent(0, 0), 0.0)


class SummaryShapeTest(unittest.TestCase):
  def setUp(self):
    self.summary = summary_from_lcov(Path("/repo"), ("host", LCOV_HOST), ("sim", LCOV_SIM))

  def test_per_file_and_total_line_counts_agree(self):
    union = self.summary["union"]
    self.assertEqual(union["covered"], sum(f["covered"] for f in union["files"].values()))
    self.assertEqual(union["total"], sum(f["total"] for f in union["files"].values()))

  def test_simulator_only_file_appears_in_the_union(self):
    self.assertIn("src/FurbleUI.cpp", self.summary["union"]["files"])
    self.assertNotIn("src/FurbleUI.cpp", self.summary["stacks"]["host"]["files"])

  def test_low_coverage_listing_uses_the_union(self):
    low = dict((row[0], row[1]) for row in COVERAGE.low_coverage_files(self.summary))
    # Camera.cpp is at 50 percent in the union, FurbleUI.cpp at 100 percent.
    self.assertNotIn("src/FurbleUI.cpp", low)
    self.assertNotIn("lib/furble/Camera.cpp", low)
    self.assertEqual(COVERAGE.low_coverage_files(self.summary, limit=60.0)[0][0],
                     "lib/furble/Camera.cpp")


class FloorComparisonTest(unittest.TestCase):
  def setUp(self):
    self.summary = summary_from_lcov(Path("/repo"), ("host", LCOV_HOST), ("sim", LCOV_SIM))
    self.floor = COVERAGE.floor_from_summary(self.summary)

  def test_a_floor_at_the_measurement_passes(self):
    self.assertEqual(COVERAGE.floor_failures(self.summary, self.floor), [])

  def test_a_stack_floor_above_the_measurement_fails(self):
    mutated = json.loads(json.dumps(self.floor))
    mutated["stacks"]["host"] = self.summary["stacks"]["host"]["percent"] + 0.01
    failures = COVERAGE.floor_failures(self.summary, mutated)
    self.assertEqual(len(failures), 1)
    self.assertIn("stack host", failures[0])
    self.assertIn("below floor", failures[0])
    # Restoring the floor clears the failure.
    self.assertEqual(COVERAGE.floor_failures(self.summary, self.floor), [])

  def test_a_union_floor_above_the_measurement_fails(self):
    mutated = json.loads(json.dumps(self.floor))
    mutated["union"] = self.summary["union"]["percent"] + 0.01
    failures = COVERAGE.floor_failures(self.summary, mutated)
    self.assertEqual(len(failures), 1)
    self.assertTrue(failures[0].startswith("union:"))

  def test_a_file_floor_above_the_measurement_fails(self):
    mutated = json.loads(json.dumps(self.floor))
    mutated["files"] = {"src/FurbleControl.cpp": 99.0}
    failures = COVERAGE.floor_failures(self.summary, mutated)
    self.assertEqual(len(failures), 1)
    self.assertIn("src/FurbleControl.cpp", failures[0])

  def test_a_vanished_stack_or_file_fails_instead_of_passing_silently(self):
    mutated = json.loads(json.dumps(self.floor))
    mutated["stacks"]["sim m5stick-c (80x160)"] = 10.0
    mutated["files"] = {"src/Deleted.cpp": 1.0}
    failures = COVERAGE.floor_failures(self.summary, mutated)
    self.assertEqual(len(failures), 2)
    self.assertTrue(any("no measurement" in failure for failure in failures))
    self.assertTrue(any("not measured" in failure for failure in failures))

  def test_ratchet_margin_lowers_every_value_and_clamps_at_zero(self):
    lowered = COVERAGE.floor_from_summary(self.summary, margin=1.0)
    self.assertAlmostEqual(lowered["union"], round(self.summary["union"]["percent"] - 1.0, 2))
    for name, value in lowered["stacks"].items():
      self.assertAlmostEqual(value, round(self.summary["stacks"][name]["percent"] - 1.0, 2))
    huge = COVERAGE.floor_from_summary(self.summary, margin=500.0)
    self.assertEqual(huge["union"], 0.0)

  def test_ratchet_tracks_only_the_critical_files(self):
    self.assertEqual(sorted(self.floor["files"]),
                     sorted(name for name in COVERAGE.CRITICAL_FILES
                            if name in self.summary["union"]["files"]))


class CommittedFloorTest(unittest.TestCase):
  """The floor shipped in the repository must be usable and honest."""

  def setUp(self):
    self.path = ROOT / COVERAGE.DEFAULT_FLOOR
    self.floor = json.loads(self.path.read_text(encoding="utf-8"))

  def test_shape(self):
    self.assertEqual(sorted(self.floor), ["files", "stacks", "union"])
    expected_stacks = ["host"] + [
        f"sim {board[0]} ({board[3]})" for board in COVERAGE.SIM_BOARDS
    ] + ["sim union"]
    self.assertEqual(sorted(self.floor["stacks"]), sorted(expected_stacks))
    self.assertEqual(sorted(self.floor["files"]), sorted(COVERAGE.CRITICAL_FILES))

  def test_every_value_is_a_percentage(self):
    values = [self.floor["union"], *self.floor["stacks"].values(),
              *self.floor["files"].values()]
    for value in values:
      self.assertIsInstance(value, (int, float))
      self.assertGreaterEqual(value, 0.0)
      self.assertLessEqual(value, 100.0)

  def test_the_union_floor_is_at_least_every_stack_floor(self):
    for name, value in self.floor["stacks"].items():
      self.assertGreaterEqual(self.floor["union"], value,
                              f"union floor is below the {name} floor")

  def test_tracked_files_exist(self):
    for name in self.floor["files"]:
      self.assertTrue((ROOT / name).is_file(), f"{name} is not in the tree")

  def test_mutating_the_committed_floor_upward_fails_the_check(self):
    """Raise one committed floor past 100 percent, in a temporary copy only."""
    with tempfile.TemporaryDirectory() as directory:
      copy = Path(directory) / "coverage_floor.json"
      copy.write_text(self.path.read_text(encoding="utf-8"), encoding="utf-8")
      mutated = json.loads(copy.read_text(encoding="utf-8"))
      mutated["union"] = 100.0
      copy.write_text(json.dumps(mutated), encoding="utf-8")

      summary = {
          "stacks": {name: {"covered": 1, "total": 1, "percent": value, "files": {}}
                     for name, value in self.floor["stacks"].items()},
          "union": {
              "covered": 1,
              "total": 1,
              "percent": self.floor["union"],
              "files": {name: {"covered": 1, "total": 1, "percent": value}
                        for name, value in self.floor["files"].items()},
          },
      }
      self.assertEqual(COVERAGE.floor_failures(summary, self.floor), [])
      failures = COVERAGE.floor_failures(
          summary, json.loads(copy.read_text(encoding="utf-8")))
      self.assertTrue(any(failure.startswith("union:") for failure in failures))
    # The committed file is untouched.
    self.assertEqual(json.loads(self.path.read_text(encoding="utf-8")), self.floor)


class RenderingTest(unittest.TestCase):
  def test_markdown_carries_the_sticky_marker_and_every_stack(self):
    summary = summary_from_lcov(Path("/repo"), ("host", LCOV_HOST), ("sim", LCOV_SIM))
    floor = COVERAGE.floor_from_summary(summary)
    text = COVERAGE.render_markdown(summary, floor)
    self.assertIn("<!-- furble-coverage-report -->", text)
    self.assertIn("| host |", text)
    self.assertIn("| sim |", text)
    self.assertIn("grand union", text)
    self.assertIn("src/FurbleControl.cpp", text)

  def test_markdown_without_a_floor_still_renders(self):
    summary = summary_from_lcov(Path("/repo"), ("host", LCOV_HOST))
    text = COVERAGE.render_markdown(summary, None)
    self.assertIn("n/a", text)

  def test_uninstrumented_sources_are_listed(self):
    summary = summary_from_lcov(Path("/repo"), ("host", LCOV_HOST))
    summary["not_instrumented"] = ["src/main.cpp"]
    self.assertIn("`src/main.cpp`", COVERAGE.render_markdown(summary, None))


class ToolDiscoveryTest(unittest.TestCase):
  def test_a_missing_llvm_cov_fails_with_a_clear_message(self):
    original_path = os.environ.get("PATH", "")
    original_override = os.environ.pop("LLVM_COV", None)
    with tempfile.TemporaryDirectory() as directory:
      os.environ["PATH"] = directory
      try:
        with self.assertRaises(COVERAGE.CoverageError) as caught:
          COVERAGE.find_llvm_tool("llvm-cov", "LLVM_COV")
        self.assertIn("llvm-cov was not found", str(caught.exception))
        self.assertIn("LLVM_COV", str(caught.exception))
      finally:
        os.environ["PATH"] = original_path
        if original_override is not None:
          os.environ["LLVM_COV"] = original_override

  def test_a_bad_override_is_rejected_rather_than_ignored(self):
    original = os.environ.get("LLVM_COV")
    os.environ["LLVM_COV"] = "/definitely/not/a/tool"
    try:
      with self.assertRaises(COVERAGE.CoverageError):
        COVERAGE.find_llvm_tool("llvm-cov", "LLVM_COV")
    finally:
      if original is None:
        os.environ.pop("LLVM_COV", None)
      else:
        os.environ["LLVM_COV"] = original


if __name__ == "__main__":
  unittest.main()
