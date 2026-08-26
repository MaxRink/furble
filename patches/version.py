"""PlatformIO adapter for the firmware build version."""

import os
from pathlib import Path

from build_version import resolve_version

Import("env")

version = resolve_version(os.environ["FURBLE_VERSION"], Path(env.subst("$PROJECT_DIR")))
env.Append(CPPDEFINES=[("FURBLE_VERSION", env.StringifyMacro(version))])
