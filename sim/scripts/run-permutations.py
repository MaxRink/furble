#!/usr/bin/env python3
"""Run the memory-critical configuration permutations.

The manifest expands every board and every memory-critical axis. Full runs are
long, so --limit selects a deterministic prefix for pull-request smoke coverage.
The release gate requires a result file covering the full expected count.

Use --compact for the release artifact. It drops the per-result output tail and
writes compact JSON, which keeps all 6,912 results small enough to review. The
readiness gate only needs id, board, combination, passed and exit.
"""
from __future__ import annotations

import argparse
import itertools
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def combinations(manifest: dict):
    boards = manifest["boards"]
    axes = manifest["axes"]
    keys = list(axes)
    for board in boards:
        for values in itertools.product(*(axes[key] for key in keys)):
            yield board, dict(zip(keys, values))


def scenario_for(board: str, combo: dict) -> str:
    psram = {"present": "octal", "absent": "absent", "failure": "init_fail"}[combo["psram"]]
    lines = [
        f"seed hw_profile {board}",
        f"seed web_ui {'true' if combo['web_ui'] == 'on' else 'false'}",
        "seed webui_password true",
    ]
    # The stack model is only meaningful when MQTT itself is enabled. Seeding
    # mqtt_owner_stack for an off combination used to turn the model on and
    # exercise a service the permutation had explicitly disabled.
    if combo["mqtt"] == "on":
        lines += [
            "seed mqtt_owner_stack 10240",
            "seed mqtt_worker_stack 6144" if combo["mqtt_tls"] == "tls"
            else "seed mqtt_worker_stack 4096",
            f"seed mqtt_tls {'true' if combo['mqtt_tls'] == 'tls' else 'false'}",
            f"seed mqtt_ha {'true' if combo['mqtt_ha'] == 'on' else 'false'}",
            "seed mqtt_buffer 2048",
        ]
    lines += [
        f"seed companion_enabled {'true' if combo['companion'] == 'on' else 'false'}",
        f"seed wifi_connected {'true' if combo['wifi'] == 'connected' else 'false'}",
        "seed fauxny true",
        f"seed saved_camera {'true' if combo['camera'] in ('saved', 'connected') else 'false'}",
        f"seed autoconnect {'true' if combo['camera'] == 'connected' else 'false'}",
        f"seed psram_mode {psram}",
    ]
    if combo["wifi"] == "reconnecting":
        lines.append("action wifi-fail-auth")
    lines += [
        "wait 100",
        "assert setting.fauxny 1",
        "assert mem.leaks 0",
        # heap.free is unsigned, so a floor of 0 could never fail. Exhaustion
        # already aborts the run with status 3, as on the device; this also
        # fails a modelled allocation failure that a seam reported and survived.
        "assert heap.alloc_failures 0",
        "exit",
    ]
    return "\n".join(lines) + "\n"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path,
                        default=Path(os.environ.get("FURBLE_SIM_BIN",
                                                    ROOT / "sim/build/furble-sim")))
    parser.add_argument("--manifest", type=Path,
                        default=ROOT / "sim/coverage/permutations.json")
    parser.add_argument("--results", type=Path,
                        default=ROOT / "sim/coverage/permutation_results.json")
    parser.add_argument("--limit", type=int, default=0,
                        help="run only the first N combinations (0 means all)")
    parser.add_argument("--board", action="append", default=[],
                        help="restrict to a board (repeatable)")
    parser.add_argument("--compact", action="store_true",
                        help="omit output_tail and write compact release JSON")
    args = parser.parse_args(argv)
    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    unknown_boards = sorted(set(args.board) - set(manifest.get("boards", [])))
    if unknown_boards:
        print(f"unknown --board value(s): {', '.join(unknown_boards)}", file=sys.stderr)
        return 2
    results = []
    expected_ids = []
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    for index, (board, combo) in enumerate(combinations(manifest)):
        if args.board and board not in args.board:
            continue
        cid = board + "." + ".".join(f"{key}={combo[key]}" for key in manifest["axes"])
        expected_ids.append(cid)
        if args.limit and len(results) >= args.limit:
            break
        with tempfile.TemporaryDirectory() as directory:
            scenario = Path(directory) / f"perm-{index}.txt"
            scenario.write_text(scenario_for(board, combo), encoding="utf-8")
            run = subprocess.run([str(args.binary), "--script", str(scenario)],
                                 cwd=ROOT, env=env, capture_output=True, text=True)
        result = {
            "id": cid,
            "board": board,
            "combination": combo,
            "passed": run.returncode == 0,
            "exit": run.returncode,
        }
        if not args.compact:
            result["output_tail"] = (run.stdout + run.stderr)[-1000:]
        results.append(result)
        if run.returncode != 0:
            print(f"FAIL {cid} exit {run.returncode}", file=sys.stderr)
    result_ids = [result["id"] for result in results]
    selection_error = ""
    if len(set(result_ids)) != len(result_ids):
        selection_error = "permutation results contain duplicate ids"
    elif result_ids != expected_ids[:len(results)]:
        selection_error = "permutation results do not match the manifest order"
    elif not args.limit and not args.board and len(results) != manifest["expected_count"]:
        selection_error = "a full permutation run did not cover the expected count"

    document = {
        "version": 1,
        "expected_count": manifest["expected_count"],
        "run_count": len(results),
        # A board- or limit-filtered smoke run is never a complete release
        # artifact, even when it covered every selected combination.
        "complete": (not args.board and not args.limit
                     and len(results) == manifest["expected_count"]),
        "results": results,
    }
    if selection_error:
        document["error"] = selection_error
    encoding = json.dumps(document, separators=(",", ":")) if args.compact \
        else json.dumps(document, indent=2)
    args.results.write_text(encoding + "\n", encoding="utf-8")
    failed = sum(1 for result in results if not result["passed"])
    if selection_error:
        print(f"permutations: {selection_error}", file=sys.stderr)
    print(f"permutations: {len(results)} run, {failed} failed "
          f"({'complete' if document['complete'] else 'partial'})")
    return 1 if failed or selection_error else 0


if __name__ == "__main__":
    raise SystemExit(main())
