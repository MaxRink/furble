"""Contract tests for the online review-thread gate."""

import importlib.util
import io
import json
from pathlib import Path
import subprocess
import sys
import unittest
from contextlib import redirect_stderr, redirect_stdout
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "check_review_threads", ROOT / "tools" / "check_review_threads.py")
assert SPEC and SPEC.loader
REVIEW = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = REVIEW
SPEC.loader.exec_module(REVIEW)


def completed(payload, returncode=0, stderr=""):
    return subprocess.CompletedProcess(
        args=["gh"], returncode=returncode, stdout=json.dumps(payload), stderr=stderr)


def page(nodes, has_next=False, cursor=None):
    return {
        "data": {
            "repository": {
                "pullRequest": {
                    "reviewThreads": {
                        "pageInfo": {"hasNextPage": has_next, "endCursor": cursor},
                        "nodes": nodes,
                    }
                }
            }
        }
    }


def thread(resolved, path="src/file.cpp", line=12, body="finding"):
    return {
        "isResolved": resolved,
        "isOutdated": False,
        "path": path,
        "line": line,
        "originalLine": line,
        "comments": {"nodes": [{"author": {"login": "reviewer"}, "body": body}]},
    }


class ReviewThreadGateTest(unittest.TestCase):
    def run_gate(self, *args):
        stdout = io.StringIO()
        stderr = io.StringIO()
        with redirect_stdout(stdout), redirect_stderr(stderr):
            code = REVIEW.main(list(args))
        return code, stdout.getvalue(), stderr.getvalue()

    def test_unresolved_thread_fails_and_prints_location(self):
        with mock.patch.object(REVIEW.subprocess, "run",
                               return_value=completed(page([thread(False)]))) as run:
            code, _, stderr = self.run_gate("--repo", "owner/name", "--pr", "391")
        self.assertEqual(code, 1)
        self.assertIn("src/file.cpp:12", stderr)
        self.assertIn("finding", stderr)
        self.assertEqual(run.call_args.args[0][:3], ["gh", "api", "graphql"])

    def test_all_resolved_passes(self):
        with mock.patch.object(REVIEW.subprocess, "run",
                               return_value=completed(page([thread(True)]))):
            code, stdout, _ = self.run_gate("--repo", "owner/name", "--pr", "391")
        self.assertEqual(code, 0)
        self.assertIn("all resolved", stdout)

    def test_api_error_fails_closed(self):
        with mock.patch.object(REVIEW.subprocess, "run",
                               return_value=completed({}, returncode=1, stderr="auth")):
            code, _, stderr = self.run_gate("--repo", "owner/name", "--pr", "391")
        self.assertEqual(code, 2)
        self.assertIn("failed closed", stderr)

    def test_missing_gh_fails_closed(self):
        with mock.patch.object(REVIEW.subprocess, "run", side_effect=FileNotFoundError("gh")):
            code, _, stderr = self.run_gate("--repo", "owner/name", "--pr", "391")
        self.assertEqual(code, 2)
        self.assertIn("could not run gh", stderr)

    def test_graphql_error_fails_closed(self):
        with mock.patch.object(REVIEW.subprocess, "run",
                               return_value=completed({"errors": [{"message": "boom"}]})):
            code, _, stderr = self.run_gate("--repo", "owner/name", "--pr", "391")
        self.assertEqual(code, 2)
        self.assertIn("GraphQL errors", stderr)

    def test_pagination_collects_every_page(self):
        pages = [
            completed(page([thread(True)], has_next=True, cursor="next")),
            completed(page([thread(False, path="src/second.cpp", line=7)])),
        ]
        with mock.patch.object(REVIEW.subprocess, "run", side_effect=pages):
            code, _, stderr = self.run_gate("--repo", "owner/name", "--pr", "391")
        self.assertEqual(code, 1)
        self.assertIn("src/second.cpp:7", stderr)

    def test_invalid_repo_is_a_usage_error(self):
        code, _, _ = self.run_gate("--repo", "not-a-repo", "--pr", "1")
        self.assertEqual(code, 2)


if __name__ == "__main__":
    unittest.main()
