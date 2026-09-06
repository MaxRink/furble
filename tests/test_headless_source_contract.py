"""Guard the ESP-IDF source split between display and headless images."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class HeadlessSourceContractTest(unittest.TestCase):
    def test_lvgl_sources_are_display_only_and_not_duplicated(self):
        text = (ROOT / "src/CMakeLists.txt").read_text(encoding="utf-8")
        headless, display = text.split("if(NOT FURBLE_NO_DISPLAY)", 1)
        display, _ = display.split("endif()", 1)
        display_sources = (
            "FurbleSpinValue.cpp",
            "FurbleUI.cpp",
            "FurbleUIAudit.cpp",
            "FurbleUIBulb.cpp",
            "FurbleUIGesture.cpp",
            "FurbleUIIntervalometer.cpp",
        )
        for source in display_sources:
            self.assertNotIn(source, headless, source)
            self.assertEqual(display.count(source), 1, source)


if __name__ == "__main__":
    unittest.main()
