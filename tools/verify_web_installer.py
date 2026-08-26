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


def expected_manifests():
  return {
    f"manifest_{platform}{variant}.json"
    for platform in RELEASE_PLATFORMS
    for variant in ("", "-debug")
  }


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
    for build in builds:
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
