"""Add realpath variants to ESP-IDF's reproducible-build prefix maps."""

from __future__ import annotations

import fcntl
import hashlib
from pathlib import Path
import tempfile


MARKER = "# Furble realpath prefix maps\n"
MACRO_ANCHOR = '        list(APPEND compile_options "-fmacro-prefix-map=${CMAKE_SOURCE_DIR}=.")\n'
DEBUG_ANCHOR = '        list(APPEND compile_options "-fdebug-prefix-map=${PROJECT_DIR}=/IDF_PROJECT")\n'


def patch_text(source: str) -> str:
  """Return patched text, failing closed on unexpected IDF source drift."""
  if MARKER in source:
    return source
  if source.count("    idf_build_get_property(build_components BUILD_COMPONENTS)\n") != 1:
    raise RuntimeError("ESP-IDF prefix-map build-components anchor changed")
  if source.count(MACRO_ANCHOR) != 1:
    raise RuntimeError("ESP-IDF prefix-map macro anchor changed")
  if source.count(DEBUG_ANCHOR) != 1:
    raise RuntimeError("ESP-IDF prefix-map debug anchor changed")

  source = source.replace(
      "    idf_build_get_property(build_components BUILD_COMPONENTS)\n",
      "    idf_build_get_property(build_components BUILD_COMPONENTS)\n\n"
      + MARKER
      + '    get_filename_component(furble_project_real_dir "${PROJECT_DIR}" REALPATH)\n'
      + '    get_filename_component(furble_source_real_dir "${CMAKE_SOURCE_DIR}" REALPATH)\n',
      1,
  )
  source = source.replace(
      MACRO_ANCHOR,
      MACRO_ANCHOR
      + '        list(APPEND compile_options "-fmacro-prefix-map=${furble_source_real_dir}=.")\n',
      1,
  )
  source = source.replace(
      DEBUG_ANCHOR,
      DEBUG_ANCHOR
      + '        list(APPEND compile_options "-fdebug-prefix-map=${furble_project_real_dir}=/IDF_PROJECT")\n',
      1,
  )
  if source.count(MARKER) != 1 or source.count("furble_project_real_dir") != 2:
    raise RuntimeError("ESP-IDF prefix-map patch verification failed")
  return source


def apply(path: Path) -> str:
  """Patch a shared framework file under the same lock as the BLE patch."""
  path = path.resolve()
  lock_name = hashlib.sha256(str(path).encode("utf-8")).hexdigest()[:16]
  lock_path = Path(tempfile.gettempdir()) / f"furble-framework-{lock_name}.lock"
  with lock_path.open("w", encoding="utf-8") as lock:
    fcntl.flock(lock.fileno(), fcntl.LOCK_EX)
    original = path.read_text(encoding="utf-8")
    patched = patch_text(original)
    if patched == original:
      return "present"
    path.write_text(patched, encoding="utf-8")
    return "applied"
