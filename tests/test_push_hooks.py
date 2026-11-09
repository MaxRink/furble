"""The pre-push git hook and the Claude Code push gate (plan 314 PR 14)."""

import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
GATE_PATH = ROOT / ".claude" / "hooks" / "pre_push_gate.py"
SPEC = importlib.util.spec_from_file_location("pre_push_gate", GATE_PATH)
assert SPEC is not None and SPEC.loader is not None
GATE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = GATE
SPEC.loader.exec_module(GATE)


class PushDetectionTest(unittest.TestCase):
    def test_push_commands_are_detected(self):
        for command in ("git push", "git push -u origin feat/x",
                        "cd wt && git push", "git -C wt push origin HEAD",
                        "for i in 1 2; do git push && break; done"):
            self.assertTrue(GATE.is_push(command), command)

    def test_other_commands_and_no_verify_pass_through(self):
        for command in ("git status", "git pushd", "echo git", "gh pr create",
                        "git push --no-verify", "git commit -m 'push it'"):
            self.assertFalse(GATE.is_push(command), command)


class GateProcessTest(unittest.TestCase):
    def run_gate(self, event):
        return subprocess.run([sys.executable, str(GATE_PATH)], input=json.dumps(event),
                              capture_output=True, text=True, timeout=60)

    def test_a_non_push_call_is_allowed_without_review(self):
        result = self.run_gate({"tool_input": {"command": "ls"}, "cwd": str(ROOT)})
        self.assertEqual((result.returncode, result.stdout), (0, ""))

    def test_a_push_outside_the_repository_is_allowed(self):
        result = self.run_gate({"tool_input": {"command": "git push"}, "cwd": "/"})
        self.assertEqual(result.returncode, 0)

    def test_malformed_input_is_allowed(self):
        result = subprocess.run([sys.executable, str(GATE_PATH)], input="not json",
                                capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, 0)


class GitHookTest(unittest.TestCase):
    def test_a_branch_deletion_skips_the_review(self):
        line = "(delete) " + "0" * 40 + " refs/heads/old " + "1" * 40 + "\n"
        result = subprocess.run(["sh", str(ROOT / ".githooks" / "pre-push"), "origin", "url"],
                                input=line, cwd=ROOT, capture_output=True, text=True,
                                timeout=60)
        self.assertEqual(result.returncode, 0)
        self.assertNotIn("reviewing", result.stdout)

    def test_the_hook_is_executable(self):
        mode = (ROOT / ".githooks" / "pre-push").stat().st_mode
        self.assertTrue(mode & 0o111)


if __name__ == "__main__":
    unittest.main()
