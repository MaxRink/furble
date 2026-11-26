"""Pin the M5GFX board on single-board images (post: lib_deps exist by now).

`platformio.ini` passes `-DM5GFX_BOARD=board_t::board_X` on an image that runs
on exactly one M5 board. Two patches, applied separately so a libdeps tree
patched by an older revision picks up the newer half instead of reporting the
whole pin as already applied:

- `m5gfx_board.patch` assigns the pin at the top of `M5GFX::autodetect`, so
  only that board's arm can match and the compiler drops every other board's
  probe, panel and touch driver.
- `m5gfx_board_noprobe.patch` returns the display-less StampS3 family from the
  ESP32-S3 section before the first probe. M5GFX 0.2.27 has no arm for those
  boards, and the fall-through to `board_unknown` ran the whole probe set five
  times.

`tools/check_m5gfx_pinning.py` reads the patched source after a build and
fails when a pin stops naming a board in the branch that env compiles.
"""

from pathlib import Path
import sys

Import("env")

patches_dir = Path(env.subst("$PROJECT_DIR")) / "patches"
sys.path.insert(0, str(patches_dir))
from ble_gap_patch import apply_patch  # noqa: E402

source = Path(env.subst("$PROJECT_LIBDEPS_DIR")) / env.subst("$PIOENV") / "M5GFX" / "src" / "M5GFX.cpp"
apply_patch(source, patches_dir / "m5gfx_board.patch", marker="furble: a single-board image pins")
apply_patch(
    source,
    patches_dir / "m5gfx_board_noprobe.patch",
    marker="furble: a pinned board that has no arm",
)
