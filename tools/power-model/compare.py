#!/usr/bin/env python3
"""Compare one simulator power report with its checked-in baseline."""

from __future__ import annotations

import argparse
import json
import math
import re
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
        legacy_fields = {
            "duration_ms",
            "mcu_ms",
            "display_ms",
            "radio_connected_ms",
            "radio_event_count",
            "gps_ms",
        }
        if legacy_fields.issubset(inputs) and set(inputs).issubset(legacy_fields):
            return "legacy-unaccounted", ""
        raise ValueError("report has no accounting metadata fields")
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
        or re.fullmatch(r"[0-9a-fA-F]{64}", fingerprint) is None
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


# Activity counters move with code, not with the current table, so they get
# their own band. A relative band alone is meaningless near zero, so each
# family also has an absolute slack: a change must exceed both to fail.
DEFAULT_ACTIVITY_THRESHOLD = 0.25
TIMER_FIRE_SLACK = 2
PIXEL_SLACK = 1024
DEFAULT_OPT_OUT = Path(__file__).resolve().parent / "activity-optout.json"


def _count(value, what: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value < 0:
        raise ValueError(f"report {what} is not a non-negative integer")
    return value


def activity_metrics(report: dict) -> dict[str, tuple[int, int]] | None:
    """Return {metric: (count, absolute slack)}, or None when absent."""
    activity = report.get("activity")
    if activity is None:
        return None
    if not isinstance(activity, dict):
        raise ValueError("report activity is not a JSON object")
    metrics: dict[str, tuple[int, int]] = {}
    fires = activity.get("timer_fires", {})
    if not isinstance(fires, dict):
        raise ValueError("report activity.timer_fires is not a JSON object")
    for name, entry in fires.items():
        if not isinstance(entry, dict):
            raise ValueError(f"report timer {name} is not a JSON object")
        metrics[f"timer_fires.{name}"] = (_count(entry.get("count"), f"timer {name} count"),
                                          TIMER_FIRE_SLACK)
    for key in ("invalidated_area_pixels", "flushed_pixels"):
        metrics[key] = (_count(activity.get(key), f"activity.{key}"), PIXEL_SLACK)
    return metrics


def load_opt_out(path: Path, scenario: str) -> set[str]:
    """Return the metrics this scenario opts out of. A missing file is empty."""
    if not path.exists():
        return set()
    data = json.loads(path.read_text())
    scenarios = data.get("scenarios") if isinstance(data, dict) else None
    if not isinstance(scenarios, dict):
        raise ValueError("activity opt-out has no scenarios object")
    entry = scenarios.get(scenario)
    if entry is None:
        return set()
    if not isinstance(entry, dict):
        raise ValueError(f"activity opt-out for {scenario} is not a JSON object")
    metrics = entry.get("metrics")
    reason = entry.get("reason")
    if (
        not isinstance(metrics, list)
        or not metrics
        or not all(isinstance(metric, str) and metric for metric in metrics)
    ):
        raise ValueError(f"activity opt-out for {scenario} needs a non-empty metrics list")
    if not isinstance(reason, str) or not reason.strip():
        raise ValueError(f"activity opt-out for {scenario} needs a reason")
    return set(metrics)


def activity_drift(
    report: dict, baseline: dict, threshold: float, opt_out: set[str]
) -> list[str] | None:
    """Return one line per drifted metric, or None when neither has counters."""
    current = activity_metrics(report)
    reference = activity_metrics(baseline)
    if current is None and reference is None:
        return None
    if current is None or reference is None:
        raise ValueError("activity counters are present in only one of report and baseline")
    lines = []
    for metric in sorted(set(current) | set(reference)):
        if "*" in opt_out or metric in opt_out:
            continue
        now, slack = current.get(metric, (0, TIMER_FIRE_SLACK))
        then, _ = reference.get(metric, (0, slack))
        if abs(now - then) <= max(threshold * then, slack):
            continue
        delta = "inf" if then == 0 else f"{(now - then) / then * 100.0:+.2f}%"
        lines.append(f"  activity {metric}: {now} vs {then} ({delta}) FAIL")
    return lines


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
    parser.add_argument(
        "--activity-threshold",
        type=float,
        default=DEFAULT_ACTIVITY_THRESHOLD,
        help="allowed relative drift of timer fires and invalidated or flushed "
        f"pixels, default {DEFAULT_ACTIVITY_THRESHOLD}",
    )
    parser.add_argument(
        "--activity-opt-out",
        type=Path,
        default=DEFAULT_OPT_OUT,
        help="JSON file naming per-scenario activity metrics to skip, with a reason",
    )
    args = parser.parse_args()

    if not math.isfinite(args.threshold) or args.threshold < 0:
        parser.error("threshold must be finite and non-negative")
    if not math.isfinite(args.activity_threshold) or args.activity_threshold < 0:
        parser.error("activity threshold must be finite and non-negative")

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
    try:
        opt_out = load_opt_out(args.activity_opt_out, name)
        drift = activity_drift(report, baseline, args.activity_threshold, opt_out)
    except (OSError, json.JSONDecodeError, ValueError) as error:
        print(f"compare: {error}", file=sys.stderr)
        return 2
    status = "FAIL" if failed else "PASS"
    print(f"{name}: {current:.6f} mA vs {reference:.6f} mA ({delta_text}) {status}")
    if drift:
        for line in drift:
            print(line)
        failed = True
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
