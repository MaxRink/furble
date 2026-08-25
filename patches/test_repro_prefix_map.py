#!/usr/bin/env python3
"""Regression tests for ESP-IDF realpath prefix-map patching."""

from pathlib import Path
import tempfile
import unittest
from unittest import mock

import repro_prefix_map
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
        list(APPEND compile_options "-fdebug-prefix-map=${BUILD_DIR}=/IDF_BUILD")
        foreach(component_name ${build_components})
            idf_component_get_property(component_dir ${component_name} COMPONENT_DIR)
            string(TOUPPER ${component_name} component_name_uppercase)
            set(substituted_path "/COMPONENT_${component_name_uppercase}_DIR")
            list(APPEND compile_options "-fdebug-prefix-map=${component_dir}=${substituted_path}")
        endforeach()
    endif()
endfunction()
'''


class ReproPrefixMapTest(unittest.TestCase):
  def test_adds_realpath_maps_and_is_idempotent(self):
    patched = patch_text(PREFIX_MAP)
    self.assertIn("furble_project_real_dir", patched)
    self.assertIn("furble_source_real_dir", patched)
    self.assertEqual(patch_text(patched), patched)

  def test_marker_alone_or_incomplete_patch_fails_closed(self):
    corrupt = PREFIX_MAP.replace(
        '    idf_build_get_property(build_components BUILD_COMPONENTS)\n',
        '    idf_build_get_property(build_components BUILD_COMPONENTS)\n' + MARKER,
    )
    with self.assertRaises(RuntimeError):
      patch_text(corrupt)

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

  def test_atomic_failures_leave_original_and_clean_temporary(self):
    def assert_failure_is_atomic(failure):
      with failure:
        with tempfile.TemporaryDirectory() as directory:
          path = Path(directory) / "prefix_map.cmake"
          path.write_text(PREFIX_MAP, encoding="utf-8")
          with self.assertRaises(OSError):
            apply(path)
          self.assertEqual(path.read_text(encoding="utf-8"), PREFIX_MAP)
          self.assertEqual(list(path.parent.glob(f".{path.name}.*")), [])

    with self.subTest(failure="write"):
      fake_temporary = mock.MagicMock()
      fake_temporary.name = "/nonexistent/.prefix_map.cmake.write-failure"
      fake_temporary.__enter__.return_value = fake_temporary
      fake_temporary.write.side_effect = OSError("write failed")
      assert_failure_is_atomic(
          mock.patch(
              "repro_prefix_map.tempfile.NamedTemporaryFile",
              return_value=fake_temporary,
          )
      )

    for operation in ("fsync", "chmod", "replace"):
      with self.subTest(failure=operation):
        assert_failure_is_atomic(
            mock.patch(
                f"repro_prefix_map.os.{operation}",
                side_effect=OSError(f"{operation} failed"),
            )
        )

  def test_directory_fsync_unsupported_does_not_report_after_replace(self):
    with tempfile.TemporaryDirectory() as directory:
      path = Path(directory) / "prefix_map.cmake"
      path.write_text(PREFIX_MAP, encoding="utf-8")
      calls = 0
      def fsync_file_then_reject_directory(fd):
        nonlocal calls
        calls += 1
        if calls == 2:
          raise OSError("EINVAL")
      with mock.patch("repro_prefix_map.os.fsync", side_effect=fsync_file_then_reject_directory), \
          mock.patch("repro_prefix_map.os.close"):
        self.assertEqual(apply(path), "applied")
      self.assertIn(MARKER, path.read_text(encoding="utf-8"))


if __name__ == "__main__":
  unittest.main()
