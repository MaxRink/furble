#!/usr/bin/env python3
"""Turn a PlatformIO firmware compile database into one clang-tidy can parse.

`pio run -e <env> -t compiledb` writes compile_commands.json for the Xtensa
GCC toolchain. Released LLVM has no Xtensa target, so each furble entry is
rewritten for clang:

- the compiler becomes clang or clang++ with --target=i686-unknown-none-elf, a
  32-bit little-endian stand-in. Its `a` asm constraint (eax) also accepts the
  `=a` outputs in the Xtensa FreeRTOS port headers. clang-tidy never
  assembles, so the instruction text does not matter;
- `__XTENSA__` and `__xtensa__` are defined, because GCC predefines them and
  the ESP-IDF headers select the Xtensa port on them;
- GCC-only flags are dropped, and the toolchain's libstdc++ and newlib
  directories become -isystem paths, read from the compiler's own search list.

Only furble sources (src/ and lib/furble/) are kept. The output goes to
.pio/static-build/firmware-<env>/compile_commands.json, a --build-dir for
tools/check_static_analysis.py.
"""

from __future__ import annotations

import argparse
import json
import os
import shlex
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCES = ("src/", "lib/furble/")
TARGET = "--target=i686-unknown-none-elf"
# GCC options clang rejects or that only matter for code generation.
DROP = (
    "-mlongcalls",
    "-mtext-section-literals",
    "-mdisable-hardware-atomics",
    "-mfix-esp32-psram-cache",
    "-fstrict-volatile-bitfields",
    "-fno-tree-switch-conversion",
    "-fno-shrink-wrap",
    "-fcallgraph-info",
    "-fstack-usage",
    "-fdebug-prefix-map",
    "-fmacro-prefix-map",
    "-frandom-seed",
    "-Werror",
)


def find_compiler(name: str) -> str:
    """The toolchain compiler: on PATH, or in PlatformIO's toolchain package."""
    core = Path(os.environ.get("PLATFORMIO_CORE_DIR", Path.home() / ".platformio"))
    found = shutil.which(name) or shutil.which(name, path=str(
        core / "packages" / "toolchain-xtensa-esp-elf" / "bin"))
    if found is None:
        raise SystemExit(f"FAIL: {name} is not on PATH or in {core}/packages")
    return found


def system_includes(compiler: str) -> list[str]:
    """The toolchain's C++ and newlib header directories, without GCC's own
    builtin headers, which clang supplies itself."""
    out = subprocess.run([find_compiler(compiler), "-xc++", "-E", "-v", "-"], input="",
                         capture_output=True,
                         text=True, check=True).stderr
    lines = out.splitlines()
    start = lines.index("#include <...> search starts here:") + 1
    end = lines.index("End of search list.")
    paths = [os.path.normpath(line.strip()) for line in lines[start:end]]
    return [p for p in paths if "/lib/gcc/" not in p]


def convert(entries: list[dict], includes: dict[str, list[str]]) -> list[dict]:
    out = []
    for entry in entries:
        path = Path(entry["directory"], entry["file"]).resolve()
        try:
            rel = path.relative_to(ROOT).as_posix()
        except ValueError:
            continue
        if not rel.startswith(SOURCES):
            continue
        args = entry.get("arguments") or shlex.split(entry["command"])
        compiler = args[0]
        cxx = compiler.endswith("++")
        command = ["clang++" if cxx else "clang", TARGET, "-D__XTENSA__=1", "-D__xtensa__=1",
                   "-Wno-unknown-warning-option"]
        if cxx:
            command.append("-nostdinc++")
        if compiler not in includes:
            includes[compiler] = system_includes(compiler)
        for directory in includes[compiler]:
            if cxx or "/c++/" not in directory:
                command += ["-isystem", directory]
        command += [a for a in args[1:] if not a.startswith(DROP)]
        # GCC owns compiler warnings on firmware; keep only clang-tidy checks.
        command.append("-w")
        out.append({"directory": entry["directory"], "file": str(path), "arguments": command})
    return out


def main() -> int:
    parser = argparse.ArgumentParser(description=(__doc__ or "").splitlines()[0])
    parser.add_argument("--env", required=True, help="PlatformIO env, e.g. m5stick-s3")
    parser.add_argument("--input", type=Path, default=ROOT / "compile_commands.json",
                        help="database written by pio run -t compiledb")
    args = parser.parse_args()
    entries = json.loads(args.input.read_text(encoding="utf-8"))
    converted = convert(entries, {})
    if not converted:
        print(f"FAIL: no furble sources in {args.input}", file=sys.stderr)
        return 1
    out = ROOT / ".pio" / "static-build" / f"firmware-{args.env}" / "compile_commands.json"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(converted, indent=1), encoding="utf-8")
    print(f"{out.relative_to(ROOT)}: {len(converted)} furble sources")
    return 0


if __name__ == "__main__":
    sys.exit(main())
