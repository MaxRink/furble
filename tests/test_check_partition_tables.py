"""tools/check_partition_tables.py: the core dump and upload offset rules."""

import importlib.util
import sys
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "check_partition_tables", ROOT / "tools" / "check_partition_tables.py")
assert SPEC is not None and SPEC.loader is not None
TOOL = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = TOOL
SPEC.loader.exec_module(TOOL)

MANIFEST = ROOT / "web-installer" / "manifest.tmpl"

HEAD = """nvs,      data, nvs,     0x9000,   0x6000,
otadata,  data, ota,     0xf000,   0x2000,
phy_init, data, phy,     0x11000,  0x1000,
"""
OTA_8M = HEAD + """ota_0,    app,  ota_0,   0x20000,  0x300000,
ota_1,    app,  ota_1,   0x320000, 0x300000,
"""
STOCK_4M = HEAD + """ota_0,    app,  ota_0,   0x20000,  0x1a9000,
ota_1,    app,  ota_1,   0x1d0000, 0x1a9000,
"""
SPEC_8M = TOOL.BoardSpec("t", "t.csv", TOOL.FLASH_8M, 1, 0x300000)
SPEC_4M = TOOL.BoardSpec("t", "t.csv", TOOL.FLASH_4M, 1, 0x1A9000)


class CoredumpTest(unittest.TestCase):
    def check(self, spec, text):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "t.csv"
            path.write_text(text, encoding="utf-8")
            TOOL.check_board(spec, path)

    def test_the_repository_tables_pass(self):
        # The ESP-IDF stock table is not in the repository, so a fixture with
        # its 4 MB layout stands in for it.
        with tempfile.TemporaryDirectory() as tmp:
            stock = Path(tmp) / TOOL.STOCK_PARTITION_CSV
            stock.write_text(STOCK_4M, encoding="utf-8")
            self.assertEqual(TOOL.main(["--stock-csv", str(stock)]), 0)

    def test_coredump_right_after_ota_1_passes(self):
        self.check(SPEC_8M, OTA_8M + "coredump, data, coredump, 0x620000, 0x40000,\n")

    def test_missing_moved_or_resized_coredump_fails(self):
        for tail in ("",
                     "coredump, data, coredump, 0x630000, 0x40000,\n",
                     "coredump, data, coredump, 0x620000, 0x20000,\n"):
            with self.assertRaises(TOOL.CheckError, msg=tail):
                self.check(SPEC_8M, OTA_8M + tail)

    def test_a_4m_table_rejects_a_coredump(self):
        table = STOCK_4M + "coredump, data, coredump, 0x379000, 0x40000,\n"
        with self.assertRaises(TOOL.CheckError):
            self.check(SPEC_4M, table)


class ManifestOffsetTest(unittest.TestCase):
    """The template's app offset is read from the layout table, not a literal.

    The template writes `$APP_OFFSET` rather than a decimal, so a checker that
    greps for a literal beside the file name finds nothing. These tests exercise
    the resolved placeholder, and prove the check still fails when an offset is
    wrong rather than passing because it matched nothing.
    """

    def template(self):
        return MANIFEST.read_text(encoding="utf-8")

    def mutated(self, old, new):
        text = self.template()
        self.assertIn(old, text, f"the template no longer contains {old}")
        return text.replace(old, new)

    def check_upload_offset(self, template_text):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "manifest.tmpl"
            path.write_text(template_text, encoding="utf-8")
            TOOL.check_upload_offset(ROOT, manifest=path)

    def test_the_app_offset_comes_from_the_layout_table(self):
        self.assertEqual(
            TOOL.APP_FLASH_OFFSET, TOOL.web_installer_layout.APP_OFFSET)
        self.assertEqual(TOOL.APP_FLASH_OFFSET, 0x20000)

    def test_the_template_names_the_layout_constants(self):
        tokens = [
            match.group("offset")
            for match in TOOL.MANIFEST_PART_PATTERN.finditer(self.template())
        ]
        self.assertEqual(tokens, [
            "$BOOTLOADER_OFFSET",
            "$PARTITION_TABLE_OFFSET",
            "$OTA_DATA_OFFSET",
            "$APP_OFFSET",
        ])
        self.assertEqual(
            TOOL.resolve_manifest_offset("$APP_OFFSET"), TOOL.APP_FLASH_OFFSET)

    def test_the_repository_template_passes(self):
        TOOL.check_manifest_offsets(self.template(), TOOL.APP_FLASH_OFFSET)
        self.check_upload_offset(self.template())

    def test_a_literal_app_offset_is_rejected(self):
        for literal in ("131072", "0x20000", "65536"):
            with self.subTest(literal=literal):
                text = self.mutated("$APP_OFFSET", literal)
                with self.assertRaises(TOOL.CheckError) as raised:
                    TOOL.check_manifest_offsets(text, TOOL.APP_FLASH_OFFSET)
                self.assertIn("name the layout constants", str(raised.exception))
                self.assertIn(literal, str(raised.exception))

    def test_a_wrong_placeholder_is_rejected(self):
        text = self.mutated("$APP_OFFSET", "$OTA_DATA_OFFSET")
        with self.assertRaises(TOOL.CheckError) as raised:
            TOOL.check_manifest_offsets(text, TOOL.APP_FLASH_OFFSET)
        self.assertIn("app offset differs from ota_0", str(raised.exception))
        self.assertIn("0xf000", str(raised.exception))
        self.assertIn("0x20000", str(raised.exception))

    def test_a_placeholder_outside_the_layout_table_is_rejected(self):
        text = self.mutated("$APP_OFFSET", "$APP_OFFSET_TYPO")
        with self.assertRaises(TOOL.CheckError) as raised:
            TOOL.check_manifest_offsets(text, TOOL.APP_FLASH_OFFSET)
        self.assertIn("APP_OFFSET_TYPO", str(raised.exception))
        self.assertIn("not an integer offset constant", str(raised.exception))

    def test_a_moved_layout_table_fails_against_the_template(self):
        # The template would then resolve to an offset the partition tables do
        # not use, which is the drift the literal used to hide.
        original = TOOL.web_installer_layout.APP_OFFSET
        TOOL.web_installer_layout.APP_OFFSET = 0x40000
        try:
            with self.assertRaises(TOOL.CheckError) as raised:
                TOOL.check_manifest_offsets(
                    self.template(), TOOL.APP_FLASH_OFFSET)
            self.assertIn("app offset differs from ota_0", str(raised.exception))
        finally:
            TOOL.web_installer_layout.APP_OFFSET = original

    def test_the_upload_check_fails_on_a_wrong_template_offset(self):
        with self.assertRaises(TOOL.CheckError) as raised:
            self.check_upload_offset(
                self.mutated("$APP_OFFSET", "$OTA_DATA_OFFSET"))
        self.assertIn("app offset differs from ota_0", str(raised.exception))

    def test_a_template_without_parts_is_rejected(self):
        with self.assertRaises(TOOL.CheckError) as raised:
            TOOL.check_manifest_offsets("{\"builds\": []}", TOOL.APP_FLASH_OFFSET)
        self.assertIn("no flash parts found", str(raised.exception))


if __name__ == "__main__":
    unittest.main()
