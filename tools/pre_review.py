#!/usr/bin/env python3
"""Run the quick pre-review gates against a branch.

This is the local entry point used before pushing and by the pre-review CI
workflow. It deliberately wraps the individual scanners instead of
reimplementing their rules, so the same command is used in both places.

Usage:
  python3 tools/pre_review.py
  python3 tools/pre_review.py --base origin/master
  python3 tools/pre_review.py --base <sha> --skip-unit
  python3 tools/pre_review.py --repo . --sanitized

Exit status is 0 when every selected gate passes, 1 when a gate fails, and 2
for an invalid invocation or a missing tool.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
PYTHON = sys.executable or "python3"


def run(label: str, command: list[str], cwd: Path) -> bool:
  print(f"\n== {label} ==", flush=True)
  print("$ " + " ".join(command), flush=True)
  result = subprocess.run(command, cwd=cwd)
  if result.returncode != 0:
    print(f"FAIL: {label} (exit {result.returncode})", file=sys.stderr)
    return False
  print(f"PASS: {label}", flush=True)
  return True


def main(argv: list[str] | None = None) -> int:
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("--repo", type=Path, default=ROOT,
                      help="checkout root (default: this repository)")
  parser.add_argument("--base", default="origin/master",
                      help="git base revision (default: origin/master)")
  parser.add_argument("--head", default="HEAD", help="git head revision")
  parser.add_argument("--skip-unit", action="store_true",
                      help="skip the scanner unit tests (CI runs them separately)")
  parser.add_argument("--sanitized", action="store_true",
                      help="also run the ASan/UBSan host subset")
  args = parser.parse_args(argv)

  root = args.repo.resolve()
  if not (root / "tools" / "check_pr_hygiene.py").is_file():
    print(f"FAIL: {root} does not look like the furble repository", file=sys.stderr)
    return 2

  revision_range = f"{args.base}...{args.head}"
  gates: list[tuple[str, list[str]]] = [
      ("whitespace", ["git", "diff", "--check", revision_range]),
      ("diff hygiene",
       [PYTHON, "tools/check_pr_hygiene.py", "--repo", str(root), revision_range]),
      ("settings contracts",
       [PYTHON, "tools/check_settings_contracts.py", "--root", str(root),
        "--base", args.base, "--head", args.head]),
      ("clang-format",
       [PYTHON, "tools/check_format.py", "--root", str(root),
        "--base", args.base, "--head", args.head]),
      ("C++ safety",
       [PYTHON, "tools/check_cpp_safety.py", "--root", str(root),
        "--base", args.base, "--head", args.head]),
      ("CI workflow contract", [PYTHON, "tools/check_ci_workflows.py"]),
      ("simulator scenario manifest", [PYTHON, "tools/check_sim_scenarios.py"]),
      ("simulator hardware coverage", [PYTHON, "tools/check_sim_coverage.py"]),
  ]
  if not args.skip_unit:
    gates.append(("scanner unit tests",
                  [PYTHON, "-m", "unittest",
                   "tests.test_check_pr_hygiene",
                   "tests.test_check_settings_contracts",
                   "tests.test_check_cpp_safety",
                   "tests.test_check_format",
                   "tests.test_plot_companion_power"]))
  if args.sanitized:
    gates.append(("sanitized host subset",
                  ["sh", "tools/run_sanitized_host_tests.sh", "--clean"]))

  failed: list[str] = []
  for label, command in gates:
    if not run(label, command, root):
      failed.append(label)

  print("\n== pre-review summary ==")
  if failed:
    print("FAIL: " + ", ".join(failed), file=sys.stderr)
    return 1
  print(f"PASS: {len(gates)} gates")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
