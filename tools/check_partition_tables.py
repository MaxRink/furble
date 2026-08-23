#!/usr/bin/env python3
"""Check the board partition CSVs without requiring an ESP-IDF build."""

from __future__ import annotations

import argparse
import csv
import os
import re
import sys
from dataclasses import dataclass
from pathlib import Path


FLASH_4M = 4 * 1024 * 1024
FLASH_8M = 8 * 1024 * 1024
FLASH_16M = 16 * 1024 * 1024
PARTITION_TABLE_END = 0x9000
DATA_ALIGNMENT = 0x1000
APP_ALIGNMENT = 0x10000
STOCK_PARTITION_CSV = "partitions_two_ota_large.csv"

# These are the measured firmware.bin sizes recorded in
# plans/33-wifi-hub.md:1296-1304 at commit 2b79ce8. The headless S3 env uses
# the release S3 image as its reference because it targets the same 8 MB board.
REFERENCE_APP_SIZES = {
    "m5stick-c": 1_001_584,
    "m5stick-c-plus": 1_002_240,
    "m5stack-core": 1_037_616,
    "m5stack-core2": 1_037_728,
    "m5stick-s3": 1_034_256,
    "esp32-s3-headless": 1_034_256,
}


class CheckError(RuntimeError):
    """Raised when a partition invariant is violated."""


@dataclass(frozen=True)
class Partition:
    name: str
    kind: str
    subtype: str
    offset: int
    size: int

    @property
    def end(self) -> int:
        return self.offset + self.size


@dataclass(frozen=True)
class BoardSpec:
    name: str
    csv_name: str
    flash_size: int
    reference_app_size: int
    expected_ota_size: int


BOARD_SPECS = (
    BoardSpec(
        "m5stick-c",
        STOCK_PARTITION_CSV,
        FLASH_4M,
        REFERENCE_APP_SIZES["m5stick-c"],
        0x1A9000,
    ),
    BoardSpec(
        "m5stick-c-plus",
        STOCK_PARTITION_CSV,
        FLASH_4M,
        REFERENCE_APP_SIZES["m5stick-c-plus"],
        0x1A9000,
    ),
    BoardSpec(
        "m5stack-core",
        STOCK_PARTITION_CSV,
        FLASH_4M,
        REFERENCE_APP_SIZES["m5stack-core"],
        0x1A9000,
    ),
    BoardSpec(
        "m5stack-core2",
        "partitions_two_ota_16m.csv",
        FLASH_16M,
        REFERENCE_APP_SIZES["m5stack-core2"],
        0x600000,
    ),
    BoardSpec(
        "m5stick-s3",
        "partitions_two_ota_8m.csv",
        FLASH_8M,
        REFERENCE_APP_SIZES["m5stick-s3"],
        0x300000,
    ),
    BoardSpec(
        "esp32-s3-headless",
        "partitions_two_ota_8m.csv",
        FLASH_8M,
        REFERENCE_APP_SIZES["esp32-s3-headless"],
        0x300000,
    ),
)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise CheckError(message)


def parse_size(value: str) -> int:
    token = value.strip().upper()
    if token.startswith("0X"):
        return int(token, 16)

    match = re.fullmatch(r"([0-9]+)([KMGT]?B?)?", token)
    if match is None:
        raise CheckError(f"invalid partition size {value!r}")

    number = int(match.group(1))
    suffix = (match.group(2) or "").rstrip("B")
    multiplier = {"": 1, "K": 1024, "M": 1024**2, "G": 1024**3, "T": 1024**4}[suffix]
    return number * multiplier


def align_up(value: int, alignment: int) -> int:
    return (value + alignment - 1) // alignment * alignment


def parse_csv(path: Path) -> list[Partition]:
    """Parse a CSV and resolve ESP-IDF's blank offsets."""

    rows: list[tuple[str, str, str, int | None, int]] = []
    try:
        handle = path.open(newline="", encoding="utf-8")
    except OSError as exc:
        raise CheckError(f"cannot read {path}: {exc}") from exc

    with handle:
        for line_number, row in enumerate(csv.reader(handle), start=1):
            if not row or not any(cell.strip() for cell in row):
                continue
            if row[0].strip().startswith("#"):
                continue
            if len(row) < 5:
                raise CheckError(f"{path}:{line_number}: expected at least five CSV fields")

            name, kind, subtype = (field.strip() for field in row[:3])
            offset_text = row[3].strip()
            size_text = row[4].strip()
            require(name != "", f"{path}:{line_number}: partition name is empty")
            require(kind != "", f"{path}:{line_number}: partition type is empty")
            require(subtype != "", f"{path}:{line_number}: partition subtype is empty")
            require(size_text != "", f"{path}:{line_number}: partition size is empty")

            offset = None if offset_text == "" else parse_size(offset_text)
            rows.append((name, kind, subtype, offset, parse_size(size_text)))

    partitions: list[Partition] = []
    cursor = PARTITION_TABLE_END
    for name, kind, subtype, explicit_offset, size in rows:
        alignment = APP_ALIGNMENT if kind == "app" else DATA_ALIGNMENT
        offset = explicit_offset if explicit_offset is not None else align_up(cursor, alignment)
        partitions.append(Partition(name, kind, subtype, offset, size))
        cursor = offset + size
    return partitions


def check_board(spec: BoardSpec, csv_path: Path) -> tuple[Partition, Partition]:
    partitions = parse_csv(csv_path)
    require(partitions, f"{spec.name}: CSV has no partitions")

    names = [partition.name for partition in partitions]
    require(len(names) == len(set(names)), f"{spec.name}: partition names are not unique")

    previous_end = PARTITION_TABLE_END
    previous_offset = PARTITION_TABLE_END
    for partition in partitions:
        require(
            partition.offset >= previous_offset,
            f"{spec.name}: partitions are not ordered at {partition.name}",
        )
        require(
            partition.offset >= previous_end,
            f"{spec.name}: {partition.name} overlaps the preceding partition",
        )
        if partition.offset > previous_end:
            # ESP-IDF inserts this alignment gap before an app partition when
            # a blank offset is used. No other gaps are valid in these tables.
            require(
                partition.kind == "app"
                and partition.offset == align_up(previous_end, APP_ALIGNMENT),
                f"{spec.name}: unexpected gap before {partition.name}",
            )
        require(
            partition.offset % DATA_ALIGNMENT == 0,
            f"{spec.name}: {partition.name} offset is not 0x1000-aligned",
        )
        require(
            partition.size % DATA_ALIGNMENT == 0,
            f"{spec.name}: {partition.name} size is not 0x1000-aligned",
        )
        if partition.kind == "app":
            require(
                partition.offset % APP_ALIGNMENT == 0,
                f"{spec.name}: {partition.name} app offset is not 0x10000-aligned",
            )
        require(
            partition.end <= spec.flash_size,
            f"{spec.name}: {partition.name} ends at 0x{partition.end:x}, "
            f"beyond {spec.flash_size} bytes",
        )
        previous_offset = partition.offset
        previous_end = partition.end

    nvs = next((partition for partition in partitions if partition.name == "nvs"), None)
    otadata = next((partition for partition in partitions if partition.name == "otadata"), None)
    phy_init = next((partition for partition in partitions if partition.name == "phy_init"), None)
    require(nvs is not None, f"{spec.name}: nvs partition is missing")
    require(otadata is not None, f"{spec.name}: otadata partition is missing")
    require(phy_init is not None, f"{spec.name}: phy_init partition is missing")
    require((nvs.offset, nvs.size) == (0x9000, 0x6000), f"{spec.name}: nvs moved or changed size")
    require(
        (otadata.offset, otadata.size) == (0xF000, 0x2000),
        f"{spec.name}: otadata moved or changed size",
    )
    require((phy_init.offset, phy_init.size) == (0x11000, 0x1000), f"{spec.name}: phy_init changed")

    app_partitions = [partition for partition in partitions if partition.kind == "app"]
    ota_partitions = [
        partition
        for partition in app_partitions
        if partition.subtype in {"ota_0", "ota_1"}
    ]
    require(
        [partition.name for partition in app_partitions] == ["ota_0", "ota_1"],
        f"{spec.name}: expected exactly ota_0 and ota_1 app partitions",
    )
    require(len(ota_partitions) == 2, f"{spec.name}: expected exactly two OTA app slots")
    ota_0, ota_1 = ota_partitions
    require(ota_0.size == ota_1.size, f"{spec.name}: OTA app slots differ in size")
    require(
        ota_0.size == spec.expected_ota_size,
        f"{spec.name}: expected OTA slot 0x{spec.expected_ota_size:x}, got 0x{ota_0.size:x}",
    )
    require(
        ota_0.size >= spec.reference_app_size,
        f"{spec.name}: OTA slot is smaller than the {spec.reference_app_size}-byte reference app",
    )
    require(ota_0.offset == 0x20000, f"{spec.name}: ota_0 moved")
    require(
        ota_1.offset == align_up(ota_0.end, APP_ALIGNMENT),
        f"{spec.name}: ota_1 is not contiguous after app alignment",
    )
    return ota_0, ota_1


def find_stock_csv(repo_root: Path, override: Path | None) -> Path:
    if override is not None:
        return override.expanduser().resolve()

    candidates = [repo_root / STOCK_PARTITION_CSV]
    for variable in ("PLATFORMIO_CORE_DIR", "PLATFORMIO_HOME_DIR"):
        value = os.environ.get(variable)
        if value:
            candidates.append(
                Path(value).expanduser()
                / "packages/framework-espidf/components/partition_table"
                / STOCK_PARTITION_CSV
            )
    candidates.append(
        Path.home()
        / ".platformio/packages/framework-espidf/components/partition_table"
        / STOCK_PARTITION_CSV
    )
    for candidate in candidates:
        if candidate.is_file():
            return candidate.resolve()
    searched = ", ".join(str(candidate) for candidate in candidates)
    raise CheckError(f"stock partition CSV not found; searched: {searched}")


def display_path(path: Path, repo_root: Path) -> str:
    try:
        return str(path.relative_to(repo_root))
    except ValueError:
        if path.name == STOCK_PARTITION_CSV:
            return f"framework/{STOCK_PARTITION_CSV}"
        return str(path)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--stock-csv",
        type=Path,
        help="path to ESP-IDF's stock partitions_two_ota_large.csv",
    )
    args = parser.parse_args(argv)

    repo_root = Path(__file__).resolve().parents[1]
    try:
        stock_csv = find_stock_csv(repo_root, args.stock_csv)
    except CheckError as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1

    print("Partition-table invariant check")
    print("Reference firmware sizes: plans/33-wifi-hub.md:1296-1304, commit 2b79ce8")
    print("The headless S3 env uses the measured m5stick-s3 image as its reference.")
    print()

    passed = 0
    for spec in BOARD_SPECS:
        csv_path = stock_csv if spec.csv_name == STOCK_PARTITION_CSV else repo_root / spec.csv_name
        try:
            ota_0, ota_1 = check_board(spec, csv_path)
        except (CheckError, OSError) as exc:
            print(f"{spec.name}: FAIL: {exc}")
            return 1
        print(
            f"{spec.name}: PASS "
            f"flash={spec.flash_size // (1024 * 1024)}MB "
            f"csv={display_path(csv_path, repo_root)} "
            f"ota_0/ota_1=0x{ota_0.size:x} "
            f"reference_app={spec.reference_app_size}"
        )
        passed += 1

    print(f"PASS: {passed} boards")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
