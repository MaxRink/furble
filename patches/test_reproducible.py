#!/usr/bin/env python3
"""Regression tests for deterministic PlatformIO timestamp definitions."""

from pathlib import Path
import os
import unittest


class FakeEnv:
  def __init__(self, project_dir: Path):
    self.project_dir = project_dir
    self.environment = {}
    self.defines = []

  def subst(self, value):
    self.requested_value = value
    return str(self.project_dir)

  def get(self, name, default=None):
    if name == "ENV":
      return self.environment
    return default

  def Replace(self, **values):
    self.environment = values["ENV"]

  def Append(self, **values):
    self.defines.extend(values["CPPDEFINES"])

  def StringifyMacro(self, value):
    return '\\"{}\\"'.format(value.replace('"', '\\\\"'))


class ReproducibleTimestampTest(unittest.TestCase):
  def test_timestamp_defines_are_deterministic_shell_safe_iso8601(self):
    epoch_name = "SOURCE_DATE_EPOCH"
    previous_epoch = os.environ.get(epoch_name)
    os.environ[epoch_name] = "1787604929"
    fake_env = FakeEnv(Path("/unused/project"))
    try:
      source = Path(__file__).with_name("reproducible.py").read_text(
          encoding="utf-8"
      )
      namespace = {"Import": lambda _name: None, "env": fake_env}
      exec(compile(source, "patches/reproducible.py", "exec"), namespace)
    finally:
      if previous_epoch is None:
        del os.environ[epoch_name]
      else:
        os.environ[epoch_name] = previous_epoch

    self.assertEqual(fake_env.environment[epoch_name], "1787604929")
    self.assertEqual(
        fake_env.defines,
        [
            ("FURBLE_BUILD_DATE", '\\"2026-08-24\\"'),
            ("FURBLE_BUILD_TIME", '\\"20:55:29Z\\"'),
        ],
    )
    for _name, value in fake_env.defines:
      self.assertFalse(any(character.isspace() for character in value))


if __name__ == "__main__":
  unittest.main()
