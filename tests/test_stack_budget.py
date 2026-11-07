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


# entry -> rec -> rec (self recursion), then rec -> vla, an unbounded alloca
R_CI = r'''graph: { title: "r.cpp"
node: { title: "entry" label: "void entry(void*)\nr.cpp:1:1\n32 bytes (static)" }
node: { title: "rec" label: "void rec(int)\nr.cpp:2:1\n64 bytes (static)" }
node: { title: "vla" label: "void vla(size_t)\nr.cpp:3:1\n48 bytes (dynamic)" }
edge: { sourcename: "entry" targetname: "rec" label: "r.cpp:1:5" }
edge: { sourcename: "rec" targetname: "rec" label: "r.cpp:2:5" }
edge: { sourcename: "rec" targetname: "vla" label: "r.cpp:2:9" }
}
'''
# entry -> a -> b -> a: a two-function cycle
M_CI = r'''graph: { title: "m.cpp"
node: { title: "entry" label: "void entry(void*)\nm.cpp:1:1\n32 bytes (static)" }
node: { title: "a" label: "void a(int)\nm.cpp:2:1\n40 bytes (static)" }
node: { title: "b" label: "void b(int)\nm.cpp:3:1\n24 bytes (static)" }
edge: { sourcename: "entry" targetname: "a" label: "m.cpp:1:5" }
edge: { sourcename: "a" targetname: "b" label: "m.cpp:2:5" }
edge: { sourcename: "b" targetname: "a" label: "m.cpp:3:5" }
}
'''
# entry -> a; a -> b -> a and a -> c -> a: two cycles through one head
N_CI = r'''graph: { title: "n.cpp"
node: { title: "entry" label: "void entry(void*)\nn.cpp:1:1\n32 bytes (static)" }
node: { title: "a" label: "void a(int)\nn.cpp:2:1\n40 bytes (static)" }
node: { title: "b" label: "void b(int)\nn.cpp:3:1\n24 bytes (static)" }
node: { title: "c" label: "void c(int)\nn.cpp:4:1\n16 bytes (static)" }
edge: { sourcename: "entry" targetname: "a" label: "n.cpp:1:5" }
edge: { sourcename: "a" targetname: "b" label: "n.cpp:2:5" }
edge: { sourcename: "b" targetname: "a" label: "n.cpp:3:5" }
edge: { sourcename: "a" targetname: "c" label: "n.cpp:2:9" }
edge: { sourcename: "c" targetname: "a" label: "n.cpp:4:5" }
}
'''
# entry calls elf_mid, which only the ELF defines
E_CI = r'''graph: { title: "e.cpp"
node: { title: "entry" label: "void entry(void*)\ne.cpp:1:1\n32 bytes (static)" }
node: { title: "elf_mid" label: "elf_mid\n<built-in>" shape : ellipse }
edge: { sourcename: "entry" targetname: "elf_mid" label: "e.cpp:1:5" }
}
'''
# Both l32r forms that objdump prints. The parenthesized value is a longcall
# target. The bare form names the literal's slot, and a load into the
# register drops its literal, so neither 4,000-byte function is reached.
LISTING = """
40000000 <elf_leaf>:
40000000:\t004136        \tentry\ta1, 48
40000003:\t000090        \tretw.n

40000010 <elf_mid>:
40000010:\t006136        \tentry\ta1, 64
40000013:\tfffba5        \tcall8\t40000000 <elf_leaf>
40000016:\tfffa81        \tl32r\ta8, 40000100 <elf_mid+0xf0> (40000200 <elf_far>)
40000019:\t0008e0        \tcallx8\ta8
4000001c:\tfff981        \tl32r\ta8, 4033a03c <elf_slot+0x2ec668>
4000001f:\t0008e0        \tcallx8\ta8
40000022:\tfff891        \tl32r\ta9, 40000104 <elf_mid+0xf4> (40000300 <elf_stale>)
40000025:\t0998      \tl32i.n\ta9, a9, 0
40000027:\t0009f0        \tcallx8\ta9
4000002a:\t000090        \tretw.n

40000200 <elf_far>:
40000200:\t00c136        \tentry\ta1, 400
40000203:\t0000a5        \tcall8\t40000210 <elf_far+0x10>
40000206:\t000090        \tretw.n
40000210:\t003136        \tentry\ta1, 96

40000300 <elf_stale>:
40000300:\t7d0136        \tentry\ta1, 4000

40000400 <elf_slot>:
40000400:\t7d0136        \tentry\ta1, 4000
"""


class StackBudgetTest(unittest.TestCase):
    def run_tool(self, stack, indirect=True, extra=(), rules=(), cis=None, manifest_extra=None,
                 listing=None):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            build = tmp / "build"
            build.mkdir()
            cis = cis or (("a.cpp.ci", A_CI), ("b.cpp.ci", B_CI), ("c.cpp.ci", C_CI))
            for name, text in cis:
                (build / name).write_text(text)
            (tmp / "sdkconfig").write_text('CONFIG_IDF_TARGET="esp32"\nCONFIG_STACK=%d\n' % stack)
            manifest = {
                "overhead_bytes": {"esp32": 288},
                "tasks": [{"name": "t", "entry": r"^void entry\(void\*\)$", "stack_bytes": "CONFIG_STACK"}],
                "indirect": ([{"caller": r"^void mid\(\)$", "targets": [r"\(cb_t\*\)$"]}]
                             if indirect else []) + list(rules),
            }
            manifest.update(manifest_extra or {})
            (tmp / "manifest.json").write_text(json.dumps(manifest))
            elf = ["--no-elf"]
            if listing is not None:
                # a fake objdump that prints the crafted listing
                (tmp / "listing.txt").write_text(listing)
                objdump = tmp / "objdump"
                objdump.write_text(f"#!{sys.executable}\nimport sys\n"
                                   f"sys.stdout.write(open({str(tmp / 'listing.txt')!r}).read())\n")
                objdump.chmod(0o755)
                elf = ["--objdump", str(objdump)]
            return subprocess.run(
                [sys.executable, str(TOOL), "--env", "x", "--build-dir", str(build), "--sdkconfig",
                 str(tmp / "sdkconfig"), "--manifest", str(tmp / "manifest.json"), *elf, *extra],
                capture_output=True, text=True)

    def test_resolved_depth_and_overhead(self):
        # 32 + 100 + max(leaf 64, cb_deep 400) + 288 overhead
        r = self.run_tool(820)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        # cb_deep is dynamic,bounded: GCC already reports its maximum, so it passes
        self.assertIn("PASS t: 820 of 820 bytes (532 frames + 288 port overhead)\n", r.stdout)

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

    def test_task_scoped_rule_overrides_the_unscoped_rule(self):
        # On task t, mid dispatches only cb_small, so the comdat leaf (64) is deepest.
        scoped = {"caller": r"^void mid\(\)$", "targets": [r"cb_small"], "tasks": ["t"]}
        r = self.run_tool(484, rules=(scoped,))
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("484 of 484 bytes (196 frames + 288 port overhead)", r.stdout)
        other = dict(scoped, tasks=["other"])
        self.assertEqual(self.run_tool(484, rules=(other,)).returncode, 1)

    def test_stack_override_replays_a_smaller_size(self):
        r = self.run_tool(4096, extra=("--stack", "t=800"))
        self.assertEqual(r.returncode, 1)
        self.assertIn("FAIL t: 820 of 800", r.stdout)

    def test_unbounded_recursion_and_dynamic_frame_fail(self):
        r = self.run_tool(65536, indirect=False, cis=(("r.cpp.ci", R_CI),))
        self.assertEqual(r.returncode, 1, r.stdout + r.stderr)
        self.assertIn("1 recursive (1 unbounded), 1 unbounded dynamic", r.stdout)
        self.assertIn("recursion without a recursion_bounds entry: void rec(int)", r.stdout)
        self.assertIn("dynamic frame without a dynamic_bounds entry: void vla(size_t)", r.stdout)

    def test_manifest_bounds_count_recursion_and_dynamic_frames(self):
        bounds = {"recursion_bounds": [{"function": r"^void rec\(int\)$", "depth": 4, "why": "test"}],
                  "dynamic_bounds": [{"function": r"^void vla", "bytes": 100, "why": "test"}]}
        r_ci = (("r.cpp.ci", R_CI),)
        # 32 + 4 passes of rec (64) + vla (48 + 100) + 288 overhead
        r = self.run_tool(724, indirect=False, cis=r_ci, manifest_extra=bounds)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("PASS t: 724 of 724 bytes (436 frames + 288 port overhead); 1 recursive\n",
                      r.stdout)
        r = self.run_tool(723, indirect=False, cis=r_ci, manifest_extra=bounds)
        self.assertEqual(r.returncode, 1, r.stdout)

    def test_multi_function_cycle_is_charged_depth_passes_once(self):
        bounds = {"recursion_bounds": [{"function": r"^void a\(int\)$", "depth": 3, "why": "test"}]}
        m_ci = (("m.cpp.ci", M_CI),)
        # 32 + 3 passes of (a 40 + b 24) + 288 overhead; charging each member
        # would give 32 + 5 passes = 640.
        r = self.run_tool(512, indirect=False, cis=m_ci, manifest_extra=bounds)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("PASS t: 512 of 512 bytes (224 frames + 288 port overhead); 1 recursive\n",
                      r.stdout)
        self.assertEqual(self.run_tool(511, indirect=False, cis=m_ci,
                                       manifest_extra=bounds).returncode, 1)

    def test_cycles_sharing_a_head_add_their_passes(self):
        bounds = {"recursion_bounds": [{"function": r"^void a\(int\)$", "depth": 3, "why": "test"}]}
        n_ci = (("n.cpp.ci", N_CI),)
        # 32 + a 40 + b 24 + extra passes of both cycles, 2 x 64 + 2 x 56, + 288
        r = self.run_tool(624, indirect=False, cis=n_ci, manifest_extra=bounds)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("PASS t: 624 of 624 bytes (336 frames + 288 port overhead)", r.stdout)
        self.assertEqual(self.run_tool(623, indirect=False, cis=n_ci,
                                       manifest_extra=bounds).returncode, 1)

    def test_known_limit_passes_only_on_its_tasks(self):
        dyn = {"function": r"^void vla", "bytes": 0, "why": "test"}
        known = {"recursion_bounds": [{"function": r"rec", "tasks": ["t"], "why": "test"}],
                 "dynamic_bounds": [dyn]}
        r_ci = (("r.cpp.ci", R_CI),)
        r = self.run_tool(65536, indirect=False, cis=r_ci, manifest_extra=known)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("1 recursive (1 known limit)", r.stdout)
        other = {"recursion_bounds": [{"function": r"rec", "tasks": ["other"], "why": "test"}],
                 "dynamic_bounds": [dyn]}
        r = self.run_tool(65536, indirect=False, cis=r_ci, manifest_extra=other)
        self.assertEqual(r.returncode, 1, r.stdout)

    def test_elf_listing_counts_call8_and_longcall_frames(self):
        # 32 + elf_mid 64 + max(elf_leaf 48, elf_far 400 + its unnamed
        # elf_far+0x10 96) + 288 overhead; the bare l32r and the reloaded
        # register stay unresolved indirect calls
        e_ci = (("e.cpp.ci", E_CI),)
        r = self.run_tool(880, indirect=False, cis=e_ci, listing=LISTING)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("PASS t: 880 of 880 bytes (592 frames + 288 port overhead); "
                      "2 unresolved indirect\n", r.stdout)
        r = self.run_tool(879, indirect=False, cis=e_ci, listing=LISTING)
        self.assertEqual(r.returncode, 1, r.stdout)


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
