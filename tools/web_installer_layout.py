#!/usr/bin/env python3
"""The chip family and flash map of one web installer manifest.

`web-installer/generate-manifest.py` writes the manifest that ESP Web Tools
flashes. ESP Web Tools picks its `builds` entry by the chip family it detects on
the connected board, so a manifest that offers a second family lets a user flash
one board's binaries at another board's bootloader offset. Plan 372 records the
defect that shipped that way: every manifest carried an ESP32 entry with the
bootloader at 0x1000 and an ESP32-S3 entry with it at 0x0, both pointing at the
same image.

The generator, `tools/verify_web_installer.py` and `tests/test_web_installer.py`
share this module, so the chip family and the four flash offsets have exactly
one definition. Which family a board targets is not listed here. It is read from
the sdkconfig the PlatformIO environment builds with, which is the file that
produced the binaries in the first place.
"""

from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]

# ESP-IDF target name (CONFIG_IDF_TARGET) to the `chipFamily` string ESP Web
# Tools matches against the chip it detected. A new board on a new family is
# added here with its bootloader offset below, after checking the load address
# ESP-IDF uses for that family. Guessing it is how the offsets get wrong.
CHIP_FAMILIES = {
  "esp32": "ESP32",
  "esp32s3": "ESP32-S3",
}

# The second stage bootloader sits where the ESP-IDF ROM expects to load it.
# ESP32 loads it at 0x1000, ESP32-S3 at 0x0. Both bootloaders are written by the
# same `bootloader.bin` artifact, so the offset is the only thing that differs.
BOOTLOADER_OFFSETS = {
  "ESP32": 0x1000,
  "ESP32-S3": 0x0,
}

# Shared by both supported families. The partition table, the OTA data record
# and the application slot do not move when the bootloader does.
PARTITION_TABLE_OFFSET = 0x8000
OTA_DATA_OFFSET = 0xF000
APP_OFFSET = 0x20000

TARGET_PATTERN = re.compile(r'^CONFIG_IDF_TARGET="([^"]+)"$', re.MULTILINE)


def flash_offsets(chip_family: str) -> tuple[int, int, int, int]:
  """Return the bootloader, partition table, OTA data and app offsets."""
  try:
    bootloader = BOOTLOADER_OFFSETS[chip_family]
  except KeyError:
    raise ValueError(f"unsupported chip family: {chip_family!r}") from None
  return (bootloader, PARTITION_TABLE_OFFSET, OTA_DATA_OFFSET, APP_OFFSET)


def bootloader_offset(chip_family: str) -> int:
  """Return the offset of `bootloader.bin` for a chip family."""
  return flash_offsets(chip_family)[0]


def sdkconfig_for_platform(platform: str, root: Path = ROOT) -> Path:
  """Return the sdkconfig a PlatformIO environment builds with.

  PlatformIO hands ESP-IDF `sdkconfig.<env>` unless the environment points
  `board_build.esp-idf.sdkconfig_path` somewhere else, which every `*-debug`
  environment does for its release board. Both spellings start with the base
  board name, so the longest base that prefixes the environment wins:
  `m5stick-s3-debug` resolves to `sdkconfig.m5stick-s3`, never to
  `sdkconfig.m5stick-c`. Fragments such as `sdkconfig.m5stack-core-usb-debug`
  layer on a release file and carry no target of their own, so a file without
  `CONFIG_IDF_TARGET` is not a base.
  """
  matches: list[tuple[int, str, Path]] = []
  for path in sorted(root.glob("sdkconfig.*")):
    try:
      text = path.read_text(encoding="utf-8", errors="replace")
    except OSError:
      continue
    if TARGET_PATTERN.search(text) is None:
      continue
    base = path.name.split(".", 1)[1]
    if platform == base or platform.startswith(base + "-"):
      matches.append((len(base), base, path))
  if not matches:
    raise ValueError(
      f"{platform}: no sdkconfig.<board> in {root} declares CONFIG_IDF_TARGET "
      "for this environment. Add one, or check the PLATFORM spelling."
    )
  return max(matches)[2]


def idf_target(platform: str, root: Path = ROOT) -> str:
  """Return the CONFIG_IDF_TARGET value of an environment's sdkconfig."""
  path = sdkconfig_for_platform(platform, root)
  match = TARGET_PATTERN.search(path.read_text(encoding="utf-8"))
  assert match is not None, path
  return match.group(1)


def chip_family_for_platform(platform: str, root: Path = ROOT) -> str:
  """Return the ESP Web Tools chip family a PlatformIO environment targets."""
  target = idf_target(platform, root)
  family = CHIP_FAMILIES.get(target)
  if family is None:
    raise ValueError(
      f"{platform}: ESP-IDF target {target!r} has no ESP Web Tools chip "
      "family. Add it to CHIP_FAMILIES in tools/web_installer_layout.py with "
      "its bootloader offset, taken from the ESP-IDF load address."
    )
  return family
