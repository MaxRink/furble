"""Mutation-oriented contracts for simulator ownership and triggers."""
from pathlib import Path
import importlib.util
import json
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("check_sim_scenarios", ROOT / "tools/check_sim_scenarios.py")
assert SPEC and SPEC.loader
CHECKER = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = CHECKER
SPEC.loader.exec_module(CHECKER)
CI_SPEC = importlib.util.spec_from_file_location("check_ci_workflows", ROOT / "tools/check_ci_workflows.py")
assert CI_SPEC and CI_SPEC.loader
CI_CHECKER = importlib.util.module_from_spec(CI_SPEC)
sys.modules[CI_SPEC.name] = CI_CHECKER
CI_SPEC.loader.exec_module(CI_CHECKER)

class SimCIContractTest(unittest.TestCase):
  def copy_fixture(self, root: Path) -> Path:
    scenarios = root / "sim/scenarios"
    scenarios.mkdir(parents=True)
    for path in (ROOT / "sim/scenarios").rglob("*.txt"):
      target = scenarios / path.relative_to(ROOT / "sim/scenarios")
      target.parent.mkdir(parents=True, exist_ok=True)
      target.touch()
    manifest = root / "manifest.json"
    manifest.write_text((ROOT / "sim/scenarios/manifest.json").read_text(), encoding="utf-8")
    return manifest

  def test_manifest_is_complete(self):
    self.assertEqual(CHECKER.check_manifest(ROOT, ROOT / "sim/scenarios/manifest.json"), [])

  def test_new_unlisted_scenario_is_rejected(self):
    with tempfile.TemporaryDirectory() as directory:
      root = Path(directory)
      manifest = self.copy_fixture(root)
      (root / "sim/scenarios/e2e/new.txt").touch()
      self.assertIn("missing manifest entry: sim/scenarios/e2e/new.txt", CHECKER.check_manifest(root, manifest))

  def test_renamed_scenario_reports_stale_and_missing(self):
    with tempfile.TemporaryDirectory() as directory:
      root = Path(directory)
      manifest = self.copy_fixture(root)
      old = root / "sim/scenarios/e2e/connect-flow.txt"
      old.rename(old.with_name("connect-renamed.txt"))
      errors = CHECKER.check_manifest(root, manifest)
      self.assertIn("missing manifest entry: sim/scenarios/e2e/connect-renamed.txt", errors)
      self.assertIn("stale manifest entry: sim/scenarios/e2e/connect-flow.txt", errors)

  def test_duplicate_entry_and_boolean_exit_are_rejected(self):
    with tempfile.TemporaryDirectory() as directory:
      root = Path(directory)
      manifest = self.copy_fixture(root)
      document = json.loads(manifest.read_text())
      duplicate = dict(document["scenarios"][0])
      duplicate["expected_exit"] = True
      document["scenarios"].append(duplicate)
      manifest.write_text(json.dumps(document), encoding="utf-8")
      errors = CHECKER.check_manifest(root, manifest)
      self.assertTrue(any("duplicate manifest entry" in error for error in errors))
      self.assertTrue(any("expected_exit must be an integer" in error for error in errors))

  def test_nested_certified_e2e_is_selected_by_manifest_runner(self):
    with tempfile.TemporaryDirectory() as directory:
      root = Path(directory)
      manifest = self.copy_fixture(root)
      nested = root / "sim/scenarios/e2e/nested/new.txt"
      nested.parent.mkdir(parents=True)
      nested.touch()
      document = json.loads(manifest.read_text())
      document["scenarios"].append({
        "path": "sim/scenarios/e2e/nested/new.txt",
        "suite": "e2e",
        "owner": "sim-e2e",
        "boards": ["m5stick-s3"],
        "capabilities": [],
        "expected_exit": 0,
        "certified": True,
      })
      manifest.write_text(json.dumps(document), encoding="utf-8")
      self.assertEqual(CHECKER.check_manifest(root, manifest), [])
      self.assertIn("sim/scenarios/e2e/nested/new.txt",
                    CHECKER.certified_paths(root, manifest, "e2e", "m5stick-s3"))

  def test_certified_bughunt_is_selected_per_board_and_noncertified_is_skipped(self):
    with tempfile.TemporaryDirectory() as directory:
      root = Path(directory)
      manifest = self.copy_fixture(root)
      document = json.loads(manifest.read_text())
      certified = next(entry for entry in document["scenarios"]
                       if entry["path"] == "sim/scenarios/bughunt/page-matrix.txt")
      historical = next(entry for entry in document["scenarios"]
                        if entry["path"] == "sim/scenarios/bughunt/interval-back-trap.txt")
      selected = CHECKER.certified_paths(root, manifest, "bughunt", "m5stack-core")
      self.assertIn(certified["path"], selected)
      self.assertNotIn(historical["path"], selected)

  def test_unknown_scenario_subdirectory_is_rejected(self):
    with tempfile.TemporaryDirectory() as directory:
      root = Path(directory)
      manifest = self.copy_fixture(root)
      unknown = root / "sim/scenarios/orphan/new.txt"
      unknown.parent.mkdir(parents=True)
      unknown.touch()
      document = json.loads(manifest.read_text())
      document["scenarios"].append({
        "path": "sim/scenarios/orphan/new.txt",
        "suite": "power-gate",
        "owner": "sim-power",
        "boards": ["m5stick-s3"],
        "capabilities": [],
        "expected_exit": 0,
        "certified": True,
      })
      manifest.write_text(json.dumps(document), encoding="utf-8")
      self.assertIn("sim/scenarios/orphan/new.txt: path is outside a supported suite",
                    CHECKER.check_manifest(root, manifest))

  def test_required_ci_suite_board_selection_cannot_be_empty(self):
    with tempfile.TemporaryDirectory() as directory:
      root = Path(directory)
      manifest = self.copy_fixture(root)
      document = json.loads(manifest.read_text())
      for entry in document["scenarios"]:
        if entry["suite"] == "bughunt" and "m5stack-core" in entry["boards"]:
          entry["certified"] = False
          entry["reason"] = "mutation fixture"
      manifest.write_text(json.dumps(document), encoding="utf-8")
      error = "bughunt: no certified scenarios for required board m5stack-core"
      self.assertIn(error, CHECKER.check_manifest(root, manifest))
      with self.assertRaisesRegex(ValueError, error):
        CHECKER.certified_paths(root, manifest, "bughunt", "m5stack-core")

  def test_trigger_paths_are_exact_and_equivalent(self):
    text = (ROOT / ".github/workflows/sim-e2e.yml").read_text(encoding="utf-8")
    document = CI_CHECKER._load_text(text)
    expected = ["src/**", "include/**", "lib/furble/**", "lib/preferences/**", "lib/blowfish/**", "components/icons/**", "sim/**", "tests/host/nimble/**", "tests/host/peer/**", "tests/corpus/**", "sdkconfig.*", "tools/gen_lv_conf.py", "tools/check_sim_scenarios.py", "sim/scenarios/manifest.json", ".github/workflows/sim-e2e.yml"]
    self.assertEqual(document["on"]["pull_request"]["paths"], expected)
    self.assertEqual(document["on"]["push"]["paths"], expected)
    self.assertNotIn("- 'lib/**'", text)
    self.assertNotIn("- 'test/**'", text)
    self.assertEqual(text.count("Build 320x240 (M5Stack Core) simulator"), 1)
    self.assertIn("--list-certified --suite bughunt --board m5stick-s3", text)
    self.assertIn("--list-certified --suite bughunt --board \"$board\"", text)
    self.assertIn("FURBLE_SIM_BOARD_ID=m5stick-c", text)
    self.assertIn("FURBLE_SIM_BOARD_ID=m5stack-core", text)
    self.assertGreaterEqual(text.count("if ! scenarios=$(python3 tools/check_sim_scenarios.py"), 2)
    self.assertGreaterEqual(text.count('if [ -z "$scenarios" ]'), 2)

    power = (ROOT / ".github/workflows/power-gate.yml").read_text(encoding="utf-8")
    self.assertIn("--list-certified --suite power-gate --board m5stick-s3", power)
    self.assertIn("- 'tools/check_sim_scenarios.py'", power)
    self.assertIn("if ! scenarios=$(python3 tools/check_sim_scenarios.py", power)
    self.assertIn('if [ -z "$scenarios" ]', power)
