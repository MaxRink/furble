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


if __name__ == "__main__":
    unittest.main()
