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
PANIC = "<panic handler>"


def _skip_name(s, i):
    """Index past the length-prefixed source name or substitution at i."""
    if s[i].isdigit():
        j = i
        while s[j].isdigit():
            j += 1
        return j + int(s[i:j])
    if s[i] == "S":  # St, Sa, Ss... or S<seq-id>_
        return i + 2 if s[i + 1].islower() else s.index("_", i) + 1
    return i + 1


def _skip_template_args(s, i):
    """Index past the I...E template argument list that starts at i."""
    depth, i = 1, i + 1
    while depth:
        c = s[i]
        if c.isdigit() or c == "S":
            i = _skip_name(s, i)
        elif c == "L":  # a literal such as Li5E holds digits that are not names
            i = s.index("E", i) + 1
        elif c in "IJNX":  # J...E is a template argument pack
            depth, i = depth + 1, i + 1
        elif c == "E":
            depth, i = depth - 1, i + 1
        else:
            i += 1
    return i


def body_of(symbol):
    """The C2/D2 body a C1/D1 constructor or destructor aliases, else symbol.
    A constructor or destructor is always a member, so its C1 or D1 is a
    top-level component of the _ZN...E nested name. Walking the length-prefixed
    names keeps a name that merely ends in C1, such as A::ABC1(), unchanged."""
    if not symbol.startswith("_ZN"):
        return symbol
    i = 3
    try:
        while symbol[i] in "rVK":
            i += 1
        while symbol[i] != "E":
            c = symbol[i]
            if c in "CD" and symbol[i + 1] == "1":
                return symbol[:i + 1] + "2" + symbol[i + 2:]
            if c == "I":
                i = _skip_template_args(symbol, i)
            elif c == "B":  # ABI tag: B<source-name>
                i = _skip_name(symbol, i + 1)
            else:
                i = _skip_name(symbol, i)
    except (IndexError, ValueError):
        pass  # not a name this parser understands: leave it alone
    return symbol
FUNC = re.compile(r"^([0-9a-f]+) <([^>+]+)>:$")
# objdump -t: "42086bd8  w    F .flash.text\t00000014 _ZN...D1Ev"
SYMBOL = re.compile(r"^([0-9a-f]+)\s.*\sF\s+\S+\s+([0-9a-f]+)\s+(\S+)$")
ADDRESS = re.compile(r"^\s*([0-9a-f]+):")
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
        self.bodies = None  # C2/D2 symbol -> its "file:symbol" title, built on first use

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
        table = subprocess.run([objdump, "-t", str(elf)], capture_output=True, text=True,
                               check=True).stdout
        address, size = {}, {}
        for line in table.splitlines():
            m = SYMBOL.match(line)
            if m:
                address[m.group(3)] = int(m.group(1), 16)
                size[(m.group(3), int(m.group(1), 16))] = int(m.group(2), 16)
        # A static function's .ci title is "file:symbol"; the ELF names it by
        # the bare symbol. Never add a second node for code compiled here.
        compiled = {title.rsplit(":", 1)[-1] for title in self.frame}
        fn, symbol, base, end, literal = None, None, 0, None, {}
        for line in out.splitlines():
            m = FUNC.match(line)
            if m:
                base, symbol = int(m.group(1), 16), m.group(2)
                fn = symbol
                # Past its symbol size, code belongs to an unnamed static
                # function. objdump can misdecode that function's entry after
                # alignment padding; until an entry is decoded, nothing there
                # is the symbol's own call.
                end = base + size[(symbol, base)] if size.get((symbol, base)) else None
                # A C1/D1 constructor or destructor usually shares its C2/D2
                # body's address, and objdump may label it by either name.
                # Parsing that alias would read past its retw into the literal
                # pool that follows. A C1/D1 at its own address (a class with
                # virtual bases) is distinct code and is parsed.
                body = body_of(fn)
                if (fn in compiled or fn.replace("$", ".") in compiled
                        or (body in compiled and address.get(body) == base)):
                    symbol = fn = None  # compiled here: the .ci node is authoritative
                literal = {}
                continue
            if fn is None:
                continue
            m = ADDRESS.match(line)
            if (fn == symbol and end is not None and m and int(m.group(1), 16) >= end
                    and not ENTRY.search(line)):
                continue
            m = ENTRY.search(line)
            if m:
                offset = int(m.group(1), 16) - base
                past = end is not None and base + offset >= end
                fn = (f"{symbol}+0x{offset:x}"
                      if fn != symbol or symbol in self.frame or past else symbol)
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
        body = body_of(symbol)
        for candidate in (title, symbol, body, symbol.replace(".", "$")):
            if candidate in self.frame:
                return candidate
        if body != symbol:
            # A comdat C2/D2 body can be titled "file:symbol". Every copy is
            # the same code, so take the largest frame.
            if self.bodies is None:
                self.bodies = {}
                for t in self.frame:
                    s = t.rsplit(":", 1)[-1]
                    if t != s and re.search(r"[CD]2E", s):
                        if self.frame[t] >= self.frame.get(self.bodies.get(s), -1):
                            self.bodies[s] = t
            return self.bodies.get(body)
        return None

    def find(self, pattern):
        """Titles whose demangled signature or title matches pattern exactly once."""
        rx = re.compile(pattern)
        return [t for t in self.frame if rx.search(self.name[t]) or rx.search(t)]


def worst_case(graph, entry, resolve, pruned=frozenset(), recursion=()):
    """Deepest frame sum from entry; returns (bytes, path, notes).

    The walk collapses each strongly connected component of the reachable
    call graph and takes the longest path over the resulting DAG, so a
    function's depth no longer depends on the order the walk reached it.
    Outside a cycle the result is exact. A recursive component is charged its
    passes times the sum of its members' frames: one pass puts each member on
    the stack at most once, so the sum bounds every path through it.
    recursion lists (regex, depth, why) bounds. A component pays
    1 + sum(depth - 1) passes over the distinct rules its members match. A
    rule with no depth records a documented known limit and charges one pass,
    but only for a component no finite rule matches: a finite bound always
    counts, so a catch-all known limit cannot hide it. Any other recursive
    component is noted as unbounded."""
    notes = {"recursion": {}, "dynamic": set(), "indirect": {}, "unknown": set()}

    def node(fn):
        body = graph.defined(fn)
        if body is None:
            notes["unknown"].add(fn)
        return body

    def targets(fn):
        out = set(graph.calls.get(fn, ()))
        if fn in graph.indirect:
            hit = [dst for rx, dst in resolve if dst and rx.search(graph.name.get(fn, fn))]
            for dst in hit:
                out.update(dst)
            if not hit:
                notes["indirect"][fn] = graph.indirect[fn]
        return sorted({t for t in map(node, out - pruned) if t is not None})

    start = node(entry)
    if start is None:
        notes["extra"] = {}
        return 0, [], notes
    # Iterative Tarjan. Components come out callees first, so every
    # component's callees are costed before it.
    index, low, on_stack, stack, succ = {}, {}, set(), [], {}
    comp_of, comps = {}, []
    work = [(start, 0)]
    while work:
        fn, i = work.pop()
        if i == 0:
            index[fn] = low[fn] = len(index)
            stack.append(fn)
            on_stack.add(fn)
            succ[fn] = targets(fn)
        if i < len(succ[fn]):
            work.append((fn, i + 1))
            dst = succ[fn][i]
            if dst not in index:
                work.append((dst, 0))
            elif dst in on_stack:
                low[fn] = min(low[fn], index[dst])
            continue
        if low[fn] == index[fn]:
            members = []
            while not members or members[-1] != fn:
                members.append(stack.pop())
                on_stack.discard(members[-1])
                comp_of[members[-1]] = len(comps)
            comps.append(sorted(members))
        if work:
            parent = work[-1][0]
            low[parent] = min(low[parent], low[fn])

    # cost[v] is the deepest frame sum from v. route[v] lists the nodes of
    # that path inside v's component and the first node after it.
    cost, route, extra = {}, {}, {}
    for c, members in enumerate(comps):
        frame = graph.frame
        notes["dynamic"].update(m for m in members if graph.kind[m] == "dynamic")
        exit_ = {}
        for m in members:
            outs = [d for d in succ[m] if comp_of[d] != c]
            e = max(outs, key=lambda d: (cost[d], d), default=None)
            exit_[m] = (cost[e] if e else 0, e)
        if len(members) == 1 and members[0] not in succ[members[0]]:
            m = members[0]
            cost[m], route[m] = frame[m] + exit_[m][0], ([m], exit_[m][1])
            continue
        # A list, one entry per matched rule: two rules with the same depth and
        # reason are still two recursions and each adds its passes.
        rules = [(depth, why) for rx, depth, why in recursion
                 if any(rx.search(graph.name[m]) or rx.search(m) for m in members)]
        why = min((w for _, w in rules), default=None)
        finite = [(d, w) for d, w in rules if d is not None]
        known = not finite and any(depth is None for depth, _ in rules)
        if finite:
            why = min(w for _, w in finite)
        passes = 1 + sum(d - 1 for d, _ in finite) if finite else 1
        notes["recursion"][tuple(members)] = (None if known or not rules else passes, why)
        frames = sum(frame[m] for m in members)
        out_cost, out = max(exit_.values(), key=lambda x: (x[0], x[1] or ""))
        if passes > 1:
            # Each pass puts every member on the stack at most once.
            extra[members[0]] = (passes - 1) * frames
            for m in members:
                cost[m], route[m] = passes * frames + out_cost, (members, out)
            continue
        # One pass is a simple path through the component. Enumerate them
        # while that stays cheap; past the budget, the frame sum bounds them.
        inside = {m: [d for d in succ[m] if comp_of[d] == c] for m in members}
        budget = [PATH_BUDGET]
        exact = {m: longest_simple_path(m, inside, frame, exit_, budget) for m in members}
        if budget[0] >= 0:
            for m, (total, path) in exact.items():
                cost[m], route[m] = total, (path, exit_[path[-1]][1])
        elif known:
            # A known limit is documented as a lower bound; the greedy walk
            # that visits each member once keeps it fast.
            greedy(members, inside, frame, exit_, cost, route)
        else:
            for m in members:
                cost[m], route[m] = frames + out_cost, (members, out)

    path, v = [], start
    while v is not None:
        nodes, v = route[v]
        path.extend(nodes)
    notes["extra"] = extra
    return cost[start], path, notes


# A source citation: a C, C++ or header file and a line number.
CITATION = re.compile(r"\.(c|h|cpp):\d+")
# Simple paths explored per recursive component before the walk falls back to
# the component's frame sum. Every bounded component on the seven envs fits.
PATH_BUDGET = 200000


def longest_simple_path(start, inside, frame, exit_, budget):
    """Deepest (bytes, path) from start that visits each member once and
    leaves the component through the last member's deepest callee."""
    best_total, best_path = -1, [start]
    work: list[tuple[str, list[str], int]] = [(start, [start], frame[start])]
    while work:
        budget[0] -= 1
        if budget[0] < 0:
            break
        u, path, total = work.pop()
        if total + exit_[u][0] > best_total:
            best_total, best_path = total + exit_[u][0], path
        for d in inside[u]:
            if d not in path:
                work.append((d, path + [d], total + frame[d]))
    return best_total, best_path


def greedy(members, inside, frame, exit_, cost, route):
    """The memoized depth-first walk: a member first reached inside an open
    cycle keeps the value it had there. Only for known limits."""
    on_stack = set()

    def walk(u):
        if u in cost:
            return cost[u]
        on_stack.add(u)
        best, nodes, out = exit_[u][0], [u], exit_[u][1]
        for d in inside[u]:
            if d not in on_stack and walk(d) > best:
                best, nodes, out = cost[d], [u] + route[d][0], route[d][1]
        on_stack.discard(u)
        cost[u], route[u] = frame[u] + best, (nodes, out)
        return cost[u]

    sys.setrecursionlimit(max(sys.getrecursionlimit(), 4 * len(members) + 1000))
    for m in members:
        walk(m)


def main():
    ap = argparse.ArgumentParser(description="Worst-case stack depth per firmware task.")
    ap.add_argument("--env", required=True, help="PlatformIO env, e.g. m5stick-s3")
    ap.add_argument("--build-dir", help="default .pio/build/<env>")
    ap.add_argument("--manifest", default=str(MANIFEST))
    ap.add_argument("--no-elf", action="store_true",
                    help="skip prebuilt code from firmware.elf (for synthetic graphs)")
    ap.add_argument("--objdump", help="default: the PlatformIO Xtensa toolchain for the target")
    ap.add_argument("--sdkconfig", help="default sdkconfig.<release env>")
    ap.add_argument("--debug-sdkconfig", default=str(ROOT / "sdkconfig.debug"),
                    help="fragment every -debug env merges over the release sdkconfig")
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

    def read_config(path, into):
        for line in path.read_text().splitlines():
            key, sep, value = line.partition("=")
            if sep and not key.startswith("#"):
                into[key] = value.strip('"')

    read_config(sdkconfig_path, sdkconfig)
    # Every -debug env merges sdkconfig.debug (patches/debug_sdkconfig.py) and
    # creates its furble tasks with their debug margins.
    debug = "-debug" in args.env
    debug_sdkconfig = dict(sdkconfig)
    read_config(Path(args.debug_sdkconfig), debug_sdkconfig)

    def stack_of(value, config):
        return int(config[value]) if isinstance(value, str) else value
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
    for rule in manifest.get("recursion_bounds", []):
        depth = rule.get("depth")
        # A recursive component is on the stack at least once; a depth below
        # 1 would charge fewer passes than that and undercount the task.
        if depth is not None and (not isinstance(depth, int) or depth < 1):
            raise SystemExit(f"stack_budget: recursion bound {rule['function']} has depth "
                             f"{depth!r}; it must be a whole number of at least 1")
    recursion = [(re.compile(rule["function"]), rule.get("depth"), rule["why"],
                  frozenset(rule.get("tasks", ()))) for rule in manifest.get("recursion_bounds", [])]

    # A call the source guards so it cannot run, cited in the manifest. The
    # cut hides depth, so an entry without a file:line citation is refused
    # rather than followed.
    for rule in manifest.get("infeasible_calls", []):
        if not CITATION.search(rule.get("why", "")):
            raise SystemExit(f"stack_budget: infeasible call {rule['caller']} -> {rule['callee']}"
                             " needs a why that cites the guard as file:line")
        callees = set(graph.find(rule["callee"]))
        for fn in graph.find(rule["caller"]):
            graph.calls[fn] = graph.calls.get(fn, set()) - callees

    boot_only = set()
    for pattern in manifest.get("boot_only", []):
        boot_only.update(graph.find(pattern))
    overrides = {name: int(value) for name, _, value in (o.partition("=") for o in args.stack)}
    failed = 0

    # panic_abort's illegal instruction enters the panic handler on the
    # faulting task's stack, an edge the call graph cannot see. The handler
    # is walked once with a nested panic not followed and becomes one node
    # that panic_abort calls.
    panic = manifest.get("panic")
    if panic:
        aborts, handlers = graph.find(panic["abort"]), graph.find(panic["handler"])
        if len(aborts) != 1 or len(handlers) != 1:
            print(f"FAIL panic: abort matched {len(aborts)}, handler matched {len(handlers)}")
            failed += 1
        else:
            depth, path, notes = worst_case(
                graph, handlers[0], [(rx, dst) for rx, dst, tasks in resolve if not tasks],
                frozenset(boot_only | {aborts[0]}),
                [(rx, depth, why) for rx, depth, why, tasks in recursion if not tasks])
            unbounded = [c for c, (_, why) in notes["recursion"].items() if why is None]
            ok = not unbounded and not notes["dynamic"]
            failed += not ok
            print(f"{'PASS' if ok else 'FAIL'} panic handler: {depth} bytes from {handlers[0]}"
                  + (f"; {len(unbounded)} unbounded recursion" if unbounded else "")
                  + (f"; {len(notes['dynamic'])} unbounded dynamic" if notes["dynamic"] else ""))
            if args.verbose:
                for fn in path:
                    print(f"    {graph.frame[fn]:6d}  {graph.name[fn]}")
            graph.frame[PANIC], graph.kind[PANIC] = depth, "static"
            graph.name[PANIC] = f"panic handler ({handlers[0]} on this stack)"
            graph.calls.setdefault(aborts[0], set()).add(PANIC)
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
        release_stack = stack_of(task["stack_bytes"], sdkconfig)
        debug_stack = stack_of(task.get("debug_stack_bytes", task["stack_bytes"]), debug_sdkconfig)
        if debug_stack < release_stack:
            print(f"FAIL {task['name']}: debug stack {debug_stack} is below the release stack"
                  f" {release_stack}")
            failed += 1
        stack = overrides.get(task["name"], debug_stack if debug else release_stack)
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
