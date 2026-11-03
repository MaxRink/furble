"""Tests for the static-analysis gate parsing."""

import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "check_static_analysis", ROOT / "tools" / "check_static_analysis.py"
)
CHECKER = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(CHECKER)


class CppcheckParseTest(unittest.TestCase):
    def test_production_finding_is_parsed(self):
        findings = CHECKER.parse_cppcheck(
            "src/FurbleGPS.cpp:42:warning:nullPointer:Possible null pointer dereference\n"
        )
        self.assertEqual(len(findings), 1)
        self.assertEqual(findings[0]["file"], "src/FurbleGPS.cpp")
        self.assertEqual(findings[0]["id"], "nullPointer")

    def test_test_and_vendor_findings_are_ignored(self):
        findings = CHECKER.parse_cppcheck(
            "tests/host/foo.cpp:1:warning:x:ignored\n"
            ".pio/libdeps/foo/bar.cpp:2:error:y:ignored\n"
        )
        self.assertEqual(findings, [])


class HeaderIncluderTest(unittest.TestCase):
    def test_qualified_header_include_is_found(self):
        result = CHECKER.header_includers(
            ["lib/furble/protocol/CanonTime.h"], ["lib/furble/CanonEOSSmart.cpp"]
        )
        self.assertIn(
            "lib/furble/CanonEOSSmart.cpp",
            result["lib/furble/protocol/CanonTime.h"],
        )

    def test_unrelated_source_is_not_an_includer(self):
        result = CHECKER.header_includers(
            ["lib/furble/protocol/CanonTime.h"], ["src/FurbleGPS.cpp"]
        )
        self.assertEqual(result["lib/furble/protocol/CanonTime.h"], [])


class ClangTidyParseTest(unittest.TestCase):
    def test_warning_is_parsed_with_check(self):
        findings, errors = CHECKER.parse_clang_tidy(
            "src/FurbleUI.cpp:12:3: warning: use of a signed integer operand "
            "[bugprone-signed-bitwise]\n"
        )
        self.assertEqual(errors, [])
        self.assertEqual(len(findings), 1)
        self.assertEqual(findings[0]["check"], "bugprone-signed-bitwise")

    def test_compile_error_is_separate(self):
        findings, errors = CHECKER.parse_clang_tidy(
            "src/FurbleUI.cpp:19:10: error: 'lvgl.h' file not found\n"
        )
        self.assertEqual(findings, [])
        self.assertEqual(len(errors), 1)
        self.assertIn("lvgl.h", errors[0]["message"])

    def test_fatal_error_is_separate(self):
        findings, errors = CHECKER.parse_clang_tidy(
            "src/FurbleUI.cpp:19:10: fatal error: 'lvgl.h' file not found\n"
        )
        self.assertEqual(findings, [])
        self.assertEqual(len(errors), 1)
        self.assertIn("lvgl.h", errors[0]["message"])

    def test_test_findings_are_ignored(self):
        findings, errors = CHECKER.parse_clang_tidy(
            "tests/host/foo.cpp:1:1: warning: ignored [bugprone-x]\n"
        )
        self.assertEqual(findings, [])
        self.assertEqual(errors, [])


if __name__ == "__main__":
    unittest.main()
