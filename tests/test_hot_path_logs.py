"""Tests for the hot-path log-level guard."""

from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
CHECKER = ROOT / "tools" / "check_hot_path_logs.py"


class HotPathLogTest(unittest.TestCase):
    def test_current_tree_passes(self):
        result = subprocess.run(
            [sys.executable, str(CHECKER)],
            check=False,
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)

    def make_tree(self, power_log: str, lumix_log: str = "ESP_LOGD") -> Path:
        root = Path(tempfile.mkdtemp(prefix="furble-log-"))
        (root / "src").mkdir()
        (root / "lib/furble").mkdir(parents=True)
        (root / "src/FurblePower.cpp").write_text(
            "void Power::acquire(int) {\n  " + power_log + "(\"power\", \"x\");\n}\n"
            "void Power::release(int) {\n  ESP_LOGD(\"power\", \"x\");\n}\n",
            encoding="utf-8",
        )
        (root / "lib/furble/Lumix.cpp").write_text(
            "void Lumix::updateGeoData(int) {\n  " + lumix_log + "(\"lumix\", \"x\");\n}\n",
            encoding="utf-8",
        )
        return root

    def run_checker(self, root: Path) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [sys.executable, str(CHECKER), "--root", str(root)],
            check=False,
            capture_output=True,
            text=True,
        )

    def test_info_on_guarded_path_fails(self):
        result = self.run_checker(self.make_tree("ESP_LOGI"))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("hot path", result.stderr)

    def test_warn_on_guarded_path_fails(self):
        result = self.run_checker(self.make_tree("ESP_LOGD", "ESP_LOGW"))
        self.assertNotEqual(result.returncode, 0)

    def test_error_on_guarded_path_fails(self):
        result = self.run_checker(self.make_tree("ESP_LOGD", "ESP_LOGE"))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("hot path", result.stderr)

    def test_debug_only_tree_passes(self):
        result = self.run_checker(self.make_tree("ESP_LOGD"))
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
