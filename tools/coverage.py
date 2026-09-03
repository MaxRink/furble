#!/usr/bin/env python3
"""Measure firmware line coverage across the host and simulator test stacks.

Both stacks compile the same firmware sources, so neither number alone says how
much of the firmware the tests reach. This tool builds each stack with clang
source-based coverage, runs the same suites CI runs, exports lcov, and unions
the per-line hit data so one grand-union percentage covers the firmware tree.

Firmware sources are the files under src/, lib/furble/, lib/preferences/,
lib/blowfish/ and include/. Everything else (LVGL, M5GFX, M5Unified, the test
harnesses and the simulator shims) is excluded from the numbers.

The measurement is compared against tests/coverage_floor.json, a committed set
of per-stack, union and per-file minimum percentages. Dropping below any floor
fails the run. A change that raises coverage can raise the floor in the same
commit with --ratchet, which only ever moves a floor up. Lowering one is
deliberate and needs --lower with a --reason that is recorded in the document.

Usage:
  python3 tools/coverage.py --check
  python3 tools/coverage.py --ratchet
  python3 tools/coverage.py --ratchet --lower --reason "..."
  python3 tools/coverage.py --stack host --skip-build

Requires clang, cmake and the matching llvm-profdata and llvm-cov. Override
tool discovery with LLVM_COV and LLVM_PROFDATA.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

FIRMWARE_DIRS = (
    "src",
    "lib/furble",
    "lib/preferences",
    "lib/blowfish",
    "include",
)

# The three modeled panel classes, in the same order the simulator workflow
# builds them. Each tuple is (board id, FURBLE_SIM_FURBLE_BOARD,
# FURBLE_SIM_M5GFX_BOARD, panel geometry).
SIM_BOARDS = (
    ("m5stick-s3", "FURBLE_M5STICKS3", "board_M5StickS3", "135x240"),
    ("m5stick-c", "FURBLE_M5STICKC", "board_M5StickC", "80x160"),
    ("m5stack-core", "FURBLE_M5COREX", "board_M5Stack", "320x240"),
)

# The environment the simulator workflows use. Reported capabilities gate the
# Infrared, Feedback and Storage submenus, so without them those pages are
# unreachable and their code reads as dead.
SIM_ENV = {
    "SDL_VIDEODRIVER": "dummy",
    "SDL_AUDIODRIVER": "dummy",
    "FURBLE_SIM_IR": "1",
    "FURBLE_SIM_FEEDBACK": "1",
    "FURBLE_SIM_SD": "1",
}

# Every suite tools/check_sim_scenarios.py owns. Which boards each one runs on
# comes from the manifest, so this list never needs a per-board exception.
SIM_SUITES = ("bughunt", "e2e", "invalid", "power-gate")

DEFAULT_FLOOR = "tests/coverage_floor.json"

# Files whose coverage is tracked individually. These carry the connection
# state machine, the BLE scan and camera abstractions, the UI and the console
# automation surface, so a drop in any of them matters more than the total.
CRITICAL_FILES = (
    "src/FurbleControl.cpp",
    "lib/furble/Camera.cpp",
    "lib/furble/Scan.cpp",
    "src/FurbleUI.cpp",
    "src/FurbleConsole.cpp",
)

LOW_COVERAGE_PERCENT = 30.0

# Points subtracted from every measurement when --ratchet writes the floor.
# A few tests are timing dependent, so two runs of the same tree can differ by
# a fraction of a point. A floor pinned to the exact measurement would then
# fail its own next run. One point absorbs the observed spread and still
# catches any real regression.
RATCHET_MARGIN = 1.0


class CoverageError(RuntimeError):
  """A coverage run could not be completed."""


# ---------------------------------------------------------------------------
# lcov parsing, union and floor comparison. These are pure functions with no
# subprocess or filesystem dependency so tests/test_coverage_tooling.py can
# exercise them without llvm installed.
# ---------------------------------------------------------------------------


def parse_lcov(text: str) -> dict[str, dict[int, int]]:
  """Return {source path: {line number: hit count}} from lcov tracefile text.

  Only SF and DA records are read. Repeated DA records for one line are summed,
  which is what lcov itself does when a file appears in several test runs.
  """

  files: dict[str, dict[int, int]] = {}
  current: dict[int, int] | None = None
  for raw in text.splitlines():
    line = raw.strip()
    if line.startswith("SF:"):
      name = line[3:]
      current = files.setdefault(name, {})
    elif line.startswith("DA:") and current is not None:
      payload = line[3:]
      number, _, count = payload.partition(",")
      count = count.split(",", 1)[0]
      try:
        number_value = int(number)
        count_value = int(count)
      except ValueError:
        continue
      current[number_value] = current.get(number_value, 0) + count_value
    elif line == "end_of_record":
      current = None
  return files


def union_coverage(
    reports: list[dict[str, dict[int, int]]]
) -> dict[str, dict[int, int]]:
  """Union per-line hit counts across reports.

  A line is covered in the union when any report covered it. Hit counts are
  summed so a merged report still shows relative hotness, but only the
  covered/uncovered distinction feeds the percentages.
  """

  merged: dict[str, dict[int, int]] = {}
  for report in reports:
    for name, lines in report.items():
      target = merged.setdefault(name, {})
      for number, count in lines.items():
        target[number] = target.get(number, 0) + count
  return merged


def normalize_paths(
    report: dict[str, dict[int, int]], root: Path
) -> dict[str, dict[int, int]]:
  """Rewrite absolute source paths to repository-relative posix paths.

  Paths outside the repository are dropped. llvm-cov emits resolved absolute
  paths, and a build directory outside the tree can produce '..' segments, so
  every path is resolved before the relative test.
  """

  resolved_root = root.resolve()
  out: dict[str, dict[int, int]] = {}
  for name, lines in report.items():
    try:
      relative = Path(name).resolve().relative_to(resolved_root)
    except (ValueError, OSError):
      continue
    key = relative.as_posix()
    target = out.setdefault(key, {})
    for number, count in lines.items():
      target[number] = target.get(number, 0) + count
  return out


def is_firmware_path(relative: str) -> bool:
  """Return whether a repository-relative path is a firmware source."""

  return any(
      relative == directory or relative.startswith(directory + "/")
      for directory in FIRMWARE_DIRS
  )


def filter_firmware(
    report: dict[str, dict[int, int]]
) -> dict[str, dict[int, int]]:
  return {
      name: lines for name, lines in report.items() if is_firmware_path(name)
  }


def file_totals(lines: dict[int, int]) -> tuple[int, int]:
  """Return (covered lines, total instrumented lines) for one file."""

  total = len(lines)
  covered = sum(1 for count in lines.values() if count > 0)
  return covered, total


def percent(covered: int, total: int) -> float:
  if total == 0:
    return 0.0
  return covered / total * 100.0


def summarize(report: dict[str, dict[int, int]]) -> dict:
  """Return a total plus per-file summary for one report."""

  files = {}
  covered_total = 0
  line_total = 0
  for name in sorted(report):
    covered, total = file_totals(report[name])
    covered_total += covered
    line_total += total
    files[name] = {
        "covered": covered,
        "total": total,
        "percent": round(percent(covered, total), 2),
    }
  return {
      "covered": covered_total,
      "total": line_total,
      "percent": round(percent(covered_total, line_total), 2),
      "files": files,
  }


def floor_failures(summary: dict, floor: dict) -> list[str]:
  """Return one message per floor the summary does not meet.

  A floor entry naming a stack, or a file, that the measurement does not
  contain is itself a failure. Silently passing a vanished target would let a
  build change erase a tracked file without anyone noticing.
  """

  failures: list[str] = []
  stacks = summary.get("stacks", {})
  for name, minimum in sorted(floor.get("stacks", {}).items()):
    measured = stacks.get(name)
    if measured is None:
      failures.append(f"stack {name}: no measurement, floor {minimum:.2f}%")
      continue
    if measured["percent"] < minimum:
      failures.append(
          f"stack {name}: {measured['percent']:.2f}% below floor "
          f"{minimum:.2f}%"
      )

  union_floor = floor.get("union")
  if union_floor is not None:
    measured_union = summary.get("union", {}).get("percent")
    if measured_union is None:
      failures.append(f"union: no measurement, floor {union_floor:.2f}%")
    elif measured_union < union_floor:
      failures.append(
          f"union: {measured_union:.2f}% below floor {union_floor:.2f}%"
      )

  union_files = summary.get("union", {}).get("files", {})
  for name, minimum in sorted(floor.get("files", {}).items()):
    measured_file = union_files.get(name)
    if measured_file is None:
      failures.append(f"{name}: not measured, floor {minimum:.2f}%")
      continue
    if measured_file["percent"] < minimum:
      failures.append(
          f"{name}: {measured_file['percent']:.2f}% below floor "
          f"{minimum:.2f}%"
      )
  return failures


def measured_floor(summary: dict, margin: float = 0.0) -> dict:
  """Build a floor document from a measurement alone, lowered by margin."""

  def value(number: float) -> float:
    return round(max(0.0, number - margin), 2)

  stacks = {
      name: value(data["percent"])
      for name, data in sorted(summary.get("stacks", {}).items())
  }
  union_files = summary.get("union", {}).get("files", {})
  files = {
      name: value(union_files[name]["percent"])
      for name in CRITICAL_FILES
      if name in union_files
  }
  return {
      "stacks": stacks,
      "union": value(summary.get("union", {}).get("percent", 0.0)),
      "files": files,
  }


def ratchet_floor(
    existing: dict | None,
    summary: dict,
    margin: float = 0.0,
    lower: bool = False,
    reason: str | None = None,
) -> tuple[dict, list[str]]:
  """Raise the floor to the measurement. Return (floor, lowered keys).

  A ratchet that only moves one way is the whole point. Without ``lower``,
  every value is ``max(existing, measured - margin)``, so running --ratchet on
  a branch that dropped coverage cannot quietly write the drop into the floor
  and turn a red build green. A key present in the floor but missing from the
  measurement is kept, so floor_failures still reports the vanished target.

  ``lower`` is the deliberate escape hatch, and it demands a reason that is
  written into the floor document so the diff carries its own justification.
  """

  if lower and not (reason or "").strip():
    raise CoverageError("lowering a floor requires a reason")

  existing = existing or {}
  candidate = measured_floor(summary, margin)
  lowered: list[str] = []

  def combine(label: str, old, new: float) -> float:
    if old is None:
      return new
    if new >= old:
      return new
    if lower:
      lowered.append(f"{label}: {old:.2f} to {new:.2f}")
      return new
    return old

  stacks = dict(existing.get("stacks", {}))
  for name, value in candidate["stacks"].items():
    stacks[name] = combine(f"stack {name}", stacks.get(name), value)

  files = dict(existing.get("files", {}))
  for name, value in candidate["files"].items():
    files[name] = combine(name, files.get(name), value)

  union = combine("union", existing.get("union"), candidate["union"])

  updated = {
      "stacks": dict(sorted(stacks.items())),
      "union": union,
      "files": dict(sorted(files.items())),
  }
  if lowered:
    updated["lowered"] = {
        "reason": reason.strip(),
        "keys": sorted(lowered),
    }
  return updated, lowered


def low_coverage_files(summary: dict, limit: float = LOW_COVERAGE_PERCENT):
  """Return (path, percent, covered, total) for union files under limit."""

  rows = []
  for name, data in summary.get("union", {}).get("files", {}).items():
    if data["percent"] < limit:
      rows.append((name, data["percent"], data["covered"], data["total"]))
  rows.sort(key=lambda row: (row[1], row[0]))
  return rows


def render_markdown(summary: dict, floor: dict | None) -> str:
  """Render the measurement as a markdown report."""

  lines = [
      "<!-- furble-coverage-report -->",
      "## Firmware coverage report",
      "",
      "Line coverage of `src/`, `lib/furble/`, `lib/preferences/`, "
      "`lib/blowfish/` and `include/`.",
      "",
      "| Stack | Covered | Instrumented | Coverage | Floor |",
      "| --- | ---: | ---: | ---: | ---: |",
  ]
  stack_floors = (floor or {}).get("stacks", {})
  for name, data in summary.get("stacks", {}).items():
    minimum = stack_floors.get(name)
    floor_text = f"{minimum:.2f}%" if minimum is not None else "n/a"
    lines.append(
        f"| {name} | {data['covered']:,} | {data['total']:,} | "
        f"{data['percent']:.2f}% | {floor_text} |"
    )
  union = summary.get("union", {})
  union_floor = (floor or {}).get("union")
  union_floor_text = f"{union_floor:.2f}%" if union_floor is not None else "n/a"
  lines.append(
      f"| **grand union** | {union.get('covered', 0):,} | "
      f"{union.get('total', 0):,} | {union.get('percent', 0.0):.2f}% | "
      f"{union_floor_text} |"
  )

  file_floors = (floor or {}).get("files", {})
  if file_floors:
    lines.extend([
        "",
        "### Tracked files (grand union)",
        "",
        "| File | Covered | Instrumented | Coverage | Floor |",
        "| --- | ---: | ---: | ---: | ---: |",
    ])
    union_files = union.get("files", {})
    for name in sorted(file_floors):
      data = union_files.get(name)
      minimum = file_floors[name]
      if data is None:
        lines.append(f"| `{name}` | n/a | n/a | not measured | {minimum:.2f}% |")
        continue
      lines.append(
          f"| `{name}` | {data['covered']:,} | {data['total']:,} | "
          f"{data['percent']:.2f}% | {minimum:.2f}% |"
      )

  low = low_coverage_files(summary)
  lines.extend(["", f"### Files under {LOW_COVERAGE_PERCENT:.0f}% in the union", ""])
  if low:
    lines.append("| File | Covered | Instrumented | Coverage |")
    lines.append("| --- | ---: | ---: | ---: |")
    for name, value, covered, total in low:
      lines.append(f"| `{name}` | {covered:,} | {total:,} | {value:.2f}% |")
  else:
    lines.append(f"Every instrumented firmware file is at or above "
                 f"{LOW_COVERAGE_PERCENT:.0f}%.")

  absent = summary.get("not_instrumented", [])
  if absent:
    lines.extend([
        "",
        "### Firmware sources in neither instrumented build",
        "",
        "These files are not compiled by the host harness or the simulator, so "
        "they contribute no lines to the numbers above.",
        "",
    ])
    lines.extend(f"- `{name}`" for name in absent)
  return "\n".join(lines) + "\n"


# ---------------------------------------------------------------------------
# llvm tool discovery and stack execution.
# ---------------------------------------------------------------------------


def tool_major_version(tool: str) -> int | None:
  """Return the LLVM major version a tool reports, or None.

  llvm-profdata refuses --version without a subcommand, so a versioned file
  name is the fallback. That is exactly the case Debian and Ubuntu produce.
  """

  try:
    result = subprocess.run(
        [tool, "--version"], check=False, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
    )
    match = re.search(r"(?:LLVM|clang) version (\d+)", result.stdout or "")
    if match:
      return int(match.group(1))
  except OSError:
    return None
  suffix = re.search(r"-(\d+)$", Path(tool).name)
  return int(suffix.group(1)) if suffix else None


def find_llvm_tool(name: str, env_key: str, want_major: int | None = None) -> str:
  """Locate an llvm tool, preferring an explicit environment override.

  The raw profile format is tied to the LLVM version that instrumented the
  binaries, so a mismatched llvm-profdata fails with a confusing message about
  an unsupported profile version. When the clang major version is known, a
  tool reporting that version is preferred over a newer one earlier in PATH.
  """

  override = os.environ.get(env_key)
  if override:
    if shutil.which(override) or os.access(override, os.X_OK):
      return override
    raise CoverageError(
        f"{env_key} is set to {override!r} but that is not an executable."
    )

  # Every candidate on PATH: the plain name and the versioned Debian names.
  candidates: list[str] = []
  plain = shutil.which(name)
  if plain:
    candidates.append(plain)
  for entry in os.environ.get("PATH", "").split(os.pathsep):
    if not entry:
      continue
    try:
      names = sorted(os.listdir(entry))
    except OSError:
      continue
    for candidate in names:
      if re.fullmatch(re.escape(name) + r"-\d+", candidate):
        path = str(Path(entry) / candidate)
        if os.access(path, os.X_OK) and path not in candidates:
          candidates.append(path)

  if not candidates:
    raise CoverageError(
        f"{name} was not found. Install the llvm tools that match your clang, "
        f"or set {env_key} to its path. On Debian and Ubuntu the package is "
        f"llvm, or llvm-<version> for a specific one."
    )

  versions = [(tool_major_version(tool), tool) for tool in candidates]
  if want_major is not None:
    for major, tool in versions:
      if major == want_major:
        return tool
    print(
        f"warning: no {name} reporting LLVM {want_major} was found. Using "
        f"{candidates[0]}, which may reject the raw profiles. Set {env_key} to "
        "the matching tool if the merge fails.",
        file=sys.stderr,
    )
  known = [(major, tool) for major, tool in versions if major is not None]
  if known:
    return max(known)[1]
  return candidates[0]


def run(command, *, cwd=None, env=None, check=True, capture=False):
  """Run a subprocess and raise CoverageError with context on failure."""

  merged = dict(os.environ)
  if env:
    merged.update(env)
  printable = " ".join(str(part) for part in command)
  print(f"+ {printable}", flush=True)
  result = subprocess.run(
      [str(part) for part in command],
      cwd=str(cwd) if cwd else None,
      env=merged,
      check=False,
      text=True,
      stdout=subprocess.PIPE if capture else None,
      stderr=subprocess.PIPE if capture else None,
  )
  if check and result.returncode != 0:
    detail = ""
    if capture:
      detail = f"\n{result.stdout}\n{result.stderr}"
    raise CoverageError(
        f"command failed with exit {result.returncode}: {printable}{detail}"
    )
  return result


def export_lcov(llvm_cov: str, profdata: Path, binaries, root: Path) -> dict:
  """Export one lcov report per binary and union them.

  Each binary is exported on its own. Sharing one llvm-cov invocation across
  binaries makes it reject function records that differ between targets, and
  the host harness deliberately compiles the same sources with different
  definitions per test.
  """

  sources = [str(root / directory) for directory in FIRMWARE_DIRS]

  def export_one(binary: Path) -> dict[str, dict[int, int]]:
    result = subprocess.run(
        [
            llvm_cov,
            "export",
            "--format=lcov",
            f"--instr-profile={profdata}",
            str(binary),
            *sources,
        ],
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if result.returncode != 0:
      # Never return an empty report here. A swallowed export turns a whole
      # stack into zero measured lines, and the floor check would then report
      # a vanished target instead of the real cause.
      raise CoverageError(
          f"llvm-cov export failed for {binary}: "
          f"{result.stderr.strip()[:400]}"
      )
    return parse_lcov(result.stdout)

  workers = min(8, max(1, len(binaries)))
  with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
    reports = list(pool.map(export_one, binaries))
  return filter_firmware(normalize_paths(union_coverage(reports), root))


def merge_profiles(llvm_profdata: str, profile_dir: Path, output: Path) -> None:
  raws = sorted(profile_dir.glob("*.profraw"))
  if not raws:
    raise CoverageError(
        f"no raw profiles were written to {profile_dir}. The instrumented "
        "binaries either did not run or were built without coverage."
    )
  run([llvm_profdata, "merge", "-sparse", "-o", output, *raws], capture=True)


def ctest_binaries(build_dir: Path) -> list[Path]:
  """Return the distinct test executables ctest would run."""

  result = run(
      ["ctest", "--test-dir", str(build_dir), "--show-only=json-v1"],
      capture=True,
  )
  document = json.loads(result.stdout)
  binaries: list[Path] = []
  seen: set[str] = set()
  for test in document.get("tests", []):
    command = test.get("command") or []
    if not command:
      continue
    candidate = Path(command[0])
    if not candidate.is_absolute():
      candidate = (build_dir / candidate).resolve()
    key = str(candidate)
    if key in seen or not candidate.is_file():
      continue
    seen.add(key)
    binaries.append(candidate)
  return binaries


def measure_host(args, root: Path, llvm_cov: str, llvm_profdata: str) -> dict:
  build_dir = args.build_dir / "host"
  profile_dir = args.build_dir / "profiles" / "host"
  if not args.skip_build:
    shutil.rmtree(profile_dir, ignore_errors=True)
    run([
        "cmake",
        "-S",
        str(root / "tests/host"),
        "-B",
        str(build_dir),
        "-DFURBLE_COVERAGE=ON",
        f"-DCMAKE_C_COMPILER={args.cc}",
        f"-DCMAKE_CXX_COMPILER={args.cxx}",
    ])
    run([
        "cmake", "--build", str(build_dir), "--parallel", str(args.jobs)
    ])
  profile_dir.mkdir(parents=True, exist_ok=True)
  run(
      [
          "ctest",
          "--test-dir",
          str(build_dir),
          "--output-on-failure",
          "--parallel",
          str(args.jobs),
      ],
      env={"LLVM_PROFILE_FILE": str(profile_dir / "host-%p-%m.profraw")},
  )
  profdata = args.build_dir / "host.profdata"
  merge_profiles(llvm_profdata, profile_dir, profdata)
  return export_lcov(llvm_cov, profdata, ctest_binaries(build_dir), root)


def build_simulator(args, root: Path, board: tuple[str, str, str, str]) -> Path:
  board_id, furble_board, m5gfx_board, _ = board
  build_dir = args.build_dir / f"sim-{board_id}"
  if not args.skip_build:
    run(
        ["sh", str(root / "sim/build.sh")],
        cwd=root,
        env={
            "FURBLE_SIM_BUILD_DIR": str(build_dir),
            "FURBLE_SIM_COVERAGE": "1",
            "FURBLE_SIM_FURBLE_BOARD": furble_board,
            "FURBLE_SIM_M5GFX_BOARD": m5gfx_board,
            "FURBLE_DEP_ROOT": args.dep_root,
            "FURBLE_LVGL_DIR": args.lvgl_dir,
            "CC": args.cc,
            "CXX": args.cxx,
        },
    )
  binary = build_dir / "furble-sim"
  if not binary.is_file():
    raise CoverageError(f"simulator binary is missing at {binary}")
  return binary


def incomplete_scenarios(results, timeout_s: float) -> list[str]:
  """Return the scenarios whose profile cannot be trusted to be complete.

  A scenario that is killed on the timeout, or that dies on a signal, writes no
  profile at all or a truncated one. Its lines are then simply missing from the
  merge, which moves the measured percentage on an unchanged tree: the same
  commit measured 503 of 697 lines in lib/furble/Camera.cpp on a loaded runner
  and 521 of 697 on an idle one, purely because one scenario was killed. That
  is a broken measurement, not a low one, and reporting it as a number would
  either hide a real regression behind an accidental gain or fail the floor for
  a reason no diff explains. Both outcomes fail the run instead.

  `results` is an iterable of (label, code) pairs, where code is None for a
  timeout kill and a negative number for a signal death, matching what
  subprocess reports.

  A scenario that exits with an ordinary non-zero status is deliberately not
  listed. It ran to completion and wrote a complete profile, so its measurement
  is sound; whether it should have passed is the sim-e2e workflow's gate.
  """

  failures: list[str] = []
  for label, code in results:
    if code is None:
      failures.append(
          f"{label}: timed out after {timeout_s:g} s, so it wrote no profile"
      )
    elif code < 0:
      failures.append(
          f"{label}: killed by signal {-code}, so its profile is incomplete"
      )
  return failures


def certified_scenarios(root: Path, suite: str, board: str) -> list[str]:
  result = run(
      [
          sys.executable,
          str(root / "tools/check_sim_scenarios.py"),
          "--list-certified",
          "--suite",
          suite,
          "--board",
          board,
      ],
      capture=True,
  )
  # An empty selection is a fact about the manifest, not an error. The
  # power-gate and invalid suites are modeled on the 135x240 panel only, so
  # the other two boards legitimately select nothing.
  return [line.strip() for line in result.stdout.splitlines() if line.strip()]


def measure_sim_board(
    args, root: Path, board, llvm_cov: str, llvm_profdata: str
) -> dict:
  board_id = board[0]
  binary = build_simulator(args, root, board)
  profile_dir = args.build_dir / "profiles" / f"sim-{board_id}"
  shutil.rmtree(profile_dir, ignore_errors=True)
  profile_dir.mkdir(parents=True, exist_ok=True)
  state_dir = args.build_dir / "state" / f"sim-{board_id}"
  shutil.rmtree(state_dir, ignore_errors=True)
  state_dir.mkdir(parents=True, exist_ok=True)
  report_dir = args.build_dir / "power-reports" / board_id
  report_dir.mkdir(parents=True, exist_ok=True)

  # Every suite the manifest owns, asked per board. Which boards a suite runs
  # on is the manifest's answer, not a constant here: power-gate and invalid
  # are modeled on the 135x240 panel only, so the other two boards select
  # nothing and the loop simply produces no jobs for them.
  jobs: list[tuple[str, list[str]]] = []
  for suite in SIM_SUITES:
    for scenario in certified_scenarios(root, suite, board_id):
      command = [str(binary), "--script", str(root / scenario)]
      if suite == "power-gate":
        command.extend(["--report-dir", str(report_dir)])
      jobs.append((scenario, command))
  if not jobs:
    raise CoverageError(f"the manifest selected no scenarios for {board_id}")

  def run_scenario(indexed) -> tuple[str, int]:
    index, (label, command) = indexed
    # Each run gets its own profile and its own simulated NVS file, so the
    # scenarios stay independent of each other and of run order.
    env = dict(os.environ)
    env.update(SIM_ENV)
    env["LLVM_PROFILE_FILE"] = str(
        profile_dir / f"{board_id}-{index:04d}-%p.profraw"
    )
    env["FURBLE_SIM_PREFS"] = str(state_dir / f"prefs-{index:04d}.bin")
    try:
      result = subprocess.run(
          command, cwd=str(root), env=env, check=False, text=True,
          stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
          timeout=args.scenario_timeout,
      )
    except subprocess.TimeoutExpired:
      # A wedged scenario must not hold the whole job until the runner's own
      # timeout kills it with no report at all. The killed process writes no
      # profile, so incomplete_scenarios() below fails the run by name.
      return label, None
    return label, result.returncode

  print(
      f"Running {len(jobs)} {board_id} scenarios with "
      f"{args.scenario_jobs} workers.",
      flush=True,
  )
  # Coverage never gates on a scenario's pass or fail outcome. That is the
  # sim-e2e and power-gate workflows' business. A scenario that fails here still
  # contributes the lines it reached, and dropping it would understate
  # coverage for no benefit. Failures are still printed, because a scenario
  # that stops passing will show up as a coverage drop and the reason should
  # be in the same log.
  #
  # A scenario that never finished is a different thing entirely: it has no
  # outcome and no complete profile, so the measurement it belongs to is not a
  # measurement. Those fail the run.
  with concurrent.futures.ThreadPoolExecutor(
      max_workers=args.scenario_jobs
  ) as pool:
    results = list(pool.map(run_scenario, enumerate(jobs)))
  for label, code in results:
    if code is None:
      continue
    expected = 2 if "/invalid/" in label else 0
    if code != expected:
      print(
          f"note: {board_id} scenario {label} exited {code}, expected "
          f"{expected}",
          file=sys.stderr,
      )
  incomplete = incomplete_scenarios(results, args.scenario_timeout)
  if incomplete:
    raise CoverageError(
        f"{board_id}: {len(incomplete)} scenario(s) produced no usable "
        "profile, so the measurement would be wrong rather than low:\n- "
        + "\n- ".join(incomplete)
    )

  if board_id == "m5stick-s3":
    # run-invalid.sh also drives command-line and environment rejection paths
    # that no scenario file reaches, and the restart seam re-exec.
    env = dict(os.environ)
    env.update(SIM_ENV)
    env["LLVM_PROFILE_FILE"] = str(profile_dir / f"{board_id}-cli-%p.profraw")
    env["FURBLE_SIM_PREFS"] = str(state_dir / "prefs-cli.bin")
    env["FURBLE_SIM_BIN"] = str(binary)
    run(
        ["sh", str(root / "sim/scripts/run-invalid.sh")],
        cwd=root, env=env, check=False, capture=True,
    )

  profdata = args.build_dir / f"sim-{board_id}.profdata"
  merge_profiles(llvm_profdata, profile_dir, profdata)
  return export_lcov(llvm_cov, profdata, [binary], root)


def firmware_sources(root: Path) -> list[str]:
  """Return every firmware translation unit in the tree.

  git is the fast path. An export, a tarball or a broken git is not a reason
  to silently drop the "in neither build" section from the report, so the
  fallback walks the firmware directories directly.
  """

  result = run(
      ["git", "-C", str(root), "ls-files", "-z", "--", *FIRMWARE_DIRS],
      capture=True, check=False,
  )
  if result.returncode == 0:
    candidates = [name for name in result.stdout.split("\0") if name]
  else:
    print(
        f"warning: git ls-files failed in {root}, walking the firmware "
        "directories instead.",
        file=sys.stderr,
    )
    candidates = []
    for directory in FIRMWARE_DIRS:
      base = root / directory
      if not base.is_dir():
        continue
      for path in base.rglob("*"):
        if path.is_file():
          candidates.append(path.relative_to(root).as_posix())
  return sorted(
      name for name in candidates
      if name.endswith((".c", ".cpp")) and is_firmware_path(name)
  )


def build_summary(root: Path, reports: dict[str, dict]) -> dict:
  """Assemble the machine-readable summary from per-stack reports."""

  stacks = {name: summarize(report) for name, report in reports.items()}
  sim_reports = [
      report for name, report in reports.items() if name.startswith("sim ")
  ]
  if sim_reports:
    stacks["sim union"] = summarize(union_coverage(sim_reports))
  grand = summarize(union_coverage(list(reports.values())))
  measured = set(grand["files"])
  absent = [name for name in firmware_sources(root) if name not in measured]
  return {
      "stacks": stacks,
      "union": grand,
      "not_instrumented": absent,
  }


def write_html(llvm_cov: str, profdata: Path, binaries, root: Path, out: Path):
  """Best-effort HTML report. Never fails the run."""

  if not binaries:
    print(
        f"warning: HTML report for {out.name} was skipped, no binaries were "
        "found.",
        file=sys.stderr,
    )
    return
  out.mkdir(parents=True, exist_ok=True)
  command = [
      llvm_cov,
      "show",
      "--format=html",
      f"--instr-profile={profdata}",
      f"--output-dir={out}",
      str(binaries[0]),
  ]
  for extra in binaries[1:]:
    command.extend(["-object", str(extra)])
  command.extend(str(root / directory) for directory in FIRMWARE_DIRS)
  result = subprocess.run(
      command, check=False, text=True, stdout=subprocess.PIPE,
      stderr=subprocess.PIPE,
  )
  if result.returncode != 0:
    print(
        f"warning: HTML report for {out.name} was not generated: "
        f"{result.stderr.strip()[:400]}",
        file=sys.stderr,
    )


def parse_args(argv=None):
  parser = argparse.ArgumentParser(
      description=__doc__,
      formatter_class=argparse.RawDescriptionHelpFormatter,
  )
  parser.add_argument(
      "--root", type=Path, default=Path(__file__).resolve().parents[1],
      help="repository root (default: inferred from this script)")
  parser.add_argument(
      "--build-dir", type=Path,
      default=Path(tempfile.gettempdir()) / "furble-cov",
      help="scratch directory for builds and profiles. Keep it short: one "
           "host test truncates paths into a 64 byte buffer.")
  parser.add_argument(
      "--stack", action="append", choices=["host", "sim"],
      help="measure only these stacks (default: both)")
  parser.add_argument(
      "--board", action="append",
      choices=[board[0] for board in SIM_BOARDS],
      help="measure only these simulator panels (default: all three)")
  parser.add_argument("--skip-build", action="store_true",
                      help="reuse the binaries already in --build-dir")
  parser.add_argument("--jobs", type=int, default=os.cpu_count() or 2,
                      help="parallel compile and ctest jobs")
  parser.add_argument(
      "--scenario-timeout", type=float, default=600.0,
      help="seconds before a single simulator scenario is killed")
  parser.add_argument(
      "--scenario-jobs", type=int, default=4,
      help="parallel simulator scenarios. Most scenario time is spent waiting "
           "on the simulated clock, not on the CPU, so this is the single "
           "biggest lever on wall clock time.")
  parser.add_argument("--cc", default=os.environ.get("CC", "clang"))
  parser.add_argument("--cxx", default=os.environ.get("CXX", "clang++"))
  parser.add_argument(
      "--dep-root", default=os.environ.get("FURBLE_DEP_ROOT", ""),
      help="simulator dependency root (M5GFX, M5Unified, TinyGPSPlus, lvgl)")
  parser.add_argument(
      "--lvgl-dir", default=os.environ.get("FURBLE_LVGL_DIR", ""),
      help="LVGL 9 source directory")
  parser.add_argument("--json", type=Path, help="write the JSON summary here")
  parser.add_argument("--markdown", type=Path,
                      help="write the markdown report here")
  parser.add_argument("--html", type=Path,
                      help="write per-stack HTML reports under this directory")
  parser.add_argument("--floor", type=Path, default=None,
                      help=f"floor document (default: {DEFAULT_FLOOR})")
  parser.add_argument("--check", action="store_true",
                      help="fail when a measurement is below its floor")
  parser.add_argument("--ratchet", action="store_true",
                      help="raise the floor to the current measurement")
  parser.add_argument(
      "--lower", action="store_true",
      help="allow --ratchet to lower a floor. Needs --reason, which is "
           "written into the floor document.")
  parser.add_argument(
      "--reason", default="",
      help="why a floor is being lowered. Required with --lower.")
  parser.add_argument(
      "--ratchet-margin", type=float, default=RATCHET_MARGIN,
      help="points to subtract from each measurement when writing the floor. "
           "The default absorbs the small run to run spread that timing "
           "dependent tests produce. Use 0 to pin the exact measurement.")
  parser.add_argument(
      "--summary-from", type=Path,
      help="skip measuring and reload a JSON summary written by an earlier run")
  return parser.parse_args(argv)


def main(argv=None) -> int:
  args = parse_args(argv)
  if args.lower and not args.ratchet:
    raise CoverageError("--lower only applies to --ratchet")
  if args.lower and not args.reason.strip():
    # Fail before measuring rather than after. A full run is minutes long and
    # a missing reason is knowable from the command line alone.
    raise CoverageError("--lower requires --reason")
  root = args.root.resolve()
  args.build_dir = args.build_dir.resolve()
  floor_path = args.floor or (root / DEFAULT_FLOOR)

  if args.summary_from:
    summary = json.loads(args.summary_from.read_text(encoding="utf-8"))
  else:
    clang_major = tool_major_version(args.cxx)
    llvm_cov = find_llvm_tool("llvm-cov", "LLVM_COV", clang_major)
    llvm_profdata = find_llvm_tool("llvm-profdata", "LLVM_PROFDATA", clang_major)
    args.build_dir.mkdir(parents=True, exist_ok=True)

    stacks = args.stack or ["host", "sim"]
    boards = [
        board for board in SIM_BOARDS
        if not args.board or board[0] in args.board
    ]
    reports: dict[str, dict] = {}
    if "host" in stacks:
      reports["host"] = measure_host(args, root, llvm_cov, llvm_profdata)
      if args.html:
        write_html(
            llvm_cov,
            args.build_dir / "host.profdata",
            ctest_binaries(args.build_dir / "host"),
            root,
            args.html / "host",
        )
    if "sim" in stacks:
      for board in boards:
        name = f"sim {board[0]} ({board[3]})"
        reports[name] = measure_sim_board(
            args, root, board, llvm_cov, llvm_profdata
        )
        if args.html:
          write_html(
              llvm_cov,
              args.build_dir / f"sim-{board[0]}.profdata",
              [args.build_dir / f"sim-{board[0]}" / "furble-sim"],
              root,
              args.html / f"sim-{board[0]}",
          )
    if not reports:
      raise CoverageError("no stack was measured")
    summary = build_summary(root, reports)

  floor = None
  if floor_path.is_file():
    floor = json.loads(floor_path.read_text(encoding="utf-8"))

  report = render_markdown(summary, floor)
  print()
  print(report)

  if args.json:
    args.json.parent.mkdir(parents=True, exist_ok=True)
    args.json.write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
  if args.markdown:
    args.markdown.parent.mkdir(parents=True, exist_ok=True)
    args.markdown.write_text(report, encoding="utf-8")

  if args.ratchet:
    updated, lowered = ratchet_floor(
        floor, summary, args.ratchet_margin, args.lower, args.reason
    )
    if not args.lower:
      # Report what a ratchet refused to write, so a real regression is
      # visible here rather than only in the next --check run.
      blocked = floor_failures(summary, updated)
      for failure in blocked:
        print(f"note: floor held above the measurement, {failure}")
      if blocked:
        print(
            "The floor was not lowered. Raise coverage, or rerun with --lower "
            "and --reason if the drop is intended.",
            file=sys.stderr,
        )
    for entry in lowered:
      print(f"lowered {entry}")
    floor_path.parent.mkdir(parents=True, exist_ok=True)
    floor_path.write_text(
        json.dumps(updated, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print(f"Wrote the coverage floor to {floor_path}.")
    return 0

  if args.check:
    if floor is None:
      raise CoverageError(f"--check needs a floor document at {floor_path}")
    failures = floor_failures(summary, floor)
    if failures:
      print("Coverage floor check failed:", file=sys.stderr)
      for failure in failures:
        print(f"- {failure}", file=sys.stderr)
      print(
          "Raise coverage, or run tools/coverage.py --ratchet if the drop is "
          "intended and justified in the PR.",
          file=sys.stderr,
      )
      return 1
    print("Coverage is at or above every floor.")
  return 0


if __name__ == "__main__":
  try:
    raise SystemExit(main())
  except CoverageError as error:
    print(f"coverage: {error}", file=sys.stderr)
    raise SystemExit(2)
