import unittest

from tools.verify_web_installer import build_family_error, flash_offset_error


class WebInstallerOffsetTest(unittest.TestCase):
  def test_esp32_offsets_match_bootloader_partition_ota_and_app_layout(self):
    build = {
      "chipFamily": "ESP32",
      "parts": [
        {"offset": 0x1000},
        {"offset": 0x8000},
        {"offset": 0xF000},
        {"offset": 0x20000},
      ],
    }
    self.assertIsNone(flash_offset_error(build))

  def test_s3_bootloader_offset_differs_but_app_offset_is_shared(self):
    build = {
      "chipFamily": "ESP32-S3",
      "parts": [
        {"offset": 0},
        {"offset": 0x8000},
        {"offset": 0xF000},
        {"offset": 0x20000},
      ],
    }
    self.assertIsNone(flash_offset_error(build))

  def test_wrong_app_offset_is_rejected(self):
    build = {
      "chipFamily": "ESP32-S3",
      "parts": [
        {"offset": 0},
        {"offset": 0x8000},
        {"offset": 0xF000},
        {"offset": 0x10000},
      ],
    }
    self.assertIn("0x20000", flash_offset_error(build))

  def test_duplicate_chip_family_is_rejected(self):
    builds = [
      {"chipFamily": "ESP32", "parts": []},
      {"chipFamily": "ESP32", "parts": []},
    ]
    self.assertIn("exactly one", build_family_error(builds))

  def test_unknown_chip_family_is_rejected(self):
    build = {
      "chipFamily": "ESP32-C6",
      "parts": [
        {"offset": 0},
        {"offset": 0x8000},
        {"offset": 0xF000},
        {"offset": 0x20000},
      ],
    }
    self.assertIn("unsupported chip family", flash_offset_error(build))


if __name__ == "__main__":
  unittest.main()
