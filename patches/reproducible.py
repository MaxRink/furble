"""Make PlatformIO firmware builds use a deterministic build timestamp.

ESP-IDF supplies the path and metadata normalization when
CONFIG_APP_REPRODUCIBLE_BUILD is enabled. The application still owns the
About page's build string, so this script supplies that string from the same
SOURCE_DATE_EPOCH used by the compiler.
"""

from datetime import datetime, timezone
import os
from pathlib import Path
import subprocess

Import("env")


def commit_epoch(project_dir: Path) -> int:
  result = subprocess.run(
      ["git", "-C", str(project_dir), "show", "-s", "--format=%ct", "HEAD"],
      check=True,
      capture_output=True,
      text=True,
  )
  value = result.stdout.strip()
  if not value.isdigit():
    raise RuntimeError("git did not provide a numeric commit timestamp")
  return int(value)


project_dir = Path(env.subst("$PROJECT_DIR")).resolve()
raw_epoch = os.environ.get("SOURCE_DATE_EPOCH")
epoch = int(raw_epoch) if raw_epoch else commit_epoch(project_dir)
if epoch < 0:
  raise RuntimeError("SOURCE_DATE_EPOCH must not be negative")

build_time = datetime.fromtimestamp(epoch, timezone.utc)
# Keep macro values free of whitespace. PlatformIO renders CPPDEFINES through
# a shell command line, where spaces inside a quoted macro value are otherwise
# split into stray compiler inputs (for example ``24``, ``2026``, ``UTC``).
date_text = build_time.strftime("%Y-%m-%d")
time_text = build_time.strftime("%H:%M:%SZ")

# SCons passes ENV to every compiler, linker, and ESP-IDF CMake subprocess.
# Keep the value in the process environment too because PlatformIO helpers
# may snapshot os.environ before constructing a child environment.
os.environ["SOURCE_DATE_EPOCH"] = str(epoch)
child_environment = dict(env.get("ENV", {}))
child_environment["SOURCE_DATE_EPOCH"] = str(epoch)
env.Replace(ENV=child_environment)

# CONFIG_APP_REPRODUCIBLE_BUILD supplies ESP-IDF's complete prefix map. These
# two defines are the application-level replacement for __DATE__ and __TIME__.
env.Append(
    CPPDEFINES=[
        ("FURBLE_BUILD_DATE", '"{}"'.format(date_text)),
        ("FURBLE_BUILD_TIME", '"{}"'.format(time_text)),
    ]
)
