#!/usr/bin/env python3
"""Regression tests for ESP-IDF realpath prefix-map patching."""

from pathlib import Path
import tempfile
import unittest

from repro_prefix_map import MARKER, apply, patch_text


PREFIX_MAP = '''function(__generate_prefix_map compile_options_var)
    set(compile_options)
    idf_build_get_property(idf_path IDF_PATH)
    idf_build_get_property(build_components BUILD_COMPONENTS)
    if(CONFIG_COMPILER_HIDE_PATHS_MACROS)
        list(APPEND compile_options "-fmacro-prefix-map=${CMAKE_SOURCE_DIR}=.")
        list(APPEND compile_options "-fmacro-prefix-map=${idf_path}=/IDF")
    endif()
    if(CONFIG_APP_REPRODUCIBLE_BUILD)
        list(APPEND compile_options "-fdebug-prefix-map=${idf_path}=/IDF")
        list(APPEND compile_options "-fdebug-prefix-map=${PROJECT_DIR}=/IDF_PROJECT")
    endif()
endfunction()
'''


class ReproPrefixMapTest(unittest.TestCase):
  def test_adds_realpath_maps_and_is_idempotent(self):
    patched = patch_text(PREFIX_MAP)
    self.assertIn("furble_project_real_dir", patched)
    self.assertIn("furble_source_real_dir", patched)
    self.assertEqual(patch_text(patched), patched)

  def test_missing_anchor_fails_closed_without_partial_text(self):
    broken = PREFIX_MAP.replace("CMAKE_SOURCE_DIR", "SOURCE_DIR")
    with self.assertRaises(RuntimeError):
      patch_text(broken)
    self.assertNotIn(MARKER, broken)

  def test_debug_anchor_is_required(self):
    broken = PREFIX_MAP.replace("${PROJECT_DIR}=/IDF_PROJECT", "${PROJECT}=x")
    with self.assertRaises(RuntimeError):
      patch_text(broken)

  def test_apply_does_not_partially_write_on_anchor_failure(self):
    with tempfile.TemporaryDirectory() as directory:
      path = Path(directory) / "prefix_map.cmake"
      original = PREFIX_MAP.replace("${PROJECT_DIR}=/IDF_PROJECT", "${PROJECT}=x")
      path.write_text(original, encoding="utf-8")
      with self.assertRaises(RuntimeError):
        apply(path)
      self.assertEqual(path.read_text(encoding="utf-8"), original)


if __name__ == "__main__":
  unittest.main()
