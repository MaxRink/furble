#!/usr/bin/env python3
"""Compare simulator memory dumps against the golden release budget."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]


def check_dump(dump: dict, budget: dict) -> list[str]:
    errors: list[str] = []
    heap = dump.get("heap", {})
    components = {item.get("name"): item.get("bytes")
                  for item in dump.get("static", {}).get("components", [])}
    if "min_internal_free" in budget and heap.get("free", -1) < budget["min_internal_free"]:
        errors.append(f"free {heap.get('free')} below {budget['min_internal_free']}")
    if "min_internal_largest" in budget and heap.get("largest", -1) < budget["min_internal_largest"]:
        errors.append(f"largest {heap.get('largest')} below {budget['min_internal_largest']}")
    if "max_leaks" in budget and heap.get("leaks", 0) > budget["max_leaks"]:
        errors.append(f"leaks {heap.get('leaks')} above {budget['max_leaks']}")
    if ("max_fragmentation_permille" in budget
            and heap.get("fragmentation_permille", 0) > budget["max_fragmentation_permille"]):
        errors.append(f"fragmentation {heap.get('fragmentation_permille')} above "
                      f"{budget['max_fragmentation_permille']}")
    if "min_alloc_failures" in budget and heap.get("alloc_failures", 0) < budget["min_alloc_failures"]:
        errors.append(f"alloc_failures {heap.get('alloc_failures')} below "
                      f"{budget['min_alloc_failures']}")
    for name, expected in budget.get("components", {}).items():
        if components.get(name) != expected:
            errors.append(f"component {name}={components.get(name)} expected {expected}")
    return errors


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--budget", type=Path,
                        default=ROOT / "sim/coverage/memory_budget.json")
    parser.add_argument("dumps", nargs="+", type=Path)
    args = parser.parse_args(argv)
    budget = json.loads(args.budget.read_text(encoding="utf-8"))
    scenarios = budget.get("scenarios", {})
    errors: list[str] = []
    checked = 0
    for path in args.dumps:
        try:
            dump = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as error:
            errors.append(f"{path}: {error}")
            continue
        scenario = dump.get("scenario")
        if scenario not in scenarios:
            errors.append(f"{path}: no budget for scenario {scenario!r}")
            continue
        checked += 1
        for error in check_dump(dump, scenarios[scenario]):
            errors.append(f"{scenario}: {error}")
    if errors:
        for error in errors:
            print(f"FAIL: {error}", file=sys.stderr)
        return 1
    print(f"memory budget passed for {checked} dump(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
