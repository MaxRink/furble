"""Tests for the stdlib-only companion power plotter."""

import contextlib
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "tools/plot_companion_power.py"


def load_plotter():
    spec = importlib.util.spec_from_file_location("plot_companion_power", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


SAMPLE_CSV = """\
host_time_iso,host_elapsed_s,uptime_s,battery_percent,battery_mv,battery_ma,gps_source,gps_satellites,control_state,rssi,label
2026-01-01T00:00:00Z,0.0,100,100,4200,0,0,0,0,-50,GPS off
2026-01-01T00:01:00Z,60.0,160,99,4180,0,0,0,0,-51,GPS off
2026-01-01T00:02:00Z,120.0,,,,,,,,,link-lost
2026-01-01T00:02:30Z,150.0,,,,,,,,,reconnected
2026-01-01T00:03:30Z,210.0,370,98,4150,0,0,0,0,-55,GPS on
2026-01-01T00:04:00Z,240.0,400,97,4140,0,0,0,0,-56,GPS on
2026-01-01T00:05:00Z,300.0,,,,,,,,,battery-empty
"""


class CompanionPowerPlotTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.plot = load_plotter()

    def write_csv(self, directory, text=SAMPLE_CSV):
        path = Path(directory) / "power.csv"
        path.write_text(text, encoding="utf-8")
        return path

    def test_csv_parser_keeps_samples_markers_and_labels(self):
        with tempfile.TemporaryDirectory() as directory:
            run = self.plot.load_csv(self.write_csv(directory))
        self.assertEqual(len(run.samples), 4)
        self.assertEqual([sample.battery_percent for sample in run.samples], [100, 99, 98, 97])
        self.assertEqual([sample.battery_mv for sample in run.samples], [4200, 4180, 4150, 4140])
        self.assertEqual([marker.event for marker in run.markers],
                         ["link-lost", "reconnected", "battery-empty"])
        self.assertEqual(run.labels, [(0.0, "GPS off"), (210.0, "GPS on")])
        self.assertTrue(run.all_current_missing)

    def test_svg_is_self_contained_and_has_both_panels(self):
        with tempfile.TemporaryDirectory() as directory:
            run = self.plot.load_csv(self.write_csv(directory))
        svg = self.plot.build_svg(run, "battery run")
        self.assertTrue(svg.startswith("<?xml"))
        self.assertIn("<svg", svg)
        self.assertIn("Battery percentage", svg)
        self.assertIn("Battery voltage", svg)
        self.assertEqual(svg.count("<polyline"), 2)
        self.assertIn("GPS off", svg)
        self.assertIn("GPS on", svg)
        self.assertIn("link-lost", svg)
        self.assertIn("battery-empty", svg)
        self.assertIn("battery_ma is 0/absent", svg)
        self.assertNotIn("<script", svg)
        self.assertNotIn("xlink:href", svg)

    def test_unknown_percent_is_a_marker_not_a_sample(self):
        text = SAMPLE_CSV.replace("0.0,100,100,4200,0",
                                  "0.0,100,255,0,0")
        with tempfile.TemporaryDirectory() as directory:
            run = self.plot.load_csv(self.write_csv(directory, text))
        self.assertEqual(len(run.samples), 3)
        self.assertIn("battery-unknown", [marker.event for marker in run.markers])

    def test_single_sample_svg_still_renders_a_point(self):
        text = SAMPLE_CSV.splitlines()
        single = "\n".join([text[0], text[1]]) + "\n"
        with tempfile.TemporaryDirectory() as directory:
            run = self.plot.load_csv(self.write_csv(directory, single))
        svg = self.plot.build_svg(run)
        self.assertIn("<circle", svg)
        self.assertIn("1 samples", svg)

    def test_missing_required_column_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = self.write_csv(directory, "host_time_iso,host_elapsed_s\n0,1\n")
            with self.assertRaises(self.plot.CsvError):
                self.plot.load_csv(path)

    def test_main_writes_svg_from_cli(self):
        with tempfile.TemporaryDirectory() as directory:
            csv_path = self.write_csv(directory)
            svg_path = Path(directory) / "power.svg"
            with contextlib.redirect_stderr(io.StringIO()):
                code = self.plot.main([str(csv_path), "--output", str(svg_path)])
            self.assertEqual(code, 0)
            self.assertTrue(svg_path.exists())
            self.assertIn("<svg", svg_path.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
