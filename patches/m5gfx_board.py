"""Pin the M5GFX board on single-board images (post: lib_deps exist by now)."""

from pathlib import Path
import sys

Import("env")

patches_dir = Path(env.subst("$PROJECT_DIR")) / "patches"
sys.path.insert(0, str(patches_dir))
from ble_gap_patch import apply_patch  # noqa: E402

source = Path(env.subst("$PROJECT_LIBDEPS_DIR")) / env.subst("$PIOENV") / "M5GFX" / "src" / "M5GFX.cpp"
apply_patch(source, patches_dir / "m5gfx_board.patch", marker="furble: a single-board image pins")
