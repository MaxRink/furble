import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest
from importlib.util import module_from_spec, spec_from_file_location

from tools.verify_web_installer import (
  RELEASE_PLATFORMS,
  build_family_error,
  expected_manifests,
  flash_offset_error,
  validate,
)
from tools.web_installer_layout import (
  bootloader_offset,
  chip_family_for_platform,
  flash_offsets,
  idf_target,
)


ROOT = Path(__file__).resolve().parents[1]
INSTALLER = ROOT / "web-installer"
GENERATOR_PATH = INSTALLER / "generate-manifest.py"
TEMPLATE = INSTALLER / "manifest.tmpl"
PAGE = INSTALLER / "index.html"
VERSION = "v9.9.9"
ASSET_BASE_URL = "firmware/abc"

SPEC = spec_from_file_location("generate_manifest", GENERATOR_PATH)
assert SPEC is not None and SPEC.loader is not None
GENERATOR = module_from_spec(SPEC)
sys.modules[SPEC.name] = GENERATOR
SPEC.loader.exec_module(GENERATOR)

# One ESP32 environment and one ESP32-S3 environment per family, chosen from the
# six published boards. The bootloader offset is the whole point: ESP32 loads it
# at 0x1000 and ESP32-S3 at 0x0.
ESP32_PLATFORMS = ("m5stick-c", "m5stack-core")
ESP32_S3_PLATFORMS = ("m5stick-s3", "waveshare-s3-eth")

# What the manifest generator produced before plan 372: both families in every
# manifest, each pointing at the same single-family binaries. The offsets are
# what a user of the other family would write.
LEGACY_BUILDS = [
  {
    "chipFamily": "ESP32",
    "parts": [
      {"path": "firmware/abc/furble-bootloader-m5stick-s3-v9.9.9.bin", "offset": 4096},
      {"path": "firmware/abc/furble-partitions-m5stick-s3-v9.9.9.bin", "offset": 32768},
      {"path": "firmware/abc/furble-ota_data_initial-m5stick-s3-v9.9.9.bin", "offset": 61440},
      {"path": "firmware/abc/furble-m5stick-s3-v9.9.9.bin", "offset": 131072},
    ],
  },
  {
    "chipFamily": "ESP32-S3",
    "parts": [
      {"path": "firmware/abc/furble-bootloader-m5stick-s3-v9.9.9.bin", "offset": 0},
      {"path": "firmware/abc/furble-partitions-m5stick-s3-v9.9.9.bin", "offset": 32768},
      {"path": "firmware/abc/furble-ota_data_initial-m5stick-s3-v9.9.9.bin", "offset": 61440},
      {"path": "firmware/abc/furble-m5stick-s3-v9.9.9.bin", "offset": 131072},
    ],
  },
]


def render(platform):
  text = GENERATOR.render(str(TEMPLATE), platform, VERSION, ASSET_BASE_URL)
  return json.loads(text)


def write_site(root, platforms, legacy_platform=None):
  """Write a deploy tree the way pages.yml assembles one."""
  for platform in platforms:
    document = render(platform)
    if platform == legacy_platform:
      document["builds"] = LEGACY_BUILDS
    (root / f"manifest_{platform}.json").write_text(
      json.dumps(document), encoding="utf-8")
    firmware = root / "firmware" / "abc"
    firmware.mkdir(parents=True, exist_ok=True)
    for build in document["builds"]:
      for part in build["parts"]:
        (root / part["path"]).write_bytes(b"bin")
  return root


def expected_manifests_platforms():
  return [
    name[len("manifest_"):-len(".json")] for name in expected_manifests()
  ]


def radio_families(page):
  """Yield (board value, declared chip family) for every installer radio."""
  pattern = re.compile(
    r'<input type="radio" name="type" value="([^"]+)"'
    r' data-chip-family="([^"]+)"/>'
  )
  return pattern.findall(page)


class WebInstallerChipFamilyTest(unittest.TestCase):
  """Plan 372: one chipFamily per manifest, derived from the board it targets."""

  def assert_single_family(self, platform, family, bootloader):
    document = render(platform)
    builds = document["builds"]
    self.assertEqual(len(builds), 1, f"{platform} declares {len(builds)} builds")
    for build in builds:
      self.assertEqual(build["chipFamily"], family, platform)
      offsets = tuple(part["offset"] for part in build["parts"])
      self.assertEqual(offsets, flash_offsets(family), platform)
      self.assertEqual(offsets[0], bootloader, platform)
      self.assertIn("furble-bootloader-", build["parts"][0]["path"])

  def test_esp32_environment_declares_only_the_esp32_build(self):
    for platform in ESP32_PLATFORMS:
      with self.subTest(platform=platform):
        self.assertEqual(idf_target(platform), "esp32")
        self.assert_single_family(platform, "ESP32", 0x1000)

  def test_s3_environment_declares_only_the_esp32_s3_build(self):
    for platform in ESP32_S3_PLATFORMS:
      with self.subTest(platform=platform):
        self.assertEqual(idf_target(platform), "esp32s3")
        self.assert_single_family(platform, "ESP32-S3", 0x0)

  def test_families_come_from_the_environment_not_a_template_constant(self):
    # If the generator kept a board list of its own, both of these would render
    # the same manifest. They must differ in exactly the family and the offset.
    esp32 = render("m5stick-c")["builds"]
    s3 = render("m5stick-s3")["builds"]
    self.assertNotEqual(esp32[0]["chipFamily"], s3[0]["chipFamily"])
    self.assertEqual(esp32[0]["parts"][0]["offset"], 0x1000)
    self.assertEqual(s3[0]["parts"][0]["offset"], 0x0)
    self.assertEqual(
      tuple(part["offset"] for part in esp32[0]["parts"][1:]),
      tuple(part["offset"] for part in s3[0]["parts"][1:]),
    )

  def test_every_published_manifest_declares_one_matching_family(self):
    published = sorted(
      name[len("manifest_"):-len(".json")]
      for name in expected_manifests()
    )
    self.assertEqual(len(published), 11)
    for platform in published:
      with self.subTest(platform=platform):
        family = chip_family_for_platform(platform)
        self.assert_single_family(platform, family, bootloader_offset(family))

  def test_debug_variant_shares_the_release_family(self):
    for platform in ("m5stick-c", "m5stick-s3"):
      with self.subTest(platform=platform):
        release = render(platform)["builds"]
        debug = render(f"{platform}-debug")["builds"]
        self.assertEqual(debug[0]["chipFamily"], release[0]["chipFamily"])
        self.assertEqual(
          [part["offset"] for part in debug[0]["parts"]],
          [part["offset"] for part in release[0]["parts"]],
        )

  def test_two_chip_family_manifest_is_rejected(self):
    # The exact pre-fix output. This is the regression that would have caught
    # the defect: the old template was accepted by the old validator.
    self.assertIn("exactly one", build_family_error(LEGACY_BUILDS))
    self.assertIsNotNone(
      flash_offset_error(LEGACY_BUILDS[0], "ESP32-S3"),
      "an ESP32 entry in an S3 manifest must be rejected",
    )
    self.assertIsNone(build_family_error(render("m5stick-s3")["builds"]))

  def test_pages_validation_accepts_the_generated_site(self):
    with tempfile.TemporaryDirectory() as directory:
      site = write_site(Path(directory), sorted(expected_manifests_platforms()))
      validate(site)

  def test_pages_validation_rejects_a_legacy_two_family_manifest(self):
    with tempfile.TemporaryDirectory() as directory:
      site = write_site(
        Path(directory), sorted(expected_manifests_platforms()),
        legacy_platform="m5stick-s3")
      with self.assertRaises(SystemExit) as raised:
        validate(site)
      message = str(raised.exception)
      self.assertIn("exactly one", message)
      self.assertIn("manifest_m5stick-s3.json", message)

  def test_unknown_environment_fails_closed(self):
    result = subprocess.run(
      [sys.executable, "generate-manifest.py"],
      cwd=INSTALLER,
      env={
        **os.environ,
        "PLATFORM": "not-a-board",
        "VERSION": VERSION,
        "ASSET_BASE_URL": ASSET_BASE_URL,
      },
      capture_output=True,
      text=True,
      check=False,
    )
    self.assertNotEqual(result.returncode, 0)
    self.assertEqual(result.stdout, "")
    self.assertIn("not-a-board", result.stderr)

  def test_cli_writes_the_manifest_the_page_requests(self):
    result = subprocess.run(
      [sys.executable, "generate-manifest.py"],
      cwd=INSTALLER,
      env={
        **os.environ,
        "PLATFORM": "m5stick-s3",
        "VERSION": VERSION,
        "ASSET_BASE_URL": ASSET_BASE_URL,
      },
      capture_output=True,
      text=True,
      check=True,
    )
    document = json.loads(result.stdout)
    self.assertEqual(document["builds"][0]["chipFamily"], "ESP32-S3")
    self.assertEqual(document["builds"][0]["parts"][0]["offset"], 0)

  def test_page_labels_every_published_board_with_its_family(self):
    page = PAGE.read_text(encoding="utf-8")
    radios = dict(radio_families(page))
    self.assertEqual(sorted(radios), sorted(RELEASE_PLATFORMS))
    for platform, family in radios.items():
      with self.subTest(platform=platform):
        self.assertEqual(family, chip_family_for_platform(platform))

  def test_page_keeps_the_published_manifest_url_contract(self):
    page = PAGE.read_text(encoding="utf-8")
    self.assertIn("`./manifest_${radio.value}${variant}.json`", page)


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
