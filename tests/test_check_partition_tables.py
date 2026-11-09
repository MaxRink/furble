"""tools/check_partition_tables.py: the core dump partition rule."""

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


if __name__ == "__main__":
    unittest.main()
