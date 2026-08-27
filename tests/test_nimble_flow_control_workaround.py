"""Keep the ESP-IDF 5.5.3 NimBLE flow-control workaround enabled.

ESP-IDF 5.5.3 has a NimBLE host flow-control regression which can stop all
encrypted GATT traffic on ESP32-S3 and eventually produce a supervision
timeout. The host simulator cannot exercise the S3 controller, so this build
input test is the executable guard for the hardware-proven workaround.
"""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
KNOWN_BAD_FRAMEWORK = "3.50503.0"


def _read_lock() -> dict[str, str]:
  values: dict[str, str] = {}
  lock = ROOT / "tools" / "reproducible-build.lock"
  for line in lock.read_text(encoding="utf-8").splitlines():
    line = line.strip()
    if line and not line.startswith("#"):
      key, value = line.split("=", 1)
      values[key] = value
  return values


def _enabled(config: str, symbol: str) -> bool:
  return re.search(rf"^{re.escape(symbol)}=y$", config, re.MULTILINE) is not None


class NimbleFlowControlWorkaroundTest(unittest.TestCase):
  def test_known_bad_idf_disables_host_flow_control_on_every_ble_board(self):
    lock = _read_lock()
    if lock.get("framework") != KNOWN_BAD_FRAMEWORK:
      self.skipTest("the ESP-IDF framework is no longer the affected 5.5.3 package")

    configs = sorted(ROOT.glob("sdkconfig*"))
    self.assertTrue(configs, "checked-in sdkconfig files are required for firmware builds")
    for path in configs:
      config = path.read_text(encoding="utf-8")
      if not _enabled(config, "CONFIG_BT_NIMBLE_ENABLED"):
        continue
      self.assertFalse(
          _enabled(config, "CONFIG_BT_NIMBLE_HS_FLOW_CTRL"),
          f"{path.name} enables the IDF 5.5.3 NimBLE flow-control regression",
      )
      # IDF's sdkconfig.rename table leaves this legacy spelling in older
      # checked-in configurations. Keep it disabled too, so regeneration
      # cannot resurrect the feature through an alias.
      self.assertFalse(
          _enabled(config, "CONFIG_NIMBLE_HS_FLOW_CTRL"),
          f"{path.name} enables the legacy NimBLE flow-control alias",
      )


if __name__ == "__main__":
  unittest.main()
