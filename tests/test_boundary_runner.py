"""Fail-closed regression for the linked boundary conformance runner."""

import os
from pathlib import Path
import re
import stat
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
RUNNER = ROOT / "sim" / "scripts" / "run-boundary-conformance.sh"


def runner_tokens() -> list[str]:
    text = RUNNER.read_text(encoding="utf-8")
    block = text.split("for token in \\", 1)[1].split("; do", 1)[0]
    return re.findall(r'"([^"]+)"', block)


def fake_tool(directory: Path, name: str, body: str) -> None:
    path = directory / name
    path.write_text("#!/bin/sh\n" + body, encoding="utf-8")
    path.chmod(path.stat().st_mode | stat.S_IXUSR)


def run_with_fake_verdict(verdict: int):
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        tools = root / "bin"
        tools.mkdir()
        fake_tool(tools, "cmake", "exit 0\n")
        lines = "\n".join(f"echo '{token}'" for token in runner_tokens())
        fake_tool(tools, "ctest", f"{lines}\nexit {verdict}\n")
        env = dict(os.environ,
                   PATH=f"{tools}:{os.environ['PATH']}",
                   FURBLE_BOUNDARY_BUILD_DIR=str(root / "build"))
        return subprocess.run(["sh", str(RUNNER)], cwd=ROOT, env=env,
                              capture_output=True, text=True)


class BoundaryRunnerTest(unittest.TestCase):
    def test_failing_ctest_cannot_hide_behind_pass_tokens(self):
        result = run_with_fake_verdict(1)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("FAIL: boundary ctest exited 1", result.stderr)
        self.assertIn("headless_service_test: PASS", result.stdout)

    def test_successful_ctest_and_tokens_pass(self):
        result = run_with_fake_verdict(0)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Linked boundary conformance passed.", result.stdout)


if __name__ == "__main__":
    unittest.main()
