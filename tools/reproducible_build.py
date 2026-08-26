#!/usr/bin/env python3
"""Build firmware twice from different absolute paths and compare artifacts."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import re
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


def read_lock(root: Path) -> dict[str, str]:
  """Read the reproducibility lock file into a small, validated mapping."""
  lock = {}
  for line in (root / LOCK_NAME).read_text(encoding="utf-8").splitlines():
    line = line.strip()
    if not line or line.startswith("#"):
      continue
    key, separator, value = line.partition("=")
    if not separator or not key or not value or key in lock:
      raise RuntimeError(f"invalid lock entry in {root / LOCK_NAME}: {line!r}")
    lock[key] = value
  return lock


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
  lock = read_lock(root)

  requirements = (root / "requirements.txt").read_text(encoding="utf-8")
  platformio = (root / "platformio.ini").read_text(encoding="utf-8")
  sim_platformio = (root / "sim" / "platformio.ini").read_text(encoding="utf-8")
  sim_workflows = {
      name: (root / ".github" / "workflows" / name).read_text(encoding="utf-8")
      for name in ("sim-e2e.yml", "power-gate.yml", "ui-screenshots.yml")
  }
  component_manifest = (root / "src" / "idf_component.yml").read_text(encoding="utf-8")
  expected = {
      "platformio": (requirements, f"platformio=={lock.get('platformio', '')}"),
      "platform": (platformio, f"platform = {lock.get('platform', '')}"),
      "framework": (platformio, f"framework-espidf@{lock.get('framework', '')}"),
      "M5PM1": (platformio, f"M5PM1@{lock.get('M5PM1', '')}"),
      "M5GFX": (platformio, f"M5GFX@{lock.get('M5GFX', '')}"),
      "M5Unified": (platformio, f"M5Unified@{lock.get('M5Unified', '')}"),
      "TinyGPSPlus": (platformio, f"TinyGPSPlus#{lock.get('TinyGPSPlus', '')}"),
      "NimBLE": (
          component_manifest,
          'h2zero/esp-nimble-cpp:\n    version: "{}"'.format(lock.get("NimBLE", "")),
      ),
      "LVGL": (
          component_manifest,
          'lvgl/lvgl:\n    version: "{}"'.format(lock.get("LVGL", "")),
      ),
  }
  missing = [
      key
      for key, (text, needle) in expected.items()
      if not lock.get(key) or needle not in text
  ]
  aligned = {
      "sim-M5GFX": (sim_platformio, f"M5GFX@{lock.get('M5GFX', '')}"),
      "sim-M5Unified": (sim_platformio, f"M5Unified@{lock.get('M5Unified', '')}"),
  }
  for workflow_name, workflow in sim_workflows.items():
    aligned[f"{workflow_name}-M5GFX"] = (
        workflow,
        f"clone_tag m5stack/M5GFX {lock.get('M5GFX', '')} M5GFX",
    )
    aligned[f"{workflow_name}-M5Unified"] = (
        workflow,
        f"clone_tag m5stack/M5Unified {lock.get('M5Unified', '')} M5Unified",
    )
  missing.extend(
      key for key, (text, needle) in aligned.items() if not needle or needle not in text
  )
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


_VERSION_RE = re.compile(r"^(\d+)(?:\.(\d+))?(?:\.(\d+))?(?:[-+].*)?$")


def _version_tuple(version: str) -> tuple[int, int, int] | None:
  match = _VERSION_RE.fullmatch(version.strip())
  if not match:
    return None
  return tuple(int(part or 0) for part in match.groups())


def _version_satisfies(version: str, requirement: str) -> bool:
  """Match the PlatformIO version forms used by platform manifests.

  This intentionally handles only the small semver subset emitted by the
  Espressif platform (exact, ~, ^, and comparison operators). Unknown syntax
  is rejected so a cache entry is never reused on an assumption.
  """
  actual = _version_tuple(version)
  requirement = requirement.strip()
  if actual is None or not requirement or "||" in requirement or " " in requirement:
    return False
  operator = ""
  for candidate in (">=", "<=", ">", "<", "=", "~", "^"):
    if requirement.startswith(candidate):
      operator = candidate
      requirement = requirement[len(candidate):]
      break
  expected = _version_tuple(requirement)
  if expected is None:
    return False
  if operator in ("", "="):
    return actual == expected
  if operator == ">=":
    return actual >= expected
  if operator == "<=":
    return actual <= expected
  if operator == ">":
    return actual > expected
  if operator == "<":
    return actual < expected
  if operator == "~":
    return actual >= expected and actual < (expected[0], expected[1] + 1, 0)
  if operator == "^":
    return actual >= expected and actual < (expected[0] + 1, 0, 0)
  return False


def _safe_component(value: object, label: str) -> str:
  """Validate a cache name before using it in a filesystem path."""
  if (
      not isinstance(value, str)
      or not value
      or value in (".", "..")
      or "/" in value
      or "\\" in value
  ):
    raise RuntimeError(f"unsafe {label}: {value!r}")
  return value


def _contained_child(root: Path, component: str, label: str) -> Path:
  """Join one validated component and assert lexical/resolved containment."""
  child = root / _safe_component(component, label)
  try:
    child.relative_to(root)
    root_real = root.resolve(strict=True)
    child.parent.resolve(strict=True).relative_to(root_real)
  except ValueError as error:
    raise RuntimeError(f"unsafe {label}: {component!r}") from error
  except OSError as error:
    raise RuntimeError(f"unavailable {label}: {root}") from error
  return child


def _require_private_directory(path: Path, label: str) -> None:
  """Reject symlinked isolated directories before following them."""
  if path.is_symlink():
    raise RuntimeError(f"isolated {label} must not be a symlink: {path}")
  if not path.is_dir():
    raise RuntimeError(f"incomplete isolated {label}: {path}")


def _remove_isolated_entry(path: Path) -> None:
  """Remove one stale entry owned by the isolated PlatformIO core."""
  if path.is_symlink() or path.is_file():
    path.unlink()
  elif path.is_dir():
    shutil.rmtree(path)


def _manifest_identity(package: Path, names: tuple[str, ...]) -> tuple[str, str] | None:
  """Return a package identity only when all available metadata agrees.

  PlatformIO normally writes both ``.piopm`` and ``package.json`` (or
  ``platform.json``). Requiring both makes a half-restored cache fall back to
  the installer instead of creating a symlink that the installer cannot
  replace.
  """
  if not package.is_dir() or package.is_symlink():
    return None
  identities = []
  for name in names:
    metadata = package / name
    if not metadata.is_file():
      return None
    try:
      data = json.loads(metadata.read_text(encoding="utf-8"))
    except (OSError, ValueError, TypeError):
      return None
    package_name = data.get("name")
    version = data.get("version")
    if not isinstance(package_name, str) or not isinstance(version, str):
      return None
    _safe_component(package_name, "cached manifest name")
    identities.append((package_name, version))
  if not identities or any(identity != identities[0] for identity in identities[1:]):
    return None
  return identities[0]


def _platform_cache(
    source: Path, expected_name: str, expected_version: str
) -> tuple[Path, dict[str, dict[str, object]]] | None:
  """Return an exact platform cache and its dependency requirements."""
  identity = _manifest_identity(source, (".piopm", "platform.json"))
  if identity != (expected_name, expected_version):
    return None
  try:
    manifest = json.loads((source / "platform.json").read_text(encoding="utf-8"))
    requirements = manifest["packages"]
  except (OSError, ValueError, TypeError, KeyError):
    return None
  if not isinstance(requirements, dict):
    return None
  for package_name in requirements:
    _safe_component(package_name, "platform package name")
  return source, requirements


def _package_matches(
    package: Path, name: str, requirement: dict[str, object], exact_version: str = ""
) -> bool:
  identity = _manifest_identity(package, (".piopm", "package.json"))
  if identity is None or identity[0] != name:
    return False
  if exact_version and identity[1] != exact_version:
    return False
  versions = []
  primary = requirement.get("version")
  if isinstance(primary, str):
    versions.append(primary)
  alternatives = requirement.get("optionalVersions")
  if isinstance(alternatives, list):
    versions.extend(item for item in alternatives if isinstance(item, str))
  return bool(versions) and any(_version_satisfies(identity[1], item) for item in versions)


def prepare_platformio_core(
    destination: Path,
    inherited_environment: dict[str, str],
    *,
    project: Path | None = None,
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
  if destination.is_symlink() or core.is_symlink():
    raise RuntimeError(f"isolated PlatformIO core must not be a symlink: {core}")
  if core.exists():
    _require_private_directory(packages, "packages directory")
    _require_private_directory(platforms, "platforms directory")
  else:
    packages.mkdir(parents=True)
    platforms.mkdir(parents=True)

  source_platforms = base_core / "platforms"
  source_packages = base_core / "packages"
  lock = read_lock(project) if project is not None else {}
  platform_spec = lock.get("platform", "")
  platform_name, separator, platform_version = platform_spec.partition("@")
  if separator:
    _safe_component(platform_name, "locked platform name")
  cached_platform = None
  if separator and source_platforms.is_dir():
    cached_platform = _platform_cache(
        _contained_child(source_platforms, platform_name, "platform cache name"),
        platform_name,
        platform_version,
    )

  # A stale platform can make PlatformIO try to replace a symlink in the
  # isolated core, which fails with EEXIST. Link only the exact locked
  # platform. If it is absent or partial, let PlatformIO install a clean copy.
  if cached_platform is not None:
    platform, requirements = cached_platform
    platform_target = _contained_child(platforms, platform_name, "isolated platform name")
    if platform_target.is_symlink():
      if platform_target.resolve() != platform.resolve():
        _remove_isolated_entry(platform_target)
    elif platform_target.exists() and _platform_cache(
        platform_target, platform_name, platform_version
    ) is None:
      _remove_isolated_entry(platform_target)
    if not platform_target.exists() and not platform_target.is_symlink():
      platform_target.symlink_to(platform, target_is_directory=True)

    # Dependencies are validated against the exact platform manifest. In
    # particular, a stale generic package directory must not be linked: the
    # installer would otherwise need to replace that symlink and hit EEXIST.
    if source_packages.is_dir():
      package_paths = {
          package_name: (
              _contained_child(source_packages, package_name, "package cache name"),
              _contained_child(packages, package_name, "isolated package name"),
          )
          for package_name in requirements
      }
      for package_name, requirement in requirements.items():
        if not isinstance(requirement, dict):
          continue
        package, target = package_paths[package_name]
        exact_version = lock.get("framework", "") if package_name == "framework-espidf" else ""
        if not _package_matches(package, package_name, requirement, exact_version):
          if target.is_symlink() or target.exists():
            _remove_isolated_entry(target)
          continue
        if package_name == "framework-espidf":
          # The framework patch mutates files in-place; never leave a link to
          # the shared cache, even when it resolves to the same source.
          if target.is_symlink():
            _remove_isolated_entry(target)
          elif target.exists() and not _package_matches(
              target, package_name, requirement, exact_version
          ):
            _remove_isolated_entry(target)
          if not target.exists():
            shutil.copytree(package, target)
          continue
        if target.is_symlink() and target.resolve() != package.resolve():
          _remove_isolated_entry(target)
        elif target.exists() and not target.is_symlink() and not _package_matches(
            target, package_name, requirement
        ):
          _remove_isolated_entry(target)
        if not target.exists() and not target.is_symlink():
          target.symlink_to(package, target_is_directory=True)
  elif separator:
    # A prior interrupted invocation may have left a stale link in the
    # temporary core. Remove stale entries only inside that isolated core.
    platform_target = _contained_child(platforms, platform_name, "isolated platform name")
    if platform_target.is_symlink():
      _remove_isolated_entry(platform_target)
    elif platform_target.exists() and _platform_cache(
        platform_target, platform_name, platform_version
    ) is None:
      _remove_isolated_entry(platform_target)

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


def stable_project_path(project: Path) -> Path:
  """Make a project path absolute without resolving symlink spelling."""
  return Path(os.path.abspath(os.fspath(project)))


def build_once(
    project: Path,
    environment_name: str,
    epoch: int,
    version: str,
    inherited_environment: dict[str, str],
) -> BuildResult:
  # Preserve the lexical /tmp spelling. PlatformIO's ESP-IDF builder derives
  # object paths from both lexical and real paths; resolving /tmp to
  # /private/tmp can make two distinct source identities collide. The
  # framework prefix-map patch handles both spellings in compiler/debug flags.
  project = stable_project_path(project)
  child_environment = dict(inherited_environment)
  child_environment.update(
      {
          "FURBLE_VERSION": version,
          "FURBLE_TEST": "0",
          "SOURCE_DATE_EPOCH": str(epoch),
      }
  )
  child_environment = prepare_platformio_core(
      project.parent, child_environment, project=project
  )
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
      "esp32-s3-headless",
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
