#!/usr/bin/env python3
"""Validate the self-contained firmware files used by GitHub Pages."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
  sys.path.insert(0, str(ROOT))

from tools.web_installer_layout import (  # noqa: E402  - after the sys.path fix
  BOOTLOADER_OFFSETS,
  chip_family_for_platform,
  flash_offsets,
)


RELEASE_PLATFORMS = (
  "m5stack-core",
  "m5stack-core2",
  "m5stick-c",
  "m5stick-c-plus",
  "m5stick-s3",
  "waveshare-s3-eth",
)

# Derived from the one layout table, so a manifest cannot pass here with a
# bootloader offset the generator does not use.
EXPECTED_FLASH_OFFSETS = {
  family: flash_offsets(family) for family in BOOTLOADER_OFFSETS
}


def build_family_error(builds):
  """Return an error unless builds hold exactly one supported chip family.

  ESP Web Tools selects the build whose `chipFamily` matches the chip it
  detected. A second entry for another family is never flashed on a matching
  board, but it does let a user whose board belongs to the other family flash
  this image at the wrong bootloader offset, which leaves the device
  unbootable until it is re-flashed over UART.
  """
  if not isinstance(builds, list):
    return "builds must be a list"
  families = [
    build.get("chipFamily") if isinstance(build, dict) else None
    for build in builds
  ]
  if len(families) == 1 and families[0] in EXPECTED_FLASH_OFFSETS:
    return None
  return (
    "expected exactly one build for one supported chip family "
    f"{sorted(EXPECTED_FLASH_OFFSETS)}, got {families!r}"
  )


def expected_manifests():
  return {
    f"manifest_{platform}{variant}.json"
    for platform in RELEASE_PLATFORMS
    for variant in ("", "-debug")
    if not (platform == "m5stack-core" and variant == "-debug")
  }


def expected_chip_family(manifest_name: str) -> tuple[str | None, str | None]:
  """Return the chip family a manifest file name must declare, plus any error.

  The name carries the PlatformIO environment (`m5stick-s3`, or `m5stick-s3-debug`
  for the debug variant), and that environment's sdkconfig names the target.
  """
  prefix, suffix = "manifest_", ".json"
  if not (manifest_name.startswith(prefix) and manifest_name.endswith(suffix)):
    return None, f"unexpected manifest name {manifest_name!r}"
  platform = manifest_name[len(prefix):-len(suffix)]
  try:
    return chip_family_for_platform(platform), None
  except ValueError as error:
    return None, str(error)


def flash_offset_error(build, expected_family: str | None = None):
  """Return an error when a generated manifest has the wrong flash map."""
  chip_family = build.get("chipFamily") if isinstance(build, dict) else None
  expected = EXPECTED_FLASH_OFFSETS.get(chip_family)
  parts = build.get("parts") if isinstance(build, dict) else None
  if expected is None:
    return f"unsupported chip family: {chip_family!r}"
  if expected_family is not None and chip_family != expected_family:
    return (
      f"expected chipFamily {expected_family!r} for this board, "
      f"got {chip_family!r}"
    )
  if not isinstance(parts, list):
    return f"{chip_family}: missing flash parts"
  if any(not isinstance(part, dict) for part in parts):
    return f"{chip_family}: flash parts must be objects"
  actual = tuple(
    part.get("offset") for part in parts
  )
  if actual != expected:
    return (
      f"{chip_family}: expected flash offsets "
      f"{tuple(hex(offset) for offset in expected)}, got "
      f"{tuple(hex(offset) if isinstance(offset, int) else offset for offset in actual)}"
    )
  return None


def validate(site: Path):
  manifests = sorted(site.glob("manifest_*.json"))
  actual_names = {manifest.name for manifest in manifests}
  missing = expected_manifests() - actual_names
  unexpected = actual_names - expected_manifests()
  errors = []
  if missing:
    errors.append("missing manifests: " + ", ".join(sorted(missing)))
  if unexpected:
    errors.append("unexpected manifests: " + ", ".join(sorted(unexpected)))

  referenced = set()
  for manifest_path in manifests:
    expected_family, name_error = expected_chip_family(manifest_path.name)
    if name_error:
      errors.append(f"{manifest_path.name}: {name_error}")
      continue
    try:
      document = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
      errors.append(f"{manifest_path.name}: invalid JSON: {exc}")
      continue
    version = document.get("version")
    builds = document.get("builds")
    if not isinstance(version, str) or not version:
      errors.append(f"{manifest_path.name}: missing version")
    if not isinstance(builds, list) or not builds:
      errors.append(f"{manifest_path.name}: missing builds")
      continue
    family_error = build_family_error(builds)
    if family_error:
      errors.append(f"{manifest_path.name}: {family_error}")
    for build in builds:
      offset_error = flash_offset_error(build, expected_family)
      if offset_error:
        errors.append(f"{manifest_path.name}: {offset_error}")
      parts = build.get("parts") if isinstance(build, dict) else None
      if not isinstance(parts, list) or len(parts) != 4:
        errors.append(f"{manifest_path.name}: expected four flash parts")
        continue
      for part in parts:
        path = part.get("path") if isinstance(part, dict) else None
        if not isinstance(path, str) or not path.startswith("firmware/"):
          errors.append(f"{manifest_path.name}: non-local asset path {path!r}")
          continue
        relative = Path(path)
        if relative.is_absolute() or ".." in relative.parts:
          errors.append(f"{manifest_path.name}: unsafe asset path {path!r}")
          continue
        asset = site / relative
        referenced.add(relative)
        if not asset.is_file() or asset.stat().st_size == 0:
          errors.append(f"{manifest_path.name}: missing or empty asset {path}")
        if version and not asset.name.endswith(f"-{version}.bin"):
          errors.append(f"{manifest_path.name}: asset does not match version: {path}")

  assets = {
    path.relative_to(site)
    for path in (site / "firmware").rglob("*.bin")
  } if (site / "firmware").is_dir() else set()
  if assets - referenced:
    errors.append(
      "unreferenced firmware assets: "
      + ", ".join(str(path) for path in sorted(assets - referenced))
    )
  if errors:
    raise SystemExit("web installer validation failed:\n" + "\n".join(errors))
  print(f"validated {len(manifests)} manifests and {len(assets)} firmware assets")


if __name__ == "__main__":
  parser = argparse.ArgumentParser()
  parser.add_argument("site", type=Path)
  validate(parser.parse_args().site)
