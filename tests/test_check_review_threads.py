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


def checks_page(nodes, has_next=False, cursor=None, rollup=True):
    commit = {"oid": "0123456789abcdef", "statusCheckRollup": None}
    if rollup:
        commit["statusCheckRollup"] = {
            "contexts": {
                "pageInfo": {"hasNextPage": has_next, "endCursor": cursor},
                "nodes": nodes,
            }
        }
    return {
        "data": {
            "repository": {
                "pullRequest": {"commits": {"nodes": [{"commit": commit}]}}
            }
        }
    }


def check_run(name="build", status="COMPLETED", conclusion="SUCCESS"):
    return {"__typename": "CheckRun", "name": name, "status": status,
            "conclusion": conclusion}


PASSING_CHECKS = completed(checks_page([check_run()]))


def gh(threads, checks=PASSING_CHECKS):
    """Answer the review-thread query and the check query separately."""
    thread_pages = list(threads)
    check_pages = list(checks) if isinstance(checks, list) else [checks]

    def run(command, **_):
        query = next(arg for arg in command if arg.startswith("query="))
        pages = check_pages if "statusCheckRollup" in query else thread_pages
        return pages.pop(0) if len(pages) > 1 else pages[0]

    return run


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
                               side_effect=gh([completed(page([thread(False)]))])) as run:
            code, _, stderr = self.run_gate("--repo", "owner/name", "--pr", "391")
        self.assertEqual(code, 1)
        self.assertIn("src/file.cpp:12", stderr)
        self.assertIn("finding", stderr)
        self.assertEqual(run.call_args_list[0].args[0][:3], ["gh", "api", "graphql"])

    def test_all_resolved_passes(self):
        with mock.patch.object(REVIEW.subprocess, "run",
                               side_effect=gh([completed(page([thread(True)]))])):
            code, stdout, _ = self.run_gate("--repo", "owner/name", "--pr", "391")
        self.assertEqual(code, 0)
        self.assertIn("all resolved", stdout)
        self.assertIn("all passed or skipped", stdout)

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
        with mock.patch.object(REVIEW.subprocess, "run", side_effect=gh(pages)):
            code, _, stderr = self.run_gate("--repo", "owner/name", "--pr", "391")
        self.assertEqual(code, 1)
        self.assertIn("src/second.cpp:7", stderr)

    def run_checks(self, checks):
        threads = [completed(page([thread(True)]))]
        with mock.patch.object(REVIEW.subprocess, "run", side_effect=gh(threads, checks)):
            return self.run_gate("--repo", "owner/name", "--pr", "391")

    def test_head_without_checks_fails(self):
        # Actions was disabled on the fork and PRs merged with no CI at all.
        code, _, stderr = self.run_checks(completed(checks_page([], rollup=False)))
        self.assertEqual(code, 1)
        self.assertIn("no CI check ran on head 0123456789ab", stderr)

    def test_head_without_checks_passes_only_with_a_reason(self):
        # A web-installer-only PR matches no pull-request workflow.
        threads = [completed(page([thread(True)]))]
        checks = completed(checks_page([], rollup=False))
        with mock.patch.object(REVIEW.subprocess, "run", side_effect=gh(threads, checks)):
            code, stdout, _ = self.run_gate(
                "--repo", "owner/name", "--pr", "391",
                "--allow-no-checks", "web-installer only; no workflow covers it")
        self.assertEqual(code, 0)
        self.assertIn("accepted: web-installer only", stdout)
        with mock.patch.object(REVIEW.subprocess, "run", side_effect=gh(threads, checks)):
            code, _, _ = self.run_gate("--repo", "owner/name", "--pr", "391",
                                       "--allow-no-checks", "  ")
        self.assertEqual(code, 1)

    def test_reason_does_not_hide_unresolved_threads(self):
        threads = [completed(page([thread(False)]))]
        checks = completed(checks_page([], rollup=False))
        with mock.patch.object(REVIEW.subprocess, "run", side_effect=gh(threads, checks)):
            code, _, _ = self.run_gate("--repo", "owner/name", "--pr", "391",
                                       "--allow-no-checks", "docs only")
        self.assertEqual(code, 1)

    def test_failed_and_cancelled_checks_fail(self):
        code, _, stderr = self.run_checks(completed(checks_page([
            check_run("clang-format", conclusion="FAILURE"),
            check_run("apple-project", conclusion="CANCELLED"),
            check_run("docs", conclusion="SKIPPED"),
        ])))
        self.assertEqual(code, 1)
        self.assertIn("2 of 3 check(s) did not pass", stderr)
        self.assertIn("clang-format: failure", stderr)
        self.assertIn("apple-project: cancelled", stderr)
        self.assertNotIn("docs", stderr)

    def test_running_check_fails(self):
        code, _, stderr = self.run_checks(completed(checks_page([
            check_run("build", status="IN_PROGRESS", conclusion=None),
        ])))
        self.assertEqual(code, 1)
        self.assertIn("build: in_progress", stderr)

    def test_failed_status_context_fails(self):
        code, _, stderr = self.run_checks(completed(checks_page([
            {"__typename": "StatusContext", "context": "ci/legacy", "state": "ERROR"},
        ])))
        self.assertEqual(code, 1)
        self.assertIn("ci/legacy: error", stderr)

    def test_check_pagination_collects_every_page(self):
        code, _, stderr = self.run_checks([
            completed(checks_page([check_run()], has_next=True, cursor="next")),
            completed(checks_page([check_run("late", conclusion="TIMED_OUT")])),
        ])
        self.assertEqual(code, 1)
        self.assertIn("late: timed_out", stderr)

    def test_unresolved_thread_still_fails_when_checks_pass(self):
        with mock.patch.object(REVIEW.subprocess, "run",
                               side_effect=gh([completed(page([thread(False)]))])):
            code, _, _ = self.run_gate("--repo", "owner/name", "--pr", "391")
        self.assertEqual(code, 1)

    def test_invalid_repo_is_a_usage_error(self):
        code, _, _ = self.run_gate("--repo", "not-a-repo", "--pr", "1")
        self.assertEqual(code, 2)


if __name__ == "__main__":
    unittest.main()
