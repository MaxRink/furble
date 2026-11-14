#!/usr/bin/env python3
"""Focused static analysis for changed production C++ files.

The old pre-review gate catches diff hygiene and settings drift. This gate adds
the two analyzers that find the classes of defects the compiler cannot: cppcheck
for uninitialized/out-of-bounds/format issues and clang-tidy for bugprone,
analyzer, cert, performance, and portability checks.

Only files under src/, lib/furble/, include/, and sim/ are production code for
this gate. Generated trees, vendored components, and test doubles are excluded.
"""

from __future__ import annotations

import argparse
import glob
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PRODUCTION_PREFIXES = ("src/", "lib/furble/", "include/", "sim/")
# sim/.pio holds the CI clone of M5GFX, M5Unified, LVGL and TinyGPSPlus:
# third-party code that only looks like furble's because it sits under sim/.
EXCLUDED_PREFIXES = ("sim/build/", "sim/shim/", "sim/.pio/")


def is_production(path: str) -> bool:
    """Return whether a repo-relative path is furble's own analyzed code."""

    return path.startswith(PRODUCTION_PREFIXES) and not path.startswith(EXCLUDED_PREFIXES)
SOURCE_SUFFIXES = (".cpp", ".cc", ".cxx", ".c")
# Production sources no compile database builds. clang-tidy cannot analyze
# them, so they are named here instead of being skipped silently; cppcheck
# still covers them. Every other source missing from the databases fails.
NO_COMPILE_DB = {
    "sim/env_mutation_guard.c": "Linux LD_PRELOAD helper built by sim/scripts/run-env-order.sh",
}
HEADER_SUFFIXES = (".h",)
CPPCHECK_SUPPRESSIONS = (
    # Aggregate structs are initialized at their use sites; cppcheck cannot see
    # the designated initializer through the header.
    "uninitMemberVarNoCtor",
    # Virtual overrides intentionally repeat a base-class member name.
    "duplInheritedMember",
    # Host test shims and mocks reuse type names across translation units.
    "ctuOneDefinitionRuleViolation",
    # Test harnesses throw control-flow exceptions from task entry points.
    "throwInEntryPoint",
    # cppcheck cannot parse a few ESP-IDF macro-heavy headers.
    "syntaxError",
)
CPPCHECK_TEMPLATE = "{file}:{line}:{severity}:{id}:{message}"
CPPCHECK_LINE = re.compile(
    r"^(?P<file>[^:]+):(?P<line>\d+):(?P<severity>error|warning|performance|portability|style):"
    r"(?P<id>[A-Za-z0-9_]+):(?P<message>.*)$"
)
TIDY_LINE = re.compile(
    r"^(?P<file>[^:]+):(?P<line>\d+):(?P<column>\d+): (?P<severity>warning|error): "
    r"(?P<message>.*) \[(?P<check>[^\]]+)\]$"
)
COMPILE_ERROR = re.compile(
    r"^(?P<file>[^:]+):(?P<line>\d+):(?P<column>\d+): (?:fatal )?error: (?P<message>.*)$"
)


def find_clang_tidy(explicit: str | None = None) -> str | None:
    """Find clang-tidy on PATH or in the common Homebrew/LLVM locations."""
    candidates: list[str] = []
    if explicit:
        candidates.append(explicit)
    candidates.extend(
        ["clang-tidy", "clang-tidy-20", "clang-tidy-19", "clang-tidy-18", "clang-tidy-17"]
    )
    for candidate in candidates:
        found = shutil.which(candidate)
        if found is not None:
            return found
        if Path(candidate).is_file():
            return candidate
    for pattern in (
        "/opt/homebrew/opt/llvm/bin/clang-tidy",
        "/usr/local/opt/llvm/bin/clang-tidy",
        "/usr/lib/llvm-*/bin/clang-tidy",
    ):
        matches = sorted(glob.glob(pattern))
        if matches:
            return matches[-1]
    return None


def production_paths(paths: list[str]) -> tuple[list[str], list[str]]:
    sources: list[str] = []
    headers: list[str] = []
    for raw in paths:
        path = raw.replace(os.sep, "/")
        if not is_production(path):
            continue
        if not (ROOT / path).is_file():
            continue
        if path.endswith(SOURCE_SUFFIXES):
            sources.append(path)
        elif path.endswith(HEADER_SUFFIXES):
            headers.append(path)
    return sorted(set(sources)), sorted(set(headers))


def production_files(paths: list[str]) -> list[str]:
    return production_paths(paths)[0]


def changed_paths(base: str) -> tuple[list[str], list[str]]:
    command = [
        "git",
        "diff",
        "--name-only",
        "--diff-filter=ACMR",
        base,
        "--",
        "*.cpp",
        "*.cc",
        "*.cxx",
        "*.c",
        "*.h",
    ]
    output = subprocess.run(
        command, cwd=ROOT, check=False, capture_output=True, text=True
    )
    if output.returncode != 0:
        raise RuntimeError(output.stderr.strip() or "git diff failed")
    return production_paths(output.stdout.splitlines())


def tracked_production_files() -> tuple[list[str], list[str]]:
    output = subprocess.run(
        ["git", "ls-files", "*.cpp", "*.cc", "*.cxx", "*.c"],
        cwd=ROOT,
        check=False,
        capture_output=True,
        text=True,
    )
    if output.returncode != 0:
        raise RuntimeError(output.stderr.strip() or "git ls-files failed")
    return production_paths(output.stdout.splitlines())


def normalize_path(raw: str) -> str:
    """Return a repo-relative path when the analyzer reported an absolute one."""
    text = raw.replace(os.sep, "/")
    path = Path(text)
    if path.is_absolute():
        try:
            return path.resolve().relative_to(ROOT).as_posix()
        except (OSError, ValueError):
            return text
    return text


def parse_cppcheck(text: str) -> list[dict[str, str]]:
    findings: list[dict[str, str]] = []
    for line in text.splitlines():
        match = CPPCHECK_LINE.match(line)
        if match is None:
            continue
        finding = match.groupdict()
        finding["file"] = normalize_path(finding["file"])
        if not is_production(finding["file"]):
            continue
        findings.append(finding)
    return findings


def parse_clang_tidy(text: str) -> tuple[list[dict[str, str]], list[dict[str, str]]]:
    findings: list[dict[str, str]] = []
    compile_errors: list[dict[str, str]] = []
    for line in text.splitlines():
        match = TIDY_LINE.match(line)
        if match is not None:
            finding = match.groupdict()
            finding["file"] = normalize_path(finding["file"])
            if is_production(finding["file"]):
                findings.append(finding)
            continue
        match = COMPILE_ERROR.match(line)
        if match is not None:
            finding = match.groupdict()
            finding["file"] = normalize_path(finding["file"])
            if is_production(finding["file"]):
                compile_errors.append(finding)
    return findings, compile_errors


def run_cppcheck(files: list[str], jobs: int) -> list[dict[str, str]]:
    executable = shutil.which("cppcheck")
    if executable is None:
        raise RuntimeError("cppcheck is not installed")
    groups = {
        "c++": [path for path in files if not path.endswith(".c")],
        "c": [path for path in files if path.endswith(".c")],
    }
    findings: list[dict[str, str]] = []
    for language, group in groups.items():
        if not group:
            continue
        command = [
            executable,
            "--enable=warning,performance,portability",
            "--std=c++17" if language == "c++" else "--std=c11",
            "--language=" + language,
            "--inline-suppr",
            "--quiet",
            "--template=" + CPPCHECK_TEMPLATE,
            f"-j{jobs}",
            "-I",
            "include",
            "-I",
            "lib/furble",
            "-I",
            "sim/shim",
        ]
        for suppression in CPPCHECK_SUPPRESSIONS:
            command.append("--suppress=" + suppression)
        command.extend(group)
        output = subprocess.run(
            command, cwd=ROOT, check=False, capture_output=True, text=True
        )
        # cppcheck reports diagnostics on stderr; stdout is progress/verbose text.
        group_findings = parse_cppcheck(output.stderr)
        findings.extend(group_findings)
        if output.returncode != 0 and not group_findings:
            raise RuntimeError(
                f"cppcheck ({language}) exited {output.returncode}: "
                f"{output.stderr.strip()[:500]}"
            )
    return findings


def compile_database(build_dirs: list[Path]) -> dict[str, list[Path]]:
    files: dict[str, list[Path]] = {}
    for build_dir in build_dirs:
        database = build_dir / "compile_commands.json"
        if not database.is_file():
            continue
        try:
            entries = json.loads(database.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            continue
        for entry in entries:
            try:
                path = Path(entry["file"]).resolve().relative_to(ROOT)
            except (KeyError, ValueError):
                continue
            entry_list = files.setdefault(path.as_posix(), [])
            if build_dir not in entry_list:
                entry_list.append(build_dir)
    return files


def header_includers(headers: list[str], sources: list[str]) -> dict[str, list[str]]:
    """Map each changed header to production sources that include it.

    A header has no compile command of its own. Analyzing a small set of its
    includers is the closest host check; cppcheck also parses the header
    directly. Includes may name a header by basename or by a path relative to
    an include root (`protocol/CanonTime.h`), so both forms are matched. The
    cap keeps a widely included header from turning one change into a
    full-tree clang-tidy run.
    """
    roots = ("lib/furble/", "include/", "sim/", "src/")
    candidates: list[str] = []
    for header in headers:
        names = {Path(header).name, header}
        for root in roots:
            if header.startswith(root):
                names.add(header[len(root) :])
        candidates.extend(sorted(names))

    includers: dict[str, list[str]] = {}
    for header in headers:
        names = {Path(header).name, header}
        for root in roots:
            if header.startswith(root):
                names.add(header[len(root) :])
        matches: list[str] = []
        for source in sources:
            try:
                text = (ROOT / source).read_text(encoding="utf-8", errors="ignore")
            except OSError:
                continue
            if any(
                f'#include "{name}"' in text or f"#include <{name}>" in text for name in names
            ):
                matches.append(source)
                if len(matches) >= 4:
                    break
        includers[header] = matches
    return includers


def run_clang_tidy(
    targets: list[str], build_dirs: list[Path], executable: str, jobs: int
) -> tuple[list[dict[str, str]], list[dict[str, str]], list[str]]:
    database_files = compile_database(build_dirs)
    if not database_files:
        joined = ", ".join(str(path) for path in build_dirs)
        raise RuntimeError(
            f"no compile_commands.json in {joined}; configure the host and simulator "
            "builds with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON"
        )
    selected: list[tuple[str, Path]] = []
    skipped: list[str] = []
    for path in targets:
        directories = database_files.get(path, [])
        if not directories:
            if path not in NO_COMPILE_DB:
                skipped.append(path)
            continue
        for build_dir in directories:
            selected.append((path, build_dir))
    findings: list[dict[str, str]] = []
    compile_errors: list[dict[str, str]] = []
    extra_args = [
        f"-I{ROOT / 'sim' / 'shim'}",
    ]
    lvgl = ROOT / "managed_components" / "lvgl__lvgl"
    if lvgl.is_dir():
        extra_args.append(f"-I{lvgl}")
    for path, build_dir in selected:
        command = [
            executable,
            "-p",
            str(build_dir),
            "--config-file=.clang-tidy",
            "--quiet",
        ]
        # The simulator shims stand in for ESP-IDF headers; a firmware
        # database from tools/firmware_compile_db.py has the real ones.
        if not build_dir.name.startswith("firmware-"):
            for argument in extra_args:
                command.append("--extra-arg=" + argument)
        command.append(path)
        output = subprocess.run(
            command, cwd=ROOT, check=False, capture_output=True, text=True
        )
        file_findings, file_errors = parse_clang_tidy(output.stdout + output.stderr)
        findings.extend(file_findings)
        compile_errors.extend(file_errors)
        # WarningsAsErrors turns a check in a vendored header into an error and
        # a non-zero exit. Only an error that is not a promoted check means
        # the translation unit did not compile.
        errors = [line for line in (output.stdout + output.stderr).splitlines()
                  if " error: " in line]
        promoted = bool(errors) and all("-warnings-as-errors]" in line for line in errors)
        if output.returncode != 0 and not file_findings and not file_errors and not promoted:
            compile_errors.append(
                {
                    "file": path,
                    "line": "0",
                    "column": "0",
                    "message": f"clang-tidy exited {output.returncode}",
                }
            )
    return findings, compile_errors, skipped


def deduplicate(findings: list[dict[str, str]]) -> list[dict[str, str]]:
    """Drop repeated findings from sources analyzed with two compile databases."""
    seen: set[tuple[str, ...]] = set()
    unique: list[dict[str, str]] = []
    for finding in findings:
        key = tuple(
            finding.get(field, "")
            for field in ("file", "line", "column", "severity", "message", "check", "id")
        )
        if key in seen:
            continue
        seen.add(key)
        unique.append(finding)
    return unique


def print_findings(label: str, findings: list[dict[str, str]]) -> None:
    for finding in findings:
        location = f"{finding['file']}:{finding['line']}"
        if "column" in finding:
            location += f":{finding['column']}"
        check = finding.get("check") or finding.get("id", "")
        print(
            f"{label} {location}: {finding['severity']}: {finding['message']} [{check}]",
            file=sys.stderr,
        )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--base",
        default="origin/master",
        help="git ref to diff against; ignored with --all",
    )
    parser.add_argument(
        "--all", action="store_true", help="analyze every tracked production source"
    )
    parser.add_argument(
        "--build-dir",
        type=Path,
        action="append",
        default=None,
        help="CMake build directory with compile_commands.json; repeat for the host "
        "and simulator builds",
    )
    parser.add_argument(
        "--clang-tidy",
        default=None,
        help="clang-tidy executable (auto-detected when omitted)",
    )
    parser.add_argument("--jobs", type=int, default=2, help="cppcheck worker count")
    parser.add_argument(
        "--skip-clang-tidy", action="store_true", help="run cppcheck only"
    )
    args = parser.parse_args()

    try:
        sources, headers = (
            tracked_production_files() if args.all else changed_paths(args.base)
        )
    except RuntimeError as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 2
    if not sources and not headers:
        print("static analysis: no changed production C++ files")
        return 0

    static = ROOT / ".pio" / "static-build"
    build_dirs = args.build_dir or [static, *sorted(static.glob("firmware-*"))]
    print(f"static analysis: {len(sources)} source(s), {len(headers)} header(s)")
    failures = 0

    cppcheck_findings = deduplicate(run_cppcheck(sources + headers, args.jobs))
    if cppcheck_findings:
        print_findings("cppcheck", cppcheck_findings)
        failures += len(cppcheck_findings)

    if not args.skip_clang_tidy:
        tidy = find_clang_tidy(args.clang_tidy)
        if tidy is None:
            print(
                "FAIL: clang-tidy is not installed; install it or pass --skip-clang-tidy",
                file=sys.stderr,
            )
            return 2
        try:
            all_sources, _ = tracked_production_files()
        except RuntimeError:
            all_sources = sources
        includers = header_includers(headers, all_sources)
        tidy_targets = list(sources)
        for targets in includers.values():
            tidy_targets.extend(targets)
        try:
            tidy_findings, compile_errors, skipped = run_clang_tidy(
                sorted(set(tidy_targets)), build_dirs, tidy, args.jobs
            )
            tidy_findings = deduplicate(tidy_findings)
            compile_errors = deduplicate(compile_errors)
        except RuntimeError as error:
            print(f"FAIL: {error}", file=sys.stderr)
            return 2
        if skipped:
            for path in skipped:
                print(
                    f"clang-tidy {path}: no compile command in any database; add its "
                    "build to a --build-dir or list it in NO_COMPILE_DB",
                    file=sys.stderr,
                )
            failures += len(skipped)
        if compile_errors:
            for finding in compile_errors:
                print(
                    f"clang-tidy {finding['file']}:{finding['line']}: "
                    f"could not compile: {finding['message']}",
                    file=sys.stderr,
                )
            failures += len(compile_errors)
        if tidy_findings:
            print_findings("clang-tidy", tidy_findings)
            failures += len(tidy_findings)

    if failures:
        print(f"FAIL: {failures} static-analysis finding(s)", file=sys.stderr)
        return 1
    print("PASS: static analysis")
    return 0


if __name__ == "__main__":
    sys.exit(main())
