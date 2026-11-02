#!/usr/bin/env python3
"""Guard the hot-path log levels.

Per-transition power-lock logging and per-fix GPS logging used INFO. At the
display and GPS rates they dominate the console line rate, block under the power
statistics mutex, and add serial jitter. Console `power stats` and the GPS pages
report the same facts without a log line, so these paths must stay at DEBUG.

Exit 0 when every guarded function keeps its hot path at DEBUG or below.
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent

CHECKS = (
    ("src/FurblePower.cpp", "void Power::acquire("),
    ("src/FurblePower.cpp", "void Power::release("),
    ("lib/furble/Lumix.cpp", "void Lumix::updateGeoData("),
)


def function_body(text: str, signature: str) -> str | None:
    start = text.find(signature)
    if start < 0:
        return None
    brace = text.find("{", start)
    if brace < 0:
        return None
    depth = 0
    for index in range(brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[brace : index + 1]
    return None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=pathlib.Path, default=ROOT)
    args = parser.parse_args()
    failures = 0
    for relative, signature in CHECKS:
        path = args.root / relative
        body = function_body(path.read_text(encoding="utf-8"), signature)
        if body is None:
            print(f"MISSING {relative}: {signature}", file=sys.stderr)
            failures += 1
            continue
        if re.search(r"\bESP_LOG[IWE]\b", body):
            print(
                f"FAIL {relative}: {signature} still logs above DEBUG on the hot path",
                file=sys.stderr,
            )
            failures += 1
        else:
            print(f"{relative}: {signature.strip()} is DEBUG or below")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
