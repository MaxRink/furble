"""Tests for the lib/hal datasheet citation gate."""

from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

from tools.check_datasheet_citations import (
  COVER_MAX_LINES,
  PAGE_DIR,
  SUFFIXES,
  check,
  inline_comment,
  page_for,
  tokens_in,
)
from tools.check_format import SUFFIXES as FORMAT_SUFFIXES


ROOT = Path(__file__).resolve().parents[1]
CHECKER = ROOT / "tools" / "check_datasheet_citations.py"
GAP_HEADER = (
  "| Constant or fact | Driver location | Why no section | Page row |\n"
  "| :--- | :--- | :--- | :--- |\n"
)


class CitationGateTest(unittest.TestCase):
  def make_root(self, driver: str, page: str, name: str = "FAKE7",
                path: str | None = None) -> Path:
    root = Path(tempfile.mkdtemp(prefix="furble-citations-"))
    self.addCleanup(shutil.rmtree, root)
    driver_path = root / "lib" / "hal" / (path or f"{name}.h")
    driver_path.parent.mkdir(parents=True, exist_ok=True)
    (root / "docs" / "components").mkdir(parents=True)
    driver_path.write_text(driver, encoding="utf-8")
    (root / "docs" / "components" / f"{name.lower()}.md").write_text(page, encoding="utf-8")
    return root

  def test_cited_register_passes(self):
    root = self.make_root(
        "#pragma once\n"
        "/*\n"
        " * Fake datasheet revision 2.0, section 4.2.\n"
        " */\n"
        "constexpr unsigned char REG_A = 0x10;\n",
        "# FAKE7\n")
    errors, checked, cited, tolerated = check(root)
    self.assertEqual(errors, [])
    self.assertEqual((checked, cited, tolerated), (1, 1, 0))

  def test_uncited_register_fails_and_names_the_page(self):
    root = self.make_root(
        "constexpr unsigned char REG_A = 0x10;\n",
        "# FAKE7\n\n## Citation gaps\n\n" + GAP_HEADER)
    errors, _, _, _ = check(root)
    self.assertEqual(len(errors), 1)
    self.assertIn("REG_A", errors[0])
    self.assertIn("docs/components/fake7.md", errors[0])

  def test_uncited_register_listed_as_gap_passes(self):
    root = self.make_root(
        "constexpr unsigned char REG_A = 0x10;\n",
        "# FAKE7\n\n## Citation gaps\n\n" + GAP_HEADER
        + "| `REG_A` | `FAKE7.h:1` | no revision string in the copy | Facts row 1 |\n")
    errors, _, _, tolerated = check(root)
    self.assertEqual(errors, [])
    self.assertEqual(tolerated, 1)

  def test_gap_in_another_section_does_not_count(self):
    root = self.make_root(
        "constexpr unsigned char REG_A = 0x10;\n",
        "# FAKE7\n\n## Facts still unverified\n\n"
        "| Constant | Reason |\n| :--- | :--- |\n| `REG_A` | guess |\n")
    errors, _, _, tolerated = check(root)
    self.assertEqual(tolerated, 0)
    self.assertEqual(len(errors), 1)

  def test_malformed_gap_row_fails(self):
    root = self.make_root(
        "constexpr unsigned char REG_A = 0x10;\n",
        "# FAKE7\n\n## Citation gaps\n\n" + GAP_HEADER
        + "| `REG_A` | `FAKE7.h:1` |\n")
    errors, _, _, tolerated = check(root)
    self.assertEqual(tolerated, 0)
    self.assertTrue(any("four non-empty columns" in error for error in errors), errors)

  def test_gap_header_must_match_the_documented_columns(self):
    root = self.make_root(
        "constexpr unsigned char REG_A = 0x10;\n",
        "# FAKE7\n\n## Citation gaps\n\n"
        "| Name | Why |\n| :--- | :--- |\n| `REG_A` | nothing |\n")
    errors, _, _, tolerated = check(root)
    self.assertEqual(tolerated, 0)
    self.assertTrue(any("header must be" in error for error in errors), errors)

  def test_gap_table_is_validated_on_every_component_page(self):
    """A page no driver maps to is checked too, so no table escapes."""
    root = self.make_root(
        "constexpr unsigned char REG_A = 0x10;  // section 1.1\n",
        "# FAKE7\n")
    other = root / PAGE_DIR / "otherpart.md"
    other.write_text(
        "# OTHERPART\n\n## Citation gaps\n\n| Name | Why |\n| :--- | :--- |\n"
        "| none | - |\n", encoding="utf-8")
    errors, _, _, _ = check(root)
    self.assertEqual(len(errors), 1, errors)
    self.assertIn("otherpart.md", errors[0])
    self.assertIn("header must be", errors[0])
    other.write_text(
        "# OTHERPART\n\n## Citation gaps\n\n" + GAP_HEADER
        + "| `REG_A` | two cells only |\n", encoding="utf-8")
    errors, _, _, _ = check(root)
    self.assertEqual(len(errors), 1, errors)
    self.assertIn("otherpart.md", errors[0])
    self.assertIn("four non-empty columns", errors[0])

  def test_missing_page_fails(self):
    root = self.make_root(
        "/* section 4.2 */\nconstexpr unsigned char REG_A = 0x10;\n",
        "# FAKE7\n")
    (root / PAGE_DIR / "fake7.md").unlink()
    errors, _, _, _ = check(root)
    self.assertTrue(any("no component page" in error for error in errors), errors)

  def test_new_driver_maps_to_lower_case_page(self):
    self.assertEqual(page_for(Path("lib/hal/newchip.h")), PAGE_DIR / "newchip.md")

  def test_raw_bus_call_register_is_a_token(self):
    root = self.make_root(
        "bool f(Port &port) { return port.write8(0x42, 1); }\n",
        "# FAKE7\n")
    errors, checked, _, _ = check(root)
    self.assertEqual(checked, 1)
    self.assertEqual(len(errors), 1)
    self.assertIn("0x42", errors[0])

  def test_reg_name_with_shift_expression_is_not_a_token(self):
    root = self.make_root(
        "constexpr unsigned char REG_BIT = 1U << 5;\n"
        "constexpr unsigned char REG_MASK = 0xE0;\n",
        "# FAKE7\n")
    found = tokens_in(Path("lib/hal/FAKE7.h"), root)
    self.assertEqual([token.name for token in found], ["REG_MASK"])
    errors, checked, _, _ = check(root)
    self.assertEqual(checked, 1)
    self.assertEqual(len(errors), 1)
    self.assertIn("REG_MASK", errors[0])

  def test_timing_constant_needs_a_citation(self):
    root = self.make_root(
        "/* section 8.1 Reset */\n"
        "constexpr unsigned int RESET_US = 200;\n",
        "# FAKE7\n")
    errors, _, _, _ = check(root)
    self.assertEqual(errors, [])

  def test_inline_citation_on_the_token_line_is_accepted(self):
    root = self.make_root(
        "constexpr unsigned int POLL_US = 100;  // driver choice, no figure\n"
        "constexpr unsigned int SETTLE_US = 500;  // section 13.1.1\n",
        "# FAKE7\n")
    errors, checked, cited, _ = check(root)
    self.assertEqual((checked, cited), (2, 1))
    self.assertEqual(len(errors), 1)
    self.assertIn("POLL_US", errors[0])
    root2 = self.make_root(
        "constexpr unsigned int SETTLE_US = 500;  // section 13.1.1\n",
        "# FAKE7\n")
    errors2, _, cited2, _ = check(root2)
    self.assertEqual(errors2, [])
    self.assertEqual(cited2, 1)

  def test_inline_comment_ignores_markers_inside_literals(self):
    self.assertEqual(inline_comment('x = "a//section 4.2";'), "")
    self.assertEqual(inline_comment('x = "http://a";  // Table 2-6'), " Table 2-6")
    self.assertEqual(inline_comment("char c = '/';  // section 1"), " section 1")
    self.assertEqual(inline_comment('x = "q\\"//y";  // section 2'), " section 2")

  def test_url_in_a_string_cannot_fabricate_a_citation(self):
    root = self.make_root(
        'constexpr unsigned int SETTLE_MS = 20; const char *kDoc = "a//section 9.9";\n'
        'constexpr unsigned int RESET_MS = 30; const char *kRef = "b//c";  // section 4.2\n',
        "# FAKE7\n")
    errors, checked, cited, _ = check(root)
    self.assertEqual((checked, cited), (2, 1))
    self.assertEqual(len(errors), 1)
    self.assertIn("SETTLE_MS", errors[0])

  def test_define_and_plain_const_are_tokens(self):
    root = self.make_root(
        "#define REG_MODE 0x12\n"
        "const unsigned char REG_CTRL = 0x20;\n"
        "static const unsigned char ADDRESS = 0x55;\n",
        "# FAKE7\n")
    found = tokens_in(Path("lib/hal/FAKE7.h"), root)
    self.assertEqual(
      sorted(token.name for token in found), ["ADDRESS", "REG_CTRL", "REG_MODE"])
    errors, checked, _, _ = check(root)
    self.assertEqual(checked, 3)
    self.assertEqual(len(errors), 3)

  def test_definition_forms_are_read(self):
    root = self.make_root(
        "#define REG_HASH_SPACE   0x12UL\n"
        "#  define REG_IN_PARENS (0x13)\n"
        "constexpr unsigned char REG_BRACED{0x14};\n"
        "constexpr unsigned char REG_PAREN_VALUE = (0x15);\n"
        "constexpr unsigned char REG_FIRST = 0x16, REG_SECOND = 0x17;\n"
        "constexpr unsigned char REG_SUFFIXED = 0x18U;\n",
        "# FAKE7\n")
    found = tokens_in(Path("lib/hal/FAKE7.h"), root)
    self.assertEqual(
      sorted(token.name for token in found),
      ["REG_BRACED", "REG_FIRST", "REG_HASH_SPACE", "REG_IN_PARENS",
       "REG_PAREN_VALUE", "REG_SECOND", "REG_SUFFIXED"])
    errors, checked, _, _ = check(root)
    self.assertEqual(checked, 7)
    self.assertEqual(len(errors), 7)

  def test_non_literal_initialiser_is_out_of_scope(self):
    """The one documented fail-open path: only a literal initialiser counts.

    `lib/hal/CLAUDE.md` names the trade-off. This pins it, so the blind spot
    stays explicit and cannot widen without a failing test.
    """
    root = self.make_root(
        "constexpr unsigned char REG_OR = 0x10 | 0x01;\n"
        "constexpr unsigned char REG_CAST = static_cast<unsigned char>(0x10);\n"
        "constexpr unsigned char REG_WRAPPED =\n"
        "    0x10;\n",
        "# FAKE7\n")
    self.assertEqual(tokens_in(Path("lib/hal/FAKE7.h"), root), [])
    errors, checked, _, _ = check(root)
    self.assertEqual(checked, 0)
    self.assertEqual(errors, [])

  def test_nested_driver_directory_is_scanned(self):
    root = self.make_root(
        "constexpr unsigned char REG_A = 0x10;\n",
        "# FAKE7\n",
        path="radio/FAKE7.h")
    errors, checked, _, _ = check(root)
    self.assertEqual(checked, 1)
    self.assertEqual(len(errors), 1)
    self.assertIn("radio/FAKE7.h", errors[0])

  def test_gate_scans_the_cxx_extensions_the_format_gate_formats(self):
    self.assertEqual(set(SUFFIXES), set(FORMAT_SUFFIXES))
    for name in ("FAKE7.cxx", "FAKE7.hh", "FAKE7.hxx"):
      root = self.make_root(
          "constexpr unsigned char REG_A = 0x10;\n", "# FAKE7\n", path=name)
      errors, checked, _, _ = check(root)
      self.assertEqual(checked, 1, name)
      self.assertEqual(len(errors), 1, name)
      self.assertIn(name, errors[0])

  def test_block_rule_covers_a_declaration_run(self):
    body = "".join(f"constexpr unsigned char REG_{n} = 0x{n:02X};\n" for n in range(1, 6))
    root = self.make_root(
        "/* section 6.13.2 Register Description */\n" + body,
        "# FAKE7\n")
    errors, checked, cited, _ = check(root)
    self.assertEqual(errors, [])
    self.assertEqual((checked, cited), (5, 5))

  def test_block_rule_is_bounded(self):
    # Pinned to the documented bound, so raising COVER_MAX_LINES fails here
    # instead of quietly widening how far one comment reaches.
    self.assertEqual(COVER_MAX_LINES, 16)
    count = COVER_MAX_LINES + 4
    body = "".join(f"constexpr unsigned char REG_{n} = 0x{n:02X};\n" for n in range(1, count + 1))
    root = self.make_root(
        "/* section 6.13.2 Register Description */\n" + body,
        "# FAKE7\n")
    errors, checked, cited, _ = check(root)
    self.assertEqual(checked, count)
    self.assertEqual(cited, 16)
    self.assertEqual(len(errors), count - 16)

  def test_malformed_citation_shapes_do_not_count(self):
    for fragment in ("section 6x", "Table Item", "section six", "table 9a"):
      root = self.make_root(
        f"// {fragment}\nconstexpr unsigned char REG_A = 0x10;\n",
        "# FAKE7\n")
      errors, _, _, _ = check(root)
      self.assertEqual(len(errors), 1, fragment)

  def test_valid_citation_shapes_count(self):
    for fragment in ("section 6.13.2.30", "sections 6.10 and 6.13.2.30",
                     "Table 2-6", "table 8-2", "Table A-1", "section 3"):
      root = self.make_root(
        f"// {fragment}\nconstexpr unsigned char REG_A = 0x10;\n",
        "# FAKE7\n")
      errors, _, _, _ = check(root)
      self.assertEqual(errors, [], fragment)

  def test_real_tree_passes_and_token_count_is_pinned(self):
    """A coverage regression fails here, not silently in a page."""
    errors, checked, cited, tolerated = check(ROOT)
    self.assertEqual(errors, [])
    self.assertEqual((checked, cited, tolerated), (34, 30, 4))
    counts = {
      "lib/hal/AXP2101.cpp": 15,
      "lib/hal/AXP2101.h": 1,
      "lib/hal/BQ27220.cpp": 5,
      "lib/hal/BQ27220.h": 3,
      "lib/hal/HalBus.h": 0,
      "lib/hal/SX1262.cpp": 0,
      "lib/hal/SX1262.h": 6,
      "lib/hal/XL9555.cpp": 3,
      "lib/hal/XL9555.h": 1,
    }
    for relative, expected in counts.items():
      self.assertEqual(len(tokens_in(Path(relative), ROOT)), expected, relative)

  def test_cli_reports_success_on_the_real_tree(self):
    result = subprocess.run(
      [sys.executable, str(CHECKER), "--root", str(ROOT)],
      capture_output=True, text=True, check=False)
    self.assertEqual(result.returncode, 0, result.stderr + result.stdout)


if __name__ == "__main__":
  unittest.main()
