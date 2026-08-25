#!/usr/bin/env python3
"""Check the current portability boundary before adding another MCU port.

This is an inventory guard, not a Nordic build.  It deliberately reports the
existing coupling and fails only when the declared portable contract grows a
platform dependency.  Keeping the check small makes it usable from both the
host simulator and a future Zephyr CI job.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[1]
PORTABLE_PATHS = (
    ROOT / "lib" / "furble" / "protocol",
)

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


def relative(path: Path) -> str:
    return path.relative_to(ROOT).as_posix()


def check_portable_contract() -> list[str]:
    violations: list[str] = []
    for root in PORTABLE_PATHS:
        for path in source_files(root):
            text = path.read_text(encoding="utf-8")
            for name, pattern in FORBIDDEN.items():
                match = pattern.search(text)
                if match:
                    line = text.count("\n", 0, match.start()) + 1
                    violations.append(f"{relative(path)}:{line}: {name}")
    return violations


def direct_platform_tokens(path: Path) -> set[str]:
    tokens: set[str] = set()
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
    args = parser.parse_args()

    protocol_files = source_files(ROOT / "lib" / "furble" / "protocol")
    app_files = source_files(ROOT / "src") + source_files(ROOT / "include")
    vendor_files = source_files(ROOT / "lib" / "furble")
    violations = check_portable_contract()

    print("furble portability inventory")
    print(f"portable contract files: {len(protocol_files)}")
    print(f"camera library files: {len(vendor_files)}")
    print(f"app and public-header files: {len(app_files)}")
    print(
        "camera library direct coupling: "
        + ", ".join(sorted(direct_platform_tokens(ROOT / "lib" / "furble")))
    )
    print(
        "app-layer direct coupling: "
        + ", ".join(sorted(direct_platform_tokens(ROOT / "src")))
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
