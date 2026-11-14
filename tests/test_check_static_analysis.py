"""Tests for the static-analysis gate parsing."""

import importlib.util
import json
import re
from pathlib import Path
import sys
import tempfile
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

    def test_absolute_path_is_normalized(self):
        findings = CHECKER.parse_cppcheck(
            f"{ROOT}/src/FurbleGPS.cpp:42:warning:nullPointer:x\n"
        )
        self.assertEqual(len(findings), 1)
        self.assertEqual(findings[0]["file"], "src/FurbleGPS.cpp")


class ProductionPathTest(unittest.TestCase):
    def test_c_helper_is_a_production_source(self):
        sources, headers = CHECKER.production_paths(["sim/env_mutation_guard.c"])
        self.assertEqual(sources, ["sim/env_mutation_guard.c"])
        self.assertEqual(headers, [])


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
    def test_third_party_headers_under_sim_are_not_findings(self):
        # CI clones the simulator's dependencies into sim/.pio/libdeps/sim, so
        # an M5GFX header there must not count as furble code.
        root = str(CHECKER.ROOT)
        findings, errors = CHECKER.parse_clang_tidy(
            f"{root}/sim/.pio/libdeps/sim/M5GFX/src/lgfx/v1/LGFXBase.hpp:965:5: "
            "error: deleting a pointer [cppcoreguidelines-owning-memory]\n"
            f"{root}/sim/.pio/libdeps/sim/M5GFX/src/lgfx/v1/Panel.hpp:98:48: "
            "error: unknown type name 'x' [clang-diagnostic-error]\n"
        )
        self.assertEqual(findings, [])
        self.assertEqual(errors, [])
        cpp = CHECKER.parse_cppcheck(
            "sim/.pio/libdeps/sim/lvgl/src/core/lv_obj.c:10:1: warning: x [y]\n"
        )
        self.assertEqual(cpp, [])
        self.assertTrue(CHECKER.is_production("sim/sdl_lifecycle.cpp"))
        self.assertFalse(CHECKER.is_production("sim/.pio/libdeps/sim/M5GFX/a.hpp"))

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

    def test_absolute_path_is_normalized(self):
        findings, errors = CHECKER.parse_clang_tidy(
            f"{ROOT}/src/FurbleUI.cpp:1:1: warning: x [bugprone-x]\n"
        )
        self.assertEqual(errors, [])
        self.assertEqual(len(findings), 1)
        self.assertEqual(findings[0]["file"], "src/FurbleUI.cpp")

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


class ClangTidyRunTest(unittest.TestCase):
    """run_clang_tidy against a fake clang-tidy that prints its arguments."""

    def run_tidy(self, dirname, targets):
        with tempfile.TemporaryDirectory() as tmp:
            build = Path(tmp) / dirname
            build.mkdir()
            entry = {"directory": str(ROOT), "file": str(ROOT / "src" / "main.cpp"),
                     "arguments": ["clang++", "-c", "src/main.cpp"]}
            (build / "compile_commands.json").write_text(json.dumps([entry]))
            tidy = Path(tmp) / "clang-tidy"
            tidy.write_text(f"#!{sys.executable}\nimport sys\nprint(' '.join(sys.argv[1:]))\n")
            tidy.chmod(0o755)
            return CHECKER.run_clang_tidy(targets, [build], str(tidy), 1)

    def test_source_without_a_compile_command_is_reported(self):
        _, _, skipped = self.run_tidy("host", ["src/main.cpp", "src/FurbleGPS.cpp"])
        self.assertEqual(skipped, ["src/FurbleGPS.cpp"])

    def test_named_exemption_is_not_reported(self):
        _, _, skipped = self.run_tidy("host", ["sim/env_mutation_guard.c"])
        self.assertEqual(skipped, [])

    def run_fake(self, script):
        with tempfile.TemporaryDirectory() as tmp:
            build = Path(tmp) / "host"
            build.mkdir()
            entry = {"directory": str(ROOT), "file": str(ROOT / "src" / "main.cpp"),
                     "arguments": ["clang++", "-c", "src/main.cpp"]}
            (build / "compile_commands.json").write_text(json.dumps([entry]))
            tidy = Path(tmp) / "clang-tidy"
            tidy.write_text(f"#!{sys.executable}\nimport sys\n{script}\n")
            tidy.chmod(0o755)
            return CHECKER.run_clang_tidy(["src/main.cpp"], [build], str(tidy), 1)

    def test_promoted_check_in_a_vendored_header_is_not_a_compile_error(self):
        findings, errors, _ = self.run_fake(
            "print('/x/.pio/libdeps/M5GFX/a.hpp:1:1: error: deleting [cppcoreguidelines-owning-"
            "memory,-warnings-as-errors]'); sys.exit(1)")
        self.assertEqual((findings, errors), ([], []))

    def test_quoted_source_line_with_error_is_not_a_compile_error(self):
        # clang-tidy quotes the source under a diagnostic; M5GFX's result.hpp
        # has "failure(E2&& error)", which read as a real error (issue #463)
        findings, errors, _ = self.run_fake(
            "print('/x/.pio/libdeps/M5GFX/r.hpp:1:1: error: x [bugprone-y,-warnings-as-errors]');"
            "print(' 3576 | failure(E2&& error) error: here');"
            "print('      |          ^'); sys.exit(1)")
        self.assertEqual((findings, errors), ([], []))

    def test_header_filter_is_anchored_to_the_checkout(self):
        # an unanchored filter matched src/ inside .pio/libdeps/<env>/M5GFX
        flag = CHECKER.HEADER_FILTER.split("=", 1)[1]
        pattern = re.compile(flag)
        root = str(CHECKER.ROOT)
        for path in ("src/FurbleGPS.h", f"{root}/include/FurbleGPS.h", "lib/furble/Camera.h",
                     "sim/driver.h", f"{root}/tests/host/../../include/FurbleGPS.h",
                     f"{root}/tests/host/../../lib/furble/Camera.h",
                     f"{root}/sim/../include/FurbleGPS.h"):
            self.assertTrue(pattern.search(path), path)
        for path in (".pio/libdeps/m5stick-s3/M5GFX/src/lgfx/v1/LGFXBase.hpp",
                     f"{root}/.pio/libdeps/m5stick-s3/M5GFX/src/lgfx/v1/LGFXBase.hpp",
                     f"{root}/managed_components/lvgl__lvgl/src/lvgl.h",
                     f"{root}/sim/.pio/libdeps/sim/M5GFX/src/lgfx/v1/LGFXBase.hpp",
                     f"{root}/sim/.pio/libdeps/sim/M5GFX/src/lgfx/v1/platforms/../misc/x.hpp"):
            self.assertIsNone(pattern.search(path), path)

    def test_header_filter_reaches_clang_tidy(self):
        with tempfile.TemporaryDirectory() as tmp:
            build = Path(tmp) / "host"
            build.mkdir()
            entry = {"directory": str(ROOT), "file": str(ROOT / "src" / "main.cpp"),
                     "arguments": ["clang++", "-c", "src/main.cpp"]}
            (build / "compile_commands.json").write_text(json.dumps([entry]))
            log = Path(tmp) / "args"
            tidy = Path(tmp) / "tidy"
            tidy.write_text(f"#!{sys.executable}\nimport sys\n"
                            f"open({str(log)!r}, 'w').write('\\n'.join(sys.argv[1:]))\n")
            tidy.chmod(0o755)
            CHECKER.run_clang_tidy(["src/main.cpp"], [build], str(tidy), 1)
            self.assertIn(CHECKER.HEADER_FILTER, log.read_text().splitlines())

    def test_silent_failure_and_real_error_are_compile_errors(self):
        _, errors, _ = self.run_fake("sys.exit(1)")
        self.assertEqual(len(errors), 1)
        _, errors, _ = self.run_fake("print('/x/vendor.h:1:1: error: unknown type name'); sys.exit(1)")
        self.assertEqual(len(errors), 1)

    def test_firmware_database_gets_no_simulator_shims(self):
        with tempfile.TemporaryDirectory() as tmp:
            for name, shimmed in (("host", True), ("firmware-x", False)):
                build = Path(tmp) / name
                build.mkdir()
                entry = {"directory": str(ROOT), "file": str(ROOT / "src" / "main.cpp"),
                         "arguments": ["clang++", "-c", "src/main.cpp"]}
                (build / "compile_commands.json").write_text(json.dumps([entry]))
                log = Path(tmp) / f"{name}.args"
                tidy = Path(tmp) / f"tidy-{name}"
                tidy.write_text(f"#!{sys.executable}\nimport sys\n"
                                f"open({str(log)!r}, 'w').write(' '.join(sys.argv[1:]))\n")
                tidy.chmod(0o755)
                CHECKER.run_clang_tidy(["src/main.cpp"], [build], str(tidy), 1)
                self.assertEqual("sim/shim" in log.read_text(), shimmed, name)


if __name__ == "__main__":
    unittest.main()
