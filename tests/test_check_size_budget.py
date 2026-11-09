"""tools/check_size_budget.py: ELF section sizes, budget checks and slots."""

import importlib.util
import json
from pathlib import Path
import re
import struct
import tempfile
import unittest
import unittest.mock

ROOT = Path(__file__).resolve().parents[1]


def load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / "tools" / f"{name}.py")
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


TOOL = load("check_size_budget")
PARTITIONS = load("check_partition_tables")


def elf(sections):
    """A minimal ELF32 LE image whose section headers carry the given sizes."""
    names = b"\0" + b"".join(n.encode() + b"\0" for n, _ in sections) + b".shstrtab\0"
    offsets, cursor = [], 1
    for name, _ in sections:
        offsets.append(cursor)
        cursor += len(name) + 1
    table_name = cursor
    shoff = 52 + len(names)
    header = bytearray(52)
    header[:6] = b"\x7fELF\x01\x01"
    struct.pack_into("<I", header, 0x20, shoff)
    struct.pack_into("<HHH", header, 0x2E, 40, len(sections) + 2, len(sections) + 1)
    entries = [bytes(40)]  # the null section
    for (name, size), offset in zip(sections, offsets):
        entries.append(struct.pack("<IIIIII", offset, 1, 0, 0, 0, size) + bytes(16))
    entries.append(struct.pack("<IIIIII", table_name, 3, 0, 0, 52, len(names)) + bytes(16))
    return bytes(header) + names + b"".join(entries)


class SectionsTest(unittest.TestCase):
    def test_dram_and_iram_sum_their_sections(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "firmware.elf"
            path.write_bytes(elf([(".dram0.data", 100), (".dram0.bss", 200), (".noinit", 4),
                                  (".iram0.text", 3000), (".iram0.vectors", 1024),
                                  (".flash.text", 999999)]))
            sizes = TOOL.sections(path)
        self.assertEqual(sum(sizes.get(n, 0) for n in TOOL.DRAM), 304)
        self.assertEqual(sum(s for n, s in sizes.items() if n.startswith(".iram0.")), 4024)


class CheckTest(unittest.TestCase):
    BUDGET = {"flash_slot": 1000, "dram_bytes": 2048, "iram_bytes": 4096}

    def test_within_budget_passes_and_one_byte_over_fails(self):
        self.assertEqual(TOOL.check(self.BUDGET, {"flash": 1000, "dram": 2048, "iram": 4096}), [])
        errors = TOOL.check(self.BUDGET, {"flash": 1001, "dram": 2049, "iram": 4096})
        self.assertEqual(len(errors), 2)
        self.assertIn("flash 1,001 bytes exceeds flash_slot 1,000 by 1", errors[0])

    def test_round_up_is_to_the_next_kib(self):
        self.assertEqual(TOOL.round_up(1024), 1024)
        self.assertEqual(TOOL.round_up(1025), 2048)

    def test_budget_leaves_at_least_the_headroom(self):
        self.assertEqual(TOOL.budget_for(115668), 116736)
        self.assertEqual(TOOL.budget_for(1024 - 512), 1024)
        self.assertEqual(TOOL.budget_for(1024 - 511), 2048)

    def test_a_missing_build_fails_without_a_traceback(self):
        with unittest.mock.patch.object(TOOL, "measure", side_effect=FileNotFoundError(2, "x", "firmware.elf")):
            with unittest.mock.patch("builtins.print") as out:
                self.assertEqual(TOOL.main(["--env", "m5stick-c"]), 1)
        self.assertIn("FAIL m5stick-c: not built", out.call_args_list[0].args[0])


class BudgetFileTest(unittest.TestCase):
    """Every recorded slot is the app partition of the CSV it names."""

    def test_every_platformio_env_has_a_budget(self):
        config = (ROOT / "platformio.ini").read_text(encoding="utf-8")
        envs = set(re.findall(r"(?m)^\s*\[env:([^\]]+)\]\s*$", config))
        budgets = json.loads(TOOL.BUDGET.read_text(encoding="utf-8"))["envs"]
        self.assertEqual(envs, set(budgets))

    def test_flash_slots_match_the_partition_tables(self):
        budgets = json.loads(TOOL.BUDGET.read_text(encoding="utf-8"))["envs"]
        for env, budget in budgets.items():
            name = budget["partitions"]
            if name == PARTITIONS.STOCK_PARTITION_CSV:
                try:
                    path = PARTITIONS.find_stock_csv(ROOT, None)
                except PARTITIONS.CheckError:
                    continue  # the ESP-IDF package is not installed here
            else:
                path = ROOT / name
            apps = [p for p in PARTITIONS.parse_csv(path) if p.subtype in ("ota_0", "factory")]
            self.assertEqual(budget["flash_slot"], apps[0].size, env)
            self.assertGreater(budget["dram_bytes"], 0, env)
            self.assertGreater(budget["iram_bytes"], 0, env)


if __name__ == "__main__":
    unittest.main()
