#!/usr/bin/env python3
"""Worst-case stack depth per firmware task from GCC call-graph info.

Every firmware object is compiled with -fcallgraph-info=su, which writes a
.ci file next to it: one node per function with its frame size, and one edge
per call. This tool merges them, walks the call graph from each task entry in
tools/stack_budget.json and reports the deepest path.

The walk is exact for direct calls. GCC cannot name the target of an indirect
call (virtual, std::function, a C callback), so each indirect call site on a
path is listed, not followed; the manifest may resolve a caller's indirect
calls to named targets. A task fails when its resolved worst case plus the
port overhead exceeds its stack, when its path recurses through a function
with no manifest recursion bound, or when it reaches an alloca or VLA frame
with no manifest size bound.
"""

import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MANIFEST = ROOT / "tools" / "stack_budget.json"

NODE = re.compile(r'node: \{ title: "([^"]*)" label: "([^"]*)"')
EDGE = re.compile(r'edge: \{ sourcename: "([^"]*)" targetname: "([^"]*)"(?: label: "([^"]*)")?')
FRAME = re.compile(r"\\n(\d+) bytes \(([a-z,]+)\)")
INDIRECT = "__indirect_call"
ALIAS = re.compile(r"([CD])1E")
FUNC = re.compile(r"^([0-9a-f]+) <([^>+]+)>:$")
ENTRY = re.compile(r"^\s*([0-9a-f]+):.*\tentry\ta1, (\d+)$")
# A blob's static functions have no symbol, so objdump lists them under the
# previous one and calls them as <symbol+0xoffset>. Each entry instruction
# after the first in a symbol starts such a function, named that way.
CALL = re.compile(r"\tcall(0|4|8|12)\t[0-9a-f]+ <([^>]+)>$")
# objdump prints "l32r aN, <slot> (<value>)" when it can read the literal and
# "l32r aN, <slot>" when it cannot. The bare form names the literal's slot, not
# its value, so it is never a call target.
LITERAL = re.compile(r"\tl32r\t(a\d+), [^(]*(?:\([0-9a-f]+ <([^>]+)>\))?")
CALLX = re.compile(r"\tcallx(0|4|8|12)\t(a\d+)$")
WRITE = re.compile(r"\t([a-z][\w.]*)\t(a\d+)\b")
# Mnemonics whose first a-register operand is only read, from every form that
# occurs in the firmware listings: stores, branches and jumps, shift-amount
# loads, special and user register writes, TLB writes, loops and MAC16
# multiplies. S32C1I, XSR and the post-increment ee.*.ip/xp forms do write
# their first operand and are not listed.
READS_FIRST = re.compile(r"(s(8|16|32)i(\.n)?|s32ri|s32e|s32nb|b\w*|j\w*|ssl|ssr|ssa8[bl]"
                         r"|wsr\.\w+|wur\.\w+|wer|w[di]tlb|[di]itlb|loop\w*|u?mula?\.aa\.\w+)$")


def clobber(literal, window):
    """callN rotates the window by N: the callee may change aN to a15."""
    return {reg: value for reg, value in literal.items() if int(reg[1:]) < int(window)}


class Graph:
    def __init__(self):
        self.frame = {}  # title -> bytes
        self.kind = {}  # title -> static | dynamic | dynamic,bounded
        self.name = {}  # title -> demangled signature
        self.calls = {}  # title -> set of target titles
        self.indirect = {}  # title -> list of call-site labels

    def load(self, build_dir):
        files = sorted(Path(build_dir).rglob("*.ci"))
        for path in files:
            text = path.read_text(errors="replace")
            for title, label in NODE.findall(text):
                m = FRAME.search(label)
                if not m:
                    continue
                size = int(m.group(1))
                # comdat functions (inline, templates) are defined in many objects
                if size >= self.frame.get(title, -1):
                    self.frame[title] = size
                    self.kind[title] = m.group(2)
                    self.name[title] = label.split("\\n", 1)[0]
            for src, dst, site in EDGE.findall(text):
                if dst == INDIRECT:
                    self.indirect.setdefault(src, []).append(site)
                else:
                    self.calls.setdefault(src, set()).add(dst)
        return len(files)

    def load_elf(self, elf, objdump):
        """Frames and calls of prebuilt code (newlib, libstdc++, the WiFi and
        BT blobs) from the image's disassembly. Only functions with no .ci
        node are taken: the frame is the windowed ABI entry allocation, a
        call is call8 to a symbol or a longcall l32r literal then callx8.
        A literal names its target only until its register is written again
        or a call rotates the window over it; a callx8 through any other
        register is an indirect call."""
        out = subprocess.run([objdump, "-d", str(elf)], capture_output=True, text=True,
                             check=True).stdout
        # A static function's .ci title is "file:symbol"; the ELF names it by
        # the bare symbol. Never add a second node for code compiled here.
        compiled = {title.rsplit(":", 1)[-1] for title in self.frame}
        fn, symbol, base, literal = None, None, 0, {}
        for line in out.splitlines():
            m = FUNC.match(line)
            if m:
                base, symbol = int(m.group(1), 16), m.group(2)
                fn = symbol
                if fn in compiled or fn.replace("$", ".") in compiled:
                    symbol = fn = None  # compiled here: the .ci node is authoritative
                literal = {}
                continue
            if fn is None:
                continue
            m = ENTRY.search(line)
            if m:
                offset = int(m.group(1), 16) - base
                fn = f"{symbol}+0x{offset:x}" if fn != symbol or symbol in self.frame else symbol
                if fn not in self.frame:
                    self.frame[fn] = int(m.group(2))
                    self.kind[fn] = "static"
                    self.name[fn] = fn
                literal = {}
                continue
            m = CALL.search(line)
            if m:
                self.calls.setdefault(fn, set()).add(m.group(2))
                literal = clobber(literal, m.group(1))
                continue
            m = LITERAL.search(line)
            if m:
                literal[m.group(1)] = m.group(2)  # None: the value is unknown
                continue
            m = CALLX.search(line)
            if m:
                target = literal.get(m.group(2))
                if target:
                    self.calls.setdefault(fn, set()).add(target)
                else:
                    self.indirect.setdefault(fn, []).append("elf")
                literal = clobber(literal, m.group(1))
                continue
            m = WRITE.search(line)
            # any other write to a literal register makes its value unknown;
            # stores and branches only read their first operand
            if m and m.group(2) in literal and not READS_FIRST.match(m.group(1)):
                del literal[m.group(2)]

    def defined(self, title):
        """The node holding title's frame: itself, its global name, or the
        C2/D2 body that a C1/D1 constructor or destructor aliases."""
        symbol = title.rsplit(":", 1)[-1]
        for candidate in (title, symbol, ALIAS.sub(r"\g<1>2E", symbol), symbol.replace(".", "$")):
            if candidate in self.frame:
                return candidate
        return None

    def find(self, pattern):
        """Titles whose demangled signature or title matches pattern exactly once."""
        rx = re.compile(pattern)
        return [t for t in self.frame if rx.search(self.name[t]) or rx.search(t)]


def worst_case(graph, entry, resolve, pruned=frozenset(), recursion=()):
    """Deepest frame sum from entry; returns (bytes, path, notes).

    A call back into a function already on the walk's stack closes a cycle.
    The cycle is cut there, so the walk follows the deepest path that visits
    each function once. recursion lists (regex, depth, why) bounds: a cycle
    with a matching member adds depth - 1 more passes of its frames at every
    member. A bound with no depth records a documented known limit and adds
    nothing. Any other cycle is noted as unbounded."""
    memo = {}
    cycles = {}  # function -> {cycle: bytes of its extra passes}, cycles entered there
    notes = {"recursion": {}, "dynamic": set(), "indirect": {}, "unknown": set()}
    stack = []

    def targets(fn):
        out = set(graph.calls.get(fn, ()))
        if fn in graph.indirect:
            hit = [(rx, dst) for rx, dst in resolve
                   if dst and rx.search(graph.name.get(fn, fn))]
            if hit:
                for _, dst in hit:
                    out.update(dst)
            else:
                notes["indirect"][fn] = graph.indirect[fn]
        return out

    def bound(cycle):
        for rx, depth, why in recursion:
            if any(rx.search(graph.name[c]) or rx.search(c) for c in cycle):
                return depth, why
        return None, None

    def walk(fn):
        if fn in memo:
            return memo[fn]
        if fn in stack:
            cycle = tuple(stack[stack.index(fn):])
            depth, why = bound(cycle)
            notes["recursion"][cycle] = (depth, why)
            if depth:
                # The walk entered the cycle at fn, so every path through the
                # cycle runs through fn. Each extra pass costs the whole cycle;
                # charge it once, at fn, so a path crossing several members
                # pays depth passes, not one set per member.
                # Two cycles through the same head can nest (A-B-A, then
                # A-C-A from an inner A), so their passes add; summing them
                # also bounds the sequential case.
                cost = (depth - 1) * sum(graph.frame[c] for c in cycle)
                cycles.setdefault(fn, {})[frozenset(cycle)] = cost
            return 0, []
        body = graph.defined(fn)
        if body is None:
            notes["unknown"].add(fn)
            return 0, []
        if body != fn:
            return walk(body)
        if graph.kind[fn] == "dynamic":
            notes["dynamic"].add(fn)
        stack.append(fn)
        best, best_path = 0, []
        for dst in sorted(targets(fn) - pruned):
            depth, path = walk(dst)
            if depth > best:
                best, best_path = depth, path
        stack.pop()
        more = sum(cycles.get(fn, {}).values())
        memo[fn] = (graph.frame[fn] + more + best, [fn] + best_path)
        return memo[fn]

    sys.setrecursionlimit(20000)
    depth, path = walk(entry)
    notes["extra"] = {fn: sum(costs.values()) for fn, costs in cycles.items()}
    return depth, path, notes


def main():
    ap = argparse.ArgumentParser(description="Worst-case stack depth per firmware task.")
    ap.add_argument("--env", required=True, help="PlatformIO env, e.g. m5stick-s3")
    ap.add_argument("--build-dir", help="default .pio/build/<env>")
    ap.add_argument("--manifest", default=str(MANIFEST))
    ap.add_argument("--no-elf", action="store_true",
                    help="skip prebuilt code from firmware.elf (for synthetic graphs)")
    ap.add_argument("--objdump", help="default: the PlatformIO Xtensa toolchain for the target")
    ap.add_argument("--sdkconfig", help="default sdkconfig.<release env>")
    ap.add_argument("--stack", action="append", default=[], metavar="TASK=BYTES",
                    help="override one task's stack, e.g. to replay a past size")
    ap.add_argument("--verbose", action="store_true", help="print paths and indirect sites")
    args = ap.parse_args()

    build = Path(args.build_dir or ROOT / ".pio" / "build" / args.env)
    manifest = json.loads(Path(args.manifest).read_text())
    graph = Graph()
    if graph.load(build) == 0:
        print(f"no .ci files under {build}; build with -fcallgraph-info=su", file=sys.stderr)
        return 2

    sdkconfig = {}
    # a debug or diagnostic env shares its release env's sdkconfig and tasks
    # only a complete sdkconfig names its target; fragments such as
    # sdkconfig.m5stack-core-usb-debug layer on the release file
    release_envs = [p.name.split(".", 1)[1] for p in ROOT.glob("sdkconfig.*")
                    if "\nCONFIG_IDF_TARGET=" in p.read_text()]
    release_env = max((e for e in release_envs if args.env == e or args.env.startswith(e + "-")),
                      key=len, default=args.env)
    sdkconfig_path = Path(args.sdkconfig or ROOT / f"sdkconfig.{release_env}")
    for line in sdkconfig_path.read_text().splitlines():
        key, sep, value = line.partition("=")
        if sep and not key.startswith("#"):
            sdkconfig[key] = value.strip('"')
    target = sdkconfig["CONFIG_IDF_TARGET"]
    margin = manifest["overhead_bytes"][target]
    if not args.no_elf:
        # PlatformIO keeps its packages under PLATFORMIO_CORE_DIR when set.
        core = Path(os.environ.get("PLATFORMIO_CORE_DIR") or Path.home() / ".platformio")
        objdump = args.objdump or str(core / "packages" / "toolchain-xtensa-esp-elf"
                                      / "bin" / f"xtensa-{target}-elf-objdump")
        graph.load_elf(build / "firmware.elf", objdump)

    resolve = []
    for rule in manifest.get("indirect", []):
        # a rule without a linked caller or target is inert, e.g. LVGL headless;
        # a typo there shows up as unresolved indirect sites, not a silent pass
        dst = set()
        for pattern in rule["targets"]:
            dst.update(graph.find(pattern))
        resolve.append((re.compile(rule["caller"]), dst, frozenset(rule.get("tasks", ()))))

    # GCC reports an alloca or VLA frame as "dynamic" with only its static
    # part. A manifest bound adds the cited maximum; an unbounded one fails.
    for rule in manifest.get("dynamic_bounds", []):
        for fn in graph.find(rule["function"]):
            if graph.kind[fn] == "dynamic":
                graph.frame[fn] += rule["bytes"]
                graph.kind[fn] = "dynamic,bounded"
    recursion = [(re.compile(rule["function"]), rule.get("depth"), rule["why"],
                  frozenset(rule.get("tasks", ()))) for rule in manifest.get("recursion_bounds", [])]

    boot_only = set()
    for pattern in manifest.get("boot_only", []):
        boot_only.update(graph.find(pattern))
    overrides = {name: int(value) for name, _, value in (o.partition("=") for o in args.stack)}
    failed = 0
    for task in manifest["tasks"]:
        if release_env not in task.get("envs", [release_env]):
            continue
        found = graph.find(task["entry"])
        if len(found) != 1:
            print(f"FAIL {task['name']}: entry {task['entry']!r} matched {len(found)} functions")
            failed += 1
            continue
        pruned = frozenset() if task["name"] == "main" else frozenset(boot_only)
        # A rule scoped to tasks overrides the unscoped rules for its caller
        # on those tasks, e.g. an esp_event loop that dispatches one handler.
        scoped = [(rx, dst) for rx, dst, tasks in resolve if task["name"] in tasks]
        scoped_callers = {rx.pattern for rx, _ in scoped}
        task_rules = scoped + [(rx, dst) for rx, dst, tasks in resolve
                               if not tasks and rx.pattern not in scoped_callers]
        task_bounds = [(rx, depth, why) for rx, depth, why, tasks in recursion
                       if not tasks or task["name"] in tasks]
        depth, path, notes = worst_case(graph, found[0], task_rules, pruned, task_bounds)
        need = depth + margin
        stack = overrides.get(task["name"], task["stack_bytes"])
        if isinstance(stack, str):
            stack = int(sdkconfig[stack])
        unbounded = [c for c, (_, why) in notes["recursion"].items() if why is None]
        known = [c for c, (depth, why) in notes["recursion"].items() if why and depth is None]
        ok = need <= stack and not unbounded and not notes["dynamic"]
        failed += not ok
        flags = []
        if notes["indirect"]:
            flags.append(f"{sum(len(v) for v in notes['indirect'].values())} unresolved indirect")
        if notes["recursion"]:
            flags.append(f"{len(notes['recursion'])} recursive"
                         + (f" ({len(unbounded)} unbounded)" if unbounded else "")
                         + (f" ({len(known)} known limit)" if known else ""))
        if notes["unknown"]:
            flags.append(f"{len(notes['unknown'])} callees without a frame")
        if notes["dynamic"]:
            flags.append(f"{len(notes['dynamic'])} unbounded dynamic")
        print(f"{'PASS' if ok else 'FAIL'} {task['name']}: {need} of {stack} bytes"
              f" ({depth} frames + {margin} port overhead)" + (f"; {', '.join(flags)}" if flags else ""))
        if args.verbose or not ok:
            for fn in path:
                more = notes["extra"].get(fn)
                print(f"    {graph.frame[fn]:6d}  {graph.name[fn]}"
                      + (f" (+{more} for its bounded recursion)" if more else ""))
            for comp in unbounded:
                print("    recursion without a recursion_bounds entry: "
                      + " <-> ".join(graph.name[fn] for fn in comp))
            for fn in sorted(notes["dynamic"]):
                print(f"    dynamic frame without a dynamic_bounds entry: {graph.name[fn]}")
        if args.verbose:
            for comp, (depth, why) in notes["recursion"].items():
                if why:
                    print(f"    recursion, {f'{depth} passes' if depth else 'known limit'}: "
                          + " <-> ".join(graph.name[fn] for fn in comp))
            for fn in sorted(notes["unknown"]):
                print(f"    no frame (ROM, prebuilt or assembly): {fn}")
            for fn, sites in sorted(notes["indirect"].items()):
                print(f"    indirect in {graph.name.get(fn, fn)}: {len(sites)} site(s)")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
