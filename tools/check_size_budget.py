#!/usr/bin/env python3
"""Fail a firmware build that outgrows its flash slot or its static RAM budget.

For each env, tools/size_budget.json records:

- flash_slot: the app partition size. firmware.bin must fit, or the image
  cannot be flashed or installed over the air.
- dram_bytes: the static DRAM budget, .dram0.data + .noinit + .dram0.bss. Every
  byte here comes out of the boot heap (tools/derive_sim_heap.py).
- iram_bytes: the IRAM budget, every .iram0.* section.

A RAM budget is the measured size plus at least HEADROOM bytes, rounded up to
a KiB. Real growth past it fails here and has to raise the budget, with its
reason, in the same PR; a few bytes of unrelated churn does not. --write
records the current sizes that way.
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUDGET = ROOT / "tools" / "size_budget.json"
DRAM = (".dram0.data", ".noinit", ".dram0.bss")
STEP = 1024
HEADROOM = 512


def sections(elf: Path) -> dict[str, int]:
    """Section name -> size from an ELF32 little-endian section header table."""
    data = elf.read_bytes()
    if data[:4] != b"\x7fELF" or data[4] != 1 or data[5] != 1:
        raise SystemExit(f"FAIL: {elf} is not a 32-bit little-endian ELF")
    shoff, = struct.unpack_from("<I", data, 0x20)
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", data, 0x2E)
    headers = [struct.unpack_from("<IIIIII", data, shoff + i * shentsize) for i in range(shnum)]
    names = headers[shstrndx][4]  # sh_offset of the name table
    out = {}
    for name, *_, size in headers:
        end = data.index(b"\0", names + name)
        out[data[names + name:end].decode()] = size
    return out


def measure(env: str, build: Path | None = None) -> dict[str, int]:
    build = build or ROOT / ".pio" / "build" / env
    sizes = sections(build / "firmware.elf")
    return {
        "flash": (build / "firmware.bin").stat().st_size,
        "dram": sum(sizes.get(name, 0) for name in DRAM),
        "iram": sum(size for name, size in sizes.items() if name.startswith(".iram0.")),
    }


def round_up(value: int) -> int:
    return -(-value // STEP) * STEP


def budget_for(value: int) -> int:
    return round_up(value + HEADROOM)


def check(budget: dict, used: dict[str, int]) -> list[str]:
    errors = []
    for key, limit in (("flash", "flash_slot"), ("dram", "dram_bytes"), ("iram", "iram_bytes")):
        if used[key] > budget[limit]:
            errors.append(f"{key} {used[key]:,} bytes exceeds {limit} {budget[limit]:,} "
                          f"by {used[key] - budget[limit]:,}")
    return errors


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=(__doc__ or "").splitlines()[0])
    parser.add_argument("--env", action="append", required=True, help="PlatformIO env; repeat")
    parser.add_argument("--write", action="store_true",
                        help="record the measured RAM sizes plus headroom, rounded up to a KiB")
    parser.add_argument("--build-dir", type=Path,
                        help="build directory for a single --env, e.g. a kept reproducible build")
    args = parser.parse_args(argv)
    if args.build_dir and len(args.env) != 1:
        parser.error("--build-dir takes exactly one --env")
    budgets = json.loads(BUDGET.read_text(encoding="utf-8"))
    failed = False
    for env in args.env:
        if env not in budgets["envs"]:
            print(f"FAIL {env}: no entry in {BUDGET.relative_to(ROOT)}")
            failed = True
            continue
        budget = budgets["envs"][env]
        try:
            used = measure(env, args.build_dir)
        except FileNotFoundError as error:
            print(f"FAIL {env}: not built ({error.filename})")
            failed = True
            continue
        if args.write:
            budget["dram_bytes"] = budget_for(used["dram"])
            budget["iram_bytes"] = budget_for(used["iram"])
        errors = check(budget, used)
        summary = (f"flash {used['flash']:,}/{budget['flash_slot']:,}, "
                   f"dram {used['dram']:,}/{budget['dram_bytes']:,}, "
                   f"iram {used['iram']:,}/{budget['iram_bytes']:,}")
        print(f"{'FAIL' if errors else 'PASS'} {env}: {summary}")
        for error in errors:
            print(f"    {error}")
        failed |= bool(errors)
    if args.write:
        BUDGET.write_text(json.dumps(budgets, indent=2) + "\n", encoding="utf-8")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
