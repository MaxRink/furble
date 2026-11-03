"""Mutation tests for the settings default and documentation contract scanner."""

from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
CHECKER_PATH = ROOT / "tools" / "check_settings_contracts.py"
SPEC = spec_from_file_location("check_settings_contracts", CHECKER_PATH)
assert SPEC is not None and SPEC.loader is not None
CHECKER = module_from_spec(SPEC)
sys.modules[SPEC.name] = CHECKER
SPEC.loader.exec_module(CHECKER)


SOURCE = """\
#include "FurbleSettings.h"

namespace Furble {

const std::unordered_map<Settings::type_t, Settings::setting_t> Settings::m_Setting = {
    {BRIGHTNESS,         {BRIGHTNESS, 1, "Brightness", "brightness", "M5ez"}       },
    {INACTIVITY,         {INACTIVITY, 2, "Inactivity", "inactivity", "M5ez"}       },
    {DISPLAY_OFF,        {DISPLAY_OFF, 24, "Screen off", "display_off", FURBLE_STR} },
    {SLEEP_CONN,         {SLEEP_CONN, 20, "Sleep while connected", "sleep_conn", FURBLE_STR}},
    {AUTO_OFF,           {AUTO_OFF, 37, "Auto off", "auto_off", FURBLE_STR}         },
    {GPS_BAUD,           {GPS_BAUD, 6, "GPS Baud", "gps_baud", FURBLE_STR}          },
    {GPS_NMEA,           {GPS_NMEA, 14, "GPS Sentences", "gps_nmea", FURBLE_STR}    },
    {CPU_FREQ,           {CPU_FREQ, 17, "CPU Speed", "cpu_freq", FURBLE_STR}        },
    {COMPANION_PASSWORD,
     {COMPANION_PASSWORD, 47, "Companion password", "companion_pw", FURBLE_STR}     },
    {MULTISELECT,        {MULTISELECT, 0, "Multi-Select", "multiselect", FURBLE_STR}},
#if defined(FURBLE_M5STICKS3)
    {WATCHDOG,           {WATCHDOG, 23, "Watchdog", "watchdog", FURBLE_STR}         },
#endif
};

void Settings::init(void) {
  for (const auto &it : m_Setting) {
    auto &setting = it.second;
    if (setting.type == COMPANION_PASSWORD) {
      continue;
    }
    Preferences prefs;
    prefs.begin(setting.nvs_namespace, true);
    bool exists = prefs.isKey(setting.key);
    prefs.end();

    if (!exists) {
      switch (setting.type) {
        case BRIGHTNESS:
          save<uint8_t>(setting.type, 128);
          break;
        case INACTIVITY:
        case DISPLAY_OFF:
        case AUTO_OFF:
          save<uint8_t>(setting.type, 0);
          break;
        case SLEEP_CONN:
        case GPS_NMEA:
          save<bool>(setting.type, false);
          break;
        case GPS_BAUD:
          save<uint32_t>(setting.type, BAUD_9600);
          break;
        case CPU_FREQ:
          save<uint8_t>(setting.type, CPU_FREQ_DEFAULT);
          break;
        case COMPANION_PASSWORD:
          break;
        case MULTISELECT:
        {
          multiselect_t selection = {};
          save<multiselect_t>(setting.type, selection);
        } break;
#if defined(FURBLE_M5STICKS3)
        case WATCHDOG:
          save<bool>(setting.type, true);
          break;
#endif
      }
    }
  }
}

}  // namespace Furble
"""

HEADER = """\
#ifndef FURBLE_SETTINGS_H
#define FURBLE_SETTINGS_H

#include <cstdint>

namespace Furble {

class Settings {
 public:
  typedef enum {
    BRIGHTNESS,
    INACTIVITY,
    DISPLAY_OFF,
    SLEEP_CONN,
    AUTO_OFF,
    GPS_BAUD,
    GPS_NMEA,
    CPU_FREQ,
    COMPANION_PASSWORD,
    MULTISELECT,
#if defined(FURBLE_M5STICKS3)
    WATCHDOG,
#endif
  } type_t;

  static constexpr uint32_t BAUD_9600 = 9600;
  static constexpr uint8_t CPU_FREQ_DEFAULT = 160;
};

}  // namespace Furble

#endif
"""

CMAKE = """\
add_executable(settings_nvs_roundtrip_test
               settings_nvs_roundtrip_test.cpp
               ${FURBLE_ROOT}/src/FurbleSettings.cpp)
target_compile_definitions(settings_nvs_roundtrip_test PRIVATE FURBLE_M5STICKS3)

add_executable(settings_plain_test
               settings_plain_test.cpp
               ${FURBLE_ROOT}/src/FurbleSettings.cpp)
"""

DOCS = """\
# Settings reference

## Part 1: Settings reference

| Setting | Default | Values | Applies | Notes |
| :--- | :--- | :--- | :--- | :--- |
| Brightness | 128 | slider | Now | Saved live. |
| Inactivity timeout | Never | Never, 30 secs | Now | Dims the panel. |
| Screen off | Dim | Dim, Off | Restart | Panel off mode. |
| Sleep while connected | Off | On, Off | Next connection | Stick only. |
| Auto off | Never | Never, 5 mins | Now | Idle power off. |
| GPS Baud | 9600 | Auto, 9600, 115200 | Now | Receiver rate. |
| Sentences | Default | Default, RMC+GGA | Now | NMEA pruning. |
| CPU speed | 160 MHz | 80 MHz, 160 MHz, 240 MHz | Now | Higher is faster. |

Some rows are pages rather than settings.
"""

LEDGER = """\
# include/

### Companion wire id reservations

| Owner | Setting keys | Wire ids |
| --- | --- | --- |
| Master | shipped settings | 1-2, 6, 14, 17, 20, 24, 37, 47 |
| Master (conditional) | conditional | 23 (WATCHDOG on FURBLE_M5STICKS3) |
| Historical claims | compatibility audit required | 42, 45 |
| Free | available after rechecking open heads | 48-50 |
| Open PRs #317, #318, #319 | no setting claim | none |

Take the next free id only after checking every open PR head.
"""

README = """\
# furble

The Brightness default is 128.
"""

TESTS = """\
// Synthetic settings test used by the contract scanner fixtures.
"""


def fixture_files(**overrides: str) -> dict[str, str]:
  files = {
      "src/FurbleSettings.cpp": SOURCE,
      "include/FurbleSettings.h": HEADER,
      "tests/host/CMakeLists.txt": CMAKE,
      "tests/host/settings_nvs_roundtrip_test.cpp": TESTS,
      "tests/host/settings_plain_test.cpp": TESTS,
      "docs/settings-and-controls.md": DOCS,
      "docs/wiki/Settings-Reference.md": DOCS,
      "include/CLAUDE.md": LEDGER,
      "README.md": README,
  }
  files.update(overrides)
  return files


def write_fixture(root: Path, **overrides: str) -> None:
  for relative, content in fixture_files(**overrides).items():
    path = root / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


def run_git(root: Path, *arguments: str) -> subprocess.CompletedProcess[str]:
  return subprocess.run(
      ["git", "-C", str(root), *arguments],
      check=False,
      capture_output=True,
      text=True,
  )


class SettingsContractsTest(unittest.TestCase):
  def setUp(self) -> None:
    self.temporary = tempfile.TemporaryDirectory()
    self.root = Path(self.temporary.name)
    write_fixture(self.root)

  def tearDown(self) -> None:
    self.temporary.cleanup()

  def diagnostics(self, *arguments: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(CHECKER_PATH), "--root", str(self.root), *arguments],
        check=False,
        capture_output=True,
        text=True,
    )

  def direct_report(self):
    return CHECKER.check_repository(self.root)

  def messages(self, report, severity: str) -> list[str]:
    return [
        diagnostic.render()
        for diagnostic in report.diagnostics
        if diagnostic.severity == severity
    ]

  def test_clean_fixture_has_no_failures(self):
    report = self.direct_report()
    self.assertEqual(self.messages(report, "FAIL"), [])
    self.assertEqual(report.stats["table_rows"], 11)
    self.assertEqual(report.stats["default_symbols"], 10)
    self.assertEqual(report.stats["branches_checked"], 1)
    self.assertEqual(report.stats["doc_defaults_compared"], 16)

    result = self.diagnostics()
    self.assertEqual(result.returncode, 0, result.stderr)
    self.assertIn("summary: 0 FAIL", result.stdout)

  def test_namespaced_defaults_header_is_resolved(self):
    defaults = """\
#ifndef FURBLE_SETTINGS_DEFAULTS_H
#define FURBLE_SETTINGS_DEFAULTS_H

#include <cstdint>

namespace Furble {
namespace SettingsDefaults {

constexpr uint8_t INACTIVITY = 2;

}  // namespace SettingsDefaults
}  // namespace Furble

#endif
"""
    source = SOURCE.replace(
        """        case INACTIVITY:
        case DISPLAY_OFF:
        case AUTO_OFF:
          save<uint8_t>(setting.type, 0);
          break;""",
        """        case INACTIVITY:
          save<uint8_t>(setting.type, SettingsDefaults::INACTIVITY);
          break;
        case DISPLAY_OFF:
        case AUTO_OFF:
          save<uint8_t>(setting.type, 0);
          break;""",
    )
    docs = DOCS.replace(
        "| Inactivity timeout | Never |", "| Inactivity timeout | 60 secs |"
    )
    write_fixture(
        self.root,
        **{
            "include/FurbleSettingsDefaults.h": defaults,
            "src/FurbleSettings.cpp": source,
            "docs/settings-and-controls.md": docs,
            "docs/wiki/Settings-Reference.md": docs,
        },
    )
    report = self.direct_report()
    self.assertEqual(self.messages(report, "FAIL"), [])

  def test_missing_default_case_is_failed(self):
    source = SOURCE.replace(
        """        case CPU_FREQ:
          save<uint8_t>(setting.type, CPU_FREQ_DEFAULT);
          break;
""",
        "",
    )
    write_fixture(self.root, **{"src/FurbleSettings.cpp": source})
    report = self.direct_report()
    failures = self.messages(report, "FAIL")
    table_line = next(
        line
        for line, text in enumerate(SOURCE.splitlines(), 1)
        if "{CPU_FREQ," in text
    )
    self.assertTrue(
        any(
            f"src/FurbleSettings.cpp:{table_line}:" in message
            and "CPU_FREQ" in message
            and "has no default case" in message
            for message in failures
        ),
        failures,
    )

  def test_case_without_save_is_failed(self):
    source = SOURCE.replace(
        """        case SLEEP_CONN:
        case GPS_NMEA:
          save<bool>(setting.type, false);
          break;
""",
        """        case SLEEP_CONN:
        case GPS_NMEA:
          break;
""",
    )
    write_fixture(self.root, **{"src/FurbleSettings.cpp": source})
    report = self.direct_report()
    failures = self.messages(report, "FAIL")
    self.assertTrue(
        any("case SLEEP_CONN does not save a default" in message for message in failures),
        failures,
    )
    self.assertTrue(any("case GPS_NMEA does not save a default" in message for message in failures))

  def test_companion_password_and_off_wire_defaults_are_excluded(self):
    source = SOURCE.replace(
        """        case COMPANION_PASSWORD:
          break;
""",
        "",
    ).replace(
        """        case MULTISELECT:
        {
          multiselect_t selection = {};
          save<multiselect_t>(setting.type, selection);
        } break;
""",
        "",
    )
    write_fixture(self.root, **{"src/FurbleSettings.cpp": source})
    report = self.direct_report()
    failures = self.messages(report, "FAIL")
    self.assertFalse(any("COMPANION_PASSWORD" in message for message in failures), failures)
    warnings = self.messages(report, "WARN")
    self.assertTrue(any("off-wire setting MULTISELECT" in message for message in warnings))
    self.assertFalse(any("MULTISELECT is in the settings table" in message for message in failures))

  def test_missing_host_define_fails_branch_coverage(self):
    cmake = CMAKE.replace(
        "target_compile_definitions(settings_nvs_roundtrip_test PRIVATE FURBLE_M5STICKS3)\n",
        "",
    )
    write_fixture(self.root, **{"tests/host/CMakeLists.txt": cmake})
    report = self.direct_report()
    failures = self.messages(report, "FAIL")
    self.assertTrue(
        any(
            "no host test target covers default branch" in message
            and "FURBLE_M5STICKS3 defined" in message
            for message in failures
        ),
        failures,
    )
    source_lines = [
        line
        for line, text in enumerate(SOURCE.splitlines(), 1)
        if text.strip() == "#if defined(FURBLE_M5STICKS3)"
    ]
    self.assertTrue(
        any(
            any(f"src/FurbleSettings.cpp:{line}:" in message for line in source_lines)
            for message in failures
        ),
        failures,
    )

  def test_explicit_test_guard_satisfies_branch_coverage(self):
    cmake = CMAKE.replace(
        "target_compile_definitions(settings_nvs_roundtrip_test PRIVATE FURBLE_M5STICKS3)\n",
        "",
    )
    guard = """\
#include "FurbleSettings.h"
#if defined(FURBLE_M5STICKS3)
static_assert(Settings::WATCHDOG > 0, "watchdog is compiled for the S3");
#endif
"""
    write_fixture(
        self.root,
        **{
            "tests/host/CMakeLists.txt": cmake,
            "tests/host/settings_branch_test.cpp": guard,
        },
    )
    report = self.direct_report()
    self.assertEqual(self.messages(report, "FAIL"), [])

  def test_documentation_default_mismatch_is_failed(self):
    docs = DOCS.replace("| Brightness | 128 |", "| Brightness | 100 |")
    write_fixture(self.root, **{"docs/settings-and-controls.md": docs})
    report = self.direct_report()
    failures = self.messages(report, "FAIL")
    self.assertTrue(
        any(
            "docs/settings-and-controls.md:7:" in message
            and "BRIGHTNESS documents default '100'" in message
            and "source default is '128'" in message
            for message in failures
        ),
        failures,
    )

  def test_unparsable_documented_default_warns_instead_of_passing(self):
    docs = DOCS.replace("| Brightness | 128 |", "| Brightness | TBD |")
    write_fixture(self.root, **{"docs/settings-and-controls.md": docs})
    report = self.direct_report()
    self.assertEqual(self.messages(report, "FAIL"), [])
    warnings = self.messages(report, "WARN")
    self.assertTrue(
        any("could not parse documented default 'TBD' for BRIGHTNESS" in message for message in warnings),
        warnings,
    )

  def test_missing_documented_row_is_failed(self):
    docs = DOCS.replace(
        "| Brightness | 128 | slider | Now | Saved live. |\n",
        "",
    )
    write_fixture(self.root, **{"docs/settings-and-controls.md": docs})
    report = self.direct_report()
    failures = self.messages(report, "FAIL")
    self.assertTrue(
        any("no settings row documents the 'Brightness' default" in message for message in failures),
        failures,
    )

  def test_multiple_save_calls_in_one_case_warn(self):
    source = SOURCE.replace(
        """        case CPU_FREQ:
          save<uint8_t>(setting.type, CPU_FREQ_DEFAULT);
          break;
""",
        """        case CPU_FREQ:
#if defined(FURBLE_M5STICKS3)
          save<uint8_t>(setting.type, CPU_FREQ_DEFAULT);
#else
          save<uint8_t>(setting.type, 80);
#endif
          break;
""",
    )
    write_fixture(self.root, **{"src/FurbleSettings.cpp": source})
    report = self.direct_report()
    self.assertEqual(self.messages(report, "FAIL"), [])
    warnings = self.messages(report, "WARN")
    self.assertTrue(
        any("case body has 2 save<> calls" in message for message in warnings),
        warnings,
    )

  def test_unresolved_source_default_is_named_as_a_warning(self):
    source = SOURCE.replace(
        "save<uint8_t>(setting.type, CPU_FREQ_DEFAULT);",
        "save<uint8_t>(setting.type, computeCpuDefault());",
    )
    write_fixture(self.root, **{"src/FurbleSettings.cpp": source})
    report = self.direct_report()
    self.assertEqual(self.messages(report, "FAIL"), [])
    warnings = self.messages(report, "WARN")
    self.assertTrue(
        any(
            "CPU_FREQ default could not be resolved to a scalar" in message
            for message in warnings
        ),
        warnings,
    )

  def test_duplicate_ledger_wire_id_is_failed(self):
    ledger = LEDGER.replace(
        "\nTake the next free id",
        "| Free | duplicate | 1 |\n\nTake the next free id",
    )
    write_fixture(self.root, **{"include/CLAUDE.md": ledger})
    report = self.direct_report()
    failures = self.messages(report, "FAIL")
    self.assertTrue(
        any("wire id 1 is claimed by both" in message for message in failures),
        failures,
    )

  def test_source_wire_id_outside_ledger_is_failed(self):
    source = SOURCE.replace(
        '{BRIGHTNESS,         {BRIGHTNESS, 1, "Brightness", "brightness", "M5ez"}       }',
        '{BRIGHTNESS,         {BRIGHTNESS, 99, "Brightness", "brightness", "M5ez"}      }',
    )
    write_fixture(self.root, **{"src/FurbleSettings.cpp": source})
    report = self.direct_report()
    failures = self.messages(report, "FAIL")
    self.assertTrue(
        any("BRIGHTNESS uses nonzero wire id 99" in message and "outside every ledger row" in message
            for message in failures),
        failures,
    )

  def test_duplicate_source_wire_id_is_failed(self):
    source = SOURCE.replace(
        '{DISPLAY_OFF,        {DISPLAY_OFF, 24, "Screen off", "display_off", FURBLE_STR} }',
        '{DISPLAY_OFF,        {DISPLAY_OFF, 1, "Screen off", "display_off", FURBLE_STR}  }',
    )
    write_fixture(self.root, **{"src/FurbleSettings.cpp": source})
    report = self.direct_report()
    failures = self.messages(report, "FAIL")
    self.assertTrue(
        any("DISPLAY_OFF reuses nonzero wire id 1 already used by BRIGHTNESS" in message
            for message in failures),
        failures,
    )

  def test_unparsable_settings_row_warns(self):
    source = SOURCE.replace(
        '{GPS_NMEA,           {GPS_NMEA, 14, "GPS Sentences", "gps_nmea", FURBLE_STR}    }',
        '{GPS_NMEA,           {GPS_NMEA, GPS_NMEA_ID, "GPS Sentences", "gps_nmea", FURBLE_STR}}',
    )
    write_fixture(self.root, **{"src/FurbleSettings.cpp": source})
    report = self.direct_report()
    warnings = self.messages(report, "WARN")
    self.assertTrue(
        any("GPS_NMEA wire id 'GPS_NMEA_ID' is not a decimal or hex literal" in message
            for message in warnings),
        warnings,
    )
    failures = self.messages(report, "FAIL")
    self.assertTrue(
        any("GPS_NMEA wire id could not be parsed" in message for message in failures),
        failures,
    )

  def test_diff_mode_requires_readme_docs_and_settings_test(self):
    if shutil.which("git") is None:
      self.skipTest("git is required for the diff contract test")
    self.initialize_git()
    source = SOURCE.replace("save<uint8_t>(setting.type, 128);", "save<uint8_t>(setting.type, 129);")
    (self.root / "src/FurbleSettings.cpp").write_text(source, encoding="utf-8")
    diff = run_git(self.root, "diff", "--no-color", "--unified=8", "HEAD").stdout
    self.assertIn("save<uint8_t>(setting.type, 129);", diff)

    report = CHECKER.check_repository(self.root, CHECKER.parse_unified_diff(diff))
    failures = self.messages(report, "FAIL")
    self.assertTrue(any("README.md" in message for message in failures), failures)
    self.assertTrue(any("docs/settings-and-controls.md" in message for message in failures), failures)
    self.assertTrue(any("docs/wiki/Settings-Reference.md" in message for message in failures), failures)
    self.assertTrue(any("requires a settings test" in message for message in failures), failures)

    self.update_documentation_and_test()
    diff = run_git(self.root, "diff", "--no-color", "--unified=8", "HEAD").stdout
    report = CHECKER.check_repository(self.root, CHECKER.parse_unified_diff(diff))
    self.assertEqual(self.messages(report, "FAIL"), [])
    warnings = self.messages(report, "WARN")
    self.assertFalse(any("legacy-preservation" in message for message in warnings), warnings)

  def test_diff_file_cli_and_git_range(self):
    if shutil.which("git") is None:
      self.skipTest("git is required for the diff contract test")
    self.initialize_git()
    source = SOURCE.replace("save<uint8_t>(setting.type, 128);", "save<uint8_t>(setting.type, 129);")
    (self.root / "src/FurbleSettings.cpp").write_text(source, encoding="utf-8")
    patch = self.root / "change.patch"
    patch.write_text(
        run_git(self.root, "diff", "--no-color", "--unified=8", "HEAD").stdout,
        encoding="utf-8",
    )

    diff_file_result = self.diagnostics("--diff-file", str(patch))
    self.assertEqual(diff_file_result.returncode, 1, diff_file_result.stdout)
    self.assertIn("FAIL:", diff_file_result.stderr)

    range_result = self.diagnostics("--base", "HEAD")
    self.assertEqual(range_result.returncode, 1, range_result.stdout)

    self.update_documentation_and_test()
    self.assertEqual(self.diagnostics("--base", "HEAD").returncode, 0)

  def test_git_range_with_explicit_head(self):
    if shutil.which("git") is None:
      self.skipTest("git is required for the diff contract test")
    self.initialize_git()
    source = SOURCE.replace("save<uint8_t>(setting.type, 128);", "save<uint8_t>(setting.type, 129);")
    (self.root / "src/FurbleSettings.cpp").write_text(source, encoding="utf-8")
    self.update_documentation_and_test()
    result = run_git(self.root, "commit", "-am", "change the default")
    self.assertEqual(result.returncode, 0, result.stderr)
    range_result = self.diagnostics("--base", "HEAD~1", "--head", "HEAD")
    self.assertEqual(range_result.returncode, 0, range_result.stderr + range_result.stdout)

  def test_diff_requires_legacy_preservation_coverage(self):
    if shutil.which("git") is None:
      self.skipTest("git is required for the diff contract test")
    self.initialize_git()
    source = SOURCE.replace("save<uint8_t>(setting.type, 128);", "save<uint8_t>(setting.type, 129);")
    (self.root / "src/FurbleSettings.cpp").write_text(source, encoding="utf-8")
    self.update_documentation_and_test()
    test_path = self.root / "tests/host/settings_nvs_roundtrip_test.cpp"
    test_path.write_text(
        "// Synthetic settings test used by the contract scanner fixtures.\n"
        "// The default assertion moved from 128 to 129.\n",
        encoding="utf-8",
    )
    diff = run_git(self.root, "diff", "--no-color", "--unified=8", "HEAD").stdout
    report = CHECKER.check_repository(self.root, CHECKER.parse_unified_diff(diff))
    self.assertEqual(self.messages(report, "FAIL"), [])
    warnings = self.messages(report, "WARN")
    self.assertTrue(any("legacy-preservation" in message for message in warnings), warnings)

  def test_plain_unified_diff_with_timestamps_is_parsed(self):
    diff = (
        "--- a/src/FurbleSettings.cpp\t2026-11-02 20:00:00\n"
        "+++ b/src/FurbleSettings.cpp\t2026-11-02 20:00:01\n"
        "@@ -53,3 +53,3 @@\n"
        "         case BRIGHTNESS:\n"
        "-          save<uint8_t>(setting.type, 128);\n"
        "+          save<uint8_t>(setting.type, 129);\n"
    )
    parsed = CHECKER.parse_unified_diff(diff)
    self.assertEqual(parsed.changed_paths, {"src/FurbleSettings.cpp"})
    self.assertEqual(len(parsed.default_changes), 1)
    report = CHECKER.check_repository(self.root, parsed)
    failures = self.messages(report, "FAIL")
    self.assertTrue(any("README.md" in message for message in failures), failures)

  def test_diff_default_without_case_label_warns(self):
    diff = (
        "diff --git a/src/FurbleSettings.cpp b/src/FurbleSettings.cpp\n"
        "--- a/src/FurbleSettings.cpp\n"
        "+++ b/src/FurbleSettings.cpp\n"
        "@@ -1,2 +1,2 @@\n"
        " context\n"
        "-          save<uint8_t>(setting.type, 128);\n"
        "+          save<uint8_t>(setting.type, 129);\n"
    )
    report = CHECKER.check_repository(self.root, CHECKER.parse_unified_diff(diff))
    warnings = self.messages(report, "WARN")
    self.assertTrue(
        any("no visible case label in the diff hunk" in message for message in warnings),
        warnings,
    )

  def test_diff_without_default_change_needs_no_docs(self):
    if shutil.which("git") is None:
      self.skipTest("git is required for the diff contract test")
    self.initialize_git()
    source = SOURCE.replace(
        "        case BRIGHTNESS:\n",
        "        // The brightness default is documented below.\n        case BRIGHTNESS:\n",
    )
    (self.root / "src/FurbleSettings.cpp").write_text(source, encoding="utf-8")
    diff = run_git(self.root, "diff", "--no-color", "--unified=8", "HEAD").stdout
    report = CHECKER.check_repository(self.root, CHECKER.parse_unified_diff(diff))
    self.assertEqual(self.messages(report, "FAIL"), [])

  def initialize_git(self) -> None:
    run_git(self.root, "init")
    run_git(self.root, "config", "user.email", "tests@example.invalid")
    run_git(self.root, "config", "user.name", "Contract Tests")
    run_git(self.root, "add", ".")
    result = run_git(self.root, "commit", "-m", "fixture")
    self.assertEqual(result.returncode, 0, result.stderr)

  def update_documentation_and_test(self) -> None:
    for relative in ("docs/settings-and-controls.md", "docs/wiki/Settings-Reference.md"):
      path = self.root / relative
      path.write_text(path.read_text(encoding="utf-8").replace(
          "| Brightness | 128 |", "| Brightness | 129 |"
      ), encoding="utf-8")
    readme = self.root / "README.md"
    readme.write_text(
        readme.read_text(encoding="utf-8").replace("default is 128", "default is 129"),
        encoding="utf-8",
    )
    test_path = self.root / "tests/host/settings_nvs_roundtrip_test.cpp"
    test_path.write_text(
        "// Synthetic settings test used by the contract scanner fixtures.\n"
        "// The default assertion moved from 128 to 129 and keeps legacy preservation.\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
  unittest.main()
