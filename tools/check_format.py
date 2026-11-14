#!/usr/bin/env python3
"""Check clang-format on the C and C++ files a branch changes.

PlatformIO CI checks every file with clang-format 21. This gate checks only
the files changed between the base and head revisions, under the same
directories and suffixes, so pre_review and the pre-push hook catch a
formatting failure before CI does.

Usage:
  python3 tools/check_format.py --base origin/master
  python3 tools/check_format.py --base <sha> --head <sha> --root .

Exit status is 0 when every changed file is formatted, 1 when one is not, and
2 when clang-format 21 is missing or git fails.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
# Keep in step with the "Check clang-format" step in main.yml.
DIRECTORIES = ("src", "include", "lib/furble", "lib/testing", "sim", "tests/host")
SUFFIXES = (".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx")
REQUIRED_MAJOR = 21
INSTALL_HINT = "python3 -m pip install -r .github/workflows/clang-format-requirements.txt"


def checked_path(path: str) -> bool:
  """Return whether CI's clang-format step checks this repo-relative path."""

  return path.endswith(SUFFIXES) and any(
      path.startswith(directory + "/") for directory in DIRECTORIES
  )


def changed_files(root: Path, base: str, head: str) -> list[str]:
  """Return the checked files added, copied, modified or renamed in base...head."""

  output = subprocess.run(
      ["git", "diff", "--name-only", "--diff-filter=ACMR", "-z", f"{base}...{head}"],
      cwd=root, check=True, capture_output=True, text=True,
  ).stdout
  return sorted(path for path in output.split("\0") if path and checked_path(path))


def clang_format_major(binary: str) -> int | None:
  output = subprocess.run(
      [binary, "--version"], check=False, capture_output=True, text=True
  ).stdout
  match = re.search(r"clang-format version (\d+)\.", output)
  return int(match.group(1)) if match else None


def main(argv: list[str] | None = None) -> int:
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("--root", type=Path, default=ROOT)
  parser.add_argument("--base", default="origin/master")
  parser.add_argument("--head", default="HEAD")
  parser.add_argument("--clang-format", default="clang-format",
                      help="clang-format binary (default: clang-format on PATH)")
  args = parser.parse_args(argv)

  root = args.root.resolve()
  try:
    files = changed_files(root, args.base, args.head)
  except subprocess.CalledProcessError as error:
    print(f"check_format: git diff failed: {error.stderr.strip()}", file=sys.stderr)
    return 2
  if not files:
    print("check_format: no changed C or C++ files")
    return 0

  binary = shutil.which(args.clang_format)
  if binary is None:
    print(f"check_format: clang-format not found; install it with: {INSTALL_HINT}",
          file=sys.stderr)
    return 2
  major = clang_format_major(binary)
  if major != REQUIRED_MAJOR:
    print(f"check_format: {binary} is version {major}, CI uses {REQUIRED_MAJOR}; "
          f"install it with: {INSTALL_HINT}", file=sys.stderr)
    return 2

  missing = [path for path in files if not (root / path).is_file()]
  if missing:
    print("check_format: changed files are not in the checkout: " + ", ".join(missing),
          file=sys.stderr)
    return 2
  result = subprocess.run(
      [binary, "--dry-run", "--Werror", "--style=file", *files], cwd=root
  )
  if result.returncode != 0:
    print("check_format: run `clang-format -i --style=file` on the files above",
          file=sys.stderr)
    return 1
  print(f"check_format: {len(files)} changed files are formatted")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
