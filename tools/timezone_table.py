#!/usr/bin/env python3
"""Validate and refresh the curated timezone table.

The firmware table in src/FurbleTimezone.cpp is small on purpose: a POSIX TZ
string per supported zone plus representative coordinates for nearest-zone
lookup. This script checks the invariants that a wrong table edit would break,
and it prints zone.tab suggestions for a refresh against a newer tzdata.

Usage:
  python3 tools/timezone_table.py --check
  python3 tools/timezone_table.py --zone-tab /usr/share/zoneinfo/zone.tab
  python3 tools/timezone_table.py --check --zone-tab /path/to/zone1970.tab

Refresh procedure:
  1. Update the POSIX strings from
     https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv.
  2. Update representative coordinates from zone1970.tab or zone.tab.
  3. Bump TZDATA_REVIEWED in src/FurbleTimezone.cpp.
  4. Run --check, then the host test timezone.
"""

from __future__ import annotations

import argparse
import os
import re
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "src" / "FurbleTimezone.cpp"

ZONE_ROW = re.compile(
    r"\{Id::([A-Z0-9_]+),\s*\"([^\"]+)\",\s*\"([^\"]+)\",\s*\"([^\"]+)\","
    r"\s*(-?\d+),\s*(\d+)\s*\}")
POINT_ROW = re.compile(r"\{Id::([A-Z0-9_]+),\s*(-?\d+),\s*(-?\d+)\s*\}")
REVIEWED = re.compile(r"TZDATA_REVIEWED\s*=\s*\"([^\"]+)\"")
ZONE_TAB_ROW = re.compile(r"^[A-Z]{2}\t([+-]\d{4,7})([+-]\d{5,7})\t(\S+)")


def coordinate_e2(value: str) -> int:
  sign = -1 if value[0] == "-" else 1
  body = value[1:]
  if len(body) == 4:
    degrees = int(body[:2]) + int(body[2:]) / 60.0
  elif len(body) == 5:
    degrees = int(body[:3]) + int(body[3:]) / 60.0
  elif len(body) == 6:
    degrees = int(body[:2]) + int(body[2:4]) / 60.0 + int(body[4:]) / 3600.0
  elif len(body) == 7:
    degrees = int(body[:3]) + int(body[3:5]) / 60.0 + int(body[5:]) / 3600.0
  else:
    raise ValueError(f"unsupported coordinate {value}")
  return round(sign * degrees * 100)


def parse_source(text: str) -> tuple[str | None, list[dict], list[dict]]:
  reviewed = REVIEWED.search(text)
  zones: list[dict] = []
  for match in ZONE_ROW.finditer(text):
    zones.append({
        "id": match.group(1),
        "name": match.group(2),
        "label": match.group(3),
        "posix": match.group(4),
        "standard": int(match.group(5)),
        "dst_shift": int(match.group(6)),
    })
  points: list[dict] = []
  for match in POINT_ROW.finditer(text):
    points.append({
        "id": match.group(1),
        "lat_e2": int(match.group(2)),
        "lon_e2": int(match.group(3)),
    })
  return (reviewed.group(1) if reviewed else None), zones, points


def check_table(reviewed: str | None, zones: list[dict], points: list[dict]) -> list[str]:
  errors: list[str] = []
  if reviewed is None:
    errors.append("TZDATA_REVIEWED is missing")

  ids = {zone["id"] for zone in zones}
  names: set[str] = set()
  labels: set[str] = set()
  for zone in zones:
    if zone["name"] in names:
      errors.append(f"duplicate zone name {zone['name']}")
    names.add(zone["name"])
    if zone["label"] in labels:
      errors.append(f"duplicate zone label {zone['label']}")
    labels.add(zone["label"])
    if not (-720 <= zone["standard"] <= 840):
      errors.append(f"{zone['name']} standard offset is out of range")
    if zone["dst_shift"] not in (0, 30, 60, 120):
      errors.append(f"{zone['name']} DST shift is not a plausible value")

    if zone["id"] != "UTC":
      try:
        previous = os.environ.get("TZ")
        os.environ["TZ"] = zone["posix"]
        time.tzset()
        time.localtime(1768478400)  # 2026-01-15, northern winter
        time.localtime(1784246400)  # 2026-07-16, northern summer
        if previous is None:
          os.environ.pop("TZ", None)
        else:
          os.environ["TZ"] = previous
        time.tzset()
      except (OSError, ValueError) as error:
        errors.append(f"{zone['name']} POSIX string does not apply: {error}")

  for point in points:
    if point["id"] not in ids:
      errors.append(f"auto point references unknown id {point['id']}")
    if not (-9000 <= point["lat_e2"] <= 9000):
      errors.append(f"auto point latitude out of range for {point['id']}")
    if not (-18000 <= point["lon_e2"] <= 18000):
      errors.append(f"auto point longitude out of range for {point['id']}")

  ids_with_points = {point["id"] for point in points}
  for zone in zones:
    if zone["id"] == "UTC":
      continue
    if zone["id"] not in ids_with_points:
      errors.append(f"{zone['name']} has no auto point")

  return errors


def read_zone_tab(path: Path) -> list[dict]:
  rows: list[dict] = []
  for line in path.read_text(encoding="utf-8").splitlines():
    match = ZONE_TAB_ROW.match(line)
    if match is None:
      continue
    rows.append({
        "name": match.group(3),
        "lat_e2": coordinate_e2(match.group(1)),
        "lon_e2": coordinate_e2(match.group(2)),
    })
  return rows


def print_suggestions(zones: list[dict], zone_tab: list[dict]) -> None:
  by_name = {row["name"]: row for row in zone_tab}
  for zone in zones:
    if zone["id"] == "UTC":
      continue
    row = by_name.get(zone["name"])
    if row is None:
      print(f"# no zone.tab row for {zone['name']}")
      continue
    print(f"    {{Id::{zone['id']}, {row['lat_e2']:5d}, {row['lon_e2']:5d}}},"
          f"  // {zone['name']}")


def main(argv: list[str]) -> int:
  parser = argparse.ArgumentParser(description=__doc__,
                                   formatter_class=argparse.RawDescriptionHelpFormatter)
  parser.add_argument("--check", action="store_true", help="validate the checked-in table")
  parser.add_argument("--zone-tab", type=Path,
                      help="print representative coordinates from a tzdata zone.tab")
  args = parser.parse_args(argv)

  reviewed, zones, points = parse_source(SOURCE.read_text(encoding="utf-8"))
  print(f"zones: {len(zones)}")
  print(f"auto points: {len(points)}")
  print(f"tzdata review: {reviewed or 'missing'}")

  failed = False
  if args.check:
    errors = check_table(reviewed, zones, points)
    for error in errors:
      print(f"ERROR: {error}", file=sys.stderr)
    failed = bool(errors)

  if args.zone_tab is not None:
    print_suggestions(zones, read_zone_tab(args.zone_tab))

  if args.check:
    print("timezone table: PASS" if not failed else "timezone table: FAIL")
  return 1 if failed else 0


if __name__ == "__main__":
  raise SystemExit(main(sys.argv[1:]))
