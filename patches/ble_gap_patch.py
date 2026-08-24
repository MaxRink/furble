"""Apply the NimBLE workaround without accepting a rejected patch."""

from __future__ import annotations

import fcntl
import hashlib
from pathlib import Path
import subprocess
import tempfile


PATCH_MARKER = "Peer LTK not found,  initiate pairing"


def _lock_path(source: Path) -> Path:
  digest = hashlib.sha256(str(source.resolve()).encode("utf-8")).hexdigest()[:16]
  return Path(tempfile.gettempdir()) / f"furble-framework-{digest}.lock"


def apply_patch(
    source: Path, patch_file: Path, marker: str = PATCH_MARKER
) -> str:
  """Apply *patch_file* to *source*, returning ``applied`` or ``present``.

  PlatformIO framework packages are shared across projects. The lock prevents
  two PlatformIO processes from patching the same package concurrently. A
  rejected hunk is always an error, rather than leaving a ``.rej`` file that
  could silently produce an unpatched firmware build.
  """
  source = source.resolve()
  patch_file = patch_file.resolve()
  if marker in source.read_text(encoding="utf-8"):
    return "present"

  lock_file = _lock_path(source)
  lock_file.parent.mkdir(parents=True, exist_ok=True)
  with lock_file.open("w", encoding="utf-8") as stream:
    fcntl.flock(stream.fileno(), fcntl.LOCK_EX)
    if marker in source.read_text(encoding="utf-8"):
      return "present"
    result = subprocess.run(
        [
            "patch",
            "--batch",
            "--forward",
            "--reject-file=-",
            "-p1",
            "-i",
            str(patch_file),
            str(source),
        ],
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0 or marker not in source.read_text(
        encoding="utf-8"
    ):
      output = (result.stdout + result.stderr).strip()
      raise RuntimeError(
          f"failed to apply {patch_file} to {source}"
          + (f": {output}" if output else "")
      )
    return "applied"
