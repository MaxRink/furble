#!/usr/bin/env python3
"""Build firmware twice from different absolute paths and compare artifacts."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


LOCK_NAME = "tools/reproducible-build.lock"
ARTIFACTS = (
    "firmware.bin",
    "firmware.elf",
    "bootloader.bin",
    "partitions.bin",
    "ota_data_initial.bin",
)


@dataclass(frozen=True)
class BuildResult:
  root: Path
  hashes: dict[str, str]


def run(command: list[str], *, cwd: Path, environment: dict[str, str]) -> None:
  print("+", " ".join(command))
  subprocess.run(command, cwd=cwd, env=environment, check=True)


def commit_metadata(root: Path) -> tuple[int, str]:
  epoch = subprocess.check_output(
      ["git", "show", "-s", "--format=%ct", "HEAD"],
      cwd=root,
      text=True,
  ).strip()
  version = subprocess.check_output(
      ["git", "describe", "--tags", "--always", "HEAD"],
      cwd=root,
      text=True,
  ).strip()
  if not epoch.isdigit():
    raise RuntimeError("git returned a non-numeric commit timestamp")
  return int(epoch), version


def check_inputs(root: Path) -> None:
  lock_path = root / LOCK_NAME
  lock = {}
  for line in lock_path.read_text(encoding="utf-8").splitlines():
    line = line.strip()
    if not line or line.startswith("#"):
      continue
    key, separator, value = line.partition("=")
    if not separator or not key or not value or key in lock:
      raise RuntimeError(f"invalid lock entry in {lock_path}: {line!r}")
    lock[key] = value

  requirements = (root / "requirements.txt").read_text(encoding="utf-8")
  platformio = (root / "platformio.ini").read_text(encoding="utf-8")
  expected = {
      "platformio": (requirements, f"platformio=={lock.get('platformio', '')}"),
      "platform": (platformio, f"platform = {lock.get('platform', '')}"),
      "framework": (platformio, f"framework-espidf@{lock.get('framework', '')}"),
      "M5PM1": (platformio, f"M5PM1@{lock.get('M5PM1', '')}"),
      "M5GFX": (platformio, f"M5GFX@{lock.get('M5GFX', '')}"),
      "M5Unified": (platformio, f"M5Unified@{lock.get('M5Unified', '')}"),
      "TinyGPSPlus": (platformio, f"TinyGPSPlus#{lock.get('TinyGPSPlus', '')}"),
  }
  missing = [
      key
      for key, (text, needle) in expected.items()
      if not lock.get(key) or needle not in text
  ]
  if missing:
    raise RuntimeError("locked build inputs do not match project files: " + ", ".join(missing))
  print("Locked build inputs verified from", LOCK_NAME)


def copy_source(source: Path, destination: Path) -> None:
  ignored_names = {
      ".git",
      ".pio",
      ".pio-core",
      ".vscode",
      ".claude",
      ".worktrees",
      "build",
  }

  def ignore(_directory: str, names: list[str]) -> set[str]:
    return {name for name in names if name in ignored_names}

  shutil.copytree(source, destination, ignore=ignore)


def prepare_platformio_core(
    destination: Path, inherited_environment: dict[str, str]
) -> dict[str, str]:
  """Create an isolated PlatformIO core for one reproducibility build.

  PlatformIO normally shares its framework package between every checkout.
  This project patches one framework source file, so sharing that package
  would make the gate depend on build order and would race concurrent builds.
  Read-only packages and platforms can be linked from the user's cache. The
  patched ESP-IDF framework is copied so the pre-build script can mutate only
  this build's package.
  """
  base_core = Path(
      inherited_environment.get("PLATFORMIO_CORE_DIR", "")
      or (Path.home() / ".platformio")
  )
  core = destination / "pio-core"
  packages = core / "packages"
  platforms = core / "platforms"
  if core.exists():
    if not packages.is_dir() or not platforms.is_dir():
      raise RuntimeError(f"incomplete isolated PlatformIO core: {core}")
  else:
    packages.mkdir(parents=True)
    platforms.mkdir(parents=True)

  source_packages = base_core / "packages"
  if source_packages.is_dir():
    for package in source_packages.iterdir():
      target = packages / package.name
      if package.name == "framework-espidf" and package.is_dir():
        if not target.exists():
          shutil.copytree(package, target)
      elif not target.exists() and not target.is_symlink():
        target.symlink_to(package, target_is_directory=package.is_dir())

  source_platforms = base_core / "platforms"
  if source_platforms.is_dir():
    for platform in source_platforms.iterdir():
      target = platforms / platform.name
      if not target.exists() and not target.is_symlink():
        target.symlink_to(platform, target_is_directory=platform.is_dir())

  environment = dict(inherited_environment)
  environment.update(
      {
          "PLATFORMIO_CORE_DIR": str(core),
          "PLATFORMIO_PACKAGES_DIR": str(packages),
          "PLATFORMIO_PLATFORMS_DIR": str(platforms),
          "PLATFORMIO_CACHE_DIR": str(core / ".cache"),
      }
  )
  return environment


def build_once(
    project: Path,
    environment_name: str,
    epoch: int,
    version: str,
    inherited_environment: dict[str, str],
) -> BuildResult:
  child_environment = dict(inherited_environment)
  child_environment.update(
      {
          "FURBLE_VERSION": version,
          "FURBLE_TEST": "0",
          "SOURCE_DATE_EPOCH": str(epoch),
      }
  )
  child_environment = prepare_platformio_core(project.parent, child_environment)
  run(
      [
          "platformio",
          "run",
          "--project-dir",
          str(project),
          "--environment",
          environment_name,
      ],
      cwd=project,
      environment=child_environment,
  )

  output = project / ".pio" / "build" / environment_name
  hashes = {}
  for name in ARTIFACTS:
    path = output / name
    if not path.is_file():
      raise RuntimeError(f"PlatformIO did not produce {path}")
    hashes[name] = hashlib.sha256(path.read_bytes()).hexdigest()
  return BuildResult(project, hashes)


def compare(first: BuildResult, second: BuildResult, label: str) -> None:
  differences = [
      name
      for name in ARTIFACTS
      if first.hashes[name] != second.hashes[name]
  ]
  if differences:
    print(f"{label} differences:")
    for name in differences:
      print(f"  {name}: {first.hashes[name]} != {second.hashes[name]}")
    raise RuntimeError(f"{label} artifacts differ")
  print(f"{label}: {len(ARTIFACTS)} artifacts match byte-for-byte")


def main() -> int:
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument(
      "--env",
      dest="environments",
      action="append",
      help="release PlatformIO environment, repeat to test more than one board",
  )
  parser.add_argument(
      "--negative-version",
      action="store_true",
      help="also prove that changing FURBLE_VERSION changes firmware artifacts",
  )
  parser.add_argument(
      "--keep",
      action="store_true",
      help="keep the two or three temporary build directories",
  )
  parser.add_argument(
      "--check-inputs",
      action="store_true",
      help="verify pinned build inputs and exit without compiling",
  )
  args = parser.parse_args()

  source = Path(__file__).resolve().parents[1]
  check_inputs(source)
  if args.check_inputs:
    return 0

  environments = args.environments or [
      "m5stick-c",
      "m5stick-c-plus",
      "m5stack-core",
      "m5stack-core2",
      "m5stick-s3",
  ]
  epoch, version = commit_metadata(source)
  print(f"SOURCE_DATE_EPOCH={epoch}")
  print(f"FURBLE_VERSION={version}")
  inherited_environment = dict(os.environ)

  temporary_root = Path(tempfile.mkdtemp(prefix="furble-repro-"))
  first_root = temporary_root / "a" / "source"
  second_root = temporary_root / "b" / "source"
  copy_source(source, first_root)
  copy_source(source, second_root)

  try:
    for environment_name in environments:
      print(f"\n== {environment_name} ==")
      first = build_once(first_root, environment_name, epoch, version, inherited_environment)
      second = build_once(second_root, environment_name, epoch, version, inherited_environment)
      compare(first, second, "reproducibility")

      if args.negative_version:
        negative_root = temporary_root / "negative" / "source"
        copy_source(source, negative_root)
        negative = build_once(
            negative_root,
            environment_name,
            epoch,
            version + "+changed",
            inherited_environment,
        )
        if all(first.hashes[name] == negative.hashes[name] for name in ARTIFACTS):
          raise RuntimeError("changing FURBLE_VERSION did not change any artifact")
        print("negative version test: artifacts changed as expected")
        shutil.rmtree(negative_root.parent)
  finally:
    if args.keep:
      print("Kept reproducibility builds in", temporary_root)
    else:
      shutil.rmtree(temporary_root)
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
