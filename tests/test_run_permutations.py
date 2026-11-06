"""Regression tests for the permutation scenario generator."""

import importlib.util
import json
import os
from pathlib import Path
import stat
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "run_permutations", ROOT / "sim" / "scripts" / "run-permutations.py")
assert SPEC and SPEC.loader
PERMS = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = PERMS
SPEC.loader.exec_module(PERMS)


COMBO = {
    "web_ui": "on",
    "mqtt": "off",
    "mqtt_ha": "on",
    "companion": "on",
    "wifi": "connected",
    "camera": "none",
    "psram": "present",
    "mqtt_tls": "tls",
}


class ScenarioGeneratorTest(unittest.TestCase):
    def test_mqtt_off_does_not_enable_the_stack_model(self):
        script = PERMS.scenario_for("sticks3", COMBO)
        self.assertNotIn("mqtt_owner_stack", script)
        self.assertNotIn("mqtt_worker_stack", script)
        self.assertNotIn("mqtt_tls", script)
        self.assertNotIn("mqtt_ha", script)
        self.assertNotIn("mqtt_buffer", script)

    def test_mqtt_on_selects_the_tls_worker_for_secure(self):
        combo = dict(COMBO, mqtt="on")
        script = PERMS.scenario_for("sticks3", combo)
        self.assertIn("seed mqtt_owner_stack 10240", script)
        self.assertIn("seed mqtt_worker_stack 6144", script)
        self.assertIn("seed mqtt_tls true", script)

    def test_wifi_seed_is_boolean(self):
        script = PERMS.scenario_for("sticks3", COMBO)
        self.assertIn("seed wifi_connected true", script)
        reconnecting = dict(COMBO, wifi="reconnecting")
        script = PERMS.scenario_for("sticks3", reconnecting)
        self.assertIn("seed wifi_connected false", script)
        self.assertIn("action wifi-fail-auth", script)

    def test_compact_result_omits_output_and_is_small(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            fake = root / "fake-sim"
            fake.write_text("#!/bin/sh\nexit 0\n", encoding="utf-8")
            fake.chmod(fake.stat().st_mode | stat.S_IXUSR)
            results = root / "results.json"
            code = PERMS.main(["--binary", str(fake), "--results", str(results),
                               "--limit", "1", "--compact"])
            self.assertEqual(code, 0)
            raw = results.read_text(encoding="utf-8")
            document = json.loads(raw)
        self.assertNotIn("\n  ", raw)
        self.assertEqual(len(document["results"]), 1)
        self.assertFalse(document["complete"])
        entry = document["results"][0]
        self.assertEqual(set(entry), {"id", "board", "combination", "passed", "exit"})

    def _write_small_manifest(self, root):
        manifest = {
            "version": 1,
            "boards": ["sticks3", "core2"],
            "axes": {
                "web_ui": ["on"],
                "mqtt": ["off"],
                "mqtt_ha": ["off"],
                "companion": ["off"],
                "wifi": ["disconnected"],
                "camera": ["none"],
                "psram": ["present"],
                "mqtt_tls": ["plain"],
            },
            "expected_count": 2,
            "scenario": "sim/fixtures/hardware/config-matrix.txt",
            "assertions": ["setting.fauxny"],
        }
        path = root / "permutations.json"
        path.write_text(json.dumps(manifest), encoding="utf-8")
        return path

    def _fake_binary(self, root):
        fake = root / "fake-sim"
        fake.write_text("#!/bin/sh\nexit 0\n", encoding="utf-8")
        fake.chmod(fake.stat().st_mode | stat.S_IXUSR)
        return fake

    def test_unknown_board_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest = self._write_small_manifest(root)
            fake = self._fake_binary(root)
            code = PERMS.main(["--binary", str(fake), "--manifest", str(manifest),
                               "--results", str(root / "results.json"),
                               "--board", "not-a-board", "--compact"])
        self.assertEqual(code, 2)

    def test_board_filtered_run_is_not_complete(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest = self._write_small_manifest(root)
            fake = self._fake_binary(root)
            results = root / "results.json"
            code = PERMS.main(["--binary", str(fake), "--manifest", str(manifest),
                               "--results", str(results), "--board", "sticks3",
                               "--compact"])
            self.assertEqual(code, 0)
            document = json.loads(results.read_text(encoding="utf-8"))
        self.assertEqual(document["run_count"], 1)
        self.assertFalse(document["complete"])


if __name__ == "__main__":
    unittest.main()
