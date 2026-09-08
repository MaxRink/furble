"""Keep settings restart actions on the portable platform abstraction."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class UiRestartPathTest(unittest.TestCase):
    def test_sensors_restart_uses_platform(self):
        source = (ROOT / "src" / "FurbleUI.cpp").read_text()
        start = source.index("void UI::addSensorsMenu")
        end = source.index("void UI::addGPSOptionMenu", start)
        sensors = source[start:end]
        self.assertIn("Platform::getInstance().restart()", sensors)
        self.assertNotIn("esp_restart()", sensors)

    def test_environment_overrides_are_fresh_boot_only(self):
        main = (ROOT / "sim" / "main.cpp").read_text()
        driver = (ROOT / "sim" / "driver.cpp").read_text()
        self.assertEqual(main.count("!Sim::resumedDeviceBoot()"), 2)
        self.assertIn('setenv(RESTART_BOOT_ENV, "1", 1)', driver)
        self.assertIn("unsetenv(RESTART_BOOT_ENV)", driver)


if __name__ == "__main__":
    unittest.main()
