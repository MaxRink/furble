"""tools/stack_budget.py on a synthetic call graph."""

import json
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TOOL = ROOT / "tools" / "stack_budget.py"

# entry -> mid (direct) -> leaf; mid also makes one indirect call that the
# manifest resolves to every (cb_t*) callback. cb_deep is the deepest.
A_CI = r'''graph: { title: "a.cpp"
node: { title: "entry" label: "void entry(void*)\na.cpp:1:1\n32 bytes (static)" }
node: { title: "a.cpp:mid" label: "void mid()\na.cpp:2:1\n100 bytes (static)" }
edge: { sourcename: "entry" targetname: "a.cpp:mid" label: "a.cpp:1:5" }
node: { title: "leaf" label: "leaf\n<built-in>" shape : ellipse }
edge: { sourcename: "a.cpp:mid" targetname: "leaf" label: "a.cpp:2:5" }
node: { title: "__indirect_call" label: "Indirect Call Placeholder" shape : ellipse }
edge: { sourcename: "a.cpp:mid" targetname: "__indirect_call" label: "a.cpp:2:9" }
}
'''
B_CI = r'''graph: { title: "b.cpp"
node: { title: "leaf" label: "void leaf()\nb.cpp:1:1\n48 bytes (static)" }
node: { title: "cb_small" label: "void cb_small(cb_t*)\nb.cpp:2:1\n16 bytes (static)" }
node: { title: "cb_deep" label: "void cb_deep(cb_t*)\nb.cpp:3:1\n400 bytes (dynamic,bounded)" }
}
'''
# comdat: the same inline function in a second object with a bigger frame wins
C_CI = r'''graph: { title: "c.cpp"
node: { title: "leaf" label: "void leaf()\nc.cpp:1:1\n64 bytes (static)" }
}
'''


class StackBudgetTest(unittest.TestCase):
    def run_tool(self, stack, indirect=True, extra=()):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            build = tmp / "build"
            build.mkdir()
            for name, text in (("a.cpp.ci", A_CI), ("b.cpp.ci", B_CI), ("c.cpp.ci", C_CI)):
                (build / name).write_text(text)
            (tmp / "sdkconfig").write_text('CONFIG_IDF_TARGET="esp32"\nCONFIG_STACK=%d\n' % stack)
            manifest = {
                "overhead_bytes": {"esp32": 288},
                "tasks": [{"name": "t", "entry": r"^void entry\(void\*\)$", "stack_bytes": "CONFIG_STACK"}],
                "indirect": [{"caller": r"^void mid\(\)$", "targets": [r"\(cb_t\*\)$"]}] if indirect else [],
            }
            (tmp / "manifest.json").write_text(json.dumps(manifest))
            return subprocess.run(
                [sys.executable, str(TOOL), "--env", "x", "--build-dir", str(build), "--sdkconfig",
                 str(tmp / "sdkconfig"), "--manifest", str(tmp / "manifest.json"), *extra],
                capture_output=True, text=True)

    def test_resolved_depth_and_overhead(self):
        # 32 + 100 + max(leaf 64, cb_deep 400) + 288 overhead
        r = self.run_tool(820)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("PASS t: 820 of 820 bytes (532 frames + 288 port overhead); 1 dynamic", r.stdout)

    def test_fails_one_byte_short_and_prints_path(self):
        r = self.run_tool(819)
        self.assertEqual(r.returncode, 1)
        self.assertIn("FAIL t: 820 of 819", r.stdout)
        self.assertIn("cb_deep(cb_t*)", r.stdout)

    def test_unresolved_indirect_is_reported_not_followed(self):
        # without the rule the indirect call is listed and the comdat leaf (64) is deepest
        r = self.run_tool(484, indirect=False)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("484 of 484 bytes (196 frames + 288 port overhead); 1 unresolved indirect", r.stdout)

    def test_stack_override_replays_a_smaller_size(self):
        r = self.run_tool(4096, extra=("--stack", "t=800"))
        self.assertEqual(r.returncode, 1)
        self.assertIn("FAIL t: 820 of 800", r.stdout)


class ManifestSourceTest(unittest.TestCase):
    """The stack sizes in the manifests must be the ones the firmware creates."""

    def test_numeric_stacks_match_their_cited_source(self):
        manifest = json.loads((ROOT / "tools" / "stack_budget.json").read_text())
        for task in manifest["tasks"]:
            if isinstance(task["stack_bytes"], str):
                continue
            with self.subTest(task=task["name"]):
                path, pattern = task["source"]
                found = re.findall(pattern, (ROOT / path).read_text())
                self.assertEqual(found, [str(task["stack_bytes"])], f"{path}: {pattern}")

    def test_memory_budget_components_match_the_task_stacks(self):
        stacks = {t["name"]: t["stack_bytes"]
                  for t in json.loads((ROOT / "tools" / "stack_budget.json").read_text())["tasks"]}
        component_task = {"mqtt_owner": "mqtt", "mqtt_worker": "mqtt_client", "companion_task": "companion"}
        budget = json.loads((ROOT / "sim" / "coverage" / "memory_budget.json").read_text())
        checked = 0
        for name, scenario in budget["scenarios"].items():
            for component, task in component_task.items():
                if component in scenario.get("components", {}):
                    with self.subTest(scenario=name, component=component):
                        self.assertEqual(scenario["components"][component], stacks[task])
                    checked += 1
        self.assertGreater(checked, 0)


if __name__ == "__main__":
    unittest.main()
