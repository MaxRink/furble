#!/usr/bin/env python3
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "furble_reproducible_build", ROOT / "tools" / "reproducible_build.py"
)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


class ReproduciblePathTest(unittest.TestCase):
  def test_preserves_tmp_symlink_spelling(self):
    with tempfile.TemporaryDirectory() as directory:
      real = Path(directory) / "real"
      real.mkdir()
      alias = Path(directory) / "alias"
      alias.symlink_to(real, target_is_directory=True)
      lexical = alias / "source"
      self.assertEqual(MODULE.stable_project_path(lexical), lexical)
      self.assertNotEqual(MODULE.stable_project_path(lexical), lexical.resolve())

  @staticmethod
  def _metadata(directory, name, version, *, platform=False):
    directory.mkdir(parents=True, exist_ok=True)
    payload = {"name": name, "version": version}
    (directory / ".piopm").write_text(json.dumps(payload), encoding="utf-8")
    metadata = "platform.json" if platform else "package.json"
    (directory / metadata).write_text(json.dumps(payload), encoding="utf-8")

  @staticmethod
  def _project(directory):
    lock = directory / "tools" / "reproducible-build.lock"
    lock.parent.mkdir(parents=True, exist_ok=True)
    lock.write_text("platform=espressif32@6.13.0\nframework=3.50503.0\n", encoding="utf-8")
    return directory

  def test_stale_platform_and_packages_are_not_linked(self):
    with tempfile.TemporaryDirectory() as directory:
      root = Path(directory)
      base = root / "base"
      (base / "platforms").mkdir(parents=True)
      (base / "packages").mkdir(parents=True)
      self._metadata(base / "platforms" / "espressif32", "espressif32", "6.12.0", platform=True)
      self._metadata(base / "packages" / "framework-espidf", "framework-espidf", "3.50402.0")
      project = self._project(root / "project")

      environment = MODULE.prepare_platformio_core(
          root / "build", {"PLATFORMIO_CORE_DIR": str(base)}, project=project
      )
      self.assertFalse((Path(environment["PLATFORMIO_PLATFORMS_DIR"]) / "espressif32").exists())
      self.assertFalse((Path(environment["PLATFORMIO_PACKAGES_DIR"]) / "framework-espidf").exists())

  def test_stale_isolated_platform_link_is_removed(self):
    with tempfile.TemporaryDirectory() as directory:
      root = Path(directory)
      base = root / "base"
      stale = base / "platforms" / "espressif32"
      self._metadata(stale, "espressif32", "6.12.0", platform=True)
      project = self._project(root / "project")
      build = root / "build"
      isolated = build / "pio-core"
      (isolated / "packages").mkdir(parents=True)
      (isolated / "platforms").mkdir(parents=True)
      (isolated / "platforms" / "espressif32").symlink_to(stale, target_is_directory=True)

      environment = MODULE.prepare_platformio_core(
          build, {"PLATFORMIO_CORE_DIR": str(base)}, project=project
      )
      self.assertFalse(
          (Path(environment["PLATFORMIO_PLATFORMS_DIR"]) / "espressif32").is_symlink()
      )

  def test_stale_isolated_directories_are_replaced(self):
    with tempfile.TemporaryDirectory() as directory:
      root = Path(directory)
      base = root / "base"
      (base / "platforms").mkdir(parents=True)
      (base / "packages").mkdir(parents=True)
      platform = base / "platforms" / "espressif32"
      self._metadata(platform, "espressif32", "6.13.0", platform=True)
      (platform / "platform.json").write_text(
          json.dumps({
              "name": "espressif32",
              "version": "6.13.0",
              "packages": {"framework-espidf": {"version": "~3.50503.0"}},
          }),
          encoding="utf-8",
      )
      framework = base / "packages" / "framework-espidf"
      self._metadata(framework, "framework-espidf", "3.50503.0")
      project = self._project(root / "project")
      build = root / "build"
      isolated = build / "pio-core"
      (isolated / "platforms" / "espressif32").mkdir(parents=True)
      (isolated / "platforms" / "espressif32" / "stale").write_text("bad")
      (isolated / "packages" / "framework-espidf").mkdir(parents=True)
      (isolated / "packages" / "framework-espidf" / "stale").write_text("bad")

      environment = MODULE.prepare_platformio_core(
          build, {"PLATFORMIO_CORE_DIR": str(base)}, project=project
      )
      platform_target = Path(environment["PLATFORMIO_PLATFORMS_DIR"]) / "espressif32"
      framework_target = Path(environment["PLATFORMIO_PACKAGES_DIR"]) / "framework-espidf"
      self.assertTrue(platform_target.is_symlink())
      self.assertEqual(platform_target.resolve(), platform.resolve())
      self.assertFalse(framework_target.is_symlink())
      self.assertTrue((framework_target / "package.json").is_file())
      self.assertFalse((framework_target / "stale").exists())

  def test_exact_platform_reuses_only_matching_dependencies(self):
    with tempfile.TemporaryDirectory() as directory:
      root = Path(directory)
      base = root / "base"
      (base / "platforms").mkdir(parents=True)
      (base / "packages").mkdir(parents=True)
      platform = base / "platforms" / "espressif32"
      self._metadata(platform, "espressif32", "6.13.0", platform=True)
      manifest = {"name": "espressif32", "version": "6.13.0", "packages": {
          "framework-espidf": {"version": "~3.50503.0"},
          "tool-esptoolpy": {"version": "~2.41100.0"},
      }}
      (platform / "platform.json").write_text(json.dumps(manifest), encoding="utf-8")
      framework = base / "packages" / "framework-espidf"
      self._metadata(framework, "framework-espidf", "3.50503.0")
      shared_file = framework / "shared.txt"
      shared_file.write_text("shared", encoding="utf-8")
      self._metadata(base / "packages" / "tool-esptoolpy", "tool-esptoolpy", "2.40000.0")
      project = self._project(root / "project")
      build = root / "build"
      isolated_framework = build / "pio-core" / "packages" / "framework-espidf"
      isolated_framework.parent.mkdir(parents=True, exist_ok=True)
      (build / "pio-core" / "platforms").mkdir(parents=True, exist_ok=True)
      isolated_framework.symlink_to(framework, target_is_directory=True)

      environment = MODULE.prepare_platformio_core(
          build, {"PLATFORMIO_CORE_DIR": str(base)}, project=project
      )
      platforms = Path(environment["PLATFORMIO_PLATFORMS_DIR"])
      packages = Path(environment["PLATFORMIO_PACKAGES_DIR"])
      self.assertTrue((platforms / "espressif32").is_symlink())
      self.assertTrue((packages / "framework-espidf").is_dir())
      self.assertFalse((packages / "framework-espidf").is_symlink())
      (packages / "framework-espidf" / "shared.txt").write_text("private", encoding="utf-8")
      self.assertEqual(shared_file.read_text(encoding="utf-8"), "shared")
      self.assertFalse((packages / "tool-esptoolpy").exists())

  def test_traversal_in_locked_platform_name_is_rejected(self):
    with tempfile.TemporaryDirectory() as directory:
      root = Path(directory)
      base = root / "base"
      (base / "platforms").mkdir(parents=True)
      (base / "packages").mkdir(parents=True)
      sentinel = root / "sentinel"
      sentinel.write_text("unchanged", encoding="utf-8")
      project = root / "project"
      lock = project / "tools" / "reproducible-build.lock"
      lock.parent.mkdir(parents=True)
      lock.write_text("platform=../../sentinel@6.13.0\n", encoding="utf-8")

      with self.assertRaisesRegex(RuntimeError, "unsafe locked platform name"):
        MODULE.prepare_platformio_core(
            root / "build", {"PLATFORMIO_CORE_DIR": str(base)}, project=project
        )
      self.assertEqual(sentinel.read_text(encoding="utf-8"), "unchanged")

  def test_traversal_in_cached_package_name_is_rejected(self):
    with tempfile.TemporaryDirectory() as directory:
      root = Path(directory)
      base = root / "base"
      platform = base / "platforms" / "espressif32"
      self._metadata(platform, "espressif32", "6.13.0", platform=True)
      (platform / "platform.json").write_text(
          json.dumps({
              "name": "espressif32",
              "version": "6.13.0",
              "packages": {"../../sentinel": {"version": "1.0.0"}},
          }),
          encoding="utf-8",
      )
      (base / "packages").mkdir(parents=True)
      sentinel = root / "sentinel"
      sentinel.write_text("unchanged", encoding="utf-8")
      project = self._project(root / "project")

      with self.assertRaisesRegex(RuntimeError, "unsafe platform package name"):
        MODULE.prepare_platformio_core(
            root / "build", {"PLATFORMIO_CORE_DIR": str(base)}, project=project
        )
      self.assertEqual(sentinel.read_text(encoding="utf-8"), "unchanged")

  def test_symlinked_isolated_containers_are_rejected(self):
    for container in ("packages", "platforms"):
      with self.subTest(container=container), tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        base = root / "base"
        (base / "platforms").mkdir(parents=True)
        (base / "packages").mkdir(parents=True)
        project = self._project(root / "project")
        outside = root / "outside"
        outside.mkdir()
        sentinel = outside / "sentinel"
        sentinel.write_text("unchanged", encoding="utf-8")
        isolated = root / "build" / "pio-core"
        isolated.mkdir(parents=True)
        other = "platforms" if container == "packages" else "packages"
        (isolated / other).mkdir()
        (isolated / container).symlink_to(outside, target_is_directory=True)

        with self.assertRaisesRegex(RuntimeError, f"isolated {container} directory"):
          MODULE.prepare_platformio_core(
              root / "build", {"PLATFORMIO_CORE_DIR": str(base)}, project=project
          )
        self.assertEqual(sentinel.read_text(encoding="utf-8"), "unchanged")

  def test_symlinked_isolated_core_is_rejected(self):
    with tempfile.TemporaryDirectory() as directory:
      root = Path(directory)
      base = root / "base"
      (base / "platforms").mkdir(parents=True)
      (base / "packages").mkdir(parents=True)
      project = self._project(root / "project")
      outside = root / "outside"
      outside.mkdir()
      sentinel = outside / "sentinel"
      sentinel.write_text("unchanged", encoding="utf-8")
      build = root / "build"
      build.mkdir()
      (build / "pio-core").symlink_to(outside, target_is_directory=True)

      with self.assertRaisesRegex(RuntimeError, "PlatformIO core must not be a symlink"):
        MODULE.prepare_platformio_core(
            build, {"PLATFORMIO_CORE_DIR": str(base)}, project=project
        )
      self.assertEqual(sentinel.read_text(encoding="utf-8"), "unchanged")

  def test_partial_platform_metadata_falls_back_to_installer(self):
    with tempfile.TemporaryDirectory() as directory:
      root = Path(directory)
      base = root / "base"
      platform = base / "platforms" / "espressif32"
      platform.mkdir(parents=True)
      (platform / ".piopm").write_text(
          json.dumps({"name": "espressif32", "version": "6.13.0"}),
          encoding="utf-8",
      )
      project = self._project(root / "project")
      environment = MODULE.prepare_platformio_core(
          root / "build", {"PLATFORMIO_CORE_DIR": str(base)}, project=project
      )
      self.assertFalse(
          (Path(environment["PLATFORMIO_PLATFORMS_DIR"]) / "espressif32").exists()
      )


if __name__ == "__main__":
  unittest.main()
