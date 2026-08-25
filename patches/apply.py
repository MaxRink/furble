from pathlib import Path
import sys

Import("env")

patches_dir = Path(env.subst("$PROJECT_DIR")) / "patches"
sys.path.insert(0, str(patches_dir))
from ble_gap_patch import apply_patch  # noqa: E402
from repro_prefix_map import apply as apply_reproducible_prefix_map  # noqa: E402

framework_dir = env.PioPlatform().get_package_dir("framework-espidf")


def apply_ble_gap_patch():
  src_path = (
      Path(framework_dir)
      / "components"
      / "bt"
      / "host"
      / "nimble"
      / "nimble"
      / "nimble"
      / "host"
      / "src"
      / "ble_gap.c"
  )
  patch_path = patches_dir / "ble_gap.patch"
  apply_patch(src_path, patch_path)


# patch NimBLE
apply_ble_gap_patch()
apply_reproducible_prefix_map(
    Path(framework_dir) / "tools" / "cmake" / "prefix_map.cmake"
)
