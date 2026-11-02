"""Regression tests for the stdlib-only C++ safety scanner.

Every rule gets a synthetic bad case and a fixed case in a temporary source
tree. The fake_uart payload pattern is copied from the review finding that
started this scanner.
"""

from contextlib import redirect_stderr, redirect_stdout
from importlib.util import module_from_spec, spec_from_file_location
from io import StringIO
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = spec_from_file_location("check_cpp_safety", ROOT / "tools/check_cpp_safety.py")
assert SPEC is not None and SPEC.loader is not None
SCANNER = module_from_spec(SPEC)
sys.modules[SPEC.name] = SCANNER
SPEC.loader.exec_module(SCANNER)


class CppSafetyScannerTest(unittest.TestCase):
  def scan(self, sources: dict[str, str], options=None):
    with tempfile.TemporaryDirectory() as directory:
      root = Path(directory)
      paths = []
      for name, text in sources.items():
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
        if path.suffix in SCANNER.CPP_SUFFIXES:
          paths.append(path)
      return SCANNER.scan_files(root, paths, options)

  def run_cli(self, arguments):
    output = StringIO()
    errors = StringIO()
    with redirect_stdout(output), redirect_stderr(errors):
      code = SCANNER.main([str(argument) for argument in arguments])
    return code, output.getvalue()

  def findings_for(self, findings, rule, severity=None):
    return [
        finding
        for finding in findings
        if finding.rule == rule
        and (severity is None or finding.severity == severity)
    ]

  # container-bounds

  def test_vector_literal_size_out_of_bounds_is_error(self):
    findings = self.scan(
        {
            "src/bad.cpp": """
void fill() {
  std::vector<uint8_t> payload(20);
  payload[20] = 1;
}
"""
        }
    )
    matches = self.findings_for(findings, SCANNER.RULE_BOUNDS, SCANNER.ERROR)
    self.assertEqual(len(matches), 1)
    self.assertEqual(matches[0].line, 4)
    self.assertIn("payload[20]", matches[0].message)

  def test_vector_in_bounds_is_accepted(self):
    findings = self.scan(
        {
            "src/good.cpp": """
void fill() {
  std::vector<uint8_t> payload(20);
  payload[19] = 1;
}
"""
        }
    )
    self.assertEqual(self.findings_for(findings, SCANNER.RULE_BOUNDS), [])

  def test_fake_uart_ternary_payload_is_caught(self):
    findings = self.scan(
        {
            "sim/fake_uart.cpp": """
void queuePollResponseLocked(bool monHwShort) {
  std::vector<uint8_t> payload(monHwShort ? 20 : 56, 0);
  payload[0] = 0x11;
  payload[20] = 2;
  payload[24] = 7;
}
"""
        }
    )
    matches = self.findings_for(findings, SCANNER.RULE_BOUNDS, SCANNER.ERROR)
    self.assertEqual([finding.line for finding in matches], [5, 6])
    self.assertIn("capacity 20", matches[0].message)

  def test_fake_uart_short_branch_guard_is_accepted(self):
    findings = self.scan(
        {
            "sim/fake_uart.cpp": """
void queuePollResponseLocked(bool monHwShort) {
  std::vector<uint8_t> payload(monHwShort ? 20 : 56, 0);
  payload[0] = 0x11;
  if (!monHwShort) {
    payload[20] = 2;
    payload[24] = 7;
  }
}
"""
        }
    )
    self.assertEqual(self.findings_for(findings, SCANNER.RULE_BOUNDS), [])

  def test_array_reserve_and_resize_bounds_are_checked(self):
    findings = self.scan(
        {
            "src/bounds.cpp": """
void fill() {
  std::array<uint8_t, 4> a{};
  a[4] = 1;
  std::vector<uint8_t> b;
  b.reserve(3);
  b[3] = 1;
  std::vector<uint8_t> c;
  c.resize(5);
  c[5] = 1;
}
"""
        }
    )
    matches = self.findings_for(findings, SCANNER.RULE_BOUNDS, SCANNER.ERROR)
    self.assertEqual([finding.line for finding in matches], [4, 7, 10])
    self.assertEqual(len({finding.message for finding in matches}), 3)

  def test_symbolic_size_is_skipped(self):
    findings = self.scan(
        {
            "src/symbolic.cpp": """
void fill(std::size_t count) {
  std::vector<uint8_t> payload(count);
  payload[999] = 1;
}
"""
        }
    )
    self.assertEqual(self.findings_for(findings, SCANNER.RULE_BOUNDS), [])

  def test_constant_size_is_propagated(self):
    findings = self.scan(
        {
            "src/constants.cpp": """
void fill() {
  constexpr std::size_t kSize = 20;
  std::vector<uint8_t> payload(kSize);
  payload[20] = 1;
}
"""
        }
    )
    matches = self.findings_for(findings, SCANNER.RULE_BOUNDS, SCANNER.ERROR)
    self.assertEqual([finding.line for finding in matches], [5])

  def test_resize_after_declaration_uses_latest_capacity(self):
    findings = self.scan(
        {
            "src/resize.cpp": """
void fill() {
  std::vector<uint8_t> payload(4);
  payload.resize(8);
  payload[7] = 1;
}
"""
        }
    )
    self.assertEqual(self.findings_for(findings, SCANNER.RULE_BOUNDS), [])

  # filesystem-remove

  def test_directory_iterator_remove_without_filter_is_error(self):
    findings = self.scan(
        {
            "sim/sweep.cpp": """
void sweep(const std::filesystem::path &directory) {
  for (const auto &entry : std::filesystem::directory_iterator(directory)) {
    std::filesystem::remove(entry.path());
  }
}
"""
        }
    )
    matches = self.findings_for(findings, SCANNER.RULE_FILESYSTEM, SCANNER.ERROR)
    self.assertEqual([finding.line for finding in matches], [4])

  def test_directory_iterator_remove_with_regular_file_filter_is_accepted(self):
    findings = self.scan(
        {
            "sim/sweep.cpp": """
void sweep(const std::filesystem::path &directory) {
  for (const auto &entry : std::filesystem::directory_iterator(directory)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    std::filesystem::remove(entry.path());
  }
}
"""
        }
    )
    self.assertEqual(self.findings_for(findings, SCANNER.RULE_FILESYSTEM), [])

  def test_filter_after_the_remove_does_not_hide_the_bug(self):
    findings = self.scan(
        {
            "sim/sweep.cpp": """
void sweep(const std::filesystem::path &directory) {
  for (const auto &entry : std::filesystem::directory_iterator(directory)) {
    std::filesystem::remove(entry.path());
    if (!entry.is_regular_file()) {
      continue;
    }
  }
}
"""
        }
    )
    matches = self.findings_for(findings, SCANNER.RULE_FILESYSTEM, SCANNER.ERROR)
    self.assertEqual([finding.line for finding in matches], [4])

  def test_classic_directory_iterator_remove_is_caught(self):
    findings = self.scan(
        {
            "sim/sweep.cpp": """
void sweep(const std::filesystem::path &directory) {
  std::error_code ignored;
  for (std::filesystem::directory_iterator it(directory), end; it != end;
       it.increment(ignored)) {
    std::filesystem::remove(it->path(), ignored);
  }
}
"""
        }
    )
    matches = self.findings_for(findings, SCANNER.RULE_FILESYSTEM, SCANNER.ERROR)
    self.assertEqual([finding.line for finding in matches], [6])

  def test_initializer_directory_iterator_remove_is_caught(self):
    findings = self.scan(
        {
            "sim/sweep.cpp": """
void sweep(const std::filesystem::path &directory) {
  for (auto it = std::filesystem::directory_iterator(directory);
       it != std::filesystem::directory_iterator(); ++it) {
    std::filesystem::remove(it->path());
  }
}
"""
        }
    )
    matches = self.findings_for(findings, SCANNER.RULE_FILESYSTEM, SCANNER.ERROR)
    self.assertEqual([finding.line for finding in matches], [5])

  def test_remove_of_other_path_is_only_a_warning(self):
    findings = self.scan(
        {
            "sim/sweep.cpp": """
void sweep(const std::filesystem::path &directory,
           const std::filesystem::path &other) {
  for (const auto &entry : std::filesystem::directory_iterator(directory)) {
    std::filesystem::remove(other);
  }
}
"""
        }
    )
    matches = self.findings_for(findings, SCANNER.RULE_FILESYSTEM)
    self.assertEqual([finding.severity for finding in matches], [SCANNER.WARN])

  def test_std_remove_algorithm_is_not_filesystem_removal(self):
    findings = self.scan(
        {
            "sim/algorithm.cpp": """
void sweep(const std::filesystem::path &directory) {
  for (const auto &entry : std::filesystem::directory_iterator(directory)) {
    std::remove(values.begin(), values.end(), 1);
  }
}
"""
        }
    )
    self.assertEqual(self.findings_for(findings, SCANNER.RULE_FILESYSTEM), [])

  # low-byte-sentinel

  LOW_BYTE_HEADER = """
#pragma once
#include <cstdint>
struct Stats {
  uint32_t jam_indicator;
};
"""

  def test_low_byte_assertion_is_warned(self):
    findings = self.scan(
        {
            "include/stats.h": self.LOW_BYTE_HEADER,
            "tests/stats_test.cpp": """
#include "stats.h"
void testStats() {
  std::vector<uint8_t> buf(32, 0);
  buf[24] = 0x2A;
  Stats stats{};
  check(stats.jam_indicator == 0x2Au, "jam");
}
""",
        }
    )
    matches = self.findings_for(findings, SCANNER.RULE_LOW_BYTE, SCANNER.WARN)
    self.assertEqual([finding.line for finding in matches], [7])
    self.assertIn("32-bit field", matches[0].message)

  def test_full_width_assertion_is_accepted(self):
    findings = self.scan(
        {
            "include/stats.h": self.LOW_BYTE_HEADER,
            "tests/stats_test.cpp": """
#include "stats.h"
void testStats() {
  std::vector<uint8_t> buf(32, 0);
  buf[24] = 0x2A; buf[25] = 0x33; buf[26] = 0x22; buf[27] = 0x11;
  Stats stats{};
  check(stats.jam_indicator == 0x1122332Au, "jam");
}
""",
        }
    )
    self.assertEqual(self.findings_for(findings, SCANNER.RULE_LOW_BYTE), [])

  def test_uint8_field_assertion_is_accepted(self):
    findings = self.scan(
        {
            "include/stats.h": """
#pragma once
#include <cstdint>
struct Stats { uint8_t jam_indicator; };
""",
            "tests/stats_test.cpp": """
#include "stats.h"
void testStats() {
  std::vector<uint8_t> buf(32, 0);
  buf[24] = 0x2A;
  Stats stats{};
  check(stats.jam_indicator == 0x2Au, "jam");
}
""",
        }
    )
    self.assertEqual(self.findings_for(findings, SCANNER.RULE_LOW_BYTE), [])

  def test_masked_comparison_is_accepted(self):
    findings = self.scan(
        {
            "include/stats.h": self.LOW_BYTE_HEADER,
            "tests/stats_test.cpp": """
#include "stats.h"
void testStats() {
  std::vector<uint8_t> buf(32, 0);
  buf[24] = 0x2A;
  Stats stats{};
  check((stats.jam_indicator & 0xFF) == 0x2Au, "low byte");
}
""",
        }
    )
    self.assertEqual(self.findings_for(findings, SCANNER.RULE_LOW_BYTE), [])

  def test_missing_fixture_byte_write_is_skipped(self):
    findings = self.scan(
        {
            "include/stats.h": self.LOW_BYTE_HEADER,
            "tests/stats_test.cpp": """
#include "stats.h"
void testStats() {
  Stats stats{};
  check(stats.jam_indicator == 0x2Au, "jam");
}
""",
        }
    )
    self.assertEqual(self.findings_for(findings, SCANNER.RULE_LOW_BYTE), [])

  # terminal-state-acquire

  TERMINAL_ENUMS = """
#include <cstdint>
enum class receiver_state_t : uint8_t { UNKNOWN, DETECTING, PRESENT, ABSENT };
enum class cycle_state_t : uint8_t { ENABLED, DISABLED };
"""

  def test_acquire_without_terminal_guard_warns(self):
    findings = self.scan(
        {
            "src/state.cpp": self.TERMINAL_ENUMS
            + """
void acquirePowerLock() {}
void beginBurst() {
  acquirePowerLock();
}
"""
        }
    )
    matches = self.findings_for(findings, SCANNER.RULE_TERMINAL, SCANNER.WARN)
    self.assertEqual([finding.line for finding in matches], [8])
    self.assertIn("ABSENT", matches[0].message)
    self.assertIn("DISABLED", matches[0].message)

  def test_acquire_with_terminal_guard_is_accepted(self):
    findings = self.scan(
        {
            "src/state.cpp": self.TERMINAL_ENUMS
            + """
void acquirePowerLock() {}
void beginBurst() {
  if ((m_CycleState == cycle_state_t::DISABLED)
      || (m_Receiver == receiver_state_t::ABSENT)) {
    return;
  }
  acquirePowerLock();
}
"""
        }
    )
    self.assertEqual(self.findings_for(findings, SCANNER.RULE_TERMINAL), [])

  def test_partial_terminal_guard_warns_for_missing_state(self):
    findings = self.scan(
        {
            "src/state.cpp": self.TERMINAL_ENUMS
            + """
void acquirePowerLock() {}
void beginBurst() {
  if (m_CycleState == cycle_state_t::DISABLED) {
    return;
  }
  acquirePowerLock();
}
"""
        }
    )
    matches = self.findings_for(findings, SCANNER.RULE_TERMINAL)
    self.assertEqual(len(matches), 1)
    self.assertIn("ABSENT", matches[0].message)
    self.assertNotIn("DISABLED", matches[0].message)

  def test_guard_in_sibling_branch_does_not_cover_a_later_call(self):
    findings = self.scan(
        {
            "src/state.cpp": self.TERMINAL_ENUMS
            + """
void acquirePowerLock() {}
void beginBurst(bool branch) {
  if (branch) {
    if ((m_Receiver == receiver_state_t::ABSENT)
        || (m_CycleState == cycle_state_t::DISABLED)) {
      return;
    }
    acquirePowerLock();
  }
  acquirePowerLock();
}
"""
        }
    )
    matches = self.findings_for(findings, SCANNER.RULE_TERMINAL)
    self.assertEqual([finding.line for finding in matches], [15])

  def test_acquire_inside_the_guard_body_is_not_guarded(self):
    findings = self.scan(
        {
            "src/state.cpp": self.TERMINAL_ENUMS
            + """
void acquirePowerLock() {}
void beginBurst() {
  if (m_Receiver == receiver_state_t::ABSENT) {
    acquirePowerLock();
    return;
  }
}
"""
        }
    )
    matches = self.findings_for(findings, SCANNER.RULE_TERMINAL)
    self.assertEqual([finding.line for finding in matches], [9])

  def test_unrelated_function_is_not_inspected_by_default(self):
    findings = self.scan(
        {
            "src/state.cpp": self.TERMINAL_ENUMS
            + """
void acquirePowerLock() {}
void enable() {
  acquirePowerLock();
}
"""
        }
    )
    self.assertEqual(self.findings_for(findings, SCANNER.RULE_TERMINAL), [])

  def test_acquire_function_list_is_configurable(self):
    options = SCANNER.ScanOptions(
        rules=frozenset({SCANNER.RULE_TERMINAL}),
        acquire_functions=("enable",),
        acquire_regex=r"^$",
    )
    findings = self.scan(
        {
            "src/state.cpp": self.TERMINAL_ENUMS
            + """
void acquirePowerLock() {}
void enable() {
  acquirePowerLock();
}
"""
        },
        options,
    )
    matches = self.findings_for(findings, SCANNER.RULE_TERMINAL)
    self.assertEqual(len(matches), 1)
    self.assertIn("enable()", matches[0].message)

  def test_terminal_rule_can_be_disabled(self):
    options = SCANNER.ScanOptions(
        rules=frozenset({SCANNER.RULE_BOUNDS}),
    )
    findings = self.scan(
        {
            "src/state.cpp": self.TERMINAL_ENUMS
            + """
void acquirePowerLock() {}
void beginBurst() {
  acquirePowerLock();
}
"""
        },
        options,
    )
    self.assertEqual(self.findings_for(findings, SCANNER.RULE_TERMINAL), [])

  # Command line selection and exit status

  def test_diff_file_selects_changed_cpp_file(self):
    with tempfile.TemporaryDirectory() as directory:
      root = Path(directory)
      (root / "src").mkdir()
      (root / "src/bad.cpp").write_text(
          "void fill() {\n"
          "  std::vector<uint8_t> payload(20);\n"
          "  payload[20] = 1;\n"
          "}\n",
          encoding="utf-8",
      )
      diff = root / "review.diff"
      diff.write_text(
          "diff --git a/src/bad.cpp b/src/bad.cpp\n"
          "index 1111111..2222222 100644\n"
          "--- a/src/bad.cpp\n"
          "+++ b/src/bad.cpp\n"
          "@@ -1,2 +1,4 @@\n"
          "+void fill() {\n"
          "+  std::vector<uint8_t> payload(20);\n"
          "+  payload[20] = 1;\n"
          "+}\n",
          encoding="utf-8",
      )
      code, output = self.run_cli(["--root", root, "--diff-file", diff])
      self.assertEqual(code, 1)
      self.assertIn("src/bad.cpp:3: ERROR container-bounds", output)

  def test_clean_diff_file_exits_zero(self):
    with tempfile.TemporaryDirectory() as directory:
      root = Path(directory)
      (root / "src").mkdir()
      (root / "src/good.cpp").write_text(
          "void fill() {\n  std::vector<uint8_t> payload(4);\n  payload[3] = 1;\n}\n",
          encoding="utf-8",
      )
      diff = root / "review.diff"
      diff.write_text(
          "--- a/src/good.cpp\n+++ b/src/good.cpp\n@@ -1,3 +1,3 @@\n",
          encoding="utf-8",
      )
      code, output = self.run_cli(["--root", root, "--diff-file", diff])
      self.assertEqual(code, 0)
      self.assertIn("0 error(s), 0 warning(s)", output)

  def test_missing_selector_is_a_usage_error(self):
    with tempfile.TemporaryDirectory() as directory:
      code, _ = self.run_cli(["--root", directory])
      self.assertEqual(code, 2)

  def test_strict_promotes_warnings_to_failure(self):
    with tempfile.TemporaryDirectory() as directory:
      root = Path(directory)
      (root / "include").mkdir()
      (root / "include/stats.h").write_text(
          self.LOW_BYTE_HEADER, encoding="utf-8"
      )
      (root / "tests").mkdir()
      (root / "tests/stats_test.cpp").write_text(
          '#include "stats.h"\n'
          "void testStats() {\n"
          "  std::vector<uint8_t> buf(32, 0);\n"
          "  buf[24] = 0x2A;\n"
          "  Stats stats{};\n"
          '  check(stats.jam_indicator == 0x2Au, "jam");\n'
          "}\n",
          encoding="utf-8",
      )
      code, _ = self.run_cli(["--root", root, "--all"])
      self.assertEqual(code, 0)
      strict_code, output = self.run_cli(["--root", root, "--all", "--strict"])
      self.assertEqual(strict_code, 1)
      self.assertIn("WARN low-byte-sentinel", output)


if __name__ == "__main__":
  unittest.main()
