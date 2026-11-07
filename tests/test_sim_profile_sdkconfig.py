"""The simulator board profile table must match each board's sdkconfig.

sim/memory_profile.cpp keeps one row per modelled board. The main task stack
and PSRAM presence there drifted from the sdkconfig once (headless and
Waveshare listed 8 KiB against a 16 KiB config). This test reads both.
"""

import importlib.util
import json
import math
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

    def test_draw_buffers_follow_the_firmware_rule(self):
        rows = profile_rows()
        for profile, env in PROFILE_ENV.items():
            _, width, height, _ = DERIVE.ENVS.get(env, (None, 0, 0, None))
            if not width:
                continue
            with self.subTest(profile=profile):
                fields = rows[profile]
                # Field 8 is one buffer's bytes, field 9 the single-buffer flag.
                self.assertEqual(math.prod(int(term) for term in fields[8].split("*")),
                                 DERIVE.draw_buffer_bytes(width, height))
                self.assertEqual(fields[9] == "true", DERIVE.single_draw_buffer(width))

    def test_draw_buffer_rule_matches_the_host_test_pins(self):
        # tests/host/ui_memory_test.cpp pins these values for the C++ rule.
        self.assertEqual(DERIVE.draw_buffer_bytes(320, 240), 10240)
        self.assertEqual(DERIVE.draw_buffer_bytes(135, 240), 2160)
        self.assertEqual(DERIVE.draw_buffer_bytes(80, 160), 800)
        self.assertEqual(DERIVE.draw_buffer_bytes(240, 135), 4320)
        self.assertEqual(DERIVE.draw_buffer_bytes(0, 0), 2)
        self.assertEqual(DERIVE.draw_buffer_bytes(1000, 1000), 10240)


class DerivedHeapTest(unittest.TestCase):

    def test_every_profile_is_derived_or_explicitly_underived(self):
        spec = importlib.util.spec_from_file_location(
            "run_memory_profile", ROOT / "sim/scripts/run-memory-profile.py")
        runner = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(runner)
        derived = set(runner.derived_pools())
        self.assertEqual(derived | runner.UNDERIVED_PROFILES, set(runner.BOARDS))
        self.assertFalse(derived & runner.UNDERIVED_PROFILES)

    def test_subtract_merges_adjacent_regions_and_cuts_reservations(self):
        regions = [(0x1000, 0x1000, "a"), (0x2000, 0x1000, "b"), (0x4000, 0x800, "c")]
        reserved = [(0x1800, 0x1900, "x"), (0x3F00, 0x4100, "y")]
        free, largest = DERIVE.subtract(regions, reserved)
        self.assertEqual(free, 0x2800 - 0x100 - 0x100)
        # 0x1900..0x3000 spans the merged a/b boundary.
        self.assertEqual(largest, 0x3000 - 0x1900)

    def test_subset_run_keeps_the_other_boards(self):
        import tempfile
        from unittest import mock
        boards = json.loads((ROOT / "sim/coverage/board_heap.json").read_text())["boards"]
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "board_heap.json"
            output.write_text(json.dumps({"version": 1, "boards": boards}))
            changed = dict(boards[0], derived_free_upper_bound=1)
            with mock.patch.object(DERIVE, "derive", return_value=changed):
                DERIVE.main(["--env", boards[0]["env"], "--output", str(output)])
            written = json.loads(output.read_text())["boards"]
        self.assertEqual([board["env"] for board in written], [board["env"] for board in boards])
        self.assertEqual(written[0]["derived_free_upper_bound"], 1)
        self.assertEqual(written[1:], boards[1:])

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
