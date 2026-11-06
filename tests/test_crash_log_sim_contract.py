"""Source contract for the simulator crash-record concurrency state."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "sim" / "CrashLogSim.cpp"


class CrashLogSimContractTest(unittest.TestCase):
    def test_injected_flag_is_atomic(self):
        text = SOURCE.read_text(encoding="utf-8")
        self.assertIn("std::atomic<bool> s_Injected", text)
        self.assertNotIn("bool s_Injected = false", text)

    def test_printing_guard_is_atomic(self):
        text = SOURCE.read_text(encoding="utf-8")
        self.assertIn("std::atomic<bool> s_Printing", text)


if __name__ == "__main__":
    unittest.main()
