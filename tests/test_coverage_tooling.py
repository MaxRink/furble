"""Mutation-oriented contracts for the coverage floor logic.

The floor gate is the only thing standing between a coverage regression and a
green build, so the comparison itself needs tests. Everything here runs on
synthetic lcov text and synthetic summaries, so no clang, llvm or built
binaries are required.
"""
from pathlib import Path
import importlib.util
import inspect
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
    self.floor = COVERAGE.measured_floor(self.summary)

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

  def test_measured_floor_margin_lowers_every_value_and_clamps_at_zero(self):
    lowered = COVERAGE.measured_floor(self.summary, margin=1.0)
    self.assertAlmostEqual(lowered["union"], round(self.summary["union"]["percent"] - 1.0, 2))
    for name, value in lowered["stacks"].items():
      self.assertAlmostEqual(value, round(self.summary["stacks"][name]["percent"] - 1.0, 2))
    huge = COVERAGE.measured_floor(self.summary, margin=500.0)
    self.assertEqual(huge["union"], 0.0)

  def test_measured_floor_tracks_only_the_critical_files(self):
    self.assertEqual(sorted(self.floor["files"]),
                     sorted(name for name in COVERAGE.CRITICAL_FILES
                            if name in self.summary["union"]["files"]))



class RatchetTest(unittest.TestCase):
  """--ratchet must be a ratchet. It may only move a floor upward."""

  def setUp(self):
    self.summary = summary_from_lcov(Path("/repo"), ("host", LCOV_HOST), ("sim", LCOV_SIM))
    self.existing = COVERAGE.measured_floor(self.summary)

  def regressed(self, union=None, host=None):
    """A copy of the measurement with lower percentages."""
    summary = json.loads(json.dumps(self.summary))
    if union is not None:
      summary["union"]["percent"] = union
    if host is not None:
      summary["stacks"]["host"]["percent"] = host
    return summary

  def test_a_regression_never_lowers_the_floor(self):
    regressed = self.regressed(union=10.0, host=5.0)
    updated, lowered = COVERAGE.ratchet_floor(self.existing, regressed)
    self.assertEqual(lowered, [])
    self.assertEqual(updated["union"], self.existing["union"])
    self.assertEqual(updated["stacks"]["host"], self.existing["stacks"]["host"])
    self.assertNotIn("lowered", updated)
    # The held floor still fails the check, so the regression is not hidden.
    self.assertTrue(COVERAGE.floor_failures(regressed, updated))

  def test_a_per_file_regression_never_lowers_its_floor(self):
    regressed = json.loads(json.dumps(self.summary))
    regressed["union"]["files"]["src/FurbleControl.cpp"]["percent"] = 1.0
    updated, lowered = COVERAGE.ratchet_floor(self.existing, regressed)
    self.assertEqual(lowered, [])
    self.assertEqual(updated["files"]["src/FurbleControl.cpp"],
                     self.existing["files"]["src/FurbleControl.cpp"])

  def test_an_improvement_raises_the_floor(self):
    improved = json.loads(json.dumps(self.summary))
    improved["union"]["percent"] = 99.0
    improved["stacks"]["host"]["percent"] = 90.0
    updated, lowered = COVERAGE.ratchet_floor(self.existing, improved, margin=1.0)
    self.assertEqual(lowered, [])
    self.assertEqual(updated["union"], 98.0)
    self.assertEqual(updated["stacks"]["host"], 89.0)

  def test_the_margin_cannot_walk_a_floor_downward(self):
    """Repeated ratchets at the same measurement must be a fixed point."""
    first, _ = COVERAGE.ratchet_floor(None, self.summary, margin=1.0)
    second, _ = COVERAGE.ratchet_floor(first, self.summary, margin=1.0)
    third, _ = COVERAGE.ratchet_floor(second, self.summary, margin=1.0)
    self.assertEqual(first, second)
    self.assertEqual(second, third)

  def test_lower_lowers_and_records_the_reason(self):
    regressed = self.regressed(union=10.0)
    updated, lowered = COVERAGE.ratchet_floor(
        self.existing, regressed, lower=True, reason="plan 161 removed a page")
    self.assertEqual(updated["union"], 10.0)
    self.assertEqual(len(lowered), 1)
    self.assertIn("union", lowered[0])
    self.assertEqual(updated["lowered"]["reason"], "plan 161 removed a page")
    self.assertEqual(updated["lowered"]["keys"], sorted(lowered))
    self.assertEqual(COVERAGE.floor_failures(regressed, updated), [])

  def test_lower_without_a_reason_is_refused(self):
    for reason in (None, "", "   "):
      with self.assertRaises(COVERAGE.CoverageError):
        COVERAGE.ratchet_floor(
            self.existing, self.regressed(union=10.0), lower=True, reason=reason)

  def test_lower_records_nothing_when_nothing_dropped(self):
    updated, lowered = COVERAGE.ratchet_floor(
        self.existing, self.summary, lower=True, reason="no drop expected")
    self.assertEqual(lowered, [])
    self.assertNotIn("lowered", updated)

  def test_a_new_stack_or_file_is_added(self):
    grown = json.loads(json.dumps(self.summary))
    grown["stacks"]["sim m5stack-core (320x240)"] = {
        "covered": 1, "total": 2, "percent": 50.0, "files": {}}
    grown["union"]["files"]["lib/furble/Scan.cpp"] = {
        "covered": 9, "total": 10, "percent": 90.0}
    updated, lowered = COVERAGE.ratchet_floor(self.existing, grown, margin=1.0)
    self.assertEqual(lowered, [])
    self.assertEqual(updated["stacks"]["sim m5stack-core (320x240)"], 49.0)
    self.assertEqual(updated["files"]["lib/furble/Scan.cpp"], 89.0)

  def test_a_vanished_key_is_kept_so_the_check_still_reports_it(self):
    existing = json.loads(json.dumps(self.existing))
    existing["stacks"]["sim m5stick-c (80x160)"] = 40.0
    existing["files"]["src/FurbleConsole.cpp"] = 80.0
    updated, _ = COVERAGE.ratchet_floor(existing, self.summary)
    self.assertEqual(updated["stacks"]["sim m5stick-c (80x160)"], 40.0)
    self.assertEqual(updated["files"]["src/FurbleConsole.cpp"], 80.0)
    failures = COVERAGE.floor_failures(self.summary, updated)
    self.assertTrue(any("no measurement" in failure for failure in failures))
    self.assertTrue(any("not measured" in failure for failure in failures))

  def test_ratcheting_the_committed_floor_with_a_regression_is_a_no_op(self):
    """The real regression the reviewer found, against the shipped floor."""
    committed = json.loads(
        (ROOT / COVERAGE.DEFAULT_FLOOR).read_text(encoding="utf-8"))
    # Every value is below every committed floor, so a correct ratchet must
    # change nothing at all.
    floor_values = [committed["union"], *committed["stacks"].values(),
                    *committed["files"].values()]
    dropped = min(floor_values) - 1.0
    self.assertGreater(dropped, 0.0)
    regressed = {
        "stacks": {name: {"covered": 1, "total": 2, "percent": dropped,
                          "files": {}}
                   for name in committed["stacks"]},
        "union": {"covered": 1, "total": 2, "percent": dropped,
                  "files": {name: {"covered": 1, "total": 2,
                                   "percent": dropped}
                            for name in committed["files"]}},
    }
    updated, lowered = COVERAGE.ratchet_floor(committed, regressed)
    self.assertEqual(lowered, [])
    self.assertEqual(updated["stacks"], committed["stacks"])
    self.assertEqual(updated["union"], committed["union"])
    self.assertEqual(updated["files"], committed["files"])
    self.assertTrue(COVERAGE.floor_failures(regressed, updated))


class CommittedFloorTest(unittest.TestCase):
  """The floor shipped in the repository must be usable and honest."""

  def setUp(self):
    self.path = ROOT / COVERAGE.DEFAULT_FLOOR
    self.floor = json.loads(self.path.read_text(encoding="utf-8"))

  def test_shape(self):
    self.assertLessEqual(set(self.floor), {"files", "stacks", "union", "lowered"})
    self.assertLessEqual({"files", "stacks", "union"}, set(self.floor))
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
    floor = COVERAGE.measured_floor(summary)
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


class ScenarioOutcomeTest(unittest.TestCase):
  """A scenario that never finished must fail the run, not vanish from it.

  The measured symptom this guards: the same commit reported 503 of 697 lines
  in lib/furble/Camera.cpp on one CI run and 521 of 697 on the next, because
  one scenario was killed on its timeout and contributed no profile. The run
  still exited zero and published the wrong number.
  """

  def test_a_timed_out_scenario_fails_the_run_and_is_named(self):
    failures = COVERAGE.incomplete_scenarios(
        [("sim/scenarios/e2e/scan-distinct-rows-heartbeat.txt", None)], 600.0
    )
    self.assertEqual(len(failures), 1)
    self.assertIn("scan-distinct-rows-heartbeat.txt", failures[0])
    self.assertIn("timed out after 600 s", failures[0])

  def test_a_crashed_scenario_fails_the_run_and_names_the_signal(self):
    failures = COVERAGE.incomplete_scenarios(
        [("sim/scenarios/e2e/boot.txt", -11)], 600.0
    )
    self.assertEqual(len(failures), 1)
    self.assertIn("boot.txt", failures[0])
    self.assertIn("signal 11", failures[0])

  def test_a_scenario_that_merely_failed_still_contributes_its_profile(self):
    self.assertEqual(
        COVERAGE.incomplete_scenarios(
            [("sim/scenarios/e2e/boot.txt", 1),
             ("sim/scenarios/invalid/bad-verb.txt", 2),
             ("sim/scenarios/e2e/gps.txt", 0)],
            600.0,
        ),
        [],
    )

  def test_every_incomplete_scenario_is_reported_not_just_the_first(self):
    failures = COVERAGE.incomplete_scenarios(
        [("one.txt", None), ("two.txt", 0), ("three.txt", -6)], 42.5
    )
    self.assertEqual(len(failures), 2)
    self.assertIn("one.txt", failures[0])
    self.assertIn("42.5 s", failures[0])
    self.assertIn("three.txt", failures[1])

  def test_the_board_measurement_raises_rather_than_noting_the_loss(self):
    """The classification above only helps if the caller acts on it.

    This is the line that regressed: measure_sim_board() used to print a note
    for a timed out scenario and carry on with the profiles it did have. Assert
    the wiring, so restoring the note without the raise fails here.
    """
    source = inspect.getsource(COVERAGE.measure_sim_board)
    self.assertIn("incomplete_scenarios(results", source)
    self.assertIn("raise CoverageError", source)
    self.assertNotIn("contributed no profile", source)


class CrashedHostTestTest(unittest.TestCase):
  """A host test that crashed must be named, not folded into an exit code.

  Issue #275: tests/host console-commands segfaulted about one coverage run in
  two, on the control task, and only under instrumentation. The run failed with
  `command failed with exit 8` and named nothing, so a crash in a suite of 93
  read exactly like an assertion failure. A killed process also writes no
  profile, which is the same broken measurement incomplete_scenarios() exists
  to catch on the simulator side.
  """

  # Verbatim ctest 3.25 output, tabs included, so a format change fails here.
  CTEST_MIXED = (
      "2/2 Test #2: fail-test ........................***Failed    0.00 sec\n"
      "\n"
      "0% tests passed, 2 tests failed out of 2\n"
      "\n"
      "Total Test time (real) =   0.00 sec\n"
      "\n"
      "The following tests FAILED:\n"
      "\t  1 - console-commands (SEGFAULT)\n"
      "\t  2 - fail-test (Failed)\n"
      "Errors while running CTest\n"
  )

  def test_a_segfaulting_test_is_named_with_its_reason(self):
    crashed = COVERAGE.crashed_host_tests(self.CTEST_MIXED)
    self.assertEqual(len(crashed), 1)
    self.assertIn("console-commands", crashed[0])
    self.assertIn("SEGFAULT", crashed[0])

  def test_an_ordinary_failure_is_not_reported_as_a_crash(self):
    crashed = COVERAGE.crashed_host_tests(self.CTEST_MIXED)
    self.assertFalse([entry for entry in crashed if "fail-test" in entry])

  def test_every_way_a_process_can_be_lost_counts(self):
    output = (
        "The following tests FAILED:\n"
        "\t  1 - a (Timeout)\n"
        "\t  2 - b (Subprocess aborted)\n"
        "\t  3 - c (Not Run)\n"
        "\t  4 - d (ILLEGAL)\n"
        "\t  5 - e (Failed)\n"
    )
    crashed = COVERAGE.crashed_host_tests(output)
    self.assertEqual(len(crashed), 4)
    self.assertIn("a: Timeout", crashed[0])
    self.assertIn("d: ILLEGAL", crashed[3])

  def test_a_green_run_reports_nothing(self):
    self.assertEqual(
        COVERAGE.crashed_host_tests(
            "100% tests passed, 0 tests failed out of 93\n"
        ),
        [],
    )

  def test_test_names_before_the_summary_block_are_not_parsed(self):
    """Only the summary block names tests. The per-test progress lines above
    it carry no reason, and a captured test's own stdout may contain anything.
    """
    output = (
        "1/2 Test #1: console-commands .............***Exception: SegFault\n"
        "  9 - not a real row (SEGFAULT)\n"
        "The following tests FAILED:\n"
        "\t  1 - console-commands (SEGFAULT)\n"
    )
    crashed = COVERAGE.crashed_host_tests(output)
    self.assertEqual(len(crashed), 1)
    self.assertIn("console-commands", crashed[0])

  def test_the_host_measurement_raises_and_names_rather_than_exiting_blind(self):
    """The classification only helps if measure_host() acts on it.

    This is the wiring that was missing: the ctest call went through run(),
    which raises on any non-zero exit with the command line and nothing else.
    """
    source = inspect.getsource(COVERAGE.measure_host)
    self.assertIn("crashed_host_tests(output)", source)
    self.assertIn("raise CoverageError", source)
    # run() discards the output it would need, so the ctest call must not go
    # back through it.
    self.assertIn("run_streamed(", source)


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
