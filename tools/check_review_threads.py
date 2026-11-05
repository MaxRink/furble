#!/usr/bin/env python3
"""Fail closed while any GitHub review thread on a PR is unresolved.

The policy is: no merge before every review finding is fixed, or proven false
or outdated with evidence. The remediation PR itself must pass this check
before merge. This tool is deliberately online-only and is not wired into the
offline pre-review or fork CI jobs, where no GitHub token is available.

Usage:
  python3 tools/check_review_threads.py --repo owner/name --pr 123
"""
from __future__ import annotations

import argparse
import json
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


class ReviewQueryError(RuntimeError):
    """An API or response-shape failure. Never treated as a clean result."""


def run_graphql(owner: str, name: str, number: int) -> list[dict]:
    threads: list[dict] = []
    cursor: str | None = None
    while True:
        command = [
            "gh", "api", "graphql",
            "-f", f"query={QUERY}",
            "-f", f"owner={owner}",
            "-f", f"name={name}",
            "-F", f"number={number}",
        ]
        if cursor is not None:
            command += ["-f", f"endCursor={cursor}"]
        result = subprocess.run(command, capture_output=True, text=True)
        if result.returncode != 0:
            raise ReviewQueryError(
                f"gh api graphql failed with exit {result.returncode}: "
                f"{result.stderr.strip() or result.stdout.strip()}")
        try:
            payload = json.loads(result.stdout)
        except json.JSONDecodeError as error:
            raise ReviewQueryError(f"gh returned invalid JSON: {error}") from error
        if payload.get("errors"):
            raise ReviewQueryError(f"GraphQL errors: {payload['errors']!r}")
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
    args = parser.parse_args(argv)

    if "/" not in args.repo or args.repo.count("/") != 1:
        print(f"invalid --repo {args.repo!r}; expected owner/name", file=sys.stderr)
        return 2
    owner, name = args.repo.split("/", 1)
    try:
        threads = run_graphql(owner, name, args.pr)
    except ReviewQueryError as error:
        print(f"review-thread check failed closed: {error}", file=sys.stderr)
        return 2

    unresolved = [thread for thread in threads if not thread.get("isResolved")]
    if unresolved:
        print(f"{len(unresolved)} unresolved review thread(s) on {args.repo}#{args.pr}:",
              file=sys.stderr)
        for thread in unresolved:
            print(f"  {describe(thread)}", file=sys.stderr)
        return 1
    print(f"review threads: {len(threads)} total, all resolved on {args.repo}#{args.pr}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
