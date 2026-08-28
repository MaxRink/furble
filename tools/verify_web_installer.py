#!/usr/bin/env python3
"""Validate the self-contained firmware files used by GitHub Pages."""

import argparse
import json
from pathlib import Path


RELEASE_PLATFORMS = (
  "m5stack-core",
  "m5stack-core2",
  "m5stick-c",
  "m5stick-c-plus",
  "m5stick-s3",
  "waveshare-s3-eth",
)

EXPECTED_FLASH_OFFSETS = {
  "ESP32": (0x1000, 0x8000, 0xF000, 0x20000),
  "ESP32-S3": (0x0, 0x8000, 0xF000, 0x20000),
}


def build_family_error(builds):
  """Return an error unless builds contain one of each supported chip family."""
  if not isinstance(builds, list):
    return "builds must be a list"
  families = [
    build.get("chipFamily") if isinstance(build, dict) else None
    for build in builds
  ]
  expected = tuple(EXPECTED_FLASH_OFFSETS)
  if (
    len(families) != len(expected)
    or any(not isinstance(family, str) for family in families)
    or any(families.count(family) != 1 for family in expected)
  ):
    return (
      "expected exactly one build for each chip family "
      f"{list(expected)}, got {families!r}"
    )
  return None


def expected_manifests():
  return {
    f"manifest_{platform}{variant}.json"
    for platform in RELEASE_PLATFORMS
    for variant in ("", "-debug")
  }


def flash_offset_error(build):
  """Return an error when a generated manifest has the wrong flash map."""
  chip_family = build.get("chipFamily") if isinstance(build, dict) else None
  expected = EXPECTED_FLASH_OFFSETS.get(chip_family)
  parts = build.get("parts") if isinstance(build, dict) else None
  if expected is None:
    return f"unsupported chip family: {chip_family!r}"
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
    try:
      document = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
      errors.append(f"{manifest_path.name}: invalid JSON: {exc}")
      continue
    version = document.get("version")
    builds = document.get("builds")
    if not isinstance(version, str) or not version:
      errors.append(f"{manifest_path.name}: missing version")
    if not isinstance(builds, list) or len(builds) != 2:
      errors.append(f"{manifest_path.name}: expected ESP32 and ESP32-S3 builds")
      continue
    family_error = build_family_error(builds)
    if family_error:
      errors.append(f"{manifest_path.name}: {family_error}")
    for build in builds:
      offset_error = flash_offset_error(build)
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
