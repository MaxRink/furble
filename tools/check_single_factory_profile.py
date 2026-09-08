#!/usr/bin/env python3
"""Validate the developer-only Core single-factory profile."""

from __future__ import annotations

import argparse
import re
from pathlib import Path

from check_partition_tables import CheckError, parse_csv, parse_size


FLASH_SIZE = 4 * 1024 * 1024
APP_OFFSET = 0x20000
FACTORY_SIZE = 0x3E0000
CSV_NAME = "partitions_single_factory_4m.csv"
PROFILE_NAME = "m5stack-core-usb-debug"
FRAGMENT_NAME = "sdkconfig.m5stack-core-usb-debug"


def require(condition: bool, message: str) -> None:
  if not condition:
    raise CheckError(message)


def profile_section(text: str) -> str:
  match = re.search(rf"(?ms)^\[env:{re.escape(PROFILE_NAME)}\]\s*\n(.*?)(?=^\[|\Z)", text)
  require(match is not None, f"platformio.ini: [{PROFILE_NAME}] is missing")
  return match.group(1)


def option(section: str, name: str) -> str | None:
  match = re.search(rf"(?m)^{re.escape(name)}\s*=\s*([^\s#]+)", section)
  return match.group(1) if match is not None else None


def check_platformio(repo_root: Path) -> None:
  text = (repo_root / "platformio.ini").read_text(encoding="utf-8")
  section = profile_section(text)
  require(
      re.search(r"(?m)^extends\s*=\s*env:m5stack-core-debug\s*$", section) is not None,
      f"[{PROFILE_NAME}]: must extend m5stack-core-debug",
  )
  require(option(section, "board_build.partitions") == CSV_NAME,
          f"[{PROFILE_NAME}]: wrong partition CSV")
  require(option(section, "board_build.esp-idf.sdkconfig_fragment")
          == "${platformio.src_dir}/../" + FRAGMENT_NAME,
          f"[{PROFILE_NAME}]: wrong sdkconfig fragment")

  global_match = re.search(
      r"(?ms)^\[env\]\s*$.*?^board_upload\.offset_address\s*=\s*([^\s;#]+)",
      text,
  )
  require(global_match is not None, "platformio.ini: global upload offset is missing")
  require(parse_size(global_match.group(1)) == APP_OFFSET,
          "platformio.ini: upload app offset must remain 0x20000")


def check_manifest(repo_root: Path) -> None:
  manifest = (repo_root / "web-installer" / "manifest.tmpl").read_text(encoding="utf-8")
  app_offsets = [
      int(value)
      for value in re.findall(
          r'"path":\s*"[^"]+/furble-\$PLATFORM-\$VERSION\.bin",\s*'
          r'"offset":\s*(\d+)',
          manifest,
      )
  ]
  require(app_offsets and all(value == APP_OFFSET for value in app_offsets),
          "web-installer/manifest.tmpl: published app offset changed")
  ota_parts = re.findall(r"ota_data_initial", manifest)
  require(len(ota_parts) == 2,
          "web-installer/manifest.tmpl: published OTA parts must remain present")


def check_table(csv_path: Path) -> None:
  partitions = parse_csv(csv_path)
  require([partition.name for partition in partitions] == ["nvs", "phy_init", "factory"],
          "single-factory table must contain nvs, phy_init, factory in order")
  require(all(partition.end <= FLASH_SIZE for partition in partitions),
          "single-factory table exceeds 4 MB flash")

  nvs, phy_init, factory = partitions
  require((nvs.kind, nvs.subtype) == ("data", "nvs"),
          "nvs must be a data/nvs partition")
  require((nvs.offset, nvs.size) == (0x9000, 0x6000),
          "nvs must remain at 0x9000 with size 0x6000")
  require((phy_init.kind, phy_init.subtype) == ("data", "phy"),
          "phy_init must be a data/phy partition")
  require((phy_init.offset, phy_init.size) == (0x11000, 0x1000),
          "phy_init must remain at 0x11000 with size 0x1000")
  require(factory.kind == "app" and factory.subtype == "factory",
          "the only app partition must be factory")
  require((factory.offset, factory.size) == (APP_OFFSET, FACTORY_SIZE),
          "factory must occupy 0x20000..0x400000")
  require(not any(partition.name == "otadata" for partition in partitions),
          "single-factory table must not contain otadata")
  require(not any(partition.subtype in {"ota_0", "ota_1"} for partition in partitions),
          "single-factory table must not contain OTA app slots")


def main(argv: list[str] | None = None) -> int:
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("--repo-root", type=Path,
                      default=Path(__file__).resolve().parents[1])
  parser.add_argument("--csv", type=Path)
  args = parser.parse_args(argv)
  repo_root = args.repo_root.resolve()
  csv_path = (args.csv or (repo_root / CSV_NAME)).resolve()
  try:
    check_table(csv_path)
    check_platformio(repo_root)
    check_manifest(repo_root)
  except (CheckError, OSError) as exc:
    print(f"FAIL: {exc}")
    return 1
  print(f"PASS: {PROFILE_NAME} single-factory table and USB upload contract")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
