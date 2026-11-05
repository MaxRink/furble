"""Regression tests for the input/API/memory fuzz gate."""

from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path
import sys
import tempfile
import unittest

from tools.check_fuzz import load_seeds, resolve_mqtt_owner_stack


ROOT = Path(__file__).resolve().parents[1]

MEMORY_SPEC = spec_from_file_location(
    "memory_fuzz", ROOT / "tests" / "fuzz" / "memory_fuzz.py"
)
assert MEMORY_SPEC is not None and MEMORY_SPEC.loader is not None
MEMORY_FUZZ = module_from_spec(MEMORY_SPEC)
sys.modules[MEMORY_SPEC.name] = MEMORY_FUZZ
MEMORY_SPEC.loader.exec_module(MEMORY_FUZZ)


class MqttOwnerStackTest(unittest.TestCase):
  def test_numeric_stack_is_read(self):
    with tempfile.TemporaryDirectory() as directory:
      root = Path(directory)
      (root / "src").mkdir()
      (root / "src" / "FurbleMQTT.cpp").write_text(
          'xTaskCreate(taskEntry, "mqtt", 10240, this, 2, &m_Task);\n',
          encoding="utf-8",
      )
      value, detail = resolve_mqtt_owner_stack(root)
      self.assertEqual((value, detail), (10240, "10240"))

  def test_named_stack_is_resolved_from_the_header(self):
    with tempfile.TemporaryDirectory() as directory:
      root = Path(directory)
      (root / "src").mkdir()
      (root / "include").mkdir()
      (root / "src" / "FurbleMQTT.cpp").write_text(
          'xTaskCreate(taskEntry, "mqtt", MQTTMemory::OWNER_STACK_BYTES, this, 2, &m_Task);\n',
          encoding="utf-8",
      )
      (root / "include" / "FurbleMQTTMemory.h").write_text(
          "inline constexpr size_t OWNER_STACK_BYTES = 10240;\n",
          encoding="utf-8",
      )
      value, detail = resolve_mqtt_owner_stack(root)
      self.assertEqual(value, 10240)
      self.assertEqual(detail, "MQTTMemory::OWNER_STACK_BYTES")

  def test_unresolved_expression_fails_closed(self):
    with tempfile.TemporaryDirectory() as directory:
      root = Path(directory)
      (root / "src").mkdir()
      (root / "src" / "FurbleMQTT.cpp").write_text(
          'xTaskCreate(taskEntry, "mqtt", UNKNOWN_STACK, this, 2, &m_Task);\n',
          encoding="utf-8",
      )
      value, detail = resolve_mqtt_owner_stack(root)
      self.assertIsNone(value)
      self.assertIn("UNKNOWN_STACK", detail)


class SeedCorpusTest(unittest.TestCase):
  def test_seed_corpus_has_every_area_and_known_finding(self):
    seeds = load_seeds(ROOT / "tests" / "fuzz" / "seeds.json")
    for area in ("ui", "webui", "companion", "mqtt", "memory"):
      self.assertIn("smoke", seeds[area])
      self.assertIn("full", seeds[area])
    owners = {entry["id"]: entry["owner"] for entry in seeds["known_findings"]}
    self.assertEqual(owners["mqtt-owner-stack"], "fix/mqtt-task-stack")
    self.assertEqual(owners["ha-discovery-oom"], "perf/mqtt-ha-memory")

  def test_memory_script_is_deterministic_and_sized(self):
    first, first_expectation = MEMORY_FUZZ.build_script(7, mqtt=True)
    second, second_expectation = MEMORY_FUZZ.build_script(7, mqtt=True)
    self.assertEqual(first, second)
    self.assertEqual(first_expectation, second_expectation)
    self.assertIn("seed heap_internal_free", first)
    self.assertIn("assert ui.page main", first)
    self.assertIn("assert platform.watchdog armed", first)


if __name__ == "__main__":
  unittest.main()
