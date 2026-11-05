"""Regression tests for simulator coverage evidence checks."""

import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "check_sim_coverage", ROOT / "tools" / "check_sim_coverage.py")
assert SPEC and SPEC.loader
COVERAGE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = COVERAGE
SPEC.loader.exec_module(COVERAGE)


class ExecutableAssertionTest(unittest.TestCase):
    def test_comment_and_log_text_do_not_count(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "scenario.txt"
            path.write_text(
                "# assert heap.free 1\n"
                "seed tag heap.free\n"
                "print heap.free\n"
                "assert ui.page main\n"
                "assert_min heap.largest 1\n"
                "assert-eventually 100 gps.state standby\n",
                encoding="utf-8")
            assertions = COVERAGE.executable_assertions(path)
        self.assertNotIn("heap.free", assertions)
        self.assertIn("ui.page", assertions)
        self.assertIn("heap.largest", assertions)
        self.assertIn("gps.state", assertions)

    def test_shell_runner_evidence_must_be_a_grep_command(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            scenario = root / "check.sh"
            scenario.write_text(
                "# FUZZ SUMMARY in prose only\n"
                "echo 'FUZZ SUMMARY'\n"
                "grep -c '^FUZZ SUMMARY ' result.log\n",
                encoding="utf-8")
            old_root = COVERAGE.ROOT
            COVERAGE.ROOT = root
            try:
                ok, error = COVERAGE.assertion_evidence({
                    "id": "fuzz.example",
                    "scenario": "check.sh",
                    "assertions": ["FUZZ SUMMARY"],
                })
            finally:
                COVERAGE.ROOT = old_root
        self.assertTrue(ok, error)

    def test_missing_executable_assertion_is_reported(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            scenario = root / "scenario.txt"
            scenario.write_text("# assert heap.free 1\nprint heap.free\n", encoding="utf-8")
            old_root = COVERAGE.ROOT
            COVERAGE.ROOT = root
            try:
                ok, error = COVERAGE.assertion_evidence({
                    "id": "memory.example",
                    "scenario": "scenario.txt",
                    "assertions": ["heap.free"],
                })
            finally:
                COVERAGE.ROOT = old_root
        self.assertFalse(ok)
        self.assertIn("not executable", error)

    def test_permutation_template_requires_assert_lines(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            scenario = root / "template.txt"
            scenario.write_text("assert setting.fauxny 1\n", encoding="utf-8")
            old_root = COVERAGE.ROOT
            COVERAGE.ROOT = root
            try:
                errors, _ = COVERAGE.check_permutations({
                    "boards": ["sticks3"],
                    "axes": {},
                    "expected_count": 1,
                    "scenario": "template.txt",
                    "assertions": ["setting.fauxny"],
                })
            finally:
                COVERAGE.ROOT = old_root
        self.assertEqual(errors, [])


if __name__ == "__main__":
    unittest.main()
