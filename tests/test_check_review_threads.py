"""Contract tests for the online review-thread gate."""

import base64
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
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

# The base commit the pull request targets. The gate reads workflow filters here,
# never from the checkout, which can be stale against that commit.
BASE_OID = "0123456789abcdef0123456789abcdef01234567"


def base_page(oid=BASE_OID):
    return {"data": {"repository": {"pullRequest": {"baseRefOid": oid}}}}


def base_page_without_oid():
    return {"data": {"repository": {"pullRequest": {}}}}


def workflow_paths(patterns):
    """A pull-request workflow whose path filter lists patterns."""
    lines = "".join(f"      - '{pattern}'\n" for pattern in patterns)
    return f"on:\n  pull_request:\n    paths:\n{lines}"


BASE = completed(base_page())


def gh(threads, checks=PASSING_CHECKS, base=BASE):
    """Answer the review-thread, check and base-oid queries separately."""
    thread_pages = list(threads)
    check_pages = list(checks) if isinstance(checks, list) else [checks]
    base_pages = list(base) if isinstance(base, list) else [base]

    def run(command, **_):
        query = next(arg for arg in command if arg.startswith("query="))
        if "baseRefOid" in query:
            pages = base_pages
        elif "statusCheckRollup" in query:
            pages = check_pages
        else:
            pages = thread_pages
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

    def test_non_object_graphql_payload_fails_closed(self):
        # gh can print a JSON array or a bare string; neither may reach .get().
        with mock.patch.object(REVIEW.subprocess, "run",
                               return_value=completed(["not", "an", "object"])):
            code, _, stderr = self.run_gate("--repo", "owner/name", "--pr", "391")
        self.assertEqual(code, 2)
        self.assertIn("not a JSON object", stderr)

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

    def run_override(self, changed, *extra, workflows=None):
        """Run the gate on a head with no checks, with the base texts stubbed."""
        threads = [completed(page([thread(True)]))]
        checks = completed(checks_page([], rollup=False))
        texts = {"main.yml": workflow_paths(["src/**"])} if workflows is None else workflows
        with mock.patch.object(REVIEW.subprocess, "run", side_effect=gh(threads, checks)), \
                mock.patch.object(REVIEW, "run_changed_files", return_value=changed), \
                mock.patch.object(REVIEW, "fetch_workflows", return_value=texts), \
                mock.patch.object(REVIEW, "read_local_workflows",
                                  side_effect=AssertionError("the gate read the checkout")):
            return self.run_gate("--repo", "owner/name", "--pr", "391", *extra)

    def test_no_checks_override_holds_when_no_workflow_covers_the_paths(self):
        # A web-installer-only PR matches no pull-request workflow at its base.
        code, stdout, _ = self.run_override(
            ["web-installer/index.html"], "--allow-no-checks", "web-installer only")
        self.assertEqual(code, 0)
        self.assertIn("accepted: web-installer only", stdout)
        self.assertIn(f"base {BASE_OID[:12]}", stdout)

    def test_no_checks_override_is_refused_when_a_workflow_should_have_run(self):
        # An Actions outage on a source change must still fail, whatever the reason says.
        code, _, stderr = self.run_override(
            ["src/FurbleUI.cpp"], "--allow-no-checks", "outage")
        self.assertEqual(code, 1)
        self.assertIn("--allow-no-checks refused", stderr)
        self.assertIn("main.yml", stderr)

    def test_gate_fetches_the_workflows_at_the_base_ref(self):
        # The seam is bound to the oid the payload returned, not to a branch
        # name and not to the checkout.
        threads = [completed(page([thread(True)]))]
        checks = completed(checks_page([], rollup=False))
        with mock.patch.object(REVIEW.subprocess, "run", side_effect=gh(threads, checks)), \
                mock.patch.object(REVIEW, "run_changed_files",
                                  return_value=["web-installer/index.html"]), \
                mock.patch.object(REVIEW, "fetch_workflows", return_value={}) as fetch:
            code, _, _ = self.run_gate("--repo", "owner/name", "--pr", "391",
                                       "--allow-no-checks", "installer only")
        self.assertEqual(code, 0)
        fetch.assert_called_once_with("owner", "name", BASE_OID)

    def test_override_is_refused_when_the_base_workflow_covers_a_changed_path(self):
        # Issue #464. The clone's own copy of main.yml does not cover
        # web-installer/**, which is the premise: the old local-tree rule
        # accepted the override here. The base copy does cover it, so the gate
        # must refuse. Both copies are synthetic so this test does not depend on
        # the checkout's current workflow set.
        stale_clone = {"main.yml": workflow_paths(["src/**"])}
        self.assertEqual(
            REVIEW.covering_workflows(["web-installer/index.html"], workflows=stale_clone), [])
        code, _, stderr = self.run_override(
            ["web-installer/index.html"], "--allow-no-checks", "installer only",
            workflows={"main.yml": workflow_paths(["web-installer/**"])})
        self.assertEqual(code, 1)
        self.assertIn("--allow-no-checks refused", stderr)
        self.assertIn("main.yml", stderr)

    def test_override_is_refused_when_the_pr_edits_a_workflow(self):
        # The #450 guard, and the reason a base-only read is not enough. The
        # stub base covers nothing for these paths, so the base filters alone
        # would accept. The pull request's own copy of main.yml broadens the
        # filter to web-installer/**, and GitHub evaluates that copy for its
        # pull_request run, so a head with no checks is an Actions failure.
        broadened = {"main.yml": workflow_paths(["src/**", "web-installer/**"])}
        self.assertEqual(
            REVIEW.covering_workflows(["web-installer/index.html"], workflows=broadened),
            ["main.yml"])
        self.assertEqual(
            REVIEW.covering_workflows(["web-installer/index.html"],
                                      workflows={"main.yml": workflow_paths(["src/**"])}),
            [])
        code, _, stderr = self.run_override(
            ["web-installer/index.html", ".github/workflows/main.yml"],
            "--allow-no-checks", "installer only")
        self.assertEqual(code, 1)
        self.assertIn("--allow-no-checks refused", stderr)
        self.assertIn(".github/workflows/main.yml", stderr)

    def test_override_is_refused_for_a_deleted_workflow_and_a_workflow_directory(self):
        # A deletion and a nested path are both edits to what CI runs.
        for changed in ([".github/workflows/main.yml"],
                        [".github/workflows/pages/x.yml"]):
            with self.subTest(changed=changed):
                code, _, stderr = self.run_override(
                    changed, "--allow-no-checks", "installer only")
                self.assertEqual(code, 1)
                self.assertIn("--allow-no-checks refused", stderr)

    def test_missing_base_oid_fails_closed(self):
        threads = [completed(page([thread(True)]))]
        checks = completed(checks_page([], rollup=False))
        with mock.patch.object(
                REVIEW.subprocess, "run",
                side_effect=gh(threads, checks, base=completed(base_page_without_oid()))), \
                mock.patch.object(REVIEW, "run_changed_files",
                                  return_value=["web-installer/index.html"]), \
                mock.patch.object(REVIEW, "fetch_workflows") as fetch:
            code, _, stderr = self.run_gate(
                "--repo", "owner/name", "--pr", "391", "--allow-no-checks", "installer only")
        self.assertEqual(code, 2)
        self.assertIn("failed closed", stderr)
        self.assertIn("baseRefOid", stderr)
        self.assertFalse(fetch.called)

    def test_workflow_fetch_error_fails_closed(self):
        threads = [completed(page([thread(True)]))]
        checks = completed(checks_page([], rollup=False))
        with mock.patch.object(REVIEW.subprocess, "run", side_effect=gh(threads, checks)), \
                mock.patch.object(REVIEW, "run_changed_files",
                                  return_value=["web-installer/index.html"]), \
                mock.patch.object(REVIEW, "fetch_workflows",
                                  side_effect=REVIEW.ReviewQueryError("HTTP 404")):
            code, _, stderr = self.run_gate(
                "--repo", "owner/name", "--pr", "391", "--allow-no-checks", "installer only")
        self.assertEqual(code, 2)
        self.assertIn("HTTP 404", stderr)

    def test_unreadable_base_workflow_fails_closed(self):
        # A base workflow that does not parse cannot be shown to miss the paths.
        code, _, stderr = self.run_override(
            ["web-installer/index.html"], "--allow-no-checks", "installer only",
            workflows={"main.yml": "on: [pull_request\n"})
        self.assertEqual(code, 2)
        self.assertIn("failed closed", stderr)

    def test_blank_override_reason_is_no_override(self):
        code, _, _ = self.run_override(["web-installer/index.html"], "--allow-no-checks", "  ")
        self.assertEqual(code, 1)

    def test_reason_does_not_hide_unresolved_threads(self):
        threads = [completed(page([thread(False)]))]
        checks = completed(checks_page([], rollup=False))
        with mock.patch.object(REVIEW.subprocess, "run", side_effect=gh(threads, checks)), \
                mock.patch.object(REVIEW, "run_changed_files",
                                  return_value=["web-installer/index.html"]), \
                mock.patch.object(REVIEW, "fetch_workflows", return_value={}):
            code, _, _ = self.run_gate("--repo", "owner/name", "--pr", "391",
                                       "--allow-no-checks", "docs only")
        self.assertEqual(code, 1)

    def test_filter_globs_follow_github_semantics(self):
        self.assertTrue(REVIEW.filter_matches(["src/**"], "src/a/b.cpp"))
        self.assertFalse(REVIEW.filter_matches(["src/*"], "src/a/b.cpp"))
        self.assertTrue(REVIEW.filter_matches(["**/*.md"], "README.md"))
        self.assertTrue(REVIEW.filter_matches(["**/*.md"], "docs/x/y.md"))
        self.assertFalse(REVIEW.filter_matches(["src/**", "!src/gen/**"], "src/gen/a.c"))
        self.assertTrue(REVIEW.filter_matches(["sdkconfig*"], "sdkconfig.m5stick-s3"))

    def test_paths_ignore_runs_unless_every_file_is_ignored(self):
        with tempfile.TemporaryDirectory() as scratch:
            workflows = Path(scratch)
            (workflows / "ci.yml").write_text(
                "on:\n  pull_request:\n    paths-ignore:\n      - 'web-installer/**'\n",
                encoding="utf-8")
            (workflows / "push.yml").write_text("on: push\n", encoding="utf-8")
            self.assertEqual(
                REVIEW.covering_workflows(["web-installer/a.js"], workflows), [])
            self.assertEqual(
                REVIEW.covering_workflows(["web-installer/a.js", "src/a.cpp"], workflows),
                ["ci.yml"])

    def test_local_mode_reads_the_checkout_when_no_texts_are_given(self):
        # Local mode is the test-only fallback: covering_workflows with no texts
        # reads the checkout on disk. --pr is mandatory, so the gate cannot reach
        # it. This test pins that supplied texts take precedence over the same
        # directory.
        with tempfile.TemporaryDirectory() as scratch:
            workflows = Path(scratch)
            (workflows / "ci.yml").write_text(workflow_paths(["src/**"]), encoding="utf-8")
            self.assertEqual(REVIEW.covering_workflows(["src/a.cpp"], workflows), ["ci.yml"])
            self.assertEqual(
                REVIEW.covering_workflows(
                    ["src/a.cpp"], workflows,
                    workflows={"base.yml": workflow_paths(["docs/**"])}),
                [])

    def test_supplied_texts_replace_the_checkout_entirely(self):
        self.assertEqual(
            REVIEW.covering_workflows(
                ["src/a.cpp"], Path("/nonexistent-workflows"),
                workflows={"main.yml": workflow_paths(["src/**"])}),
            ["main.yml"])

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


class BaseRefOidTest(unittest.TestCase):
    def test_base_ref_oid_comes_from_the_pull_request_payload(self):
        with mock.patch.object(REVIEW.subprocess, "run", return_value=BASE):
            self.assertEqual(REVIEW.run_base_ref_oid("owner", "name", 391), BASE_OID)

    def test_malformed_base_ref_oid_is_refused(self):
        with mock.patch.object(REVIEW.subprocess, "run",
                               return_value=completed(base_page(oid="master"))):
            with self.assertRaises(REVIEW.ReviewQueryError) as raised:
                REVIEW.run_base_ref_oid("owner", "name", 391)
        self.assertIn("baseRefOid", str(raised.exception))

    def test_api_error_on_the_base_query_fails_closed(self):
        with mock.patch.object(REVIEW.subprocess, "run",
                               return_value=completed({}, returncode=1, stderr="auth")):
            with self.assertRaises(REVIEW.ReviewQueryError):
                REVIEW.run_base_ref_oid("owner", "name", 391)


class BaseWorkflowFetchTest(unittest.TestCase):
    def listing(self, *entries):
        return [{"name": name, "path": f".github/workflows/{name}", "type": kind}
                for name, kind in entries]

    def document(self, text):
        return {"encoding": "base64",
                "content": base64.b64encode(text.encode("utf-8")).decode("ascii")}

    def run_fetch(self, documents):
        def run(command, **_):
            return completed(documents[command[2]])

        return mock.patch.object(REVIEW.subprocess, "run", side_effect=run)

    def test_fetch_reads_every_workflow_at_the_base_ref(self):
        listing = f"repos/owner/name/contents/.github/workflows?ref={BASE_OID}"
        documents = {
            listing: self.listing(("main.yml", "file"), ("notes.txt", "file"),
                                  ("nested", "dir")),
            f"repos/owner/name/contents/.github/workflows/main.yml?ref={BASE_OID}":
                self.document(workflow_paths(["src/**"])),
        }
        with self.run_fetch(documents) as stub:
            texts = REVIEW.fetch_workflows("owner", "name", BASE_OID)
        self.assertEqual(texts, {"main.yml": workflow_paths(["src/**"])})
        # The listing and every file are read at the base commit, not at a branch.
        for call in stub.call_args_list:
            self.assertIn(f"ref={BASE_OID}", call.args[0][2])

    def test_fetch_skips_a_directory_entry(self):
        listing = f"repos/owner/name/contents/.github/workflows?ref={BASE_OID}"
        documents = {listing: self.listing(("archived", "dir"))}
        with self.run_fetch(documents):
            self.assertEqual(REVIEW.fetch_workflows("owner", "name", BASE_OID), {})

    def test_fetch_fails_closed_when_a_workflow_entry_has_no_type(self):
        # A missing type could hide a workflow file, so the entry is not skipped.
        listing = f"repos/owner/name/contents/.github/workflows?ref={BASE_OID}"
        documents = {listing: [{"name": "main.yml", "path": ".github/workflows/main.yml"}]}
        with self.run_fetch(documents):
            with self.assertRaises(REVIEW.ReviewQueryError) as raised:
                REVIEW.fetch_workflows("owner", "name", BASE_OID)
        self.assertIn("main.yml", str(raised.exception))

    def test_fetch_fails_closed_when_a_workflow_entry_type_is_unknown(self):
        listing = f"repos/owner/name/contents/.github/workflows?ref={BASE_OID}"
        documents = {listing: [{"name": "main.yml", "path": ".github/workflows/main.yml",
                                "type": "blob"}]}
        with self.run_fetch(documents):
            with self.assertRaises(REVIEW.ReviewQueryError) as raised:
                REVIEW.fetch_workflows("owner", "name", BASE_OID)
        self.assertIn("blob", str(raised.exception))

    def test_fetch_fails_closed_when_a_workflow_name_is_not_a_file(self):
        # A .yml entry that is not a file has filters the gate cannot read.
        listing = f"repos/owner/name/contents/.github/workflows?ref={BASE_OID}"
        documents = {listing: [{"name": "main.yml", "path": ".github/workflows/main.yml",
                                "type": "dir"}]}
        with self.run_fetch(documents):
            with self.assertRaises(REVIEW.ReviewQueryError) as raised:
                REVIEW.fetch_workflows("owner", "name", BASE_OID)
        self.assertIn("not a workflow file", str(raised.exception))

    def test_fetch_fails_closed_when_a_workflow_has_no_text(self):
        # The contents API sends an empty content and encoding "none" above 1 MB.
        listing = f"repos/owner/name/contents/.github/workflows?ref={BASE_OID}"
        documents = {
            listing: self.listing(("main.yml", "file")),
            f"repos/owner/name/contents/.github/workflows/main.yml?ref={BASE_OID}":
                {"encoding": "none", "content": ""},
        }
        with self.run_fetch(documents):
            with self.assertRaises(REVIEW.ReviewQueryError) as raised:
                REVIEW.fetch_workflows("owner", "name", BASE_OID)
        self.assertIn("main.yml", str(raised.exception))
        self.assertIn(BASE_OID[:12], str(raised.exception))

    def test_fetch_fails_closed_on_an_api_error(self):
        with mock.patch.object(REVIEW.subprocess, "run",
                               return_value=completed({}, returncode=1, stderr="HTTP 404")):
            with self.assertRaises(REVIEW.ReviewQueryError) as raised:
                REVIEW.fetch_workflows("owner", "name", BASE_OID)
        self.assertIn("HTTP 404", str(raised.exception))

    def test_fetch_fails_closed_when_the_listing_is_not_a_directory(self):
        with mock.patch.object(REVIEW.subprocess, "run",
                               return_value=completed({"message": "Not Found"})):
            with self.assertRaises(REVIEW.ReviewQueryError) as raised:
                REVIEW.fetch_workflows("owner", "name", BASE_OID)
        self.assertIn("directory", str(raised.exception))

    def test_fetch_fails_closed_when_gh_is_missing(self):
        with mock.patch.object(REVIEW.subprocess, "run", side_effect=FileNotFoundError("gh")):
            with self.assertRaises(REVIEW.ReviewQueryError) as raised:
                REVIEW.fetch_workflows("owner", "name", BASE_OID)
        self.assertIn("could not run gh", str(raised.exception))


if __name__ == "__main__":
    unittest.main()
