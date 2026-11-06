"""The simulator board profile table must match each board's sdkconfig.

sim/memory_profile.cpp keeps one row per modelled board. The main task stack
and PSRAM presence there drifted from the sdkconfig once (headless and
Waveshare listed 8 KiB against a 16 KiB config). This test reads both.
"""

import importlib.util
import json
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
_SPEC = importlib.util.spec_from_file_location("derive_sim_heap", ROOT / "tools/derive_sim_heap.py")
DERIVE = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(DERIVE)

# Simulator profile id to firmware env. Profiles without a firmware env
# (stickc-plus2, tough) are hardware variants of an env row and are skipped.
PROFILE_ENV = {
    "stickc": "m5stick-c",
    "stickc-plus": "m5stick-c-plus",
    "sticks3": "m5stick-s3",
    "core": "m5stack-core",
    "core2": "m5stack-core2",
    "headless-s3": "esp32-s3-headless",
    "waveshare-s3-eth": "waveshare-s3-eth",
}


def profile_rows():
    text = (ROOT / "sim/memory_profile.cpp").read_text()
    table = text[text.index("const profile_budget_t BUDGETS[] = {"):]
    table = table[:table.index("};")]
    rows = {}
    for match in re.finditer(r'\{"([a-z0-9-]+)",\s*([^}]*)\}', table, re.S):
        fields = [field.strip() for field in match.group(2).split(",")]
        rows[match.group(1)] = fields
    return rows


def sdkconfig(env):
    values = {}
    for line in (ROOT / f"sdkconfig.{env}").read_text().splitlines():
        match = re.match(r"^(CONFIG_[A-Z0-9_]+)=(.*)$", line)
        if match:
            values[match.group(1)] = match.group(2)
    return values


class ProfileSdkconfigTest(unittest.TestCase):

    def test_main_stack_and_psram_match_sdkconfig(self):
        rows = profile_rows()
        for profile, env in PROFILE_ENV.items():
            with self.subTest(profile=profile):
                fields = rows[profile]
                config = sdkconfig(env)
                # Field order: internalTotal, psramTotal, flashKb, mainStack.
                self.assertEqual(int(fields[3]), int(config["CONFIG_ESP_MAIN_TASK_STACK_SIZE"]))
                has_psram = fields[1] != "0"
                self.assertEqual(has_psram, config.get("CONFIG_SPIRAM") == "y")


class DerivedHeapTest(unittest.TestCase):

    def test_subtract_merges_adjacent_regions_and_cuts_reservations(self):
        regions = [(0x1000, 0x1000, "a"), (0x2000, 0x1000, "b"), (0x4000, 0x800, "c")]
        reserved = [(0x1800, 0x1900, "x"), (0x3F00, 0x4100, "y")]
        free, largest = DERIVE.subtract(regions, reserved)
        self.assertEqual(free, 0x2800 - 0x100 - 0x100)
        # 0x1900..0x3000 spans the merged a/b boundary.
        self.assertEqual(largest, 0x3000 - 0x1900)

    def test_profile_floors_match_the_derived_board_heap(self):
        boards = json.loads((ROOT / "sim/coverage/board_heap.json").read_text())["boards"]
        budget = json.loads((ROOT / "sim/coverage/memory_budget.json").read_text())["scenarios"]
        for board in boards:
            with self.subTest(profile=board["profile"]):
                entry = budget[f"profile-{board['profile']}"]
                self.assertEqual(entry["min_internal_free"], board["derived_free_upper_bound"])
                self.assertEqual(entry["min_internal_largest"], board["derived_largest_upper_bound"])


if __name__ == "__main__":
    unittest.main()
