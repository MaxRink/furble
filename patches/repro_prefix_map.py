"""Add realpath variants to ESP-IDF's reproducible-build prefix maps."""

from __future__ import annotations

import fcntl
import hashlib
import os
from pathlib import Path
import stat
import tempfile


MARKER = "# Furble realpath prefix maps\n"
MACRO_ANCHOR = '        list(APPEND compile_options "-fmacro-prefix-map=${CMAKE_SOURCE_DIR}=.")\n'
DEBUG_ANCHOR = '        list(APPEND compile_options "-fdebug-prefix-map=${PROJECT_DIR}=/IDF_PROJECT")\n'
REALPATH_DECLARATIONS = (
    '    get_filename_component(furble_project_real_dir "${PROJECT_DIR}" REALPATH)\n'
    '    get_filename_component(furble_source_real_dir "${CMAKE_SOURCE_DIR}" REALPATH)\n'
    '    get_filename_component(furble_idf_real_dir "${idf_path}" REALPATH)\n'
    '    get_filename_component(furble_build_real_dir "${BUILD_DIR}" REALPATH)\n'
)
REALPATH_MACRO = '        list(APPEND compile_options "-fmacro-prefix-map=${furble_source_real_dir}=.")\n'
REALPATH_MACRO_IDF = '        list(APPEND compile_options "-fmacro-prefix-map=${furble_idf_real_dir}=/IDF")\n'
REALPATH_DEBUG = (
    '        list(APPEND compile_options "-fdebug-prefix-map=${furble_idf_real_dir}=/IDF")\n'
    '        list(APPEND compile_options "-fdebug-prefix-map=${furble_project_real_dir}=/IDF_PROJECT")\n'
    '        list(APPEND compile_options "-fdebug-prefix-map=${furble_build_real_dir}=/IDF_BUILD")\n'
)
COMPONENT_ANCHOR = '            idf_component_get_property(component_dir ${component_name} COMPONENT_DIR)\n'
REALPATH_COMPONENT = '            get_filename_component(furble_component_real_dir "${component_dir}" REALPATH)\n'
REALPATH_COMPONENT_MAP = '            list(APPEND compile_options "-fdebug-prefix-map=${furble_component_real_dir}=${substituted_path}")\n'


def _validate_patched(source: str) -> None:
  if source.count(MARKER) != 1:
    raise RuntimeError("ESP-IDF prefix-map patch marker is invalid")
  if source.count(REALPATH_DECLARATIONS) != 1:
    raise RuntimeError("ESP-IDF prefix-map realpath declarations are invalid")
  if source.count(REALPATH_MACRO) != 1:
    raise RuntimeError("ESP-IDF prefix-map realpath macro mapping is invalid")
  if source.count(REALPATH_DEBUG) != 1:
    raise RuntimeError("ESP-IDF prefix-map realpath debug mapping is invalid")
  if source.count(REALPATH_MACRO_IDF) != 1:
    raise RuntimeError("ESP-IDF prefix-map realpath IDF macro mapping is invalid")
  if source.count(REALPATH_COMPONENT) != 1:
    raise RuntimeError("ESP-IDF prefix-map component realpath mapping is invalid")
  if source.count(REALPATH_COMPONENT_MAP) != 1:
    raise RuntimeError("ESP-IDF prefix-map component realpath flag is invalid")


def patch_text(source: str) -> str:
  """Return patched text, failing closed on unexpected IDF source drift."""
  if MARKER in source:
    _validate_patched(source)
    return source
  if source.count("    idf_build_get_property(build_components BUILD_COMPONENTS)\n") != 1:
    raise RuntimeError("ESP-IDF prefix-map build-components anchor changed")
  if source.count(MACRO_ANCHOR) != 1:
    raise RuntimeError("ESP-IDF prefix-map macro anchor changed")
  if source.count(DEBUG_ANCHOR) != 1:
    raise RuntimeError("ESP-IDF prefix-map debug anchor changed")
  if source.count(COMPONENT_ANCHOR) != 1:
    raise RuntimeError("ESP-IDF prefix-map component anchor changed")

  source = source.replace(
      "    idf_build_get_property(build_components BUILD_COMPONENTS)\n",
      "    idf_build_get_property(build_components BUILD_COMPONENTS)\n\n"
      + MARKER
      + REALPATH_DECLARATIONS,
      1,
  )
  source = source.replace(
      MACRO_ANCHOR,
      MACRO_ANCHOR
      + REALPATH_MACRO
      + REALPATH_MACRO_IDF,
      1,
  )
  source = source.replace(
      DEBUG_ANCHOR,
      DEBUG_ANCHOR
      + REALPATH_DEBUG,
      1,
  )
  source = source.replace(
      COMPONENT_ANCHOR,
      COMPONENT_ANCHOR
      + REALPATH_COMPONENT,
      1,
  )
  source = source.replace(
      '            list(APPEND compile_options "-fdebug-prefix-map=${component_dir}=${substituted_path}")\n',
      '            list(APPEND compile_options "-fdebug-prefix-map=${component_dir}=${substituted_path}")\n'
      + REALPATH_COMPONENT_MAP,
      1,
  )
  _validate_patched(source)
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
    mode = stat.S_IMODE(path.stat().st_mode)
    temporary_name = None
    try:
      with tempfile.NamedTemporaryFile(
          mode="w", encoding="utf-8", dir=path.parent,
          prefix=f".{path.name}.", delete=False
      ) as temporary:
        temporary_name = temporary.name
        temporary.write(patched)
        temporary.flush()
        os.fsync(temporary.fileno())
      os.chmod(temporary_name, mode)
      os.replace(temporary_name, path)
      temporary_name = None
      try:
        directory_fd = os.open(path.parent, os.O_RDONLY)
      except OSError:
        directory_fd = None
      if directory_fd is not None:
        try:
          # Some supported filesystems do not implement directory fsync. The
          # replacement is already durable as far as this operation can tell;
          # do not report failure after successfully replacing the target.
          try:
            os.fsync(directory_fd)
          except OSError:
            pass
        finally:
          os.close(directory_fd)
    finally:
      if temporary_name is not None:
        try:
          os.unlink(temporary_name)
        except FileNotFoundError:
          pass
    return "applied"
