import unittest

import importlib.util
import json
from pathlib import Path

from tools.verify_web_installer import (
  SINGLE_FACTORY_MANIFESTS,
  build_family_error,
  expected_manifests,
  flash_offset_error,
)

ROOT = Path(__file__).resolve().parents[1]


class WebInstallerOffsetTest(unittest.TestCase):
  def test_core_debug_manifest_is_not_expected(self):
    manifests = expected_manifests()
    self.assertEqual(len(manifests), 11)
    self.assertNotIn("manifest_m5stack-core-debug.json", manifests)

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

  def test_single_factory_map_has_no_ota_data_part(self):
    build = {
      "chipFamily": "ESP32",
      "parts": [{"offset": 0x1000}, {"offset": 0x8000}, {"offset": 0x20000}],
    }
    self.assertIsNone(flash_offset_error(build, single_factory=True))
    self.assertIsNotNone(flash_offset_error(build))
    four = {
      "chipFamily": "ESP32",
      "parts": [{"offset": 0x1000}, {"offset": 0x8000}, {"offset": 0xF000},
                {"offset": 0x20000}],
    }
    self.assertIsNotNone(flash_offset_error(four, single_factory=True))

  def test_generated_single_factory_manifests_pass(self):
    spec = importlib.util.spec_from_file_location(
      "generate_manifest", ROOT / "web-installer" / "generate-manifest.py")
    if spec is None or spec.loader is None:
      raise RuntimeError("cannot load web-installer/generate-manifest.py")
    generator = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(generator)
    for name in SINGLE_FACTORY_MANIFESTS:
      self.assertIn(name, expected_manifests())
      platform = name.removeprefix("manifest_").removesuffix(".json")
      text = generator.render(str(ROOT / "web-installer" / "manifest.tmpl"), platform,
                              "v1", "firmware/x", ota_data=False)
      for build in json.loads(text)["builds"]:
        self.assertIsNone(flash_offset_error(build, single_factory=True), name)

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
