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
# entry -> a -> {big, x}, x -> a, entry -> z -> x: x is first reached inside
# the open a-x cycle and later again under z
X_CI = r'''graph: { title: "x.cpp"
node: { title: "entry" label: "void entry(void*)\nx.cpp:1:1\n32 bytes (static)" }
node: { title: "a" label: "void a(int)\nx.cpp:2:1\n16 bytes (static)" }
node: { title: "big" label: "void big()\nx.cpp:3:1\n300 bytes (static)" }
node: { title: "x" label: "void x(int)\nx.cpp:4:1\n8 bytes (static)" }
node: { title: "z" label: "void z()\nx.cpp:5:1\n400 bytes (static)" }
edge: { sourcename: "entry" targetname: "a" label: "x.cpp:1:5" }
edge: { sourcename: "entry" targetname: "z" label: "x.cpp:1:9" }
edge: { sourcename: "a" targetname: "big" label: "x.cpp:2:5" }
edge: { sourcename: "a" targetname: "x" label: "x.cpp:2:9" }
edge: { sourcename: "x" targetname: "a" label: "x.cpp:4:5" }
edge: { sourcename: "z" targetname: "x" label: "x.cpp:5:5" }
}
'''
# entry -> abort -> panic_abort; the handler is reached by an exception and
# can itself abort, a nested panic
P_CI = r'''graph: { title: "p.c"
node: { title: "entry" label: "void entry(void*)\np.c:1:1\n32 bytes (static)" }
node: { title: "abort" label: "abort\np.c:2:1\n48 bytes (static)" }
node: { title: "panic_abort" label: "panic_abort\np.c:3:1\n16 bytes (static)" }
node: { title: "xt_unhandled_exception" label: "xt_unhandled_exception\np.c:4:1\n208 bytes (static)" }
edge: { sourcename: "entry" targetname: "abort" label: "p.c:1:5" }
edge: { sourcename: "abort" targetname: "panic_abort" label: "p.c:2:5" }
edge: { sourcename: "xt_unhandled_exception" targetname: "abort" label: "p.c:4:5" }
}
'''
# entry calls elf_mid, which only the ELF defines
E_CI = r'''graph: { title: "e.cpp"
node: { title: "entry" label: "void entry(void*)\ne.cpp:1:1\n32 bytes (static)" }
node: { title: "elf_mid" label: "elf_mid\n<built-in>" shape : ellipse }
edge: { sourcename: "entry" targetname: "elf_mid" label: "e.cpp:1:5" }
}
'''
# entry calls the D1 destructor, which aliases a D2 body compiled here
D_CI = r'''graph: { title: "d.cpp"
node: { title: "entry" label: "void entry(void*)\nd.cpp:1:1\n32 bytes (static)" }
node: { title: "d.cpp:_ZN1XD2Ev" label: "X::~X()\nd.cpp:2:1\n16 bytes (static)" }
edge: { sourcename: "entry" targetname: "_ZN1XD1Ev" label: "d.cpp:1:5" }
}
'''
# objdump labels the shared address by the D1 alias; the bytes after its retw
# decode as a call into a 4,000-byte function that must not be followed.
ALIAS_LISTING = """
40000000 <_ZN1XD1Ev>:
40000000:\t004136        \tentry\ta1, 32
40000003:\t000090        \tretw.n
40000006:\tfffba5        \tcall8\t40000300 <elf_stale>

40000300 <elf_stale>:
40000300:\t7d0136        \tentry\ta1, 4000
"""
ALIAS_SYMBOLS = """
40000000  w    F .flash.text\t00000006 _ZN1XD1Ev
40000000  w    F .flash.text\t00000006 _ZN1XD2Ev
"""
# A class with virtual bases: the complete-object D1 is its own code, at its
# own address, and must be parsed even though the D2 body is compiled here.
DISTINCT_LISTING = """
40000000 <_ZN1XD1Ev>:
40000000:\t006136        \tentry\ta1, 48
40000003:\t000090        \tretw.n
"""
DISTINCT_SYMBOLS = """
40000000  w    F .flash.text\t00000006 _ZN1XD1Ev
40000500  w    F .flash.text\t00000006 _ZN1XD2Ev
"""
# The WiFi blob's config_get_wifi_task_stack_size is 0x1e bytes. The unnamed
# static function after it starts behind alignment padding, objdump misdecodes
# its entry, and its call8 is listed under the getter. Past the symbol size,
# that call is not the getter's.
TAIL_LISTING = """
40000010 <elf_mid>:
40000010:\t006136        \tentry\ta1, 64
40000013:\t000090        \tretw.n
40000016:\t360000        \tee.ldf.64.xp\tf0, f3, a0, a0
40000019:\tfffba5        \tcall8\t40000300 <elf_stale>

40000300 <elf_stale>:
40000300:\t7d0136        \tentry\ta1, 4000
"""
TAIL_SYMBOLS = """
40000010 g     F .flash.text\t00000006 elf_mid
40000300 g     F .flash.text\t00000010 elf_stale
"""
# A symbol without an entry of its own (a call0 leaf), then an unnamed
# windowed function past its size that callers reach by offset. The boundary
# entry must start elf_mid+0x8, not become elf_mid's own frame.
OFFSET_CI = r'''graph: { title: "o.cpp"
node: { title: "entry" label: "void entry(void*)\no.cpp:1:1\n32 bytes (static)" }
node: { title: "elf_mid+0x8" label: "elf_mid+0x8\n<built-in>" shape : ellipse }
edge: { sourcename: "entry" targetname: "elf_mid+0x8" label: "o.cpp:1:5" }
}
'''
BOUNDARY_LISTING = """
40000010 <elf_mid>:
40000010:\t000090        \tret.n
40000012:\tfffba5        \tcall8\t40000018 <elf_mid+0x8>
40000018:\t00c136        \tentry\ta1, 96
4000001b:\tfffba5        \tcall8\t40000200 <elf_far>

40000200 <elf_far>:
40000200:\t00c136        \tentry\ta1, 400
"""
BOUNDARY_SYMBOLS = """
40000010 g     F .flash.text\t00000008 elf_mid
40000200 g     F .flash.text\t00000010 elf_far
"""
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
40000017:\t130800        \twsr.ps\ta8
40000018:\t400800        \tssl\ta8
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
                 listing=None, symbols=""):
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
                # a fake objdump: -t prints the symbol table, -d the listing
                (tmp / "listing.txt").write_text(listing)
                (tmp / "symbols.txt").write_text(symbols)
                objdump = tmp / "objdump"
                objdump.write_text(f"#!{sys.executable}\nimport sys\n"
                                   f"name = 'symbols.txt' if '-t' in sys.argv else 'listing.txt'\n"
                                   f"sys.stdout.write(open({str(tmp)!r} + '/' + name).read())\n")
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

    def test_cycles_in_one_component_charge_every_member_per_pass(self):
        bounds = {"recursion_bounds": [{"function": r"^void a\(int\)$", "depth": 3, "why": "test"}]}
        n_ci = (("n.cpp.ci", N_CI),)
        # a, b and c form one component: 32 + 3 passes of (40 + 24 + 16) + 288
        r = self.run_tool(560, indirect=False, cis=n_ci, manifest_extra=bounds)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("PASS t: 560 of 560 bytes (272 frames + 288 port overhead)", r.stdout)
        self.assertEqual(self.run_tool(559, indirect=False, cis=n_ci,
                                       manifest_extra=bounds).returncode, 1)
        # two rules in one component add their extra passes: 1 + 2 + 1 = 4
        bounds["recursion_bounds"].append({"function": r"^void c\(int\)$", "depth": 2, "why": "t"})
        r = self.run_tool(640, indirect=False, cis=n_ci, manifest_extra=bounds)
        self.assertIn("PASS t: 640 of 640 bytes (352 frames + 288 port overhead)", r.stdout)

    def test_depth_behind_a_cut_cycle_is_found_on_another_path(self):
        # The memoized walk reached x first inside the open a-x cycle, cut
        # x -> a and reused x's 8 bytes under z. The real path is
        # entry -> z -> x -> a -> big: 32 + 400 + 8 + 16 + 300 + 288.
        bounds = {"recursion_bounds": [{"function": r"^void a\(int\)$", "depth": 1, "why": "test"}]}
        r = self.run_tool(1044, indirect=False, cis=(("x.cpp.ci", X_CI),), manifest_extra=bounds)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("PASS t: 1044 of 1044 bytes (756 frames + 288 port overhead)", r.stdout)
        r = self.run_tool(1043, indirect=False, cis=(("x.cpp.ci", X_CI),), manifest_extra=bounds)
        self.assertEqual(r.returncode, 1, r.stdout)

    def test_infeasible_call_is_not_followed(self):
        # Uncut, entry -> z -> x -> a -> big is 1,044 bytes (see above).
        # Cutting z -> x leaves z alone: 32 + 400 + 288 = 720.
        cut = {"infeasible_calls": [{"caller": r"^void z\(\)$", "callee": r"^void x\(int\)$",
                                     "why": "test"}],
               "recursion_bounds": [{"function": r"^void a\(int\)$", "depth": 1, "why": "test"}]}
        r = self.run_tool(720, indirect=False, cis=(("x.cpp.ci", X_CI),), manifest_extra=cut)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("PASS t: 720 of 720 bytes (432 frames + 288 port overhead)", r.stdout)

    def test_infeasible_calls_cite_their_guard(self):
        manifest = json.loads((ROOT / "tools" / "stack_budget.json").read_text())
        for rule in manifest["infeasible_calls"]:
            with self.subTest(caller=rule["caller"]):
                self.assertRegex(rule["why"], r"\.(c|h|cpp):\d+")

    def test_panic_handler_counts_on_the_faulting_stack(self):
        # entry -> abort -> panic_abort, whose ill instruction enters the
        # handler on the same stack. The handler's own abort is followed to
        # abort (48) but not into a second panic: handler 208 + 48 = 256,
        # task 32 + 48 + 16 + 256 + 288 overhead.
        p_ci = (("p.cpp.ci", P_CI),)
        panic = {"panic": {"abort": "^panic_abort$", "handler": "^xt_unhandled_exception$",
                           "why": "test"}}
        r = self.run_tool(640, indirect=False, cis=p_ci, manifest_extra=panic)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("PASS panic handler: 256 bytes from xt_unhandled_exception\n", r.stdout)
        self.assertIn("PASS t: 640 of 640 bytes (352 frames + 288 port overhead)", r.stdout)
        self.assertEqual(self.run_tool(639, indirect=False, cis=p_ci,
                                       manifest_extra=panic).returncode, 1)
        # without the rule the handler is invisible: 32 + 48 + 16 + 288
        self.assertIn("PASS t: 384 of 384 bytes", self.run_tool(384, indirect=False, cis=p_ci).stdout)
        # the real manifest enters the ESP-IDF handler from panic_abort
        manifest = json.loads((ROOT / "tools" / "stack_budget.json").read_text())
        self.assertEqual(manifest["panic"]["abort"], "^panic_abort$")
        self.assertEqual(manifest["panic"]["handler"], "^xt_unhandled_exception$")

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

    def test_elf_alias_of_a_compiled_body_is_not_parsed(self):
        # 32 + the compiled D2 body 16 + 288 overhead
        r = self.run_tool(336, indirect=False, cis=(("d.cpp.ci", D_CI),), listing=ALIAS_LISTING,
                          symbols=ALIAS_SYMBOLS)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("PASS t: 336 of 336 bytes (48 frames + 288 port overhead)", r.stdout)

    def test_elf_call_past_the_symbol_size_is_not_followed(self):
        # 32 + elf_mid 64 + 288 overhead; the misdecoded tail's call is dropped
        r = self.run_tool(384, indirect=False, cis=(("e.cpp.ci", E_CI),),
                          listing=TAIL_LISTING, symbols=TAIL_SYMBOLS)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("PASS t: 384 of 384 bytes (96 frames + 288 port overhead)", r.stdout)

    def test_elf_entry_past_the_symbol_size_starts_its_own_function(self):
        # elf_mid+0x8 (96) calls elf_far (400): 32 + 96 + 400 + 288
        r = self.run_tool(816, indirect=False, cis=(("o.cpp.ci", OFFSET_CI),),
                          listing=BOUNDARY_LISTING, symbols=BOUNDARY_SYMBOLS)
        self.assertIn("PASS t: 816 of 816 bytes (528 frames + 288 port overhead)", r.stdout)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)

    def test_elf_distinct_address_constructor_is_parsed(self):
        # 32 + the ELF D1's own 48-byte frame + 288 overhead, not the D2 body's 16
        r = self.run_tool(368, indirect=False, cis=(("d.cpp.ci", D_CI),),
                          listing=DISTINCT_LISTING, symbols=DISTINCT_SYMBOLS)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("PASS t: 368 of 368 bytes (80 frames + 288 port overhead)", r.stdout)

    def test_alias_rewrites_only_a_constructor_component(self):
        sys.path.insert(0, str(TOOL.parent))
        import stack_budget
        self.assertEqual(stack_budget.body_of("_ZN1XD1Ev"), "_ZN1XD2Ev")
        self.assertEqual(stack_budget.body_of("_ZN3FooC1ERKS_"), "_ZN3FooC2ERKS_")
        # A::ABC1() and A::C1() are ordinary functions whose names end in C1
        self.assertEqual(stack_budget.body_of("_ZN1A4ABC1Ev"), "_ZN1A4ABC1Ev")
        self.assertEqual(stack_budget.body_of("_ZN1A2C1Ev"), "_ZN1A2C1Ev")
        # a class name that ends in a digit is not a length prefix
        self.assertEqual(stack_budget.body_of("_ZN2A2C1Ev"), "_ZN2A2C2Ev")
        self.assertEqual(stack_budget.body_of("_ZN4Foo2D1Ev"), "_ZN4Foo2D2Ev")
        # template arguments, including a literal, are skipped whole
        self.assertEqual(stack_budget.body_of("_ZN3FooILi5EEC1Ev"), "_ZN3FooILi5EEC2Ev")
        self.assertEqual(stack_budget.body_of("_ZN3FooIJiEEC1Ev"), "_ZN3FooIJiEEC2Ev")
        self.assertEqual(
            stack_budget.body_of("_ZNSt12_Vector_baseIP19NimBLERemoteServiceSaIS1_EED1Ev"),
            "_ZNSt12_Vector_baseIP19NimBLERemoteServiceSaIS1_EED2Ev")


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
