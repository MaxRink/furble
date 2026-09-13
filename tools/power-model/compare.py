#!/usr/bin/env python3
"""Compare one simulator power report with its checked-in baseline."""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path


def estimated_ma(report: dict) -> float:
    if not isinstance(report, dict):
        raise ValueError("report is not a JSON object")
    value = report.get("estimated_mA")
    if value is None:
        energy = report.get("energy", {})
        if not isinstance(energy, dict):
            raise ValueError("report energy is not a JSON object")
        value = energy.get("estimated_mA")
    if (
        isinstance(value, bool)
        or not isinstance(value, (int, float))
        or not math.isfinite(value)
        or value < 0
    ):
        raise ValueError("report has no finite non-negative estimated_mA value")
    return float(value)


def scenario_name(report: dict, path: Path) -> str:
    value = report.get("scenario")
    return value if isinstance(value, str) and value else path.stem


def accounting_identity(report: dict) -> tuple[str, str]:
    if not isinstance(report, dict):
        raise ValueError("report is not a JSON object")
    energy = report.get("energy", {})
    if not isinstance(energy, dict):
        raise ValueError("report energy is not a JSON object")
    if "accounting_inputs" not in energy:
        return "legacy-unaccounted", ""
    inputs = energy["accounting_inputs"]
    if not isinstance(inputs, dict):
        raise ValueError("report accounting_inputs is not a JSON object")
    if not inputs:
        raise ValueError("report has empty accounting metadata")
    fields = {"accounting_mode", "accounting_version", "accounting_fingerprint", "accounting_valid"}
    present = fields.intersection(inputs)
    if not present:
        return "legacy-unaccounted", ""
    if present != fields:
        raise ValueError("report has incomplete accounting metadata")
    mode = inputs["accounting_mode"]
    version = inputs["accounting_version"]
    fingerprint = inputs["accounting_fingerprint"]
    valid = inputs["accounting_valid"]
    if mode not in ("legacy-unaccounted", "synthetic-virtual-work"):
        raise ValueError("report has unknown accounting mode")
    if not isinstance(valid, bool) or not valid:
      raise ValueError("report accounting is not valid")
    if mode == "synthetic-virtual-work" and (
        not isinstance(version, int)
        or isinstance(version, bool)
        or version != 1
        or not isinstance(fingerprint, str)
        or not fingerprint
    ):
      raise ValueError("report has invalid synthetic accounting metadata")
    if mode == "legacy-unaccounted" and (
        not isinstance(version, int)
        or isinstance(version, bool)
        or version != 0
        or fingerprint != ""
    ):
        raise ValueError("report has invalid legacy accounting metadata")
    return mode, fingerprint


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=Path)
    parser.add_argument("baseline", type=Path)
    parser.add_argument(
        "--threshold",
        type=float,
        default=0.10,
        help="allowed relative increase or decrease, default 0.10",
    )
    args = parser.parse_args()

    if not math.isfinite(args.threshold) or args.threshold < 0:
        parser.error("threshold must be finite and non-negative")

    try:
        report = json.loads(args.report.read_text())
        baseline = json.loads(args.baseline.read_text())
        report_identity = accounting_identity(report)
        baseline_identity = accounting_identity(baseline)
        if report_identity != baseline_identity:
            raise ValueError(
                "accounting mode/model-cost provenance mismatch "
                f"(report {report_identity[0]}/{report_identity[1]} vs "
                f"baseline {baseline_identity[0]}/{baseline_identity[1]})"
            )
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
        failed = (
            current > reference * (1.0 + args.threshold)
            or current < reference * (1.0 - args.threshold)
        )

    delta_text = "inf" if math.isinf(delta) else f"{delta * 100.0:+.2f}%"
    name = scenario_name(report, args.report)
    status = "FAIL" if failed else "PASS"
    print(f"{name}: {current:.6f} mA vs {reference:.6f} mA ({delta_text}) {status}")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
