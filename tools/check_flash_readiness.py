#!/usr/bin/env python3
"""Machine-enforced sim-first flash gate.

The gate is blocked unless:
- every hardware quirk is modeled (no approximate or not-modeled entries);
- every known crash has a modeled reproduction scenario;
- every fuzz target is modeled and its results are present and passing;
- every memory-critical permutation has a passing result;
- the memory profile dumps pass the golden budget;
- the branch is clean and the report names the exact commit.

Run without --ready to print the current blockers as evidence.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def read_json(path: Path):
    return json.loads(path.read_text(encoding="utf-8"))


def git_commit() -> str:
    result = subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT,
                            capture_output=True, text=True)
    return result.stdout.strip() if result.returncode == 0 else "unknown"


def git_dirty() -> bool:
    result = subprocess.run(["git", "status", "--porcelain"], cwd=ROOT,
                            capture_output=True, text=True)
    return bool(result.stdout.strip())


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ready", action="store_true",
                        help="fail unless every gate is ready to flash")
    parser.add_argument("--write-report", type=Path,
                        help="write the report to this path")
    parser.add_argument("--memory-dir", type=Path,
                        default=ROOT / ".pio/sim-memory")
    args = parser.parse_args(argv)

    coverage = load_module("sim_coverage",
                           ROOT / "tools/check_sim_coverage.py")
    hardware = read_json(ROOT / "sim/coverage/hardware_quirks.json")
    crashes = read_json(ROOT / "sim/coverage/crashes.json")
    fuzz = read_json(ROOT / "sim/coverage/fuzz_targets.json")
    permutations = read_json(ROOT / "sim/coverage/permutations.json")

    blockers: list[str] = []
    h_errors, h_counts = coverage.check_hardware(hardware)
    c_errors, c_counts = coverage.check_crashes(crashes)
    f_errors, f_counts = coverage.check_fuzz(fuzz)
    p_errors, p_counts = coverage.check_permutations(permutations)
    blockers += h_errors + c_errors + f_errors + p_errors

    for entry in hardware.get("entries", []):
        if entry.get("status") != "modeled":
            blockers.append(f"hardware not 100% modeled: {entry.get('id')} "
                            f"({entry.get('status')})")
    for entry in crashes.get("crashes", []):
        if entry.get("status") != "modeled":
            blockers.append(f"crash not modeled: {entry.get('id')}")
        for seed in entry.get("known_failing_seeds", []):
            if not seed.get("fixed"):
                blockers.append(f"known failing crash seed: {entry.get('id')}")
    for entry in fuzz.get("targets", []):
        if entry.get("status") != "modeled":
            blockers.append(f"fuzz target not modeled: {entry.get('id')}")
        for seed in entry.get("known_failing_seeds", []):
            if not seed.get("fixed"):
                blockers.append(f"known failing fuzz seed: {entry.get('id')} {seed}")
    fuzz_results = ROOT / fuzz.get("results", "")
    if not fuzz_results.is_file():
        blockers.append("fuzz results missing")
    else:
        results = read_json(fuzz_results)
        if results.get("status") != "pass":
            blockers.append("fuzz results are not passing")
    perm_results = ROOT / permutations.get("results", "")
    if not perm_results.is_file():
        blockers.append("permutation results missing")
    else:
        results = read_json(perm_results).get("results", [])
        if len(results) != permutations.get("expected_count"):
            blockers.append("permutation results incomplete")
        for result in results:
            if not result.get("passed"):
                blockers.append(f"permutation failed: {result.get('id')}")
    if not args.memory_dir.is_dir():
        blockers.append("memory profile dumps missing")
    else:
        memory = load_module("sim_memory", ROOT / "tools/check_sim_memory.py")
        budget = read_json(ROOT / "sim/coverage/memory_budget.json")
        dumps = sorted(args.memory_dir.glob("*.json"))
        if not dumps:
            blockers.append("memory profile dumps missing")
        for dump_path in dumps:
            try:
                dump = read_json(dump_path)
            except json.JSONDecodeError:
                blockers.append(f"memory dump unreadable: {dump_path.name}")
                continue
            scenario = dump.get("scenario")
            if scenario not in budget.get("scenarios", {}):
                blockers.append(f"memory dump has no budget: {scenario}")
                continue
            for error in memory.check_dump(dump, budget["scenarios"][scenario]):
                blockers.append(f"memory {scenario}: {error}")
    if git_dirty():
        blockers.append("worktree is dirty")

    status = "READY TO FLASH" if not blockers else "BLOCKED"
    lines = [
        "# Sim-first flash readiness",
        "",
        f"Commit: `{git_commit()}`",
        f"Status: **{status}**",
        "",
        f"- hardware entries: {h_counts}",
        f"- crash entries: {c_counts}",
        f"- fuzz targets: {f_counts}",
        f"- permutations: {p_counts}",
        "",
    ]
    if blockers:
        lines.append("## Blockers")
        lines.append("")
        lines.extend(f"- {blocker}" for blocker in blockers)
        lines.append("")
    lines.append("## Required commands")
    lines.append("")
    lines.append("```sh")
    lines.append("python3 tools/check_sim_scenarios.py")
    lines.append("python3 tools/check_sim_coverage.py --require-ready")
    lines.append("sh sim/scripts/check-doc-tokens.sh")
    lines.append("sh sim/scripts/check-hardware-quirks.sh")
    lines.append("python3 sim/scripts/run-memory-profile.py")
    lines.append("sh sim/scripts/run-fuzz.sh")
    lines.append("python3 tools/check_flash_readiness.py --ready")
    lines.append("python3 tools/pre_review.py --base origin/master")
    lines.append("```")
    report = "\n".join(lines) + "\n"
    print(report)
    if args.write_report:
        args.write_report.parent.mkdir(parents=True, exist_ok=True)
        args.write_report.write_text(report, encoding="utf-8")
    if args.ready and blockers:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
