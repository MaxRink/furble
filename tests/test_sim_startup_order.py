"""Keep simulator boot inputs ahead of the platform consumer."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def function_body(source, signature, terminator=None):
  start = source.index(signature)
  end = source.index(terminator, start) if terminator is not None else len(source)
  return source[start:end]


class SimulatorStartupOrderTest(unittest.TestCase):
  def setUp(self):
    source = (ROOT / "sim" / "main.cpp").read_text()
    self.simulator = function_body(source, "int runSimulator()", "\n}\n\n}  // namespace")
    self.main = function_body(source, "int main(int argc, char **argv)")

  def test_settings_and_scenario_precede_platform(self):
    calls = [
        "Settings::init();",
        "Sim::applyScenarioSettings();",
        "Platform::init();",
    ]
    positions = [self.simulator.index(call) for call in calls]
    self.assertEqual(positions, sorted(positions))

  def test_panel_ready_and_profiler_boundaries_stay_after_platform(self):
    calls = [
        "Platform::init();",
        "panelReady.store(true, std::memory_order_release);",
        "Sim::startProfiler();",
    ]
    positions = [self.simulator.index(call) for call in calls]
    self.assertEqual(positions, sorted(positions))

  def test_platform_records_the_consumed_boot_settings(self):
    calls = [
        "Settings::load<bool>(Settings::IMU)",
        "Settings::load<uint8_t>(Settings::FB_OUTPUT)",
        "captureBootSettings(bootSettingsSnapshot);",
    ]
    platform = (ROOT / "sim" / "FurblePlatformSim.cpp").read_text()
    positions = [platform.index(call) for call in calls]
    self.assertEqual(positions, sorted(positions))

    driver = (ROOT / "sim" / "driver.cpp").read_text()
    self.assertIn('key == "boot_settings_imu"', driver)
    self.assertIn('key == "boot_settings_fb_output"', driver)

  def test_preferences_and_sdl_setup_stay_before_thread_start(self):
    calls = [
        "Furble::Sim::preparePreferences();",
        "lgfx::Panel_sdl::setup()",
        "std::thread simulator",
    ]
    positions = [self.main.index(call) for call in calls]
    self.assertEqual(positions, sorted(positions))


if __name__ == "__main__":
  unittest.main()
