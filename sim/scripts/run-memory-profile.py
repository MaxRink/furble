#!/usr/bin/env python3
"""Run the memory profile scenarios and compare the dumps to the golden budget."""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
BOARDS = ("sticks3", "core2", "core", "stickc", "stickc-plus", "stickc-plus2",
          "headless-s3", "waveshare-s3-eth")
BUDGET_SCENARIOS = (
    "hw-memory-post394-budget",
    "hw-memory-webui-post394",
    "hw-memory-mqtt-ha-oom",
    "hw-memory-mqtt-ha-ok",
    "memory-webui-fail-closed",
)


def run(binary: Path, scenario: Path, dump: Path) -> tuple[int, str]:
    text = scenario.read_text(encoding="utf-8")
    text = text.replace("exit\n", f"action memory-dump {dump}\nexit\n")
    with tempfile.TemporaryDirectory() as directory:
        generated = Path(directory) / scenario.name
        generated.write_text(text, encoding="utf-8")
        env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
        result = subprocess.run([str(binary), "--script", str(generated)],
                                cwd=ROOT, env=env, capture_output=True, text=True)
        return result.returncode, result.stdout + result.stderr


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path,
                        default=Path(os.environ.get("FURBLE_SIM_BIN",
                                                    ROOT / "sim/build/furble-sim")))
    parser.add_argument("--output", type=Path, default=ROOT / ".pio/sim-memory")
    args = parser.parse_args(argv)
    args.output.mkdir(parents=True, exist_ok=True)
    dumps: list[Path] = []
    failures = 0
    for board in BOARDS:
        scenario = args.output / f"profile-{board}.txt"
        scenario.write_text(
            f"seed hw_profile {board}\nwait 1200\nprint mem.profile\n"
            "assert mem.leaks 0\nexit\n", encoding="utf-8")
        dump = args.output / f"profile-{board}.json"
        rc, output = run(args.binary, scenario, dump)
        if rc != 0:
            print(f"FAIL profile-{board} exit {rc}\n{output}", file=sys.stderr)
            failures += 1
        dumps.append(dump)
    for name in BUDGET_SCENARIOS:
        scenario = ROOT / "sim/scenarios/e2e" / f"{name}.txt"
        dump = args.output / f"{name}.json"
        rc, output = run(args.binary, scenario, dump)
        if rc != 0:
            print(f"FAIL {name} exit {rc}\n{output}", file=sys.stderr)
            failures += 1
        dumps.append(dump)
    result = subprocess.run([sys.executable, str(ROOT / "tools/check_sim_memory.py"),
                             *[str(path) for path in dumps]], cwd=ROOT)
    if result.returncode != 0:
        failures += 1
    if failures:
        print(f"memory profile failed in {failures} leg(s)", file=sys.stderr)
        return 1
    print("memory profile passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
