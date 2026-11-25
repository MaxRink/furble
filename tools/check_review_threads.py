#!/usr/bin/env python3
"""Fail closed while any GitHub review thread on a PR is unresolved.

The policy is: no merge before every review finding is fixed, or proven false
or outdated with evidence. The remediation PR itself must pass this check
before merge. This tool is deliberately online-only and is not wired into the
offline pre-review or fork CI jobs, where no GitHub token is available.

The same gate also fails when the PR head commit has no check runs at all, or
any check that is still running or did not pass. GitHub Actions was disabled
on the fork for three weeks and about thirty PRs merged with no CI; a head
with zero checks is now a hard failure, not a quiet pass.

A head with no checks still passes with --allow-no-checks REASON, but only when
no pull-request workflow would have run on the changed paths. That decision is
made from the workflow files at the pull request's base commit, read through
the contents API at baseRefOid, because a checkout can be stale against that
commit and a PR's own copy of a workflow is not evidence of what CI should have
run. covering_workflows() also has a mode-distinct local path: with no workflow
texts it reads .github/workflows from the checkout on disk. That path is a
test-only fallback. --pr is mandatory, so the gate never reaches it, and it
never decides a pull request. A missing base oid, an API error, an unfetchable
or unparseable base workflow, and a workflow whose text the API truncates are
all failures, never a pass.

Usage:
  python3 tools/check_review_threads.py --repo owner/name --pr 123
"""
from __future__ import annotations

import argparse
import base64
import json
from pathlib import Path
import re
import subprocess
import sys


QUERY = """
query($owner: String!, $name: String!, $number: Int!, $endCursor: String) {
  repository(owner: $owner, name: $name) {
    pullRequest(number: $number) {
      reviewThreads(first: 100, after: $endCursor) {
        pageInfo {
          hasNextPage
          endCursor
        }
        nodes {
          isResolved
          isOutdated
          path
          line
          originalLine
          comments(first: 1) {
            nodes {
              author { login }
              body
            }
          }
        }
      }
    }
  }
}
"""


CHECKS_QUERY = """
query($owner: String!, $name: String!, $number: Int!, $endCursor: String) {
  repository(owner: $owner, name: $name) {
    pullRequest(number: $number) {
      commits(last: 1) {
        nodes {
          commit {
            oid
            statusCheckRollup {
              contexts(first: 100, after: $endCursor) {
                pageInfo {
                  hasNextPage
                  endCursor
                }
                nodes {
                  __typename
                  ... on CheckRun { name status conclusion }
                  ... on StatusContext { context state }
                }
              }
            }
          }
        }
      }
    }
  }
}
"""

# A check run with one of these conclusions passed or was skipped on purpose.
PASSING_CONCLUSIONS = {"SUCCESS", "NEUTRAL", "SKIPPED"}


BASE_QUERY = """
query($owner: String!, $name: String!, $number: Int!) {
  repository(owner: $owner, name: $name) {
    pullRequest(number: $number) {
      baseRefOid
    }
  }
}
"""

WORKFLOWS_PATH = ".github/workflows"

# The entry types the contents API returns. Anything else fails closed, so a
# workflow file cannot hide behind an unknown type.
ENTRY_TYPES = {"file", "dir", "symlink", "submodule"}


class ReviewQueryError(RuntimeError):
    """An API or response-shape failure. Never treated as a clean result."""


def graphql(query: str, owner: str, name: str, number: int,
            cursor: str | None) -> dict:
    command = [
        "gh", "api", "graphql",
        "-f", f"query={query}",
        "-f", f"owner={owner}",
        "-f", f"name={name}",
        "-F", f"number={number}",
    ]
    if cursor is not None:
        command += ["-f", f"endCursor={cursor}"]
    try:
        result = subprocess.run(command, capture_output=True, text=True)
    except OSError as error:
        raise ReviewQueryError(f"could not run gh api graphql: {error}") from error
    if result.returncode != 0:
        raise ReviewQueryError(
            f"gh api graphql failed with exit {result.returncode}: "
            f"{result.stderr.strip() or result.stdout.strip()}")
    try:
        payload = json.loads(result.stdout)
    except json.JSONDecodeError as error:
        raise ReviewQueryError(f"gh returned invalid JSON: {error}") from error
    if not isinstance(payload, dict):
        raise ReviewQueryError(
            f"gh returned {type(payload).__name__}, not a JSON object")
    if payload.get("errors"):
        raise ReviewQueryError(f"GraphQL errors: {payload['errors']!r}")
    return payload


def run_graphql(owner: str, name: str, number: int) -> list[dict]:
    threads: list[dict] = []
    cursor: str | None = None
    while True:
        payload = graphql(QUERY, owner, name, number, cursor)
        try:
            review_threads = payload["data"]["repository"]["pullRequest"]["reviewThreads"]
            nodes = review_threads["nodes"]
            page_info = review_threads["pageInfo"]
        except (KeyError, TypeError) as error:
            raise ReviewQueryError(
                f"GraphQL response is missing review threads: {error}") from error
        if not isinstance(nodes, list):
            raise ReviewQueryError("GraphQL reviewThreads.nodes is not a list")
        threads.extend(nodes)
        if not page_info.get("hasNextPage"):
            break
        cursor = page_info.get("endCursor")
        if not cursor:
            raise ReviewQueryError("reviewThreads requested another page without a cursor")
    return threads


WORKFLOW_DIR = Path(__file__).resolve().parents[1] / ".github" / "workflows"


def api_json(path: str, what: str) -> object:
    """Read one REST path through gh and fail closed on any error."""
    command = ["gh", "api", path]
    try:
        result = subprocess.run(command, capture_output=True, text=True)
    except OSError as error:
        raise ReviewQueryError(f"could not run gh api for {what}: {error}") from error
    if result.returncode != 0:
        raise ReviewQueryError(
            f"gh api {what} failed with exit {result.returncode}: "
            f"{result.stderr.strip() or result.stdout.strip()}")
    try:
        return json.loads(result.stdout)
    except json.JSONDecodeError as error:
        raise ReviewQueryError(f"gh api {what} returned invalid JSON: {error}") from error


def run_base_ref_oid(owner: str, name: str, number: int) -> str:
    """Return the base commit oid the pull request targets, or fail closed."""
    payload = graphql(BASE_QUERY, owner, name, number, None)
    try:
        oid = payload["data"]["repository"]["pullRequest"]["baseRefOid"]
    except (KeyError, TypeError) as error:
        raise ReviewQueryError(
            f"GraphQL response is missing baseRefOid: {error}") from error
    if not isinstance(oid, str) or not re.fullmatch(r"[0-9a-f]{40}|[0-9a-f]{64}", oid):
        raise ReviewQueryError(f"pull request baseRefOid is missing or malformed: {oid!r}")
    return oid


def fetch_workflows(owner: str, name: str, ref: str) -> dict[str, str]:
    """Return every workflow text as it exists at one commit.

    The listing and each file are read through the contents API at `ref`, so
    the filters belong to that commit and never to a branch or a checkout. This
    is the gate's only network seam and the unit tests stub it. An API error, a
    missing entry and a text the API truncates all raise ReviewQueryError.
    """
    shorter = ref[:12]
    listing = api_json(f"repos/{owner}/{name}/contents/{WORKFLOWS_PATH}?ref={ref}",
                       f"the workflow directory at {shorter}")
    if not isinstance(listing, list):
        raise ReviewQueryError(
            f"contents API did not return a workflow directory at {shorter}: {listing!r}")
    texts: dict[str, str] = {}
    for entry in listing:
        if not isinstance(entry, dict):
            raise ReviewQueryError(f"contents API returned a malformed entry at {shorter}")
        entry_name = entry.get("name")
        entry_type = entry.get("type")
        # An entry with no known type could hide a workflow, so it fails closed.
        if entry_type not in ENTRY_TYPES:
            raise ReviewQueryError(
                f"contents API returned type {entry_type!r} for {entry_name!r} at {shorter}")
        if not isinstance(entry_name, str):
            raise ReviewQueryError(
                f"contents API returned no name for a {entry_type} entry at {shorter}")
        if not entry_name.endswith((".yml", ".yaml")):
            continue
        # A workflow name that is not a file is not something GitHub runs, and
        # guessing past it would let a filter go unread.
        if entry_type != "file":
            raise ReviewQueryError(
                f"{entry_name} at {shorter} is a {entry_type}, not a workflow file")
        path = entry.get("path")
        if not isinstance(path, str) or not path.startswith(WORKFLOWS_PATH + "/"):
            raise ReviewQueryError(
                f"contents API returned path {path!r} for {entry_name} at {shorter}")
        document = api_json(f"repos/{owner}/{name}/contents/{path}?ref={ref}",
                            f"{entry_name} at {shorter}")
        if not isinstance(document, dict):
            raise ReviewQueryError(
                f"contents API returned no file for {entry_name} at {shorter}")
        content = document.get("content")
        encoding = document.get("encoding")
        # Above 1 MB the contents API sends an empty content and encoding "none".
        if encoding != "base64" or not isinstance(content, str):
            raise ReviewQueryError(
                f"{entry_name} at {shorter} has no base64 text (encoding {encoding!r}), "
                "so its path filters cannot be read")
        try:
            texts[entry_name] = base64.b64decode(content).decode("utf-8")
        except (ValueError, UnicodeDecodeError) as error:
            raise ReviewQueryError(
                f"could not decode {entry_name} at {shorter}: {error}") from error
    return texts


def read_local_workflows(workflow_dir: Path = WORKFLOW_DIR) -> dict[str, str]:
    """Return the workflow texts in a checkout. This is the test-only fallback."""
    texts: dict[str, str] = {}
    for path in sorted([*workflow_dir.glob("*.yml"), *workflow_dir.glob("*.yaml")]):
        try:
            texts[path.name] = path.read_text(encoding="utf-8")
        except OSError as error:
            raise ReviewQueryError(f"could not read {path.name}: {error}") from error
    return texts


def run_changed_files(owner: str, name: str, number: int) -> list[str]:
    """Return every path the pull request changes."""
    command = ["gh", "api", "--paginate", f"repos/{owner}/{name}/pulls/{number}/files",
               "--jq", ".[].filename"]
    try:
        result = subprocess.run(command, capture_output=True, text=True)
    except OSError as error:
        raise ReviewQueryError(f"could not run gh api: {error}") from error
    if result.returncode != 0:
        raise ReviewQueryError(
            f"gh api pulls/{number}/files failed with exit {result.returncode}: "
            f"{result.stderr.strip() or result.stdout.strip()}")
    return [line for line in result.stdout.splitlines() if line]


def filter_regex(pattern: str) -> re.Pattern[str]:
    """Translate a GitHub Actions path filter glob into a regex."""
    out = []
    index = 0
    while index < len(pattern):
        if pattern.startswith("**/", index):
            out.append("(?:.*/)?")
            index += 3
        elif pattern.startswith("**", index):
            out.append(".*")
            index += 2
        elif pattern[index] == "*":
            out.append("[^/]*")
            index += 1
        elif pattern[index] == "?":
            out.append("[^/]")
            index += 1
        else:
            out.append(re.escape(pattern[index]))
            index += 1
    return re.compile("".join(out) + r"\Z")


def filter_matches(patterns: list[str], path: str) -> bool:
    """Apply GitHub's ordered include and !exclude filter patterns to one path."""
    matched = False
    for pattern in patterns:
        if pattern.startswith("!"):
            if filter_regex(pattern[1:]).match(path):
                matched = False
        elif filter_regex(pattern).match(path):
            matched = True
    return matched


def covering_workflows(files: list[str], workflow_dir: Path = WORKFLOW_DIR,
                       workflows: dict[str, str] | None = None) -> list[str]:
    """Return the pull-request workflows whose path filters select any file.

    Two modes. With `workflows` the texts are the ones fetched at the pull
    request base commit and they alone decide; the merge gate always uses this
    mode. Without it the texts come from `workflow_dir` in the checkout. That
    local mode is a test-only fallback: --pr is mandatory, so the gate cannot
    reach it.
    """
    try:
        import yaml
    except ImportError as error:
        raise ReviewQueryError(f"PyYAML is required to read workflow filters: {error}") from error
    texts = read_local_workflows(workflow_dir) if workflows is None else workflows
    covering = []
    for name in sorted(texts):
        try:
            document = yaml.safe_load(texts[name]) or {}
        except yaml.YAMLError as error:
            raise ReviewQueryError(f"could not parse {name}: {error}") from error
        if not isinstance(document, dict):
            raise ReviewQueryError(f"{name} is not a YAML mapping")
        trigger = document.get("on", document.get(True))
        if isinstance(trigger, str):
            trigger = {trigger: None}
        elif isinstance(trigger, list):
            trigger = {trigger_name: None for trigger_name in trigger}
        if not isinstance(trigger, dict) or "pull_request" not in trigger:
            continue
        config = trigger["pull_request"] or {}
        if "paths" in config:
            runs = any(filter_matches(list(config["paths"]), file) for file in files)
        elif "paths-ignore" in config:
            runs = any(not filter_matches(list(config["paths-ignore"]), file) for file in files)
        else:
            runs = bool(files)
        if runs:
            covering.append(name)
    return covering


def run_checks(owner: str, name: str, number: int) -> tuple[str, list[dict]]:
    """Return the PR head SHA and every check run and status on it."""
    contexts: list[dict] = []
    head = ""
    cursor: str | None = None
    while True:
        payload = graphql(CHECKS_QUERY, owner, name, number, cursor)
        try:
            commit = payload["data"]["repository"]["pullRequest"]["commits"]["nodes"][0]["commit"]
            head = commit["oid"]
            rollup = commit["statusCheckRollup"]
        except (KeyError, IndexError, TypeError) as error:
            raise ReviewQueryError(
                f"GraphQL response is missing the head commit: {error}") from error
        if rollup is None:
            # GitHub returns no rollup when nothing reported on the commit.
            return head, contexts
        try:
            page = rollup["contexts"]
            nodes = page["nodes"]
            page_info = page["pageInfo"]
        except (KeyError, TypeError) as error:
            raise ReviewQueryError(
                f"GraphQL response is missing check contexts: {error}") from error
        if not isinstance(nodes, list):
            raise ReviewQueryError("GraphQL statusCheckRollup.contexts.nodes is not a list")
        contexts.extend(nodes)
        if not page_info.get("hasNextPage"):
            return head, contexts
        cursor = page_info.get("endCursor")
        if not cursor:
            raise ReviewQueryError("check contexts requested another page without a cursor")


def check_problem(context: dict) -> str | None:
    """Describe a check that is not a pass, or return None when it passed."""
    if context.get("__typename") == "StatusContext":
        state = context.get("state")
        if state == "SUCCESS":
            return None
        return f"{context.get('context') or 'unknown status'}: {str(state).lower()}"
    label = context.get("name") or "unknown check"
    if context.get("status") != "COMPLETED":
        return f"{label}: {str(context.get('status')).lower()}"
    conclusion = context.get("conclusion")
    if conclusion in PASSING_CONCLUSIONS:
        return None
    return f"{label}: {str(conclusion).lower()}"


def first_comment(thread: dict) -> tuple[str, str]:
    try:
        comment = thread["comments"]["nodes"][0]
    except (KeyError, IndexError, TypeError):
        return "unknown", ""
    author = "unknown"
    if isinstance(comment.get("author"), dict):
        author = comment["author"].get("login") or "unknown"
    body = comment.get("body") or ""
    return author, body


def describe(thread: dict) -> str:
    path = thread.get("path") or "<no path>"
    line = thread.get("line") or thread.get("originalLine") or "?"
    author, body = first_comment(thread)
    outdated = " outdated" if thread.get("isOutdated") else ""
    body = " ".join(str(body).split())
    if len(body) > 400:
        body = body[:397] + "..."
    return f"{path}:{line} ({author}){outdated}: {body}"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", required=True,
                        help="GitHub repository as owner/name")
    parser.add_argument("--pr", required=True, type=int,
                        help="pull request number")
    parser.add_argument("--allow-no-checks", metavar="REASON", default="",
                        help="accept a head with no check runs, for a PR whose "
                             "paths no workflow covers at its base commit; the "
                             "reason is printed")
    args = parser.parse_args(argv)

    if "/" not in args.repo or args.repo.count("/") != 1:
        print(f"invalid --repo {args.repo!r}; expected owner/name", file=sys.stderr)
        return 2
    owner, name = args.repo.split("/", 1)
    try:
        threads = run_graphql(owner, name, args.pr)
        head, checks = run_checks(owner, name, args.pr)
    except ReviewQueryError as error:
        print(f"review-thread check failed closed: {error}", file=sys.stderr)
        return 2

    status = 0
    unresolved = [thread for thread in threads if not thread.get("isResolved")]
    if unresolved:
        print(f"{len(unresolved)} unresolved review thread(s) on {args.repo}#{args.pr}:",
              file=sys.stderr)
        for thread in unresolved:
            print(f"  {describe(thread)}", file=sys.stderr)
        status = 1
    else:
        print(f"review threads: {len(threads)} total, all resolved on {args.repo}#{args.pr}")

    if not checks:
        reason = args.allow_no_checks.strip()
        if reason:
            # A PR that only touches paths no pull-request workflow filters on
            # (web-installer/**, say) legitimately has no checks. The reason is
            # not trusted: the override holds only if no workflow's filter at
            # the base commit selects a changed file, so an Actions outage
            # still fails. The filters come from the API at baseRefOid, because
            # the checkout can be stale against that commit and a PR edits only
            # its own branch copy of a workflow, never its base.
            try:
                base = run_base_ref_oid(owner, name, args.pr)
                workflows = fetch_workflows(owner, name, base)
                changed = run_changed_files(owner, name, args.pr)
                covering = covering_workflows(changed, workflows=workflows)
            except ReviewQueryError as error:
                print(f"review-thread check failed closed: {error}", file=sys.stderr)
                return 2
            if covering:
                print(f"--allow-no-checks refused: {', '.join(covering)} should have run "
                      f"on head {head[:12]} against base {base[:12]}, so a missing check "
                      "is an Actions failure.", file=sys.stderr)
                return 1
            print(f"checks: none on head {head[:12]}, and no workflow at base {base[:12]} "
                  f"covers the changed paths; accepted: {reason}")
            return status
        print(f"no CI check ran on head {head[:12]} of {args.repo}#{args.pr}. "
              "Confirm GitHub Actions is enabled and rerun the workflows, or pass "
              "--allow-no-checks REASON if no workflow covers the changed paths.",
              file=sys.stderr)
        return 1
    problems = [problem for problem in map(check_problem, checks) if problem]
    if problems:
        print(f"{len(problems)} of {len(checks)} check(s) did not pass on head {head[:12]}:",
              file=sys.stderr)
        for problem in problems:
            print(f"  {problem}", file=sys.stderr)
        return 1
    print(f"checks: {len(checks)} total, all passed or skipped on head {head[:12]}")
    return status


if __name__ == "__main__":
    raise SystemExit(main())
