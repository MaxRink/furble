"""Regression tests for the sim-first flash readiness result checks."""

import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "check_flash_readiness", ROOT / "tools" / "check_flash_readiness.py")
assert SPEC and SPEC.loader
READINESS = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = READINESS
SPEC.loader.exec_module(READINESS)


FUZZ_MANIFEST = {
    "targets": [
        {"id": "fuzz.one", "status": "modeled"},
        {"id": "fuzz.two", "status": "modeled"},
    ]
}

PERM_MANIFEST = {
    "boards": ["sticks3"],
    "axes": {"mqtt": ["on", "off"], "tls": ["plain", "tls"]},
    "expected_count": 4,
}


class FuzzResultTest(unittest.TestCase):
    def test_all_targets_present_and_passing(self):
        document = {"status": "pass", "targets": [
            {"id": "fuzz.one", "status": "pass"},
            {"id": "fuzz.two", "status": "pass"},
        ]}
        self.assertEqual(READINESS.check_fuzz_results(FUZZ_MANIFEST, document), [])

    def test_missing_target_is_a_blocker(self):
        document = {"status": "pass", "targets": [{"id": "fuzz.one", "status": "pass"}]}
        errors = READINESS.check_fuzz_results(FUZZ_MANIFEST, document)
        self.assertTrue(any("missing: fuzz.two" in error for error in errors))

    def test_individual_failure_is_a_blocker_even_when_top_level_passes(self):
        document = {"status": "pass", "targets": [
            {"id": "fuzz.one", "status": "pass"},
            {"id": "fuzz.two", "status": "fail"},
        ]}
        errors = READINESS.check_fuzz_results(FUZZ_MANIFEST, document)
        self.assertTrue(any("not passing: fuzz.two" in error for error in errors))

    def test_unknown_and_duplicate_results_are_blockers(self):
        document = {"status": "pass", "targets": [
            {"id": "fuzz.one", "status": "pass"},
            {"id": "fuzz.one", "status": "pass"},
            {"id": "fuzz.extra", "status": "pass"},
        ]}
        errors = READINESS.check_fuzz_results(FUZZ_MANIFEST, document)
        self.assertTrue(any("duplicated" in error for error in errors))
        self.assertTrue(any("no manifest target" in error for error in errors))


class BoundaryRunnerTest(unittest.TestCase):
    def docs(self, runner):
        return ({"entries": [{"runner": runner}]}, {"crashes": []})

    def test_failing_runner_is_a_blocker(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            runner = root / "boundary.sh"
            runner.write_text("#!/bin/sh\necho 'headless_service_test: PASS'\nexit 1\n",
                              encoding="utf-8")
            runner.chmod(0o755)
            errors = READINESS.check_boundary_runners(
                *self.docs("boundary.sh"), root=root, execute=True)
        self.assertTrue(any("boundary runner failed (1)" in error for error in errors))

    def test_missing_or_non_executable_runner_is_a_blocker(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            runner = root / "boundary.sh"
            runner.write_text("#!/bin/sh\nexit 0\n", encoding="utf-8")
            runner.chmod(0o644)
            errors = READINESS.check_boundary_runners(
                *self.docs("boundary.sh"), root=root, execute=True)
            missing = READINESS.check_boundary_runners(
                *self.docs("missing.sh"), root=root, execute=True)
        self.assertTrue(any("not executable" in error for error in errors))
        self.assertTrue(any("missing" in error for error in missing))

    def test_successful_runner_passes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            runner = root / "boundary.sh"
            runner.write_text("#!/bin/sh\nexit 0\n", encoding="utf-8")
            runner.chmod(0o755)
            errors = READINESS.check_boundary_runners(
                *self.docs("boundary.sh"), root=root, execute=True)
        self.assertEqual(errors, [])


class MemoryScenarioSetTest(unittest.TestCase):
    def test_missing_and_unexpected_dumps_are_blockers(self):
        errors = READINESS.memory_scenario_set_errors({"a", "b"}, {"a", "c"})
        self.assertIn("memory dump missing: c", errors)
        self.assertIn("memory dump unexpected: b", errors)

    def test_exact_set_is_clean(self):
        self.assertEqual(
            READINESS.memory_scenario_set_errors({"a", "b"}, {"b", "a"}), [])


class PermutationResultTest(unittest.TestCase):
    def expected(self):
        entries = []
        for mqtt in ("on", "off"):
            for tls in ("plain", "tls"):
                entries.append({
                    "id": f"sticks3.mqtt={mqtt}.tls={tls}",
                    "board": "sticks3",
                    "combination": {"mqtt": mqtt, "tls": tls},
                    "passed": True,
                })
        return {"results": entries}

    def test_complete_unique_order_matches(self):
        self.assertEqual(READINESS.check_permutation_results(PERM_MANIFEST, self.expected()), [])

    def test_incomplete_results_are_blockers(self):
        document = self.expected()
        document["results"].pop()
        errors = READINESS.check_permutation_results(PERM_MANIFEST, document)
        self.assertTrue(any("incomplete" in error for error in errors))

    def test_duplicate_and_reordered_results_are_blockers(self):
        document = self.expected()
        document["results"][1] = dict(document["results"][0])
        errors = READINESS.check_permutation_results(PERM_MANIFEST, document)
        self.assertTrue(any("not unique" in error for error in errors))
        self.assertTrue(any("order mismatch" in error for error in errors))

    def test_failed_result_is_a_blocker(self):
        document = self.expected()
        document["results"][2]["passed"] = False
        errors = READINESS.check_permutation_results(PERM_MANIFEST, document)
        self.assertTrue(any("failed" in error for error in errors))


if __name__ == "__main__":
    unittest.main()
