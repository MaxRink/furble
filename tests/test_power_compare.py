"""Regression contracts for the simulator power report comparator."""

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
COMPARE = ROOT / "tools/power-model/compare.py"


class PowerCompareTest(unittest.TestCase):
  SYNTHETIC_INPUTS = {
      "accounting_mode": "synthetic-virtual-work",
      "accounting_version": 1,
      "accounting_fingerprint": "a" * 64,
      "accounting_valid": True,
  }

  def run_compare(self, current, reference, *arguments):
    with tempfile.TemporaryDirectory() as directory:
      root = Path(directory)
      report = root / "report.json"
      baseline = root / "baseline.json"
      report.write_text(json.dumps(current), encoding="utf-8")
      baseline.write_text(json.dumps(reference), encoding="utf-8")
      return subprocess.run(
          [sys.executable, str(COMPARE), str(report), str(baseline), *arguments],
          capture_output=True,
          text=True,
          check=False,
      )

  def test_large_decrease_fails(self):
    result = self.run_compare({"estimated_mA": 0.59}, {"estimated_mA": 1.0})
    self.assertEqual(result.returncode, 1)
    self.assertIn("-41.00%", result.stdout)
    self.assertIn("FAIL", result.stdout)

  def test_large_increase_fails(self):
    result = self.run_compare({"estimated_mA": 1.11}, {"estimated_mA": 1.0})
    self.assertEqual(result.returncode, 1)
    self.assertIn("+11.00%", result.stdout)
    self.assertIn("FAIL", result.stdout)

  def test_delta_inside_band_passes(self):
    result = self.run_compare({"estimated_mA": 0.95}, {"estimated_mA": 1.0})
    self.assertEqual(result.returncode, 0)
    self.assertIn("-5.00%", result.stdout)
    self.assertIn("PASS", result.stdout)

  def test_exact_band_boundaries_pass(self):
    for current in (0.9, 1.1):
      result = self.run_compare({"estimated_mA": current}, {"estimated_mA": 1.0})
      self.assertEqual(result.returncode, 0)

  def test_just_outside_band_fails(self):
    for current in (0.899999, 1.100001):
      result = self.run_compare({"estimated_mA": current}, {"estimated_mA": 1.0})
      self.assertEqual(result.returncode, 1)

  def test_zero_baseline_keeps_existing_semantics(self):
    self.assertEqual(
        self.run_compare({"estimated_mA": 0.0}, {"estimated_mA": 0.0}).returncode,
        0,
    )
    result = self.run_compare({"estimated_mA": 0.1}, {"estimated_mA": 0.0})
    self.assertEqual(result.returncode, 1)
    self.assertIn("inf", result.stdout)

  def test_missing_and_nonfinite_inputs_are_errors(self):
    for report, baseline in (
        ({}, {"estimated_mA": 1.0}),
        ({"estimated_mA": 1.0}, {}),
        ({"estimated_mA": float("nan")}, {"estimated_mA": 1.0}),
        ({"estimated_mA": 1.0}, {"estimated_mA": float("inf")}),
        ({"estimated_mA": True}, {"estimated_mA": 1.0}),
        ({"estimated_mA": -1.0}, {"estimated_mA": 1.0}),
        ([], {
  "estimated_mA" : 1.0}),
        ({"energy": []}, {"estimated_mA": 1.0}),
    ):
      result = self.run_compare(report, baseline)
      self.assertEqual(result.returncode, 2)
      self.assertIn("compare:", result.stderr)

  def test_custom_threshold_allows_known_band(self):
    result = self.run_compare(
        {"estimated_mA": 0.5}, {"estimated_mA": 1.0}, "--threshold", "0.5"
    )
    self.assertEqual(result.returncode, 0)

  def test_accounting_identity_uses_nested_report_inputs(self):
    current = {
        "estimated_mA": 1.0,
        "energy": {"accounting_inputs": {**self.SYNTHETIC_INPUTS}},
    }
    current["energy"]["accounting_inputs"]["accounting_fingerprint"] = "a" * 64
    baseline = {
        "estimated_mA": 1.0,
        "energy": {"accounting_inputs": {**self.SYNTHETIC_INPUTS}},
    }
    baseline["energy"]["accounting_inputs"]["accounting_fingerprint"] = "b" * 64
    result = self.run_compare(current, baseline)
    self.assertEqual(result.returncode, 2)
    self.assertIn("accounting mode/model-cost provenance mismatch", result.stderr)

  def test_schema_arrays_are_rejected_before_field_access(self):
    result = self.run_compare([], {"estimated_mA": 1.0})
    self.assertEqual(result.returncode, 2)
    self.assertIn("report is not a JSON object", result.stderr)

  def test_accounting_identity_must_match(self):
    current = {
        "estimated_mA": 1.0,
        "energy": {"accounting_inputs": {**self.SYNTHETIC_INPUTS}},
    }
    current["energy"]["accounting_inputs"]["accounting_fingerprint"] = "a" * 64
    baseline = {
        "estimated_mA": 1.0,
        "energy": {"accounting_inputs": {**self.SYNTHETIC_INPUTS}},
    }
    baseline["energy"]["accounting_inputs"]["accounting_fingerprint"] = "b" * 64
    result = self.run_compare(current, baseline)
    self.assertEqual(result.returncode, 2)
    self.assertIn("accounting mode/model-cost provenance mismatch", result.stderr)

  def test_matching_synthetic_accounting_identity_passes(self):
    report = {"estimated_mA": 1.0, "energy": {"accounting_inputs": self.SYNTHETIC_INPUTS}}
    self.assertEqual(self.run_compare(report, report).returncode, 0)

  def test_legacy_reports_have_compatible_implicit_identity(self):
    result = self.run_compare(
        {"estimated_mA": 1.0},
        {"estimated_mA": 1.0, "energy": {"accounting_inputs": {
            "accounting_mode": "legacy-unaccounted",
            "accounting_version": 0,
            "accounting_fingerprint": "",
            "accounting_valid": True,
        }}},
    )
    self.assertEqual(result.returncode, 0)

  def test_legacy_reports_with_pre_accounting_inputs_remain_compatible(self):
    legacy_inputs = {
        "duration_ms": 1000,
        "mcu_ms": {},
        "display_ms": {},
        "radio_connected_ms": 0,
        "radio_event_count": 0,
        "gps_ms": {},
    }
    result = self.run_compare(
        {"estimated_mA": 1.0, "energy": {"accounting_inputs": legacy_inputs}},
        {"estimated_mA": 1.0},
    )
    self.assertEqual(result.returncode, 0)

  def test_explicit_malformed_accounting_metadata_is_not_legacy(self):
    for key, value in (
        ("accounting_version", 2),
        ("accounting_valid", False),
        ("accounting_fingerprint", ""),
    ):
      inputs = {**self.SYNTHETIC_INPUTS, key: value}
      result = self.run_compare(
          {"estimated_mA": 1.0, "energy": {"accounting_inputs": inputs}},
          {"estimated_mA": 1.0},
      )
      self.assertEqual(result.returncode, 2)
      self.assertIn("compare:", result.stderr)

  def test_empty_explicit_accounting_metadata_is_rejected(self):
    result = self.run_compare(
        {"estimated_mA": 1.0, "energy": {"accounting_inputs": {}}},
        {"estimated_mA": 1.0},
    )
    self.assertEqual(result.returncode, 2)
    self.assertIn("empty accounting metadata", result.stderr)

  def test_unknown_explicit_accounting_metadata_is_rejected(self):
    result = self.run_compare(
        {"estimated_mA": 1.0, "energy": {"accounting_inputs": {"future": 1}}},
        {"estimated_mA": 1.0},
    )
    self.assertEqual(result.returncode, 2)
    self.assertIn("no accounting metadata fields", result.stderr)

  def test_invalid_threshold_is_rejected(self):
    for value in ("-0.1", "nan", "inf"):
      result = self.run_compare(
          {"estimated_mA": 1.0}, {"estimated_mA": 1.0}, "--threshold", value
      )
      self.assertEqual(result.returncode, 2)
      self.assertIn("threshold must be finite", result.stderr)


if __name__ == "__main__":
  unittest.main()
