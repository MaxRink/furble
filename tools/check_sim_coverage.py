#!/usr/bin/env python3
"""Validate the simulator hardware, crash, fuzz and permutation coverage manifest.

The manifest is the denominator. A modeled or approximate entry must name a
model, a scenario or runner, and at least one assertion token that the scenario
actually contains. A not-modeled entry must state the reason and the missing
evidence. The release gate additionally requires every entry to be modeled and
every fuzz/permutation result to be present and passing.
"""
from __future__ import annotations

import argparse
import itertools
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
CATEGORIES = {"board", "memory", "power", "usb", "display", "input", "radio",
              "peripheral", "reset", "crash", "boot", "fuzz"}
STATUSES = {"modeled", "approximate", "not-modeled", "external-pending"}


def load(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def scenario_text(path: Path) -> str:
    if not path.is_file():
        return ""
    return path.read_text(encoding="utf-8", errors="replace")


def check_hardware(doc: dict) -> tuple[list[str], dict[str, int]]:
    errors: list[str] = []
    entries = doc.get("entries")
    if not isinstance(entries, list) or not entries:
        return ["hardware manifest has no entries"], {}
    profiles = set(doc.get("profiles", []))
    seen: set[str] = set()
    counts = {"total": len(entries), "modeled": 0, "approximate": 0,
              "not-modeled": 0}
    for index, entry in enumerate(entries):
        label = f"hardware entry {index + 1}"
        if not isinstance(entry, dict):
            errors.append(f"{label} is not an object")
            continue
        missing = {"id", "profile", "category", "feature", "source", "model",
                   "scenario", "assertions", "status"} - set(entry)
        if missing:
            errors.append(f"{label} missing: {', '.join(sorted(missing))}")
            continue
        eid = entry["id"]
        if eid in seen:
            errors.append(f"duplicate hardware id: {eid}")
        seen.add(eid)
        if entry["profile"] not in profiles and entry["profile"] != "all":
            errors.append(f"{eid}: unknown profile {entry['profile']}")
        if entry["category"] not in CATEGORIES:
            errors.append(f"{eid}: unknown category {entry['category']}")
        status = entry["status"]
        if status not in STATUSES:
            errors.append(f"{eid}: unknown status {status}")
            continue
        if status in counts:
            counts[status] += 1
        if status == "not-modeled":
            if not entry.get("missing_evidence"):
                errors.append(f"{eid}: not-modeled needs missing_evidence")
            continue
        if status == "external-pending":
            if not entry.get("missing_evidence"):
                errors.append(f"{eid}: external-pending needs missing_evidence")
            continue
        model = ROOT / entry["model"]
        scenario = ROOT / entry["scenario"]
        if not model.is_file():
            errors.append(f"{eid}: model not found: {entry['model']}")
        if not scenario.is_file():
            errors.append(f"{eid}: scenario/runner not found: {entry['scenario']}")
            continue
        assertions = entry["assertions"]
        if not isinstance(assertions, list) or not assertions:
            errors.append(f"{eid}: no assertions")
            continue
        text = scenario_text(scenario)
        if not any(str(assertion).split()[0] in text for assertion in assertions):
            errors.append(f"{eid}: none of {assertions!r} appear in {entry['scenario']}")
    return errors, counts


def check_crashes(doc: dict) -> tuple[list[str], dict[str, int]]:
    errors: list[str] = []
    crashes = doc.get("crashes")
    if not isinstance(crashes, list) or not crashes:
        return ["crash manifest has no entries"], {}
    seen: set[str] = set()
    counts = {"total": len(crashes), "modeled": 0, "approximate": 0,
              "not-modeled": 0}
    for index, entry in enumerate(crashes):
        label = f"crash entry {index + 1}"
        if not isinstance(entry, dict):
            errors.append(f"{label} is not an object")
            continue
        missing = {"id", "scenario", "assertions", "status", "known_failing_seeds"} - set(entry)
        if missing:
            errors.append(f"{label} missing: {', '.join(sorted(missing))}")
            continue
        cid = entry["id"]
        if cid in seen:
            errors.append(f"duplicate crash id: {cid}")
        seen.add(cid)
        status = entry["status"]
        if status not in {"modeled", "approximate", "not-modeled"}:
            errors.append(f"{cid}: unknown status {status}")
            continue
        counts[status] += 1
        if status == "not-modeled":
            if not entry.get("missing_evidence"):
                errors.append(f"{cid}: not-modeled needs missing_evidence")
            continue
        scenario = ROOT / entry["scenario"]
        if not scenario.is_file():
            errors.append(f"{cid}: scenario not found: {entry['scenario']}")
            continue
        text = scenario_text(scenario)
        if not any(str(assertion).split()[0] in text for assertion in entry["assertions"]):
            errors.append(f"{cid}: none of {entry['assertions']!r} appear in {entry['scenario']}")
        seeds = entry["known_failing_seeds"]
        if not isinstance(seeds, list):
            errors.append(f"{cid}: known_failing_seeds must be a list")
        for seed in seeds:
            if not isinstance(seed, dict) or not seed.get("fixed"):
                errors.append(f"{cid}: failing seed {seed!r} is not fixed")
            if isinstance(seed, dict) and not seed.get("regression"):
                errors.append(f"{cid}: failing seed {seed.get('id', '?')} has no regression")
    return errors, counts


def check_fuzz(doc: dict) -> tuple[list[str], dict[str, int]]:
    errors: list[str] = []
    targets = doc.get("targets")
    if not isinstance(targets, list) or not targets:
        return ["fuzz manifest has no targets"], {}
    seen: set[str] = set()
    counts = {"total": len(targets), "modeled": 0, "external-pending": 0}
    for index, entry in enumerate(targets):
        label = f"fuzz target {index + 1}"
        if not isinstance(entry, dict):
            errors.append(f"{label} is not an object")
            continue
        missing = {"id", "command", "scenario", "assertions", "known_failing_seeds",
                   "status"} - set(entry)
        if missing:
            errors.append(f"{label} missing: {', '.join(sorted(missing))}")
            continue
        fid = entry["id"]
        if fid in seen:
            errors.append(f"duplicate fuzz id: {fid}")
        seen.add(fid)
        status = entry["status"]
        if status not in {"modeled", "external-pending"}:
            errors.append(f"{fid}: unknown status {status}")
            continue
        counts[status] += 1
        if status == "external-pending" and not entry.get("missing_evidence"):
            errors.append(f"{fid}: external-pending needs missing_evidence")
        command = entry["command"].split()
        script = command[-1] if command else ""
        if script.startswith("sim/") and not (ROOT / script).is_file():
            errors.append(f"{fid}: command target not found: {script}")
        scenario = ROOT / entry["scenario"]
        if not scenario.is_file():
            errors.append(f"{fid}: scenario not found: {entry['scenario']}")
            continue
        text = scenario_text(scenario) + "\n" + scenario_text(ROOT / script)
        if not any(str(assertion).split()[0] in text for assertion in entry["assertions"]):
            errors.append(f"{fid}: none of {entry['assertions']!r} appear in scenario or command")
        for seed in entry["known_failing_seeds"]:
            if not isinstance(seed, dict) or not seed.get("fixed"):
                errors.append(f"{fid}: failing seed {seed!r} is not fixed")
            if isinstance(seed, dict) and not seed.get("regression"):
                errors.append(f"{fid}: failing seed {seed.get('id', '?')} has no regression")
    return errors, counts


def expected_permutations(doc: dict) -> int:
    boards = doc.get("boards", [])
    axes = doc.get("axes", {})
    return len(boards) * (1 if not axes else 1) * __import__("math").prod(
        len(values) for values in axes.values())


def check_permutations(doc: dict) -> tuple[list[str], dict[str, int]]:
    errors: list[str] = []
    expected = expected_permutations(doc)
    declared = doc.get("expected_count")
    counts = {"declared": declared, "expected": expected, "results": 0}
    if declared != expected:
        errors.append(f"permutation count {declared} does not match dimensions {expected}")
    scenario = ROOT / doc.get("scenario", "")
    if not scenario.is_file():
        errors.append(f"permutation scenario not found: {doc.get('scenario')}")
    else:
        text = scenario_text(scenario)
        if not any(str(assertion).split()[0] in text for assertion in doc.get("assertions", [])):
            errors.append("permutation assertions do not appear in template")
    results_path = ROOT / doc.get("results", "")
    if results_path.is_file():
        results = load(results_path)
        entries = results.get("results", [])
        counts["results"] = len(entries)
        if len(entries) != expected:
            errors.append(f"permutation results {len(entries)} do not match expected {expected}")
    return errors, counts


def check_ready(hardware_doc: dict, crash_doc: dict, fuzz_doc: dict,
                permutation_doc: dict) -> list[str]:
    errors: list[str] = []
    for entry in hardware_doc.get("entries", []):
        if entry.get("status") != "modeled":
            errors.append(f"not modeled: {entry.get('id')} ({entry.get('status')})")
    for entry in crash_doc.get("crashes", []):
        if entry.get("status") != "modeled":
            errors.append(f"crash not modeled: {entry.get('id')} ({entry.get('status')})")
    for entry in fuzz_doc.get("targets", []):
        if entry.get("status") != "modeled":
            errors.append(f"fuzz target pending: {entry.get('id')}")
        if entry.get("known_failing_seeds"):
            errors.append(f"fuzz target has failing seeds: {entry.get('id')}")
    results_path = ROOT / permutation_doc.get("results", "")
    if not results_path.is_file():
        errors.append("permutation results missing")
    else:
        results = load(results_path)
        if len(results.get("results", [])) != expected_permutations(permutation_doc):
            errors.append("permutation results incomplete")
        for result in results.get("results", []):
            if not result.get("passed"):
                errors.append(f"permutation failed: {result.get('id')}")
    return errors


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--hardware", type=Path,
                        default=ROOT / "sim/coverage/hardware_quirks.json")
    parser.add_argument("--crashes", type=Path,
                        default=ROOT / "sim/coverage/crashes.json")
    parser.add_argument("--fuzz", type=Path,
                        default=ROOT / "sim/coverage/fuzz_targets.json")
    parser.add_argument("--permutations", type=Path,
                        default=ROOT / "sim/coverage/permutations.json")
    parser.add_argument("--require-ready", action="store_true",
                        help="require every entry modeled and every result present")
    args = parser.parse_args(argv)

    hardware_doc = load(args.hardware)
    crash_doc = load(args.crashes)
    fuzz_doc = load(args.fuzz)
    permutation_doc = load(args.permutations)

    hardware_errors, hardware_counts = check_hardware(hardware_doc)
    crash_errors, crash_counts = check_crashes(crash_doc)
    fuzz_errors, fuzz_counts = check_fuzz(fuzz_doc)
    permutation_errors, permutation_counts = check_permutations(permutation_doc)
    errors = hardware_errors + crash_errors + fuzz_errors + permutation_errors
    if args.require_ready:
        errors += check_ready(hardware_doc, crash_doc, fuzz_doc, permutation_doc)

    print(f"hardware: {hardware_counts}")
    print(f"crashes:  {crash_counts}")
    print(f"fuzz:     {fuzz_counts}")
    print(f"perms:    {permutation_counts}")
    if errors:
        print("simulator coverage failures:", file=sys.stderr)
        for error in errors:
            print(f"  {error}", file=sys.stderr)
        return 1
    print("simulator coverage manifest is complete")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
