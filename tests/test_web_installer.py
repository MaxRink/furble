import base64
import json
import os
from pathlib import Path
import re
import shutil
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
  page_script_errors,
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
PAGE_NODE_TEST = ROOT / "tests" / "web-installer" / "manifest.test.cjs"
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
  # The deploy also copies the page and every local script it loads, and
  # validate() now checks that each reference resolves.
  page = PAGE.read_text(encoding="utf-8")
  (root / "index.html").write_text(page, encoding="utf-8")
  for script in re.findall(r'<script[^>]*\bsrc="\./([^"]+)"', page):
    (root / script).write_bytes((INSTALLER / script).read_bytes())
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


PROVISION_JS = ROOT / "web-installer" / "provision.js"
PROVISION_FIXTURE = ROOT / "tests" / "web-installer" / "provision-fixture.json"
PROVISION_NODE_TEST = ROOT / "tests" / "web-installer" / "provision.test.cjs"


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


class WebInstallerPageTest(unittest.TestCase):
  """The page's own script, executed instead of read.

  A source grep passes with the line commented out, so the fail-closed paths are
  driven through the real `verifyFamily` in Node against a fake DOM and a fake
  fetch: unreachable manifest, 404, no builds, two builds, the wrong family, a
  radio with no or an empty `data-chip-family`, and a stale response for an
  older selection.
  """

  def test_the_page_script_fails_closed(self):
    node = shutil.which("node")
    if node is None:
      self.skipTest("node is not installed")
    result = subprocess.run(
      [node, str(PAGE_NODE_TEST)], capture_output=True, text=True, check=False)
    self.assertEqual(result.returncode, 0, result.stderr)
    self.assertIn("0 failure(s)", result.stderr)
    # A harness that stopped running its cases would still print that line.
    counts = re.findall(r"(\d+) checks, 0 failure", result.stderr)
    self.assertTrue(counts, result.stderr)
    self.assertGreaterEqual(int(counts[0]), 40, result.stderr)

  def test_every_published_radio_carries_its_family(self):
    # The regex in radio_families() requires the attribute, so a radio that
    # loses it also disappears from this mapping. The count pins the six.
    page = PAGE.read_text(encoding="utf-8")
    self.assertEqual(len(radio_families(page)), len(RELEASE_PLATFORMS))


class WebInstallerPageScriptTest(unittest.TestCase):
  """The Pages deploy copies each page script by hand, so validate the copies."""

  def test_a_missing_page_script_is_an_error(self):
    with tempfile.TemporaryDirectory() as directory:
      site = Path(directory)
      (site / "index.html").write_text(
        '<script src="./provision.js"></script>', encoding="utf-8"
      )
      errors = page_script_errors(site)
      self.assertEqual(errors, ["missing or empty page script provision.js"])

  def test_an_empty_page_script_is_an_error(self):
    with tempfile.TemporaryDirectory() as directory:
      site = Path(directory)
      (site / "index.html").write_text(
        '<script src="./provision.js"></script>', encoding="utf-8"
      )
      (site / "provision.js").write_text("", encoding="utf-8")
      self.assertEqual(page_script_errors(site), ["missing or empty page script provision.js"])

  def test_a_missing_index_is_an_error(self):
    with tempfile.TemporaryDirectory() as directory:
      self.assertEqual(
        page_script_errors(Path(directory)),
        ["index.html is missing from the site"],
      )

  def test_a_page_without_local_scripts_is_an_error(self):
    with tempfile.TemporaryDirectory() as directory:
      site = Path(directory)
      (site / "index.html").write_text(
        '<script src="https://example.com/button.js?module"></script>',
        encoding="utf-8",
      )
      self.assertEqual(page_script_errors(site), ["index.html loads no local scripts"])

  def test_the_real_page_references_only_present_scripts(self):
    site_files = {
      "index.html": (ROOT / "web-installer" / "index.html").read_text(encoding="utf-8"),
      "provision.js": PROVISION_JS.read_text(encoding="utf-8"),
      "pmic-preflight.js": (
        ROOT / "web-installer" / "pmic-preflight.js"
      ).read_text(encoding="utf-8"),
    }
    with tempfile.TemporaryDirectory() as directory:
      site = Path(directory)
      for name, text in site_files.items():
        (site / name).write_text(text, encoding="utf-8")
      self.assertEqual(page_script_errors(site), [])

  def test_the_page_loads_the_provisioning_encoder(self):
    index = (ROOT / "web-installer" / "index.html").read_text(encoding="utf-8")
    self.assertIn('src="./provision.js"', index)
    for element in (
      "provisionPanel",
      "provisionUnsupported",
      "provisionNeedsDebug",
      "provisionSupported",
      "provSsid",
      "provPsk",
      "provCompanion",
      "provTheme",
      "provTxPower",
      "provMqttGroup",
      "provMqttUri",
      "provMqttBase",
      "provMqttUser",
      "provMqttPass",
      "provSend",
      "provStatus",
      "provResult",
    ):
      self.assertIn(f'id="{element}"', index)
    # A release build has no console, so the panel must require the debug build
    # instead of pretending to provision.
    self.assertIn("el.debug.checked", index)


def javascript_integer(name, source):
  match = re.search(rf"\b{re.escape(name)}\s*[:=]\s*(\d+)", source)
  if match is None:
    raise AssertionError(f"{name} is not defined")
  return int(match.group(1))


class ProvisioningContractTest(unittest.TestCase):
  """Pin the browser constants to the firmware they mirror."""

  @classmethod
  def setUpClass(cls):
    cls.js = PROVISION_JS.read_text(encoding="utf-8")
    cls.header = (ROOT / "lib" / "furble" / "protocol" / "ProvisionTLV.h").read_text(
      encoding="utf-8"
    )
    cls.console = (ROOT / "src" / "FurbleConsole.cpp").read_text(encoding="utf-8")
    cls.settings = (ROOT / "src" / "FurbleSettings.cpp").read_text(encoding="utf-8")
    cls.settings_header = (ROOT / "include" / "FurbleSettings.h").read_text(
      encoding="utf-8"
    )
    cls.network_settings = (ROOT / "src" / "FurbleNetworkSettings.cpp").read_text(
      encoding="utf-8"
    )

  def test_field_limits_match_the_production_parser(self):
    limits = {
      "MAX_BLOB_BYTES": "blobBytes",
      "MAX_RECORDS": "records",
      "MAX_WIFI_SSID_BYTES": "ssidBytes",
      "MAX_WIFI_PSK_BYTES": "pskBytes",
      "MAX_COMPANION_PASSWORD_BYTES": "companionPasswordBytes",
      "MAX_MQTT_FIELD_BYTES": "mqttFieldBytes",
      "MAX_STRING_BYTES": "settingStringBytes",
    }
    for header_name, js_name in limits.items():
      header = re.search(rf"{header_name}\s*=\s*(\d+)", self.header)
      self.assertIsNotNone(header, f"{header_name} is defined in ProvisionTLV.h")
      self.assertEqual(
        int(header.group(1)),
        javascript_integer(js_name, self.js),
        f"{js_name} mirrors {header_name}",
      )

  def test_passphrase_range_matches_the_settings_validator(self):
    # WPA2-PSK and WPA3-SAE take 8 to 63 characters. The page and the firmware
    # must refuse the same range, because a 3-byte passphrase is stored and
    # reported as applied on a device that can never associate.
    minimum = re.search(r"WIFI_PSK_MIN_LENGTH\s*=\s*(\d+)", self.settings_header)
    maximum = re.search(r"WIFI_PSK_MAX_LENGTH\s*=\s*(\d+)", self.settings_header)
    self.assertIsNotNone(minimum, "FurbleSettings.h defines WIFI_PSK_MIN_LENGTH")
    self.assertIsNotNone(maximum, "FurbleSettings.h defines WIFI_PSK_MAX_LENGTH")
    self.assertEqual(int(minimum.group(1)), javascript_integer("pskMinBytes", self.js))
    self.assertEqual(int(maximum.group(1)), javascript_integer("pskBytes", self.js))
    self.assertEqual(8, int(minimum.group(1)), "the WPA2 and WPA3-SAE minimum is 8")

    # The firmware validator must use both bounds, and keep the empty value.
    validator = re.search(
      r"case WIFI_PSK:(.*?)case NTP_SERVER:", self.network_settings, re.S
    )
    self.assertIsNotNone(validator, "validNetworkString has a WIFI_PSK case")
    self.assertIn("value.empty()", validator.group(1))
    self.assertIn("WIFI_PSK_MIN_LENGTH", validator.group(1))
    self.assertIn("WIFI_PSK_MAX_LENGTH", validator.group(1))

    # The console tells the user the same range.
    self.assertIn("passphrase must be empty or 8 to 63 characters", self.console)

  def test_tx_power_bound_matches_the_settings_header(self):
    maximum = re.search(r"TX_POWER_MAX\s*=\s*(\d+)", self.settings_header)
    self.assertIsNotNone(maximum, "FurbleSettings.h defines TX_POWER_MAX")
    self.assertEqual(int(maximum.group(1)), javascript_integer("txPowerMax", self.js))
    provision = (ROOT / "src" / "FurbleProvision.cpp").read_text(encoding="utf-8")
    self.assertIn("TX power must be 0 (P3), 1 (P6) or 2 (P9)", provision)
    self.assertIn("Settings::TX_POWER_MAX", provision)

  def test_theme_set_matches_the_firmware_and_the_page(self):
    validator = re.search(
      r"bool Settings::validTheme\(const std::string &value\) \{(.*?)\}",
      self.network_settings,
      re.S,
    )
    self.assertIsNotNone(validator, "Settings::validTheme is defined")
    firmware = re.findall(r'value == "([^"]+)"', validator.group(1))
    self.assertEqual(firmware, ["Dark", "Default", "Mono Furble"])

    js = re.search(r"var THEMES = \[(.*?)\];", self.js)
    self.assertIsNotNone(js, "provision.js lists its themes")
    self.assertEqual(re.findall(r'"([^"]+)"', js.group(1)), firmware)

    page = (ROOT / "web-installer" / "index.html").read_text(encoding="utf-8")
    select = re.search(r'id="provTheme">(.*?)</select>', page, re.S)
    self.assertIsNotNone(select, "the page has a theme select")
    options = [
      value for value in re.findall(r'<option value="([^"]*)"', select.group(1)) if value
    ]
    self.assertEqual(options, firmware, "the page offers exactly the accepted themes")

  def test_command_limit_is_one_below_the_console_buffer(self):
    max_line = re.search(r"constexpr size_t MAX_LINE = (\d+);", self.console)
    self.assertIsNotNone(max_line, "FurbleConsole.cpp defines MAX_LINE")
    self.assertEqual(
      int(max_line.group(1)) - 1,
      javascript_integer("MAX_COMMAND_CHARS", self.js),
      "the browser limit is the longest line esp_console_run() keeps",
    )
    self.assertIn(
      "line longer than %u bytes, command not run",
      self.console,
      "the console refuses an oversized line instead of clipping it",
    )

  def test_setting_wire_ids_match_the_settings_table(self):
    self.assertEqual(
      3, javascript_integer("THEME", self.js), "theme is wire id 3"
    )
    self.assertEqual(
      4, javascript_integer("TX_POWER", self.js), "TX power is wire id 4"
    )
    self.assertIn('{THEME, 3, "Theme"', self.settings)
    self.assertIn('{TX_POWER, 4, "TX Power"', self.settings)

  def test_mqtt_fields_are_omitted_on_boards_without_the_backend(self):
    page = (ROOT / "web-installer" / "index.html").read_text(encoding="utf-8")
    radios = set(re.findall(r'name="type" value="([^"]+)"', page))
    self.assertTrue(radios, "the page lists the supported boards")

    mqtt_envs = set()
    current = None
    for line in (ROOT / "platformio.ini").read_text(encoding="utf-8").splitlines():
      stripped = line.strip()
      if stripped.startswith("[env:") and stripped.endswith("]"):
        current = stripped[len("[env:") : -1]
      elif current and "-DFURBLE_MQTT=1" in stripped and not current.endswith("-debug"):
        mqtt_envs.add(current)

    block = re.search(r"BOARD_HAS_MQTT = \{(.*?)\};", PROVISION_JS.read_text(encoding="utf-8"), re.S)
    self.assertIsNotNone(block, "provision.js declares its board capability map")
    js_mqtt = set(re.findall(r'"([^"]+)": true', block.group(1)))

    self.assertEqual(
      js_mqtt,
      mqtt_envs & radios,
      "the MQTT boards in provision.js are the MQTT builds the page offers",
    )


class ProvisioningFixtureTest(unittest.TestCase):
  """The fixture is one bundle that both codecs must reproduce."""

  def test_the_fixture_derives_its_base64_and_command_length(self):
    document = json.loads(PROVISION_FIXTURE.read_text(encoding="utf-8"))
    payload = bytes.fromhex(document["expectedHex"])
    self.assertEqual(len(payload), document["expectedByteCount"])
    self.assertEqual(
      base64.b64encode(payload).decode("ascii"),
      document["expectedBase64"],
      "expectedBase64 is the base64 of expectedHex",
    )
    self.assertEqual(
      len("provision base64:") + len(document["expectedBase64"]),
      document["expectedCommandChars"],
      "expectedCommandChars is the console line length",
    )

  def test_the_fixture_fields_are_the_bundle_the_firmware_test_builds(self):
    document = json.loads(PROVISION_FIXTURE.read_text(encoding="utf-8"))
    fields = document["fields"]
    source = (ROOT / "tests" / "host" / "provision_tlv_test.cpp").read_text(encoding="utf-8")
    for value in (
      fields["wifiSsid"],
      fields["wifiPsk"],
      fields["companionPassword"],
      fields["mqttUri"],
      fields["mqttUsername"],
      fields["mqttPassword"],
      fields["mqttBaseTopic"],
    ):
      self.assertIn(f'text("{value}")', source, f"the firmware test builds {value}")
    self.assertIn('theme.value = text("Dark")', source)
    self.assertIn("power.value = {2};", source)


class ProvisioningBrowserTest(unittest.TestCase):
  """Run the browser encoder and parser tests under Node."""

  def test_the_browser_encoder_and_parser_pass(self):
    node = shutil.which("node")
    if node is None:
      self.skipTest("node is not installed")
    result = subprocess.run(
      [node, str(PROVISION_NODE_TEST)],
      capture_output=True,
      text=True,
      timeout=180,
      check=False,
    )
    self.assertEqual(result.returncode, 0, result.stderr)
    self.assertIn("0 failure(s)", result.stderr)


if __name__ == "__main__":
  unittest.main()
