"""Tests for the power-management Kconfig checker."""

from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
CHECKER = ROOT / "tools" / "check_power_config.py"


class PowerConfigTest(unittest.TestCase):
    def run_checker(self, root: Path) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [sys.executable, str(CHECKER), "--root", str(root)],
            check=False,
            capture_output=True,
            text=True,
        )

    def make_root(self, text: str) -> Path:
        temp = Path(tempfile.mkdtemp(prefix="furble-power-config-"))
        (temp / "sdkconfig.test").write_text(text, encoding="utf-8")
        return temp

    def test_tickless_without_idle_opt_fails(self):
        root = self.make_root(
            "CONFIG_PM_ENABLE=y\n"
            "CONFIG_FREERTOS_USE_TICKLESS_IDLE=y\n"
            "# CONFIG_PM_RTOS_IDLE_OPT is not set\n"
        )
        result = self.run_checker(root)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("PM_RTOS_IDLE_OPT", result.stderr)

    def test_tickless_with_idle_opt_passes(self):
        root = self.make_root(
            "CONFIG_PM_ENABLE=y\n"
            "CONFIG_FREERTOS_USE_TICKLESS_IDLE=y\n"
            "CONFIG_PM_RTOS_IDLE_OPT=y\n"
        )
        result = self.run_checker(root)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_mac_bb_pd_without_tickless_fails(self):
        root = self.make_root(
            "CONFIG_ESP_PHY_MAC_BB_PD=y\n"
            "# CONFIG_FREERTOS_USE_TICKLESS_IDLE is not set\n"
        )
        result = self.run_checker(root)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("MAC_BB_PD", result.stderr)

    def test_brownout_without_reduce_tx_power_fails(self):
        root = self.make_root(
            "CONFIG_ESP_BROWNOUT_DET=y\n"
            "# CONFIG_ESP_PHY_REDUCE_TX_POWER is not set\n"
        )
        result = self.run_checker(root)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("REDUCE_TX_POWER", result.stderr)

    def test_brownout_with_reduce_tx_power_passes(self):
        root = self.make_root(
            "CONFIG_ESP_BROWNOUT_DET=y\n"
            "CONFIG_ESP_PHY_REDUCE_TX_POWER=y\n"
        )
        result = self.run_checker(root)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_pm_without_tickless_is_a_recorded_exception(self):
        root = self.make_root("CONFIG_PM_ENABLE=y\n")
        result = self.run_checker(root)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("recorded exception", result.stdout)

    def test_current_tree_passes(self):
        result = subprocess.run(
            [sys.executable, str(CHECKER)],
            check=False,
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
