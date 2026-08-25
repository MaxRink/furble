#!/usr/bin/env python3
"""Check the current portability boundary before adding another MCU port.

This is an inventory guard, not a Nordic build.  It deliberately reports the
existing coupling and fails only when the declared portable contract grows a
platform dependency.  Keeping the check small makes it usable from both the
host simulator and a future Zephyr CI job.
"""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[1]
PORTABLE_RELATIVE_PATHS = (Path("lib/furble/protocol"),)
MANIFEST_RELATIVE_PATH = Path("tools/portable_core_manifest.txt")
NORDIC_SOC_PATTERN = re.compile(r"nrf(?:52840|5340|54[a-z0-9-]*)$", re.IGNORECASE)

FORBIDDEN = {
    "Arduino": re.compile(r"\bArduino(?:\.h)?\b"),
    "ESP-IDF": re.compile(r"\b(?:ESP_|esp_|CONFIG_ESP_|nvs_|NVS_)"),
    "FreeRTOS": re.compile(r"\b(?:FreeRTOS|xTask|xQueue|SemaphoreHandle_t)\b"),
    "M5Unified": re.compile(r"\b(?:M5Unified|M5GFX|M5\.)\b"),
    "LVGL": re.compile(r"\b(?:lv_|LVGL|LV_)\b"),
    "NimBLE": re.compile(r"\bNimBLE\w*\b"),
}

SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".h", ".hh", ".hpp"}


def source_files(path: Path) -> list[Path]:
    if path.is_file():
        return [path]
    return sorted(
        candidate
        for candidate in path.rglob("*")
        if candidate.is_file() and candidate.suffix in SOURCE_SUFFIXES
    )


def relative(path: Path, root: Path = ROOT) -> str:
    return path.relative_to(root).as_posix()


def manifest_files(root: Path) -> tuple[list[Path], list[str]]:
    manifest = root / MANIFEST_RELATIVE_PATH
    if not manifest.is_file():
        return [], [f"missing shared source manifest: {relative(manifest, root)}"]

    entries = [line.strip() for line in manifest.read_text(encoding="utf-8").splitlines()]
    entries = [entry for entry in entries if entry and not entry.startswith("#")]
    errors: list[str] = []
    if len(entries) != len(set(entries)):
        errors.append("shared source manifest contains duplicate entries")

    files: list[Path] = []
    for entry in entries:
        path = root / entry
        if not path.is_file() or path.suffix not in SOURCE_SUFFIXES:
            errors.append(f"shared source manifest entry is not a source file: {entry}")
        else:
            files.append(path)
    return files, errors


def is_declared_nordic_port(path: Path, root: Path) -> bool:
    """Return whether path is under a conventional, declared Nordic port root."""

    parts = path.relative_to(root).parts
    for index, part in enumerate(parts[:-1]):
        name = part.lower()
        next_name = parts[index + 1].lower()
        if name == "platform" and next_name == "nordic":
            return True
        if name == "ports" and (next_name == "nordic" or NORDIC_SOC_PATTERN.fullmatch(next_name)):
            return True
    return False


def normalized_source_hash(path: Path) -> str:
    normalized = "".join(path.read_text(encoding="utf-8").split())
    return hashlib.sha256(normalized.encode("utf-8")).hexdigest()


def declared_nordic_sources(root: Path) -> list[Path]:
    return sorted(
        path
        for path in root.rglob("*")
        if path.is_file()
        and path.suffix in SOURCE_SUFFIXES
        and is_declared_nordic_port(path, root)
    )


def check_portable_contract(root: Path = ROOT) -> list[str]:
    violations: list[str] = []
    portable_files: list[Path] = []
    for relative_root in PORTABLE_RELATIVE_PATHS:
        path_root = root / relative_root
        files = source_files(path_root)
        if not path_root.is_dir():
            violations.append(f"missing portable contract root: {relative_root}")
            continue
        if not files:
            violations.append(f"portable contract root is empty: {relative_root}")
            continue
        portable_files.extend(files)

    manifest, manifest_errors = manifest_files(root)
    violations.extend(manifest_errors)
    if manifest and sorted(manifest) != sorted(portable_files):
        violations.append("shared source manifest does not match portable contract roots")

    manifest_hashes = {normalized_source_hash(path) for path in manifest}
    for path in declared_nordic_sources(root):
        if normalized_source_hash(path) in manifest_hashes:
            violations.append(
                "duplicate portable source content under Nordic port tree: "
                f"{relative(path, root)}"
            )

    for path in portable_files:
        text = path.read_text(encoding="utf-8")
        for name, pattern in FORBIDDEN.items():
            match = pattern.search(text)
            if match:
                line = text.count("\n", 0, match.start()) + 1
                violations.append(f"{relative(path, root)}:{line}: {name}")
    return violations


def direct_platform_tokens(paths: Path | tuple[Path, ...]) -> set[str]:
    if isinstance(paths, Path):
        paths = (paths,)
    tokens: set[str] = set()
    for path in paths:
        for file_path in source_files(path):
            text = file_path.read_text(encoding="utf-8")
            for name, pattern in FORBIDDEN.items():
                if pattern.search(text):
                    tokens.add(name)
    return tokens


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--check",
        action="store_true",
        help="fail if a portable contract file uses a platform token",
    )
    parser.add_argument(
        "--root",
        type=Path,
        default=ROOT,
        help="repository root to inspect (used by tests and CI wrappers)",
    )
    args = parser.parse_args()
    root = args.root.resolve()

    protocol_files = source_files(root / "lib" / "furble" / "protocol")
    app_paths = (root / "src", root / "include")
    app_files = source_files(app_paths[0]) + source_files(app_paths[1])
    vendor_files = source_files(root / "lib" / "furble")
    violations = check_portable_contract(root)

    print("furble portability inventory")
    print(f"portable contract files: {len(protocol_files)}")
    print(f"shared source manifest files: {len(manifest_files(root)[0])}")
    print(f"declared Nordic port source files scanned: {len(declared_nordic_sources(root))}")
    print(f"camera library files: {len(vendor_files)}")
    print(f"app and public-header files: {len(app_files)}")
    print(
        "camera library direct coupling: "
        + ", ".join(sorted(direct_platform_tokens(root / "lib" / "furble")))
    )
    print(
        "app-layer direct coupling: "
        + ", ".join(sorted(direct_platform_tokens(app_paths)))
    )
    print("Waveshare Ethernet and optional PoE: ESP-only boundary")

    if violations:
        print("portable contract violations:", file=sys.stderr)
        for violation in violations:
            print(f"  {violation}", file=sys.stderr)
        return 1 if args.check else 0

    print("portable contract: clean")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
