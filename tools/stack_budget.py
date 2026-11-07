#!/usr/bin/env python3
"""Worst-case stack depth per firmware task from GCC call-graph info.

Every firmware object is compiled with -fcallgraph-info=su, which writes a
.ci file next to it: one node per function with its frame size, and one edge
per call. This tool merges them, walks the call graph from each task entry in
tools/stack_budget.json and reports the deepest path.

The walk is exact for direct calls. GCC cannot name the target of an indirect
call (virtual, std::function, a C callback), so each indirect call site on a
path is listed, not followed; the manifest may resolve a caller's indirect
calls to named targets. Recursion and dynamic frames are reported too. A task
fails when its resolved worst case plus the interrupt frame exceeds its stack.
"""

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MANIFEST = ROOT / "tools" / "stack_budget.json"

NODE = re.compile(r'node: \{ title: "([^"]*)" label: "([^"]*)"')
EDGE = re.compile(r'edge: \{ sourcename: "([^"]*)" targetname: "([^"]*)"(?: label: "([^"]*)")?')
FRAME = re.compile(r"\\n(\d+) bytes \(([a-z,]+)\)")
INDIRECT = "__indirect_call"


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

    def find(self, pattern):
        """Titles whose demangled signature or title matches pattern exactly once."""
        rx = re.compile(pattern)
        return [t for t in self.frame if rx.search(self.name[t]) or rx.search(t)]


def worst_case(graph, entry, resolve, pruned=frozenset()):
    """Deepest frame sum from entry; returns (bytes, path, notes)."""
    memo = {}
    notes = {"recursion": set(), "dynamic": set(), "indirect": {}, "unknown": set()}
    onstack = set()

    def targets(fn):
        out = set(graph.calls.get(fn, ()))
        if fn in graph.indirect:
            hit = [(rx, dst) for rx, dst in resolve if rx.search(graph.name.get(fn, fn))]
            if hit:
                for _, dst in hit:
                    out.update(dst)
            else:
                notes["indirect"][fn] = graph.indirect[fn]
        return out

    def walk(fn):
        if fn in memo:
            return memo[fn]
        if fn in onstack:
            notes["recursion"].add(fn)
            return 0, []
        if fn not in graph.frame:
            notes["unknown"].add(fn)
            return 0, []
        if graph.kind[fn] != "static":
            notes["dynamic"].add(fn)
        onstack.add(fn)
        best, best_path = 0, []
        for dst in sorted(targets(fn) - pruned):
            depth, path = walk(dst)
            if depth > best:
                best, best_path = depth, path
        onstack.discard(fn)
        memo[fn] = (graph.frame[fn] + best, [fn] + best_path)
        return memo[fn]

    sys.setrecursionlimit(20000)
    depth, path = walk(entry)
    return depth, path, notes


def main():
    ap = argparse.ArgumentParser(description="Worst-case stack depth per firmware task.")
    ap.add_argument("--env", required=True, help="PlatformIO env, e.g. m5stick-s3")
    ap.add_argument("--build-dir", help="default .pio/build/<env>")
    ap.add_argument("--manifest", default=str(MANIFEST))
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

    resolve = []
    for rule in manifest.get("indirect", []):
        # a rule without a linked caller or target is inert, e.g. LVGL headless;
        # a typo there shows up as unresolved indirect sites, not a silent pass
        dst = set()
        for pattern in rule["targets"]:
            dst.update(graph.find(pattern))
        resolve.append((re.compile(rule["caller"]), dst))

    sdkconfig = {}
    # a debug or diagnostic env shares its release env's sdkconfig and tasks
    release_envs = [p.name.split(".", 1)[1] for p in ROOT.glob("sdkconfig.*")]
    release_env = max((e for e in release_envs if args.env == e or args.env.startswith(e + "-")),
                      key=len, default=args.env)
    sdkconfig_path = Path(args.sdkconfig or ROOT / f"sdkconfig.{release_env}")
    for line in sdkconfig_path.read_text().splitlines():
        key, sep, value = line.partition("=")
        if sep and not key.startswith("#"):
            sdkconfig[key] = value.strip('"')
    margin = manifest["overhead_bytes"][sdkconfig["CONFIG_IDF_TARGET"]]
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
        depth, path, notes = worst_case(graph, found[0], resolve, pruned)
        need = depth + margin
        stack = overrides.get(task["name"], task["stack_bytes"])
        if isinstance(stack, str):
            stack = int(sdkconfig[stack])
        ok = need <= stack
        failed += not ok
        flags = []
        if notes["indirect"]:
            flags.append(f"{sum(len(v) for v in notes['indirect'].values())} unresolved indirect")
        if notes["recursion"]:
            flags.append(f"{len(notes['recursion'])} recursive")
        if notes["dynamic"]:
            flags.append(f"{len(notes['dynamic'])} dynamic")
        print(f"{'PASS' if ok else 'FAIL'} {task['name']}: {need} of {stack} bytes"
              f" ({depth} frames + {margin} port overhead)" + (f"; {', '.join(flags)}" if flags else ""))
        if args.verbose or not ok:
            for fn in path:
                print(f"    {graph.frame[fn]:6d}  {graph.name[fn]}")
        if args.verbose:
            for fn, sites in sorted(notes["indirect"].items()):
                print(f"    indirect in {graph.name.get(fn, fn)}: {len(sites)} site(s)")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
