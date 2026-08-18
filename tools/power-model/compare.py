#!/usr/bin/env python3
"""Compare one simulator power report with its checked-in baseline."""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path


def estimated_ma(report: dict) -> float:
    value = report.get("estimated_mA")
    if value is None:
        value = report.get("energy", {}).get("estimated_mA")
    if not isinstance(value, (int, float)) or not math.isfinite(value):
        raise ValueError("report has no finite estimated_mA value")
    return float(value)


def scenario_name(report: dict, path: Path) -> str:
    value = report.get("scenario")
    return value if isinstance(value, str) and value else path.stem


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=Path)
    parser.add_argument("baseline", type=Path)
    parser.add_argument(
        "--threshold",
        type=float,
        default=0.10,
        help="allowed relative increase, default 0.10",
    )
    args = parser.parse_args()

    if args.threshold < 0:
        parser.error("threshold must not be negative")

    try:
        report = json.loads(args.report.read_text())
        baseline = json.loads(args.baseline.read_text())
        current = estimated_ma(report)
        reference = estimated_ma(baseline)
    except (OSError, json.JSONDecodeError, ValueError) as error:
        print(f"compare: {error}", file=sys.stderr)
        return 2

    report_scenario = report.get("scenario")
    baseline_scenario = baseline.get("scenario")
    if (
        isinstance(report_scenario, str)
        and isinstance(baseline_scenario, str)
        and report_scenario != baseline_scenario
    ):
        print(
            f"compare: scenario mismatch (report {report_scenario} vs baseline {baseline_scenario})",
            file=sys.stderr,
        )
        return 2

    if reference == 0:
        delta = 0.0 if current == 0 else math.inf
        failed = current > 0
    else:
        delta = (current - reference) / reference
        failed = current > reference * (1.0 + args.threshold)

    delta_text = "inf" if math.isinf(delta) else f"{delta * 100.0:+.2f}%"
    name = scenario_name(report, args.report)
    status = "FAIL" if failed else "PASS"
    print(f"{name}: {current:.6f} mA vs {reference:.6f} mA ({delta_text}) {status}")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
